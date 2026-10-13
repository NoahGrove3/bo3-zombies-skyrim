#include "BO3Z.h"

// The player's own settings for Undying Nights (UndyingNights.ini next to SkyrimSE.exe, [Settings]), changed in the
// panel the System menu shows (Hud.cpp SettingsPanel):
//   FovFirst   the field of view in first person (degrees)
//   FovThird   the field of view in third person
namespace bo3z::Settings
{
	namespace
	{
		float g_fovFirst = 100.0f, g_fovThird = 85.0f;
		bool  g_loaded = false;

		std::filesystem::path Path()
		{
			wchar_t exe[MAX_PATH]{};
			::GetModuleFileNameW(nullptr, exe, MAX_PATH);
			return std::filesystem::path(exe).parent_path() / "UndyingNights.ini";
		}

		float Read(const wchar_t* a_key, float a_def)
		{
			wchar_t buf[32]{};
			::GetPrivateProfileStringW(L"Settings", a_key, L"", buf, 32, Path().wstring().c_str());
			if (!buf[0]) {
				return a_def;
			}
			try {
				return std::stof(std::wstring(buf));
			} catch (...) {
				return a_def;
			}
		}

		void Write(const wchar_t* a_key, float a_v)
		{
			const std::wstring v = std::to_wstring(static_cast<int>(std::round(a_v)));
			::WritePrivateProfileStringW(L"Settings", a_key, v.c_str(), Path().wstring().c_str());
		}

		void Ensure()
		{
			if (!g_loaded) {
				g_loaded = true;
				g_fovFirst = std::clamp(Read(L"FovFirst", g_fovFirst), kFovMin, kFovMax);
				g_fovThird = std::clamp(Read(L"FovThird", g_fovThird), kFovMin, kFovMax);
				logger::info("settings: field of view {:.0f} first person, {:.0f} third person", g_fovFirst, g_fovThird);
			}
		}
	}

	float FovFirst() { Ensure(); return g_fovFirst; }
	float FovThird() { Ensure(); return g_fovThird; }

	void SetFov(bool a_first, float a_v)
	{
		Ensure();
		const float v = std::clamp(std::round(a_v), kFovMin, kFovMax);
		(a_first ? g_fovFirst : g_fovThird) = v;
		Write(a_first ? L"FovFirst" : L"FovThird", v);
	}
}
