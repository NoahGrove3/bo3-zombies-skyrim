#include "BO3Z.h"

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

		std::string Where(DWORD64 a_pc)
		{
			HMODULE mod = nullptr;
			if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(a_pc), &mod) && mod) {
				wchar_t path[MAX_PATH]{};
				::GetModuleFileNameW(mod, path, MAX_PATH);
				std::wstring w(path);
				const auto   slash = w.find_last_of(L"\\/");
				w = slash == std::wstring::npos ? w : w.substr(slash + 1);
				return fmt::format("{}+{:X}", std::string(w.begin(), w.end()), a_pc - reinterpret_cast<DWORD64>(mod));
			}
			return fmt::format("{:X}", a_pc);
		}

		void Dump(int a_stuckSec)
		{
			HANDLE th = ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_main.load());
			if (!th) {
				return;
			}
			std::array<DWORD64, 48> pcs{};
			int                     n = 0;
			if (::SuspendThread(th) != static_cast<DWORD>(-1)) {
				// (nothing that allocates or takes a lock while it is stopped: it may hold them)
				CONTEXT ctx{};
				ctx.ContextFlags = CONTEXT_FULL;
				if (::GetThreadContext(th, &ctx)) {
					for (; n < static_cast<int>(pcs.size()) && ctx.Rip; ++n) {
						pcs[n] = ctx.Rip;
						DWORD64 base = 0;
						auto*   fe = ::RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
						if (!fe) {
							ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
							ctx.Rsp += 8;
						} else {
							PVOID   data = nullptr;
							DWORD64 frame = 0;
							::RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fe, &ctx, &data, &frame, nullptr);
						}
					}
				}
				::ResumeThread(th);
			}
			::CloseHandle(th);
			logger::warn("watchdog: no frame drawn for {} s - the main thread is at:", a_stuckSec);
			for (int i = 0; i < n; ++i) {
				logger::warn("watchdog:   {:2} {}", i, Where(pcs[i]));
			}
		}

		DWORD WINAPI Watch(LPVOID)
		{
			long long dumpedAt = 0;
			for (;;) {
				::Sleep(1000);
				const long long beat = g_beat.load();
				if (!beat || !g_main.load()) {
					continue;
				}
				const long long stuck = NowMs() - beat;
				// once at 12 s without a frame, again every 30 s while it stays stuck (to see whether it moves)
				if (stuck > 12000 && (dumpedAt != beat ? true : false)) {
					Dump(static_cast<int>(stuck / 1000));
					dumpedAt = beat;
					::Sleep(30000);
					dumpedAt = 0;
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

	void Start()
	{
		static bool started = false;
		if (!started) {
			started = true;
			::CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr);
		}
	}
}
