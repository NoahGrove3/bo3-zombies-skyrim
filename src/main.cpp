#include "BO3Z.h"

// Loaded two ways, same file:
//  - as BO3Zombies.asi by Ultimate ASI Loader (what Melty installs; no Script Extender on 1.7.104):
//    DllMain starts a thread and nothing else; the thread waits for the game's UI, then watches menus.
//  - as an SKSE plugin (BO3Zombies.dll in Data/SKSE/Plugins) for players who run SKSE.
namespace
{
	std::atomic_bool g_booted{ false };

	void SetupLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		auto path = *dir / "BO3Zombies.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	DWORD WINAPI BootThread(LPVOID)
	{
		SetupLog();
		logger::info("BO3 Zombies {} loading as .asi", bo3z::kVersion);
		for (int i = 0; i < 6000 && !RE::UI::GetSingleton(); ++i) {  // up to 10 minutes
			::Sleep(100);
		}
		if (!RE::UI::GetSingleton()) {
			logger::error("game UI never came up; not starting");
			return 0;
		}
		logger::info("game UI is up; Skyrim runtime {}", REL::Module::get().version().string());
		bo3z::Events::InstallMenuWatch();
		return 0;
	}

	bool IsAsi(HMODULE a_module)
	{
		wchar_t path[MAX_PATH]{};
		const auto n = ::GetModuleFileNameW(a_module, path, MAX_PATH);
		if (n < 4) {
			return false;
		}
		std::wstring p(path, n);
		std::transform(p.begin(), p.end(), p.begin(), ::towlower);
		return p.ends_with(L".asi");
	}
}

BOOL APIENTRY DllMain(HMODULE a_module, DWORD a_reason, LPVOID)
{
	if (a_reason == DLL_PROCESS_ATTACH && IsAsi(a_module) && !g_booted.exchange(true)) {
		::DisableThreadLibraryCalls(a_module);
		if (auto h = ::CreateThread(nullptr, 0, BootThread, nullptr, 0, nullptr)) {
			::CloseHandle(h);
		}
	}
	return TRUE;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	if (g_booted.exchange(true)) {
		return false;  // the .asi copy is already running
	}
	SKSE::Init(a_skse, { .log = false });
	SetupLog();
	logger::info("BO3 Zombies {} loading as SKSE plugin", bo3z::kVersion);
	bo3z::Events::InstallMenuWatch();
	return true;
}
