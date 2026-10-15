#include "BO3Z.h"
#include <psapi.h>

// The watchdog: if the game stops drawing frames (a hang - after a load, at the main menu), it writes where the main
// thread is stuck to the log (module + offset per frame of its stack), so a hang can be traced to the mod behind it.
namespace bo3z::Watchdog
{
	namespace
	{
		std::atomic<long long> g_beat{ 0 };   // the last frame drawn (steady clock, ms)
		std::atomic<DWORD>     g_main{ 0 };   // the thread that draws them

		long long NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// the modules loaded (taken while the game is healthy: listing them needs the loader's lock, which a stuck
		// thread may be holding)
		struct Mod { DWORD64 base, end; std::string name; };
		std::vector<Mod> g_mods;

		void SnapModules()
		{
			HMODULE mods[1024];
			DWORD   need = 0;
			HANDLE  proc = ::GetCurrentProcess();
			if (!::K32EnumProcessModules(proc, mods, sizeof(mods), &need)) {
				return;
			}
			std::vector<Mod> out;
			for (DWORD i = 0; i < std::min<DWORD>(need / sizeof(HMODULE), 1024); ++i) {
				MODULEINFO mi{};
				wchar_t    nm[MAX_PATH]{};
				if (::K32GetModuleInformation(proc, mods[i], &mi, sizeof(mi)) && ::K32GetModuleBaseNameW(proc, mods[i], nm, MAX_PATH)) {
					std::wstring w(nm);
					out.push_back({ reinterpret_cast<DWORD64>(mi.lpBaseOfDll), reinterpret_cast<DWORD64>(mi.lpBaseOfDll) + mi.SizeOfImage, std::string(w.begin(), w.end()) });
				}
			}
			g_mods = std::move(out);
		}

		const Mod* ModOf(DWORD64 a_pc)
		{
			for (const auto& m : g_mods) {
				if (a_pc >= m.base && a_pc < m.end) {
					return &m;
				}
			}
			return nullptr;
		}

		// where the main thread is: its instruction pointer, then every value on the top of its stack that points into
		// a module's code (return addresses, mostly - a rough stack). The stack is only copied while it is stopped.
		void Dump(int a_stuckSec)
		{
			HANDLE th = ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_main.load());
			if (!th) {
				return;
			}
			static std::array<DWORD64, 4096> stack{};  // 32 KB of it
			DWORD64 rip = 0, rsp = 0;
			SIZE_T  got = 0;
			if (::SuspendThread(th) != static_cast<DWORD>(-1)) {
				CONTEXT ctx{};
				ctx.ContextFlags = CONTEXT_CONTROL;
				if (::GetThreadContext(th, &ctx)) {
					rip = ctx.Rip, rsp = ctx.Rsp;
					::ReadProcessMemory(::GetCurrentProcess(), reinterpret_cast<LPCVOID>(rsp), stack.data(), sizeof(stack), &got);
				}
				::ResumeThread(th);
			}
			::CloseHandle(th);
			auto where = [](DWORD64 a) {
				const Mod* m = ModOf(a);
				return m ? fmt::format("{}+{:X}", m->name, a - m->base) : fmt::format("{:X}", a);
			};
			logger::warn("watchdog: no frame drawn for {} s - the main thread is at {}; on its stack:", a_stuckSec, where(rip));
			int shown = 0;
			for (std::size_t i = 0; i < got / 8 && shown < 40; ++i) {
				if (const Mod* m = ModOf(stack[i]); m && stack[i] - m->base > 0x1000) {
					logger::warn("watchdog:   [{:4X}] {}", i * 8, where(stack[i]));
					++shown;
				}
			}
		}

		DWORD WINAPI Watch(LPVOID)
		{
			long long snapAt = 0;
			for (;;) {
				::Sleep(1000);
				const long long beat = g_beat.load();
				if (!beat || !g_main.load()) {
					continue;
				}
				const long long now = NowMs(), stuck = now - beat;
				if (stuck < 2000 && now - snapAt > 15000) {  // healthy: refresh the module list now and then
					SnapModules();
					snapAt = now;
				}
				// once at 12 s without a frame, again every 30 s while it stays stuck (to see whether it moves)
				if (stuck > 12000) {
					Dump(static_cast<int>(stuck / 1000));
					for (int i = 0; i < 30 && g_beat.load() == beat; ++i) {
						::Sleep(1000);
					}
				}
			}
		}
	}

	void Beat()
	{
		if (!g_main.load()) {
			g_main = ::GetCurrentThreadId();
		}
		g_beat = NowMs();
	}

	void Start(bool a_onMainThread)
	{
		static bool started = false;
		if (!started) {
			started = true;
			// (called on the game's main thread at load: watched from now on, so a hang before the first frame shows too)
			if (a_onMainThread) {
				g_main = ::GetCurrentThreadId();
				g_beat = NowMs();
			}
			SnapModules();
			::CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr);
		}
	}
}
