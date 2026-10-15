#include "BO3Z.h"

// Co-op with Skyrim Together Reborn (first stage, no network of our own yet). CurseOfAlduin.ini next to SkyrimSE.exe:
//   [Coop]
//   Role=host     your game runs the nights: raises the horde, events, the boss; Skyrim Together shows it to everyone
//   Role=client   a friend's game: raises nothing itself; the host's undead count as the night's undead here
//                 (points, quests, power-up timers stay per player)
//   Role=solo     (default) as before
namespace bo3z::Coop
{
	namespace
	{
		Role Read()
		{
			// next to SkyrimSE.exe (Skyrim Together's launcher may start the game from its own folder)
			wchar_t exe[MAX_PATH]{};
			::GetModuleFileNameW(nullptr, exe, MAX_PATH);
			std::filesystem::path path = std::filesystem::path(exe).parent_path() / "CurseOfAlduin.ini";
			if (!std::filesystem::exists(path)) {
				path = std::filesystem::path(exe).parent_path() / "Data" / "CurseOfAlduin.ini";
			}
			std::ifstream f(path);
			std::string   line;
			Role          r = Role::Solo;
			while (std::getline(f, line)) {
				std::erase_if(line, [](char c) { return c == ' ' || c == '\t' || c == '\r'; });
				std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (line.starts_with("role=")) {
					const auto v = line.substr(5);
					r = v == "client" ? Role::Client : v == "host" ? Role::Host : Role::Solo;
				}
			}
			return r;
		}
	}

	Role Get()
	{
		static const Role r = [] {
			const Role v = Read();
			logger::info("coop: role {}", v == Role::Client ? "client" : v == Role::Host ? "host" : "solo");
			return v;
		}();
		return r;
	}
	bool Client() { return Get() == Role::Client || Net::IsClient(); }  // a Steam party joined from the menu counts too
	bool TogetherLoaded()
	{
		static const bool loaded = [] {
			auto* dh = RE::TESDataHandler::GetSingleton();
			const bool on = dh && dh->LookupModByName("SkyrimTogether.esp");
			logger::info("coop: Skyrim Together {}", on ? "loaded" : "not loaded");
			return on;
		}();
		return loaded;
	}
}
