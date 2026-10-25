#include "BO3Z.h"
#include <psapi.h>
#include <tlhelp32.h>

// The watchdog: if the game stops drawing frames (a hang - after a load, at the main menu), it writes where the main
// thread is stuck to the log (module + offset per frame of its stack), so a hang can be traced to the mod behind it.
namespace bo3z::Watchdog
{
	namespace
	{
		std::atomic<long long> g_beat{ 0 };   // the last frame drawn (steady clock, ms)
		std::atomic<DWORD>     g_main{ 0 };   // the thread that draws them
		std::atomic_bool       g_drawn{ false };  // a frame has been drawn at all
		std::atomic<long long> g_tickDue{ 0 };    // the world is loaded: the player must update by then (ms)

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

		// (Noah: the game froze loading back after a death - the main thread waiting on something) every other thread too:
		// where each one is and the code on its stack, to see which one the main thread waits for. Each is copied while
		// it is stopped, and written out only after it runs again (writing needs the heap a stopped thread may hold).
		void DumpAll()
		{
			struct One { DWORD id; DWORD64 rip; std::vector<DWORD64> st; };
			std::vector<DWORD> ids;
			ids.reserve(256);
			HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
			if (snap == INVALID_HANDLE_VALUE) return;
			THREADENTRY32 te{ sizeof(te) };
			const DWORD pid = ::GetCurrentProcessId(), self = ::GetCurrentThreadId();
			for (BOOL ok = ::Thread32First(snap, &te); ok; ok = ::Thread32Next(snap, &te)) {
				if (te.th32OwnerProcessID == pid && te.th32ThreadID != self && te.th32ThreadID != g_main.load()) ids.push_back(te.th32ThreadID);
			}
			::CloseHandle(snap);
			std::vector<One> all;
			all.reserve(ids.size());
			static std::array<DWORD64, 1024> buf{};  // 8 KB of each
			for (DWORD id : ids) {
				HANDLE th = ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
				if (!th) continue;
				DWORD64 rip = 0;
				SIZE_T  got = 0;
				if (::SuspendThread(th) != static_cast<DWORD>(-1)) {
					CONTEXT ctx{};
					ctx.ContextFlags = CONTEXT_CONTROL;
					if (::GetThreadContext(th, &ctx)) {
						rip = ctx.Rip;
						::ReadProcessMemory(::GetCurrentProcess(), reinterpret_cast<LPCVOID>(ctx.Rsp), buf.data(), sizeof(buf), &got);
					}
					::ResumeThread(th);
				}
				::CloseHandle(th);
				One o{ id, rip, {} };
				for (std::size_t i = 0; i < got / 8 && o.st.size() < 14; ++i) {
					if (const Mod* m = ModOf(buf[i]); m && buf[i] - m->base > 0x1000) o.st.push_back(buf[i]);
				}
				all.push_back(std::move(o));
			}
			auto where = [](DWORD64 a) {
				const Mod* m = ModOf(a);
				return m ? fmt::format("{}+{:X}", m->name, a - m->base) : fmt::format("{:X}", a);
			};
			logger::warn("watchdog: the other {} threads:", all.size());
			for (const auto& o : all) {
				std::string line;
				for (auto a : o.st) line += " < " + where(a);
				logger::warn("watchdog:   thread {} at {}{}", o.id, where(o.rip), line);
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
				if ((stuck < 2000 || !g_drawn) && now - snapAt > (g_drawn ? 15000 : 4000)) {  // refresh the module list now and then
					SnapModules();
					snapAt = now;
				}
				// a load that finished but the world never starts (no player update since): where the main thread is
				if (const long long due = g_tickDue.load(); due && now > due) {
					logger::warn("watchdog: the world loaded {} s ago but has not started", (now - due) / 1000 + 15);
					Dump(static_cast<int>(stuck / 1000));
					g_tickDue = now + 30000;
				}
				// once at 12 s without a frame, again every 30 s while it stays stuck (to see whether it moves)
				if (stuck > 12000) {
					Dump(static_cast<int>(stuck / 1000));
					if (stuck > 40000) {
						static long long toldBeat = 0;
						if (toldBeat != beat) toldBeat = beat, DumpAll();
					}
					for (int i = 0; i < 30 && g_beat.load() == beat; ++i) {
						::Sleep(1000);
					}
				}
			}
		}
	}

	namespace
	{
		// a crash: where it happened and what is on that thread's stack (module + offset), written before the game dies
		LONG CALLBACK OnCrash(EXCEPTION_POINTERS* a_info)
		{
			static std::atomic_int told{ 0 };
			if (!a_info || !a_info->ExceptionRecord || a_info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || told++ >= 3) {
				return EXCEPTION_CONTINUE_SEARCH;
			}
			const auto* ctx = a_info->ContextRecord;
			auto where = [](DWORD64 a) {
				const Mod* m = ModOf(a);
				return m ? fmt::format("{}+{:X}", m->name, a - m->base) : fmt::format("{:X}", a);
			};
			logger::critical("crash: access violation at {} ({} address {:X}) on thread {}{}", where(ctx->Rip),
				a_info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading", a_info->ExceptionRecord->ExceptionInformation[1], ::GetCurrentThreadId(),
				::GetCurrentThreadId() == g_main.load() ? " (the game's main thread)" : "");
			logger::critical("crash: rax {:X} rbx {:X} rcx {:X} rdx {:X} rsi {:X} rdi {:X} r8 {:X} r9 {:X}", ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx, ctx->Rsi, ctx->Rdi, ctx->R8, ctx->R9);
			const auto* sp = reinterpret_cast<const DWORD64*>(ctx->Rsp);
			int         shown = 0;
			for (int i = 0; i < 2048 && shown < 40; ++i) {
				DWORD64 v = 0;
				SIZE_T  got = 0;
				if (!::ReadProcessMemory(::GetCurrentProcess(), sp + i, &v, sizeof(v), &got) || got != sizeof(v)) {
					break;
				}
				if (const Mod* m = ModOf(v); m && v - m->base > 0x1000) {
					logger::critical("crash:   [{:4X}] {}", i * 8, where(v));
					++shown;
				}
			}
			spdlog::default_logger()->flush();
			return EXCEPTION_CONTINUE_SEARCH;
		}
	}

	void WorldLoaded() { g_tickDue = NowMs() + 15000; }
	void PlayerTicked() { g_tickDue = 0; }

	void Beat()
	{
		g_drawn = true;
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
			::AddVectoredExceptionHandler(1, OnCrash);
			::CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr);
		}
	}
}
