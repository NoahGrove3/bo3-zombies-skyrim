#include "BO3Z.h"

// The player's own settings for The Curse of Alduin. They live where Skyrim's Mod Configuration menu (SkyUI + MCM Helper)
// keeps them: Data/MCM/Settings/CurseOfAlduin.ini over the defaults in Data/MCM/Config/CurseOfAlduin/settings.ini
// (the MCM page itself: Data/MCM/Config/CurseOfAlduin/config.json, built by tools/mcm.py). The main menu's own
// Settings rows (field of view) write the same file, so both always agree. The file is read again whenever it changes
// (the MCM writes it as you close the menu).
//   [General]    fFovFirst, fFovThird (degrees), bShelter (townsfolk go indoors at night), fCorpseSeconds
//   [Difficulty] bFollowSkyrim (Skyrim's difficulty slider applies to the night too), fUndeadHealth, fUndeadDamage
//   [Keys]       iShrineRoll, iShrineReforge, iShrineBless, iShrineShout, iShrineLeave (DirectInput scancodes)
namespace bo3z::Settings
{
	namespace
	{
		float g_fovFirst = 100.0f, g_fovThird = 85.0f, g_corpse = 10.0f, g_hp = 1.0f, g_dmg = 1.0f;
		bool  g_shelter = true, g_follow = true;
		int   g_keys[kKeys] = { 0x02, 0x03, 0x04, 0x05, 0x0E };
		constexpr const wchar_t* kKeyIds[kKeys] = { L"iShrineRoll", L"iShrineReforge", L"iShrineBless", L"iShrineShout", L"iShrineLeave" };
		bool  g_loaded = false;
		std::filesystem::file_time_type g_stamp{};

		std::filesystem::path Data()
		{
			wchar_t exe[MAX_PATH]{};
			::GetModuleFileNameW(nullptr, exe, MAX_PATH);
			return std::filesystem::path(exe).parent_path() / "Data";
		}
		std::filesystem::path UserIni() { return Data() / "MCM" / "Settings" / "CurseOfAlduin.ini"; }
		std::filesystem::path DefaultIni() { return Data() / "MCM" / "Config" / "CurseOfAlduin" / "settings.ini"; }

		// The ini is read and written by hand: MCM Helper saves it as UTF-8 with a byte-order mark, which Windows'
		// own profile functions don't understand (they then miss the first section and every value in it).
		std::string Lower(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}
		std::string Trim(std::string_view a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string_view::npos) {
				return {};
			}
			return std::string(a_s.substr(b, a_s.find_last_not_of(" \t\r\n") - b + 1));
		}
		std::vector<std::string> Lines(const std::filesystem::path& a_file, bool* a_bom = nullptr)
		{
			std::ifstream in(a_file, std::ios::binary);
			std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			const bool bom = all.size() >= 3 && static_cast<unsigned char>(all[0]) == 0xEF && static_cast<unsigned char>(all[1]) == 0xBB && static_cast<unsigned char>(all[2]) == 0xBF;
			if (a_bom) {
				*a_bom = bom;
			}
			if (bom) {
				all.erase(0, 3);
			}
			std::vector<std::string> out;
			std::size_t at = 0;
			while (at <= all.size() && !all.empty()) {
				const auto nl = all.find('\n', at);
				std::string line = all.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				out.push_back(std::move(line));
				if (nl == std::string::npos) {
					break;
				}
				at = nl + 1;
			}
			return out;
		}
		std::string Narrow(const wchar_t* a_w)
		{
			std::string s;
			for (; *a_w; ++a_w) {
				s.push_back(static_cast<char>(*a_w));
			}
			return s;
		}
		bool Find(const std::filesystem::path& a_file, const std::string& a_section, const std::string& a_key, std::string& a_out)
		{
			std::string sec;
			for (const auto& raw : Lines(a_file)) {
				const auto line = Trim(raw);
				if (line.empty() || line[0] == ';' || line[0] == '#') {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					sec = Lower(Trim(std::string_view(line).substr(1, line.size() - 2)));
					continue;
				}
				const auto eq = line.find('=');
				if (eq != std::string::npos && sec == a_section && Lower(Trim(std::string_view(line).substr(0, eq))) == a_key) {
					a_out = Trim(std::string_view(line).substr(eq + 1));
					return true;
				}
			}
			return false;
		}

		float Read(const wchar_t* a_section, const wchar_t* a_key, float a_def)
		{
			const auto sec = Lower(Narrow(a_section)), key = Lower(Narrow(a_key));
			for (const auto& f : { UserIni(), DefaultIni() }) {
				std::string v;
				if (Find(f, sec, key, v) && !v.empty()) {
					try {
						return std::stof(v);
					} catch (...) {
					}
				}
			}
			return a_def;
		}

		void Write(const wchar_t* a_section, const wchar_t* a_key, float a_v, bool a_int)
		{
			std::error_code ec;
			std::filesystem::create_directories(UserIni().parent_path(), ec);
			const auto sec = Narrow(a_section), key = Narrow(a_key);
			const std::string val = a_int ? std::to_string(std::lround(a_v)) : fmt::format("{:.6f}", a_v);
			bool bom = false;
			auto lines = std::filesystem::exists(UserIni(), ec) ? Lines(UserIni(), &bom) : std::vector<std::string>{};
			std::string cur;
			int  secEnd = -1, found = -1;
			bool inSec = false;
			for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
				const auto line = Trim(lines[i]);
				if (!line.empty() && line.front() == '[' && line.back() == ']') {
					inSec = Lower(Trim(std::string_view(line).substr(1, line.size() - 2))) == Lower(sec);
					if (inSec) {
						secEnd = i + 1;
					}
					continue;
				}
				if (inSec) {
					if (!line.empty()) {
						secEnd = i + 1;
					}
					const auto eq = line.find('=');
					if (eq != std::string::npos && Lower(Trim(std::string_view(line).substr(0, eq))) == Lower(key)) {
						found = i;
					}
				}
			}
			const std::string entry = key + " = " + val;
			if (found >= 0) {
				lines[found] = entry;
			} else if (secEnd >= 0) {
				lines.insert(lines.begin() + secEnd, entry);
			} else {
				if (!lines.empty() && !Trim(lines.back()).empty()) {
					lines.emplace_back();
				}
				lines.push_back("[" + sec + "]");
				lines.push_back(entry);
			}
			std::ofstream out(UserIni(), std::ios::binary | std::ios::trunc);
			if (bom) {
				out << "\xEF\xBB\xBF";
			}
			for (std::size_t i = 0; i < lines.size(); ++i) {
				out << lines[i];
				if (i + 1 < lines.size() || !lines[i].empty()) {
					out << "\r\n";
				}
			}
			out.close();
			g_stamp = std::filesystem::last_write_time(UserIni(), ec);
		}

		void Load()
		{
			g_loaded = true;
			// (an older install kept the field of view next to SkyrimSE.exe: carried over once)
			wchar_t exe[MAX_PATH]{};
			::GetModuleFileNameW(nullptr, exe, MAX_PATH);
			const auto old = std::filesystem::path(exe).parent_path() / "UndyingNights.ini";  // (the name it had then)
			std::error_code ec;
			if (std::filesystem::exists(old, ec) && !std::filesystem::exists(UserIni(), ec)) {
				wchar_t buf[32]{};
				for (auto [from, to] : { std::pair{ L"FovFirst", L"fFovFirst" }, std::pair{ L"FovThird", L"fFovThird" } }) {
					if (::GetPrivateProfileStringW(L"Settings", from, L"", buf, 32, old.wstring().c_str()) && buf[0]) {
						Write(L"General", to, std::wcstof(buf, nullptr), false);
					}
				}
			}
			g_fovFirst = std::clamp(Read(L"General", L"fFovFirst", 100.0f), kFovMin, kFovMax);
			g_fovThird = std::clamp(Read(L"General", L"fFovThird", 85.0f), kFovMin, kFovMax);
			g_shelter = Read(L"General", L"bShelter", 1.0f) != 0.0f;
			g_corpse = std::clamp(Read(L"General", L"fCorpseSeconds", 10.0f), 3.0f, 60.0f);
			g_follow = Read(L"Difficulty", L"bFollowSkyrim", 1.0f) != 0.0f;
			g_hp = std::clamp(Read(L"Difficulty", L"fUndeadHealth", 1.0f), 0.25f, 4.0f);
			g_dmg = std::clamp(Read(L"Difficulty", L"fUndeadDamage", 1.0f), 0.25f, 4.0f);
			static constexpr int kDefault[kKeys] = { 0x02, 0x03, 0x04, 0x05, 0x0E };
			for (int i = 0; i < kKeys; ++i) {
				const int k = static_cast<int>(Read(L"Keys", kKeyIds[i], static_cast<float>(kDefault[i])));
				g_keys[i] = k > 0 && k < 0x100 ? k : kDefault[i];
			}
			g_stamp = std::filesystem::last_write_time(UserIni(), ec);
			logger::info("settings: fov {:.0f}/{:.0f}, shelter {}, corpses {:.0f} s, difficulty follows Skyrim {}, undead health x{:.2f} damage x{:.2f}",
				g_fovFirst, g_fovThird, g_shelter, g_corpse, g_follow, g_hp, g_dmg);
		}

		void Ensure()
		{
			if (!g_loaded) {
				Load();
			}
		}

		// Skyrim's difficulty (Settings > Gameplay): how hard you hit and how hard you are hit, the game's own steps
		int GameDifficulty()
		{
			if (auto* ini = RE::INIPrefSettingCollection::GetSingleton()) {
				if (auto* s = ini->GetSetting("iDifficulty:GamePlay")) {
					return std::clamp(static_cast<int>(s->GetInteger()), 0, 5);
				}
			}
			return 2;
		}
		constexpr float kDealt[6] = { 2.0f, 1.5f, 1.0f, 0.75f, 0.5f, 0.25f };
		constexpr float kTaken[6] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f };
	}

	float FovFirst() { Ensure(); return g_fovFirst; }
	float FovThird() { Ensure(); return g_fovThird; }
	bool  Shelter() { Ensure(); return g_shelter; }
	float CorpseSeconds() { Ensure(); return g_corpse; }
	int   Key(int a_action) { Ensure(); return a_action >= 0 && a_action < kKeys ? g_keys[a_action] : 0; }

	// the night's dead: their health against your hits, and their hits against you
	float UndeadHealthMult()
	{
		Ensure();
		return g_hp / (g_follow ? kDealt[GameDifficulty()] : 1.0f);
	}
	float UndeadDamageMult()
	{
		Ensure();
		return g_dmg * (g_follow ? kTaken[GameDifficulty()] : 1.0f);
	}

	void SetFov(bool a_first, float a_v)
	{
		Ensure();
		const float v = std::clamp(std::round(a_v), kFovMin, kFovMax);
		(a_first ? g_fovFirst : g_fovThird) = v;
		Write(L"General", a_first ? L"fFovFirst" : L"fFovThird", v, false);
	}

	std::string KeyName(int a_action)
	{
		const int k = Key(a_action);
		static const std::unordered_map<int, const char*> kNames{ { 0x0E, "Backspace" }, { 0x0F, "Tab" }, { 0x1C, "Enter" }, { 0x39, "Space" },
			{ 0x1D, "Ctrl" }, { 0x2A, "Shift" }, { 0x38, "Alt" }, { 0xC8, "Up" }, { 0xD0, "Down" }, { 0xCB, "Left" }, { 0xCD, "Right" },
			{ 0xD2, "Insert" }, { 0xD3, "Delete" }, { 0xC7, "Home" }, { 0xCF, "End" }, { 0xC9, "PgUp" }, { 0xD1, "PgDn" } };
		if (auto it = kNames.find(k); it != kNames.end()) {
			return it->second;
		}
		static constexpr const char* kRow = "??1234567890-=??QWERTYUIOP[]??ASDFGHJKL;'`?\\ZXCVBNM,./";
		if (k >= 0x02 && k < static_cast<int>(std::strlen(kRow)) && kRow[k] != '?') {
			return std::string(1, kRow[k]);
		}
		if (k >= 0x3B && k <= 0x44) {
			return "F" + std::to_string(k - 0x3A);
		}
		return fmt::format("Key {}", k);
	}

	// Mods with no menu of their own get their settings from the Other mods page: written into their files at start
	// (they read them as they load, so a change counts from the next launch). Only what the player set is written.
	void ApplyMods()
	{
		Ensure();
		std::error_code ec;
		if (!std::filesystem::exists(UserIni(), ec)) {
			return;
		}
		auto has = [](const wchar_t* a_sec, const wchar_t* a_key) {
			std::string v;
			return Find(UserIni(), Lower(Narrow(a_sec)), Lower(Narrow(a_key)), v) && !v.empty();
		};
		// QuickLoot RE: SKSE/Plugins/QuickLootRE/config.toml, "key = value  # comment" lines
		const auto toml = Data() / "SKSE" / "Plugins" / "QuickLootRE" / "config.toml";
		if (std::filesystem::exists(toml, ec)) {
			std::ifstream in(toml);
			std::vector<std::string> lines;
			for (std::string l; std::getline(in, l);) {
				lines.push_back(l);
			}
			in.close();
			const std::pair<const wchar_t*, const char*> keys[] = { { L"bCloseInCombat", "closeInCombat" }, { L"bCloseOnEmpty", "closeOnEmpty" },
				{ L"fScale", "scale" }, { L"fHorizontal", "horizontalAdjust" }, { L"fVertical", "verticalAdjust" } };
			bool changed = false;
			for (auto [ours, theirs] : keys) {
				if (!has(L"QuickLoot", ours)) {
					continue;
				}
				const float v = Read(L"QuickLoot", ours, 0.0f);
				const std::string val = ours[0] == L'b' ? (v != 0.0f ? "true" : "false") : fmt::format("{:+.2f}", v);
				for (auto& l : lines) {
					if (l.rfind(std::string(theirs) + " ", 0) == 0 || l.rfind(std::string(theirs) + "=", 0) == 0) {
						const auto hash = l.find('#');
						l = std::string(theirs) + " = " + (ours[1] == 'S' && ours[0] == L'f' ? fmt::format("{:.2f}", v) : val) + (hash != std::string::npos ? "\t" + l.substr(hash) : "");
						changed = true;
					}
				}
			}
			if (changed) {
				std::ofstream out(toml, std::ios::trunc);
				for (auto& l : lines) {
					out << l << "\n";
				}
				logger::info("settings: QuickLoot's config written");
			}
		}
		// Character Menu SE: Data/Character Menu SE.ini
		const auto cm = (Data() / "Character Menu SE.ini").wstring();
		if (has(L"CharacterMenu", L"iOpenMenu")) {
			::WritePrivateProfileStringW(L"General", L"iOpenMenuKeycode", std::to_wstring(static_cast<int>(Read(L"CharacterMenu", L"iOpenMenu", 49.0f))).c_str(), cm.c_str());
		}
		if (has(L"CharacterMenu", L"bBlur")) {
			::WritePrivateProfileStringW(L"General", L"iEnableBlur", Read(L"CharacterMenu", L"bBlur", 1.0f) != 0.0f ? L"1" : L"0", cm.c_str());
		}
	}

	void Tick()
	{
		static auto next = std::chrono::steady_clock::now();
		const auto  now = std::chrono::steady_clock::now();
		if (now < next) {
			return;
		}
		next = now + std::chrono::seconds(2);
		std::error_code ec;
		const auto t = std::filesystem::last_write_time(UserIni(), ec);
		if (!g_loaded || (!ec && t != g_stamp)) {
			Load();  // the Mod Configuration menu saved new values
		}
	}
}
