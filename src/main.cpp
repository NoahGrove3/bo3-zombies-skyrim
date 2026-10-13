#include "BO3Z.h"

// Loaded two ways, same file:
//  - as UndyingNights.asi by Ultimate ASI Loader (what Melty installs; no Script Extender on 1.7.104):
//    DllMain starts a thread and nothing else; the thread waits for the game's UI, then watches menus.
//  - as an SKSE plugin (UndyingNights.dll in Data/SKSE/Plugins) for players who run SKSE.
// Only one copy may run in a game: a process-wide mutex says who came first. Older installs named the files
// BO3Zombies.asi / .dll: if one of those is loaded, it runs this time and its files are renamed out of the way so
// the next start is ours alone.
namespace
{
	std::atomic_bool g_booted{ false };

	void SetupLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		auto path = *dir / "UndyingNights.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	// The game window must fit the screen it is on: SkyrimPrefs.ini keeps the size of whatever monitor the game
	// was first set up on, and a 2560x1600 window on a 2560x1440 screen hides the bottom of every menu and HUD.
	// At start, if the saved size is bigger than the main screen, it is set to the screen's own size (the game
	// reads the file a moment later; if it was quicker, the next start is right).
	void FitResolution()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		const auto prefs = (dir->parent_path() / "SkyrimPrefs.ini").wstring();
		DEVMODEW   dm{};
		dm.dmSize = sizeof(dm);
		if (!::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) || dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) {
			return;
		}
		const UINT w = ::GetPrivateProfileIntW(L"Display", L"iSize W", 0, prefs.c_str());
		const UINT h = ::GetPrivateProfileIntW(L"Display", L"iSize H", 0, prefs.c_str());
		const UINT full = ::GetPrivateProfileIntW(L"Display", L"bFull Screen", 0, prefs.c_str());
		if (w == 0 || h == 0 || full || (w <= dm.dmPelsWidth && h <= dm.dmPelsHeight)) {
			logger::info("display: window {}x{} fits the main screen {}x{}", w, h, dm.dmPelsWidth, dm.dmPelsHeight);
			return;
		}
		const auto W = std::to_wstring(dm.dmPelsWidth), H = std::to_wstring(dm.dmPelsHeight);
		::WritePrivateProfileStringW(L"Display", L"iSize W", W.c_str(), prefs.c_str());
		::WritePrivateProfileStringW(L"Display", L"iSize H", H.c_str(), prefs.c_str());
		logger::info("display: window {}x{} was bigger than the main screen; set to {}x{}", w, h, dm.dmPelsWidth, dm.dmPelsHeight);
	}

	DWORD WINAPI BootThread(LPVOID)
	{
		SetupLog();
		FitResolution();
		logger::info("Undying Nights {} loading as .asi", bo3z::kVersion);
		for (int i = 0; i < 6000 && !RE::UI::GetSingleton(); ++i) {  // up to 10 minutes
			if (i % 5 == 0) {
				bo3z::Audio::SetMenuMusic();  // as soon as the game's settings exist, before the main menu plays
			}
			::Sleep(100);
		}
		bo3z::Audio::SetMenuMusic();
		if (!RE::UI::GetSingleton()) {
			logger::error("game UI never came up; not starting");
			return 0;
		}
		logger::info("game UI is up; Skyrim runtime {}", REL::Module::get().version().string());
		bo3z::Events::InstallMenuWatch();
		bo3z::Net::PatchTogether();
		return 0;
	}

	// The first copy in this game takes the mutex; any later copy stays quiet. An old BO3Zombies copy already
	// loaded wins this once, and its files are moved aside (*.old) for next time.
	bool FirstCopy(HMODULE a_module)
	{
		const std::wstring name = L"Local\\UndyingNights.Plugin." + std::to_wstring(::GetCurrentProcessId());
		::CreateMutexW(nullptr, FALSE, name.c_str());  // (kept for the life of the game)
		if (::GetLastError() == ERROR_ALREADY_EXISTS) {
			return false;
		}
		for (const wchar_t* old : { L"BO3Zombies.asi", L"BO3Zombies.dll" }) {
			if (HMODULE h = ::GetModuleHandleW(old); h && h != a_module) {
				wchar_t path[MAX_PATH]{};
				if (const auto n = ::GetModuleFileNameW(h, path, MAX_PATH); n > 0) {
					const std::wstring p(path, n);
					::MoveFileExW(p.c_str(), (p + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
				}
				return false;
			}
		}
		return true;
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
	if (a_reason == DLL_PROCESS_ATTACH && IsAsi(a_module) && !g_booted.exchange(true) && FirstCopy(a_module)) {
		::DisableThreadLibraryCalls(a_module);
		bo3z::PluginList::Ensure();  // before the game reads Plugins.txt
		if (auto h = ::CreateThread(nullptr, 0, BootThread, nullptr, 0, nullptr)) {
			::CloseHandle(h);
		}
	}
	return TRUE;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	if (g_booted.exchange(true) || !FirstCopy(nullptr)) {
		return false;  // another copy (the .asi, or an old BO3Zombies build) is already running
	}
	SKSE::Init(a_skse, { .log = false });
	bo3z::PluginList::Ensure();
	SetupLog();
	logger::info("Undying Nights {} loading as SKSE plugin", bo3z::kVersion);
	bo3z::Save::InstallCoSave();
	bo3z::Events::InstallMenuWatch();
	bo3z::Net::PatchTogether();  // (Skyrim Together loads us through SKSE: its menu must learn to follow the party here too)
	return true;
}
