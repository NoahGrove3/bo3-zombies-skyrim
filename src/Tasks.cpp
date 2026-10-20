#include "BO3Z.h"

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
