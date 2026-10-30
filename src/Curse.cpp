#include "BO3Z.h"

// The Curse of Alduin (sheet: curse). At Helgen, Alduin shouted SLEN TIID VO (his raising shout) into the earth and every barrow in
// Skyrim answered; the Echo Altars the Tongues raised in every city have woken only half. Each altar sets five trials
// (curse.trials); the player takes up any three, in any order, at the altar's menu:
//   ward      Ward of the Tongues: stand in the runic circle round the altar while the dead come faster (leaving pauses it)
//   culling   The Culling: n kills in the city before the clock runs out (or the count starts again)
//   unbroken  The Unbroken: n kills without falling below half health (or the count starts again)
//   steel     Trial of Steel: n melee kills
//   breath    The Long Night's Breath: n waves round the altar, each stronger; clear each before its clock or it doubles
// When three are done the city's Dragon Priest (curse.bosses) rises near the altar at night; slay it and the city is freed
// (and the night ends): a sanctuary where no undead rise, its shout waiting at the altar. Counts, clocks, the ward's
// swarm, the waves' elites and the Dragon Priest's health follow Skyrim's difficulty (Novice..Legendary). Trials run at night.
// Progress: one global per city, 1000000 + doneBits*100000 + active*10000 + progress (active 0 none, 1-5 a trial, 6 the
// Dragon Priest), 9000000 freed. Every step only grows the value, so the party's sync (the biggest wins) carries it.
namespace bo3z
{
	namespace
	{
		constexpr int kTrialCount = static_cast<int>(gen::kTrials.size());
		constexpr int kNeeded = 4;            // trials to take before the Dragon Priest (the classes overhaul: the three shared and your class's)
		constexpr int kFreed = kNeeded + 1;   // steps: three trials and the Dragon Priest
		constexpr int kBase = 1'000'000, kFreedValue = 9'000'000, kBossActive = 6;
		constexpr float kBreathRange = 3000.0f;  // the Long Night's Breath: how near the altar you hold it
		static_assert(kTrialCount == 5);

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
			int  Count() const { return std::popcount(static_cast<unsigned>(done & 31)); }
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
				s.done = (r / 100000) & 31, s.active = (r / 10000) % 10, s.prog = r % 10000;
			}
			return s;
		}
		int Encode(const St& s) { return s.freed ? kFreedValue : kBase + (s.done & 31) * 100000 + std::clamp(s.active, 0, 9) * 10000 + std::clamp(s.prog, 0, 9999); }
		St   Get(int a_town) { return Decode(Value(a_town)); }
		void Put(int a_town, const St& s) { SetValue(a_town, Encode(s)); }
		int  Step(int a_town)
		{
			const St s = Get(a_town);
			return s.freed ? kFreed : s.Count();
		}

		// (Noah: in a party each player takes three trials at an altar, then the night's waves are finished together) the
		// trials you were there for, per city - your own, whoever's game counted the last step (co-save 'MINE')
		std::array<std::uint8_t, 16> g_mine{};
		// (Noah: the three shared trials with the party's help, the class trial each player's own) the shared ones' bits and
		// progress come over from the party; the class trial (the steel slot) never does: each player wins their own
		constexpr int kSharedMask = 31 & ~(1 << gen::kTrial_steel);
		bool OwnClassTrial(int a_town) { return a_town >= 0 && a_town < 16 && ((g_mine[a_town] >> gen::kTrial_steel) & 1); }
		int  Mine(int a_town) { return OwnClassTrial(a_town) ? 1 : 0; }  // (told to the party: your class trial won here)
		bool InParty() { return Party::FriendCount() > 0; }
		bool SharedDone(const St& a_s)
		{
			for (int t : { gen::kTrial_ward, gen::kTrial_culling, gen::kTrial_breath }) if (!((a_s.done >> t) & 1)) return false;
			return true;
		}
		// the Dragon Priest may rise: alone, the four trials; in a party, the three shared ones (anyone's work) and every
		// player's own class trial
		// (Noah: "the Dragon Priests shouldn't wait for a Skyrim quest - they spawn as soon as all the trials are done: the
		// next wave if mid-night, the next night if it was the last wave") each wave that begins is counted; a priest whose
		// trials were finished during wave N rises from wave N+1 on (the next night's first, after a last wave or by day)
		int                 g_waveSerial = 0;
		std::array<int, 16> g_priestAfter = [] { std::array<int, 16> a{}; a.fill(-1); return a; }();
		bool PriestDue(int a_town) { return a_town < 0 || a_town >= 16 || g_waveSerial > g_priestAfter[a_town]; }
		bool PriestReady(int a_town, const St& a_s, std::string* a_wait = nullptr)
		{
			if (!InParty()) return a_s.Count() >= kNeeded;
			std::string names;
			const int short_ = Party::FriendsShort(a_town, 1, &names);
			if (!OwnClassTrial(a_town)) names = std::string("you") + (names.empty() ? "" : ", " + names);
			if (a_wait) *a_wait = names;
			return SharedDone(a_s) && OwnClassTrial(a_town) && short_ == 0;
		}

		// Skyrim's difficulty picks each trial's numbers
		int Diff() { return std::clamp(Settings::Difficulty(), 0, 5); }
		int N(int a_trial) { return gen::kTrials[a_trial].n[Diff()]; }
		int Secs(int a_trial) { return gen::kTrials[a_trial].seconds[Diff()]; }
		constexpr float kWardSwarm[6] = { 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 3.0f };      // the dead come this much faster in the circle
		constexpr int   kWaveBase[6] = { 4, 5, 6, 7, 8, 10 };                          // the first wave (each after: +2)
		constexpr float kBossHealth[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };       // the Dragon Priest's health (the difficulty is applied once: Settings::UndeadHealthMult)
		[[maybe_unused]] int Elites(int a_wave, int a_waves)  // Thanes of the Mound and Bone Jotnar in a wave
		{
			const int d = Diff();
			if (d >= 3) {
				return a_wave >= 1 ? 1 + (d - 3) + (a_wave + 1 == a_waves ? 1 : 0) : 0;
			}
			return d == 2 && a_wave + 1 == a_waves ? 1 : 0;
		}

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
		[[maybe_unused]] std::string Clock(float a_seconds)
		{
			const int t = std::max(0, static_cast<int>(std::ceil(a_seconds)));
			return fmt::format("{}:{:02}", t / 60, t % 60);
		}
		// (the classes overhaul) the steel slot is the class trial: its name and goal follow the class of whoever takes it
		constexpr const char* kClassTrial[4] = { "Pact of the Host", "Trial of the Elements", "Trial of the Long Shot", "Trial of Steel" };
		std::string_view Title(int a_trial)
		{
			if (a_trial == gen::kTrial_steel) {
				const int c = Classes::Get();
				return c >= 0 && c < 4 ? kClassTrial[c] : "Your Class's Trial";
			}
			return gen::kTrials[a_trial].title;
		}
		constexpr float kLongShot = 2100.0f;  // 30 m
		std::uint32_t   g_elementsUsed = 0;   // Trial of the Elements: the elements that have killed (this run)
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
		bool  g_holding = false;
		float g_holdAcc = 0;
		// the trial under way (not saved: a load starts its clock or wave again)
		int   g_clockKey = -1;               // town*10+active the clock belongs to
		float g_clock = 0;                   // the Culling's seconds left, or this wave's
		bool  g_belowHalf = false;           // the Unbroken: under half health now
		bool  g_waveBroken = false;          // the Unbroken: this wave fell below half (it won't count)
		std::vector<RE::ActorHandle> g_wave; // the Long Night's Breath: this wave's dead
		int   g_waveKey = -1;                // town*100+wave they belong to
		int   g_waveSize = 0, g_waveDoubled = 0, g_waveLeft = 0;
		float g_waveRest = 0, g_waveRetry = 0;
		std::unordered_map<RE::FormID, std::pair<std::chrono::steady_clock::time_point, bool>> g_lastBlow;  // Trial of Steel
		RE::ObjectRefHandle g_probe;

		void ClearRun()
		{
			g_clockKey = -1, g_clock = 0, g_belowHalf = false;
			g_wave.clear(), g_waveKey = -1, g_waveSize = 0, g_waveDoubled = 0, g_waveLeft = 0, g_waveRest = 0, g_waveRetry = 0;
			g_holding = false, g_holdAcc = 0;
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

		float HealthFrac(RE::PlayerCharacter* a_player)
		{
			auto*       av = a_player->AsActorValueOwner();
			const float mx = std::max(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth));
			return av->GetActorValue(RE::ActorValue::kHealth) / mx;
		}

		// The city's shout (sheet: shouts): what freeing it earns, claimed at the Echo Altar.
		// (Noah: "remove the shouts from the echo alter rewards but keep the dance of death shout and dragons updraft") no
		// altar teaches a shout now: Whiterun's Dragon's Updraft comes with the dragon (src/Movement.cpp EnsureUpdraft) and
		// Solitude's Dance of Death from its bards (src/Director.cpp TickDanceShout), both the moment the city is freed
		int ShoutOf(int)
		{
			return -1;
		}

		int Relic(int a_town)
		{
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (gen::kGuns[i].relicTown == a_town && !gen::kGuns[i].retired) {
					return i;
				}
			}
			return -1;
		}

		// what each trial asks tonight, in a line (the altar's menu and the trial's HUD line)
		std::string Ask(int a_trial)
		{
			const int n = N(a_trial), s = Secs(a_trial);
			switch (a_trial) {
			case gen::kTrial_ward: return fmt::format("Hold the runic circle {} seconds", s);
			case gen::kTrial_culling: return fmt::format("{} kills in the city", n);
			case gen::kTrial_unbroken: return fmt::format("{} {} cleared, never below half health", n, n == 1 ? "wave" : "waves");
			case gen::kTrial_steel:
				switch (Classes::Get()) {
				case 0: return fmt::format("{} kills by your summons", n * 2);
				case 1: return fmt::format("{} kills with Destruction spells, of two elements or more", n * 2);
				case 2: return fmt::format("{} bow or crossbow kills from 30 m or more", n);
				case 3: return fmt::format("{} melee kills with a spell in your other hand", n);
				default: return "Choose a class first";
				}
			case gen::kTrial_breath: return fmt::format("{} {} held at the altar{}", n, n == 1 ? "wave" : "waves", Diff() >= 4 ? ", never below half health" : "");
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
			if (all) {
				// (the story audit: it pointed to the Echo while the dragons still flew)
				Note(Echo::DragonsSlain() ? std::string(gen::kCurseFreedAll)
				                          : std::string("Every Echo Altar in Skyrim is awake. Alduin's three dragons still fly - silence them, and his echo will wait for you on the Throat of the World."),
					12.0f);
			}
		}

		// a trial done: three of them call the Dragon Priest
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
			std::string wait;
			s.active = PriestReady(a_town, s, &wait) ? kBossActive : 0;
			if (s.Boss() && a_town >= 0 && a_town < 16) g_priestAfter[a_town] = g_waveSerial;  // (it rises from the next wave)
			Put(a_town, s);
			ClearRun();
			Forms::Play(gen::Sound::quest_step);
			Points::Add(500 * s.Count());
			const std::string head = Upper(std::string(Title(tr))) + " - COMPLETE";
			if (s.Boss()) {
				Hud::Banner(head + "   " + std::string(BossName(a_town)) + (Director::Fighting() ? " rises with the next wave" : " rises when night falls"), 7.0f);
			} else if (InParty()) {
				Hud::Banner(head + (SharedDone(s) ? std::string("   the party's three trials are done") : std::string()) + (wait.empty() ? std::string() : "   class trials still to win: " + wait), 6.0f);
			} else {
				Hud::Banner(head + "   (" + std::to_string(s.Count()) + " of " + std::to_string(kNeeded) + ")", 5.0f);
			}
			logger::info("curse: {} trial {} done ({} of {}, yours {})", gen::kTowns[a_town], gen::kTrials[tr].id, s.Count(), kNeeded, Mine(a_town));
		}
		// the count starts again
		void Broken(int a_town, std::string_view a_why)
		{
			St s = Get(a_town);
			if (s.prog > 0) {
				s.prog = 0;
				Put(a_town, s);
				Forms::Play(gen::Sound::rite_reset);
				Hud::Banner(std::string(a_why), 3.5f);
			}
		}

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
				const int tr = s.Trial();  // (Noah: the ping stays over the altar for every step - it doesn't chase the dead)
				if (Shops::StationTarget(z, a_at)) {
					a_label = s.Boss() ? std::string(BossName(z)) : tr == gen::kTrial_ward ? "Stand in the runic circle" : tr == gen::kTrial_breath ? "Survive the waves" : tr >= 0 ? std::string(Title(tr)) : "Choose a trial";
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
					a_label = std::string(gen::kTowns[t]) + ": " + (s.freed ? std::string("its shout waits") : s.Boss() ? std::string(BossName(t)) : s.Trial() >= 0 ? std::string(Title(s.Trial())) : std::string("choose a trial"));
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
		} g_ring;
		void PublishRing(float a_delta)
		{
			g_ring.flash = std::max(0.0f, g_ring.flash - a_delta);
			std::vector<std::array<float, 3>> pts;
			auto* cam = RE::Main::WorldRootCamera();
			if (g_ring.on && cam) {
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
			h.ringOn = g_ring.on && !pts.empty();
			h.ringPts = std::move(pts);
			h.ringFrac = g_ring.frac;
			h.ringFlash = g_ring.flash;
			h.ringSeconds = g_ring.seconds;
			h.ringInside = g_ring.inside;
			h.ringHold = g_ring.hold;
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
			h.questCity.clear(), h.questTitle.clear(), h.questGoal.clear(), h.sanctuary.clear();
			h.questFrac = 0, h.questTimer = -1, h.questWarn = false;
			if (a_zone < 0) {
				Echo::Strip(h.questTitle, h.questGoal, h.questFrac);  // out in the wilds: a hunt for a wonder's part
				return;
			}
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
				h.questGoal = PriestDue(a_zone) ? "Slay " + std::string(BossName(a_zone)) : std::string(BossName(a_zone)) + (Director::Fighting() ? " rises with the next wave" : " rises when night falls");
				h.questWarn = false;
				return;
			}
			const int tr = s.Trial(), n = N(tr), sec = Secs(tr);
			const bool live = g_clockKey == a_zone * 10 + s.active;
			h.questTitle = Upper(std::string(Title(tr)));
			switch (tr) {
			case gen::kTrial_ward:
				h.questGoal = fmt::format("{}  {} / {} s", g_holding ? "Hold the circle" : "Stand in the runic circle", s.prog, sec);
				h.questFrac = static_cast<float>(s.prog) / std::max(1, sec);
				h.questWarn = !g_holding;
				break;
			case gen::kTrial_culling:
				h.questGoal = fmt::format("Kills in {}  {} / {}", gen::kTowns[a_zone], s.prog, n);
				h.questFrac = static_cast<float>(s.prog) / std::max(1, n);
				break;
			case gen::kTrial_unbroken:
				h.questGoal = g_waveBroken ? fmt::format("This wave fell below half - the next one counts  {} / {}", s.prog, n) : fmt::format("Clear the wave above half health  {} / {}", s.prog, n);
				h.questFrac = static_cast<float>(s.prog) / std::max(1, n);
				h.questWarn = g_waveBroken || (player && HealthFrac(player) < 0.6f);
				break;
			case gen::kTrial_steel:
				{
					const int need = Classes::Get() <= 1 ? n * 2 : n;
					static constexpr const char* kWhat[4] = { "Kills by your summons", "Destruction kills", "Kills from 30 m", "Melee kills, a spell in hand" };
					const int c = Classes::Get();
					h.questGoal = fmt::format("{}  {} / {}{}", c >= 0 && c < 4 ? kWhat[c] : "Choose a class", s.prog, need,
						c == 1 && s.prog >= need ? "  (kill with a second element)" : "");
					h.questFrac = static_cast<float>(s.prog) / std::max(1, need);
				}
				break;
			case gen::kTrial_breath:
				{
					RE::NiPoint3       c;
					float              yaw = 0;
					RE::TESWorldSpace* ws = nullptr;
					const bool atAltar = player && Shops::StandFrame(a_zone, c, yaw, ws) && ws == player->GetWorldspace() && std::hypot(player->GetPosition().x - c.x, player->GetPosition().y - c.y) < kBreathRange;
					h.questGoal = fmt::format("{}  {} / {}", atAltar ? "Hold the altar until the wave is cleared" : "Go back to the altar", s.prog, n);
					h.questFrac = static_cast<float>(s.prog) / std::max(1, n);
					h.questWarn = !atAltar;
				}
				break;
			default: break;
			}
		}

		// The Long Night's Breath: a wave of the dead claws up in a ring round the altar
		[[maybe_unused]] int RaiseWave(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_c, int a_count, int a_elites)
		{
			int raised = 0;
			for (int i = 0; i < a_count; ++i) {
				const float a = (i + Rand01() * 0.6f) * 6.2832f / std::max(1, a_count), r = 900.0f + 500.0f * Rand01();
				const auto  at = Walkable(a_player, a_c + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, 700.0f);
				if (!at) {
					continue;
				}
				if (auto h = Director::RaiseTrial(*at + RE::NiPoint3{ 0, 0, 40.0f }, i < a_elites); h) {
					g_wave.push_back(h);
					++raised;
				}
			}
			return raised;
		}
	}

	namespace Curse
	{
		int  ZoneTown() { return g_zone; }
		int  RelicOf(int a_town) { return a_town >= 0 ? Relic(a_town) : -1; }

		void ClaimRelic(int a_town)
		{
			if (const int sh = a_town >= 0 ? ShoutOf(a_town) : -1; sh >= 0) {
				const std::string name(gen::kShouts[sh].label);
				if (!Get(a_town).freed) {
					Hud::Banner(name + " sleeps here until the Echo Altar of " + std::string(gen::kTowns[a_town]) + " wakes", 3.5f);
					return;
				}
				if (Gun::KnowsShout(sh)) {
					Hud::Banner("You already know " + name, 2.5f);
					return;
				}
				// (Noah: no Word Walls) the runes cut into the altar's heart-stone are the words: they burn out of the rock
				// one by one and sink into you, as a Word Wall's would
				Gun::LearnShout(sh);
				Forms::Play(gen::Sound::shout_learned);
				Hud::WordReveal(sh);
				Note("The runes of the heart-stone give up their words: " + name + "  (" + std::string(gen::kShouts[sh].w1) + " " + std::string(gen::kShouts[sh].w2) + " " + std::string(gen::kShouts[sh].w3) + ") - equip it in the Magic menu", 9.0f);
				logger::info("curse: {} learned at {}", gen::kShouts[sh].id, gen::kTowns[a_town]);
				return;
			}
			const int relic = a_town >= 0 ? Relic(a_town) : -1;
			if (relic < 0) {
				return;
			}
			const std::string name(gen::kGuns[relic].label);
			if (!Get(a_town).freed) {
				Hud::Banner(name + " waits until the Echo Altar of " + std::string(gen::kTowns[a_town]) + " wakes", 3.5f);
				if (GiveBook(RE::PlayerCharacter::GetSingleton())) {
					Note("The Curse of Alduin is back in your pack", 5.0f);
				}
				return;
			}
			if (Gun::Owns(relic)) {
				Hud::Banner("You already carry " + name, 2.5f);
				Gun::Give(relic);  // (puts it in your hands)
				return;
			}
			Gun::Give(relic);
			Forms::Play(gen::Sound::buy);
			Hud::Banner(name + " RECLAIMED", 3.0f);
			logger::info("curse: {} reclaimed at {}", gen::kGuns[relic].id, gen::kTowns[a_town]);
		}
		int ClaimableShout(int a_town)
		{
			const int sh = a_town >= 0 ? ShoutOf(a_town) : -1;
			// (Noah: "when I freed the altar I didn't see an option to retrieve the shout") no story gate: a freed city's shout waits at its altar at once
			return sh >= 0 && Get(a_town).freed && !Gun::KnowsShout(sh) ? sh : -1;
		}
		bool  SafeHere(RE::PlayerCharacter*) { return g_zone >= 0 && Get(g_zone).freed; }
		float SpawnBoost() { return g_holding ? kWardSwarm[Diff()] : 1.0f; }
		int   Steps(int a_town) { return a_town >= 0 && a_town < static_cast<int>(gen::kCurseCities.size()) ? Step(a_town) : 0; }

		void StartTrial(int a_town, int a_trial)
		{
			if (a_town < 0 || a_town >= static_cast<int>(gen::kCurseCities.size()) || a_trial < 0 || a_trial >= kTrialCount) {
				return;
			}
			St s = Get(a_town);
			const std::string title(Title(a_trial));
			if (s.freed) {
				Hud::Banner("The Echo Altar of " + std::string(gen::kTowns[a_town]) + " is awake", 2.5f);
				return;
			}
			if ((s.done >> a_trial) & 1) {
				Hud::Banner(title + " is done", 2.5f);
				return;
			}
			if (std::string why; !Story::TrialsOpen(a_town, &why)) {
				Hud::Banner(why.empty() ? std::string("The altar sleeps until the story moves on") : why, 4.0f);
				return;
			}
			if (a_trial == gen::kTrial_unbroken) {
				Hud::Banner("The Unbroken is now part of the Long Night's Breath", 3.0f);
				return;
			}
			if (a_trial == gen::kTrial_steel && Classes::Get() < 0) {
				Hud::Banner("Choose a class first: the fourth trial is your class's own", 3.0f);
				return;
			}
			if (s.Boss() || s.Count() >= kNeeded) {
				Hud::Banner(InParty() && !s.Boss() ? "Your trials are done - the rest of the party must win their class trials" : "The four trials are done - " + std::string(BossName(a_town)) + " must fall", 3.0f);
				return;
			}
			if (s.Trial() == a_trial) {
				Hud::Banner(title + " is under way", 2.5f);
				return;
			}
			const int was = s.Trial();
			s.active = a_trial + 1, s.prog = 0;
			g_elementsUsed = 0;
			Put(a_town, s);
			ClearRun();
			Forms::Play(gen::Sound::quest_start);
			Hud::Banner((was >= 0 ? std::string(Title(was)) + " set aside.   " : std::string()) + Upper(title) + ": " + Ask(a_trial) + (Director::Fighting() ? "" : " (at night)"), 6.0f);
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
			v.done = s.done, v.active = s.active;
			for (int i = 0; i < kTrialCount; ++i) {
				v.desc[i] = Ask(i);
			}
			v.boss = std::string(BossName(a_town));
			v.bossLine = s.Boss() ? std::string(PriestDue(a_town) ? "Rises near the altar at night - slay it to wake the altar" : Director::Fighting() ? "Rises with the next wave - slay it to wake the altar" : "Rises when night falls - slay it to wake the altar") : fmt::format("Rises when the four trials are done ({} of {})", s.Count(), kNeeded);
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
			if (!Director::Fighting() || g_zone < 0) {
				return;
			}
			const int z = g_zone;
			St s = Get(z);
			const int tr = s.freed ? -1 : s.Trial();
			if (tr != gen::kTrial_culling) {
				return;  // (the class trial counts in OnClassKill)
			}
			if (++s.prog >= N(tr)) {
				CompleteTrial(z);
			} else {
				Put(z, s);
			}
		}

		// (the classes overhaul) the class trial: a kill of your class's own kind (Classes::OnKill tells us how it was made)
		std::string TrialTitle(int a_trial) { return a_trial >= 0 && a_trial < kTrialCount ? std::string(Title(a_trial)) : std::string(); }
		void OnClassKill(int a_how, int a_element, float a_dist)
		{
			if (!Director::Fighting() || g_zone < 0) return;
			const int z = g_zone;
			St s = Get(z);
			if (s.freed || s.Trial() != gen::kTrial_steel) return;
			const int c = Classes::Get();
			bool ok = false;
			switch (c) {
			case 0: ok = a_how == 0; break;
			case 1: ok = a_how == 1; break;
			case 2: ok = a_how == 2 && a_dist >= kLongShot; break;
			case 3: {  // a melee kill with a spell in either hand (an altar spell, or any spell)
				auto* pl = RE::PlayerCharacter::GetSingleton();
				const auto h = Arcana::Hands();
				bool spell = h[0] >= 0 || h[1] >= 0 || Classes::BladeSpell() >= 0;  // (a two-handed blade carries the last one held)
				for (bool left : { true, false }) {
					if (auto* f = pl ? pl->GetEquippedObject(left) : nullptr; f && f->As<RE::SpellItem>()) spell = true;
				}
				ok = a_how == 3 && spell;
				break;
			}
			}
			if (!ok) return;
			if (a_element >= 0) g_elementsUsed |= 1u << a_element;
			const int need = c <= 1 ? N(gen::kTrial_steel) * 2 : N(gen::kTrial_steel);
			s.prog = std::min(s.prog + 1, need);
			const bool twoElements = (g_elementsUsed & (g_elementsUsed - 1)) != 0;  // (two bits or more)
			if (s.prog >= need && (c != 1 || twoElements)) {
				CompleteTrial(z);
			} else {
				Put(z, s);
				if (s.prog % 10 == 0) Forms::Play(gen::Sound::quest_step);
			}
		}

		// the night's waves (Director): the Unbroken and the Long Night's Breath count whole waves
		void OnWave(bool a_cleared)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!a_cleared) {
				++g_waveSerial;
				g_waveBroken = false;  // (the audit: sampled only as the wave began - a fresh wave starts clean, the fall is watched through it)
				return;
			}
			const int z = g_zone;
			if (z < 0 || !player) {
				g_waveBroken = false;
				return;
			}
			St s = Get(z);
			const int tr = s.freed ? -1 : s.Trial();
			bool count = false;
			if (tr == gen::kTrial_unbroken) {
				count = !g_waveBroken;
			} else if (tr == gen::kTrial_breath) {
				if (Diff() >= 4 && g_waveBroken) {  // (the Unbroken, folded in: on Expert and up the wave must be held above half health)
					Hud::Banner("THE LONG NIGHT'S BREATH - you fell below half health: that wave doesn't count", 4.0f);
					g_waveBroken = false;
					return;
				}
				RE::NiPoint3       c;
				float              yaw = 0;
				RE::TESWorldSpace* ws = nullptr;
				count = Shops::StandFrame(z, c, yaw, ws) && ws == player->GetWorldspace() && std::hypot(player->GetPosition().x - c.x, player->GetPosition().y - c.y) < kBreathRange;
				if (!count) {
					Hud::Banner("THE LONG NIGHT'S BREATH - you were too far from the altar: that wave doesn't count", 4.0f);
				}
			}
			g_waveBroken = false;
			if (!count) {
				return;
			}
			if (++s.prog >= N(tr)) {
				CompleteTrial(z);
			} else {
				Put(z, s);
				Forms::Play(gen::Sound::quest_step);
				Hud::Banner(fmt::format("{} - {} of {} waves", Upper(std::string(Title(tr))), s.prog, N(tr)), 3.5f);
			}
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
				// (the audit: the kill lived only in memory - a save and load raised the Priest again) kept in the city's own
				// value: the Boss state with its progress 1 means "fallen, the night's last wave still to go"
				St fs = Get(a_town);
				fs.prog = 1;
				Put(a_town, fs);
				Hud::Banner(Upper(std::string(BossName(a_town))) + " FALLS - " + Upper(std::string(gen::kTowns[a_town])) + " IS FREE", 5.0f);
				logger::info("curse: {}'s Dragon Priest slain - the night ends, the city is freed", gen::kTowns[a_town]);
			}
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
			// (Noah: in a party each player's trials are their own - a Summoner's and a Ranger's differ) only the Dragon
			// Priest's rising and the freeing come over from the party; trials and their progress stay each player's
			{
				const St in = Decode(a_value);
				if (before.freed) return;
				St mine = before;
				if (in.freed) {
					mine.freed = true;
				} else if (in.Boss() && !before.Boss()) {
					mine.active = kBossActive, mine.prog = 0;
				} else {
					mine.done |= in.done & kSharedMask;  // (the shared trials a friend finished)
					const int it = in.Trial();
					if (it >= 0 && it != gen::kTrial_steel && !((mine.done >> it) & 1) && !mine.Boss()) {  // (a shared trial under way: its count is the party's)
						if (mine.active == in.active) mine.prog = std::max(mine.prog, in.prog);
						else if (mine.active == 0) mine.active = in.active, mine.prog = in.prog;
					}
					if (mine.Trial() >= 0 && ((mine.done >> mine.Trial()) & 1)) mine.active = 0, mine.prog = 0;  // (finished by a friend)
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
					Hud::Banner(Upper(std::string(Title(i))) + " - COMPLETE (your party)", 5.0f);
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
			g_mine.fill(0), g_elementsUsed = 0;  // (the audit: last cycle's class-trial credit carried over)
			logger::info("curse: begun again - every city under the curse, night 1 next");
		}

		void MigrateRites()
		{
			for (int t = 0; t < static_cast<int>(gen::kCurseCities.size()); ++t) {
				const int v = Value(t);
				if (v > 0 && v < kBase) {
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
					shown(6, true, true);
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
						// (Noah: every trial listed from the start - open until done, ticked once done; the Unbroken, folded into
						// the Long Night's Breath, only if an old save had it done)
						shown(1 + i, i != gen::kTrial_unbroken || done, done);
					}
					shown(6, s.Boss(), false);
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
				// (a party's altar) the last of the party finished three trials: the Dragon Priest may rise now
				if (z >= 0) {
					if (St cs = Get(z); !cs.freed && cs.active == 0 && InParty() && PriestReady(z, cs)) {
						cs.active = kBossActive;
						Put(z, cs);
						Forms::Play(gen::Sound::quest_step);
						Hud::Banner("THE PARTY'S TRIALS ARE DONE   " + std::string(BossName(z)) + " rises at the altar when night falls - then finish the night's waves", 7.0f);
						logger::info("curse: {} - every player has three trials: the Dragon Priest", gen::kTowns[z]);
					}
				}
				Arcana::RefreshPower();  // (the spells grow with the altars woken)
				// saves from the ten-rite cities: a freed city stays freed, the rest begin the trials
				if (g_normDelay > 0) {
					g_normDelay -= 0.5f;
				} else {
					for (int t = 0; t < static_cast<int>(gen::kCurseCities.size()); ++t) {
						if (const int v = Value(t); v < kBase) {
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
					Note("The Curse of Alduin is in your pack (Books). Your class guide follows when you choose a class at an Echo Altar", 8.0f);
					return;
				}
			}
			const int z = g_zone;
			const St  s = z >= 0 ? Get(z) : St{ true };
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().wardFrac = 0;
			}
			if (!fighting || z < 0 || s.freed || s.active == 0) {
				g_holding = false;
				g_holdAcc = 0;
				g_ring.on = false;
				PublishRing(a_delta);
				Audio::LoopStop(Audio::kLoopRite, 800);
				return;
			}
			RE::NiPoint3       c;
			float              yaw = 0;
			RE::TESWorldSpace* ws = nullptr;
			const bool         here = Shops::StandFrame(z, c, yaw, ws) && ws == player->GetWorldspace();
			const int          tr = s.Trial();
			const int          key = z * 10 + s.active;
			g_ring.on = false;
			g_holding = false;
			if (tr == gen::kTrial_ward && here) {
				Audio::LoopStart(Audio::kLoopRite, gen::Sound::rite_hum, Shops::Placed(z, 0));
			} else {
				Audio::LoopStop(Audio::kLoopRite, 800);
			}
			switch (tr) {
			case gen::kTrial_ward:  // stand in the circle: each second inside counts, outside it waits; the dead come faster
				{
					const int sec = Secs(tr);
					g_holding = here && std::hypot(player->GetPosition().x - c.x, player->GetPosition().y - c.y) < gen::kCurseHoldRadius;
					if (here) {
						g_ring = { true, c, gen::kCurseHoldRadius, static_cast<float>(s.prog) / std::max(1, sec), g_ring.flash, sec - s.prog, g_holding ? 0 : 1, true };
					}
					if (g_holding && (g_holdAcc += a_delta) >= 1.0f) {
						g_holdAcc -= 1.0f;
						St n = s;
						if (++n.prog >= sec) {
							g_holding = false;
							g_ring.on = false;
							CompleteTrial(z);
						} else {
							Put(z, n);
						}
					}
				}
				break;
			case gen::kTrial_culling:  // (Noah: the nights are waves now) every kill in the city counts, no clock
				break;
			case gen::kTrial_unbroken:  // below half health: this wave no longer counts (Curse::OnWave)
				{
					const bool below = HealthFrac(player) < 0.5f;
					if (below && !g_belowHalf && !g_waveBroken) {
						g_waveBroken = true;
						Forms::Play(gen::Sound::rite_reset);
						Hud::Banner("THE UNBROKEN - you fell below half health: this wave won't count", 4.0f);
					}
					g_belowHalf = below;
				}
				break;
			case gen::kTrial_breath:  // counted as each wave is cleared (Curse::OnWave); on Expert and up, watched for the fall below half
				if (Diff() >= 4) {
					const bool below = HealthFrac(player) < 0.5f;
					if (below && !g_belowHalf && !g_waveBroken && Director::Fighting()) {
						g_waveBroken = true;
						Forms::Play(gen::Sound::rite_reset);
						Hud::Banner("THE LONG NIGHT'S BREATH - you fell below half health: this wave won't count", 4.0f);
					}
					g_belowHalf = below;
				}
				break;
			case gen::kTrial_steel:
				break;
			default:
				if (s.Boss() && s.prog == 0 && here && PriestDue(z) && !Director::ChampionActive(z)) {  // (no story gate: from the wave after its trials)  // the Dragon Priest rises out in the city, once a night until it falls
					static float retry = 0;
					if ((retry -= a_delta) <= 0) {
						retry = 4.0f;
						const float a = Rand01() * 6.2832f, r = 1500.0f + 700.0f * Rand01();
						const auto  at = Walkable(player, c + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, 800.0f);
						const auto& b = gen::kCityBosses[z];
						if (at) {
							Director::RaiseChampion(z, b.row, b.name, b.health * kBossHealth[Diff()], *at + RE::NiPoint3{ 0, 0, 40.0f });
						}
					}
				}
				break;
			}
			PublishRing(a_delta);
		}
	}
}
