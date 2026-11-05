#include "BO3Z.h"

// (Noah: the Curse synced with the main story) Alduin's return at Helgen starts the Curse and his fall ends it; each
// main-quest step opens the next part of it:
//   0 Unbound              no nights yet (Helgen plays, or is skipped)
//   1 Before the Storm     the first nights; Riverwood's altar (every other city's waits on its own quest: TrialsOpen)
//   2 Bleak Falls Barrow   (the Dragon Priests no longer wait for it: they rise once a city's trials are done)
//   3 Dragon Rising        the nights' elites; Whiterun's trials
//   4 The Way of the Voice  (the Greybeards know you: they will call when your path's secret is won)
//   5 Diplomatic Immunity / Season Unending
// (Noah: "Skyrim quests not requiring Sovngarde") the rest of the curse follows its own story, not the main quest's:
// the wonders (src/Eggs.cpp, src/Classes.cpp), the three dragons and the Echo of Alduin (src/Echo.cpp). Grimmvar,
// Alduin's Herald, hunts on the Fog Nights once the three dragons wake; the wonders' Legendary form comes with their fall.
// and the story's set pieces are never interrupted: no night begins while you're in one (the Watchtower's dragon,
// Dragonsreach's trap, the Throat of the World, the Peace Council, Sovngarde).
namespace bo3z::Story
{
	namespace
	{
		// the main quests by name (their EditorIDs start MQ): English names, read from the game
		RE::TESQuest* Quest(std::string_view a_name)
		{
			static std::unordered_map<std::string, RE::TESQuest*> cache;
			const std::string key(a_name);
			if (auto it = cache.find(key); it != cache.end()) return it->second;
			RE::TESQuest* found = nullptr;
			if (auto* dh = RE::TESDataHandler::GetSingleton()) {
				for (auto* q : dh->GetFormArray<RE::TESQuest>()) {
					if (!q || a_name != q->GetName()) continue;
					const std::string_view ed = q->GetFormEditorID();
					if (ed.starts_with("MQ") || !found) found = q;
					if (ed.starts_with("MQ")) break;
				}
			}
			cache[key] = found;
			return found;
		}
		bool Done(std::string_view a_name)
		{
			auto* q = Quest(a_name);
			return q && (q->IsCompleted() || (!q->IsRunning() && q->GetCurrentStageID() > 0));
		}
		bool Running(std::string_view a_name)
		{
			auto* q = Quest(a_name);
			return q && q->IsRunning() && !q->IsCompleted();
		}
		bool InPlace(RE::PlayerCharacter* a_p, std::string_view a_name)
		{
			for (auto* loc = a_p ? a_p->GetCurrentLocation() : nullptr; loc; loc = loc->parentLoc) {
				if (const char* n = loc->GetName(); n && std::string_view(n).find(a_name) != std::string_view::npos) return true;
			}
			if (auto* ws = a_p ? a_p->GetWorldspace() : nullptr; ws) {
				if (const char* n = ws->GetName(); n && std::string_view(n).find(a_name) != std::string_view::npos) return true;
			}
			return false;
		}
		int  g_stage = -1;
		auto g_checked = std::chrono::steady_clock::now() - std::chrono::seconds(10);
		int  Compute()
		{
			if (!Forms::IntroDone()) return 0;
			if (Done("Diplomatic Immunity") || Done("Season Unending")) return 5;
			if (Done("The Way of the Voice")) return 4;
			if (Done("Dragon Rising")) return 3;
			if (Done("Bleak Falls Barrow")) return 2;
			return 1;
		}
	}

	int Stage()
	{
		if (std::chrono::steady_clock::now() - g_checked > std::chrono::seconds(3)) {
			g_checked = std::chrono::steady_clock::now();
			const int now = Compute();
			if (g_stage >= 0 && now > g_stage) {
				static constexpr const char* kNews[6] = { "", "Night falls on Skyrim: the dead rise",
					"The Dragonstone named the burial mounds of the old Dragon Cult", "The dragon is slain - Whiterun's trials open, and the nights bring their elites",
					"Arngeir speaks of the Echo Altars - the Greybeards will call when your path's secret is won", "The Greybeards' peace is made" };
				Forms::Play(gen::Sound::quest_start);
				Hud::QuestBanner(std::string("THE CURSE DEEPENS - ") + kNews[now], 6.0f);
			}
			if (now != g_stage) logger::info("story: stage {} -> {}", g_stage, now);
			g_stage = now;
		}
		return std::max(0, g_stage);
	}
	void Reset() { g_stage = -1, g_checked = std::chrono::steady_clock::now() - std::chrono::seconds(10); }

	bool SceneHold()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return false;
		if (InPlace(p, "Sovngarde")) return true;
		if (Running("Dragon Rising") && InPlace(p, "Western Watchtower")) return true;
		if (Running("The Fallen") && InPlace(p, "Dragonsreach")) return true;
		// (the story audit: the Echo of Alduin waits on this summit - a player stalled mid-quest could never fight it) held only
		// until the Echo wakes
		if (Running("Alduin's Bane") && InPlace(p, "Throat of the World") && !Echo::EchoAwake()) return true;
		if (Running("Season Unending") && InPlace(p, "High Hrothgar")) return true;
		return false;
	}

	// (Noah: "I want each Echo Altar to be locked until the main quest for that city is completed ... other than Whiterun, the
	// rest of them should have the city's own quest"; Riften and Winterhold neutral - no guild to join) Riverwood's altar
	// wakes with the first night; Whiterun's with Dragon Rising; every other city's once its own quest is done
	struct CityQuest { std::string_view town, quest, editorID; };
	constexpr CityQuest kCityQuests[] = {
		{ "Solitude", "The Wolf Queen Awakened", "" },
		{ "Windhelm", "The White Phial", "" },
		{ "Markarth", "The Forsworn Conspiracy", "" },
		{ "Riften", "The Book of Love", "T02" },
		{ "Falkreath", "Ill Met By Moonlight", "" },
		{ "Morthal", "Laid to Rest", "" },
		{ "Dawnstar", "Waking Nightmare", "" },
		{ "Winterhold", "Dungeon Delving", "Favor158" },  // (the Jarl's favour: the Helm of Winterhold from Yngol Barrow)
	};
	namespace
	{
		RE::TESQuest* CityQuestForm(const CityQuest& a_c)
		{
			static std::unordered_map<std::string, RE::TESQuest*> cache;
			const std::string key(a_c.town);
			if (auto it = cache.find(key); it != cache.end()) return it->second;
			RE::TESQuest* found = nullptr;
			if (auto* dh = RE::TESDataHandler::GetSingleton()) {
				auto lower = [](std::string_view v) { std::string o(v); for (auto& ch : o) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return o; };
				const std::string want = lower(a_c.quest);
				for (auto* q : dh->GetFormArray<RE::TESQuest>()) {
					if (!q) continue;
					if (!a_c.editorID.empty() && a_c.editorID == q->GetFormEditorID()) { found = q; break; }
					if (const char* n = q->GetName(); n && lower(n) == want && (!found || std::string_view(q->GetFormEditorID()).size())) found = q;
				}
			}
			logger::info("story: {}'s altar waits on {} - {}", a_c.town, a_c.quest, found ? fmt::format("{:08X} {}", found->GetFormID(), found->GetFormEditorID()) : std::string("not found"));
			cache[key] = found;
			return found;
		}
	}
	std::string_view CityQuestName(int a_town)
	{
		const std::string_view t = a_town >= 0 && a_town < static_cast<int>(gen::kTowns.size()) ? gen::kTowns[a_town] : "";
		if (t == "Whiterun") return "Dragon Rising";
		for (const auto& c : kCityQuests) if (c.town == t) return c.quest;
		return "";
	}
	bool TrialsOpen(int a_town, std::string* a_why)
	{
		const std::string_view t = a_town >= 0 && a_town < static_cast<int>(gen::kTowns.size()) ? gen::kTowns[a_town] : "";
		const int st = Stage();
		if (st < 1) return false;
		if (t == "Whiterun") {
			if (st < 3 && a_why) *a_why = "Whiterun's Echo Altar wakes once the dragon at the Western Watchtower falls (Dragon Rising)";
			return st >= 3;
		}
		for (const auto& c : kCityQuests) {
			if (c.town != t) continue;
			auto* q = CityQuestForm(c);
			const bool done = q && (q->IsCompleted() || (!q->IsRunning() && q->GetCurrentStageID() > 0));
			if (!done && a_why) *a_why = fmt::format("{}'s Echo Altar wakes once {} is done", t, c.quest);
			return done || !q;  // (its quest missing from the game: never locked for ever)
		}
		return true;  // Riverwood
	}
	bool BarrowJarlsOpen() { return Stage() >= 2; }
	bool ElitesOpen() { return Stage() >= 3; }
	bool AlduinFallen() { return Echo::DragonsSlain(); }  // (the story's height, not Sovngarde: the three dragons slain - it stays through the Seat's cycles)
	bool GrimmvarOpen() { return Echo::WondersAll(); }  // (Director: only the Herald waits for it - he hunts once the three dragons wake)
}
