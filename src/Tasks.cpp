#include "BO3Z.h"
#include <condition_variable>

// Work handed to the game's main thread. Loaded through SKSE, SKSE's task queue carries it; loaded as the .asi (no
// SKSE: its task interface is null - the audit's crash) it waits here and runs from the Present hook, which is on the
// main thread, menus open or not.
namespace bo3z
{
	namespace Tasks
	{
		namespace
		{
			std::mutex                         g_lock;
			std::vector<std::function<void()>> g_queue;
			void Queue(std::function<void()> a_task)
			{
				std::scoped_lock l(g_lock);
				g_queue.push_back(std::move(a_task));
			}
		}
		void Add(std::function<void()> a_task)
		{
			if (auto* ti = SKSE::GetTaskInterface()) {
				ti->AddTask(std::move(a_task));
			} else {
				Queue(std::move(a_task));
			}
		}
		void AddUI(std::function<void()> a_task)
		{
			if (auto* ti = SKSE::GetTaskInterface()) {
				ti->AddUITask(std::move(a_task));
			} else {
				Queue(std::move(a_task));
			}
		}
		// (the audit: a thread per death, per cast, per equip - dozens a second in a big wave, and one that failed to start
		// inside an event sink ended the game) one timer thread for all of them: each waits its time there, then runs on
		// the main thread through Add
		namespace
		{
			struct Timed { std::chrono::steady_clock::time_point at; std::function<void()> f; };
			std::mutex              g_timedLock;
			std::condition_variable g_timedCv;
			std::vector<Timed>      g_timed;
			void TimerLoop()
			{
				std::unique_lock l(g_timedLock);
				for (;;) {
					if (g_timed.empty()) { g_timedCv.wait(l); continue; }
					auto next = std::min_element(g_timed.begin(), g_timed.end(), [](const Timed& a, const Timed& b) { return a.at < b.at; });
					if (std::chrono::steady_clock::now() < next->at) { g_timedCv.wait_until(l, next->at); continue; }
					auto f = std::move(next->f);
					g_timed.erase(next);
					l.unlock();
					Add(std::move(f));
					l.lock();
				}
			}
		}
		void Later(float a_seconds, std::function<void()> a_task)
		{
			static std::once_flag once;
			static bool           started = false;
			std::call_once(once, [] {
				try {
					std::thread(TimerLoop).detach();
					started = true;
				} catch (...) {
				}
			});
			if (!started) {
				Add(std::move(a_task));  // (no timer thread: at once, on the main thread)
				return;
			}
			{
				std::unique_lock l(g_timedLock);
				// (never dropped: a "wake its AI" task lost left the dead frozen; never run early either - the AI-off, move, wake
				// steps must keep their gaps) past 2000 it is said, once in a while
				if (g_timed.size() > 2000) {
					static int told = 0;
					if (told++ % 500 == 0) logger::warn("tasks: {} timed tasks waiting", g_timed.size());
				}
				g_timed.push_back({ std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(std::max(0.0f, a_seconds) * 1000.0f)), std::move(a_task) });
			}
			g_timedCv.notify_one();
		}
		void Drain()
		{
			std::vector<std::function<void()>> run;
			{
				std::scoped_lock l(g_lock);
				run.swap(g_queue);
			}
			for (auto& t : run) {
				t();
			}
		}
	}
}
