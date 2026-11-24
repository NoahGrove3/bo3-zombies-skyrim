#include "BO3Z.h"

// The Curse of Alduin (sheet: curse). At Helgen, Alduin shouted SLEN TIID VO (his raising shout) into the earth and every barrow in
// Skyrim answered; the Echo Altars the Tongues raised in every city have woken only half. (Noah, 2026-11-23: the trials remade to
// the story - the same four in every city, each with the city's own places; one after another, all at night, the party sharing them)
//   restless  Lay the Restless to Rest: n of the risen slain in the altar's light - their souls rise through Alduin's mist
//   whisper   The Stone's Whisper (Felldir's Whisper once six altars are free): the altar's voice speaks a riddle naming a place in
//             the city; find it at night, then slay n of the risen there
//   echo      Repeat the Echo: at midnight (and each hour after, until dawn) the altar sings n notes on six stones round it;
//             touch the stones in the same order
//   seals     Break the Priest's Seals: three seals at the city's landmarks, each kept by an elite; kill the guard, strike the
//             seal. The last calls the city's Dragon Priest (curse.bosses) out at once
// Slaying the Dragon Priest frees the city (and ends the night): a sanctuary where no undead rise. Counts follow Skyrim's
// difficulty (Novice..Legendary). Progress: one global per city, 5000000 + doneBits*100000 + active*10000 + progress (active 0
// none, 1-4 a trial, 6 the Dragon Priest), 9000000 freed; a value from the old trials (1000000..4999999) starts the city fresh.
// Every step only grows the value, so the party's sync (the biggest wins) carries it.
namespace bo3z
{
	namespace
	{
		constexpr int kTrialCount = static_cast<int>(gen::kTrials.size());
		constexpr int kNeeded = kTrialCount;  // trials won before the Dragon Priest (all four, in order)
		constexpr int kFreed = kNeeded + 1;   // steps: four trials and the Dragon Priest
		constexpr int kOldBase = 1'000'000;   // (the old trials' values: a city part-way through them starts fresh - Noah)
		constexpr int kBase = 5'000'000, kFreedValue = 9'000'000, kBossActive = 6;
		static_assert(kTrialCount == 4);

		float Rand01()
		{
			static std::mt19937 r{ std::random_device{}() };
			return std::uniform_real_distribution<float>(0.0f, 1.0f)(r);
		}

		RE::TESGlobal* Global(std::uint32_t a_id)
		{
			return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(a_id, gen::kPlugin);
		}
		int Value(int a_town)
		{
			auto* g = Global(gen::kCurseCities[a_town].global);
			return g ? static_cast<int>(std::lround(g->value)) : 0;
		}
		void SetValue(int a_town, int a_v)
		{
			if (auto* g = Global(gen::kCurseCities[a_town].global)) {
				g->value = static_cast<float>(a_v);
			}
		}

		struct St
		{
			bool freed{ false };
			int  done{ 0 }, active{ 0 }, prog{ 0 };
			int  Count() const { return std::popcount(static_cast<unsigned>(done & 15)); }
			int  Trial() const { return active >= 1 && active <= kTrialCount ? active - 1 : -1; }
			bool Boss() const { return !freed && active == kBossActive; }
		};
		St Decode(int a_v)
		{
			St s;
			if (a_v >= kFreedValue) {
				s.freed = true;
			} else if (a_v >= kBase) {
				const int r = a_v - kBase;
				s.done = (r / 100000) & 15, s.active = (r / 10000) % 10, s.prog = r % 10000;
			}
			return s;
		}
		int Encode(const St& s) { return s.freed ? kFreedValue : kBase + (s.done & 15) * 100000 + std::clamp(s.active, 0, 9) * 10000 + std::clamp(s.prog, 0, 9999); }
		St   Get(int a_town) { return Decode(Value(a_town)); }
		void Put(int a_town, const St& s) { SetValue(a_town, Encode(s)); }
		int  Step(int a_town)
		{
			const St s = Get(a_town);
			return s.freed ? kFreed : s.Count();
		}

		// (co-save 'MINE') the trials you were there for, per city
		std::array<std::uint8_t, 16> g_mine{};
		// (Noah, 2026-11-23: the trials remade - "shared") every trial is the party's: anyone's work counts for all
		constexpr int kSharedMask = 15;
		int  Mine(int) { return 1; }  // (told to the party: no class trial left to wait on, nobody is ever short of one)
		[[maybe_unused]] bool InParty() { return Party::FriendCount() > 0; }
		// the first trial not yet won (kTrialCount: all four)
		int NextTrial(const St& a_s)
		{
			for (int t = 0; t < kTrialCount; ++t) if (!((a_s.done >> t) & 1)) return t;
			return kTrialCount;
		}
		int  g_waveSerial = 0;
		bool PriestReady(int, const St& a_s) { return a_s.Count() >= kNeeded; }

		// Skyrim's difficulty picks each trial's numbers
		int Diff() { return std::clamp(Settings::Difficulty(), 0, 5); }
		int N(int a_trial) { return gen::kTrials[a_trial].n[Diff()]; }
		[[maybe_unused]] int Secs(int a_trial) { return gen::kTrials[a_trial].seconds[Diff()]; }
		constexpr float kBossHealth[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };       // the Dragon Priest's health (the difficulty is applied once: Settings::UndeadHealthMult)

		std::string Upper(std::string s)
		{
			for (auto& c : s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return s;
		}
		void Replace(std::string& s, std::string_view what, const std::string& with)
		{
			for (std::size_t p = 0; (p = s.find(what, p)) != std::string::npos; p += with.size()) {
				s.replace(p, what.size(), with);
			}
		}
		std::string Fill(std::string_view a_text, int a_town, int a_n = 0, int a_s = 0)
		{
			std::string s(a_text);
			const std::string city(gen::kTowns[a_town]);
			Replace(s, "{city}", city);
			Replace(s, "{CITY}", Upper(city));
			Replace(s, "{landmark}", std::string(gen::kCurseCities[a_town].landmark));
			Replace(s, "{n}", std::to_string(a_n));
			Replace(s, "{s}", std::to_string(a_s));
			return s;
		}
		int FreedCount()
		{
			int n = 0;
			for (int t = 0; t < static_cast<int>(gen::kCurseCities.size()); ++t) n += Value(t) >= kFreedValue ? 1 : 0;
			return n;
		}
		// (Noah: "The Stone's Whisper" - the Codex keeps Felldir's name until six altars are free; then the trial wears it)
		std::string_view Title(int a_trial)
		{
			if (a_trial == gen::kTrial_whisper && FreedCount() >= 6) return "Felldir's Whisper";
			return a_trial >= 0 && a_trial < kTrialCount ? gen::kTrials[a_trial].title : std::string_view{};
		}
		std::string_view BossName(int a_town) { return gen::kCityBosses[a_town].name; }

		// Quest news lives in the tracker (top right): a line under the goal for a few seconds. No pop-ups.
		void Note(std::string a_text, float a_seconds = 5.0f)
		{
			// (the UI audit: the tracker this wrote to is off - every city freed, every book notice went unseen) a card under the Banner
			Hud::Lore("", std::move(a_text), std::max(5.0f, a_seconds));
		}

		// The lore lives in a book, "The Curse of Alduin", in the player's pack from the moment the character is made;
		// (Noah: two books) how to play is in the class guide, given when a class is chosen (src/Classes.cpp)
		bool GiveBook(RE::PlayerCharacter* a_player)
		{
			bool gave = false;
			for (auto id : { gen::kDirgeBook }) {
				auto* book = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectBOOK>(id, gen::kPlugin);
				if (!book || !a_player) {
					continue;
				}
				auto inv = a_player->GetInventory([&](RE::TESBoundObject& o) { return &o == book; });
				if (!inv.empty() && inv.begin()->second.first > 0) {
					continue;
				}
				a_player->AddObjectToContainer(book, nullptr, 1, nullptr);
				gave = true;
			}
			return gave;
		}

		int   g_zone = -1;
		std::array<int, 16> g_journalKey{};  // the journal (Journal below): the progress value each city's quest last showed
		bool                g_journalFresh = true;
		RE::ObjectRefHandle g_marker;        // the quest Target's marker
		float g_zoneTimer = 0;
		float g_normDelay = 3.0f;            // a moment after a load before old saves' values are read (MigrateRites goes first)
		// ---- the trial under way (not saved: a load starts a song, a seal's guard or a riddle again)
		std::unordered_map<RE::FormID, std::pair<std::chrono::steady_clock::time_point, bool>> g_lastBlow;
		// Repeat the Echo: six stones in a ring round the altar, the song it sings on them, and your answer
		struct EchoRun
		{
			int                                town = -1;
			std::array<RE::ObjectRefHandle, 6> stone;
			std::array<RE::NiPoint3, 6>        at{};
			std::array<float, 6>               ang{}, baseZ{};
			int                                phase = 0;    // 0 waiting for the hour, 1 the altar sings, 2 your turn
			std::vector<int>                   seq;
			int                                idx = 0;      // singing: the note; your turn: the stone wanted next
			float                              t = 0;        // time in the phase
			int                                lastHour = -100;
			int                                touching = -1;  // the stone you stand at (standing still is not a second touch)
		} g_echo;
		// Break the Priest's Seals: each seal (a violet heart-stone over a turning rune circle) and the elite who keeps it
		struct SealRun
		{
			int                                town = -1;
			std::array<RE::ObjectRefHandle, 3> stone, runes;
			std::array<RE::ActorHandle, 3>     guard;
			std::array<bool, 3>                down{}, raised{}, snapped{};
			std::array<RE::NiPoint3, 3>        at{};
			std::array<float, 3>               retry{}, pulse{}, ang{};
		} g_seals;
		RE::NiPoint3 g_priestAt{};           // where the last seal broke: the Dragon Priest steps out there
		bool         g_priestCall = false;
		// The Stone's Whisper: the place, snapped to the navmesh once
		int          g_whisperTown = -1;
		RE::NiPoint3 g_whisperAt{};
		bool         g_whisperSnapped = false;
		float        g_whisperSay = 0;       // the riddle said again while you stand at the altar at night
		float        g_wispT = 0;            // (Lay the Restless to Rest: the rising sound, not more than one at a time)
		RE::ObjectRefHandle g_probe;

		void Gone(RE::ObjectRefHandle& h)
		{
			if (auto r = h.get()) {
				r->Disable();
				r->SetDelete(true);
			}
			h = {};
		}
		void ClearEcho()
		{
			for (auto& h : g_echo.stone) Gone(h);
			g_echo = {};
		}
		void ClearSeals()
		{
			for (auto& h : g_seals.stone) Gone(h);
			for (auto& h : g_seals.runes) Gone(h);
			g_seals = {};
		}

		void ClearRun()
		{
			ClearEcho();
			ClearSeals();
			g_whisperTown = -1, g_whisperSay = 0, g_whisperSnapped = false;
		}

		// The city the player is in: its own location (streets and interiors), or within reach of its station.
		int ComputeZone(RE::PlayerCharacter* a_player)
		{
			if (auto* loc = a_player->GetCurrentLocation()) {
				for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
					auto* city = RE::TESForm::LookupByID<RE::BGSLocation>(gen::kCurseCities[t].location);
					for (auto* l = loc; l && city; l = l->parentLoc) {
						if (l == city) {
							return t;
						}
					}
				}
			}
			auto*       ws = a_player->GetWorldspace();
			const auto  p = a_player->GetPosition();
			int         best = -1;
			float       bestD = gen::kCurseZone;
			for (int t = 0; ws && t < static_cast<int>(gen::kTowns.size()); ++t) {
				RE::NiPoint3        c;
				float               yaw = 0;
				RE::TESWorldSpace*  w = nullptr;
				Shops::StandFrame(t, c, yaw, w);
				if (w != ws) {
					continue;
				}
				const float d = std::hypot(c.x - p.x, c.y - p.y);
				if (d < bestD) {
					bestD = d, best = t;
				}
			}
			return best;
		}

		// Nearest walkable ground (navmesh) to a point: city streets have paving the land height knows nothing of.
		std::optional<RE::NiPoint3> Walkable(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, float a_maxShift = 400.0f)
		{
			auto probe = g_probe.get();
			if (!probe || probe->GetWorldspace() != a_player->GetWorldspace()) {
				auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);
				auto  ref = xm ? a_player->PlaceObjectAtMe(xm, false) : nullptr;
				if (!ref) {
					return std::nullopt;
				}
				g_probe = ref->CreateRefHandle();
				probe = ref;
			}
			RE::NiPoint3 at = a_at;
			float        land = 0;
			if (RE::TES::GetSingleton()->GetLandHeight(at, land)) {
				at.z = land;
			}
			probe->SetPosition(at);
			probe->MoveToNearestNavmesh();
			const auto got = probe->GetPosition();
			if (std::hypot(got.x - at.x, got.y - at.y) > a_maxShift) {
				return std::nullopt;
			}
			return got;
		}

		[[maybe_unused]] float HealthFrac(RE::PlayerCharacter* a_player)
		{
			auto*       av = a_player->AsActorValueOwner();
			const float mx = std::max(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth));
			return av->GetActorValue(RE::ActorValue::kHealth) / mx;
		}

		// The city's shout (sheet: shouts): what freeing it earns, claimed at the Echo Altar.
		// (Noah: "remove the shouts from the echo alter rewards but keep the dance of death shout and dragons updraft") no
		// altar teaches a shout now: Whiterun's Dragon's Updraft comes with the dragon (src/Movement.cpp EnsureUpdraft) and
		// Solitude's Tiny Terrors from its bards (once the Dance of Death) (src/Director.cpp TickDanceShout), both the moment the city is freed
		int ShoutOf(int)
		{
			return -1;
		}

		// what each trial asks tonight, in a line (the altar's menu and the trial's HUD line)
		std::string Ask(int a_trial, int a_town = -1)
		{
			const int n = N(a_trial);
			switch (a_trial) {
			case gen::kTrial_restless: return fmt::format("Slay {} of the risen in the altar's light", n);
			case gen::kTrial_whisper:
				if (a_town >= 0 && a_town < static_cast<int>(gen::kCurseCities.size())) return fmt::format("\"{}\" - find it, then slay {} there", gen::kCurseCities[a_town].riddle, n);
				return fmt::format("Find the place the altar's voice names, then slay {} of the risen there", n);
			case gen::kTrial_echo: return fmt::format("At midnight, touch the six stones in the order they sing ({} notes)", n);
			case gen::kTrial_seals: return "Kill the three guards and break their seals - the last calls the Dragon Priest";
			default: return {};
			}
		}

		void Lift(RE::PlayerCharacter* a_player, int a_town)
		{
			Put(a_town, St{ true });
			ClearRun();
			Forms::Play(gen::Sound::curse_lifted);
			Shops::FreedPulse(a_town);
			Audio::FreedMusic();
			std::string text = Fill(gen::kCurseLiftedBanner, a_town);
			if (const int sh = ShoutOf(a_town); sh >= 0) {
				// its shout waits on the Echo Altar at the station (nothing is forced into your hands)
				text += " - its Thu'um, " + std::string(gen::kShouts[sh].label) + ", waits in the runes of the city's altar (" + (Hud::PadLast() ? std::string("its LEARN THE THU'UM row") : Settings::KeyName(3) + " in its menu") + ")";
			}
			Note(text, 10.0f);
			Party::CityFreed(a_town);  // (the dawn that follows saves under the city's name)
			// (Noah: a night is won by slaying all of its waves, not by the Dragon Priest's fall) the night goes on
			auto* shrine = Shops::Placed(a_town, 0);
			Director::LiftWave(shrine ? shrine->GetPosition() : a_player->GetPosition());
			Points::Add(5000);
			logger::info("curse: {} is freed", gen::kTowns[a_town]);
			bool all = true;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				all &= Get(t).freed;
			}
			Classes::VoiceSay(all ? gen::VoTrigger::kAllFree : gen::VoTrigger::kFreed, 10.0f);  // (after the Dragon Priest's fall is said)
			if (all) {
				// (the story audit: it pointed to the Echo while the dragons still flew)
				Note(Echo::DragonsSlain() ? std::string(gen::kCurseFreedAll)
				                          : std::string("Every Echo Altar in Skyrim is awake. Alduin's three dragons still fly - silence them, and his echo will wait for you on the Throat of the World."),
					12.0f);
			}
		}

		// a riddle from the altar: its voice, subtitled (the trial's line under the compass keeps it at night, the journal always)
		void SayRiddle(int a_town)
		{
			if (a_town < 0 || a_town >= static_cast<int>(gen::kCurseCities.size())) return;
			Hud::Subtitle(std::string(gen::kCurseCities[a_town].riddle), 11.0f);
			g_whisperSay = 120.0f;
		}
		// a trial done: the next one begins at once (in order); the last, the seals, calls the Dragon Priest out
		void CompleteTrial(int a_town)
		{
			St s = Get(a_town);
			const int tr = s.Trial();
			if (tr < 0) {
				return;
			}
			s.done |= 1 << tr;
			s.prog = 0;
			if (a_town >= 0 && a_town < 16) g_mine[a_town] |= static_cast<std::uint8_t>(1 << tr);
			const int next = NextTrial(s);
			s.active = next < kTrialCount ? next + 1 : PriestReady(a_town, s) ? kBossActive : 0;
			Put(a_town, s);
			ClearRun();
			Forms::Play(gen::Sound::quest_step);
			Points::Add(500 * s.Count());
			if (next < kTrialCount) {
				Hud::QuestBanner(Upper(std::string(Title(tr))) + " - COMPLETE   next: " + Upper(std::string(Title(next))), 6.0f);
				if (next == gen::kTrial_whisper && Get(a_town).Trial() == gen::kTrial_whisper) SayRiddle(a_town), Classes::VoiceSay(gen::VoTrigger::kWhisperStart, 12.0f);
			} else {
				Hud::QuestBanner(Upper(std::string(Title(tr))) + " - COMPLETE   " + std::string(BossName(a_town)) + " comes", 6.0f);
			}
			logger::info("curse: {} trial {} done ({} of {}), next {}", gen::kTowns[a_town], gen::kTrials[tr].id, s.Count(), kNeeded, next < kTrialCount ? gen::kTrials[next].id : std::string_view("the Dragon Priest"));
		}
		// the city's places (the sheet's spot, or where it was snapped to the navmesh once the trial was near)
		RE::NiPoint3 PlaceAt(const gen::CursePlace& a_p) { return { a_p.x, a_p.y, a_p.z }; }
		RE::NiPoint3 SealAt(int a_town, int i) { return g_seals.town == a_town && g_seals.snapped[i] ? g_seals.at[i] : PlaceAt(gen::kCurseCities[a_town].seals[i]); }
		RE::NiPoint3 WhisperAt(int a_town) { return g_whisperTown == a_town ? g_whisperAt : PlaceAt(gen::kCurseCities[a_town].whisper); }

		// Where the quest wants you now (in your current world): the altar to choose a trial or hold its circle, the
		// nearest of the dead to kill, the Dragon Priest, a freed city's waiting shout, or from the wilds the nearest held city.
		RE::TESQuest* RiteQuestOf(int a_town)
		{
			return a_town >= 0 && a_town < static_cast<int>(gen::kRiteQuests.size()) ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(gen::kRiteQuests[a_town], gen::kPlugin) : nullptr;
		}
		// (Noah: the quest marker only for the quest selected in the journal) a city's marks show only while its rite is
		// the active quest; the Echo hunt (no journal quest of its own) only while none of Skyrim's quests is selected
		bool RiteSelected(int a_town)
		{
			auto* q = RiteQuestOf(a_town);
			return q && q->IsActive();
		}
		bool AnyQuestSelected()
		{
			for (auto* q : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESQuest>()) {
				if (q && q->IsActive()) return true;
			}
			return false;
		}
		bool ObjectiveTarget(RE::PlayerCharacter* a_player, RE::NiPoint3& a_at, std::string& a_label)
		{
			const int z = g_zone;
			// (the audit: a freed city's quest is completed and can't be selected - its shout and Echo markers never showed)
			if (z >= 0 && !Get(z).freed && !RiteSelected(z)) {
				return false;
			}
			if (z >= 0) {
				const St s = Get(z);
				if (s.freed) {
					const int sh = ShoutOf(z);
					if (sh >= 0 && !Gun::KnowsShout(sh)) {
						if (auto* altar = Shops::Placed(z, 0)) {
							a_at = altar->GetPosition() + RE::NiPoint3{ 0, 0, 200.0f };
							a_label = "Claim " + std::string(gen::kShouts[sh].label);
							return true;
						}
					}
					return Echo::Target(a_player, a_at, a_label);  // its Echo Stone, then the hunt
				}
				const int tr = s.Trial();  // (Noah: the ping stays over the altar - it doesn't chase the dead - save for the places a trial sends you)
				if (tr == gen::kTrial_whisper) {
					if (s.prog == 0) return false;  // (a riddle: no marker until you find it)
					a_at = WhisperAt(z) + RE::NiPoint3{ 0, 0, 120.0f };
					a_label = "Hold this place";
					return true;
				}
				if (tr == gen::kTrial_seals) {  // the nearest seal still whole
					float best = FLT_MAX;
					for (int i = 0; i < 3; ++i) {
						if ((s.prog >> i) & 1) continue;
						const auto at = SealAt(z, i);
						if (const float d = at.GetDistance(a_player->GetPosition()); d < best) {
							best = d, a_at = at + RE::NiPoint3{ 0, 0, 200.0f };
							a_label = "Break the seal - " + std::string(gen::kCurseCities[z].seals[i].place);
						}
					}
					if (best < FLT_MAX) return true;
				}
				if (Shops::StationTarget(z, a_at)) {
					a_label = s.Boss() ? std::string(BossName(z)) : tr >= 0 ? std::string(Title(tr)) : "Take up the trials";
					return true;
				}
				return false;
			}
			// out in the wilds: the nearest discovered city still held, or whose shout waits
			float best = FLT_MAX;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				const St   s = Get(t);
				const int  sh = ShoutOf(t);
				const bool wants = !s.freed || (sh >= 0 && !Gun::KnowsShout(sh));
				RE::NiPoint3 at;
				if (!wants || !(s.freed || RiteSelected(t)) || !Shops::Discovered(t) || !Shops::StationTarget(t, at)) {
					continue;
				}
				if (const float d = at.GetDistance(a_player->GetPosition()); d < best) {
					best = d, a_at = at;
					a_label = std::string(gen::kTowns[t]) + ": " + (s.freed ? std::string("its shout waits") : s.Boss() ? std::string(BossName(t)) : s.Trial() >= 0 ? std::string(Title(s.Trial())) : std::string("take up the trials"));
				}
			}
			// stage 2: a wonder's part, the Reliquary - whichever is nearer
			RE::NiPoint3 eat;
			std::string  elabel;
			static float anyT = 0;
			static bool  any = false;
			if ((anyT -= 0.016f) <= 0) anyT = 1.0f, any = AnyQuestSelected();
			if (!any && Echo::Target(a_player, eat, elabel) && eat.GetDistance(a_player->GetPosition()) < best) {
				a_at = eat, a_label = std::move(elabel);
				return true;
			}
			return best < FLT_MAX;
		}

		// The floating runic circle of the Ward of the Tongues: a ring of runes drawn on the ground round the altar
		// (projected to the screen here, drawn by the HUD) and how far the trial has come.
		struct RingState
		{
			bool         on{ false };
			RE::NiPoint3 center{};
			float        radius{ 0 }, frac{ 0 }, flash{ 0 };
			int          seconds{ 0 }, inside{ 0 };
			bool         hold{ false };
			std::string  head, sub;  // (the trials remade) its words: what it asks, and the count
			bool         runes{ true };  // (Noah, 2026-11-24: Lay the Restless to Rest not the old ward's ring of runes) the words only, when false
		} g_ring;
		// (Noah, 2026-11-24: "the souls that come from the enemies for killing trials should be light blue wisps that go into the echo
		// altar's center stone, not a magic effect or a little burst") each of the restless put down sends its soul up out of the body
		// and across to the altar's centre stone on a soft arc - a pale blue wisp with a fading tail - where the stone takes it with a
		// small glow and a chime
		struct Soul { RE::NiPoint3 a, c, b; float t = 0, dur = 1.5f; };
		struct Taken { RE::NiPoint3 at; float t = 0; };
		std::vector<Soul>  g_souls;
		std::vector<Taken> g_taken;
		RE::NiPoint3 SoulAt(const Soul& a_s, float a_t)
		{
			float u = std::clamp(a_t / a_s.dur, 0.0f, 1.0f);
			u = u * u * (3.0f - 2.0f * u);   // (eases away from the body and into the stone)
			const float v = 1.0f - u;
			return a_s.a * (v * v) + a_s.c * (2.0f * v * u) + a_s.b * (u * u);
		}
		void SendSoul(const RE::NiPoint3& a_from, const RE::NiPoint3& a_stone)
		{
			if (g_souls.size() >= 24) return;
			Soul so;
			so.a = a_from + RE::NiPoint3{ 0, 0, 70.0f };
			so.b = a_stone;
			const float dist = so.a.GetDistance(so.b);
			const float side = (static_cast<float>(std::rand() % 200) - 100.0f) * 1.2f;
			RE::NiPoint3 dir = so.b - so.a;
			const RE::NiPoint3 across{ -dir.y, dir.x, 0 };
			const float al = std::max(1.0f, std::hypot(across.x, across.y));
			so.c = (so.a + so.b) * 0.5f + RE::NiPoint3{ across.x / al * side, across.y / al * side, 220.0f + dist * 0.18f };
			so.dur = std::clamp(dist / 650.0f, 1.2f, 3.2f);
			g_souls.push_back(so);
		}
		void PublishRing(float a_delta)
		{
			g_ring.flash = std::max(0.0f, g_ring.flash - a_delta);
			std::vector<std::array<float, 3>> pts;
			auto* cam = RE::Main::WorldRootCamera();
			// the souls on their way, and the stone taking them
			std::vector<std::vector<std::array<float, 4>>> souls;
			std::vector<std::array<float, 4>>              taken;
			{
				auto* pl = RE::PlayerCharacter::GetSingleton();
				const RE::NiPoint3 eye = cam ? cam->world.translate : (pl ? pl->GetPosition() : RE::NiPoint3{});
				for (auto it = g_souls.begin(); it != g_souls.end();) {
					it->t += a_delta;
					if (it->t >= it->dur) {
						g_taken.push_back({ it->b, 0 });
						Audio::AtPoint(static_cast<gen::Sound>(static_cast<int>(gen::Sound::echo_tone_0) + std::rand() % 6), it->b, 0.0f, 0.3f);
						it = g_souls.erase(it);
						continue;
					}
					if (cam) {
						std::vector<std::array<float, 4>> tr;
						for (int k = 0; k < 10; ++k) {   // (its head now, and where it was a moment before: the tail)
							const float tt = it->t - k * 0.035f;
							if (tt < 0) break;
							const auto w = SoulAt(*it, tt);
							float x = 0, y = 0, zc = 0;
							cam->WorldPtToScreenPt3(w, x, y, zc, 1e-5f);
							if (zc <= 0) break;
							tr.push_back({ x, 1.0f - y, zc, 900.0f / std::max(120.0f, w.GetDistance(eye)) });
						}
						if (!tr.empty()) souls.push_back(std::move(tr));
					}
					++it;
				}
				for (auto it = g_taken.begin(); it != g_taken.end();) {
					if ((it->t += a_delta) > 0.9f) { it = g_taken.erase(it); continue; }
					if (cam) {
						float x = 0, y = 0, zc = 0;
						cam->WorldPtToScreenPt3(it->at, x, y, zc, 1e-5f);
						if (zc > 0) taken.push_back({ x, 1.0f - y, it->t, 900.0f / std::max(120.0f, it->at.GetDistance(eye)) });
					}
					++it;
				}
			}
			if (g_ring.on && g_ring.runes && cam) {
				constexpr int N = 180;  // (fine enough for the runes riding it to sit smoothly)
				for (int i = 0; i <= N; ++i) {
					const float a = i * 2.0f * RE::NI_PI / N;
					RE::NiPoint3 w = g_ring.center + RE::NiPoint3{ std::cos(a) * g_ring.radius, std::sin(a) * g_ring.radius, 18.0f };
					float x = 0, y = 0, zc = 0;
					cam->WorldPtToScreenPt3(w, x, y, zc, 1e-5f);
					pts.push_back({ x, 1.0f - y, zc });
				}
			}
			bool  labelOn = false;
			float lx = 0, ly = 0;
			if (g_ring.on && cam) {  // (Noah: the words stay put over the altar)
				float zc = 0;
				cam->WorldPtToScreenPt3(g_ring.center + RE::NiPoint3{ 0, 0, 330.0f }, lx, ly, zc, 1e-5f);
				ly = 1.0f - ly;
				labelOn = zc > 0.0f;
			}
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.ringLabelOn = labelOn, h.ringLabelX = lx, h.ringLabelY = ly;
			h.ringOn = g_ring.on && (!pts.empty() || !g_ring.runes);
			h.ringRunes = g_ring.runes;
			h.wisps = std::move(souls), h.wispsTaken = std::move(taken);
			h.ringPts = std::move(pts);
			h.ringFrac = g_ring.frac;
			h.ringFlash = g_ring.flash;
			h.ringSeconds = g_ring.seconds;
			h.ringInside = g_ring.inside;
			h.ringHold = g_ring.hold;
			h.ringHead = g_ring.head, h.ringSub = g_ring.sub;
		}

		void PublishObjective(RE::PlayerCharacter* a_player)
		{
			RE::NiPoint3 at;
			std::string  label;
			const bool   on = ObjectiveTarget(a_player, at, label);
			float        x = 0, y = 0, zc = 0;
			bool         front = false;
			if (on) {
				if (auto* cam = RE::Main::WorldRootCamera()) {
					cam->WorldPtToScreenPt3(at, x, y, zc, 1e-5f);
					front = zc > 0.0f;
					y = 1.0f - y;  // (Skyrim's screen y runs up)
				}
			}
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.objOn = on;
			h.objFront = front;
			h.objX = x, h.objY = y;
			h.objDist = on ? at.GetDistance(a_player->GetPosition()) : 0.0f;
			h.objAng = on ? std::atan2(at.x - a_player->GetPosition().x, at.y - a_player->GetPosition().y) : 0.0f;
			h.objLabel = label;
		}

		// The trial under way, as a slim line under the compass at night (its count and its clock)
		void Publish(int a_zone, bool a_night)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.questCity.clear(), h.questTitle.clear(), h.questGoal.clear(), h.questSub.clear(), h.sanctuary.clear();
			h.questFrac = 0, h.questTimer = -1, h.questWarn = false;
			if (a_zone < 0) {
				Echo::Strip(h.questTitle, h.questGoal, h.questFrac);  // out in the wilds: a hunt for a wonder's part
				return;
			}
			if (Eggs::Strip(h.questTitle, h.questGoal, h.questFrac, h.questTimer)) return;  // a wonder's secret, its step here
			const St s = Get(a_zone);
			if (s.freed) {
				if (a_night) {
					h.sanctuary = Upper(std::string(gen::kTowns[a_zone]));
				}
				return;
			}
			if (!a_night || (s.Trial() < 0 && !s.Boss())) {
				return;
			}
			h.questCity = Upper(std::string(gen::kTowns[a_zone]));
			h.questStep = s.Count();
			if (s.Boss()) {
				h.questTitle = "THE DRAGON PRIEST";
				// (Noah: "the Dragon Priest never spawned at the end of my night") it only wakes once the story reaches Bleak
				// Falls Barrow - now it says so, wherever it waits
				h.questGoal = Director::PriestHold() ? "Slay " + std::string(BossName(a_zone)) : std::string(BossName(a_zone)) + (Director::Fighting() ? " rises when the night's last wave falls" : " rises at the end of the next night");
				h.questWarn = false;
				return;
			}
			const int tr = s.Trial(), n = N(tr);
			h.questTitle = Upper(std::string(Title(tr)));
			RE::NiPoint3 c{};
			float yaw = 0;
			RE::TESWorldSpace* ws = nullptr;
			const bool here = player && Shops::StandFrame(a_zone, c, yaw, ws) && ws == player->GetWorldspace();
			switch (tr) {
			case gen::kTrial_restless:
				{
					const bool in = here && std::hypot(player->GetPosition().x - c.x, player->GetPosition().y - c.y) < gen::kCurseLight;
					h.questGoal = fmt::format("{}  {} / {}", in ? "Slay the risen in the altar's light" : "Go back to the altar's light", s.prog, n);
					h.questFrac = static_cast<float>(s.prog) / std::max(1, n);
					h.questWarn = !in;
				}
				break;
			case gen::kTrial_whisper:
				if (s.prog == 0) {
					h.questGoal = "Find the place the altar's voice names";
					h.questSub = "\"" + std::string(gen::kCurseCities[a_zone].riddle) + "\"";
				} else {
					const auto at = WhisperAt(a_zone);
					const bool in = player && std::hypot(player->GetPosition().x - at.x, player->GetPosition().y - at.y) < gen::kCurseWhisperRadius + 300.0f;
					h.questGoal = fmt::format("{} - {}  {} / {}", gen::kCurseCities[a_zone].whisper.place, in ? "slay the risen here" : "go back to it", s.prog - 1, n);
					h.questFrac = static_cast<float>(s.prog - 1) / std::max(1, n);
					h.questWarn = !in;
				}
				break;
			case gen::kTrial_echo:
				{
					auto* cal = RE::Calendar::GetSingleton();
					const float hour = cal ? cal->GetHour() : 0.0f;
					if (g_echo.phase == 1) h.questGoal = "Watch the stones", h.questFrac = static_cast<float>(g_echo.idx) / std::max(1, n);
					else if (g_echo.phase == 2) h.questGoal = fmt::format("Touch the stones in the order they sang  {} / {}", g_echo.idx, n), h.questFrac = static_cast<float>(g_echo.idx) / std::max(1, n);
					else if (hour >= 12.0f) h.questGoal = "Wait by the altar for midnight";
					else h.questGoal = fmt::format("Wait by the altar - it sings again at {}:00", static_cast<int>(hour) + 1);
					h.questWarn = !here || std::hypot(player->GetPosition().x - c.x, player->GetPosition().y - c.y) > 2500.0f;
				}
				break;
			case gen::kTrial_seals:
				h.questGoal = fmt::format("Kill each seal's guard, then strike the seal  {} / 3", std::popcount(static_cast<unsigned>(s.prog & 7)));
				h.questFrac = std::popcount(static_cast<unsigned>(s.prog & 7)) / 3.0f;
				break;
			default: break;
			}
		}

		// ---- (Noah, 2026-11-23) the four trials, run each frame in the city whose trial is under way
		// a static of ours placed at a point, or nothing
		RE::ObjectRefHandle PlaceStat(RE::PlayerCharacter* a_p, std::uint32_t a_id, const RE::NiPoint3& a_at, float a_scale, float a_yaw)
		{
			auto* base = Forms::Ours(a_id);
			auto  ref = base ? a_p->PlaceObjectAtMe(base, false) : nullptr;
			if (!ref) return {};
			ref->SetPosition(a_at);
			ref->SetAngle({ 0.0f, 0.0f, a_yaw });
			ref->SetScale(a_scale);
			return ref->CreateRefHandle();
		}
		// statics never move by their angle while loaded: their 3D node is turned (and bobbed) directly, as the altar's own stones
		void TurnNode(const RE::ObjectRefHandle& h, float a_ang, float a_bob)
		{
			auto r = h.get();
			if (!r || !r->Is3DLoaded()) return;
			auto* node = r->Get3D() ? r->Get3D()->AsNode() : nullptr;
			if (!node) return;
			node->local.rotate.SetEulerAnglesXYZ(0.0f, 0.0f, -a_ang);
			node->local.translate.z = r->GetPosition().z + std::sin(a_ang * 1.7f) * a_bob;
			RE::NiUpdateData ud;
			node->Update(ud);
		}
		// (placed pieces are kept in the save, but a load forgets them) the trials' stones and seals left from before - away from the
		// altar's own (its dressing uses the same stones) and not ours now - are cleared from round you every few seconds
		void SweepStrays(RE::PlayerCharacter* a_p, const RE::NiPoint3& a_altar)
		{
			std::vector<RE::TESBoundObject*> bases;
			for (auto id : gen::kPillarRock) bases.push_back(Forms::Ours(id));
			bases.push_back(Forms::Ours(gen::kFreedRunes));
			bases.push_back(Forms::Ours(gen::kHeartStone));
			auto mine = [&](RE::TESObjectREFR* r) {
				for (auto& h : g_echo.stone) if (h.get().get() == r) return true;
				for (auto& h : g_seals.stone) if (h.get().get() == r) return true;
				for (auto& h : g_seals.runes) if (h.get().get() == r) return true;
				return false;
			};
			std::vector<RE::TESObjectREFR*> stray;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_p, 12000.0f, [&](RE::TESObjectREFR* r) {
				if (r && !r->IsDisabled() && (r->GetFormID() >> 24) == 0xFF && std::find(bases.begin(), bases.end(), r->GetBaseObject()) != bases.end() &&
					r->GetPosition().GetDistance(a_altar) > 450.0f && !mine(r)) {
					stray.push_back(r);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (auto* r : stray) r->Disable(), r->SetDelete(true);
			if (!stray.empty()) logger::info("curse: {} stray trial stones cleared", stray.size());
		}

		// Lay the Restless to Rest: the altar's light, a ring round it (the deaths are counted in OnUndeadDeath)
		void TickRestless(RE::PlayerCharacter* a_p, int a_z, const St& a_s, const RE::NiPoint3& a_c)
		{
			const int   n = N(gen::kTrial_restless);
			const float d = std::hypot(a_p->GetPosition().x - a_c.x, a_p->GetPosition().y - a_c.y);
			g_ring = { true, a_c, gen::kCurseLight, static_cast<float>(a_s.prog) / std::max(1, n), g_ring.flash, 0, 1, false,
				d < gen::kCurseLight ? "LAY THE RESTLESS TO REST" : "BACK INTO THE ALTAR'S LIGHT", fmt::format("{} / {}", a_s.prog, n) };
			g_ring.runes = false;   // (no ring of runes: the old ward's - Noah: "it should not be recycled from the older trials")
			(void)a_z;
		}

		// The Stone's Whisper: the riddle (again, while you stand at the altar at night), the place found, then held
		void TickWhisper(RE::PlayerCharacter* a_p, int a_z, St a_s, const RE::NiPoint3& a_c, bool a_fighting, float a_dt)
		{
			const auto& w = gen::kCurseCities[a_z].whisper;
			if (g_whisperTown != a_z) g_whisperTown = a_z, g_whisperAt = { w.x, w.y, w.z }, g_whisperSnapped = false;
			const auto pp = a_p->GetPosition();
			if (!g_whisperSnapped && pp.GetDistance(g_whisperAt) < 9000.0f) {
				if (auto at = Walkable(a_p, g_whisperAt, 350.0f)) g_whisperAt = *at;
				g_whisperSnapped = true;
			}
			const float d = std::hypot(pp.x - g_whisperAt.x, pp.y - g_whisperAt.y);
			if (a_s.prog == 0) {
				if (a_fighting && std::hypot(pp.x - a_c.x, pp.y - a_c.y) < 900.0f && (g_whisperSay -= a_dt) <= 0) SayRiddle(a_z);
				if (a_fighting && d < 500.0f && std::abs(pp.z - g_whisperAt.z) < 500.0f) {
					a_s.prog = 1;
					Put(a_z, a_s);
					Forms::Play(gen::Sound::quest_step);
					Gun::FxBlastSized("light", g_whisperAt + RE::NiPoint3{ 0, 0, 100.0f }, 1.2f);
					Classes::VoiceSay(gen::VoTrigger::kWhisperFound, 0.5f);  // (your character knows the place)
					logger::info("curse: {} - the whisper's place found ({})", gen::kTowns[a_z], w.place);
				}
				return;
			}
			const int n = N(gen::kTrial_whisper);
			g_ring = { true, g_whisperAt, gen::kCurseWhisperRadius, static_cast<float>(a_s.prog - 1) / std::max(1, n), g_ring.flash, 0, 1, false,
				d < gen::kCurseWhisperRadius + 300.0f ? "HOLD THIS PLACE" : "GO BACK TO IT", fmt::format("{} / {}", a_s.prog - 1, n) };
		}

		// Repeat the Echo: six stones in a ring; at midnight (and each hour after, till dawn) the altar sings, you answer
		constexpr gen::Sound kTone[6] = { gen::Sound::echo_tone_0, gen::Sound::echo_tone_1, gen::Sound::echo_tone_2, gen::Sound::echo_tone_3, gen::Sound::echo_tone_4, gen::Sound::echo_tone_5 };
		void EchoFail(const RE::NiPoint3& a_c, std::string_view a_why)
		{
			Forms::Play(gen::Sound::echo_wrong);
			for (const auto& at : g_echo.at) Gun::FxBlast("dark", at + RE::NiPoint3{ 0, 0, 40.0f });
			(void)a_c;
			g_echo.phase = 0, g_echo.idx = 0, g_echo.t = 0, g_echo.touching = -1;
			g_ring.flash = 1.0f;
			logger::info("curse: Repeat the Echo - {}; it sings again at the next hour", a_why);
			Classes::VoiceSay(gen::VoTrigger::kEchoWrong, 1.6f);
		}
		void TickEcho(RE::PlayerCharacter* a_p, int a_z, const RE::NiPoint3& a_c, float a_yaw, bool a_fighting, float a_dt)
		{
			auto&       e = g_echo;
			auto*       cal = RE::Calendar::GetSingleton();
			const float hour = cal ? cal->GetHour() : 12.0f;
			if (e.town != a_z) {
				ClearEcho();
				e.town = a_z, e.lastHour = static_cast<int>(hour);
			}
			const auto pp = a_p->GetPosition();
			const float dAltar = std::hypot(pp.x - a_c.x, pp.y - a_c.y);
			for (int i = 0; i < 6; ++i) {
				if (!e.stone[i].get() && dAltar < 9000.0f) {
					const float a = a_yaw + i * RE::NI_PI / 3.0f;
					RE::NiPoint3 at = a_c + RE::NiPoint3{ std::cos(a) * gen::kCurseEchoRadius, std::sin(a) * gen::kCurseEchoRadius, 0.0f };
					if (auto w = Walkable(a_p, at, 300.0f)) at = *w;
					at.z += 110.0f;  // (floating, as the altar's own)
					e.at[i] = at, e.ang[i] = a;
					e.stone[i] = PlaceStat(a_p, gen::kPillarRock[i], at, 1.6f, a);
				}
				e.ang[i] += (8.0f + 1.5f * i) * RE::NI_PI / 180.0f * a_dt;
				TurnNode(e.stone[i], e.ang[i], 8.0f);
			}
			const int n = N(gen::kTrial_echo);
			e.t += a_dt;
			const int h = static_cast<int>(hour);
			switch (e.phase) {
			case 0:
				if (h != e.lastHour) {
					e.lastHour = h;
					if (a_fighting && hour < 12.0f && dAltar < 2500.0f) {  // (after midnight, before dawn, and you at the altar)
						e.seq.clear();
						for (int k = 0, prev = -1; k < n; ++k) {
							int j = std::min(5, static_cast<int>(Rand01() * 6.0f));
							if (j == prev) j = (j + 1 + static_cast<int>(Rand01() * 5.0f)) % 6;
							e.seq.push_back(prev = j);
						}
						e.phase = 1, e.idx = 0, e.t = -3.2f, e.touching = -1;  // (a breath for your character's word before the first note)
						Classes::VoiceSay(gen::VoTrigger::kEchoStart, 0.2f);
						Forms::Play(gen::Sound::echo_sing);
						Gun::FxBlast("light", a_c + RE::NiPoint3{ 0, 0, 300.0f });
						logger::info("curse: Repeat the Echo - the altar sings {} notes at {}:00", n, h);
					}
				}
				break;
			case 1:  // the altar sings: a note a second
				if (e.t >= 0.95f) {
					e.t = 0;
					const int j = e.seq[e.idx];
					Gun::FxBlastSized("light", e.at[j] + RE::NiPoint3{ 0, 0, 60.0f }, 1.0f);
					Forms::Play(kTone[j]);
					if (++e.idx >= n) e.phase = 2, e.idx = 0, e.t = 0;
				}
				break;
			case 2:  // your turn: walk into the stones, in order
				{
					int at = -1;
					for (int i = 0; i < 6; ++i) {
						if (std::hypot(pp.x - e.at[i].x, pp.y - e.at[i].y) < 170.0f && std::abs(pp.z - e.at[i].z) < 400.0f) at = i;
					}
					if (e.touching >= 0 && at != e.touching && std::hypot(pp.x - e.at[e.touching].x, pp.y - e.at[e.touching].y) > 230.0f) e.touching = -1;
					if (at >= 0 && at != e.touching) {
						e.touching = at;
						if (at == e.seq[e.idx]) {
							Forms::Play(kTone[at]);
							Gun::FxBlastSized("light", e.at[at] + RE::NiPoint3{ 0, 0, 60.0f }, 1.0f);
							if (++e.idx >= n) {
								Forms::Play(gen::Sound::echo_sing);
								Gun::FxBlastSized("lightmassive", a_c + RE::NiPoint3{ 0, 0, 200.0f }, 1.0f);
								logger::info("curse: Repeat the Echo - answered");
								Classes::VoiceSay(gen::VoTrigger::kEchoDone, 1.5f);
								CompleteTrial(a_z);  // (clears the stones)
								return;
							}
						} else {
							EchoFail(a_c, "a wrong stone");
						}
					} else if (e.t > 30.0f + 4.0f * n) {
						EchoFail(a_c, "too slow");
					}
				}
				break;
			}
			if (!a_fighting) e.phase = 0;
			std::string head, sub;
			if (e.phase == 1) head = "WATCH THE STONES";
			else if (e.phase == 2) head = "TOUCH THEM IN ORDER", sub = fmt::format("{} / {}", e.idx, n);
			else head = hour >= 12.0f ? "THE STONES SING AT MIDNIGHT" : fmt::format("THE STONES SING AT {}:00", h + 1);
			if (dAltar < 4000.0f) g_ring = { true, a_c, gen::kCurseEchoRadius, e.phase == 2 ? static_cast<float>(e.idx) / std::max(1, n) : 0.0f, g_ring.flash, 0, 1, false, head, sub };
		}

		// Break the Priest's Seals: each seal shielded while its guard lives; strike it once he falls. The last calls the priest out.
		void TickSeals(RE::PlayerCharacter* a_p, int a_z, St a_s, bool a_fighting, float a_dt)
		{
			auto& g = g_seals;
			if (g.town != a_z) {
				ClearSeals();
				g.town = a_z;
			}
			if (!a_fighting) {  // (by day the seals are hidden; their guards rise again the next night)
				for (int i = 0; i < 3; ++i) Gone(g.stone[i]), Gone(g.runes[i]), g.guard[i] = {}, g.down[i] = false, g.raised[i] = false;
				return;
			}
			const auto pp = a_p->GetPosition();
			for (int i = 0; i < 3; ++i) {
				if ((a_s.prog >> i) & 1) {
					Gone(g.stone[i]), Gone(g.runes[i]);
					continue;
				}
				const auto& pl = gen::kCurseCities[a_z].seals[i];
				if (!g.snapped[i]) {
					RE::NiPoint3 at{ pl.x, pl.y, pl.z };
					if (pp.GetDistance(at) > 9000.0f) continue;
					if (auto w = Walkable(a_p, at, 350.0f)) at = *w;
					g.at[i] = at, g.snapped[i] = true;
				}
				const auto  at = g.at[i];
				const float d = std::hypot(pp.x - at.x, pp.y - at.y);
				if (!g.stone[i].get() && d < 9000.0f) {
					g.runes[i] = PlaceStat(a_p, gen::kFreedRunes, at + RE::NiPoint3{ 0, 0, 40.0f }, 0.8f, 0.0f);
					g.stone[i] = PlaceStat(a_p, gen::kHeartStone, at + RE::NiPoint3{ 0, 0, 170.0f }, 1.0f, 0.0f);
				}
				g.ang[i] += (g.down[i] ? 40.0f : 14.0f) * RE::NI_PI / 180.0f * a_dt;
				TurnNode(g.runes[i], g.ang[i], 0.0f);
				TurnNode(g.stone[i], -g.ang[i] * 1.5f, 10.0f);
				// its guard: one of the dead's elites, raised at the seal when you come near; the seal opens when he falls
				if (!g.down[i]) {
					auto gd = g.guard[i].get();
					if (gd && gd->IsDead()) {
						g.down[i] = true;
						Forms::Play(gen::Sound::quest_step);
						Gun::FxBlast("light", at + RE::NiPoint3{ 0, 0, 170.0f });
						logger::info("curse: {} - the guard of the seal at {} is down", gen::kTowns[a_z], pl.place);
					} else if (!gd && d < 6000.0f && (g.retry[i] -= a_dt) <= 0) {
						g.retry[i] = 3.0f;
						g.guard[i] = Director::RaiseTrial(at + RE::NiPoint3{ 0, 0, 40.0f }, true);
						if (g.guard[i].get() && !g.raised[i]) g.raised[i] = true, logger::info("curse: {} - a guard rises at the seal at {}", gen::kTowns[a_z], pl.place);
					}
				}
				if ((g.pulse[i] -= a_dt) <= 0) {  // shielded: dark; open: light, calling to be struck
					g.pulse[i] = g.down[i] ? 1.6f : 3.0f;
					if (d < 6000.0f) Gun::FxBlast(g.down[i] ? "light" : "dark", at + RE::NiPoint3{ 0, 0, 170.0f });
				}
				// struck: a blow, a cast or a bow drawn within reach, or walked into
				if (g.down[i] && d < 300.0f && std::abs(pp.z - at.z) < 400.0f) {
					bool strike = d < 140.0f || a_p->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone;
					for (auto src : { RE::MagicSystem::CastingSource::kLeftHand, RE::MagicSystem::CastingSource::kRightHand }) {
						if (auto* mc = a_p->GetMagicCaster(src); mc && (mc->state.get() == RE::MagicCaster::State::kCasting || mc->state.get() == RE::MagicCaster::State::kCharging)) strike = true;
					}
					if (strike) {
						a_s.prog |= 1 << i;
						Gun::FxBlastSized("lightmassive", at + RE::NiPoint3{ 0, 0, 150.0f }, 0.8f);
						Forms::Play(gen::Sound::shout_shatter);
						Gone(g.stone[i]), Gone(g.runes[i]);
						logger::info("curse: {} - the seal at {} is broken ({} of 3)", gen::kTowns[a_z], pl.place, std::popcount(static_cast<unsigned>(a_s.prog & 7)));
						if ((a_s.prog & 7) == 7) {
							g_priestAt = at, g_priestCall = true;  // (Noah: "the last one calls the Dragon Priest out" - at once, here)
							Classes::VoiceSay(gen::VoTrigger::kPriestCalled, 1.2f);
							Put(a_z, a_s);
							CompleteTrial(a_z);
							return;
						}
						Put(a_z, a_s);
						Classes::VoiceSay(gen::VoTrigger::kSealBroken, 1.0f);
					}
				}
			}
		}

	}

	namespace Curse
	{
		int  ZoneTown() { return g_zone; }
		int ClaimableShout(int a_town)
		{
			const int sh = a_town >= 0 ? ShoutOf(a_town) : -1;
			// (Noah: "when I freed the altar I didn't see an option to retrieve the shout") no story gate: a freed city's shout waits at its altar at once
			return sh >= 0 && Get(a_town).freed && !Gun::KnowsShout(sh) ? sh : -1;
		}
		bool  SafeHere(RE::PlayerCharacter*) { return g_zone >= 0 && Get(g_zone).freed; }
		float SpawnBoost() { return 1.0f; }  // (the Ward's swarm went with the old trials)
		int   Steps(int a_town) { return a_town >= 0 && a_town < static_cast<int>(gen::kCurseCities.size()) ? Step(a_town) : 0; }

		void StartTrial(int a_town, int a_trial)
		{
			if (a_town < 0 || a_town >= static_cast<int>(gen::kCurseCities.size()) || a_trial < 0 || a_trial >= kTrialCount) {
				return;
			}
			St s = Get(a_town);
			const std::string title(Title(a_trial));
			if (s.freed) {
				Hud::QuestBanner("The Echo Altar of " + std::string(gen::kTowns[a_town]) + " is awake", 2.5f);
				return;
			}
			if ((s.done >> a_trial) & 1) {
				Hud::QuestBanner(title + " is done", 2.5f);
				return;
			}
			if (std::string why; !Story::TrialsOpen(a_town, &why)) {
				Hud::Banner(why.empty() ? std::string("The altar sleeps until the story moves on") : why, 4.0f);
				return;
			}
			if (s.Boss() || s.Count() >= kNeeded) {
				Hud::Banner("The four trials are done - " + std::string(BossName(a_town)) + " must fall", 3.0f);
				return;
			}
			if (s.Trial() == a_trial) {
				Hud::QuestBanner(title + " is under way", 2.5f);
				if (a_trial == gen::kTrial_whisper && s.prog == 0) SayRiddle(a_town);  // (the riddle again, for the asking)
				return;
			}
			// (Noah: "fixed order") only the first not yet won can be taken up
			if (const int next = NextTrial(s); a_trial != next) {
				Hud::Banner("First: " + std::string(Title(next)), 3.0f);
				return;
			}
			s.active = a_trial + 1, s.prog = 0;
			Put(a_town, s);
			ClearRun();
			Forms::Play(gen::Sound::quest_start);
			Hud::QuestBanner(Upper(title) + ": " + Ask(a_trial) + (Director::Fighting() ? "" : " (at night)"), 6.0f);
			if (a_trial == gen::kTrial_whisper) SayRiddle(a_town), Classes::VoiceSay(gen::VoTrigger::kWhisperStart, 12.0f);  // (after the riddle's subtitle)
			if (a_trial == gen::kTrial_restless) Classes::VoiceSay(gen::VoTrigger::kRestlessStart, 1.0f);
			logger::info("curse: {} trial {} taken up (difficulty {})", gen::kTowns[a_town], gen::kTrials[a_trial].id, Diff());
		}

		AltarView Altar(int a_town)
		{
			AltarView v;
			if (a_town < 0 || a_town >= static_cast<int>(gen::kCurseCities.size())) {
				return v;
			}
			const St s = Get(a_town);
			v.on = !s.freed;
			v.done = s.done, v.active = s.active, v.next = NextTrial(s);
			for (int i = 0; i < kTrialCount; ++i) {
				v.desc[i] = Ask(i, a_town);
			}
			v.boss = std::string(BossName(a_town));
			v.bossLine = s.Boss() ? std::string(Director::ChampionActive(a_town) ? "Out in the city - slay it to wake the altar" : "Called by the broken seals - it comes for you at night") : fmt::format("Comes when the last seal breaks ({} of {} trials won)", s.Count(), kNeeded);
			return v;
		}

		void NoteHit(RE::FormID a_target, bool a_melee)
		{
			const auto now = std::chrono::steady_clock::now();
			if (g_lastBlow.size() > 256) {
				std::erase_if(g_lastBlow, [&](const auto& e) { return now - e.second.first > std::chrono::seconds(30); });
			}
			g_lastBlow[a_target] = { now, a_melee };
		}

		void Reset()
		{
			ClearRun();
			g_zone = -1, g_zoneTimer = 0, g_normDelay = 3.0f;
			g_journalFresh = true, g_marker = {};
			g_lastBlow.clear();
		}

		void OnUndeadDeath(RE::Actor* a_actor)
		{
			Echo::OnUndeadDeath(a_actor);  // (a barrow that wants the dead: stage 2's hunt)
			Eggs::OnUndeadDeath(a_actor);  // (a hidden quest's marked ground)
			if (!a_actor || !Director::Fighting() || g_zone < 0) {
				return;
			}
			const int z = g_zone;
			St s = Get(z);
			const int tr = s.freed ? -1 : s.Trial();
			const auto at = a_actor->GetPosition();
			if (tr == gen::kTrial_restless) {
				// (Noah: "kill the risen inside the altar's light so their souls pass to Sovngarde, with wisps rising")
				RE::NiPoint3 c{};
				float yaw = 0;
				RE::TESWorldSpace* ws = nullptr;
				auto* p = RE::PlayerCharacter::GetSingleton();
				if (!p || !Shops::StandFrame(z, c, yaw, ws) || ws != p->GetWorldspace() || std::hypot(at.x - c.x, at.y - c.y) > gen::kCurseLight) return;
				// the soul goes home: a pale blue wisp out of the body and across into the altar's centre stone
				RE::NiPoint3 stone = c + RE::NiPoint3{ 0, 0, 170.0f };
				if (auto* alt = Shops::Placed(z, 0)) stone = alt->GetPosition() + RE::NiPoint3{ 0, 0, 170.0f };
				SendSoul(at, stone);
				if (g_wispT <= 0) Audio::AtPoint(gen::Sound::wisp_rise, at, 0.0f, 0.6f), g_wispT = 0.7f;
				g_ring.flash = 0;
				if (++s.prog >= N(tr)) {
					CompleteTrial(z);
				} else {
					Put(z, s);
				}
			} else if (tr == gen::kTrial_whisper && s.prog >= 1) {
				const auto w = WhisperAt(z);
				if (std::hypot(at.x - w.x, at.y - w.y) > gen::kCurseWhisperRadius) return;
				if (++s.prog - 1 >= N(tr)) {
					CompleteTrial(z);
				} else {
					Put(z, s);
				}
			}
		}

		std::string TrialTitle(int a_trial) { return a_trial >= 0 && a_trial < kTrialCount ? std::string(Title(a_trial)) : std::string(); }
		void        OnClassKill(int, int, float) {}  // (the class trials went with the old trials: Noah, 2026-11-23)

		// the night's waves (Director): each wave begun is counted (the old trials counted whole waves; none do now)
		void OnWave(bool a_cleared)
		{
			if (!a_cleared) ++g_waveSerial;
		}

		void OnBossSlain() {}  // (the old Herald: the Dragon Priests are the cities' champions now)

		// (Noah: killing the Dragon Priest freed the shrine at once - it isn't freed until every wave of that night is slain)
		// the Jarl's fall is held; the city is freed the moment the night's last wave falls (OnNightCleared)
		int g_pendingLift = -1;
		void OnChampionSlain(int a_town)
		{
			if (a_town >= 0 && Get(a_town).Boss()) {
				Forms::Play(gen::Sound::quest_step);
				g_pendingLift = a_town;
				Kit::OnPriestSlain();  // (Noah: the Dragon Priest's relic)
				Classes::VoiceSay(gen::VoTrigger::kPriestSlain, 1.5f);
				Kit::PriestDrop(a_town);  // (Noah: his weapon - to you, and to everyone in the party)
				Net::PriestSlain(a_town);
				// (the audit: the kill lived only in memory - a save and load raised the Priest again) kept in the city's own
				// value: the Boss state with its progress 1 means "fallen, the night's last wave still to go"
				St fs = Get(a_town);
				fs.prog = 1;
				Put(a_town, fs);
				Hud::QuestBanner(Upper(std::string(BossName(a_town))) + " FALLS - " + Upper(std::string(gen::kTowns[a_town])) + " IS FREE", 5.0f);
				logger::info("curse: {}'s Dragon Priest slain - the night ends, the city is freed", gen::kTowns[a_town]);
			}
		}
		bool PriestWaits()
		{
			if (g_zone < 0) return false;
			const St s = Get(g_zone);
			RE::NiPoint3 c; float yaw = 0; RE::TESWorldSpace* ws = nullptr;
			auto* p = RE::PlayerCharacter::GetSingleton();
			return !s.freed && s.Boss() && s.prog == 0 && p && Shops::StandFrame(g_zone, c, yaw, ws) && ws == p->GetWorldspace();
		}
		void OnNightCleared()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return;
			for (int t = 0; t < static_cast<int>(gen::kCurseCities.size()); ++t) {
				if (const St st = Get(t); st.Boss() && (st.prog >= 1 || t == g_pendingLift)) Lift(player, t);
			}
			g_pendingLift = -1;
		}
		void ForgetPendingLift() { g_pendingLift = -1; }

		int  Raw(int a_town) { return a_town >= 0 && a_town < static_cast<int>(gen::kCurseCities.size()) ? Value(a_town) : 0; }
		void SetRaw(int a_town, int a_value)
		{
			if (a_town < 0 || a_town >= static_cast<int>(gen::kCurseCities.size()) || a_value < kBase) {
				return;  // (an older party member's numbers mean nothing here)
			}
			const St before = Get(a_town);
			// (Noah, 2026-11-23: the trials remade - "shared") every trial and its progress come over from the party: the
			// trials won, the one under way (its count, the seals broken), the Dragon Priest's call and the freeing
			{
				const St in = Decode(a_value);
				if (before.freed) return;
				St mine = before;
				if (in.freed) {
					mine.freed = true;
				} else if (in.Boss() && !before.Boss()) {
					mine.done |= in.done & kSharedMask;
					mine.active = kBossActive, mine.prog = 0;
				} else {
					mine.done |= in.done & kSharedMask;  // (the trials a friend won)
					const int it = in.Trial();
					if (it >= 0 && !((mine.done >> it) & 1) && !mine.Boss()) {  // (the trial under way: its count is the party's)
						if (mine.active == in.active) mine.prog = it == gen::kTrial_seals ? (mine.prog | in.prog) : std::max(mine.prog, in.prog);
						else if (mine.Trial() < it) mine.active = in.active, mine.prog = in.prog;  // (the party is further on)
					}
					if (mine.Trial() >= 0 && ((mine.done >> mine.Trial()) & 1)) {  // (won by a friend: on to the next, or the Dragon Priest)
						const int next = NextTrial(mine);
						mine.active = next < kTrialCount ? next + 1 : kBossActive, mine.prog = 0;
					}
				}
				if (Encode(mine) == Encode(before)) return;
				SetValue(a_town, Encode(mine));
			}
			const St after = Get(a_town);
			// a friend did a trial: it completes here too, with its reward (the party shares the quests)
			for (int i = 0; i < kTrialCount && !after.freed; ++i) {
				if (((after.done >> i) & 1) && !((before.done >> i) & 1)) {
					if (g_zone == a_town) g_mine[a_town] |= static_cast<std::uint8_t>(1 << i);  // (you were there: it is yours too)
					Forms::Play(gen::Sound::quest_step);
					Hud::QuestBanner(Upper(std::string(Title(i))) + " - COMPLETE (your party)", 5.0f);
					Points::Add(500 * after.Count());
					logger::info("curse: {} trial {} done by the party", gen::kTowns[a_town], gen::kTrials[i].id);
				}
			}
			if (after.active != before.active || after.done != before.done) {
				ClearRun();
			}
			if (!before.freed && after.freed) {
				Forms::Play(gen::Sound::curse_lifted);
				Shops::FreedPulse(a_town);  // (the altar's freeing: the charge, the burst, the runes grow - for the party too)
				Audio::FreedMusic();
				std::string text = Fill(gen::kCurseLiftedBanner, a_town);
				if (const int sh = ShoutOf(a_town); sh >= 0) {
					text += " - its Thu'um, " + std::string(gen::kShouts[sh].label) + ", waits at the city's altar";
				}
				Note(text, 10.0f);
				Points::Add(5000);
				Party::CityFreed(a_town);
				if (!Net::IsClient()) {  // the host's nights: the light runs out over the fallen (the night goes on to its last wave)
					if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
						auto* shrine = Shops::Placed(a_town, 0);
						Director::LiftWave(shrine ? shrine->GetPosition() : pl->GetPosition());
					}
				}
				logger::info("curse: {} freed by the party", gen::kTowns[a_town]);
			}
		}

		void DevSet(int a_town, int a_value)
		{
			if (a_town < 0 || a_town >= static_cast<int>(gen::kCurseCities.size())) return;
			SetValue(a_town, a_value);
			logger::info("dev: curse {} set to {}", gen::kTowns[a_town], a_value);
		}
		bool Freed(int a_town) { return a_town >= 0 && a_town < static_cast<int>(gen::kCurseCities.size()) && Get(a_town).freed; }
		std::uint32_t CreditBits()
		{
			std::uint32_t b = 0;
			for (int t = 0; t < 10; ++t) b |= static_cast<std::uint32_t>(std::min(7, Mine(t))) << (3 * t);
			return b;
		}
		std::vector<std::uint8_t> MineState() { return { g_mine.begin(), g_mine.end() }; }
		void SetMineState(const std::vector<std::uint8_t>& a_s)
		{
			g_mine.fill(0);
			for (std::size_t i = 0; i < a_s.size() && i < g_mine.size(); ++i) g_mine[i] = a_s[i];
		}

		void NewGamePlus()
		{
			for (int t = 0; t < static_cast<int>(gen::kCurseCities.size()); ++t) {
				SetValue(t, kBase);
				Gun::Console("resetquest UN_Rites_" + std::string(gen::kTowns[t]));
			}
			Save::Set(gen::kAV_round, 0);
			g_mine.fill(0);
			logger::info("curse: begun again - every city under the curse, night 1 next");
		}

		void MigrateRites()
		{
			for (int t = 0; t < static_cast<int>(gen::kCurseCities.size()); ++t) {
				const int v = Value(t);
				if (v > 0 && v < kOldBase) {  // (the four rites: under the old trials' values)
					SetValue(t, v / 1000 >= 4 ? kFreedValue : kBase);
					if (v / 1000 < 4) {
						Gun::Console("resetquest UN_Rites_" + std::string(gen::kTowns[t]));
					}
				}
			}
			logger::info("curse: a four-rite save moved to the trials");
		}

		// ---- the journal: each city's altar as a quest in Skyrim's Quests tab (tools/gen_esp.py) ----
		// A city's quest begins the first time you walk into it. Stage 10 is the altar's call, 20+i a trial taken up,
		// 100 the Dragon Priest, 200 freed. Objectives: 0 choose a trial, 1-5 the trials, 6 the Dragon Priest - shown, hidden and
		// ticked to follow the city's progress. Its Target alias (the compass and map marker) sits on an unseen marker
		// moved to wherever the trial wants you.
		RE::TESQuest* RiteQuest(int a_town)
		{
			return a_town >= 0 && a_town < static_cast<int>(gen::kRiteQuests.size()) ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(gen::kRiteQuests[a_town], gen::kPlugin) : nullptr;
		}
		void Journal(RE::PlayerCharacter* a_player)
		{
			if (g_journalFresh) {
				g_journalKey.fill(-1);
				g_journalFresh = false;
			}
			for (int t = 0; t < static_cast<int>(gen::kRiteQuests.size()); ++t) {
				auto* q = RiteQuest(t);
				if (!q) {
					continue;
				}
				const std::string edid = "UN_Rites_" + std::string(gen::kTowns[t]);
				const St  s = Decode(Value(t));
				const int v = s.freed ? kFreedValue : Encode(St{ false, s.done, s.active, 0 });  // (its state, not each kill or second: no churn)
				if (!q->IsEnabled() && !q->IsCompleted()) {
					if (t != g_zone && !s.freed && s.done == 0 && s.active == 0) {
						continue;  // not been there yet
					}
					// (the audit: a quest that won't start was asked again every second, fights and all - and starting one
					// waits on its aliases) three tries a town a session, never in the night's fighting
					static std::array<int, 32> tries{};
					if (t >= static_cast<int>(tries.size()) || tries[t] >= 3 || Director::NightNow()) continue;
					++tries[t];
					Gun::Console("startquest " + edid);
					g_journalKey[t] = -1;
					continue;  // (the stages follow once it runs)
				}
				if (g_journalKey[t] == v || q->IsCompleted()) {
					continue;
				}
				// (Noah: "no check mark until it's completed") each objective's state set on the quest itself: shown and open,
				// shown and ticked, or hidden
				auto shown = [&](int i, bool on, bool done) {
					for (auto* o : q->objectives) {
						if (!o || o->index != i) continue;
						o->state = !on ? RE::QUEST_OBJECTIVE_STATE::kDormant : done ? RE::QUEST_OBJECTIVE_STATE::kCompletedDisplayed : RE::QUEST_OBJECTIVE_STATE::kDisplayed;
					}
				};
				Gun::Console(fmt::format("setstage {} 10", edid));
				if (s.freed) {
					shown(0, false, false);
					shown(kTrialCount + 1, true, true);
					Gun::Console(fmt::format("setstage {} 200", edid));
				} else {
					if (s.Trial() >= 0) {
						Gun::Console(fmt::format("setstage {} {}", edid, 20 + s.Trial()));
					} else if (s.Boss()) {
						Gun::Console(fmt::format("setstage {} 100", edid));
					}
					shown(0, s.active == 0 && s.Count() < kNeeded, false);
					for (int i = 0; i < kTrialCount; ++i) {
						const bool done = (s.done >> i) & 1;
						// (Noah: every trial listed from the start - open until done, ticked once done)
						shown(1 + i, true, done);
					}
					shown(kTrialCount + 1, s.Boss(), false);
				}
				g_journalKey[t] = v;
				logger::info("journal: {} - {} trials done, active {}", gen::kTowns[t], s.Count(), s.active);
			}
			// the marker: where the quest wants you, for the city you're in
			RE::NiPoint3 at;
			std::string  label;
			auto*        q = RiteQuest(g_zone);
			if (q && q->IsEnabled() && !q->IsCompleted() && ObjectiveTarget(a_player, at, label)) {
				auto m = g_marker.get();
				if (m && (m->GetWorldspace() != a_player->GetWorldspace() || m->GetParentCell() == nullptr)) {
					// (the audit: placed once near Riverwood, it was only slid to Whiterun's coordinates, still in Tamriel)
					m->Disable(), m->SetDelete(true);
					g_marker = {}, m = nullptr;
				}
				if (!m) {
					if (auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x00003B)) {  // XMarker
						if (auto r = a_player->PlaceObjectAtMe(xm, false)) {
							g_marker = r->CreateRefHandle();
							m = r;
						}
					}
				}
				if (m) {
					if (m->GetPosition().GetDistance(at) > 50.0f) {
						m->SetPosition(at);
					}
					q->ForceRefIntoAlias(0, m.get());
				}
			}
		}

		void Tick(float a_delta)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Is3DLoaded()) {
				return;
			}
			if ((g_zoneTimer -= a_delta) <= 0) {
				g_zoneTimer = 0.5f;
				const int z = ComputeZone(player);
				if (z != g_zone) {
					logger::info("curse: entered {}", z >= 0 ? std::string(gen::kTowns[z]) : std::string("the wilds"));
				}
				g_zone = z;
				// the four trials won (a friend's seals, say) with no Dragon Priest called yet: he is called now
				if (z >= 0) {
					if (St cs = Get(z); !cs.freed && cs.active == 0 && PriestReady(z, cs)) {
						cs.active = kBossActive;
						Put(z, cs);
						Forms::Play(gen::Sound::quest_step);
						logger::info("curse: {} - the four trials are won: the Dragon Priest", gen::kTowns[z]);
					}
				}
				Arcana::RefreshPower();  // (the spells grow with the altars woken)
				// saves from the ten-rite cities: a freed city stays freed, the rest begin the trials
				if (g_normDelay > 0) {
					g_normDelay -= 0.5f;
				} else {
					for (int t = 0; t < static_cast<int>(gen::kCurseCities.size()); ++t) {
						if (const int v = Value(t); v >= kOldBase && v < kBase) {
							// (Noah, 2026-11-23: "start fresh") a city part-way through the old trials begins the new ones
							SetValue(t, kBase);
							if (t < 16) g_mine[t] = 0;
							Gun::Console("resetquest UN_Rites_" + std::string(gen::kTowns[t]));
							logger::info("curse: {} begins the new trials (it was part-way through the old ones)", gen::kTowns[t]);
						} else if (v < kOldBase) {
							SetValue(t, v / 1000 >= 10 ? kFreedValue : kBase);
							if (v > 0 && v / 1000 < 10) {
								Gun::Console("resetquest UN_Rites_" + std::string(gen::kTowns[t]));
								logger::info("curse: {} moved from the rites to the trials", gen::kTowns[t]);
							}
						}
					}
				}
			}
			const bool fighting = Director::Fighting();
			Publish(g_zone, fighting);
			PublishObjective(player);
			static float journal = 0;
			if ((journal -= a_delta) <= 0) {
				journal = 1.0f;
				Journal(player);
			}
			// (Noah: no Word Walls) a freed altar whose Thu'um you haven't learned calls to you as a Word Wall does: its
			// heart-stone whispers while you're near it
			{
				static RE::BSSoundHandle whisper;
				static float             check = 0;
				static bool              nearAltar = false;
				if ((check -= a_delta) <= 0) {
					check = 0.5f;
					nearAltar = false;
					RE::NiPoint3        at;
					float               yaw = 0;
					RE::TESWorldSpace*  ws = nullptr;
					if (g_zone >= 0 && ClaimableShout(g_zone) >= 0 && Shops::StandFrame(g_zone, at, yaw, ws) && player->GetWorldspace() == ws) {
						nearAltar = player->GetPosition().GetDistance(at) < 1400.0f;
					}
				}
				if (nearAltar && !whisper.IsValid()) {
					auto* d = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x00021705);  // QSTWordWallWhisper2DLPSD (a loop)
					if (auto* am = RE::BSAudioManager::GetSingleton(); d && am && am->GetSoundHandle(whisper, static_cast<RE::BSISoundDescriptor*>(d))) {
						whisper.SetVolume(0.5f);
						whisper.Play();
					}
				} else if (!nearAltar && whisper.IsValid()) {
					whisper.FadeOutAndRelease(1200);
				}
			}
			// The prologue: as soon as the new character is made (out of Helgen, at Riverwood), the books are in the pack
			if (Save::Flag(Save::kFlagStarted)) {
				if (auto* intro = Global(gen::kCurseIntroGlobal); intro && intro->value < 1.5f) {  // (1 = an older save saw the pop-up: it gets the books too)
					const bool fresh = intro->value < 0.5f && player->GetLevel() <= 1 && Intro::NewGameThisSession();  // a character made just now (never on Continue or Load)
					intro->value = 2.0f;
					if (fresh) {
						Hud::IntroStart();
					}
					GiveBook(player);
					Forms::Play(gen::Sound::quest_start);
					Note("The Curse of Alduin is in your pack (Books, or F1): the lore, and every class's path as a tab", 8.0f);
					return;
				}
			}
			const int z = g_zone;
			const St  s = z >= 0 ? Get(z) : St{ true };
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().wardFrac = 0;
			}
			g_wispT = std::max(0.0f, g_wispT - a_delta);
			g_ring.on = false;
			RE::NiPoint3       c;
			float              yaw = 0;
			RE::TESWorldSpace* ws = nullptr;
			const bool         here = z >= 0 && Shops::StandFrame(z, c, yaw, ws) && ws == player->GetWorldspace();
			const int          tr = here && !s.freed ? s.Trial() : -1;
			// a trial's pieces belong to the trial under way in the city you're in: anything else is cleared
			if (tr != gen::kTrial_echo && g_echo.town >= 0) ClearEcho();
			if (tr != gen::kTrial_seals && g_seals.town >= 0) ClearSeals();
			if (here) {
				static float sweep = 0;
				if ((sweep -= a_delta) <= 0) sweep = 3.0f, SweepStrays(player, c);
			}
			if (tr == gen::kTrial_restless || tr == gen::kTrial_echo) {
				Audio::LoopStart(Audio::kLoopRite, gen::Sound::rite_hum, Shops::Placed(z, 0));
			} else {
				Audio::LoopStop(Audio::kLoopRite, 800);
			}
			switch (tr) {
			case gen::kTrial_echo:  // (the stones stand by day too: the song is at night)
				TickEcho(player, z, c, yaw, fighting, a_delta);
				break;
			case gen::kTrial_restless:
				if (fighting) TickRestless(player, z, s, c);
				break;
			case gen::kTrial_whisper:
				TickWhisper(player, z, s, c, fighting, a_delta);
				if (!fighting) g_ring.on = false;
				break;
			case gen::kTrial_seals:
				TickSeals(player, z, s, fighting, a_delta);
				break;
			default:
				// (Noah: "the last seal calls the Dragon Priest out" - at once, where it broke; after a load, or if another champion
				// held the night, anywhere out in the city while the night lasts, until he falls)
				if (here && fighting && s.Boss() && s.prog == 0 && !Director::ChampionActive(z)) {
					static float retry = 0;
					if ((retry -= a_delta) <= 0) {
						retry = 4.0f;
						std::optional<RE::NiPoint3> at;
						if (g_priestCall) at = Walkable(player, g_priestAt, 600.0f);
						if (!at) {
							const float a = Rand01() * 6.2832f, r = 1500.0f + 700.0f * Rand01();
							at = Walkable(player, c + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, 800.0f);
						}
						const auto& b = gen::kCityBosses[z];
						if (at && Director::RaiseChampion(z, b.row, b.name, b.health * kBossHealth[Diff()], *at + RE::NiPoint3{ 0, 0, 40.0f })) {
							g_priestCall = false;
							logger::info("curse: {} - {} comes out", gen::kTowns[z], b.name);
						}
					}
				}
				break;
			}
			PublishRing(a_delta);
		}
	}
}
