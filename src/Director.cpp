#include <sstream>
#include "BO3Z.h"

// Night rounds (sheet: rounds). Rounds run from nightStartHour to nightEndHour on Skyrim's clock.
// The round number saved on the player is the last round CLEARED; a round cut off by dawn starts
// again the next night.
namespace bo3z
{
	namespace
	{
		constexpr auto& R = gen::kRounds;

		enum class Phase { Day, Break, Fighting };

		struct Spawned
		{
			RE::ActorHandle handle;
			std::size_t     enemy;
			bool            inCombat{ false };
			float           combatRetry{ 0 };
			float           webbed{ 0 };
			float           webAmount{ 0 };
			float           behind{ 0 };       // seconds spent too far away / unloaded
			float           stall{ 0 };     // seconds spent not closing in
			float           stuck{ 0 };     // (Noah: "some hang out behind buildings") seconds loaded, out of reach and not getting closer
			float           lastDist{ -1 };
			RE::NiPoint3    lastPos{};      // (Noah: "stuck on porches and behind houses") where it stood a few seconds ago
			float           still{ 0 };     // seconds it has barely moved while out of reach
			float           ability{ 4.0f };   // Iron Reaper: next reap / Necromancer: next summon
			float           windup{ -1.0f };   // Iron Reaper: seconds until the telegraphed sweep lands
			float           mounted{ 0 };      // the extra speed it has while the player rides (MountChase)
			float           hook{ 6.0f };      // Iron Reaper: next chain hook (mid range)
			float           hookWind{ -1.0f }; // Iron Reaper: the hook is thrown
			bool            enraged{ false };
			float           frenzy{ 0 };       // Mindbreaker: seconds left fighting its own kind
			float           tear{ 0 };
			float           loaded{ 0 };       // seconds it has had its body (nothing is taken off a body still being built)
			RE::FormID      fid{ 0 };          // (kept: once its handle is gone, its ID still leaves our books)
			// (Noah: "each brute its own unique ability" - the chain hook and the shared scythe gone) the brutes' moves
			float           dash{ -1.0f };     // a charge / a leap in flight: seconds left
			float           dashLen{ 0 };
			RE::NiPoint3    dashDir{}, dashFrom{}, dashTo{};
			float           stun{ 0 };         // (the Barrow Champion, into a wall) dazed: held, and hurt twice as hard
			RE::NiPoint3    stunAt{};
			float           breath{ -1.0f };   // (the Ettin) frost breath: seconds left
			float           breathTick{ 0 };
			float           gate{ 0 };         // (the Gatekeeper) its bone gate: seconds left
			RE::NiPoint3    gateAt{}, gateN{};
			float           gateFx{ 0 }, gateHp{ 0 };
			int             trophies{ 0 };     // (Thorgar) skulls taken
			bool            berserk{ false };  // (the Skeleton Mutant) past half: berserk
			std::vector<RE::ActorHandle> band; // (the Goblin Warlord) the goblins it called
			bool            straggler{ false };  // (the round flow) one of a wave's last three: sent straight at you
		};

		Phase                   g_phase = Phase::Day;
		int                     g_current = 0;   // round being fought
		bool                    g_skipped = false;  // (Noah) this night was passed by: its dawn is no night survived
		int                     g_toSpawn = 0;
		// (Noah: like BO3) a night is waves: 4 + the night's number of them; wave k brings 20 x k dead (20, 40, 60...); the night
		// holds (the clock stops short of dawn) until the last wave is cleared
		constexpr int   kWavesBase = 4, kWaveDeadStep = 20;
		// (Noah: "make it fluid and fun") the first nights build up: fewer waves, smaller ones, fewer dead at once - a new
		// character finds its feet before the full horde (from night 5 on, as before)
		int WavesFor(int a_round) { return a_round >= 5 ? kWavesBase + a_round : 2 + a_round + (a_round >= 3 ? 1 : 0); }
		int WaveStep() { return g_current >= 5 ? kWaveDeadStep : std::clamp(6 + 3 * g_current, 9, kWaveDeadStep); }
		int AtOnceMin(int a_round) { return std::min(12, 4 + 2 * std::max(1, a_round)); }
		constexpr float kWaveRest = 8.0f;
		int   g_waves = 0, g_wave = 0, g_waveSize = 0, g_waveSpawned = 0;
		float g_waveRest = 0;
		bool  g_nightCleared = false;
		float                   g_spawnTimer = 0;
		float                   g_breakTimer = 0;
		bool                    g_special = false;
		std::vector<Spawned>    g_alive;
		std::deque<RE::ActorHandle> g_corpses;
		struct Settle
		{
			RE::ActorHandle handle;
			float           age;
			int             tries = 0;
		};
		std::vector<Settle> g_settle;
		std::unordered_map<RE::FormID, std::size_t> g_ours;  // every undead we raised -> enemy row
		bool IsOursRaw(RE::Actor* a) { return a && g_ours.contains(a->GetFormID()); }
		std::vector<std::pair<RE::ObjectRefHandle, float>> g_fx;
		struct Elite
		{
			RE::ActorHandle handle;
			float           lunge, shriek;
			bool            paid;
		};
		std::unordered_map<RE::FormID, Elite> g_elites;
		// (Noah: "elites, bosses and trial bosses should spawn with the summon animation, not coming from the ground") the
		// ones raised through a portal: set while a boss, champion, mini-boss or trial elite is being raised (g_summonRaise)
		std::unordered_set<RE::FormID> g_summoned;
		int                            g_summonRaise = 0;
		struct SummonScope
		{
			SummonScope() { ++g_summonRaise; }
			~SummonScope() { --g_summonRaise; }
		};
		float g_slowLeft = 0;  // Terror Shriek slow on the player  // short-lived effects (rise bursts)
		std::mt19937            g_rng{ std::random_device{}() };
		bool                    g_loggedBlocked = false;
		int                     g_event = -1;     // this night's event (sheet: events), -1 none
		// (Noah) themed waves: a wave of one theme (sheet: events) inside an ordinary night
		int  g_waveTheme = -1, g_themedCount = 0;
		bool g_lastThemed = false, g_finaleSpawned = false;
		int  ThemeNowImpl() { return g_event >= 0 ? g_event : g_waveTheme; }
		// (Noah) the mini-boss: 1 night in 5, halfway through, a great creature that always drops a wonder part
		int          g_miniWave = -1;   // the wave it comes with tonight (-1: not tonight)
		float        g_miniWarn = -1;   // seconds of warning left before it rises
		std::uint8_t g_miniSlain = 0;   // (counted for the party: each game gives its own part)
		int          g_miniPick = -1;
		bool                    g_weatherForced = false;
		float                   g_pause = 0;      // Nuke: seconds before the dead rise again
		float                   g_sanctuarySaid = 0;

		float Uniform(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }

		// (Noah: at most 2,000 dead a night for each player - 6,000 for three) the waves keep their rising shape (20, 40,
		// 60...) but are scaled down together when the night's total would pass the cap
		constexpr int kNightCapPerPlayer = 2000;
		int WaveSize(int a_wave)
		{
			const int   players = Net::InLobby() ? Net::Members() + 1 : 1;
			const float total = static_cast<float>(WaveStep()) * g_waves * (g_waves + 1) / 2.0f;
			const float scale = std::min(1.0f, static_cast<float>(kNightCapPerPlayer * std::max(1, players)) / std::max(1.0f, total));
			const int th = ThemeNowImpl();
			return std::max(1, static_cast<int>(std::lround(WaveStep() * a_wave * scale * (th >= 0 ? gen::kEvents[th].waveCount : 1.0f))));
		}

		int TotalFor(int a_round, int a_players = 1)
		{
			// Black Ops III's zombie count for a round.
			float multiplier = std::max(1.0f, a_round / 5.0f);
			if (a_round >= 10) {
				multiplier *= a_round * 0.15f;
			}
			int max = R.aiMaxBase;
			if (a_players == 1) {
				max += static_cast<int>(0.5f * R.aiPerPlayer * multiplier);
			} else {
				max += static_cast<int>((a_players - 1) * R.aiPerPlayer * multiplier);
			}
			if (a_round <= static_cast<int>(R.earlyRoundScale.size())) {
				max = static_cast<int>(max * R.earlyRoundScale[a_round - 1]);
			}
			return std::max(1, max);
		}

		float HealthFor(int a_round)
		{
			// Classic curve up to the cap round, then only gentle growth: the horde's size is the threat.
			const int cap = static_cast<int>(gen::kHpCapRound);
			float     hp = static_cast<float>(R.hpStart);
			for (int r = 2; r <= std::min(a_round, cap); ++r) {
				hp = r <= R.hpLinearUntil ? hp + R.hpLinearPerRound : hp * R.hpMultAfter;
			}
			if (a_round > cap) {
				hp *= std::pow(gen::kHpGrowthAfterCap, static_cast<float>(a_round - cap));
			}
			if (a_round > 10) {  // (Noah: harder to grow too strong) the dead grow 8% tougher every night after the tenth
				hp *= std::pow(1.08f, static_cast<float>(a_round - 10));
			}
			hp *= 1.0f + 0.5f * static_cast<float>(Echo::NewGamePlus());  // (Stage 5: each cycle begun again, the dead half again as tough)
			return hp / R.hpScale;  // design units -> Skyrim health
		}

		float SpawnDelay(int a_round)
		{
			return std::max(R.spawnDelayMin, R.spawnDelayStart * std::pow(R.spawnDelayDecay, static_cast<float>(a_round - 1)));
		}

		// (Noah: the night starts the moment the sun sets) dusk is when the sun reaches the horizon in this place's
		// climate (the middle of its sunset), not a fixed hour; the sheet's nightStartHour stands in where none is known
		// (Noah: night starts at 9pm) a fixed hour now, wherever you are
		// (Noah, later: "nighttime to start as soon as daytime ends, till just before the sunrise") from the end of this
		// place's sunset to the start of its sunrise (Skyrim's climate timing, in ten-minute steps); the sheet's hours stand
		// in where no climate is known
		float ClimateHour(bool a_dusk)
		{
			auto* sky = RE::Sky::GetSingleton();
			auto* cl = sky ? sky->currentClimate : nullptr;
			if (!cl) return a_dusk ? 20.0f : 5.0f;
			const float h = (a_dusk ? cl->timing.sunset.end : cl->timing.sunrise.begin) / 6.0f;
			return a_dusk ? (h >= 16.0f && h <= 23.0f ? h : 20.0f) : (h >= 3.0f && h <= 8.0f ? h : 5.0f);
		}
		float DuskHour() { return ClimateHour(true); }
		float DawnHour() { return ClimateHour(false); }
		bool IsNight()
		{
			auto* cal = RE::Calendar::GetSingleton();
			if (!cal) {
				return false;
			}
			const float h = cal->GetHour();
			const float dusk = DuskHour();
			return dusk > DawnHour() ? (h >= dusk || h < DawnHour()) : (h >= dusk && h < DawnHour());
		}

		bool Allowed(RE::PlayerCharacter* a_player)
		{
			auto* intro = Forms::IntroQuest();
			if (!Forms::IntroDone()) {
				if (!g_loggedBlocked) {
					logger::info("rounds wait for the Helgen intro (MQ101) to finish");
					g_loggedBlocked = true;
				}
				return false;
			}
			g_loggedBlocked = false;
			if (Story::SceneHold()) {  // (the story's set pieces play out undisturbed)
				static auto told = std::chrono::steady_clock::now() - std::chrono::seconds(600);
				if (std::chrono::steady_clock::now() - told > std::chrono::seconds(300)) {
					told = std::chrono::steady_clock::now();
					logger::info("rounds hold: a main-quest scene is playing here");
				}
				return false;
			}
			// a new character's arrival (the black, the film, the black after it) and a grace after it: no night begins
			// on a player who cannot yet see or move (it used to start under the veil and kill the newcomer)
			static auto calm = std::chrono::steady_clock::now();
			if (Hud::IntroBusy()) {
				calm = std::chrono::steady_clock::now() + std::chrono::seconds(12);
				return false;
			}
			if (std::chrono::steady_clock::now() < calm) {
				return false;
			}
			return a_player->Is3DLoaded() && !a_player->IsDead() && a_player->GetParentCell();
		}

		int EliteCount()
		{
			int n = 0;
			for (auto& s : g_alive) {
				n += gen::kEnemies[s.enemy].elite ? 1 : 0;
			}
			return n;
		}
		int Weight(const gen::Enemy& e)
		{
			if (e.elite && (!Story::ElitesOpen() || EliteCount() >= static_cast<int>(gen::elite::maxAlive))) {  // (elites from Dragon Rising on)
				return 0;
			}
			if (g_event >= 0 && (e.kind == 1 || e.kind == 2)) {  // Night of Iron: the champions march
				return static_cast<int>(std::lround(e.weight * gen::kEvents[g_event].championWeight));
			}
			return e.weight;
		}

		bool ThemeAvailable(int a_t)
		{
			if (a_t < 0) return false;
			const auto& ev = gen::kEvents[a_t];
			bool any = ev.members[0] < 0;
			for (int m : ev.members) any |= m >= 0 && Forms::Enemy(static_cast<std::size_t>(m)) != nullptr;
			return any;
		}
		std::optional<std::size_t> PickEnemy()
		{
			if (const int th = ThemeNowImpl(); th >= 0 && gen::kEvents[th].members[0] >= 0 && !g_special) {  // a theme's own
				std::vector<std::size_t> pool;
				for (int m : gen::kEvents[th].members) {
					if (m >= 0 && Forms::Enemy(static_cast<std::size_t>(m))) pool.push_back(static_cast<std::size_t>(m));
				}
				if (!pool.empty()) {
					return pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(g_rng)];
				}
			}
			int total = 0;
			for (const auto& e : gen::kEnemies) {
				if (e.specialRound == g_special && (g_special || e.minRound <= g_current) && Forms::Enemy(static_cast<std::size_t>(&e - gen::kEnemies.data()))) {  // (a creature mod missing: its rows sit out)
					total += Weight(e);
				}
			}
			if (total <= 0) {
				return std::nullopt;
			}
			int roll = std::uniform_int_distribution<int>(1, total)(g_rng);
			for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
				const auto& e = gen::kEnemies[i];
				if (e.specialRound == g_special && (g_special || e.minRound <= g_current) && Forms::Enemy(i) && Weight(e) > 0 && (roll -= Weight(e)) <= 0) {
					// (Noah: four Grave Witches at once early in night 2 - "it summoned itself") never more than two Witches
					// up at a time (one on the first nights): past that, a Risen Dead comes instead
					if (e.id == "mage") {
						int witches = 0;
						for (const auto& s : g_alive) {
							if (gen::kEnemies[s.enemy].id == "mage") {
								if (auto a = s.handle.get(); a && !a->IsDead()) ++witches;
							}
						}
						if (witches >= (g_current < 4 ? 1 : 2)) {
							for (std::size_t z = 0; z < gen::kEnemies.size(); ++z) {
								if (gen::kEnemies[z].id == "zombie" && Forms::Enemy(z)) return z;
							}
						}
					}
					return Forms::Enemy(i) ? std::optional{ i } : std::nullopt;
				}
			}
			return std::nullopt;
		}

		// A leveled list (LVLN) can't be placed as-is: resolve it to one NPC for the player's level, the
		// way the game does for encounter zones. Falls back to any NPC the list contains.
		RE::TESBoundObject* ResolveSpawnable(RE::TESBoundObject* a_base, RE::PlayerCharacter* a_player)
		{
			for (int depth = 0; depth < 6 && a_base; ++depth) {
				auto* lev = a_base->As<RE::TESLevCharacter>();
				if (!lev) {
					return a_base;
				}
				RE::BSScrapArray<RE::CALCED_OBJECT> picked;
				lev->CalculateCurrentFormList(a_player->GetLevel(), 1, picked, 0, true);
				RE::TESBoundObject* next = nullptr;
				for (auto& c : picked) {
					if (c.form && (next = c.form->As<RE::TESBoundObject>())) {
						break;
					}
				}
				if (!next) {
					std::vector<RE::TESBoundObject*> npcs;
					for (auto* f : lev->GetContainedForms()) {
						if (f && f->Is(RE::FormType::NPC)) {
							npcs.push_back(f->As<RE::TESBoundObject>());
						}
					}
					if (npcs.empty()) {
						return nullptr;
					}
					next = npcs[std::uniform_int_distribution<std::size_t>(0, npcs.size() - 1)(g_rng)];
				}
				a_base = next;
			}
			return a_base && a_base->Is(RE::FormType::NPC) ? a_base : nullptr;
		}

		// Mostly behind the player, like BO3 windows you can't see.
		// (Noah: like BO3) the dead climb out of the ground at set places: a ring of spawn points round the city's altar (in
		// a city) or round where you stood when they were laid out (in the wilds, laid out again if you go far)
		struct SpawnPoints
		{
			RE::TESWorldSpace*        ws{ nullptr };
			int                       town{ -2 };
			RE::NiPoint3              anchor{};
			std::vector<RE::NiPoint3> pts;
		} g_points;
		RE::NiPoint3 g_spotBase{};       // the hand-placed point SpawnSpot last chose (its marker spawns the next one)
		bool         g_spotBaseSet = false;
		// (the audit's suggestion: spawn markers) an XMarker stands at each hand-placed spawn point once it is used; the dead
		// are placed at it - Skyrim's own PlaceAtMe from a reference already in that spot's cell - so each one is made in
		// the right cell, on the right ground, in one step
		std::unordered_map<std::int64_t, RE::ObjectRefHandle> g_spawnMarks;
		RE::TESObjectREFR* SpawnMark(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at)
		{
			const std::int64_t key = (static_cast<std::int64_t>(std::lround(a_at.x / 8.0f)) << 32) ^ static_cast<std::int64_t>(std::lround(a_at.y / 8.0f) & 0xFFFFFFFF);
			if (auto it = g_spawnMarks.find(key); it != g_spawnMarks.end()) {
				if (auto m = it->second.get(); m && !m->IsDeleted() && m->GetWorldspace() == a_player->GetWorldspace() && m->GetParentCell() && m->GetParentCell()->IsAttached()) return m.get();
				g_spawnMarks.erase(it);
			}
			auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x00003B);  // XMarker
			auto* cell = RE::TES::GetSingleton()->GetCell(a_at);
			if (!xm || !cell || !cell->IsAttached() || cell->IsInteriorCell()) return nullptr;
			auto h = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(xm, a_at, {}, cell, a_player->GetWorldspace(), nullptr, nullptr, {}, false, true);
			auto m = h.get();
			if (!m) return nullptr;
			g_spawnMarks[key] = h;
			return m.get();
		}
		std::optional<float> WalkTop(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_at, float a_above);
		// (Noah: "from the ground - on road and dirt, not inside houses or on their porches") a spot is good ground when
		// the first thing a ray from high above meets there is the land itself (nothing built over it or on it: no roof,
		// no porch, no house), and best when the land is road or dirt
		int GroundKind(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, float a_land)
		{
			auto* cell = a_player->GetParentCell();
			if (const auto top = WalkTop(cell, { a_at.x, a_at.y, a_land }, 900.0f); top && std::abs(*top - a_land) > 24.0f) {
				return 0;  // something stands there (a house, a porch, a wall, a rock shelf)
			}
			switch (RE::TES::GetSingleton()->GetLandMaterialType({ a_at.x, a_at.y, a_land })) {
			case RE::MATERIAL_ID::kDirt:
			case RE::MATERIAL_ID::kGravel:
			case RE::MATERIAL_ID::kMud:
			case RE::MATERIAL_ID::kStoneBroken: return 2;  // road and dirt
			default: return 1;                            // open land
			}
		}
		void LayPoints(RE::PlayerCharacter* a_player, int a_town, const RE::NiPoint3& a_anchor)
		{
			g_points.ws = a_player->GetWorldspace(), g_points.town = a_town, g_points.anchor = a_anchor;
			g_points.pts.clear();
			const float turn = std::fmod(std::abs(a_anchor.x * 0.0013f + a_anchor.y * 0.0007f), 6.2832f);  // (the same layout every night in a place)
			// candidates all the way round, at three distances; the good ground kept - road and dirt first
			std::vector<RE::NiPoint3> road, open;
			constexpr int kAround = 24;
			for (int k = 0; k < kAround * 3; ++k) {
				const float ang = turn + 6.2832f * (k % kAround) / kAround + (k / kAround) * 0.13f;
				const float r = (k / kAround == 0 ? 1400.0f : k / kAround == 1 ? 2000.0f : 2600.0f);
				RE::NiPoint3 p{ a_anchor.x + std::cos(ang) * r, a_anchor.y + std::sin(ang) * r, a_anchor.z };
				float land = 0;
				if (!RE::TES::GetSingleton()->GetLandHeight(p, land)) continue;
				p.z = land;
				const int kind = GroundKind(a_player, p, land);
				if (kind == 2) road.push_back(p);
				else if (kind == 1) open.push_back(p);
			}
			// (12 at the most, spread round: road and dirt, topped up with open land when the roads are few)
			auto pick = [&](std::vector<RE::NiPoint3>& from, std::size_t want) {
				const std::size_t step = std::max<std::size_t>(1, from.size() / std::max<std::size_t>(1, want));
				for (std::size_t q = 0; q < from.size() && g_points.pts.size() < 12; q += step) g_points.pts.push_back(from[q]);
			};
			pick(road, 12);
			if (g_points.pts.size() < 8) pick(open, 12 - g_points.pts.size());
			logger::info("spawn: {} points laid round {} ({} on road or dirt, {} open ground found)", g_points.pts.size(), a_town >= 0 ? std::string(gen::kTowns[a_town]) : std::string("the wilds"), road.size(), open.size());
		}

		// a ray between two points meets something (the line-of-sight layer: the world's solid things)
		bool RayBlocked(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
		{
			auto* world = a_cell ? a_cell->GetbhkWorld() : nullptr;
			if (!world) return false;
			const float s = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(a_from * s);
			pick.rayInput.to = RE::hkVector4(a_to * s);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
			RE::BSReadLockGuard lock(world->worldLock);
			world->PickObject(pick);
			return pick.rayOutput.HasHit();
		}
		// (Noah: "in the wild or a dungeon the enemies should spawn randomly within 25 meters of the player on floor level
		// and not in any objects - only clear spaces") a spot 7-25 m from you on the floor you stand on: open above it
		// (no roof, table or shelf over it, room to stand), clear all round (no wall, rock or prop touching it), and not
		// shut off from you by a wall (a clear line from your chest, or else from your feet's level)
		std::optional<RE::NiPoint3> RandomClearSpot(RE::PlayerCharacter* a_player)
		{
			auto* cell = a_player->GetParentCell();
			if (!cell) return std::nullopt;
			const auto pp = a_player->GetPosition();
			for (int tryN = 0; tryN < 32; ++tryN) {
				const float ang = Uniform(0.0f, 6.2832f), dist = Uniform(500.0f, 1750.0f);
				RE::NiPoint3 c{ pp.x + std::cos(ang) * dist, pp.y + std::sin(ang) * dist, pp.z };
				const auto floor = WalkTop(cell, c, 160.0f);
				if (!floor || std::abs(*floor - pp.z) > 220.0f) continue;  // (another floor, a pit, a ledge)
				c.z = *floor;
				if (RayBlocked(cell, c + RE::NiPoint3{ 0, 0, 12.0f }, c + RE::NiPoint3{ 0, 0, 200.0f })) continue;  // (something over it)
				bool tight = false;
				for (int k = 0; k < 6 && !tight; ++k) {
					const float a = 6.2832f * k / 6.0f;
					const RE::NiPoint3 mid = c + RE::NiPoint3{ 0, 0, 55.0f };
					tight = RayBlocked(cell, mid, mid + RE::NiPoint3{ std::cos(a) * 55.0f, std::sin(a) * 55.0f, 0 });
				}
				if (tight) continue;
				if (RayBlocked(cell, pp + RE::NiPoint3{ 0, 0, 90.0f }, c + RE::NiPoint3{ 0, 0, 90.0f }) &&
					RayBlocked(cell, pp + RE::NiPoint3{ 0, 0, 30.0f }, c + RE::NiPoint3{ 0, 0, 30.0f })) continue;  // (behind a wall)
				if (Party::FriendDistance(c) < 400.0f) continue;
				bool crowded = false;
				for (const auto& z : g_alive) {
					if (auto za = z.handle.get(); za && !za->IsDead() && za->GetPosition().GetDistance(c) < 110.0f) crowded = true;
				}
				if (crowded) continue;
				c.z += R.spawnHeightOffset;
				return c;
			}
			return std::nullopt;
		}
		RE::NiPoint3 SpawnSpot(RE::PlayerCharacter* a_player)
		{
			const bool  interior = a_player->GetParentCell() && a_player->GetParentCell()->IsInteriorCell();
			const auto& ring = interior ? R.spawnRingInterior : R.spawnRingExterior;
			// (Noah: "I have placed all my city echo altars and enemy spawn points within cities") in a city the dead climb
			// out at its hand-placed spots (sheet: spawnpoints); anywhere else - the wilds, a dungeon - at clear floor
			// near you (RandomClearSpot)
			int city = -1;
			if (!interior) {
				const auto pp = a_player->GetPosition();
				auto*      pws = a_player->GetWorldspace();
				for (int t = 0; t < static_cast<int>(gen::kStations.size()); ++t) {
					const auto& st = gen::kStations[t];
					if (st.fixed && pws && pws->GetFormID() == st.world && std::hypot(st.x - pp.x, st.y - pp.y) < 6500.0f) city = t;
				}
			}
			if (city < 0) {
				if (const auto spot = RandomClearSpot(a_player)) return *spot;
			}
			if (city >= 0) {
				if (g_points.town != city || g_points.ws != a_player->GetWorldspace()) {
					g_points.town = city, g_points.ws = a_player->GetWorldspace();
					g_points.pts.clear();
					for (const auto& sp : gen::kSpawnPoints) {
						if (sp.town == city) g_points.pts.push_back({ sp.x, sp.y, sp.z });
					}
					logger::info("spawn: {} hand-placed spawn points in {}", g_points.pts.size(), gen::kTowns[city]);
				}
				std::vector<const RE::NiPoint3*> ok;
				for (const auto& p : g_points.pts) {
					const float d = p.GetDistance(a_player->GetPosition());
					if (d > 500.0f && d < gen::kChaseRelocateDist * 0.85f && Party::FriendDistance(p) > 400.0f) {  // (the audit: a point past the chase distance was "far behind" the moment it rose)
						ok.push_back(&p);
					}
				}
				if (!ok.empty()) {
					// (Noah: "hordes surround me instead of queuing up") the next one comes from the side the fewest are on now:
					// the dead round you counted in eight directions, each spawn point weighted against its own
					std::array<int, 8> crowd{};
					const auto         pp = a_player->GetPosition();
					const auto         bin = [&](const RE::NiPoint3& q) { return static_cast<int>(std::floor((std::atan2(q.y - pp.y, q.x - pp.x) + 3.14159265f) / 6.2831853f * 8.0f)) & 7; };
					for (const auto& z : g_alive) {
						if (auto za = z.handle.get(); za && !za->IsDead() && za->GetPosition().GetDistance(pp) < 4000.0f) ++crowd[bin(za->GetPosition())];
					}
					std::vector<float> wts;
					// (the performance pass: no pop-in) a spot close by and right before your eyes is used last
					const float yawP = a_player->GetAngleZ();
					const RE::NiPoint3 fwd{ std::sin(yawP), std::cos(yawP), 0.0f };
					// (the audit's suggestion) truly out of sight - a ray from your eyes to where it would step out meets a wall, a
					// house, a rock - and close is best; in view and in front of you is the last resort
					auto* pcell = a_player->GetParentCell();
					const RE::NiPoint3 eye = pp + RE::NiPoint3{ 0, 0, 120.0f };
					for (const auto* q : ok) {
						const float d = q->GetDistance(pp);
						const float face = d > 1.0f ? ((q->x - pp.x) * fwd.x + (q->y - pp.y) * fwd.y) / d : 0.0f;
						const bool  inView = face > 0.25f && !RayBlocked(pcell, eye, *q + RE::NiPoint3{ 0, 0, 100.0f });
						const float seen = inView ? (d < 2400.0f ? 0.08f : 0.4f) : (d < 2400.0f ? 1.6f : 1.0f);
						wts.push_back(seen * (d < 3200.0f ? 1.0f : 0.25f) / (1.0f + 1.5f * static_cast<float>(crowd[bin(*q)])));  // (the near ones first)
					}
					// (Noah: "they spawn on top of each other") a spot just used rests a few seconds, and each one is set
					// where none of the dead already stands: tried round its point until it is clear by a body's width
					static std::vector<std::pair<RE::NiPoint3, std::chrono::steady_clock::time_point>> used;
					const auto nowU = std::chrono::steady_clock::now();
					std::erase_if(used, [&](const auto& u) { return nowU - u.second > std::chrono::milliseconds(2500); });
					const auto recentlyUsed = [&](const RE::NiPoint3& q) {
						for (const auto& u : used) if (std::hypot(u.first.x - q.x, u.first.y - q.y) < 350.0f) return true;
						return false;
					};
					// (Noah: "the enemies spawn one at a time from each spawn point") a portal still open (the last 2.5 s) takes no
					// other: only when every point is busy does the least loaded one take a second
					bool anyFree = false;
					for (std::size_t k = 0; k < wts.size(); ++k) anyFree |= !recentlyUsed(*ok[k]);
					for (std::size_t k = 0; k < wts.size(); ++k) if (recentlyUsed(*ok[k])) wts[k] *= anyFree ? 0.0f : 0.05f;
					std::discrete_distribution<std::size_t> pickPt(wts.begin(), wts.end());
					const RE::NiPoint3 base = *ok[pickPt(g_rng)];
					const auto clearOf = [&](const RE::NiPoint3& q) {
						for (const auto& z : g_alive) {
							if (auto za = z.handle.get(); za && !za->IsDead()) {
								const auto zp = za->GetPosition();
								if (std::hypot(zp.x - q.x, zp.y - q.y) < 110.0f) return false;
							}
						}
						for (const auto& u : used) if (u.first.GetDistance(q) < 110.0f) return false;
						return true;
					};
					RE::NiPoint3 p = base;
					for (int tryN = 0; tryN < 10; ++tryN) {
						const float ang = Uniform(0.0f, 6.2832f), r = tryN == 0 ? Uniform(0.0f, 60.0f) : 90.0f + 25.0f * tryN;
						RE::NiPoint3 q{ base.x + std::cos(ang) * r, base.y + std::sin(ang) * r, base.z };
						float land = 0;
						if (tryN > 0 && (!RE::TES::GetSingleton()->GetLandHeight(q, land) || std::abs(land - base.z) > 60.0f || GroundKind(a_player, q, land) == 0)) continue;
						if (tryN > 0) q.z = land;
						p = q;
						if (clearOf(q)) break;
					}
					used.emplace_back(p, nowU);
					p.z += R.spawnHeightOffset;
					g_spotBase = base, g_spotBaseSet = true;  // (SpawnOne steps it out of this point's marker)
					return p;
				}
			}
			const auto  base = a_player->GetPosition();
			RE::NiPoint3 pos{};
			for (int tryN = 0; tryN < 8; ++tryN) {  // (in a party: never close to a friend either)
				const float angle = a_player->GetAngleZ() + RE::NI_PI + Uniform(-2.1f, 2.1f);
				const float dist = Uniform(ring[0], ring[1]);
				pos = { base.x + std::sin(angle) * dist, base.y + std::cos(angle) * dist, base.z + R.spawnHeightOffset };
				if (Party::FriendDistance(pos) > ring[0]) {
					break;
				}
			}
			float land = 0;
			if (!interior && RE::TES::GetSingleton()->GetLandHeight(pos, land)) {
				pos.z = land + R.spawnHeightOffset;
			}
			return pos;
		}

		// (Noah: the dead kept getting stuck in the floor) the top of whatever is walkable under a point - paving, a boardwalk,
		// a stair - by a ray from above, not the landscape's height under it
		std::optional<float> WalkTop(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_at, float a_above = 350.0f)
		{
			auto* world = a_cell ? a_cell->GetbhkWorld() : nullptr;
			if (!world) {
				return std::nullopt;
			}
			const float s = RE::bhkWorld::GetWorldScale();
			const RE::NiPoint3 from{ a_at.x, a_at.y, a_at.z + a_above }, to{ a_at.x, a_at.y, a_at.z - 1500.0f };
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(from * s);
			pick.rayInput.to = RE::hkVector4(to * s);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
			{
				RE::BSReadLockGuard lock(world->worldLock);
				world->PickObject(pick);
			}
			if (!pick.rayOutput.HasHit()) {
				return std::nullopt;
			}
			return from.z + (to.z - from.z) * pick.rayOutput.hitFraction;
		}

		// (Noah: the undead rising out of the ground) each one starts under the floor and climbs out over a second and a
		// half - collision and its mind off until it stands on the floor - the ground bursting, a rumble that shakes the
		// view when it is close
		struct Riser
		{
			RE::ActorHandle h;
			float           floorZ, t = 0;
			bool            burst = false;
			int             anim = 0;  // (Noah: Undead Summons Emerge from the Ground) 0 trying, 1 its emerge animation plays, -1 our climb
			bool            portal = false;  // its red portal opened
			bool            hidden = false;  // (Noah: it showed standing on the ground a moment before the emerge took it under) unseen till the emerge has begun
			float           standFor = 0;    // the stand-up clip's length (anim 3)
			int             resets = 0;      // times set back on the floor after rising
		};
		std::vector<Riser> g_risers;
		constexpr float    kRiseDepth = 115.0f, kRiseTime = 1.5f;
		void StartRise(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
		{
			if (!a_actor || g_risers.size() > 40) {
				return;  // (a flood of them: the rest simply stand there)
			}
			auto at = a_actor->GetPosition();
			float floor = at.z;  // (Noah: "some of them get stuck in the ground when spawning") the walkable top under it, if higher
			if (const auto top = WalkTop(a_actor->GetParentCell(), at, 45.0f); top && *top > floor - 40.0f && *top < floor + 40.0f) floor = *top;
			g_risers.push_back({ a_actor->GetHandle(), floor });
			Undead::FreshStep(a_actor);
			a_actor->SetCollision(false);
			a_actor->EnableAI(false);
			at.z -= kRiseDepth;
			a_actor->SetPosition(at, true);
			if (Settings::ScreenShake() && a_player && a_player->GetPosition().GetDistance(at) < 700.0f) {
				RE::ShakeCamera(0.25f, at, 0.6f);
			}
		}
		bool IsRising(RE::Actor* a_a)
		{
			for (const auto& r : g_risers) if (r.anim != 9 && r.h.get().get() == a_a) return true;
			return false;
		}
		// (Noah: stuck or left behind) set at a spawn point near you, on the navmesh, and climbs out of the ground there
		// like a fresh one - never a body popping into place
		void ShowRiser(RE::Actor* a_a, bool a_show);
		void Resurface(RE::Actor* a_a, RE::PlayerCharacter* a_player)
		{
			std::erase_if(g_risers, [&](const Riser& r) { return r.h.get().get() == a_a; });
			ShowRiser(a_a, true), a_a->SetCollision(true), a_a->EnableAI(true);  // (the audit: should StartRise refuse, it isn't left hidden, AI off)
			a_a->SetPosition(SpawnSpot(a_player), true);
			a_a->MoveToNearestNavmesh();
			StartRise(a_a, a_player);
			a_a->StartCombat(a_player);
		}
		void ShowRiser(RE::Actor* a_a, bool a_show)
		{
			if (auto* root = a_a ? a_a->Get3D() : nullptr) {
				if (a_show) root->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
				else root->GetFlags().set(RE::NiAVObject::Flag::kHidden);
			}
		}
		void TickRisers(float a_delta)
		{
			for (auto& r : g_risers) {
				auto a = r.h.get();
				if (!a) {
					r.t = 99;
					continue;
				}
				r.t += a_delta;
				// the draugr's own summoned-from-the-grave animation (Undead Summons Emerge from the Ground replaces it):
				// any of the dead on a draugr skeleton (draugr, skeletons, ...) plays it standing on the floor; the rest climb
				// (Noah's plan: every one of the night's dead comes through the Curse's red portal; elites and bosses a bigger
				// one) - opened once it stands at the floor (Noah: "I hear the bubble but can't see it": it opened while the
				// body still lay under the ground, and had played out by the time it rose)
				const auto openPortal = [&] {
					if (r.portal) return;
					r.portal = true;
					// (the map crashed with a queue of art being built: only the near ones get a portal, a few at a time)
					static auto lastPortal = std::chrono::steady_clock::now() - std::chrono::seconds(5);
					auto* pc = RE::PlayerCharacter::GetSingleton();
					if (!pc || pc->GetPosition().GetDistance(a->GetPosition()) > 4500.0f || std::chrono::steady_clock::now() - lastPortal < std::chrono::milliseconds(120)) return;
					if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::MapMenu::MENU_NAME)) return;
					lastPortal = std::chrono::steady_clock::now();
					if (auto* dh = RE::TESDataHandler::GetSingleton()) {
						const bool big = Director::IsBoss(a.get()) || g_elites.contains(a->GetFormID());
						if (auto* art = dh->LookupForm<RE::BGSArtObject>(big ? gen::kArtPortalBig : gen::kArtPortal, gen::kPlugin)) a->ApplyArtObject(art, big ? 3.0f : 2.4f);
					}
				};
				const bool summoned = g_summoned.contains(a->GetFormID()) || Director::IsBoss(a.get()) || g_elites.contains(a->GetFormID());
				if (r.anim == 0) {
					if (a->Is3DLoaded()) {
						ShowRiser(a.get(), false), r.hidden = true;  // (unseen until the emerge has it in hand)
					}
					if (summoned && a->Is3DLoaded() && r.t > 0.05f) {
						// (elites, bosses, champions, trial elites) never out of the ground: the Curse's portal opens and it steps
						// out of it, its summoning shown
						r.anim = 2, r.t = 0;
						auto f = a->GetPosition();
						f.z = r.floorZ + 2.0f;
						a->SetPosition(f, true);
						a->SetCollision(true);
						r.hidden = true;  // (unseen a breath while the portal opens round it, then it steps out)
						openPortal();
						const bool great = Director::IsBoss(a.get()) || g_elites.contains(a->GetFormID());
						if (great) Forms::PlayAt(gen::Sound::champ_telegraph, a.get());
						continue;
					}
					if (!summoned && a->Is3DLoaded() && Undead::HasClips(a.get())) {
						// (Noah: the dead "come from the ground") lying in the earth it bursts from, it drags itself up - the
						// Mixamo stand-up - and only then runs at you
						r.anim = 3, r.t = 0;
						auto f = a->GetPosition();
						f.z = r.floorZ + 2.0f;
						a->SetPosition(f, true);
						a->SetCollision(true);
						r.standFor = Undead::StandUpSeconds();
						Undead::CueFor(a.get(), Undead::kCueStandUp, r.standFor);
						if (auto* fx = Forms::Ours(gen::kRiseFx); fx && g_fx.size() < 24) {
							if (auto puff = a->PlaceObjectAtMe(fx, false)) {
								puff->SetPosition({ f.x, f.y, r.floorZ });
								g_fx.push_back({ puff->CreateRefHandle(), 3.0f });
							}
						}
						continue;
					}
					if (!summoned && a->Is3DLoaded() && a->NotifyAnimationGraph("summonStart")) {
						// (Noah: the dead "come from the ground") the emerge-from-the-ground animation; no portal for the
						// common dead - the ground bursts instead
						r.anim = 1, r.t = 0;
						auto f = a->GetPosition();
						f.z = r.floorZ + 2.0f;
						a->SetPosition(f, true);
						a->SetCollision(true);
						if (auto* fx = Forms::Ours(gen::kRiseFx); fx && g_fx.size() < 24) {
							if (auto puff = a->PlaceObjectAtMe(fx, false)) {
								puff->SetPosition({ f.x, f.y, r.floorZ });
								g_fx.push_back({ puff->CreateRefHandle(), 3.0f });
							}
						}
					} else if (!summoned && r.t > 0.4f && a->Is3DLoaded()) {
						// no emerge animation for this body: it climbs out of the ground (our own rise, below)
						r.anim = -1, r.t = 0;
						ShowRiser(a.get(), true), r.hidden = false;
					} else if (r.t > 0.4f) {
						// (Noah: the night's spawns look conjured) no draugr rise for this one: it steps out of a portal
						r.anim = 2, r.t = 0;
						auto f = a->GetPosition();
						f.z = r.floorZ + 2.0f;
						a->SetPosition(f, true);
						a->SetCollision(true);
						ShowRiser(a.get(), true), r.hidden = false;
						if (a->Is3DLoaded()) openPortal();  // (the audit: no portal art on a body not built yet)
						continue;
					} else {
						continue;
					}
				}
				if (r.anim == 9) {  // risen: for two seconds more, anyone found sunk under the floor is set back on it
					if (r.t >= 2.0f || a->IsDead()) {
						r.t = 99;
						continue;
					}
					// (Noah: "stuck in the floor for a moment") measured against the ground under it now - a ray from just over
					// its head - not the floor it rose from, and set back at most twice (a third time means the floor it
					// rose to was wrong: the body's own fall is right, and fighting it is the stutter)
					auto at = a->GetPosition();
					float ground = r.floorZ;
					if (const auto top = WalkTop(a->GetParentCell(), { at.x, at.y, r.floorZ }, 60.0f); top && std::abs(*top - r.floorZ) < 90.0f) ground = *top;
					if (at.z < ground - 35.0f && r.resets < 2) {
						++r.resets;
						at.z = ground + 4.0f;
						a->SetPosition(at, true);
						a->SetCollision(true);
						static int told = 0;
						if (told++ < 10) logger::info("night: {:08X} sank into the ground after rising - set back on the floor", a->GetFormID());
					}
					continue;
				}
				if (r.anim == 3) {  // getting up off the ground (its first moment hidden: the clip starts lying down)
					if (r.hidden && (r.t >= 0.08f || a->IsDead())) ShowRiser(a.get(), true), r.hidden = false;
					if (r.t >= r.standFor || a->IsDead()) {
						a->EnableAI(true);
						r.anim = 9, r.t = 0;  // (then watched a moment: it must stay on the floor)
					}
					continue;
				}
				if (r.anim == 2) {  // through the portal: a moment to step out, then it hunts
					if (r.hidden && (r.t >= 0.35f || a->IsDead())) ShowRiser(a.get(), true), r.hidden = false;
					if (r.t >= 0.45f || a->IsDead()) {  // (Noah: "it takes them a few seconds before they chase me - make it instant") the moment it steps out
						a->EnableAI(true);
						// (Noah: "their AI gets confused and they just run in a random direction") it steps out facing you, its
						// hunt begun at once - no package of its own to wander off on first
						if (auto* pc = RE::PlayerCharacter::GetSingleton(); pc && !a->IsDead()) {
							const auto d = pc->GetPosition() - a->GetPosition();
							a->SetAngle({ 0.0f, 0.0f, std::atan2(d.x, d.y) });
							a->StopCombat();
							a->StartCombat(pc);
						}
						r.anim = 9, r.t = 0;  // (then watched a moment: it must stay on the floor)
					}
					continue;
				}
				if (r.anim == 1) {
					if (r.hidden && (r.t >= 0.15f || a->IsDead())) ShowRiser(a.get(), true), r.hidden = false;  // (the emerge's first pose is under the ground)
					if (r.t >= 2.6f || a->IsDead()) {
						// (Noah: "I don't want any zombies to get stuck under the floor") stood on the floor, whatever the animation did
						if (auto at = a->GetPosition(); at.z < r.floorZ - 20.0f) {
							at.z = r.floorZ + 4.0f;
							a->SetPosition(at, true);
						}
						a->SetCollision(true);
						a->EnableAI(true);
						r.anim = 9, r.t = 0;  // (then watched a moment: it must stay on the floor)
					}
					continue;
				}
				const float u = std::clamp(r.t / kRiseTime, 0.0f, 1.0f), e = u * u * (3 - 2 * u);
				auto        at = a->GetPosition();
				if (u >= 1.0f || a->IsDead()) {
					at.z = r.floorZ + 4.0f;
					a->SetPosition(at, true);
					a->SetCollision(true);
					a->EnableAI(true);
					r.anim = 9, r.t = 0;  // (then watched a moment: it must stay on the floor)
					continue;
				}
				at.z = r.floorZ - kRiseDepth * (1.0f - e);
				a->SetPosition(at, true);
				if (!r.burst && u > 0.45f) {  // the second burst as the shoulders break the ground
					r.burst = true;
					if (auto* fx = Forms::Ours(gen::kRiseFx); fx && g_fx.size() < 24) {
						if (auto puff = a->PlaceObjectAtMe(fx, false)) {
							puff->SetPosition({ at.x, at.y, r.floorZ });
							g_fx.push_back({ puff->CreateRefHandle(), 3.0f });
						}
					}
				}
			}
			for (auto& r : g_risers) {
				if (r.t >= 99 && r.hidden) {
					if (auto a = r.h.get()) ShowRiser(a.get(), true);
					r.hidden = false;
				}
			}
			std::erase_if(g_risers, [](const Riser& r) { return r.t >= 99; });
		}
		void EndRisers()  // (dawn, a load: anyone still under the ground is put on it)
		{
			for (auto& r : g_risers) {
				if (auto a = r.h.get(); a && !a->IsDisabled() && !a->IsDeleted()) {  // (the audit: not the ones just vanished)
					ShowRiser(a.get(), true);
					auto at = a->GetPosition();
					at.z = r.floorZ + 4.0f;
					a->SetPosition(at, true);
					a->SetCollision(true);
					a->EnableAI(true);
				}
			}
			g_risers.clear();
		}

		// (Noah: "stop the shield bashing - run straight at me instead of hesitating a few feet away and strafing") a
		// combat style made headlong in place: all offence, no circling, falling back, flanking, stalking or strafing,
		// no dueling or flanking stance, and no bashes
		void MakeHeadlong(RE::TESCombatStyle* cs)
		{
			if (!cs) return;
			cs->generalData.offensiveMult = 1.0f, cs->generalData.defensiveMult = 0.0f, cs->generalData.groupOffensiveMult = 1.0f;
			cs->generalData.avoidThreatChance = 0.0f, cs->generalData.meleeScoreMult = std::max(1.0f, cs->generalData.meleeScoreMult);
			cs->closeRangeData.circleMult = 0.0f, cs->closeRangeData.fallbackMult = 0.0f;
			cs->closeRangeData.flankDistanceMult = 0.0f, cs->closeRangeData.stalkTimeMult = 0.0f;
			cs->longRangeData.strafeMult = 0.0f;
			cs->meleeData.bashMult = 0.0f, cs->meleeData.bashRecoilMult = 0.0f, cs->meleeData.bashAttackMult = 0.0f, cs->meleeData.bashPowerAttackMult = 0.0f;
			cs->flags.reset(RE::TESCombatStyle::FLAG::kDuelingStyle, RE::TESCombatStyle::FLAG::kFlankingStyle);
			cs->flightData.groundAttackChance = 1.0f, cs->flightData.groundAttackTimeMult = 3.0f;
			cs->flightData.perchAttackChance = 0.0f, cs->flightData.diveBombChance = 0.0f, cs->flightData.hoverChance = 0.1f;
		}
		bool SpawnOne(RE::PlayerCharacter* a_player, int a_force = -1, const RE::NiPoint3* a_at = nullptr)
		{
			auto pick = a_force >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(a_force)) : PickEnemy();
			if (!pick) {
				return false;
			}
			// (Noah: the four common kinds spread across the night by their own weights - the sheet - no longer re-rolled
			// out of the Risen Dead's row)
			std::optional<Undead::Type> rolled;
			auto* cell = a_player->GetParentCell();
			if (!cell) return false;  // (the audit: a trial's raise could come before you stand in a cell)
			const bool interior = cell->IsInteriorCell();
			RE::NiPoint3 pos = a_at ? *a_at : SpawnSpot(a_player);
			if (a_at && !interior) {
				float land = 0;
				if (RE::TES::GetSingleton()->GetLandHeight(pos, land)) {
					pos.z = land + R.spawnHeightOffset;
				}
			}

			auto* npc = ResolveSpawnable(Forms::Enemy(*pick), a_player);
			if (!npc) {
				logger::warn("could not resolve {} to an NPC", gen::kEnemies[*pick].id);
				return false;
			}
			// (the audit: placed at you and slid to the spawn point, its parent cell stayed yours - its navmesh snap searched
			// your cell, and one set down in the next cell could sit there without ever getting a body) made where it stands,
			// in that spot's own cell
			RE::NiPointer<RE::TESObjectREFR> ref;
			const bool fromPoint = !a_at && g_spotBaseSet && !interior && std::hypot(g_spotBase.x - pos.x, g_spotBase.y - pos.y) < 400.0f;
			g_spotBaseSet = false;
			if (auto* mark = fromPoint ? SpawnMark(a_player, g_spotBase) : nullptr) {
				ref = mark->PlaceObjectAtMe(npc, false);
				if (auto* ac = ref ? ref->As<RE::Actor>() : nullptr) ac->SetPosition(pos, true);  // (a step off its point, clear of the others)
			} else if (auto* tcell = interior ? cell : RE::TES::GetSingleton()->GetCell(pos); tcell && tcell->IsAttached()) {
				const auto d = a_player->GetPosition() - pos;
				ref = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(npc, pos, { 0.0f, 0.0f, std::atan2(d.x, d.y) }, tcell, interior ? nullptr : a_player->GetWorldspace(), nullptr, nullptr, {}, false, true).get();
			}
			const bool placed = static_cast<bool>(ref);
			if (!ref) ref = a_player->PlaceObjectAtMe(npc, false);
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				logger::warn("spawn failed for {}", gen::kEnemies[*pick].id);
				return false;
			}
			if (!placed) actor->SetPosition(pos, true);
			actor->MoveToNearestNavmesh();
			// on the floor itself: standing at floor level, never under it (a little over it, they settle)
			{
				auto at = actor->GetPosition();
				// (Noah: "stuck in the floor", "stuck on porches") the navmesh's height is the ground it walks on; a ray from
				// over its head only lifts it for a bump in the ground a step high, never onto a porch, a cart or a fence
				if (auto top = WalkTop(cell, at, 45.0f); top && *top > at.z - 40.0f && *top < at.z + 40.0f) {
					at.z = *top + 6.0f;
					actor->SetPosition(at, true);
				}
			}

			// Round health (BO3 curve). Permanent modifier so leveled base health doesn't matter.
			auto*       av = actor->AsActorValueOwner();
			const auto& kindRow = gen::kEnemies[*pick];
			// which of the new dead it is (Undead.cpp): its body, its gait and its own attacks
			const std::string_view rowId = kindRow.id;
			std::optional<Undead::Type> type;
			if (!kindRow.rig) {
				// a creature from the party's mods: its own body, animations and attacks (no rig, no Undying body type)
			} else if (rolled) {
				type = rolled;
			} else if (rowId == "zombie" || rowId == "runner" || rowId == "volatile") {
				type = Undead::Type::Runner;  // (Noah: the Risen Dead fast melee; the Brute's slowness is its row's)
			} else if (rowId == "wailer" || rowId == "mage") {
				type = Undead::Type::Wailer;  // (the Grave Witch: the summoning scream - Undead; her bolt: SetEyes)
			} else if (rowId == "girl") {
				type = Undead::Type::Shambler;  // (Noah: slower, a little more health, slower swings)
			} else if (rowId == "bloater") {
				type = Undead::Type::Bloater;
			} else if (rowId == "shadow") {
				type = Undead::Type::Stalker;
			} else if (rowId == "boss") {
				type = Undead::Type::Wyrm;
			} else if (kindRow.kind == 1) {
				type = Undead::Type::Reaper;
			} else if (kindRow.kind == 2) {
				type = Undead::Type::Necro;
			}
			const int   theme = ThemeNowImpl();
			const float want = HealthFor(g_current) * kindRow.healthMult * (type ? Undead::HealthMult(*type) : 1.0f) * Settings::UndeadHealthMult() * (theme >= 0 ? gen::kEvents[theme].waveHp : 1.0f);
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, want - av->GetPermanentActorValue(RE::ActorValue::kHealth));

			// BO3 tuning: speed tier by round, damage comes from our hit rule (weapon damage turned down),
			// and no faction ties so they only go for players and NPCs leave them alone.
			float speed = 100.0f;
			for (const auto& tier : gen::kSpeedTiers) {
				if (g_current >= static_cast<int>(tier[0])) {
					speed = tier[1];
				}
			}
			speed *= kindRow.speedMult * (g_event >= 0 ? gen::kEvents[g_event].speedMult : g_waveTheme >= 0 ? gen::kEvents[g_waveTheme].waveSpeed : 1.0f) * (type ? Undead::SpeedMult(*type) : 1.0f);
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kSpeedMult, speed - av->GetBaseActorValue(RE::ActorValue::kSpeedMult));
			av->SetActorValue(RE::ActorValue::kAttackDamageMult, gen::kZombieWeaponDamageMult * (type ? Undead::DamageMult(*type) : 1.0f));
			av->SetActorValue(RE::ActorValue::kConfidence, 4);
			if (gen::kIgnoreNpcs) {
				if (auto* base = actor->GetActorBase()) {
					for (auto& f : base->factions) {
						if (f.faction) {
							actor->RemoveFromFaction(f.faction);
						}
					}
				}
				// One undead faction: allies to each other (no infighting), enemies of the player. Aggressive (not
				// very aggressive) so neutral townsfolk and each other are ignored; KeepThemComing keeps them on players.
				if (auto* fac = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESFaction>(gen::kUndeadFaction, gen::kPlugin)) {
					actor->AddToFaction(fac, 0);
				}
				av->SetActorValue(RE::ActorValue::kAggression, 1);
			}
			// Armed: an ancient Nord sword, an iron sword or a dagger (bare-handed undead swing slowly); the Iron
			// Reaper swings its scythe two-handed; necromancers fight with spells; raised skeletons keep their own.
			const std::string_view eid = gen::kEnemies[*pick].id;
			const std::uint32_t    own = !kindRow.rig ? 0 : eid == "bonebrute" ? gen::kZombieClaws : kindRow.kind == 1 ? gen::kReaperScythe : eid == "boss" ? gen::kWyrmClaws : eid == "shadow" ? gen::kShadowClaws : (eid == "zombie" || eid == "runner" || eid == "wailer" || eid == "bloater" || eid == "lich" || eid == "volatile" || eid == "mage" || eid == "girl") ? gen::kZombieClaws : 0;
			if (own) {  // their own fighting style: the Reaper's scythe, Grimmvar's raking claws, the Gloomstalker's quick slashes
				if (auto* w = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(own, gen::kPlugin)) {
					// (Noah: "some of them carry weapons") nothing else in their hands or packs: every other weapon and
					// shield goes, so only their own (unseen) claws are swung
					auto other = actor->GetInventory([w](RE::TESBoundObject& a_obj) {
						if (&a_obj == w) return false;
						if (a_obj.IsWeapon() || a_obj.IsAmmo()) return true;
						auto* ar = a_obj.As<RE::TESObjectARMO>();
						return ar && ar->IsShield();
					});
					for (auto& [obj, data] : other) {
						if (data.first > 0) actor->RemoveItem(obj, data.first, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					}
					if (auto* lh = actor->GetEquippedObject(true); lh && lh != w) {
						if (auto* b = lh->As<RE::TESBoundObject>()) RE::ActorEquipManager::GetSingleton()->UnequipObject(actor, b);
					}
					RE::ActorEquipManager::GetSingleton()->EquipObject(actor, w);
				}
			} else if (kindRow.rig && kindRow.kind == 0 && !actor->GetEquippedObject(false)) {
				static constexpr RE::FormID kBlades[] = { 0x02C66F, 0x012EB7, 0x01397E, 0x02C66F };
				if (auto* w = RE::TESForm::LookupByID<RE::TESObjectWEAP>(kBlades[std::uniform_int_distribution<int>(0, 3)(g_rng)])) {
					actor->AddObjectToContainer(w, nullptr, 1, nullptr);
					RE::ActorEquipManager::GetSingleton()->EquipObject(actor, w);
				}
			}
			// Close range only: bows, crossbows, staves, arrows and attack spells go; swords, axes and maces stay.
			// (Necromancers keep their spells: they are the ranged ones.)
			if (kindRow.kind != 2) {
				auto ranged = [](RE::TESBoundObject& a_obj) {
					if (a_obj.IsAmmo()) {
						return true;
					}
					auto* w = a_obj.As<RE::TESObjectWEAP>();
					return w && (!gen::kZombieMeleeWeapons || w->IsBow() || w->IsCrossbow() || w->IsStaff());
				};
				auto inv = actor->GetInventory(ranged);
				for (auto& [obj, data] : inv) {
					if (data.first > 0) {
						actor->RemoveItem(obj, data.first, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					}
				}
				for (bool left : { false, true }) {
					if (auto* eq = actor->GetEquippedObject(left)) {
						if (auto* b = eq->As<RE::TESBoundObject>(); b && eq->IsWeapon() && ranged(*b)) {
							RE::ActorEquipManager::GetSingleton()->UnequipObject(actor, b);
						}
					}
				}
				if (auto* base = actor->GetActorBase(); base && base->actorEffects) {
					auto* list = base->actorEffects;
					for (std::uint32_t i = 0; list->spells && i < list->numSpells; ++i) {
						auto* sp = list->spells[i];
						if (sp && sp->GetCastingType() != RE::MagicSystem::CastingType::kConstantEffect && (sp->GetDelivery() != RE::MagicSystem::Delivery::kSelf || !kindRow.rig)) {  // (a mod's creature loses its cloaks too: its fire burns through the hit rule)
							actor->RemoveSpell(sp);
						}
					}
				}
				actor->GetActorRuntimeData().selectedSpells[0] = nullptr;
				actor->GetActorRuntimeData().selectedSpells[1] = nullptr;
			}
			if (!kindRow.rig && kindRow.kind == 2) {
				av->SetActorValue(RE::ActorValue::kDestruction, 10.0f);
				av->SetActorValue(RE::ActorValue::kConjuration, 10.0f);
				av->SetActorValue(RE::ActorValue::kArchery, 15.0f);
			}
			if (gen::kEnemies[*pick].elite) {
				const float want = HealthFor(g_current) * gen::elite::healthMult * Settings::UndeadHealthMult();
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, want - av->GetPermanentActorValue(RE::ActorValue::kHealth));
				g_elites[actor->GetFormID()] = { actor->GetHandle(), 3.0f, 5.0f, false };
			}
			g_ours[actor->GetFormID()] = *pick;
			if (auto* base = actor->GetActorBase()) MakeHeadlong(base->GetCombatStyle());  // (its levelled copy's own style too)
			av->SetActorValue(RE::ActorValue::kConfidence, 4.0f);  // (foolhardy: never hangs back)
			av->SetActorValue(RE::ActorValue::kAggression, 2.0f);
			if (const float hm = Director::HarmMult(actor); hm < 0.999f) {  // (Noah: the harpies hit far too hard) its blows weaker too
				av->SetBaseActorValue(RE::ActorValue::kAttackDamageMult, hm);
			}
			g_alive.push_back({ actor->GetHandle(), *pick });
			g_alive.back().fid = actor->GetFormID();
			if (type) {
				Undead::Assign(actor, *type);
			}
			if (kindRow.rig && rowId != "death_hound" && rowId != "skeleton") {
				Undead::SetEyes(actor, rowId);
			}
			if (type) Undead::SetPace(actor, speed / 100.0f);  // (after its row's clips are known: the mage keeps the game's own pace)
			// (Noah: "something better for the spawns instead of coming from the ground - it's constantly looking broken; summon
			// portals with really low audio at the spawn points") every one of the night's dead steps out of the Curse's red
			// portal now, its whoosh barely above a breath (no ground burst, no climbing out)
			Audio::At(gen::Sound::rise, actor, 0.22f);
			g_summoned.insert(actor->GetFormID());
			StartRise(actor, a_player);
			return true;
		}

		bool IsOurs(RE::Actor* a) { return a && g_ours.contains(a->GetFormID()); }

		// ---- telegraphed hazards: a mark on the ground, then it erupts; stand clear or be hurt (boss and champions) ----
		struct Hazard
		{
			RE::NiPoint3     at;
			float            t, radius, share;  // seconds to go, reach, share of the player's health
			std::string_view look;              // blast name (Gun::FxBlast)
			bool             stagger;
		};
		std::vector<Hazard> g_hazards;
		void Hazards(RE::PlayerCharacter* a_player, float a_delta)
		{
			for (auto& h : g_hazards) {
				if ((h.t -= a_delta) > 0) {
					continue;
				}
				Gun::FxBlast(h.look, h.at);
				const auto  pp = a_player->GetPosition();
				const float d = std::hypot(pp.x - h.at.x, pp.y - h.at.y);
				if (d < h.radius && std::abs(pp.z - h.at.z) < 220.0f && !a_player->IsDead()) {
					auto* pav = a_player->AsActorValueOwner();
					Perks::DamagePlayer(pav->GetPermanentActorValue(RE::ActorValue::kHealth) * h.share * Settings::UndeadDamageMult());
					if (h.stagger) {
						a_player->SetGraphVariableFloat("staggerMagnitude", 0.6f);
						a_player->NotifyAnimationGraph("staggerStart");
					}
					std::scoped_lock l(Hud::Lock());
					Hud::Edit().hurtTime = 0.9f;
				}
			}
			std::erase_if(g_hazards, [](const Hazard& h) { return h.t <= 0; });
		}
		RE::NiPoint3 OnGround(RE::NiPoint3 a_at)
		{
			float z = a_at.z;
			if (RE::TES::GetSingleton()->GetLandHeight(a_at, z) && std::abs(z - a_at.z) < 400.0f) {
				a_at.z = z;
			}
			return a_at;
		}

		// ---- Grimmvar, the Undying Wyrm (sheet: boss) ----
		struct BossState
		{
			RE::ActorHandle handle;
			float           spawnIn{ -1 };  // seconds until it rises this night (-1 = not tonight)
			float           breath{ 0 }, roar{ 0 }, summon{ 0 }, step{ 0 }, slam{ 0 }, eruption{ 0 }, meteors{ 0 }, slamWindup{ -1 }, breathWind{ -1 };
			float           combo{ 0 }, comboT{ 0 }, leapT{ -1 };  // Rending Flurry cooldown / time to the next rake; Wyrm Leap airtime
			int             comboLeft{ 0 };
			RE::NiPoint3    leapAt{};
			bool            enraged{ false };
			bool            slainTonight{ false };  // one Grimmvar a night: once he falls nothing calls him again before dawn
			float           maxHp{ 1 };
		} g_boss;

		// The bosses take turns: the first on night 10, the next on night 20, and round again (each time on a later,
		// harder night). Host and party agree, since the night number is shared.
		// (Noah: Grimmvar is one of Diverse Dragons' own) the Herald (row herald, the Dark Dragon) comes only on the
		// Fog Nights, once Alduin's Bane is done; the every-tenth-night bosses take turns without him
		bool g_wantHerald = false;
		int  HeraldRow()
		{
			for (int i = 0; i < static_cast<int>(gen::kEnemies.size()); ++i) {
				if (gen::kEnemies[i].id == std::string_view("herald")) return Forms::Enemy(i) ? i : -1;
			}
			return -1;
		}
		int BossRow()
		{
			if (g_wantHerald) {
				if (const int h = HeraldRow(); h >= 0) return h;
			}
			int rows[16], n = 0, every = 10;
			for (int i = 0; i < static_cast<int>(gen::kEnemies.size()) && n < 16; ++i) {
				if (gen::kEnemies[i].bossEvery > 0 && gen::kEnemies[i].id != std::string_view("herald") && Forms::Enemy(i)) {
					rows[n++] = i, every = gen::kEnemies[i].bossEvery;
				}
			}
			if (n == 0) {
				return -1;
			}
			const int turn = std::max(0, g_current / std::max(1, every) - 1);
			return rows[turn % n];
		}

		std::string BossName()
		{
			const int row = BossRow();
			return row >= 0 ? std::string(gen::kEnemies[row].label) : std::string("The Herald");
		}

		void CastAt(RE::Actor* a_caster, int a_spell, RE::Actor* a_target, float a_strength = 1.0f)
		{
			auto* m = Forms::Spell(a_spell);
			if (m && a_caster) {
				if (auto* c = a_caster->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
					c->CastSpellImmediate(m, false, a_target, a_strength, false, 0.0f, a_caster);
				}
			}
		}

		void PublishBoss(RE::Actor* a_boss)
		{
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			if (!a_boss || a_boss->IsDead()) {
				h.bossName.clear();
				return;
			}
			h.bossName = std::string(gen::kEnemies[BossRow()].label);
			h.bossHp = std::max(0.0f, a_boss->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth)) / g_boss.maxHp;
			h.bossEnraged = g_boss.enraged;
		}

		// ---- the cities' champions (sheet: curse.rites, type champion): a named undead hunted for a rite ----
		struct Champion
		{
			RE::ActorHandle handle;
			int             town{ -1 };
			std::string     name;
			float           maxHp{ 1 };
			bool            slainTonight{ false };
		} g_champ;
		// the mini-boss down: a wonder part for you - the next one you're missing - and the same for everyone in the party
		// (each game gives its own: NetFollow sees the host's count go up); all the parts already found: a reforge's worth
		std::uint8_t g_miniSeen = 0;
		void MiniBossReward()
		{
			if (!Echo::AwardNextPart()) {
				Points::Refund(5000);
				Hud::Banner("Every wonder part is yours - 5,000 Cursed Essence instead", 5.0f);
			}
		}
		void MiniBossSlain()
		{
			++g_miniSlain;
			g_miniSeen = g_miniSlain;
			Points::Add(1500);
			MiniBossReward();
			Stats::Note(Stats::kMiniBoss);
		}
		// (Noah: the boss's wonder part was lost when its body crumbled before you reached it) its death is heard the moment
		// it falls, and the reward is yours then - straight to you, wherever the body goes
		bool g_champDying = false;
		void TickChampion()
		{
			auto c = g_champ.handle.get();
			const bool died = g_champDying || (c && c->IsDead());
			if (!c && !died) {
				return;
			}
			if (died) {
				g_champDying = false;
				Points::Add(1000);
				Forms::Play(gen::Sound::night_end);
				Hud::Banner(g_champ.name + " falls! +1000", 5.0f);
				logger::info("champion: {} slain", g_champ.name);
				const int town = g_champ.town;
				g_champ.handle = {};
				g_champ.slainTonight = true;
				{
					std::scoped_lock l(Hud::Lock());
					if (!g_boss.handle.get()) {
						Hud::Edit().bossName.clear();
					}
				}
				if (town >= 0) {
					Curse::OnChampionSlain(town);
				} else if (town == -2) {
					MiniBossSlain();  // (Noah: the mini-boss's wonder part)
				} else if (town <= -10) {
					Eggs::OnBossSlain(-10 - town);  // (a hidden quest's named foe)
				} else {
					Echo::OnBearerSlain();  // (a word's bearer: Stage 4)
				}
				return;
			}
			if (!g_boss.handle.get()) {  // its own bar while Grimmvar is away
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.bossName = g_champ.name;
				h.bossHp = std::max(0.0f, c->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth)) / g_champ.maxHp;
				h.bossEnraged = false;
			}
		}

		// ---- (Noah: "I like the volatiles concept - drawn by noise: spell impacts, running, shouts...") the Volatiles ----
		// The night listens. Noise builds a meter - a spell bursting, a shout, sprinting, running, a fight - and it fades
		// when you go quiet. Fill it and a Volatile hears you: an elite night-runner, far faster than the rest, many times
		// their health and hitting half again as hard, that comes out of the Curse's portal hunting you. One at a time
		// (two from the eighth night), never twice within a minute and a half, from the second night on.
		float g_noise = 0, g_volatileWait = 30.0f;
		std::vector<RE::ActorHandle> g_volatiles;
		constexpr float kNoiseFull = 100.0f;
		bool IsVolatile(RE::Actor* a) { return a && std::any_of(g_volatiles.begin(), g_volatiles.end(), [&](const RE::ActorHandle& h) { return h.get().get() == a; }); }
		void RaiseVolatile(RE::PlayerCharacter* a_player)
		{
			std::optional<std::size_t> row;
			for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
				if (gen::kEnemies[i].id == "volatile" && Forms::Enemy(i)) row = i;  // (Noah: the infected creep)
			}
			for (std::size_t i = 0; !row && i < gen::kEnemies.size(); ++i) {
				if (gen::kEnemies[i].id == "runner" && Forms::Enemy(i)) row = i;
			}
			if (!row) return;
			RE::NiPoint3 at = SpawnSpot(a_player);
			{
				SummonScope summon;  // (through a portal, like every elite)
				if (!SpawnOne(a_player, static_cast<int>(*row), &at) || g_alive.empty()) return;
			}
			auto v = g_alive.back().handle.get();
			if (!v) return;
			auto* av = v->AsActorValueOwner();
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 1.4f);  // (the row is already a brute's 2.5x: ~6x a runner's)
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kSpeedMult, 45.0f);                                              // far faster than the rest
			av->SetActorValue(RE::ActorValue::kAttackDamageMult, av->GetActorValue(RE::ActorValue::kAttackDamageMult) * 1.5f);
			v->SetDisplayName("Volatile", true);
			Undead::SetVolatile(v.get());
			// (Noah: "remove any taunting or roaring - combat only") it comes through straight into the hunt, no scream
			g_summoned.insert(v->GetFormID());
			g_volatiles.push_back(v->GetHandle());
			Forms::PlayAt(gen::Sound::wailer_scream, v.get());
			Hud::Banner("A VOLATILE HEARS YOU", 3.5f);
			if (Settings::ScreenShake()) RE::ShakeCamera(0.35f, a_player->GetPosition(), 0.8f);
			logger::info("night: a Volatile rises (noise {:.0f})", g_noise);
		}
		void TickVolatiles(RE::PlayerCharacter* a_player, float a_delta)
		{
			// what you do is heard: running and sprinting; the rest comes through Director::Noise
			if (a_player->AsActorState()->IsSprinting()) g_noise += 7.0f * a_delta;
			else if (a_player->IsRunning()) g_noise += 1.5f * a_delta;
			g_noise = std::max(0.0f, g_noise - g_noise * 0.07f * a_delta - 1.0f * a_delta);  // (quiet a while and it fades)
			std::erase_if(g_volatiles, [](const RE::ActorHandle& h) {
				auto a = h.get();
				if (a && a->IsDead()) {
					Points::Add(500);
					Hud::Banner("Volatile slain  +500", 2.5f);
				}
				return !a || a->IsDead() || a->IsDeleted();
			});
			g_volatileWait = std::max(0.0f, g_volatileWait - a_delta);
			const int most = g_current >= 8 ? 2 : 1;
			// (Noah: "I don't want noise to be an issue") the night no longer listens: no Volatile is drawn by noise
			if (false && g_current >= 2 && g_noise >= kNoiseFull && g_volatileWait <= 0 && static_cast<int>(g_volatiles.size()) < most && !Coop::Client()) {
				RaiseVolatile(a_player);
				g_noise = 25.0f;
				g_volatileWait = 90.0f;
			}
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().noise = 0.0f;
		}

		// ---- Gloomstalkers (sheet: elite) ----
		void TickElites(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (g_slowLeft > 0 && (g_slowLeft -= a_delta) <= 0) {
				a_player->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, gen::elite::shriekSlow);
			}
			for (auto it = g_elites.begin(); it != g_elites.end();) {
				auto& e = it->second;
				auto  a = e.handle.get();
				if (!a) {
					it = g_elites.erase(it);
					continue;
				}
				if (a->IsDead()) {
					if (!e.paid) {
						e.paid = true;
						Points::Add(static_cast<int>(gen::elite::killPoints));
						Hud::Banner("Gloomstalker banished +" + std::to_string(static_cast<int>(gen::elite::killPoints)), 2.5f);
					}
					it = g_elites.erase(it);
					continue;
				}
				const float dist = a->GetPosition().GetDistance(a_player->GetPosition());
				// Shadow Lunge: blink to the player's side from mid range.
				if ((e.lunge -= a_delta) <= 0 && dist > gen::elite::lungeMin && dist < gen::elite::lungeMax) {
					const auto  p = a_player->GetPosition();
					const float ang = a_player->GetAngleZ() + Uniform(-1.8f, 1.8f);
					a->SetPosition({ p.x + std::sin(ang) * 150.0f, p.y + std::cos(ang) * 150.0f, p.z + 20.0f }, true);
					Forms::PlayAt(gen::Sound::champ_telegraph, a.get());
					if (auto* fx = Forms::Ours(gen::kRiseFx)) {
						if (auto puff = a->PlaceObjectAtMe(fx, false)) {
							g_fx.push_back({ puff->CreateRefHandle(), 2.0f });
						}
					}
					// Umbral Pounce: out of the shadow straight into a flurry; the dark claws up where you stand
					g_hazards.push_back({ OnGround(p), 0.45f, 150.0f, 0.10f, "tentacle", false });  // (Noah: no movement-blocking hits)
					a->NotifyAnimationGraph("attackPowerStartForward");
					e.lunge = gen::elite::lungeEvery;
				}
				// Terror Shriek: slows and wounds the player nearby.
				if ((e.shriek -= a_delta) <= 0 && dist < gen::elite::shriekRange) {
					auto* pav = a_player->AsActorValueOwner();
					Perks::DamagePlayer(pav->GetPermanentActorValue(RE::ActorValue::kHealth) * gen::elite::shriekDamage * Settings::UndeadDamageMult());
					// (Noah: no movement-blocking attacks) it wounds, it no longer slows you
					Forms::PlayAt(gen::Sound::wailer_scream, a.get());
					{
						std::scoped_lock l(Hud::Lock());
						Hud::Edit().hurtTime = 0.8f;
					}
					Hud::Banner("A Gloomstalker shrieks!", 1.5f);
					e.shriek = gen::elite::shriekEvery;
				}
				++it;
			}
		}

		bool SpawnBoss(RE::PlayerCharacter* a_player)
		{
			const int row = BossRow();
			if (row < 0) {
				return true;  // (none to raise)
			}
			const auto p = a_player->GetPosition();
			const float a = a_player->GetAngleZ();
			RE::NiPoint3 at{ p.x + std::sin(a) * 900.0f, p.y + std::cos(a) * 900.0f, p.z + 60.0f };  // in front: you should see it rise
			if (!SpawnOne(a_player, row, &at)) {
				return false;
			}
			auto boss = g_alive.back().handle.get();
			if (!boss) {
				return false;
			}
			auto* av = boss->AsActorValueOwner();
			const float want = std::max(HealthFor(g_current) * gen::boss::healthMult, gen::boss::minHealth / R.hpScale) * Settings::UndeadHealthMult();
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, want - av->GetPermanentActorValue(RE::ActorValue::kHealth));
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kSpeedMult, 100.0f - av->GetActorValue(RE::ActorValue::kSpeedMult));
			g_boss.handle = boss->GetHandle();
			g_boss.maxHp = want;
			g_boss.enraged = false;
			g_boss.breath = 4, g_boss.roar = 7, g_boss.summon = 10, g_boss.step = gen::boss::stepEvery, g_boss.slam = 2, g_boss.eruption = 6, g_boss.meteors = 8, g_boss.slamWindup = -1;
			g_boss.combo = 3, g_boss.comboLeft = 0, g_boss.leapT = -1, g_boss.breathWind = -1;
			Forms::Play(gen::Sound::boss_arrive);
			Forms::PlayAt(gen::Sound::boss_growl, boss.get());
			Audio::BossMusic(true);
			Hud::Banner(std::string(gen::kEnemies[row].label).append(" rises!"), 5.0f);
			logger::info("boss: {} risen with {:.0f} health", gen::kEnemies[row].id, want);
			return true;
		}

		// Health bars over the wounded dead near you: the nearest twelve, a named, coloured bar for the great ones.
		void PublishEnemyBars(RE::PlayerCharacter* a_player)
		{
			std::vector<Hud::State::EnemyBar> bars;
			auto* cam = RE::Main::WorldRootCamera();
			if (cam) {
				struct Close { RE::Actor* a; float d; int row; };
				std::vector<Close> close;
				const auto pp = a_player->GetPosition();
				for (auto& s : g_alive) {
					auto a = s.handle.get();
					if (!a || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded()) {
						continue;
					}
					auto* av = a->AsActorValueOwner();
					const float mx = av->GetPermanentActorValue(RE::ActorValue::kHealth);
					const float hp = av->GetActorValue(RE::ActorValue::kHealth);
					const bool  great = gen::kEnemies[s.enemy].kind == 1 || gen::kEnemies[s.enemy].kind == 2 || gen::kEnemies[s.enemy].elite;
					const float d = a->GetPosition().GetDistance(pp);
					if (d < 2200.0f && mx > 0 && (hp < mx * 0.999f || great) && a.get() != g_boss.handle.get().get()) {
						close.push_back({ a.get(), d, static_cast<int>(s.enemy) });
					}
				}
				// anyone else fighting you (bandits, wolves, dragons): their bar sits over them too (Skyrim's own bar under the compass is hidden)
				for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
					auto a = h.get();
					if (!a || a.get() == a_player || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded() || IsOursRaw(a.get()) || a.get() == g_boss.handle.get().get()) {
						continue;
					}
					if (!a->IsInCombat() || !Director::IsFoe(a.get())) {
						continue;
					}
					auto* av = a->AsActorValueOwner();
					const float mx = av->GetPermanentActorValue(RE::ActorValue::kHealth), hp = av->GetActorValue(RE::ActorValue::kHealth);
					const float d = a->GetPosition().GetDistance(pp);
					if (d < 2600.0f && mx > 0 && hp < mx * 0.999f) {
						close.push_back({ a.get(), d, -1 });
					}
				}
				std::sort(close.begin(), close.end(), [](const Close& l, const Close& r) { return l.d < r.d; });
				if (close.size() > 12) {
					close.resize(12);
				}
				for (auto& n : close) {
					RE::NiPoint3 head = n.a->GetPosition();
					head.z += n.a->GetHeight() * 1.08f + 12.0f;
					float x = 0, y = 0, z = 0;
					cam->WorldPtToScreenPt3(head, x, y, z, 1e-5f);
					if (z <= 0 || x < 0 || x > 1 || y < 0 || y > 1) {
						continue;
					}
					auto*      av = n.a->AsActorValueOwner();
					const bool other = n.row < 0;
					const bool champ = !other && g_champ.handle.get().get() == n.a;
					const bool great = !other && (gen::kEnemies[n.row].kind == 1 || gen::kEnemies[n.row].kind == 2 || gen::kEnemies[n.row].elite || champ);
					Hud::State::EnemyBar b;
					b.x = x, b.y = 1.0f - y;
					b.id = n.a->GetFormID();
					b.frac = std::clamp(av->GetActorValue(RE::ActorValue::kHealth) / std::max(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth)), 0.0f, 1.0f);
					b.scale = std::clamp(900.0f / std::max(300.0f, n.d), 0.45f, 1.0f) * (great ? 1.25f : 1.0f);
					b.name = other ? std::string(n.a->GetDisplayFullName()) : champ ? g_champ.name : great ? std::string(gen::kEnemies[n.row].label) : std::string();
					b.rank = other ? 3 : champ ? 2 : great ? 1 : 0;
					b.color = 0xC03028;  // every foe bleeds red; toughness shows as fire round the plate
					b.fury = other ? 0 : champ ? 3 : (gen::kEnemies[n.row].elite || gen::kEnemies[n.row].kind == 1) ? 2 : (gen::kEnemies[n.row].kind == 2 || gen::kEnemies[n.row].specialRound) ? 1 : 0;
					bars.push_back(std::move(b));
				}
			}
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().enemyBars = std::move(bars);
		}

		void TickBoss(RE::PlayerCharacter* a_player, float a_delta)
		{
			Hazards(a_player, a_delta);
			if (g_boss.spawnIn > 0 && g_wantHerald && !Story::GrimmvarOpen()) {
				g_boss.spawnIn = -1, g_wantHerald = false;  // (the story: Grimmvar, Alduin's Herald, hunts only after Alduin's Bane)
			}
			if (g_boss.spawnIn > 0 && (g_boss.spawnIn -= a_delta) <= 0) {
				g_boss.spawnIn = -1;
				if (!SpawnBoss(a_player)) {
					g_boss.spawnIn = 3.0f;  // (Noah: the quest's boss sometimes never came) it couldn't rise just now: again shortly
					logger::info("boss: couldn't rise yet; trying again");
				}
			}
			auto boss = g_boss.handle.get();
			if (!boss || boss->IsDisabled()) {
				Audio::BossMusic(false);  // gone with the dawn (or never was)
				return;
			}
			if (boss->IsDead()) {
				Points::Add(static_cast<int>(gen::boss::killPoints));
				Forms::PlayAt(gen::Sound::boss_death, boss.get());
				Audio::BossMusic(false);
				Hud::Banner("The Undying Wyrm falls! +" + std::to_string(static_cast<int>(gen::boss::killPoints)), 6.0f);
				logger::info("boss: slain");
				g_boss.handle = {};
				g_boss.spawnIn = -1;
				g_boss.slainTonight = true;
				Curse::OnBossSlain();
				PublishBoss(nullptr);
				return;
			}
			auto* av = boss->AsActorValueOwner();
			const float hpFrac = av->GetActorValue(RE::ActorValue::kHealth) / std::max(1.0f, g_boss.maxHp);
			if (!g_boss.enraged && hpFrac <= gen::boss::enrageAt) {
				g_boss.enraged = true;
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kSpeedMult, gen::boss::enrageSpeed);
				const auto bp = OnGround(boss->GetPosition());
				for (int k = 0; k < 10; ++k) {  // a ring of fire bursts out round him
					const float a = k * 0.6283f;
					Gun::FxBlast("dragonfire", OnGround(bp + RE::NiPoint3{ std::cos(a) * 260.0f, std::sin(a) * 260.0f, 0 }), 0.04f * k);
				}
				g_boss.summon = 0;  // and the dead answer his howl
				Undead::CueFor(boss.get(), Undead::kCueRoar, 1.6f);
				Forms::PlayAt(gen::Sound::boss_growl, boss.get());
				Hud::Banner(BossName() + " is ENRAGED!", 3.0f);
			}
			const float cd = g_boss.enraged ? gen::boss::enrageCooldownMult : 1.0f;
			const float dist = boss->GetPosition().GetDistance(a_player->GetPosition());
			// Fire Breath: he rears and draws breath (a second to get clear), then breathes a cone of fire at you
			if (g_boss.breathWind >= 0) {
				if ((g_boss.breathWind -= a_delta) < 0) {
					const auto bp = OnGround(boss->GetPosition()), pp = a_player->GetPosition();
					RE::NiPoint3 dir = pp - bp;
					dir.z = 0;
					dir.Unitize();
					const RE::NiPoint3 side{ -dir.y, dir.x, 0 };
					boss->SetHeading(std::atan2(dir.x, dir.y));
					boss->NotifyAnimationGraph("attackPowerStartForward");
					int k = 0;
					for (float t = 170.0f; t < gen::boss::breathRange && k < 12; t += 140.0f, ++k) {
						const float spread = t * 0.32f;  // the cone widens
						for (int j = -1; j <= 1; ++j) {
							const auto at = OnGround(bp + dir * t + side * (spread * j));
							g_hazards.push_back({ at, 0.12f + 0.07f * k, 90.0f + t * 0.12f, gen::boss::eruptionDamage * 0.6f, j == 0 ? "dragonfire" : "", false });
						}
					}
					Forms::PlayAt(gen::Sound::boss_breath, boss.get());
				}
			} else if ((g_boss.breath -= a_delta) <= 0 && dist < gen::boss::breathRange && dist > gen::boss::slamRange && g_boss.leapT < 0 && g_boss.comboLeft == 0) {
				g_boss.breathWind = g_boss.enraged ? 0.75f : 1.05f;
				Undead::CueFor(boss.get(), Undead::kCueRoar, g_boss.breathWind);
				Forms::PlayAt(gen::Sound::boss_breath_wind, boss.get());
				Hud::Banner(BossName() + " draws breath - get out of the way!", 1.2f);
				g_boss.breath = gen::boss::breathEvery * cd;
			}
			// Rending Flurry: three (enraged, five) raking claw swipes, each lunging him forward; the last one rips the ground
			if (g_boss.comboLeft > 0) {
				if ((g_boss.comboT -= a_delta) <= 0) {
					const bool last = --g_boss.comboLeft == 0;
					const auto bp = boss->GetPosition(), pp = a_player->GetPosition();
					RE::NiPoint3 dir = pp - bp;
					dir.z = 0;
					const float len = dir.Length();
					if (len > 1.0f) {
						dir = dir * (1.0f / len);
						boss->SetHeading(std::atan2(dir.x, dir.y));
						const float lunge = std::clamp(len - 170.0f, 0.0f, 140.0f);  // closes in, never through you
						boss->SetPosition(bp + dir * lunge, true);
					}
					boss->NotifyAnimationGraph(last ? "attackPowerStartForward" : (g_boss.comboLeft % 2 ? "attackStart" : "attackStartLeft"));
					Undead::CueFor(boss.get(), last ? Undead::kCueRoar : (g_boss.comboLeft % 2 ? Undead::kCueSwipeR : Undead::kCueSwipeL), last ? 0.5f : 0.45f);
					const auto front = OnGround(boss->GetPosition() + dir * 170.0f);
					g_hazards.push_back({ front, last ? 0.35f : 0.22f, last ? 230.0f : 170.0f, last ? gen::boss::clawLast : gen::boss::clawHit, last ? "giantslam" : "", last });  // (the rakes hurt unseen; the finisher rips the ground)
					Audio::AtPoint(last ? gen::Sound::boss_land : gen::Sound::boss_claw, front, last ? 0.35f : 0.22f);  // when the claws land
					g_boss.comboT = g_boss.enraged ? 0.42f : 0.55f;
				}
			} else if ((g_boss.combo -= a_delta) <= 0 && dist < 520.0f && g_boss.slamWindup < 0 && g_boss.leapT < 0) {
				g_boss.comboLeft = g_boss.enraged ? 5 : 3;
				g_boss.comboT = 0.35f;
				g_boss.combo = gen::boss::comboEvery * cd;
				Forms::PlayAt(gen::Sound::boss_claw_wind, boss.get());
			}
			// Wyrm Leap: he springs into the air, his shadow follows you, then he crashes down where it falls
			if (g_boss.leapT >= 0) {
				if ((g_boss.leapT -= a_delta) < 0) {
					boss->SetPosition(g_boss.leapAt + RE::NiPoint3{ 0, 0, 30.0f }, true);
					boss->NotifyAnimationGraph("attackPowerStartForward");
					for (int k = 0; k < 6; ++k) {
						const float a = k * 1.0472f;
						Gun::FxBlast("dragonfire", OnGround(g_boss.leapAt + RE::NiPoint3{ std::cos(a) * 170.0f, std::sin(a) * 170.0f, 0 }), 0.05f + 0.03f * k);
					}
					Forms::PlayAt(gen::Sound::boss_land, boss.get());
				}
			} else if ((g_boss.roar -= a_delta) <= 0 && dist > 600.0f && dist < 1800.0f && g_boss.comboLeft == 0) {
				g_boss.leapAt = OnGround(a_player->GetPosition());
				g_boss.leapT = 1.1f;
				Undead::CueFor(boss.get(), Undead::kCueCrouch, 0.3f);
				g_hazards.push_back({ g_boss.leapAt, 1.1f, 240.0f, gen::boss::leapDamage, "giantslam", true });
				Gun::FxBlast("paralyze", g_boss.leapAt + RE::NiPoint3{ 0, 0, 8 }, 0.0f);  // where he will land
				if (auto* fx = Forms::Ours(gen::kRiseFx)) {
					if (auto puff = boss->PlaceObjectAtMe(fx, false)) {
						g_fx.push_back({ puff->CreateRefHandle(), 3.0f });
					}
				}
				boss->SetPosition(boss->GetPosition() + RE::NiPoint3{ 0, 0, 2400.0f }, false);  // gone into the night sky
				Hud::Banner(BossName() + " leaps - move!", 1.4f);
				Audio::AtPoint(gen::Sound::boss_leap_wind, g_boss.leapAt);  // from where he will land: the shadow growls
				g_boss.roar = gen::boss::roarEvery * cd;
			}
			// Grave Slam: rears up (a power attack), then the ground round it bursts; get out of reach in time
			if (g_boss.slamWindup >= 0) {
				if ((g_boss.slamWindup -= a_delta) < 0) {
					const auto bp = OnGround(boss->GetPosition());
					g_hazards.push_back({ bp, 0.0f, gen::boss::slamRange + 60.0f, gen::boss::slamDamage, "giantslam", true });
					for (int k = 0; k < 6; ++k) {
						const float a = k * 1.0472f;
						Gun::FxBlast("giantslam", OnGround(bp + RE::NiPoint3{ std::cos(a) * 200.0f, std::sin(a) * 200.0f, 0 }), 0.08f + 0.03f * k);
					}
					Forms::PlayAt(gen::Sound::boss_land, boss.get());
					Forms::PlayAt(gen::Sound::boss_quake, boss.get());
				}
			} else if ((g_boss.slam -= a_delta) <= 0 && dist < gen::boss::slamRange + 80.0f && g_boss.comboLeft == 0 && g_boss.leapT < 0) {
				g_boss.slamWindup = g_boss.enraged ? 0.6f : 0.9f;
				Undead::CueFor(boss.get(), Undead::kCueRoar, g_boss.slamWindup);
				boss->NotifyAnimationGraph("attackPowerStartForward");
				Forms::PlayAt(gen::Sound::boss_quake_wind, boss.get());
				g_boss.slam = gen::boss::slamEvery * cd;
			}
			// Grave Eruption: the earth splits in a line from the Wyrm to you, burst after burst; sidestep it
			if ((g_boss.eruption -= a_delta) <= 0 && dist > 380.0f && dist < 1800.0f) {
				const auto  bp = boss->GetPosition(), pp = a_player->GetPosition();
				RE::NiPoint3 dir = pp - bp;
				dir.z = 0;
				const float len = dir.Length();
				dir.Unitize();
				int k = 0;
				for (float t = 160.0f; t < len + 260.0f && k < 14; t += 150.0f, ++k) {
					g_hazards.push_back({ OnGround(bp + dir * t), 0.55f + 0.11f * k, 110.0f, gen::boss::eruptionDamage, "fireball", false });
				}
				Hud::Banner("The ground splits toward you!", 1.6f);
				Forms::PlayAt(gen::Sound::boss_quake_wind, boss.get());
				for (int q = 0; q < k; q += 3) {  // the bursts racing toward you
					Audio::AtPoint(gen::Sound::boss_quake, OnGround(bp + dir * (160.0f + 150.0f * q)), 0.55f + 0.11f * q, 0.8f);
				}
				g_boss.eruption = 11.0f * cd;
			}
			// Enraged, Ash Fall: burning stars fall round you; each marks the ground a moment before it lands
			if (g_boss.enraged && (g_boss.meteors -= a_delta) <= 0) {
				const auto pp = a_player->GetPosition();
				for (int k = 0; k < 7; ++k) {
					const float a = Uniform(0.0f, 6.2832f), r = k == 0 ? 0.0f : Uniform(120.0f, 520.0f);
					const auto  at = OnGround(pp + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 });
					Gun::FxBlast("dragonfire", at + RE::NiPoint3{ 0, 0, 10 }, 0.05f + 0.1f * k);   // the warning scorch
					g_hazards.push_back({ at, 1.3f + 0.12f * k, 150.0f, gen::boss::eruptionDamage, "fireball", false });
				}
				Hud::Banner("Ash falls from the Wyrm's sky!", 1.6f);
				Audio::AtPoint(gen::Sound::boss_quake, pp, 1.3f, 0.7f);
				g_boss.meteors = 13.0f;
			}
			if ((g_boss.summon -= a_delta) <= 0) {
				const auto bp = boss->GetPosition();
				for (int i = 0; i < static_cast<int>(gen::boss::summonCount); ++i) {
					const float a = i * 2.0f * RE::NI_PI / gen::boss::summonCount;
					RE::NiPoint3 at{ bp.x + std::sin(a) * 220.0f, bp.y + std::cos(a) * 220.0f, bp.z + 40.0f };
					SpawnOne(a_player, 0, &at);
				}
				Hud::Banner(BossName() + " calls the dead!", 2.5f);
				Forms::PlayAt(gen::Sound::boss_growl, boss.get());
				Undead::CueFor(boss.get(), Undead::kCueRoar, 1.3f);
				g_boss.summon = gen::boss::summonEvery * cd;
			}
			if ((g_boss.step -= a_delta) <= 0 && dist > 500.0f && g_boss.leapT < 0) {
				// Wyrm Step: sinks away and bursts out of the ground beside you.
				const auto p = a_player->GetPosition();
				const float a = a_player->GetAngleZ() + Uniform(-2.5f, 2.5f);
				boss->SetPosition({ p.x + std::sin(a) * 300.0f, p.y + std::cos(a) * 300.0f, p.z + 40.0f }, true);
				if (auto* fx = Forms::Ours(gen::kRiseFx)) {
					if (auto puff = boss->PlaceObjectAtMe(fx, false)) {
						g_fx.push_back({ puff->CreateRefHandle(), 3.0f });
					}
				}
				Forms::PlayAt(gen::Sound::boss_quake, boss.get());
				g_boss.step = gen::boss::stepEvery * cd;
			}
			PublishBoss(boss.get());
		}

		void TickFx(float a_delta)
		{
			for (auto& [h, t] : g_fx) {
				if ((t -= a_delta) <= 0) {
					if (auto r = h.get()) {
						r->Disable();
						r->SetDelete(true);
					}
				}
			}
			std::erase_if(g_fx, [](const auto& e) { return e.second <= 0; });
		}

		// Fog Night's ground mist: Skyrim's own low mist (the swirling marsh fog of the Hjaalmarch) laid on the ground
		// in a ring round you, so the fog drifts at knee to chest height instead of greying the screen. The ring
		// follows you; every piece goes at dawn.
		std::vector<RE::ObjectRefHandle> g_mist;
		RE::NiPoint3                     g_mistAt{};
		float                            g_mistTimer = 0;
		void ClearMist()
		{
			for (auto& h : g_mist) {
				if (auto r = h.get()) {
					r->Disable();
					r->SetDelete(true);
				}
			}
			g_mist.clear();
		}
		// (Noah: night atmosphere) how far through the night's waves (0..1; -1 not fighting), and how thick its ground fog:
		// none for the first half, thickening to the last wave; thick all through Blood Fog. Effects: fog strength.
		float NightProgress()
		{
			if (g_phase != Phase::Fighting || g_waves <= 0) return -1.0f;
			const int   killed = std::max(0, g_waveSpawned - static_cast<int>(g_alive.size()));
			const float frac = g_waveSize > 0 ? std::clamp(static_cast<float>(killed) / g_waveSize, 0.0f, 1.0f) : 0.0f;
			return std::clamp((std::max(0, g_wave - 1) + frac) / static_cast<float>(g_waves), 0.0f, 1.0f);
		}
		float MistAmount()
		{
			const float pr = NightProgress();
			if (pr < 0) return 0.0f;
			const float base = ThemeNowImpl() == gen::kEvent_bloodfog ? 1.3f : std::clamp((pr - 0.5f) * 2.0f, 0.0f, 1.0f);
			return base * Settings::FogStrength();
		}
		int g_mistRings = 0;
		void TickMist(float a_delta, float a_amount)
		{
			const int rings = std::clamp(static_cast<int>(std::lround(a_amount * 3.0f)), 0, 4);
			const bool a_on = rings > 0;
			if (rings != g_mistRings && !g_mist.empty()) {
				ClearMist();  // (thicker now: laid again)
			}
			g_mistRings = rings;
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!a_on || !p || !p->GetParentCell() || p->GetParentCell()->IsInteriorCell()) {
				if (!g_mist.empty()) {
					ClearMist();
				}
				return;
			}
			if ((g_mistTimer -= a_delta) > 0) {
				return;
			}
			g_mistTimer = 1.0f;
			const auto here = p->GetPosition();
			if (!g_mist.empty() && std::hypot(here.x - g_mistAt.x, here.y - g_mistAt.y) < 900.0f) {
				return;
			}
			ClearMist();
			g_mistAt = here;
			static const RE::FormID kMist[] = { 0x0B946C, 0x0B945E, 0x0B9466, 0x0B9456, 0x0B9468, 0x0B945B };  // FXMistLowSmokey/Swirls
			std::uniform_real_distribution<float> jitter(-220.0f, 220.0f), yaw(0.0f, 6.2832f);
			int placed = 0;
			for (int ring = 0; ring < g_mistRings; ++ring) {
				const float r = 250.0f + ring * 700.0f;
				const int   n = ring == 0 ? 3 : 6 + ring * 4;
				for (int k = 0; k < n; ++k) {
					const float a = (k + 0.5f * ring) * 6.2832f / n;
					RE::NiPoint3 at{ here.x + std::cos(a) * r + jitter(g_rng), here.y + std::sin(a) * r + jitter(g_rng), here.z };
					float land = at.z;
					if (RE::TES::GetSingleton()->GetLandHeight(at, land)) {
						at.z = land;
					}
					auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kMist[(k + ring) % std::size(kMist)]);
					auto  ref = base ? p->PlaceObjectAtMe(base, false) : nullptr;
					if (!ref) {
						continue;
					}
					ref->SetPosition(at + RE::NiPoint3{ 0, 0, 20.0f });
					ref->SetAngle({ 0, 0, yaw(g_rng) });
					ref->SetScale(1.6f);
					g_mist.push_back(ref->CreateRefHandle());
					++placed;
				}
			}
			static bool said = false;
			if (!said) {
				said = true;
				logger::info("event: fog night ground mist, {} pieces round the player", placed);
			}
		}

		// The night's sky and sound; Fog Night brings Grimmvar out of the mist.
		void StartEvent(int a_event)
		{
			const auto& ev = gen::kEvents[a_event];
			if (auto* w = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESWeather>(ev.weatherId, ev.weatherPlugin)) {
				if (auto* sky = RE::Sky::GetSingleton()) {
					sky->ForceWeather(w, true);
					g_weatherForced = true;
				}
			} else {
				logger::warn("event: weather {}|{:06X} not found", ev.weatherPlugin, ev.weatherId);
			}
			Forms::Play(ev.sound);
			{  // the night's own bed of sound, under everything until dawn
				Audio::LoopStart(Audio::kLoopNight, ev.bed);
			}
			if (ev.boss && !Coop::Client() && g_boss.spawnIn < 0 && !g_boss.handle.get() && !g_boss.slainTonight && Story::GrimmvarOpen()) {
				g_boss.spawnIn = 20.0f, g_wantHerald = true;
			}
			logger::info("event: {} night", ev.id);
		}
		void EndEvent()
		{
			Audio::LoopStop(Audio::kLoopNight, 2500);
			if (g_weatherForced) {
				if (auto* sky = RE::Sky::GetSingleton()) {
					sky->ReleaseWeatherOverride();
					sky->ResetWeather();
				}
				g_weatherForced = false;
			}
			g_event = -1;
		}

		// ---- co-op: the host's night, followed by the party's games ----
		void NetFillImpl(Net::State& s)
		{
			s.phase = g_phase == Phase::Fighting ? 2 : g_phase == Phase::Break ? 1 : 0;
			s.event = static_cast<std::int8_t>(g_phase == Phase::Fighting ? g_event : -1);
			s.pad[0] = static_cast<std::uint8_t>(g_phase == Phase::Fighting ? g_waveTheme + 1 : 0);  // (the themed wave, for the party's dials)
			s.pad[1] = g_miniSlain;                                                                   // (a mini-boss down: everyone's part)
			s.round = g_phase == Phase::Fighting ? g_current : Save::Get(gen::kAV_round);
			s.countdown = g_phase == Phase::Break ? g_breakTimer : 0.0f;
			auto boss = g_boss.handle.get();
			s.bossHp = boss && !boss->IsDead() ? std::max(0.0f, boss->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth)) / std::max(1.0f, g_boss.maxHp) : -1.0f;
			s.bossEnraged = g_boss.enraged;
			for (int k = 0; k < 4; ++k) {
				s.pu[k] = PowerUps::Timer(k);
			}
			s.nuke = g_pause;
		}

		// the clock moved by some hours (forward or back), the day of the month with it
		void ShiftClock(float a_hours)
		{
			auto* cal = RE::Calendar::GetSingleton();
			if (!cal || !cal->gameHour || !cal->gameDaysPassed) {
				return;
			}
			float nh = cal->gameHour->value + a_hours;
			while (nh >= 24.0f) {
				nh -= 24.0f;
				if (cal->gameDay) {
					cal->gameDay->value += 1.0f;
				}
			}
			while (nh < 0.0f) {
				nh += 24.0f;
				if (cal->gameDay && cal->gameDay->value > 1.0f) {
					cal->gameDay->value -= 1.0f;
				}
			}
			cal->gameHour->value = nh;
			cal->gameDaysPassed->value += a_hours / 24.0f;
		}

		void StartRound(int a_round);
		void Dawn();
		// A friend's game: the night, the event, the boss bar and the city quests follow the host's.
		void NetFollow()
		{
			const auto& s = Net::Last();
			g_waveTheme = s.phase == 2 && s.pad[0] > 0 && s.pad[0] <= gen::kEvents.size() ? s.pad[0] - 1 : -1;
			static bool primed = false;
			if (!primed) {
				primed = true, g_miniSeen = s.pad[1];  // (joined late: only what falls from now on)
			} else if (s.pad[1] != g_miniSeen) {
				g_miniSeen = s.pad[1];
				Hud::Banner("THE MINI-BOSS FALLS - A WONDER PART FOR THE PARTY", 5.0f);
				MiniBossReward();
			}
			if (s.phase == 2) {
				if (g_phase != Phase::Fighting || g_current != s.round) {
					Save::Set(gen::kAV_round, std::max(0, s.round - 1));
					StartRound(s.round);  // (raises nothing here: the host's undead come through Skyrim Together)
				}
				if (g_event != s.event) {
					EndEvent();
					g_event = s.event;
					if (g_event >= 0 && g_event < static_cast<int>(gen::kEvents.size())) {
						StartEvent(g_event);
						Hud::Banner("NIGHT " + std::to_string(s.round) + " - " + std::string(gen::kEvents[g_event].banner), 6.0f);
					}
				}
			} else if (s.phase == 1) {
				if (g_phase == Phase::Fighting) {
					Dawn();
				}
				if (g_phase == Phase::Day) {
					// (Noah: "two pop-ups when a night is starting") the night's own title under the circle says it - no banner over it
					Forms::Play(gen::Sound::night_falls);
				}
				g_phase = Phase::Break;
				g_breakTimer = s.countdown;
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().countdown = s.countdown;
			} else if (g_phase != Phase::Day) {
				Dawn();
			}
			for (int k = 0; k < 4; ++k) {  // the party shares power-ups
				if (k != 2 && s.pu[k] > PowerUps::Timer(k) + 0.5f) {
					PowerUps::SetTimer(k, s.pu[k]);
				}
			}
			g_pause = std::max(g_pause, s.nuke);
			for (int t = 0; t < static_cast<int>(gen::kCurseCities.size()) && t < 10; ++t) {
				if (true) {  // (Curse::SetRaw takes only the Dragon Priest and the freeing: trials are each player's own)
					Curse::SetRaw(t, s.curse[t]);
				}
			}
			if (s.boxAv != Save::Get(gen::kAV_box)) {
				Save::Set(gen::kAV_box, s.boxAv);
			}
			// the host's clock: when it moves (the host waits or sleeps, an hour at a time), everyone's moves with it
			if (auto* cal = RE::Calendar::GetSingleton(); s.daysPassed > 0 && cal && cal->gameDaysPassed) {
				const float diff = (s.daysPassed - cal->gameDaysPassed->value) * 24.0f;
				if (std::abs(diff) > 0.2f) {
					ShiftClock(diff);
					if (std::abs(diff) > 0.9f) {
						logger::info("coop: the clock follows the host ({:+.1f} hours)", diff);
					}
				}
			}
			// the host's sky: the same weather here (checked every few seconds; a change blends in)
			if (static float wt = 0; (wt -= 0.25f) <= 0 && s.weather) {
				wt = 3.0f;
				auto* sky = RE::Sky::GetSingleton();
				// the same weather by its plugin; if that plugin isn't here (the host's weather mod), one of Skyrim's of
				// the same kind (rain for rain, snow for snow)
				RE::TESWeather* w = nullptr;
				if (s.weatherPlugin[0]) {
					const std::string plugin(s.weatherPlugin, strnlen(s.weatherPlugin, sizeof(s.weatherPlugin)));
					w = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESWeather>(s.weatherLocal, plugin);
				}
				if (!w && s.weatherClass) {
					for (auto* cand : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESWeather>()) {
						auto* f = cand ? cand->GetFile(0) : nullptr;
						if (f && f->GetFilename() == "Skyrim.esm" && cand->data.flags.underlying() == s.weatherClass) {
							w = cand;
							break;
						}
					}
				}
				if (sky && w && sky->currentWeather != w && sky->mode.get() == RE::Sky::Mode::kFull) {
					sky->ForceWeather(w, false);
					logger::info("coop: weather follows the host ({:08X})", s.weather);
				}
			}
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			const int row = BossRow();
			if (s.bossHp >= 0 && row >= 0) {
				h.bossName = std::string(gen::kEnemies[row].label);
				h.bossHp = s.bossHp;
				h.bossEnraged = s.bossEnraged;
			} else if (!g_boss.handle.get()) {
				h.bossName.clear();
			}
		}

		void StartRound(int a_round)
		{
			g_current = a_round;
			g_special = a_round >= R.specialFirstRound && (a_round - R.specialFirstRound) % R.specialEveryRounds == 0;
			// One round per night: the dead keep coming until dawn. How many walk at once grows night by night.
			g_toSpawn = std::clamp(static_cast<int>(std::ceil(TotalFor(a_round) * gen::kSpawnMultiplier / 3.0f)), AtOnceMin(a_round), a_round < 4 ? AtOnceMin(a_round) : R.maxAlive);
			g_waves = WavesFor(a_round), g_wave = 1, g_waveSize = WaveSize(1), g_waveSpawned = 0, g_waveRest = 0, g_nightCleared = false;
			Curse::OnWave(false);
			g_spawnTimer = 0;
			g_phase = Phase::Fighting;
			g_boss.slainTonight = false;
			g_champ.slainTonight = false;
			if (!g_boss.handle.get()) g_wantHerald = false;
			if (const int row = BossRow(); !Coop::Client() && row >= 0 && a_round >= gen::kEnemies[row].minRound && a_round % gen::kEnemies[row].bossEvery == 0) {
				g_boss.spawnIn = gen::boss::spawnDelay;
			}
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().countdown = 0;
			}
			// (Noah) event nights: 1 in 8 from night 8 (never on a hound night) - Goblin Raid, Harpy Swarm, Armored
			// Champions, Blood Fog: the whole night themed, double septims, double the spawn rate
			g_event = -1;
			g_waveTheme = -1, g_themedCount = 0, g_lastThemed = false, g_finaleSpawned = false;
			if (!g_special && a_round >= gen::kEventMinNight && std::uniform_real_distribution<float>(0, 1)(g_rng) < gen::kEventChance) {
				std::vector<int> ok;
				for (int t = 0; t < static_cast<int>(gen::kEvents.size()); ++t) {
					if (ThemeAvailable(t)) ok.push_back(t);
				}
				if (!ok.empty()) g_event = ok[std::uniform_int_distribution<std::size_t>(0, ok.size() - 1)(g_rng)];
			}
			if (g_event >= 0) {
				g_waveSize = WaveSize(1);  // (the night's theme shapes its waves)
			}
			// the mini-boss: 1 night in 5 (not a boss's night), with the middle wave
			g_miniWave = -1, g_miniWarn = -1, g_miniPick = -1;
			if (!Coop::Client() && !g_special && g_boss.spawnIn < 0 && std::uniform_int_distribution<int>(1, 5)(g_rng) == 1) {
				g_miniWave = std::max(2, (g_waves + 1) / 2);
			}
			if (Coop::Client()) {
				g_event = -1;  // the host's game rolls the night
			}
			if (g_event >= 0) {
				StartEvent(g_event);
				const auto& ev = gen::kEvents[g_event];
				g_toSpawn = std::clamp(static_cast<int>(std::ceil(g_toSpawn * ev.spawnMult)), 8, R.maxAlive * 3 / 2);
				Hud::Banner("NIGHT " + std::to_string(a_round) + " - " + std::string(ev.banner), 6.0f);
			} else {
				Forms::Play(g_special ? gen::Sound::event_bloodmoon : gen::Sound::night_start);
				Hud::Banner(fmt::format("NIGHT {} - WAVE 1 OF {}{}", a_round, g_waves, g_special ? " - THE HOUNDS ARE LOOSE" : ""));
			}
			Stats::NightBegins(a_round);
			logger::info("round {} starts: {} waves (the first of {}, 20 more each), up to {} undead at once, {:.0f} Skyrim health each{}", a_round, g_waves, g_waveSize, g_toSpawn, HealthFor(a_round), g_special ? " (hound night)" : "");
		}

		// Despawning: out of combat and hidden now, deleted a moment later (deleting an actor others are still
		// fighting mid-frame can crash the game).
		std::vector<std::pair<RE::ActorHandle, float>> g_purge;
		std::vector<std::pair<RE::ActorHandle, float>> g_fading;  // the fallen: seconds until the body crumbles
		void Vanish(RE::Actor* a)
		{
			g_ours.erase(a->GetFormID()), g_summoned.erase(a->GetFormID()), g_elites.erase(a->GetFormID());
			a->StopCombat();
			a->Disable();
			g_purge.push_back({ a->GetHandle(), 2.0f });
		}
		void TickPurge(float a_delta)
		{
			for (auto& [h, t] : g_purge) {
				if ((t -= a_delta) <= 0) {
					if (auto a = h.get()) {
						a->SetDelete(true);
					}
				}
			}
			std::erase_if(g_purge, [](const auto& e) { return e.second <= 0; });
		}

		// (Noah: "smoother round flow") the wave's last kill: the world slows to a third for half a second, then back
		std::chrono::steady_clock::time_point g_slowUntil{};
		bool                                  g_slowOn = false;
		void SlowBeat(float a_realSeconds, float a_mult)
		{
			if (Net::InLobby()) return;  // (never in a party: everyone's clock must keep the same time)
			if (auto* t = RE::BSTimer::GetSingleton()) t->SetGlobalTimeMultiplier(a_mult, true);
			g_slowOn = true;
			g_slowUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(a_realSeconds * 1000.0f));
		}
		void TickSlow()
		{
			if (g_slowOn && std::chrono::steady_clock::now() >= g_slowUntil) {
				if (auto* t = RE::BSTimer::GetSingleton()) t->SetGlobalTimeMultiplier(1.0f, true);
				g_slowOn = false;
			}
		}
		// (the round flow) a wave's last three come straight for you: on your trail, a little quicker, and drawn up from
		// far off - no hunting the town for a straggler
		void Stragglers(RE::PlayerCharacter* a_player)
		{
			if (g_waveSpawned < g_waveSize || g_alive.empty() || g_alive.size() > 3) return;
			for (auto& s : g_alive) {
				auto a = s.handle.get();
				if (!a || a->IsDead() || Director::IsBoss(a.get()) || Director::Rising(a.get())) continue;
				if (!s.straggler) {
					s.straggler = true;
					a->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 20.0f);
				}
				if (auto t = a->GetActorRuntimeData().currentCombatTarget.get(); !t || t.get() != a_player) {
					a->StopCombat();
					a->StartCombat(a_player);
				}
			}
		}

		// the night's waves: each one's dead rise from the spawn points until its count is out; when every one of them is
		// down the next wave comes after a breath, and after the last the night breaks into dawn
		// (Noah) the mini-boss: a warning (a roar, the sky flashing, a banner), then it rises near you
		void RaiseMiniBoss(RE::PlayerCharacter* a_player)
		{
			static constexpr std::string_view kRows[] = { "mb_minotaur_lord", "mb_ogre_chief", "mb_ice_titan", "mb_vile_imp", "mb_harpy_matriarch", "brute", "bonebrute" };
			std::vector<std::string_view> ok;
			for (auto r : kRows) {
				for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
					if (gen::kEnemies[i].id == r && Forms::Enemy(i)) ok.push_back(r);
				}
			}
			if (ok.empty()) return;
			const auto row = ok[std::uniform_int_distribution<std::size_t>(0, ok.size() - 1)(g_rng)];
			std::string name;
			for (const auto& e : gen::kEnemies) {
				if (e.id == row) name = std::string(e.label);
			}
			if (row == "brute") name = "The Bone Jotunn";
			// at a spawn point toward you (not on top of you)
			RE::NiPoint3 at = a_player->GetPosition();
			float best = 1e9f;
			for (const auto& pt : g_points.pts) {
				const float d = std::abs(pt.GetDistance(at) - 1400.0f);
				if (d < best) best = d, at = pt;
			}
			if (Director::RaiseChampion(-2, row, name, 14.0f, at)) {
				Hud::Banner(name + " - A MINI-BOSS RISES!", 5.0f);
				Audio::Duck(4.0f);
			}
		}
		void TickMiniBoss(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (g_miniWarn < 0) return;
			if ((g_miniWarn -= a_delta) <= 0) {
				g_miniWarn = -1;
				RaiseMiniBoss(a_player);
			}
		}

		// the night's waves: each one's dead rise from the spawn points until its count is out; when every one of them is
		// down the next wave comes after a breath, and after the last the night breaks into dawn
		void WaveBegins()
		{
			Forms::Play(gen::Sound::round_start);
			Audio::Duck(2.5f);
			if (g_waveTheme >= 0) {
				const auto& ev = gen::kEvents[g_waveTheme];
				Forms::Play(ev.sound);
				Hud::Banner(fmt::format("WAVE {} - {}", g_wave, ev.label), 4.0f);
				Atmos::Lightning(0.8f);
			} else {
				Hud::Banner(fmt::format("WAVE {} OF {}", g_wave, g_waves), 3.0f);
				Atmos::Lightning(0.35f);
			}
			if (g_wave == g_miniWave && !Coop::Client() && !g_champ.handle.get()) {  // the mini-boss's warning
				g_miniWarn = 3.0f;
				Audio::Game(0x0003F35F, RE::PlayerCharacter::GetSingleton(), 0.6f);  // (a roar out in the dark: a dragon's, far off)
				Hud::Banner("SOMETHING STIRS IN THE DARK...", 3.0f);
				Atmos::Lightning(1.0f);
			}
			Curse::OnWave(false);
		}
		void TickWaves(RE::PlayerCharacter* a_player, float a_delta)
		{
			TickMiniBoss(a_player, a_delta);
			if (g_nightCleared) {
				return;
			}
			if (g_waveRest > 0) {
				// (Noah: "a short countdown with a rising sound before each wave") the last three seconds tick, the rise beneath
				const float before = g_waveRest;
				g_waveRest -= a_delta;
				if (before > 3.0f && g_waveRest <= 3.0f) Forms::Play(gen::Sound::charge_up);
				for (int sec : { 3, 2, 1 }) {
					if (before > static_cast<float>(sec) && g_waveRest <= static_cast<float>(sec)) Audio::Ui(gen::Sound::ui_move, 0.8f);
				}
				if (g_waveRest <= 0) {
					WaveBegins();
				}
				return;
			}
			Stragglers(a_player);
			// (the audit: saving is locked at night, so a wave that can't finish ends the run) the progress watchdog: should
			// neither a spawn nor a kill come for two minutes, the wave is helped along - the ones that can't be reached (no
			// body, or far off) crumble, and any still to come are let go - and why is logged. Those near you and whole are
			// left to you; a boss or a champion out holds it off.
			{
				static int   lastSpawned = -1, lastAlive = -1, lastWave = -1;
				static float still = 0;
				const int alive = static_cast<int>(g_alive.size());
				if (g_waveSpawned != lastSpawned || alive != lastAlive || g_wave != lastWave || g_boss.handle.get() || g_champ.handle.get()) {
					lastSpawned = g_waveSpawned, lastAlive = alive, lastWave = g_wave, still = 0;
				} else if ((still += a_delta) >= 120.0f) {
					still = 0;
					int gone = 0;
					for (auto& z : g_alive) {
						auto a = z.handle.get();
						if (a && !a->IsDead() && a->Is3DLoaded() && a->GetPosition().GetDistance(a_player->GetPosition()) < 1500.0f) continue;
						if (a) Vanish(a.get());
						++gone;
					}
					std::erase_if(g_alive, [&](const Spawned& z) { auto a = z.handle.get(); return !a || a->IsDisabled(); });
					logger::warn("night {}: wave {} made no progress for 2 minutes ({} of {} spawned, {} alive) - {} out of reach crumbled{}", g_current, g_wave,
						g_waveSpawned, g_waveSize, alive, gone, g_waveSpawned < g_waveSize ? ", the rest let go" : "");
					if (g_waveSpawned < g_waveSize) g_waveSpawned = g_waveSize;
					lastSpawned = -1;
				}
			}
			if (g_waveSpawned < g_waveSize) {
				const float boost = Curse::SpawnBoost() * (g_event >= 0 ? gen::kEvents[g_event].spawnMult : 1.0f);
				const int   want = std::min(static_cast<int>(g_toSpawn * Curse::SpawnBoost()), R.maxAlive * 3 / 2);
				if (g_pause <= 0 && (g_spawnTimer -= a_delta) <= 0 && static_cast<int>(g_alive.size()) < want && !Movement::Gliding() && !a_player->IsInMidair()) {  // (none dropped in under you while you fly: the crash)
					const int th = ThemeNowImpl();
					const int fin = th >= 0 ? gen::kEvents[th].finale : -1;
					const bool last = g_waveSpawned == g_waveSize - 1;
					if (last && fin >= 0 && !g_finaleSpawned && Forms::Enemy(static_cast<std::size_t>(fin))) {  // the theme's own last one (the Goblin Warlord)
						g_finaleSpawned = true;
						if (SpawnOne(a_player, fin)) {
							++g_waveSpawned;
							Hud::Banner(std::string(gen::kEnemies[fin].label) + " leads them!", 3.0f);
						}
					} else if (SpawnOne(a_player)) {
						++g_waveSpawned;
					}
					g_spawnTimer = SpawnDelay(g_current) / std::max(0.25f, boost);
				}
				return;
			}
			if (!g_alive.empty()) {
				return;
			}
			Curse::OnWave(true);
			Stats::Note(Stats::kWave);
			SlowBeat(0.55f, 0.3f);  // (the last of the wave falls in slow time)
			Audio::Duck(3.0f);
			if (auto* pav = a_player->AsActorValueOwner(); pav && !a_player->IsDead() &&
				pav->GetActorValue(RE::ActorValue::kHealth) < 0.15f * pav->GetPermanentActorValue(RE::ActorValue::kHealth)) {
				Stats::Style("LAST STAND", 100);  // (Noah: style - a wave survived on the last of your health)
			}
			logger::info("night {}: wave {} of {} cleared{}", g_current, g_wave, g_waves, g_waveTheme >= 0 ? fmt::format(" ({})", gen::kEvents[g_waveTheme].id) : std::string());
			if (g_wave >= g_waves && Echo::EndlessNight() && !Coop::Client()) {
				++g_waves;  // (Stage 5: the Endless Night - one more wave, always)
			}
			if (g_wave >= g_waves) {
				g_nightCleared = true;
				g_waveTheme = -1;
				Curse::OnNightCleared();
				Forms::Play(gen::Sound::round_end);
				Director::SkipToDawn(fmt::format("NIGHT {} CLEARED - DAWN BREAKS", g_current).c_str());
				return;
			}
			++g_wave;
			// (Noah) a themed wave: 1 in 6 from wave 3, never two in a row, two a night at most, never the last; not on an
			// event night (the whole night is themed) or a hound night
			const bool wasThemed = g_waveTheme >= 0;
			g_waveTheme = -1;
			g_finaleSpawned = false;
			if (g_event < 0 && !g_special && !Coop::Client() && g_wave >= 3 && g_wave < g_waves && !wasThemed && g_themedCount < 2 &&
				std::uniform_real_distribution<float>(0, 1)(g_rng) < gen::kWaveThemeChance) {
				std::vector<int> ok;
				for (int t = 0; t < static_cast<int>(gen::kEvents.size()); ++t) {
					if (ThemeAvailable(t)) ok.push_back(t);
				}
				if (!ok.empty()) {
					g_waveTheme = ok[std::uniform_int_distribution<std::size_t>(0, ok.size() - 1)(g_rng)];
					++g_themedCount;
				}
			}
			g_waveSize = WaveSize(g_wave);
			g_waveSpawned = 0;
			g_waveRest = kWaveRest;
			Forms::Play(gen::Sound::round_end);
			Hud::Banner(fmt::format("WAVE {} CLEARED - {} MORE", g_wave - 1, g_waves - g_wave + 1), 3.5f);
		}

		// the night holds while waves remain: the clock never reaches dawn (it slows near the end and stops just short)
		void HoldNight(RE::PlayerCharacter* a_player)
		{
			auto* cal = RE::Calendar::GetSingleton();
			if (!cal || !cal->gameHour || g_phase != Phase::Fighting || g_nightCleared || Coop::Client() || Curse::SafeHere(a_player)) {
				return;
			}
			const float h = cal->GetHour();
			const float left = std::fmod(DawnHour() - h + 24.0f, 24.0f);
			if (left > 12.0f) {
				return;  // (not a night hour)
			}
			if (left < 0.25f) {
				const float back = 0.25f - left;
				cal->gameHour->value = std::fmod(cal->gameHour->value - back + 24.0f, 24.0f);
				if (cal->gameDaysPassed) {
					cal->gameDaysPassed->value -= back / 24.0f;
				}
			}
		}

		// the night's clock follows its waves: from dusk to just short of dawn as the waves fall (the sky darkens to
		// midnight halfway through the night's dead), never backward; dawn itself comes when the last one is down
		void PaceNight(RE::PlayerCharacter* a_player, float a_delta)
		{
			auto* cal = RE::Calendar::GetSingleton();
			if (!cal || !cal->gameHour || g_phase != Phase::Fighting || g_nightCleared || Coop::Client() || Curse::SafeHere(a_player) || g_waves <= 0) {
				return;
			}
			const float nightHours = std::fmod(DawnHour() - DuskHour() + 24.0f, 24.0f);
			const float since = std::fmod(cal->GetHour() - DuskHour() + 24.0f, 24.0f);
			if (since > nightHours) {
				return;  // (not a night hour)
			}
			const int   killed = std::max(0, g_waveSpawned - static_cast<int>(g_alive.size()));
			const float frac = g_waveSize > 0 ? std::clamp(static_cast<float>(killed) / static_cast<float>(g_waveSize), 0.0f, 1.0f) : 0.0f;
			const float p = std::clamp((static_cast<float>(std::max(0, g_wave - 1)) + frac) / static_cast<float>(g_waves), 0.0f, 0.97f);
			const float target = p * (nightHours - 0.3f);
			if (target > since) {
				ShiftClock(std::min(target - since, a_delta * 0.03f));  // (at most ~2 game minutes a second: the sky turns smoothly)
			}
		}

		void Dawn()
		{
			int n = 0;
			// Anyone still fighting our undead lets go first.
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				if (auto npc = h.get(); npc && !IsOursRaw(npc.get()) && npc->IsInCombat()) {
					auto tgt = npc->GetActorRuntimeData().currentCombatTarget.get();
					if (tgt && IsOursRaw(tgt.get())) {
						npc->StopCombat();
					}
				}
			}
			// Dawn: the dead (standing or fallen) are gone with the light.
			for (auto& s : g_alive) {
				if (auto a = s.handle.get()) {
					Vanish(a.get());
					++n;
				}
			}
			g_alive.clear();
			for (auto& h : g_corpses) {
				if (auto a = h.get()) {
					Vanish(a.get());
				}
			}
			g_corpses.clear();
			g_settle.clear();
			EndRisers();
			Undead::Reset();  // no mind outlives the night (they reached for people while disabled)
			if (g_skipped) {
				// (Noah: "skip a night... it shouldn't count as a night survived; the following night is the one it would have been")
				logger::info("dawn: night {} passed by - not counted, {} undead crumbled", Save::Get(gen::kAV_round) + 1, n);
				Curse::OnWave(false);  // (a trial's count for the passed night starts over)
			} else if (g_phase == Phase::Fighting) {
				// Dawn ends the round: one more night survived (one more tally on the board).
				Save::Set(gen::kAV_round, g_current);
				Points::Add(gen::kPoints_round_survived);
				Forms::Play(gen::Sound::night_end);
				Stats::NightEnds(g_current);
				logger::info("dawn: night {} survived, {} undead crumbled", g_current, n);
				Party::DawnSave(g_current);  // every game in the party saves at this same dawn
			}
			g_phase = Phase::Day;
			g_skipped = false;
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().countdown = 0;  // (Noah: the countdown stayed up after a skipped night)
			}
			g_toSpawn = 0;
			g_waves = 0, g_wave = 0, g_waveSpawned = 0, g_waveRest = 0;
			g_pause = 0;
			EndEvent();
		}

		// The player on a horse would leave the dead far behind: while they ride, every risen thing runs
		// faster to keep up (kMountChase SpeedMult); off the horse, they slow back to their own pace.
		constexpr float kMountChase = 70.0f;
		void MountChase(RE::PlayerCharacter* a_player)
		{
			const bool riding = a_player && Movement::Riding();
			for (auto& s : g_alive) {
				const float want = riding ? kMountChase : 0.0f;
				if (s.mounted == want) {
					continue;
				}
				if (auto a = s.handle.get(); a && !a->IsDead()) {
					auto* av = a->AsActorValueOwner();
					av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, want - s.mounted);
					av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);  // (so the new speed takes at once)
					av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
				}
				s.mounted = want;
			}
			static bool was = false;
			if (riding != was) {
				was = riding;
				logger::info("director: player {} - the dead {}", riding ? "mounted" : "on foot", riding ? "run faster" : "back to their pace");
			}
		}

		void Prune()
		{
			for (auto it = g_alive.begin(); it != g_alive.end();) {
				auto a = it->handle.get();
				if (!a || a->IsDeleted()) {
					// (the audit: a reused FormID read as one of ours - the nuke struck it, the truce left it unguarded)
					if (it->fid) g_ours.erase(it->fid), g_summoned.erase(it->fid), g_elites.erase(it->fid);
					it = g_alive.erase(it);
				} else if (a->IsDead()) {
					g_corpses.push_back(it->handle);
					g_fading.push_back({ it->handle, Settings::CorpseSeconds() });  // the body lies there 10 s (MCM), then crumbles
					g_settle.push_back({ it->handle, 0.0f });
					PowerUps::MaybeDrop(a->GetPosition());
					Curse::OnUndeadDeath(a.get());
					it = g_alive.erase(it);
				} else {
					++it;
				}
			}
			while (static_cast<int>(g_corpses.size()) > R.maxCorpses) {
				if (auto a = g_corpses.front().get()) {
					Vanish(a.get());
				}
				g_corpses.pop_front();
			}
		}

		// Corpses that died without going limp (killed mid-animation, the body stays standing): knock them over.
		void TickFading(float a_delta)
		{
			for (auto& [h, t] : g_fading) {
				if ((t -= a_delta) <= 0) {
					if (auto a = h.get(); a && !a->IsDisabled()) {
						if (auto* fx = Forms::Ours(gen::kRiseFx)) {  // a puff of grave-dust where it lay
							if (auto puff = a->PlaceObjectAtMe(fx, false)) {
								puff->SetPosition(a->GetPosition());
								g_fx.push_back({ puff->CreateRefHandle(), 2.0f });
							}
						}
						Vanish(a.get());
						std::erase_if(g_corpses, [&](const RE::ActorHandle& c) { return c == h; });
					}
				}
			}
			std::erase_if(g_fading, [](const auto& e) { return e.second <= 0; });
		}

		void SettleCorpses(RE::PlayerCharacter* a_player, float a_delta)
		{
			auto* proc = a_player->GetActorRuntimeData().currentProcess;
			for (auto& c : g_settle) {
				c.age += a_delta;
				auto a = c.handle.get();
				if (!a || !proc || !a->Is3DLoaded()) {
					c.age = 99.0f;
					continue;
				}
				// Every kill goes limp at once and is thrown back from the player (BO3 ragdolls); a body still
				// upright after that is knocked again, harder.
				RE::NiPoint3 away = a->GetPosition() - a_player->GetPosition();
				away.z = 0;
				away.Unitize();
				// pushed from waist height behind: they fold and drop (a push from below the feet floated them up)
				const RE::NiPoint3 from = a->GetPosition() - away * 60.0f + RE::NiPoint3{ 0, 0, 55.0f };
				if (c.tries == 0) {
					proc->KnockExplosion(a.get(), from, 1.6f);
					++c.tries;
				} else if (c.age > 0.6f && !a->IsInRagdollState()) {
					proc->KnockExplosion(a.get(), from, 3.0f + c.tries);
					if (c.tries++ == 1) {
						logger::info("night: knocked over a standing corpse");
					}
					c.age = 0.1f;
				} else if (c.age > 2.5f) {
					c.age = 99.0f;
				}
			}
			std::erase_if(g_settle, [](const Settle& c) { return c.age > 3.0f || c.tries > 5; });
		}

		// ---- night's champions: the Iron Reaper's sweep and the Necromancer's raised dead ----
		int KindIndex(int a_kind)
		{
			for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
				if (gen::kEnemies[i].kind == a_kind) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		// (Noah: "my character still doesn't take damage from the Curse's enemies") their swings never reached Skyrim's hit
		// (no hit was ever logged from one): within arm's reach and facing you, one of the dead that hasn't landed a blow in
		// a while swings, and the blow lands - the same share of your health as a Skyrim hit of theirs would take
		std::vector<std::pair<RE::ActorHandle, float>> g_queuedHits;  // (Skyrim's own blows, landing when the swing does)
		void TickReach(RE::PlayerCharacter* a_player, float a_delta)
		{
			static std::unordered_map<RE::FormID, float> swing;  // seconds until its next swing lands (<0: none coming)
			if (!a_player || a_player->IsDead() || Coop::Client()) return;
			const auto pp = a_player->GetPosition();
			for (auto& s : g_alive) {
				auto a = s.handle.get();
				if (!a || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded() || s.frenzy > 0) continue;
				const int kind = gen::kEnemies[s.enemy].kind;
				if (Director::IsBoss(a.get()) || s.dash > 0 || s.stun > 0 || (kind == 1 && s.windup >= 0)) continue;  // (the bosses have their own blows; a brute mid-move is busy)
				auto&       t = swing.try_emplace(a->GetFormID(), -1.0f).first->second;
				const auto  ap = a->GetPosition();
				const float dist = std::hypot(ap.x - pp.x, ap.y - pp.y), dz = std::abs(ap.z - pp.z);
				// (Noah: "they can hit me before I'm in range to hit them with my sword - make their melee range closer") a
				// sword's reach, not a spear's: about the same distance your own blade lands from
				const float reach = 95.0f + a->GetBoundRadius() * 0.5f;
				if (t >= 0) {
					if ((t -= a_delta) < 0) {
						// (Noah: "the damage at the end of their contact swing, not the beginning") it waits for the claws to meet you
						if (const float wait = Undead::HitDelay(a.get()); wait > 0.02f) {
							t = wait;
							continue;
						}
						if (dist < reach + 15.0f && dz < 160.0f && Perks::SinceZombieHit(a->GetFormID()) > 0.5f) {
							Perks::OnZombieHit(a.get());
							if (static int said = 0; said++ < 10) logger::info("hurt: {} reached you ({:.0f} away)", a->GetName(), dist);
						}
						t = -1.0f;
					}
					continue;
				}
				// (Noah: "delay their swinging and attacking frequency") a swing every 2.6 s or so each, give or take
				static std::unordered_map<RE::FormID, float> cool;  // seconds until it may swing again (landed or missed)
				auto& c = cool.try_emplace(a->GetFormID(), 0.0f).first->second;
				if ((c -= a_delta) > 0.0f) continue;
				if (dist > reach || dz > 160.0f || Perks::SinceZombieHit(a->GetFormID()) < 2.6f) continue;
				if (cool.size() > 400) cool.clear();
				const float face = std::atan2(pp.x - ap.x, pp.y - ap.y);
				float d = std::fmod(face - a->GetAngleZ() + 3.0f * RE::NI_PI, 2.0f * RE::NI_PI) - RE::NI_PI;
				if (std::abs(d) > 1.2f) continue;
				a->NotifyAnimationGraph("attackStart");  // (the swing shows; its blow lands a moment later)
				t = 0.55f;  // (a beat longer to see it coming)
				cool[a->GetFormID()] = (s.berserk ? 1.4f : 2.6f) + std::uniform_real_distribution<float>(0.0f, 0.9f)(g_rng);  // (the Skeleton Mutant berserk: swipe after swipe)
			}
			if (swing.size() > 400) std::erase_if(swing, [](const auto& e) { return e.second < 0; });
			// Skyrim's own blows from them, held until their swing's claws meet you
			for (auto& q : g_queuedHits) {
				if ((q.second -= a_delta) > 0) continue;
				q.second = -99.0f;
				auto z = q.first.get();
				if (!z || z->IsDead() || z->IsDisabled()) continue;
				const auto zp = z->GetPosition();
				if (std::hypot(zp.x - pp.x, zp.y - pp.y) < 95.0f + z->GetBoundRadius() * 0.5f + 80.0f && std::abs(zp.z - pp.z) < 180.0f && Perks::SinceZombieHit(z->GetFormID()) > 0.4f) Perks::OnZombieHit(z.get());
			}
			std::erase_if(g_queuedHits, [](const auto& q) { return q.second <= -98.0f; });
		}
		// (the Ettin's frost) you slowed a while; (its roar) the dead near it quicker a while
		float g_chill = 0;
		bool  g_chilled = false;
		void ChillPlayer(float a_seconds) { g_chill = std::max(g_chill, a_seconds); }
		std::vector<std::pair<RE::ActorHandle, float>> g_roused;
		void Rouse(const RE::NiPoint3& a_at, float a_r, RE::Actor* a_self)
		{
			for (auto& o : g_alive) {
				auto z = o.handle.get();
				if (!z || z.get() == a_self || z->IsDead() || z->GetPosition().GetDistance(a_at) > a_r) continue;
				if (std::any_of(g_roused.begin(), g_roused.end(), [&](const auto& e) { return e.first == o.handle; })) continue;
				z->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 25.0f);
				g_roused.push_back({ o.handle, 6.0f });
			}
		}
		void TickBruteAfter(RE::PlayerCharacter* a_player, float a_delta)
		{
			auto* pav = a_player->AsActorValueOwner();
			g_chill = std::max(0.0f, g_chill - a_delta);
			if (g_chill > 0 && !g_chilled) {
				pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -30.0f), g_chilled = true;
				pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f), pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
			} else if (g_chill <= 0 && g_chilled) {
				pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 30.0f), g_chilled = false;
				pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f), pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
			}
			for (auto& [h, t] : g_roused) {
				if ((t -= a_delta) > 0) continue;
				if (auto z = h.get(); z && !z->IsDead()) z->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -25.0f);
			}
			std::erase_if(g_roused, [](const auto& e) { return e.second <= 0; });
			// (the Goblin Warlord fallen) its goblins scatter, and are gone
			static std::vector<std::pair<RE::ActorHandle, float>> fleeing;
			for (auto& [h, t] : fleeing) {
				if ((t -= a_delta) > 0) continue;
				if (auto g = h.get(); g && !g->IsDead()) g->KillImmediate();
			}
			std::erase_if(fleeing, [](const auto& e) { return e.second <= 0; });
			for (auto& s : g_alive) {
				if (s.band.empty() || gen::kEnemies[s.enemy].id != "hf_goblin_warlord") continue;
				auto w = s.handle.get();
				if (w && !w->IsDead()) continue;
				for (auto& h : s.band) {
					if (auto g = h.get(); g && !g->IsDead()) {
						g->AsActorValueOwner()->SetActorValue(RE::ActorValue::kConfidence, 0);
						g->StopCombat();
						g->StartCombat(a_player);  // (cowardly now: it runs)
						fleeing.push_back({ h, 5.0f });
					}
				}
				s.band.clear();
			}
		}
		void TickChampions(RE::PlayerCharacter* a_player, float a_delta)
		{
			// a necromancer raises our own dead (never Skyrim's skeletons with their swords and maces)
			static int  minionRow = [] { for (int i = 0; i < static_cast<int>(gen::kEnemies.size()); ++i) if (gen::kEnemies[i].id == "zombie") return i; return -1; }();
			static std::vector<RE::ActorHandle> raised;
			std::erase_if(raised, [](const RE::ActorHandle& h) { auto a = h.get(); return !a || a->IsDead() || a->IsDeleted(); });
			std::vector<RE::NiPoint3> raise;
			std::vector<std::pair<RE::ActorHandle, RE::NiPoint3>> calls;  // (the Goblin Warlord's horn)
			static int goblinRow = [] { for (int i = 0; i < static_cast<int>(gen::kEnemies.size()); ++i) if (gen::kEnemies[i].id == "hf_goblin") return i; return -1; }();
			int minions = static_cast<int>(raised.size());
			for (auto& s : g_alive) {
				const int kind = gen::kEnemies[s.enemy].kind;
				if (kind != 1 && kind != 2) {
					continue;
				}
				auto a = s.handle.get();
				if (!a || a->IsDead() || !a->Is3DLoaded()) {
					continue;
				}
				const float dist = a->GetPosition().GetDistance(a_player->GetPosition());
				auto*       av = a->AsActorValueOwner();
				if (kind == 1) {
					// (Noah: "I don't want brutes to have that snare ability - I want each brute to have their own unique
					// ability") no chain hook, no shared scythe: each its own move below, its blows otherwise its own (TickReach)
					const std::string_view id = gen::kEnemies[s.enemy].id;
					const float maxHp = av->GetPermanentActorValue(RE::ActorValue::kHealth), hp = av->GetActorValue(RE::ActorValue::kHealth);
					// enraged below a third of its health: faster, and its move comes round sooner
					if (!s.enraged && hp < maxHp * 0.35f) {
						s.enraged = true;
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 35.0f);
						Forms::PlayAt(gen::Sound::champ_enrage, a.get());
					}
					const RE::NiPoint3 ap = a->GetPosition(), pp = a_player->GetPosition();
					const float  yaw = a->GetAngleZ();
					const RE::NiPoint3 fwd{ std::sin(yaw), std::cos(yaw), 0.0f };
					RE::NiPoint3 toP = pp - ap;
					toP.z = 0;
					const float  flat = toP.Length();
					const RE::NiPoint3 dirP = flat > 1.0f ? toP / flat : fwd;
					const float  dz = std::abs(pp.z - ap.z);
					const bool   up = a_player->IsInMidair();  // (jumped: the ground's blows miss you)
					const bool   alive = !a_player->IsDead();
					const float  cdm = s.enraged ? 0.65f : 1.0f;
					auto*        cell = a->GetParentCell();
					auto clear = [&] { bool l = false; return a->HasLineOfSight(a_player, l) && l; };
					auto ring = [&](const RE::NiPoint3& c, float r, int n, std::string_view look) {
						for (int k = 0; k < n; ++k) {
							const float t = 2.0f * RE::NI_PI * k / n;
							Gun::FxBlast(look, OnGround(c + RE::NiPoint3{ std::cos(t) * r, std::sin(t) * r, 0.0f }) + RE::NiPoint3{ 0, 0, 10 }, 0.02f * k);
						}
					};
					auto knock = [&](float mag) {
						if (auto* pr = a_player->GetActorRuntimeData().currentProcess) pr->KnockExplosion(a_player, ap, mag);
					};
					auto windUp = [&](float sec, const char* anim) {
						s.windup = sec;
						if (anim) a->NotifyAnimationGraph(anim);
						Forms::PlayAt(gen::Sound::champ_telegraph, a.get());
					};
					// a charge or a leap in flight (the body carried, its own legs running or its leap clip playing)
					if (s.dash > 0) {
						s.dash -= a_delta;
						if (id == "hf_minotaur") {  // (it bends its run toward you, but slowly: a side-step beats it)
							const float want = std::atan2(dirP.x, dirP.y), have = std::atan2(s.dashDir.x, s.dashDir.y);
							float d = std::fmod(want - have + 3.0f * RE::NI_PI, 2.0f * RE::NI_PI) - RE::NI_PI;
							d = std::clamp(d, -0.7f * a_delta, 0.7f * a_delta);
							s.dashDir = { std::sin(have + d), std::cos(have + d), 0.0f };
						}
						if (id == "bonebrute") {  // the leap: an arc onto where you stood
							const float u = std::clamp(1.0f - s.dash / s.dashLen, 0.0f, 1.0f);
							RE::NiPoint3 at = s.dashFrom + (s.dashTo - s.dashFrom) * u;
							// (the clip itself lifts the body: its jump attack's own arc, 1.3 m at the top)
							a->SetPosition(at, true);
							if (s.dash <= 0) {  // it lands: the ground cracks
								Gun::FxBlast("giantslam", s.dashTo);
								ring(s.dashTo, 200.0f, 6, "dark");
								Forms::PlayAt(gen::Sound::boss_quake, a.get());
								if (alive && pp.GetDistance(s.dashTo) < 300.0f && !up) {
									Director::HurtPlayer(0.15f, true);
									Hud::Banner("CRUSHED", 0.8f);
								}
								s.ability = 9.0f * cdm;
							}
						} else {
							const float speed = id == "hf_minotaur" ? 760.0f : 860.0f;
							const RE::NiPoint3 chest = ap + RE::NiPoint3{ 0, 0, 70 };
							a->SetHeading(std::atan2(s.dashDir.x, s.dashDir.y));
							if (alive && flat < 120.0f && dz < 150.0f) {  // it runs you down
								s.dash = -1.0f;
								if (id == "hf_minotaur") {
									Director::HurtPlayer(0.18f, false);
									knock(1.2f);
									Hud::Banner("GORED", 0.8f);
								} else {
									Director::HurtPlayer(0.16f, true);
									Hud::Banner("BASHED", 0.8f);
								}
								Forms::PlayAt(gen::Sound::boss_land, a_player);
							} else if (RayBlocked(cell, chest, chest + s.dashDir * 95.0f)) {  // into a wall
								s.dash = -1.0f;
								Forms::PlayAt(gen::Sound::boss_land, a.get());
								Gun::FxBlast("giantslam", ap + s.dashDir * 60.0f);
								if (id == "bloater") {  // the Barrow Champion: dazed for three seconds - your moment
									s.stun = 3.0f, s.stunAt = ap;
									a->NotifyAnimationGraph("staggerStart");
									if (flat < 2500.0f) Hud::Banner("STUNNED - STRIKE NOW", 1.2f);
								} else {
									s.stun = 1.0f, s.stunAt = ap;
									a->NotifyAnimationGraph("staggerStart");
								}
							} else {
								RE::NiPoint3 next = ap + s.dashDir * (speed * a_delta);
								next = OnGround(next);
								if (std::abs(next.z - ap.z) > 60.0f) next.z = ap.z;  // (indoors no land: the floor it is on)
								a->SetPosition(next, true);
							}
						}
						continue;
					}
					if (s.stun > 0) {  // dazed where it hit
						s.stun -= a_delta;
						a->SetPosition(s.stunAt, true);
						continue;
					}
					if (s.windup >= 0) {
						if ((s.windup -= a_delta) >= 0) continue;
						// the move it wound up for
						if (id == "reaper") {  // Thane of the Mound - Grave Sweep: a half circle in front of it
							for (int k = -2; k <= 2; ++k) {
								const float t = yaw + 0.38f * k;
								Gun::FxBlast("dark", OnGround(ap + RE::NiPoint3{ std::sin(t) * 220.0f, std::cos(t) * 220.0f, 0 }) + RE::NiPoint3{ 0, 0, 40 }, 0.03f * (k + 2));
							}
							Forms::PlayAt(gen::Sound::boss_claw, a.get());
							if (alive && flat < 340.0f && dz < 160.0f && (fwd.x * dirP.x + fwd.y * dirP.y) > -0.05f && !up) {
								Director::HurtPlayer(0.15f, false);
								auto* pav = a_player->AsActorValueOwner();
								pav->DamageActorValue(RE::ActorValue::kStamina, pav->GetActorValue(RE::ActorValue::kStamina) * 0.5f);
								knock(0.6f);
								Hud::Banner("SWEPT", 0.8f);
							}
							s.ability = 7.0f * cdm;
						} else if (id == "brute") {  // the Bone Jotunn - Ground Pound: a ring 6 m round
							Gun::FxBlast("giantslam", ap);
							ring(ap, 240.0f, 8, "shoutarea");
							Forms::PlayAt(gen::Sound::boss_quake, a.get());
							if (alive && flat < 420.0f && dz < 150.0f && !up) {
								Director::HurtPlayer(0.14f, false);
								a_player->SetGraphVariableFloat("staggerMagnitude", 0.9f);
								a_player->NotifyAnimationGraph("staggerStart");
								Hud::Banner("QUAKE", 0.8f);
							}
							s.ability = 8.0f * cdm;
						} else if (id == "king") {  // the Gatekeeper - Sealed Gate: bones rise behind you
							s.gate = 4.0f, s.gateN = dirP, s.gateAt = pp + dirP * 220.0f, s.gateFx = 0, s.gateHp = hp;
							Forms::PlayAt(gen::Sound::champ_summon, a.get());
							if (flat < 2500.0f) Hud::Banner("THE GATE IS SEALED", 1.0f);
							s.ability = 15.0f * cdm;
						} else if (id == "knight") {  // Thorgar - Skull Toss: a burning skull where you stand, then a fire that lingers
							const RE::NiPoint3 at = OnGround(pp);
							Forms::PlayAt(gen::Sound::cast_heavy, a.get());
							g_hazards.push_back({ at, 0.7f, 150.0f, 0.08f, "fireball", false });
							for (int k = 1; k <= 5; ++k) g_hazards.push_back({ at, 0.7f + 0.5f * k, 130.0f, 0.025f, "atrofire", false });
							s.ability = 6.0f * cdm;
						} else if (id == "bloater" || id == "hf_minotaur") {  // Shield Charge / Gore Rush
							s.dashDir = dirP, s.dashLen = s.dash = id == "hf_minotaur" ? 1.8f : 1.2f;
							Forms::PlayAt(gen::Sound::boss_growl, a.get());
							s.ability = (id == "hf_minotaur" ? 10.0f : 9.0f) * cdm;
						} else if (id == "hf_ettin") {  // the frost head breathes
							s.breath = 1.5f, s.breathTick = 0;
							Forms::PlayAt(gen::Sound::boss_breath, a.get());
							s.ability = 8.0f * cdm;
						} else if (id == "bonebrute") {  // the Skeleton Mutant leaps
							s.dashFrom = ap, s.dashTo = OnGround(pp - dirP * 60.0f), s.dashLen = s.dash = 1.07f;  // (frames 16-48 of its jump attack: in the air)
						}
						continue;
					}
					// (the Gatekeeper) the gate's own bones: you cannot walk back through them
					if (s.gate > 0) {
						s.gate -= a_delta;
						const RE::NiPoint3 side{ -s.gateN.y, s.gateN.x, 0.0f };
						if ((s.gateFx -= a_delta) <= 0) {
							s.gateFx = 0.45f;
							for (int k = -3; k <= 3; ++k) Gun::FxBlast("paralyze", OnGround(s.gateAt + side * (110.0f * k)) + RE::NiPoint3{ 0, 0, 20 }, 0.02f * (k + 3));
						}
						const RE::NiPoint3 rel = pp - s.gateAt;
						const float past = rel.x * s.gateN.x + rel.y * s.gateN.y, along = std::abs(rel.x * side.x + rel.y * side.y);
						if (alive && past > -20.0f && along < 380.0f && std::abs(rel.z) < 250.0f) {
							a_player->SetPosition(pp - s.gateN * (past + 30.0f), true);
						}
						if (hp < s.gateHp - maxHp * 0.08f) {  // hurt it hard enough and the bones fall
							s.gate = 0;
							for (int k = -3; k <= 3; ++k) Gun::FxBlast("dark", OnGround(s.gateAt + side * (110.0f * k)) + RE::NiPoint3{ 0, 0, 40 });
							if (flat < 2500.0f) Hud::Banner("THE GATE BREAKS", 0.8f);
						}
					}
					// (the Ettin) frost breath in a cone before it: slows you
					if (s.breath > 0) {
						s.breath -= a_delta;
						if ((s.breathTick -= a_delta) <= 0) {
							s.breathTick = 0.25f;
							for (int k = 1; k <= 3; ++k) Gun::FxBlast("frost", ap + fwd * (150.0f * k) + RE::NiPoint3{ 0, 0, 90 });
							if (alive && flat < 540.0f && dz < 200.0f && (fwd.x * dirP.x + fwd.y * dirP.y) > 0.8f) {
								Director::HurtPlayer(0.02f, false);
								ChillPlayer(3.0f);
							}
						}
					}
					s.ability -= a_delta;
					s.hook -= a_delta;
					if (id == "bonebrute" && !s.berserk && hp < maxHp * 0.5f) {  // past half: it roars and goes berserk
						s.berserk = true;
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 40.0f);
						Forms::PlayAt(gen::Sound::champ_enrage, a.get());
						if (flat < 2500.0f) Hud::Banner("BERSERK", 0.9f);
					}
					if (s.ability > 0 && !(id == "hf_ettin" && s.hook <= 0) && !(id == "hf_goblin_warlord")) continue;
					if (id == "reaper" && flat < 320.0f) windUp(0.8f, "attackPowerStartForward");
					else if (id == "brute" && flat < 600.0f) windUp(0.9f, "attackPowerStart");
					else if (id == "king" && flat > 250.0f && flat < 1500.0f && clear()) windUp(0.6f, "attackStart");
					else if (id == "knight" && flat > 400.0f && flat < 1500.0f && clear()) windUp(0.6f, "attackStart");
					else if ((id == "bloater" || id == "hf_minotaur") && flat > 450.0f && flat < 1600.0f && clear()) windUp(0.8f, "attackPowerStartForward");
					else if (id == "bonebrute" && flat > 450.0f && flat < 1400.0f && clear()) {
						windUp(0.53f, nullptr);  // (its crouch: the clip's first 16 frames)
						Undead::CueFor(a.get(), Undead::kCueLeap, 0.3f);
					}
					else if (id == "hf_ettin") {
						if (s.hook <= 0 && flat < 2500.0f) {  // the other head roars: the dead near it come on faster
							s.hook = 13.0f * cdm;
							Gun::FxBlast("shoutarea", ap + RE::NiPoint3{ 0, 0, 80 });
							Forms::PlayAt(gen::Sound::boss_growl, a.get());
							Rouse(ap, 900.0f, a.get());
						} else if (s.ability <= 0 && flat < 480.0f && (fwd.x * dirP.x + fwd.y * dirP.y) > 0.6f) windUp(0.6f, "attackPowerStart");
					}
					else if (id == "hf_goblin_warlord" && s.ability <= 0 && flat < 3000.0f && goblinRow >= 0) {  // Rally Horn
						std::erase_if(s.band, [](const RE::ActorHandle& h) { auto g = h.get(); return !g || g->IsDead(); });
						s.ability = 20.0f;
						const int n = std::min(3, 4 - static_cast<int>(s.band.size()));
						if (n > 0) {
							Forms::PlayAt(gen::Sound::shout_toll, a.get());
							if (flat < 2500.0f) Hud::Banner("THE WAR HORN SOUNDS", 1.0f);
							for (int k = 0; k < n; ++k) {
								const float ang = Uniform(0.0f, 2.0f * RE::NI_PI);
								const RE::NiPoint3 at = ap + RE::NiPoint3{ std::cos(ang) * 160.0f, std::sin(ang) * 160.0f, 0.0f };
								calls.push_back({ s.handle, at });
							}
						}
					}
								} else if (kind == 2) {
					// Void Hex: a mark of dark fire under your feet a moment before it bursts (its own sound, no Skyrim spell)
					if ((s.hook -= a_delta) <= 0 && dist > 250.0f && dist < 1800.0f) {
						s.hook = s.enraged ? 3.0f : 4.5f;
						const auto at = OnGround(a_player->GetPosition());
						Gun::FxBlast("paralyze", at + RE::NiPoint3{ 0, 0, 8 }, 0.0f);
						g_hazards.push_back({ at, 0.9f, 120.0f, 0.07f, "dark", false });
						Forms::PlayAt(gen::Sound::vo_necro_attack, a.get());
						Audio::AtPoint(gen::Sound::boom_animunculi, at, 0.9f, 0.8f);
						Undead::CueFor(a.get(), Undead::kCueChannel, 0.8f);
					}
					if ((s.ability -= a_delta) <= 0 && dist < 3500.0f) {
						s.ability = 11.0f;
						if (minions < 8 && minionRow >= 0) {
							for (int k = 0; k < 2; ++k) {
								const float ang = Uniform(0.0f, 2.0f * RE::NI_PI);
								raise.push_back(a->GetPosition() + RE::NiPoint3{ std::cos(ang) * 140.0f, std::sin(ang) * 140.0f, 0.0f });
							}
							minions += 2;
							Forms::PlayAt(gen::Sound::champ_summon, a.get());
							Undead::CueFor(a.get(), Undead::kCueChannel, 1.5f);
						}
					}
				}
			}
			for (auto& at : raise) {
				if (SpawnOne(a_player, minionRow, &at) && !g_alive.empty()) {
					raised.push_back(g_alive.back().handle);
				}
			}
			for (auto& [boss, at] : calls) {
				if (SpawnOne(a_player, goblinRow, &at) && !g_alive.empty()) {
					const auto h = g_alive.back().handle;
					for (auto& o : g_alive) if (o.handle == boss) o.band.push_back(h);
				}
			}
		}

		RE::TESFaction* UndeadFaction() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESFaction>(gen::kUndeadFaction, gen::kPlugin); }

		// Mass Frenzy: out of the undead faction, set on the nearest of its kind, and the blows are real.
		void TickFrenzy(RE::PlayerCharacter* a_player, float a_delta)
		{
			for (auto& s : g_alive) {
				if (s.frenzy <= 0) {
					continue;
				}
				auto a = s.handle.get();
				if (!a || a->IsDead()) {
					s.frenzy = 0;
					continue;
				}
				if ((s.frenzy -= a_delta) <= 0) {  // back to the hunt
					if (auto* fac = UndeadFaction()) {
						a->AddToFaction(fac, 0);
					}
					a->StopCombat();
					a->StartCombat(a_player);
					continue;
				}
				if ((s.tear -= a_delta) > 0) {
					continue;
				}
				s.tear = 0.5f;
				RE::Actor* best = nullptr;
				float      bd = 900.0f;
				for (auto& o : g_alive) {
					auto b = o.handle.get();
					if (b && b.get() != a.get() && !b->IsDead()) {
						if (const float d = b->GetPosition().GetDistance(a->GetPosition()); d < bd) {
							bd = d, best = b.get();
						}
					}
				}
				if (!best) {
					continue;
				}
				auto tgt = a->GetActorRuntimeData().currentCombatTarget.get();
				if (!tgt || tgt.get() != best) {
					a->StopCombat();
					a->StartCombat(best);
				}
				if (bd < 220.0f) {  // in reach: it tears into the other one
					auto*       bav = best->AsActorValueOwner();
					const float hurt = bav->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.12f;
					bav->DamageActorValue(RE::ActorValue::kHealth, hurt);
					if (bav->GetActorValue(RE::ActorValue::kHealth) <= 0 && !best->IsDead()) {
						best->KillImpl(a_player, hurt, true, false);
						Points::Add(gen::kPoints_hit * 5);
					}
				}
			}
		}

		// the nearest of your summons (or a friend's) within a_r of a_of
		RE::Actor* NearestSummon(RE::Actor* a_of, RE::PlayerCharacter* a_player, float a_r)
		{
			RE::Actor* best = nullptr;
			const auto at = a_of->GetPosition();
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto z = h.get();
				if (!z || z.get() == a_player || z->IsDead() || !z->Is3DLoaded() || IsOurs(z.get())) continue;
				if (!(z->IsCommandedActor() || Classes::IsOwnSummon(z.get())) || !Director::PlayerSide(z.get(), a_player)) continue;
				if (const float d = z->GetPosition().GetDistance(at); d < a_r) a_r = d, best = z.get();
			}
			return best;
		}
		void KeepThemComing(RE::PlayerCharacter* a_player, float a_delta)
		{
			SettleCorpses(a_player, a_delta);
			TickFading(a_delta);
			// (Noah: crashes while gliding) the left-behind dead were being moved after you every frame you flew - a dozen
			// actors dropped into cells still loading under a gliding player. Now: never while you glide or fall, only once
			// you have stood on the ground a moment, and one at a time (a quarter second apart)
			static float grounded = 0, relocGap = 0;
			const bool   flying = Movement::Gliding() || a_player->IsInMidair();
			grounded = flying ? 0.0f : grounded + a_delta;
			relocGap = std::max(0.0f, relocGap - a_delta);
			std::vector<std::size_t> refill;  // (spawned after the loop: g_alive must not grow under it)
			struct Refill
			{
				std::vector<std::size_t>& v;
				RE::PlayerCharacter*      p;
				~Refill()
				{
					for (auto e : v) SpawnOne(p, static_cast<int>(e));
				}
			} refiller{ refill, a_player };
			for (auto& s : g_alive) {
				if (!gen::kEnemies[s.enemy].forceCombat || (s.combatRetry -= a_delta) > 0) {
					continue;
				}
				constexpr float kStep = 0.75f;
				s.combatRetry = kStep;
				auto a = s.handle.get();
				if (!a || a->IsDead() || a->IsDisabled()) {
					continue;
				}
				// (Noah: "some of them are carrying Nordic weapons - they shouldn't have any weapons at all") a weapon that
				// came back after the spawn (the body's outfit put on again as it loaded, a distributor's gift) is taken
				// away again, and its own unseen claws put back in its hands
				s.loaded = a->Is3DLoaded() ? s.loaded + kStep : 0.0f;
				// (the audit: the crash came the frame the outfit went back on, HDT-SMP still building it) only a body that
				// has stood whole for two seconds, out of its portal, is disarmed
				if (const std::string_view rid = gen::kEnemies[s.enemy].id; s.loaded >= 2.0f && !IsRising(a.get()) && (rid == "zombie" || rid == "runner" || rid == "wailer" || rid == "bloater" || rid == "volatile" || rid == "mage" || rid == "girl")) {
					auto* claws = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kZombieClaws, gen::kPlugin);
					auto* em = RE::ActorEquipManager::GetSingleton();
					bool  armed = false;
					RE::TESForm* first = nullptr;
					for (bool left : { false, true }) {
						if (auto* eq = a->GetEquippedObject(left); eq && eq != claws && eq != first && (eq->IsWeapon() || (eq->Is(RE::FormType::Armor) && eq->As<RE::TESObjectARMO>()->IsShield()))) {
							first = eq;  // (a two-handed weapon fills both hands: unequipped once)
							armed = true;
							if (auto* b = eq->As<RE::TESBoundObject>()) em->UnequipObject(a.get(), b);
						}
					}
					if (armed && claws) {
						em->EquipObject(a.get(), claws);
						// (Noah: a crash 10 ms after this - HDT-SMP still held the weapon's 3D) the weapon leaves its pack only once the
						// unequip has played out, a second and a half later
						std::thread([h = a->GetHandle(), claws] { std::this_thread::sleep_for(std::chrono::milliseconds(1500)); Tasks::Add([h, claws] {
							auto b = h.get();
							if (!b || b->IsDisabled() || b->IsDeleted() || b->IsDead() || !b->Is3DLoaded()) return;
							auto other = b->GetInventory([claws](RE::TESBoundObject& o) {
								if (&o == claws) return false;
								auto* ar = o.As<RE::TESObjectARMO>();
								return o.IsWeapon() || o.IsAmmo() || (ar && ar->IsShield());
							});
							for (auto& [obj, data] : other) {
								if (data.first <= 0 || b->GetEquippedObject(false) == obj || b->GetEquippedObject(true) == obj) continue;  // (still in a hand: the next pass)
								b->RemoveItem(obj, data.first, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
							}
						}); }).detach();
						static int told = 0;
						if (told++ < 10) logger::info("night: a {} was holding a weapon again - taken away", rid);
					}
				}
				if (s.frenzy > 0) {
					continue;  // turned on its own kind (TickFrenzy)
				}
				const bool  boss = gen::kEnemies[s.enemy].bossEvery > 0;
				const float dist = a->GetPosition().GetDistance(a_player->GetPosition());
				// Left behind (too far, or its cell unloaded): climb out of the ground again near the player, BO3-style.
				// (Noah: "6 enemies left but they were nowhere to be seen" - the log: the dead replaced again and again, each
				// one "unloaded" though it stood a few steps away, its body still loading - a churn that never let one arrive)
				// one near you that hasn't loaded yet is given time (15 s) before it is replaced; only the far are moved at once
				const bool farOff = dist > gen::kChaseRelocateDist;
				// (the audit: a boss behind a door or caught in a wall held the night forever) out of reach ten seconds, it comes
				// to you: one still whole in your world is set down at a spawn point near you, one whose cell let it go is brought
				// to you first (and set down properly on the next pass)
				if (boss && !IsRising(a.get())) {
					const bool sameWorld = a->Is3DLoaded() && a->GetWorldspace() == a_player->GetWorldspace() && a->GetParentCell() && a_player->GetParentCell() &&
						a->GetParentCell()->IsInteriorCell() == a_player->GetParentCell()->IsInteriorCell();
					if (farOff || !sameWorld) {
						if ((s.behind += kStep) >= 10.0f && relocGap <= 0) {
							relocGap = 1.0f, s.behind = 0, s.stall = 0, s.lastDist = -1;
							if (sameWorld) Resurface(a.get(), a_player);
							else a->MoveTo(a_player);
							logger::info("night: the boss {} out of reach ({:.0f} away) - brought to you", gen::kEnemies[s.enemy].id, dist);
						}
						continue;
					}
				}
				if (!boss && !IsRising(a.get()) && (farOff || !a->Is3DLoaded())) {  // (one still in its portal is never "behind")
					// (Noah: "on night two, halfway through, the enemies just stopped spawning" - the log: every new one stayed
					// bodiless, replaced every few seconds by another that never loaded either) one near you still without a body
					// after 4 s: why is logged, and whatever holds it back is undone (disabled, its AI off) and its body asked for
					if (!farOff && !a->Is3DLoaded() && s.behind < 4.0f && s.behind + kStep >= 4.0f) {
						auto* cell = a->GetParentCell();
						static int told = 0;
						if (told++ < 20) {
							logger::info("night: {} {:08X} has no body {:.0f} away - disabled {} ai {} cell {:08X} attached {} z {:.0f} (you {:.0f}) process {}",
								gen::kEnemies[s.enemy].id, a->GetFormID(), dist, a->IsDisabled(), a->IsAIEnabled(), cell ? cell->GetFormID() : 0,
								cell && cell->IsAttached(), a->GetPosition().z, a_player->GetPosition().z, a->GetActorRuntimeData().currentProcess ? static_cast<int>(a->GetActorRuntimeData().currentProcess->processLevel.get()) : -1);
						}
						if (a->IsDisabled()) a->Enable(false);
						if (!a->IsAIEnabled()) a->EnableAI(true);
						// (Noah: "my spells stop working and the dead on the compass can't be seen - everything freezes") when the
						// game stops building new bodies altogether (three without one in ten seconds), its cell buffers are
						// purged once - Skyrim's own "pcb" - to free the loader, at most once a minute and a half
						static std::deque<std::chrono::steady_clock::time_point> noBody;
						static auto lastPurge = std::chrono::steady_clock::now() - std::chrono::minutes(5);
						const auto nb = std::chrono::steady_clock::now();
						noBody.push_back(nb);
						while (!noBody.empty() && nb - noBody.front() > std::chrono::seconds(10)) noBody.pop_front();
						if (noBody.size() >= 3 && nb - lastPurge > std::chrono::seconds(90)) {
							lastPurge = nb, noBody.clear();
							Gun::Console("pcb");
							logger::warn("night: new bodies have stopped loading - cell buffers purged (pcb)");
						}
					}
					static std::deque<std::chrono::steady_clock::time_point> recent;
					const auto nowR = std::chrono::steady_clock::now();
					while (!recent.empty() && nowR - recent.front() > std::chrono::seconds(30)) recent.pop_front();
					const float wait = farOff ? gen::kChaseRelocateSeconds : 15.0f;
					if ((s.behind += kStep) >= wait && grounded >= 1.5f && relocGap <= 0 && recent.size() < 8) {
						recent.push_back(nowR);
						// (crashes right after a burst of these: an actor dragged across cells with MoveTo and then set
						// down elsewhere the same frame, its body still loading on a worker thread) one a second; one still
						// loaded in your worldspace is only set down near you, one whose cell unloaded is taken away and a
						// fresh one climbs out instead
						relocGap = 1.0f;
						s.behind = 0, s.stall = 0, s.lastDist = -1;
						if (a->Is3DLoaded() && a->GetWorldspace() == a_player->GetWorldspace() && a->GetParentCell() && a_player->GetParentCell() &&
							a->GetParentCell()->IsInteriorCell() == a_player->GetParentCell()->IsInteriorCell() && (!a->GetParentCell()->IsInteriorCell() || a->GetParentCell() == a_player->GetParentCell())) {
							Resurface(a.get(), a_player);
							logger::info("night: {} that fell behind set down near you ({:.0f} away)", gen::kEnemies[s.enemy].id, dist);
						} else {
							refill.push_back(s.enemy);
							Vanish(a.get());
							logger::info("night: {} that fell behind replaced ({:.0f} away, unloaded)", gen::kEnemies[s.enemy].id, dist);
						}
					}
					continue;
				}
				s.behind = 0;
				if (!a->Is3DLoaded()) {
					continue;
				}
				// stuck behind a building or on a rock: out of reach and not getting closer for 10 s - it climbs out of the
				// ground again near you (out of sight is not checked: it simply comes back into the fight)
				// (Noah: "getting stuck in porches and behind houses - I constantly have to be looking for zombies") two
				// ways to tell: it has barely moved for three seconds while out of reach (caught on a porch rail, a corner),
				// or it has not got closer for six - either way it climbs out of the ground again at a spawn point near you
				const bool caster = gen::kEnemies[s.enemy].kind == 2;
				{
					const auto here = a->GetPosition();
					if (std::hypot(here.x - s.lastPos.x, here.y - s.lastPos.y) < 70.0f) s.still += kStep;
					else s.lastPos = here, s.still = 0;
					if (IsRising(a.get())) s.lastPos = here, s.still = 0;
				}
				// (Noah: "the spawn frequency and locations" - the log: one of the dead set back down every second or two) slower to
				// call one stuck, and never where you are looking: only one out of your sight is taken back to a spawn point
				const bool pinned = !boss && !caster && dist > 260.0f && s.still >= 5.0f;
				if (!boss && dist > 400.0f && s.lastDist >= 0 && dist > s.lastDist - 30.0f) s.stuck += kStep;
				else s.stuck = 0;
				bool inView = false;
				{
					const auto pp = a_player->GetPosition(), zp = a->GetPosition();
					const float yaw = a_player->GetAngleZ();
					const float dx = zp.x - pp.x, dy = zp.y - pp.y, len = std::max(1.0f, std::hypot(dx, dy));
					const float facing = (dx * std::sin(yaw) + dy * std::cos(yaw)) / len;  // 1 straight ahead
					inView = facing > 0.45f && dist < 2500.0f && !RayBlocked(a_player->GetParentCell(), pp + RE::NiPoint3{ 0, 0, 110.0f }, zp + RE::NiPoint3{ 0, 0, 90.0f });
				}
				if ((pinned || s.stuck >= 10.0f) && !inView && grounded >= 1.5f && relocGap <= 0 && !IsRising(a.get())) {
					relocGap = 0.5f, s.stuck = 0, s.stall = 0, s.still = 0, s.lastDist = -1;
					Resurface(a.get(), a_player);
					s.lastPos = a->GetPosition();
					logger::info("night: {} {} ({:.0f} away) - risen again near you", gen::kEnemies[s.enemy].id, pinned ? "caught on something" : "not getting closer", dist);
					continue;
				}
				// Not closing in while still out of reach: it lost track of the player, so re-target from scratch.
				const bool closing = s.lastDist < 0 || dist < s.lastDist - 30.0f || dist < 350.0f || (gen::kEnemies[s.enemy].kind == 2 && dist < 2600.0f);  // casters hold their range
				s.lastDist = dist;
				s.stall = closing ? 0 : s.stall + kStep;
				auto target = a->GetActorRuntimeData().currentCombatTarget.get();
				if (a->IsInCombat() && target.get() == a_player && s.stall < gen::kChaseStallSeconds) {
					s.inCombat = true;
					// (Noah: "the enemies aren't attacking my summons") a summon of yours at its throat: it turns on the summon
					if (auto sm = NearestSummon(a.get(), a_player, 260.0f)) {
						a->StartCombat(sm);
					}
					continue;
				}
				// fighting one of your summons close by: it keeps at it
				if (a->IsInCombat() && target && !target->IsDead() && target.get() != a_player && Director::PlayerSide(target.get(), a_player) &&
					target->GetPosition().GetDistance(a->GetPosition()) < 900.0f) {
					s.inCombat = true;
					continue;
				}
				if (a->IsInCombat()) {
					a->StopCombat();  // fighting an NPC or searching: back to the player
				}
				s.stall = 0;
				a->StartCombat(a_player);
				a->EvaluatePackage(true, false);
			}
			// NPCs that picked a fight with our undead drop it.
			if (gen::kIgnoreNpcs) {
				for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
					auto npc = h.get();
					if (!npc || npc.get() == a_player || IsOurs(npc.get()) || !npc->IsInCombat() || Director::PlayerSide(npc.get(), a_player)) {  // (your summons and allies keep their fights)
						continue;
					}
					auto tgt = npc->GetActorRuntimeData().currentCombatTarget.get();
					if (tgt && IsOurs(tgt.get())) {
						npc->StopCombat();
					}
				}
			}
		}
	}

	namespace Director
	{
		// A vanilla spell (sheet: spells) flung from the player at a target: the visible half of a perk.
		void CastFx(int a_spell, RE::Actor* a_target)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* m = Forms::Spell(a_spell);
			if (!player || !m || !a_target) {
				return;
			}
			if (auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
				caster->CastSpellImmediate(m, false, a_target, 1.0f, false, 0.0f, player);
			}
		}

		void Web(const RE::NiPoint3& a_at, float a_radius, float a_seconds, float a_amount, bool a_frostFx)
		{
			int fx = 0;
			for (auto& s : g_alive) {
				if (auto a = s.handle.get(); a && !a->IsDead() && a->GetPosition().GetDistance(a_at) <= a_radius && s.webbed <= 0) {
					a->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -a_amount);
					s.webbed = a_seconds;
					s.webAmount = a_amount;
					if (a_frostFx && fx++ < 8) {
						CastFx(gen::kSpell_icespike, a.get());  // Frostbite: ice spikes pin them
					}
				}
			}
		}

		void Shock(const RE::NiPoint3& a_at, float a_radius, float a_bo3Damage)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			int   fx = 0;
			for (auto& s : g_alive) {
				auto a = s.handle.get();
				if (!a || a->IsDead() || a->GetPosition().GetDistance(a_at) > a_radius) {
					continue;
				}
				if (fx++ < 8) {
					CastFx(gen::kSpell_lightningbolt, a.get());  // Stormcrown: a bolt to each of them
				}
				auto* av = a->AsActorValueOwner();
				const float dmg = a_bo3Damage / R.hpScale;
				av->DamageActorValue(RE::ActorValue::kHealth, dmg);
				if (av->GetActorValue(RE::ActorValue::kHealth) <= 0) {
					a->KillImpl(player, dmg, true, false);
				}
			}
		}

		void Reset()
		{
			Story::Reset();
			Audio::BossMusic(false);
			g_phase = Phase::Day;
			EndRisers();
			g_alive.clear();
			g_corpses.clear();
			g_ours.clear();
			g_toSpawn = 0;
			g_boss = {};
			g_champ = {};
			g_elites.clear();
			g_volatiles.clear(), g_noise = 0, g_volatileWait = 30.0f;
			g_summoned.clear();
			g_slowLeft = 0;
			g_pause = 0;
			// (the audit: a hazard pending at the load erupted at the old spot on the first night's tick)
			g_hazards.clear(), g_purge.clear(), g_fading.clear(), g_settle.clear(), g_fx.clear(), g_roused.clear();
			g_spawnMarks.clear(), g_spotBaseSet = false;
			EndEvent();
			Undead::Reset();
			Curse::Reset();
			Echo::Reset();
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().bossName.clear();
		}

		void LogState()
		{
			logger::info("dev: director phase={} current={} toSpawn={} alive={} corpses={} special={} night={}", static_cast<int>(g_phase), g_current, g_toSpawn,
				g_alive.size(), g_corpses.size(), g_special, IsNight());
			for (auto& s : g_alive) {
				if (auto a = s.handle.get()) {
					const auto p = a->GetPosition();
					logger::info("dev:   {} {:08X} hp={:.0f} combat={} pos=({:.0f},{:.0f},{:.0f})", gen::kEnemies[s.enemy].id, a->GetFormID(),
						a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth), a->IsInCombat(), p.x, p.y, p.z);
				}
			}
		}

		float RoundHealth() { return HealthFor(std::max(1, g_current)); }
		bool  IsElite(RE::Actor* a_actor) { return a_actor && g_elites.contains(a_actor->GetFormID()); }
		bool BossPos(RE::NiPoint3& a_at)
		{
			auto b = g_boss.handle.get();
			if (!b || b->IsDead()) {
				return false;
			}
			a_at = b->GetPosition() + RE::NiPoint3{ 0, 0, 260.0f };
			return true;
		}
		bool ChampionActive(int a_town) { return g_champ.town == a_town && (g_champ.handle.get() || g_champ.slainTonight); }
		bool ChampionPos(RE::NiPoint3& a_at)
		{
			auto c = g_champ.handle.get();
			if (!c || c->IsDead()) {
				return false;
			}
			a_at = c->GetPosition() + RE::NiPoint3{ 0, 0, 170.0f };
			return true;
		}
		bool RaiseChampion(int a_town, std::string_view a_row, std::string_view a_name, float a_healthMult, const RE::NiPoint3& a_at)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || g_phase != Phase::Fighting || Coop::Client() || g_champ.handle.get()) {
				return false;
			}
			int row = -1;
			for (int i = 0; i < static_cast<int>(gen::kEnemies.size()); ++i) {
				if (gen::kEnemies[i].id == a_row) {
					row = i;
				}
			}
			SummonScope summon;  // (a champion steps out of a portal)
			if (row < 0 || !SpawnOne(p, row, &a_at)) {
				return false;
			}
			auto c = g_alive.back().handle.get();
			if (!c) {
				return false;
			}
			auto*       av = c->AsActorValueOwner();
			const float want = std::max(HealthFor(g_current) * a_healthMult, 6000.0f / R.hpScale) * Settings::UndeadHealthMult();
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, want - av->GetPermanentActorValue(RE::ActorValue::kHealth));
			c->SetScale(1.35f);
			g_champ = { c->GetHandle(), a_town, std::string(a_name), want, false };
			Forms::Play(gen::Sound::champ_arrive);
			Forms::PlayAt(gen::Sound::champ_enrage, c.get());
			Hud::Banner(std::string(a_name) + " walks the night!", 5.0f);
			logger::info("champion: {} risen in {} ({:.0f} health)", a_name, a_town >= 0 ? gen::kTowns[a_town] : std::string_view("the wilds"), want);
			return true;
		}
		void EndNight() { SkipToDawn("THE CITY IS FREE - DAWN BREAKS"); }
		void SkipToDawn(const char* a_banner)
		{
			auto* cal = RE::Calendar::GetSingleton();
			if (!cal || !IsNight() || Coop::Client()) {
				return;
			}
			// the clock jumps to first light; the night's round counts as survived (Dawn, from the Director's tick)
			const float h = cal->GetHour();
			const float hours = std::fmod(DawnHour() - h + 24.0f, 24.0f) + 0.05f;
			if (cal->gameHour && cal->gameDaysPassed) {
				float nh = cal->gameHour->value + hours;
				if (nh >= 24.0f) {
					nh -= 24.0f;
					if (cal->gameDay) {
						cal->gameDay->value += 1.0f;
					}
				}
				cal->gameHour->value = nh;
				cal->gameDaysPassed->value += hours / 24.0f;
				Hud::Banner(a_banner, 5.0f);
				logger::info("night: ended early ({:.1f} hours skipped): {}", hours, a_banner);
			}
		}
		void  Hazard(const RE::NiPoint3& a_at, float a_delay, float a_radius, float a_share, std::string_view a_look, bool a_stagger)
		{
			g_hazards.push_back({ a_at, a_delay, a_radius, a_share, a_look, a_stagger });
		}
		void HurtPlayer(float a_share, bool a_stagger)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || p->IsDead() || RE::PlayerCharacter::IsGodMode()) {
				return;
			}
			auto* pav = p->AsActorValueOwner();
			Perks::DamagePlayer(pav->GetPermanentActorValue(RE::ActorValue::kHealth) * a_share * Settings::UndeadDamageMult());
			if (a_stagger) {
				p->SetGraphVariableFloat("staggerMagnitude", 0.35f);
				p->NotifyAnimationGraph("staggerStart");
			}
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().hurtTime = 0.9f;
		}
		void DevSpawn(std::string_view a_row, int a_count)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			for (std::size_t i = 0; p && i < gen::kEnemies.size(); ++i) {
				if (gen::kEnemies[i].id == a_row) {
					for (int k = 0; k < a_count; ++k) {
						const float a = p->GetAngleZ() + (k - a_count / 2) * 0.35f;
						const RE::NiPoint3 at = p->GetPosition() + RE::NiPoint3{ std::sin(a) * 450.0f, std::cos(a) * 450.0f, 30.0f };
						SpawnOne(p, static_cast<int>(i), &at);
					}
					logger::info("dev: spawned {} x{}", a_row, a_count);
					return;
				}
			}
			logger::warn("dev: no enemy row '{}'", a_row);
		}
		void DevSpawnAt(std::string_view a_row, const RE::NiPoint3& a_at, float a_yawDeg)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			for (std::size_t i = 0; p && i < gen::kEnemies.size(); ++i) {
				if (gen::kEnemies[i].id == a_row) {
					SpawnOne(p, static_cast<int>(i), &a_at);
					for (auto& [id, row] : g_ours) {  // the newest one: face it the way asked
						auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
						if (a && row == i && a->GetPosition().GetDistance(a_at) < 120.0f) {
							a->SetAngle({ 0.0f, 0.0f, a_yawDeg * 0.0174533f });
						}
					}
					logger::info("dev: spawned {} at {:.0f},{:.0f},{:.0f}", a_row, a_at.x, a_at.y, a_at.z);
					return;
				}
			}
			logger::warn("dev: no enemy row '{}'", a_row);
		}
		void DevShowUndead(bool a_show)
		{
			int n = 0;
			for (auto& [id, row] : g_ours) {
				auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
				if (auto* root = a ? a->Get3D() : nullptr) {
					root->GetFlags().set(RE::NiAVObject::Flag::kHidden);
					if (a_show) {
						root->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
					}
					++n;
				}
			}
			logger::info("dev: undead {} ({})", a_show ? "shown" : "hidden", n);
		}
		void RaiseAt(const RE::NiPoint3& a_at)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (p && g_phase == Phase::Fighting && !Coop::Client() && static_cast<int>(g_alive.size()) < R.maxAlive + 6) {
				// (Noah: "the Grave Witch summoned itself instead of more of the normal enemy") the sheet's first row is not
				// always the Risen Dead: the row is found by its name
				int row = 0;
				for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
					if (gen::kEnemies[i].id == "zombie") row = static_cast<int>(i);
				}
				SpawnOne(p, row, &a_at);
			}
		}
		// a trial's wave (src/Curse.cpp): one of tonight's dead at a point (or a Thane of the Mound / Bone Jotunn when elite),
		// beyond the night's usual crowd
		RE::ActorHandle RaiseTrial(const RE::NiPoint3& a_at, bool a_elite)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || g_phase != Phase::Fighting || Coop::Client() || static_cast<int>(g_alive.size()) >= R.maxAlive + 24) {
				return {};
			}
			std::optional<std::size_t> row;
			if (a_elite) {
				static constexpr std::string_view kElite[] = { "reaper", "brute", "bonebrute" };
				const std::string_view pick = kElite[std::uniform_int_distribution<int>(0, 2)(g_rng)];
				for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
					if (gen::kEnemies[i].id == pick) {
						row = i;
					}
				}
			} else {
				row = PickEnemy();
			}
			std::optional<SummonScope> summon;
			if (a_elite) summon.emplace();  // (a trial's elite steps out of a portal)
			if (!row || !SpawnOne(p, static_cast<int>(*row), &a_at)) {
				return {};
			}
			auto a = g_alive.back().handle.get();
			return a ? a->GetHandle() : RE::ActorHandle{};
		}
		bool  IsBoss(RE::Actor* a_actor) { auto b = g_boss.handle.get(); return a_actor && b && b.get() == a_actor; }
		// ---- (Noah: "Make it a shout and can be used once per wave. Make it last 20 seconds and make the name of it Dance Of
		// Death. It should only work on enemies of The Curse Of Alduin") every one of the night's dead within 20 m (1400
		// units) dances for 20 s - a dance of the four at random each - unable to move or strike, taking half again as much
		// from every blow (Gun, Events); the song plays (Noah's own cut, 0:37-0:57, installed beside the mod). Once a wave.
		// Learned when Solitude's altar wakes - the Bards' city (TickDanceShout)
		int g_danceWave = -1;
		bool DanceOfDeath(RE::FormID a_spell)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			bool ours = false;
			for (auto id : gen::kDanceSpells) {
				if (auto* f = dh->LookupForm(id, gen::kPlugin); f && f->GetFormID() == a_spell) ours = true;
			}
			if (!ours) return false;
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return true;
			if (g_phase != Phase::Fighting) {
				Hud::Banner("The dead only dance at night", 3.0f);
				return true;
			}
			const int key = g_current * 1000 + g_wave;
			if (g_danceWave == key) {
				Hud::Banner("The dead won't dance again until the next wave", 3.0f);
				return true;
			}
			g_danceWave = key;
			int n = 0;
			for (auto& s : g_alive) {
				auto a = s.handle.get();
				if (!a || a->IsDead() || a->IsDisabled() || a->GetPosition().GetDistance(p->GetPosition()) > 1400.0f) continue;
				n += Undead::Dance(a.get(), 20.0f) ? 1 : 0;
			}
			if (auto* snd = dh->LookupForm<RE::BGSSoundDescriptorForm>(gen::kDanceSong, gen::kPlugin)) {
				static RE::BSSoundHandle h;
				h.Stop();
				if (auto* am = RE::BSAudioManager::GetSingleton(); am && am->GetSoundHandle(h, static_cast<RE::BSISoundDescriptor*>(snd))) h.Play();
			}
			Hud::Banner(n > 0 ? fmt::format("DANCE OF DEATH - {} of the dead dance", n) : std::string("DANCE OF DEATH - no one near enough to dance"), 3.5f);
			logger::info("dance of death: {} dance (night {}, wave {})", n, g_current, g_wave);
			return true;
		}
		void TickDanceShout(float a_delta)
		{
			static float t = 0;
			if ((t += a_delta) < 5.0f) return;
			t = 0;
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* sh = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESShout>(gen::kDanceShout, gen::kPlugin);
			if (!p || !sh || !Curse::Freed(2) || p->HasShout(sh)) return;  // (2: Solitude)
			for (auto w : gen::kDanceWords) {
				if (auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(w, gen::kPlugin)) {
					Gun::Console(fmt::format("player.teachword {:08X}", f->GetFormID()));
					Gun::Console(fmt::format("player.unlockword {:08X}", f->GetFormID()));
				}
			}
			p->AddShout(sh);
			Forms::Play(gen::Sound::shout_learned);
			Hud::Lore("DANCE OF DEATH", "The Bards of Solitude remember an old song the dead cannot resist. Shout it, and the Curse's dead within twenty paces dance for twenty seconds - unable to move or strike, and taking half again as much harm. Once a wave.", 12.0f);
			logger::info("dance of death: learned");
		}
		void  QueueZombieHit(RE::Actor* a_zombie, float a_seconds) { if (a_zombie && g_queuedHits.size() < 64) g_queuedHits.emplace_back(a_zombie->GetHandle(), a_seconds); }
		void  Noise(float a_amount) { if (g_phase == Phase::Fighting) g_noise = std::min(kNoiseFull * 1.5f, g_noise + a_amount); }
		// (Noah: the harpies do far too much damage) the mod creatures whose own attacks and spells were made for a
		// high-level hero, brought in line with the rest of the night
		float HarmMult(RE::Actor* a_actor)
		{
			const auto it = a_actor ? g_ours.find(a_actor->GetFormID()) : g_ours.end();
			if (it == g_ours.end()) return 1.0f;
			const std::string_view id = gen::kEnemies[it->second].id;
			if (id == "hf_harpy") return 0.1f;            // (Noah: still nearly one shot on Expert - a third of before)
			if (id == "mb_harpy_matriarch") return 0.15f;
			return 1.0f;
		}
		bool HarpyNear(const RE::NiPoint3& a_at, float a_r)
		{
			for (auto& z : g_alive) {
				auto a = z.handle.get();
				if (a && !a->IsDead() && HarmMult(a.get()) < 0.999f && a->GetPosition().GetDistance(a_at) < a_r) return true;
			}
			return false;
		}
		std::vector<RE::ActorHandle> AliveHandles()
		{
			std::vector<RE::ActorHandle> v;
			for (auto& z : g_alive) v.push_back(z.handle);
			return v;
		}
		// (Noah: "BO3-style death instead of reloading a save") you fell: the wave's dead are gone and the wave begins again
		// after a breath (a boss stays where it is)
		void RestartWave()
		{
			int n = 0;
			for (auto& s : g_alive) {
				if (auto a = s.handle.get(); a && !Director::IsBoss(a.get())) Vanish(a.get()), ++n;
			}
			std::erase_if(g_alive, [](const Spawned& s) { auto a = s.handle.get(); return !a || !Director::IsBoss(a.get()); });
			for (auto& h : g_corpses) {
				if (auto a = h.get()) Vanish(a.get());
			}
			g_corpses.clear();
			g_settle.clear();
			g_hazards.clear();
			EndRisers();
			if (g_phase == Phase::Fighting && g_wave > 0 && !g_nightCleared) {
				g_waveSize = WaveSize(g_wave);
				g_waveSpawned = 0;
				g_waveRest = kWaveRest + 2.0f;
			}
			logger::info("fallen: wave {} begins again ({} of the dead gone)", g_wave, n);
		}
		void SlowTime(float a_realSeconds, float a_mult) { SlowBeat(a_realSeconds, a_mult); }
		void NormalTime() { g_slowUntil = std::chrono::steady_clock::now(); TickSlow(); }
		float TakenMult(RE::Actor* a_actor)
		{
			for (const auto& s : g_alive) {
				if (s.handle.get().get() != a_actor) continue;
				return s.stun > 1.05f || (s.stun > 0 && gen::kEnemies[s.enemy].id == "bloater") ? 2.0f : s.berserk ? 1.25f : 1.0f;
			}
			return 1.0f;
		}
		void OnDying(RE::Actor* a_actor)
		{
			// (Noah: "the armored warrior knights all had loot in their inventory - no items on the Curse's corpses, only
			// ours, not Skyrim's") one of ours that falls can't be searched, and whatever it carried that isn't on its body
			// is gone; what it wears stays for its look, out of reach (taken off a body mid-fall is what crashed HDT-SMP)
			if (a_actor && IsOurs(a_actor)) {
				a_actor->SetActivationBlocked(true);
				std::thread([h = a_actor->GetHandle()] { std::this_thread::sleep_for(std::chrono::milliseconds(800)); Tasks::Add([h] {
					auto b = h.get();
					if (!b || b->IsDeleted()) return;
					b->SetActivationBlocked(true);
					int n = 0;
					for (auto& [obj, data] : b->GetInventory()) {
						if (data.first <= 0 || !obj || (data.second && data.second->IsWorn())) continue;
						b->RemoveItem(obj, data.first, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						++n;
					}
					static int told = 0;
					if (n && told++ < 10) logger::info("night: {} things taken off a fallen {:08X} (none of the Curse's dead carry loot)", n, b->GetFormID());
				}); }).detach();
			}
			// (Thorgar the Skulltaker) a player or a summon falls near it: its trophy - it heals, grows and quickens
			if (auto* pl = RE::PlayerCharacter::GetSingleton(); a_actor && pl && PlayerSide(a_actor, pl)) {
				for (auto& s : g_alive) {
					if (gen::kEnemies[s.enemy].id != "knight" || s.trophies >= 5) continue;
					auto t = s.handle.get();
					if (!t || t->IsDead() || t->GetPosition().GetDistance(a_actor->GetPosition()) > 900.0f) continue;
					++s.trophies;
					auto* tav = t->AsActorValueOwner();
					tav->RestoreActorValue(RE::ActorValue::kHealth, tav->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.15f);
					tav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 6.0f);
					tav->SetActorValue(RE::ActorValue::kAttackDamageMult, tav->GetActorValue(RE::ActorValue::kAttackDamageMult) * 1.1f);
					t->SetScale(t->GetScale() * 1.06f);
					Forms::PlayAt(gen::Sound::champ_enrage, t.get());
					Hud::Banner("THE SKULLTAKER TAKES A TROPHY", 1.2f);
				}
			}
			if (a_actor && g_champ.handle.get().get() == a_actor) {
				g_champDying = true;
				TickChampion();  // (now: before the body can crumble away)
			}
		}
		bool IsRoundUndead(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			if (g_ours.contains(a_actor->GetFormID())) {
				return true;
			}
			if (Coop::Client()) {  // the host's undead, shown here by Skyrim Together
				auto* base = a_actor->GetActorBase();
				auto* file = base ? base->GetFile(0) : nullptr;
				return file && file->GetFilename() == gen::kPlugin;
			}
			return false;
		}
		int  EnemyRow(RE::Actor* a_actor)
		{
			if (auto it = a_actor ? g_ours.find(a_actor->GetFormID()) : g_ours.end(); it != g_ours.end()) {
				return static_cast<int>(it->second);
			}
			return -1;
		}
		bool NightNow() { return IsNight(); }
		constexpr float kNightMinutes = 10.0f;  // a night's real minutes, dusk to the hold before dawn
		// ---- (Noah: "the wait screen in 12-hour time, the circle marking when the Curse wakes, when the dead start and
		// stop rising, and a Skip the Night button - only at night, after the countdown - that goes through the whole
		// night and doesn't count") the Curse's own rest screen, in place of Skyrim's wait menu once your nights have begun
		struct Rest
		{
			bool  open = false, waiting = false, armed = false;
			int   hours = 1;
			float target = 0, done = 0, total = 0;
		};
		Rest       g_rest;
		std::mutex g_restLock;
		RestView   g_restView;  // (built on the game's thread, read by the HUD's)
		RestView   BuildRestView();
		constexpr auto kRestControls = static_cast<RE::UserEvents::USER_EVENT_FLAG>(
			static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kMovement) | static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kLooking) |
			static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kFighting) | static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kJumping) |
			static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kSneaking) | static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kActivate) |
			static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kPOVSwitch) | static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kMenu) |
			static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kWheelZoom));
		std::chrono::steady_clock::time_point g_restClosed{};
		void RestControls(bool a_off)
		{
			static bool off = false;
			if (a_off == off) return;
			off = a_off;
			if (auto* cm = RE::ControlMap::GetSingleton()) cm->ToggleControls(kRestControls, !a_off, false);
		}
		void CloseRest()
		{
			{
				std::scoped_lock l(g_restLock);
				g_rest.open = false, g_rest.waiting = false, g_rest.armed = false;
				g_restView.open = false;
			}
			g_restClosed = std::chrono::steady_clock::now();
			RestControls(false);
		}
		void AddHours(float a_hours)
		{
			auto* cal = RE::Calendar::GetSingleton();
			if (!cal || !cal->gameHour || !cal->gameDaysPassed || a_hours <= 0) return;
			float nh = cal->gameHour->value + a_hours;
			while (nh >= 24.0f) {
				nh -= 24.0f;
				if (cal->gameDay) cal->gameDay->value += 1.0f;
			}
			cal->gameHour->value = nh;
			cal->gameDaysPassed->value += a_hours / 24.0f;
		}
		float NightScale() { return std::round(std::fmod(DawnHour() - DuskHour() + 24.0f, 24.0f) * 60.0f / kNightMinutes); }
		// when the first of the night's dead come up: the countdown after dusk, in game hours
		float RiseHour()
		{
			const float wait = g_phase == Phase::Break ? g_breakTimer : static_cast<float>(R.firstBreakSeconds);
			const float from = g_phase == Phase::Break && RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetHour() : DuskHour();
			return std::fmod(from + wait * NightScale() / 3600.0f, 24.0f);
		}
		void SkipNightNow()
		{
			if (Coop::Client() || g_phase != Phase::Fighting || !IsNight()) return;
			logger::info("night: night {} skipped from the rest screen - it will not count", g_current);
			g_skipped = true;
			SkipToDawn("THE NIGHT PASSES YOU BY - IT DOES NOT COUNT");
			if (!IsNight()) Dawn();
		}
		void TickRest()
		{
			Rest r;
			{
				std::scoped_lock l(g_restLock);
				r = g_rest;
			}
			if (!r.open) {
				std::scoped_lock l(g_restLock);
				g_restView.open = false;
				return;
			}
			{
				const auto v = BuildRestView();
				std::scoped_lock l(g_restLock);
				g_restView = v;
			}
			auto* ui = RE::UI::GetSingleton();
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (!pl || pl->IsDead() || Party::SoloFallen() || (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME))) {
				CloseRest();
				return;
			}
			RestControls(true);
			if (!r.waiting) return;
			// the hours run by in about two seconds (the hand sweeps the dial), stopping at dusk
			const float dt = std::clamp(RE::GetSecondsSinceLastFrame(), 0.0f, 0.1f);
			const float step = std::min(r.total - r.done, std::max(2.0f, r.total / 1.8f) * dt);
			AddHours(step);
			r.done += step;
			if (IsNight() || r.done >= r.total - 1e-4f) {
				auto* av = pl->AsActorValueOwner();
				for (auto v : { RE::ActorValue::kHealth, RE::ActorValue::kMagicka, RE::ActorValue::kStamina }) {
					av->RestoreActorValue(v, av->GetPermanentActorValue(v));  // (rested)
				}
				CloseRest();
				Hud::Banner(IsNight() ? "NIGHT FALLS - THE CURSE WAKES" : "RESTED", 3.0f);
				return;
			}
			std::scoped_lock l(g_restLock);
			g_rest.done = r.done;
		}
		void OpenRest()
		{
			if (Coop::Client()) {
				Hud::Banner("Only the host can rest. Their clock moves the whole party.", 3.5f);
				return;
			}
			if (g_phase == Phase::Break) {
				Hud::Banner("The countdown has begun - the night can be skipped once the dead rise", 3.5f);
				return;
			}
			std::scoped_lock l(g_restLock);
			g_rest = {};
			g_rest.open = true, g_rest.hours = 1;
			logger::info("rest: the rest screen opened ({})", g_phase == Phase::Fighting ? "night" : "day");
		}
		bool RestOpen()
		{
			std::scoped_lock l(g_restLock);
			return g_rest.open;
		}
		bool RestEats(RE::InputEvent* a_event)
		{
			if (!a_event || !a_event->AsButtonEvent()) return false;
			return RestOpen() || std::chrono::steady_clock::now() - g_restClosed < std::chrono::milliseconds(350);
		}
		RestView RestNow()
		{
			std::scoped_lock l(g_restLock);
			return g_restView;
		}
		RestView BuildRestView()
		{
			RestView v;
			Rest r;
			{
				std::scoped_lock l(g_restLock);
				r = g_rest;
			}
			v.open = r.open;
			if (!r.open) return v;
			auto* cal = RE::Calendar::GetSingleton();
			v.now = cal ? cal->GetHour() : 12.0f;
			v.dusk = DuskHour(), v.dawn = DawnHour(), v.rise = RiseHour();
			v.night = g_phase == Phase::Fighting && IsNight();
			v.canSkip = v.night && !Coop::Client();
			v.armed = r.armed, v.waiting = r.waiting, v.hours = r.hours;
			v.nightNo = g_phase == Phase::Fighting ? g_current : Save::Get(gen::kAV_round) + 1;
			const float toDusk = std::fmod(v.dusk - v.now + 24.0f, 24.0f);
			v.target = r.waiting ? std::fmod(v.now + (r.total - r.done), 24.0f) : std::fmod(v.now + std::min<float>(static_cast<float>(r.hours), toDusk), 24.0f);
			v.stopsAtDusk = !v.night && static_cast<float>(r.hours) > toDusk;
			return v;
		}
		bool RestButton(const RE::ButtonEvent* a_b)
		{
			if (!a_b || !RestOpen()) return false;
			if (!a_b->IsDown()) return true;
			const auto id = a_b->GetIDCode();
			const auto dev = a_b->GetDevice();
			const bool pad = dev == RE::INPUT_DEVICE::kGamepad, key = dev == RE::INPUT_DEVICE::kKeyboard, mouse = dev == RE::INPUT_DEVICE::kMouse;
			const bool less = (pad && id == 0x0004) || (key && (id == 0xCB || id == 0x1E)) || (mouse && id == 9);
			const bool more = (pad && id == 0x0008) || (key && (id == 0xCD || id == 0x20)) || (mouse && id == 8);
			const bool accept = (pad && id == 0x1000) || (key && (id == 0x1C || id == 0x9C || id == 0x12));
			const bool skip = (pad && id == 0x4000) || (key && id == 0x13);
			const bool back = (pad && id == 0x2000) || (key && (id == 0x01 || id == 0x0E || id == 0x0F));
			bool doSkip = false, close = false;
			{
				std::scoped_lock l(g_restLock);
				const bool night = g_phase == Phase::Fighting && IsNight();
				if (back) {
					close = true;
				} else if (g_rest.waiting) {
					// (the hours running by: only Back stops them)
				} else if (night) {
					if (skip || accept) {
						if (g_rest.armed) doSkip = true;
						else g_rest.armed = true, Forms::Play(gen::Sound::rite_fail);
					}
				} else if (less || more) {
					g_rest.hours = std::clamp(g_rest.hours + (more ? 1 : -1), 1, 24);
				} else if (accept) {
					auto* cal = RE::Calendar::GetSingleton();
					const float toDusk = cal ? std::fmod(DuskHour() - cal->GetHour() + 24.0f, 24.0f) : 24.0f;
					g_rest.total = std::min<float>(static_cast<float>(g_rest.hours), toDusk), g_rest.done = 0;
					g_rest.waiting = g_rest.total > 0.01f;
				} else if (skip) {
					Hud::Banner("Skip the Night opens once the dead have risen", 2.5f);
				}
			}
			if (close) CloseRest();
			if (doSkip) {
				CloseRest();
				SKSE::GetTaskInterface()->AddTask([] { SkipNightNow(); });
			}
			return true;
		}
		bool MayRest(std::string& a_why)
		{
			if (g_phase != Phase::Day || IsNight()) {
				a_why = "The dead walk. There is no rest until dawn.";
				return false;
			}
			if (Coop::Client()) {
				a_why = "Only the host can rest. Their clock moves the whole party.";
				return false;
			}
			return true;
		}
		int   EventNow() { return g_phase == Phase::Fighting ? g_event : -1; }
		float EventPointsMult()
		{
			if (g_phase != Phase::Fighting) return 1.0f;
			if (g_event >= 0) return static_cast<float>(gen::kEvents[g_event].pointsMult);
			return g_waveTheme >= 0 ? gen::kEvents[g_waveTheme].wavePoints : 1.0f;
		}
		int  ThemeNow() { return g_phase == Phase::Fighting ? ThemeNowImpl() : -1; }
		void DevTheme(int a_theme, bool a_night)
		{
			if (a_theme < 0 || a_theme >= static_cast<int>(gen::kEvents.size())) return;
			if (a_night) {
				EndEvent();
				g_event = a_theme;
				StartEvent(a_theme);
				Hud::Banner(std::string(gen::kEvents[a_theme].banner), 5.0f);
			} else {
				g_waveTheme = a_theme, g_finaleSpawned = false;
				g_waveSize = WaveSize(g_wave), g_waveSpawned = 0;
				WaveBegins();
			}
			logger::info("dev: theme {} ({})", gen::kEvents[a_theme].id, a_night ? "night" : "wave");
		}
		void DevMiniBoss()
		{
			if (auto* p = RE::PlayerCharacter::GetSingleton(); p && g_phase == Phase::Fighting) RaiseMiniBoss(p);
		}
		float EventDropMult() { return g_phase == Phase::Fighting && g_event >= 0 ? gen::kEvents[g_event].dropMult : 1.0f; }
		void  Pause(float a_seconds) { g_pause = std::max(g_pause, a_seconds); }
		float g_nukeLeft = 0;
		float Paused() { return g_pause; }
		bool  Fighting() { return g_phase == Phase::Fighting; }
		void  NetFill(Net::State& a_state) { NetFillImpl(a_state); }
		// (a fallen boss counts until TickBoss has dealt with him, and once he has, nothing calls another this night)
		bool  BossAlive() { return g_boss.handle.get() || g_boss.spawnIn > 0 || g_boss.slainTonight; }
		void  CallHerald()
		{
			if (g_phase != Phase::Fighting || BossAlive() || Coop::Client()) {
				return;
			}
			const int fog = gen::kEvent_bloodfog;
			if (g_event != fog) {
				EndEvent();
				g_event = fog;
				StartEvent(fog);
			}
			if (!Story::GrimmvarOpen()) {
				Hud::Banner("THE FOG ROLLS IN - but Alduin's Herald does not answer yet (after Alduin's Bane)", 6.0f);
				return;
			}
			g_boss.spawnIn = 20.0f, g_wantHerald = true;
			Hud::Banner("THE FOG ROLLS IN - GRIMMVAR, ALDUIN'S HERALD, IS COMING", 6.0f);
		}
		// The curse lifted: a wave of light runs out from the shrine and each of the night's dead it reaches is struck
		// down (a flash of Turn Undead where it stands, the body falls), then crumbles to grave-dust a few seconds on -
		// nearest first, the far ones last. The corpses already lying about crumble with it.
		struct Lifted { RE::ActorHandle h; float strike, fade; bool struck; };
		std::vector<Lifted> g_lifted;

		void LiftWave(const RE::NiPoint3& a_from)
		{
			int  n = 0;
			// (Noah: every wave must still be slain to end the night) the light takes only the fallen; the living fight on
			(void)a_from;
			for (auto& h : g_corpses) {
				if (auto a = h.get(); a && !a->IsDisabled()) {
					g_lifted.push_back({ h, 0.0f, 0.5f + 0.1f * (n++ % 12), true });
				}
			}
			g_corpses.clear();
			g_settle.clear();
			logger::info("curse: the light runs out from the shrine ({} of the dead)", n);
		}

		void TickLifted(float a_delta)
		{
			for (auto& l : g_lifted) {
				auto a = l.h.get();
				if (!a || a->IsDisabled()) {
					l.fade = -1;
					continue;
				}
				if (!l.struck) {
					if ((l.strike -= a_delta) > 0) {
						continue;
					}
					l.struck = true;
					auto at = a->GetPosition();
					at.z += a->GetHeight() * 0.5f;
					Gun::FxBlast("turnundead", at);
					a->StopCombat();
					if (!a->IsDead()) {
						a->KillImmediate();
					}
					continue;
				}
				if ((l.fade -= a_delta) <= 0) {
					if (auto* fx = Forms::Ours(gen::kRiseFx)) {  // to grave-dust
						if (auto puff = a->PlaceObjectAtMe(fx, false)) {
							puff->SetPosition(a->GetPosition());
							g_fx.push_back({ puff->CreateRefHandle(), 3.0f });
						}
					}
					Vanish(a.get());
				}
			}
			std::erase_if(g_lifted, [](const Lifted& l) { return l.fade <= 0 && l.struck; });
		}

		void UndeadNear(const RE::NiPoint3& a_from, float a_range, std::vector<std::array<float, 4>>& a_out)
		{
			auto boss = g_boss.handle.get();
			auto champ = g_champ.handle.get();
			for (auto& sp : g_alive) {
				auto a = sp.handle.get();
				if (!a || a->IsDead() || a->IsDisabled()) {
					continue;
				}
				const auto  p = a->GetPosition();
				const float d = p.GetDistance(a_from);
				if (d < a_range) {
					a_out.push_back({ p.x - a_from.x, p.y - a_from.y, d, (a == boss || a == champ) ? 1.0f : 0.0f });
				}
			}
		}

		void CrumbleNear(const RE::NiPoint3& a_at, float a_radius)
		{
			int  n = 0;
			auto boss = g_boss.handle.get();
			for (auto& sp : g_alive) {
				auto a = sp.handle.get();
				if (a && !a->IsDead() && a != boss && a->GetPosition().GetDistance(a_at) < a_radius) {
					if (auto* fx = Forms::Ours(gen::kRiseFx)) {
						if (auto puff = a->PlaceObjectAtMe(fx, false)) {
							g_fx.push_back({ puff->CreateRefHandle(), 3.0f });
						}
					}
					Vanish(a.get());
					++n;
				}
			}
			std::erase_if(g_alive, [](const Spawned& sp) { auto a = sp.handle.get(); return !a || a->IsDisabled(); });
			if (n) {
				logger::info("curse: {} undead crumbled in a freed city", n);
			}
		}

		std::string_view HeadNodeFor(RE::Actor* a_actor)
		{
			if (a_actor) {
				if (auto it = g_ours.find(a_actor->GetFormID()); it != g_ours.end()) {
					return gen::kEnemies[it->second].headNode;
				}
			}
			return "NPC Head [Head]";
		}

		// ---- night truce: townsfolk can't be hurt or killed while the undead are out ----
		// One of our undead by its base record (CurseOfAlduin.esp), whether or not this session raised it.
		bool IsOurBase(RE::Actor* a)
		{
			auto* base = a ? a->GetActorBase() : nullptr;
			auto* file = base ? base->GetFile(0) : nullptr;
			return file && file->GetFilename() == gen::kPlugin;
		}

		// Undead left over from a save (raised in an earlier session, so not on our books): they would be
		// immune to the night truce rules and fight followers forever. They crumble.
		void SweepStrays(RE::PlayerCharacter* a_player, float a_delta)
		{
			static float t = 0;
			if ((t -= a_delta) > 0) {
				return;
			}
			t = 2.0f;
			const bool party = Net::InLobby() || Coop::Client();  // in a party the other players' undead (Skyrim Together's copies) are here too: never ours to crumble
			int        n = 0;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				if (party) {
					break;
				}
				if (auto a = h.get(); a && a.get() != a_player && !g_ours.contains(a->GetFormID()) && IsOurBase(a.get()) && !a->IsDisabled()) {
					Vanish(a.get());
					++n;
				}
			}
			if (n) {
				logger::info("night: {} undead from an earlier session crumbled", n);
			}
			// (Noah: a dead one hung in the air in daylight) every body of ours near you is on the books to crumble -
			// the ones that died held, flung or frozen, out of the usual count - and none of ours outlives the night
			int late = 0, day = 0;
			const bool dark = IsNight();
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_player, 8000.0f, [&](RE::TESObjectREFR* r) {
				auto* a = r ? r->As<RE::Actor>() : nullptr;
				if (!a || a == a_player || a->IsDisabled()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (!g_ours.contains(a->GetFormID())) {
					// (Noah: after a load the bodies the freeing's light had felled stayed) a body of ours from before the
					// load is on no one's books: it crumbles
					if (a->IsDead() && IsOurBase(a)) {
						Vanish(a);
						++day;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto h = a->GetHandle();
				if (!dark && g_phase != Phase::Break && g_phase != Phase::Fighting) {
					Vanish(a);
					++day;
				} else if (a->IsDead() && std::none_of(g_fading.begin(), g_fading.end(), [&](const auto& f) { return f.first == h; })) {
					g_fading.push_back({ h, Settings::CorpseSeconds() });
					++late;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (late || day) {
				logger::info("night: {} stray bodies put on the books, {} undead left by day crumbled", late, day);
			}
		}

		struct Protected
		{
			RE::ActorHandle handle;
			bool            wasEssential;
			bool            spared = true;
		};
		std::vector<Protected> g_protected;
		float                  g_protectTimer = 0;

		// Night in a town: from the moment the countdown starts, everyone out in the streets goes indoors (they are taken
		// out of the world, so nothing of the night can hurt them), and walks back out at dawn. Followers, other players
		// and creatures stay. Every player's game does this for itself.
		std::vector<RE::ObjectRefHandle> g_sheltered;
		void TickShelter(RE::PlayerCharacter* a_player, float a_delta)
		{
			static float scan = 0;
			const bool   night = (g_phase == Phase::Break || g_phase == Phase::Fighting) && Settings::Shelter();
			if (!night) {
				if (!g_sheltered.empty()) {
					for (auto& h : g_sheltered) {
						if (auto r = h.get(); r && r->IsDisabled()) {
							// (a summoned creature sent indoors by an older build is let go, never walked back out masterless)
							if (auto* ra = r->As<RE::Actor>(); ra && ra->GetActorBase() && ra->GetActorBase()->IsSummonable()) {
								r->SetDelete(true);
								continue;
							}
							r->Enable(false);
						}
					}
					logger::info("shelter: dawn, {} townsfolk back out", g_sheltered.size());
					g_sheltered.clear();
				}
				return;
			}
			if ((scan -= a_delta) > 0) {
				return;
			}
			scan = 1.0f;
			static auto* npcKw = RE::TESForm::LookupByID<RE::BGSKeyword>(0x013794);  // ActorTypeNPC
			auto inTown = [](RE::Actor* a) {
				auto* cell = a->GetParentCell();
				if (!cell || cell->IsInteriorCell()) {
					return false;
				}
				auto* loc = a->GetCurrentLocation();
				for (int up = 0; loc && up < 4; ++up, loc = loc->parentLoc) {
					if (loc->HasKeywordString("LocTypeCity") || loc->HasKeywordString("LocTypeTown") || loc->HasKeywordString("LocTypeSettlement")) {
						return true;
					}
				}
				return false;
			};
			int n = 0;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!a || a.get() == a_player || a->IsDead() || a->IsDisabled() || a->IsPlayerTeammate() || IsOurs(a.get()) || Party::IsFriend(a.get()) ||
					a->IsCommandedActor() || (a->GetActorBase() && a->GetActorBase()->IsSummonable()) || Classes::IsOwnSummon(a.get()) || Director::PlayerSide(a.get(), a_player)) {
					// (Noah: "random Dremora spawn at the Echo Altar at dawn and attack me" - your own Conjure Dremora, a person
					// in town, was sent indoors for the night the moment it came, and walked back out at dawn its own master)
					continue;
				}
				auto* race = a->GetRace();
				if (!race || (npcKw && !race->HasKeyword(npcKw)) || a->IsHostileToActor(a_player) || !inTown(a.get())) {
					continue;
				}
				a->StopCombat();
				a->Disable();
				g_sheltered.push_back(a->CreateRefHandle());
				++n;
			}
			if (n) {
				logger::info("shelter: {} townsfolk went indoors for the night", n);
			}
		}

		bool IsPerson(RE::Actor* a)
		{
			static auto* npcKw = RE::TESForm::LookupByID<RE::BGSKeyword>(0x013794);  // ActorTypeNPC
			auto*        race = a ? a->GetRace() : nullptr;
			return race && npcKw && race->HasKeyword(npcKw);
		}
		bool Spareable(RE::Actor* a, RE::PlayerCharacter* a_player)
		{
			if (a->IsPlayerTeammate() || Party::IsFriend(a)) {
				return true;
			}
			return IsPerson(a) && !a->IsHostileToActor(a_player) && a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression) < 2.0f;
		}

		void TickTruce(RE::PlayerCharacter* a_player, float a_delta)
		{
			if ((g_protectTimer -= a_delta) > 0) {
				return;
			}
			g_protectTimer = 0.2f;
			const bool night = IsNight();
			if (!night) {
				for (auto& p : g_protected) {
					if (auto a = p.handle.get(); a && p.spared && !p.wasEssential) {
						a->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kEssential);
					}
				}
				g_protected.clear();
				return;
			}
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!a || a.get() == a_player || a->IsDead() || g_ours.contains(a->GetFormID()) || IsOurBase(a.get())) {
					continue;
				}
				auto it = std::find_if(g_protected.begin(), g_protected.end(), [&](const Protected& p) { return p.handle.get() == a; });
				if (it == g_protected.end()) {
					// Townsfolk, followers and anyone friendly are spared for the night; Skyrim's own monsters and
					// bandits that are out for you stay fair game.
					const bool ess = a->GetActorRuntimeData().boolFlags.all(RE::Actor::BOOL_FLAGS::kEssential);
					g_protected.push_back({ a->GetHandle(), ess, false });
					it = std::prev(g_protected.end());
				}
				// Townsfolk, followers and friends are spared for the night; Skyrim's own monsters, predators and bandits
				// that are out for you stay fair game (Noah: wolves and the like were spared - they aren't faction-hostile,
				// only aggressive - and so never fought or took damage). Looked at again every pass.
				const bool spare = Spareable(a.get(), a_player);
				if (spare != it->spared) {
					it->spared = spare;
					if (spare) {
						a->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kEssential);
					} else if (!it->wasEssential) {
						a->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kEssential);
					}
				}
				if (!it->spared) {
					continue;
				}
				auto*       av = a->AsActorValueOwner();
				const float missing = av->GetPermanentActorValue(RE::ActorValue::kHealth) - av->GetActorValue(RE::ActorValue::kHealth);
				if (missing > 0) {
					av->RestoreActorValue(RE::ActorValue::kHealth, missing);
				}
				if (a->IsInCombat()) {
					auto tgt = a->GetActorRuntimeData().currentCombatTarget.get();
					if (tgt && tgt.get() == a_player) {
						a->StopCombat();  // a stray shot doesn't start a fight at night
					}
				}
			}
		}

		// (Noah: Skyrim's creatures and ours never fight each other - they only go for players) Any fight between two
		// actors where neither side is a player, a follower, a party member or a wonder's summon, and either side is a
		// creature (not a person) or one of our undead, is dropped; our own undead are sent back after the player by the
		// chase loop. Followers and summons still fight anything that comes for the players.
		bool PlayerSide(RE::Actor* a, RE::PlayerCharacter* a_player)
		{
			// (Noah: "my summons were ignoring the Curse's enemies") a summon of yours is on your side: its fights are kept
			if (a && a->IsCommandedActor()) {
				if (auto c = a->GetCommandingActor(); c && (c.get() == a_player || Party::IsFriend(c.get()))) return true;
			}
			return a && (a == a_player || a->IsPlayerTeammate() || Party::IsFriend(a) || Echo::IsAlly(a) || Classes::IsOwnSummon(a));
		}
		void TickPeace(RE::PlayerCharacter* a_player, float a_delta)
		{
			// (Noah: the game crashed the moment Whiterun was freed) never while the night turns over - the dead crumbling,
			// the townsfolk walking back out - nor on anything half gone; each fighter looked at once in a while, and only
			// told to stop (it finds the player again by itself)
			static Phase   lastPhase = g_phase;
			static float   settle = 0, t = 0;
			static std::unordered_map<RE::FormID, float> recent;
			if (g_phase != lastPhase) {
				lastPhase = g_phase, settle = 6.0f;
			}
			if (!g_lifted.empty()) {
				settle = std::max(settle, 4.0f);
			}
			if ((settle -= a_delta) > 0 || (t -= a_delta) > 0) {
				return;
			}
			t = 0.5f;
			for (auto it = recent.begin(); it != recent.end();) {
				it = (it->second -= 0.5f) <= 0 ? recent.erase(it) : std::next(it);
			}
			auto alive = [](RE::Actor* x) { return x && !x->IsDead() && !x->IsDisabled() && !x->IsDeleted() && x->Is3DLoaded() && x->GetParentCell(); };
			int n = 0;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!alive(a.get()) || a.get() == a_player || !a->IsInCombat() || PlayerSide(a.get(), a_player) || recent.contains(a->GetFormID())) {
					continue;
				}
				auto tgt = a->GetActorRuntimeData().currentCombatTarget.get();
				if (!alive(tgt.get()) || PlayerSide(tgt.get(), a_player)) {
					continue;
				}
				const bool monsters = !IsPerson(a.get()) || !IsPerson(tgt.get()) || IsOurs(a.get()) || IsOurs(tgt.get()) || IsOurBase(a.get()) || IsOurBase(tgt.get());
				if (!monsters) {
					continue;  // people against people (guards and bandits) are Skyrim's own business
				}
				recent[a->GetFormID()] = 3.0f;
				a->StopCombat();
				if (++n >= 6) {
					break;  // (a few a pass)
				}
			}
			if (static int said = 0; n && said++ < 20) {
				logger::info("peace: {} creature fights dropped (only players are fair game)", n);
			}
		}

		void Frenzy(RE::Actor* a_actor, float a_seconds)
		{
			for (auto& s : g_alive) {
				if (auto a = s.handle.get(); a && a.get() == a_actor && !a->IsDead()) {
					if (s.frenzy <= 0) {
						if (auto* fac = UndeadFaction()) {
							a->RemoveFromFaction(fac);
						}
					}
					s.frenzy = a_seconds;
					s.tear = 0.2f;
				}
			}
		}

		bool IsFoe(RE::Actor* a)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!a || !p || a == p || a->IsDead() || a->IsPlayerTeammate() || Party::IsFriend(a) || Echo::IsAlly(a)) {
				return false;
			}
			if (IsRoundUndead(a)) {
				return true;
			}
			if (IsNight() && IsSpared(a)) {
				return false;
			}
			if (a->IsHostileToActor(p)) {
				return true;
			}
			if (a->IsInCombat()) {
				if (auto t = a->GetActorRuntimeData().currentCombatTarget.get(); t && t.get() == p) {
					return true;
				}
			}
			return !IsPerson(a) && a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression) >= 1.0f;  // predators and monsters
		}

		bool IsSpared(RE::Actor* a_actor)
		{
			return a_actor && std::any_of(g_protected.begin(), g_protected.end(), [&](const Protected& p) { return p.spared && p.handle.get().get() == a_actor; });
		}

		// Shorter days and nights (sheet: rounds.cycleMinutes): Skyrim's TimeScale global.
		void TickClock(float a_delta)
		{
			static float t = 0;
			if ((t -= a_delta) > 0) {
				return;
			}
			t = 5.0f;
			static auto* ts = RE::TESForm::LookupByID<RE::TESGlobal>(0x3A);  // TimeScale
			// (Noah: 18 minutes of day; the night lasts as long as its waves) the day's hours (05:36-20:00, 14.4 of the 24)
			// pass in dayMinutes: 48 (whole numbers: Skyrim Together's server runs the same). While the night is fought
			// the clock all but stops (1) and PaceNight moves it with the waves cleared; a night with no fighting (a freed
			// city, before the intro) passes at 38, about 15 minutes. In a friend's party the shared clock keeps its pace.
			const float dayHours = std::fmod(DuskHour() - DawnHour() + 24.0f, 24.0f);
			float       want = std::round(dayHours * 60.0f / gen::kDayMinutes);
			if (IsNight() && !Coop::Client()) {
				// (Noah: the night passes by - about ten minutes - and pauses just before dawn until its last wave is down,
				// then the morning comes) near dawn while waves remain the clock all but stops (HoldNight keeps it there)
				auto* pl  = RE::PlayerCharacter::GetSingleton();
				auto* cal = RE::Calendar::GetSingleton();
				const float nightHours = std::fmod(DawnHour() - DuskHour() + 24.0f, 24.0f);
				const float left = cal ? std::fmod(DawnHour() - cal->GetHour() + 24.0f, 24.0f) : 12.0f;
				const bool  fighting = (g_phase == Phase::Fighting || g_phase == Phase::Break) && !g_nightCleared && !(pl && Curse::SafeHere(pl));
				want = fighting && left < 0.4f ? 1.0f : std::round(nightHours * 60.0f / kNightMinutes);
			}
			if (ts && std::abs(ts->value - want) > 0.01f) {
				logger::info("clock: timescale {:.1f} -> {:.1f} (a full day in {:.0f} minutes)", ts->value, want, gen::kCycleMinutes);
				ts->value = want;
			}
		}


		// (Noah: "I will pick all the spawn points per city ... I will fire an arrow where I want each one to be") scout mode:
		// you fly where you look and nothing hurts you; no undead rise and the night stands still; every arrow of yours that
		// comes to rest is a spawn point for the city you are in - a beam of light stands on it, and it is written to
		// SKSE/CurseOfAlduin-spawnpoints.txt. An arrow landing on a beam already there takes that point away again.
		// (Noah: "remove the scout mode so players can't use it - we can add it again later") off: no MCM page, no F10.
		// Set true (and put the MCM page back in tools/mcm.py) to lay out spawn points again.
		constexpr bool kScoutAvailable = false;
		struct ScoutMk
		{
			int                 town;
			RE::NiPoint3        p;
			RE::ObjectRefHandle beam;
		};
		std::vector<ScoutMk> g_marks;
		bool                 g_scoutOn = false, g_marksLoaded = false;
		RE::NiPoint3         g_hover{};  // (scout flight) where you hang in the air
		struct ScoutArrow
		{
			RE::NiPoint3 last;
			float        still = 0;
			bool         marked = false, seen = true, moved = false;
		};
		std::unordered_map<std::uint32_t, ScoutArrow> g_scoutArrows;
		std::filesystem::path ScoutFile()
		{
			const auto d = SKSE::log::log_directory();
			return (d ? *d : std::filesystem::path{}) / "CurseOfAlduin-spawnpoints.txt";
		}
		std::string TownName(int t) { return t >= 0 && t < static_cast<int>(gen::kTowns.size()) ? std::string(gen::kTowns[t]) : std::string("Wilds"); }
		void SaveMarks()
		{
			std::ofstream out(ScoutFile(), std::ios::trunc);
			out << "# town x y z - spawn points marked in scout mode\n";
			for (const auto& m : g_marks) out << fmt::format("{} {:.0f} {:.0f} {:.0f}\n", TownName(m.town), m.p.x, m.p.y, m.p.z);
		}
		void LoadMarks()
		{
			g_marksLoaded = true;
			std::ifstream in(ScoutFile());
			std::string   line;
			while (std::getline(in, line)) {
				if (line.empty() || line[0] == '#') continue;
				std::istringstream ls(line);
				std::string        town;
				RE::NiPoint3       p;
				if (!(ls >> town >> p.x >> p.y >> p.z)) continue;
				int t = -1;
				for (int i = 0; i < static_cast<int>(gen::kTowns.size()); ++i) if (gen::kTowns[i] == town) t = i;
				g_marks.push_back({ t, p, {} });
			}
			logger::info("scout: {} spawn points read back", g_marks.size());
		}
		RE::TESBoundObject* ScoutBeamForm()
		{
			for (const auto& d : gen::kDress) {
				if (d.object == 0 && !d.light && d.z == 250.0f) return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESBoundObject>(d.formId, gen::kPlugin);
			}
			return nullptr;
		}
		void DropBeam(ScoutMk& m)
		{
			if (auto b = m.beam.get()) {
				b->Disable();
				b->SetDelete(true);
			}
			m.beam = {};
		}
		void ScoutConsole(const std::string& a_cmd)
		{
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>()) {
				if (auto* script = factory->Create()) {
					script->SetCommand(a_cmd);
					script->CompileAndRun(RE::PlayerCharacter::GetSingleton());
					delete script;
				}
			}
		}
		void ScoutBeams(RE::PlayerCharacter* a_player)
		{
			auto* form = ScoutBeamForm();
			for (auto& m : g_marks) {
				const bool close = m.p.GetDistance(a_player->GetPosition()) < 9000.0f;
				if (close && !m.beam.get() && form) {
					if (auto b = a_player->PlaceObjectAtMe(form, false)) {
						b->SetPosition(m.p);
						m.beam = b->CreateRefHandle();
					}
				} else if (!close && m.beam.get()) {
					DropBeam(m);
				}
			}
		}
		bool TickScout(RE::PlayerCharacter* a_player, float a_delta)
		{
			const bool want = kScoutAvailable && Settings::Scout() && Save::Flag(Save::kFlagStarted);
			auto*      av = a_player->AsActorValueOwner();
			if (want != g_scoutOn) {
				g_scoutOn = want;
				if (want) {
					if (!RE::PlayerCharacter::IsGodMode()) ScoutConsole("tgm");
					av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 200.0f);
					g_hover = a_player->GetPosition() + RE::NiPoint3{ 0, 0, 40.0f };
					// (Noah: "only really fast movement speed") the arrows already lying about are not marks: only ones shot now
					if (auto* mgr = RE::Projectile::Manager::GetSingleton()) {
						RE::BSSpinLockGuard lock(mgr->projectileLock);
						for (auto* list : { &mgr->limited, &mgr->unlimited, &mgr->pending }) {
							for (auto& h : *list) {
								if (auto pr = h.get(); pr && pr->GetFormType() == RE::FormType::ProjectileArrow) g_scoutArrows[pr->GetFormID()] = ScoutArrow{ pr->GetPosition(), 0, true };
							}
						}
					}
					av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.1f), av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.1f);  // (so the new speed takes)
					for (auto& z : g_alive) {
						if (auto a = z.handle.get(); a && !a->IsDead()) Vanish(a.get());
					}
					g_alive.clear();
					EndRisers();
					auto* dh = RE::TESDataHandler::GetSingleton();
					if (auto* bow = RE::TESForm::LookupByID<RE::TESBoundObject>(0x00013985); bow && a_player->GetItemCount(bow) == 0) a_player->AddObjectToContainer(bow, nullptr, 1, nullptr);
					if (auto* arrows = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0001397D)) a_player->AddObjectToContainer(arrows, nullptr, 100, nullptr);
					(void)dh;
					if (!g_marksLoaded) LoadMarks();
					for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
						RE::NiPoint3       c;
						float              yaw = 0;
						RE::TESWorldSpace* ws = nullptr;
						if (Shops::StandFrame(t, c, yaw, ws)) logger::info("scout: the Echo Altar of {} stands at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg", TownName(t), c.x, c.y, c.z, yaw * 57.2958f);
					}
					Hud::Banner("SCOUT MODE - fly where you look - arrows or B mark spawn points - N moves the altar here", 6.0f);
					logger::info("scout: on");
				} else {
					if (RE::PlayerCharacter::IsGodMode()) ScoutConsole("tgm");
					av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -200.0f);
					if (auto* ctrl = a_player->GetCharController()) ctrl->gravity = 1.0f;
					av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.1f), av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.1f);
					for (auto& m : g_marks) DropBeam(m);
					g_scoutArrows.clear();
					Hud::Banner("SCOUT MODE OFF", 2.5f);
					logger::info("scout: off");
				}
			}
			if (!g_scoutOn) return false;
			TickPurge(a_delta);
			// flight: moving, you go where you look - up and down too - fast (sprint: faster); let go high up and you hang
			// there; let go near the ground and you come down onto it, on your feet (Noah: "stuck in the falling animation":
			// a bow can't be drawn in the air - B marks from up there instead)
			{
				auto* ui = RE::UI::GetSingleton();
				const bool paused = ui && ui->GameIsPaused();
				auto*      ctrl = a_player->GetCharController();
				if (a_player->GetPosition().GetDistance(g_hover) > 400.0f) g_hover = a_player->GetPosition();  // (a door, a load: start from there)
				RE::NiPoint2 in{};
				if (auto* pc = RE::PlayerControls::GetSingleton()) in = pc->data.moveInputVec;
				const bool moving = std::abs(in.x) + std::abs(in.y) > 0.05f;
				float      ground = g_hover.z - 5000.0f;
				if (const auto top = WalkTop(a_player->GetParentCell(), g_hover, 40.0f)) ground = *top;
				static bool landed = true;
				if (!paused) {
					if (moving) {
						const float yaw = a_player->data.angle.z, pitch = a_player->data.angle.x;
						const RE::NiPoint3 fwd{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
						const RE::NiPoint3 right{ std::cos(yaw), -std::sin(yaw), 0.0f };
						const float speed = a_player->AsActorState()->IsSprinting() ? 5000.0f : 1800.0f;
						const float dt = std::min(a_delta, 0.05f);
						RE::NiPoint3 next = g_hover + (fwd * in.y + right * in.x) * (speed * dt);
						if (const auto top = WalkTop(a_player->GetParentCell(), next, 60.0f); top && next.z < *top + 4.0f) next.z = *top + 4.0f;  // (never into the ground)
						const bool skim = next.z - ground < 120.0f && pitch >= -0.05f;  // (on the ground and not looking up: you just run, fast)
						if (skim) {
							landed = true, g_hover = a_player->GetPosition();
							if (ctrl) ctrl->gravity = 1.0f;
						} else {
							landed = false, g_hover = next;
							if (ctrl) ctrl->gravity = 0.0f, ctrl->fallTime = 0.0f;
							a_player->SetPosition(g_hover, true);
						}
					} else if (!landed && g_hover.z - ground > 300.0f) {  // hanging in the air
						if (ctrl) ctrl->gravity = 0.0f, ctrl->fallTime = 0.0f;
						a_player->SetPosition(g_hover, true);
					} else {  // near the ground: down onto it
						if (!landed) {
							landed = true;
							a_player->SetPosition({ g_hover.x, g_hover.y, ground + 6.0f }, true);
						}
						if (ctrl) ctrl->gravity = 1.0f;
						g_hover = a_player->GetPosition();
					}
				}
			}
			// arrows of yours at rest: each a mark (gathered under the projectiles' lock, marked after it)
			std::vector<RE::NiPoint3> rest;
			if (auto* mgr = RE::Projectile::Manager::GetSingleton()) {
				RE::BSSpinLockGuard lock(mgr->projectileLock);
				for (auto& [k, a] : g_scoutArrows) a.seen = false;
				for (auto* list : { &mgr->limited, &mgr->unlimited, &mgr->pending }) {
					for (auto& h : *list) {
						auto pr = h.get();
						if (!pr || pr->GetFormType() != RE::FormType::ProjectileArrow || pr->GetProjectileRuntimeData().shooter.get().get() != a_player) continue;
						const auto pos = pr->GetPosition();
						auto [it, fresh] = g_scoutArrows.try_emplace(pr->GetFormID(), ScoutArrow{ pos });
						auto& a = it->second;
						a.seen = true;
						if (!fresh) {
							if (pos.GetDistance(a.last) < 2.0f) a.still += a_delta;
							else a.still = 0, a.moved = true;
							a.last = pos;
						}
						if (!a.marked && a.moved && a.still >= 0.25f) a.marked = true, rest.push_back(pos);
					}
				}
				std::erase_if(g_scoutArrows, [](const auto& e) { return !e.second.seen; });
			}
			for (const auto& p : rest) ScoutMark(p);
			static float beams = 0;
			if ((beams -= a_delta) <= 0) beams = 1.0f, ScoutBeams(a_player);
			return true;
		}
		bool Scouting() { return g_scoutOn; }
		bool Rising(RE::Actor* a_actor) { return IsRising(a_actor); }
		// where the crosshair meets the world (from the camera, along where you look)
		std::optional<RE::NiPoint3> LookHit(RE::PlayerCharacter* a_player)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			auto* cell = a_player->GetParentCell();
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!cam || !cam->cameraRoot || !world) return std::nullopt;
			const RE::NiPoint3 from = cam->cameraRoot->world.translate;
			const float yaw = a_player->data.angle.z, pitch = a_player->data.angle.x;
			const RE::NiPoint3 dir{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
			const RE::NiPoint3 to = from + dir * 25000.0f;
			const float s = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(from * s);
			pick.rayInput.to = RE::hkVector4(to * s);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
			{
				RE::BSReadLockGuard lock(world->worldLock);
				world->PickObject(pick);
			}
			if (!pick.rayOutput.HasHit()) return std::nullopt;
			return from + (to - from) * pick.rayOutput.hitFraction;
		}
		int NearestTown(const RE::NiPoint3& a_at, RE::TESWorldSpace* a_ws)
		{
			int best = -1;
			float bestD = 12000.0f;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				RE::NiPoint3 c;
				float yaw = 0;
				RE::TESWorldSpace* ws = nullptr;
				if (Shops::StandFrame(t, c, yaw, ws) && ws == a_ws && c.GetDistance(a_at) < bestD) bestD = c.GetDistance(a_at), best = t;
			}
			return best;
		}
		void ScoutKey(const RE::ButtonEvent* a_b)
		{
			if (!a_b || !a_b->IsDown() || a_b->GetDevice() != RE::INPUT_DEVICE::kKeyboard) return;
			auto* ui = RE::UI::GetSingleton();
			if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME) || !Save::Flag(Save::kFlagStarted)) return;
			const auto id = a_b->GetIDCode();
			if (!kScoutAvailable) return;
			if (id == 0x44) {  // F10: scout mode on / off
				SKSE::GetTaskInterface()->AddTask([] { Settings::SetScout(!Settings::Scout()); });
				return;
			}
			if (!g_scoutOn) return;
			if (id == 0x30) {  // B: a spawn point where you look
				SKSE::GetTaskInterface()->AddTask([] {
					auto* pl = RE::PlayerCharacter::GetSingleton();
					if (!pl) return;
					if (const auto at = LookHit(pl)) ScoutMark(*at);
					else Hud::Banner("NOTHING THERE TO MARK - LOOK AT THE GROUND", 2.0f);
				});
			} else if (id == 0x31) {  // N: this city's Echo Altar where you stand, facing the way you face
				SKSE::GetTaskInterface()->AddTask([] {
					auto* pl = RE::PlayerCharacter::GetSingleton();
					if (!pl) return;
					auto at = pl->GetPosition();
					if (const auto top = WalkTop(pl->GetParentCell(), at, 60.0f)) at.z = *top;
					const int t = NearestTown(at, pl->GetWorldspace());
					if (t < 0) {
						Hud::Banner("NO CITY NEAR ENOUGH FOR AN ALTAR", 2.5f);
						return;
					}
					const float yawDeg = pl->data.angle.z * 57.2958f;
					const auto d = SKSE::log::log_directory();
					std::ofstream out((d ? *d : std::filesystem::path{}) / "CurseOfAlduin-altars.txt", std::ios::app);
					out << fmt::format("{} {:.0f} {:.0f} {:.0f} {:.0f}\n", TownName(t), at.x, at.y, at.z, yawDeg);
					Hud::Banner(fmt::format("ECHO ALTAR SPOT SAVED FOR {}", TownName(t)), 3.0f);
					logger::info("scout: the Echo Altar of {} wanted at ({:.0f}, {:.0f}, {:.0f}) facing {:.0f} deg", TownName(t), at.x, at.y, at.z, yawDeg);
				});
			}
		}
		void ScoutMark(const RE::NiPoint3& a_at)
		{
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (!g_scoutOn || !pl) return;
			static auto        lastT = std::chrono::steady_clock::now() - std::chrono::seconds(5);
			static RE::NiPoint3 lastP{};
			const auto now = std::chrono::steady_clock::now();
			if (now - lastT < std::chrono::milliseconds(1200) && lastP.GetDistance(a_at) < 400.0f) return;  // (one arrow, seen twice)
			lastT = now, lastP = a_at;
			const int town = Curse::ZoneTown();
			for (std::size_t i = 0; i < g_marks.size(); ++i) {
				if (g_marks[i].p.GetDistance(a_at) < 110.0f) {
					DropBeam(g_marks[i]);
					const int t = g_marks[i].town;
					g_marks.erase(g_marks.begin() + static_cast<std::ptrdiff_t>(i));
					SaveMarks();
					const auto n = std::count_if(g_marks.begin(), g_marks.end(), [&](const ScoutMk& m) { return m.town == t; });
					Hud::Banner(fmt::format("SPAWN POINT REMOVED - {} LEFT IN {}", n, TownName(t)), 2.5f);
					logger::info("scout: removed the spawn point at ({:.0f}, {:.0f}, {:.0f}) in {}", a_at.x, a_at.y, a_at.z, TownName(t));
					return;
				}
			}
			g_marks.push_back({ town, a_at, {} });
			SaveMarks();
			ScoutBeams(pl);
			const auto n = std::count_if(g_marks.begin(), g_marks.end(), [&](const ScoutMk& m) { return m.town == town; });
			Hud::Banner(fmt::format("SPAWN POINT {} - {}", n, TownName(town)), 2.5f);
			logger::info("scout: spawn point {} in {} at ({:.0f}, {:.0f}, {:.0f})", n, TownName(town), a_at.x, a_at.y, a_at.z);
		}

		void Tick(float a_delta)
		{
			// (Noah: "my game crashed after I died" - it froze loading back) dead for good, the game is about to load the
			// last save: nothing more is spawned, moved or posed meanwhile (the load waits on the world's work to finish)
			static bool quiet = false;
			if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && pl->IsDead()) {
				if (!quiet) {
					quiet = true;
					Undead::Reset();
					logger::info("death: the night held still until the save loads");
				}
				return;
			}
			quiet = false;
			if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && TickScout(pl, a_delta)) return;
			// (Noah: "I was able to save during the night ... it let me save and it crashed my game") no saving from nightfall
			// to dawn: Skyrim's own switch for it (the one its opening uses), set as the night begins and lifted at dawn
			{
				static int noSave = -1;
				const int want = (Save::Flag(Save::kFlagStarted) && (g_phase != Phase::Day || IsNight())) ? 1 : 0;
				if (Save::Flag(Save::kFlagStarted) && want != noSave) {
					noSave = want;
					if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
						RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
						vm->DispatchStaticCall("Game", "SetInChargen", RE::MakeFunctionArguments(static_cast<bool>(want), static_cast<bool>(want), false), none);
					}
					logger::info("night: saving {}", want ? "locked until dawn" : "allowed again");
				}
			}
			TickRest();
			// a rest that runs into dusk ends there (the countdown has begun)
			if (auto* ui = RE::UI::GetSingleton(); ui && g_phase != Phase::Day && ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME)) {
				if (auto* q = RE::UIMessageQueue::GetSingleton()) {
					q->AddMessage(RE::SleepWaitMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				}
			}
			PowerUps::Tick(a_delta);
			Settings::Tick();
			TickFx(a_delta);
			TickRisers(a_delta);
			Atmos::Tick(a_delta);
			TickMist(a_delta, MistAmount());
			TickClock(a_delta);  // (Skyrim Together's server sets the same uTimeScale, 48: 18 minutes of daylight)
			auto* player = RE::PlayerCharacter::GetSingleton();
			Undead::Tick(player, a_delta);
			PublishEnemyBars(player);
			// Regen: after a few seconds without taking damage, health surges back (day and night).
			if (player) {
				static float last = -1, quiet = 0;
				auto*        av = player->AsActorValueOwner();
				const float  hp = av->GetActorValue(RE::ActorValue::kHealth);
				const float  maxHp = av->GetPermanentActorValue(RE::ActorValue::kHealth);
				quiet = (last >= 0 && hp < last - 0.01f) ? 0 : quiet + a_delta;
				if (quiet >= gen::kRegenDelay && hp < maxHp && !player->IsDead() && Party::State() == Party::Life::Alive) {
					av->RestoreActorValue(RE::ActorValue::kHealth, std::min(maxHp - hp, maxHp * gen::kRegenPerSecond * a_delta));
				}
				last = av->GetActorValue(RE::ActorValue::kHealth);
			}
			if (!player) {
				return;
			}
			TickPurge(a_delta);
			TickLifted(a_delta);
			if (!Coop::Client()) {
				SweepStrays(player, a_delta);  // (on a client every undead is the host's)
			}
			TickTruce(player, a_delta);
			TickPeace(player, a_delta);
			TickShelter(player, a_delta);
			Prune();
			MountChase(player);
			{
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.round = g_phase == Phase::Fighting ? g_current : Save::Get(gen::kAV_round);
				h.nightActive = g_phase == Phase::Fighting;
				h.nightEvent = g_phase == Phase::Fighting ? ThemeNowImpl() : -1;
				h.nightProgress = NightProgress();
				const int th = g_phase == Phase::Fighting ? g_waveTheme : -1;
				h.waveTheme = th >= 0 ? std::string(gen::kEvents[th].label) : (g_phase == Phase::Fighting && g_event >= 0 ? std::string(gen::kEvents[g_event].label) + " NIGHT" : std::string());
				const int tc = th >= 0 ? th : g_event;
				if (tc >= 0) {
					const auto c = gen::kEvents[tc].color;
					h.waveThemeColor = 0xFF000000u | ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);  // (RGB -> ImGui's ABGR)
				}
				h.nukeLeft = g_nukeLeft > 0 ? g_pause : 0.0f;  // (only a nuke's own lull is shown as one: a fall or a revive's isn't)
				const bool waving = g_phase == Phase::Fighting && g_waves > 0;
				h.wave = waving ? g_wave : 0, h.waves = waving ? g_waves : 0, h.waveSize = g_waveSize;
				h.waveLeft = waving ? std::max(0, g_waveSize - g_waveSpawned) + static_cast<int>(g_alive.size()) : 0;
				h.waveRest = g_waveRest;
				// the last of a wave: where each is (screen), for the HUD's markers
				h.lastDead.clear();
				if (waving && g_waveSpawned >= g_waveSize && !g_alive.empty() && g_alive.size() <= 3) {
					if (auto* cam = RE::Main::WorldRootCamera()) {
						for (auto& sp : g_alive) {
							auto a = sp.handle.get();
							if (!a || a->IsDead()) continue;
							RE::NiPoint3 at = a->GetPosition();
							at.z += 120.0f;
							float x = 0, y = 0, z = 0;
							cam->WorldPtToScreenPt3(at, x, y, z, 1e-5f);
							h.lastDead.push_back({ x, 1.0f - y, z > 0.0f ? 1.0f : 0.0f, at.GetDistance(player->GetPosition()) });
						}
					}
				}
			}
			TickSlow();
			g_pause = std::max(0.0f, g_pause - a_delta);
			g_nukeLeft = std::max(0.0f, g_nukeLeft - a_delta);
			Curse::Tick(a_delta);
			Echo::Tick(a_delta);
			Eggs::Tick(a_delta);
			TickDanceShout(a_delta);
			if (!Allowed(player)) {
				return;
			}
			if (Net::Fresh()) {
				NetFollow();  // the host's night (its clock, its events)
				if (g_phase != Phase::Fighting) {
					return;
				}
			} else if (!IsNight()) {
				if (g_phase != Phase::Day) {
					Dawn();
				}
				return;
			}
			switch (g_phase) {
			case Phase::Day:
				g_phase = Phase::Break;
				g_breakTimer = R.firstBreakSeconds;
				// (Noah: "two pop-ups when a night is starting") the night's own title under the circle says it - no banner over it
				Forms::Play(gen::Sound::night_falls);
				break;
			case Phase::Break:
				{
					std::scoped_lock l(Hud::Lock());
					Hud::Edit().countdown = std::max(0.0f, g_breakTimer - a_delta);
				}
				if ((g_breakTimer -= a_delta) <= 0) {
					StartRound(Save::Get(gen::kAV_round) + 1);
				}
				break;
			case Phase::Fighting:
				HoldNight(player);
				(void)&PaceNight;  // (Noah: the night passes at its own pace and holds just before dawn: HoldNight)
				for (auto& s : g_alive) {
					if (s.webbed > 0 && (s.webbed -= a_delta) <= 0) {
						if (auto a = s.handle.get()) {
							a->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, s.webAmount);
						}
					}
				}
				KeepThemComing(player, a_delta);
				TickChampions(player, a_delta);
				TickBruteAfter(player, a_delta);
				TickReach(player, a_delta);
				TickFrenzy(player, a_delta);
				TickBoss(player, a_delta);
				TickChampion();
				TickElites(player, a_delta);
				TickVolatiles(player, a_delta);
				if (Coop::Client()) {
					break;  // the host raises the dead; the night here is for counting and the HUD
				}
				if (Curse::SafeHere(player)) {
					// a freed city: the dead cannot enter (any that followed you in fall where they stand)
					if ((g_sanctuarySaid -= a_delta) <= 0 && !g_alive.empty() && !Eggs::WantsDead()) {
						CrumbleNear(player->GetPosition(), 1.0e9f);
						g_sanctuarySaid = 1.0f;
					}
					break;
				}
				TickWaves(player, a_delta);
				break;
			}
		}
	}

	// ---- BO3 power-ups: Insta-Kill, Double Points, Kaboom, Fire Sale ----
	namespace PowerUps
	{
		namespace
		{
			struct Drop
			{
				RE::ObjectRefHandle token, light;
				int                 kind;
				float               left;
			};
			std::vector<Drop> g_drops;
			float             g_timers[4] = {};   // seconds left: insta-kill, double points, (kaboom), fire sale
			int               g_dropsThisNight = 0, g_night = -1;
			std::mt19937      g_prng{ std::random_device{}() };
			constexpr const char* kNames[5] = { "INSTA-KILL", "DOUBLE POINTS", "NUKE", "FIRE SALE", "MAX AMMO" };
		}

		bool InstaKill() { return g_timers[0] > 0; }
		float Timer(int a_kind) { return a_kind >= 0 && a_kind < 4 ? g_timers[a_kind] : 0.0f; }
		void  SetTimer(int a_kind, float a_seconds)
		{
			if (a_kind >= 0 && a_kind < 4) {
				g_timers[a_kind] = a_seconds;
			}
		}
		float PointsMult() { return (g_timers[1] > 0 ? 2.0f : 1.0f) * Director::EventPointsMult(); }
		bool FireSale() { return g_timers[3] > 0; }

		// (Noah: "remove the BO3 power-up drops from the game for now") nothing drops; set true to bring them back
		constexpr bool kDropsOn = false;
		void MaybeDrop(const RE::NiPoint3& a_at)
		{
			if (!kDropsOn) return;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {  // (Noah: any enemy, any hour)
				return;
			}
			const int night = Save::Get(gen::kAV_round);
			if (night != g_night) {
				g_night = night, g_dropsThisNight = 0;
			}
			// like BO3: a few per round, never two on the ground at once
			// (Noah: a 10% drop chance for any enemy) no cap a night; at most three lying about at once
			if (g_drops.size() >= 3 || std::uniform_real_distribution<float>(0, 1)(g_prng) > 0.10f) {
				return;
			}
			const int kind = std::uniform_int_distribution<int>(0, 4)(g_prng);
			auto*     dh = RE::TESDataHandler::GetSingleton();
			auto*     stat = dh->LookupForm<RE::TESBoundObject>(gen::kPowerups[kind].first, gen::kPlugin);
			auto*     ligh = dh->LookupForm<RE::TESBoundObject>(gen::kPowerups[kind].second, gen::kPlugin);
			if (!stat) {
				return;
			}
			Drop d{ {}, {}, kind, 26.0f };
			if (auto t = player->PlaceObjectAtMe(stat, false)) {
				t->SetPosition(a_at + RE::NiPoint3{ 0, 0, 10.0f });
				d.token = t->GetHandle();
			}
			if (ligh) {
				if (auto l = player->PlaceObjectAtMe(ligh, false)) {
					l->SetPosition(a_at + RE::NiPoint3{ 0, 0, 50.0f });
					d.light = l->GetHandle();
				}
			}
			g_drops.push_back(d);
			++g_dropsThisNight;
			Forms::PlayAt(gen::Sound::powerup_spawn, player);
			logger::info("powerup: {} dropped", kNames[kind]);
		}

		void Remove(Drop& d)
		{
			for (auto h : { d.token, d.light }) {
				if (auto r = h.get()) {
					r->Disable();
					r->SetDelete(true);
				}
			}
		}

		void Grab(int a_kind, RE::PlayerCharacter* a_player, bool a_remote = false)
		{
			Forms::Play(gen::Sound::powerup_grab);
			Hud::Banner(kNames[a_kind], 2.5f);
			if (!a_remote) {
				Net::PowerUpTaken(a_kind);  // the whole party gets it
			}
			if (a_kind == 2) {  // Nuke: every undead on the field drops, and none rise for 20 seconds
				int n = 0;
				if (a_remote && Coop::Client()) {  // the host's game kills them (Skyrim Together shows it); here the flash and the quiet
					Director::Pause(20.0f);
					Forms::Play(gen::Sound::nuke);
					std::scoped_lock l(Hud::Lock());
					Hud::Edit().nukeFlash = 1.0f, Director::g_nukeLeft = 20.0f;
					return;
				}
				// (Noah: the nuke takes the whole wave) every living thing the night sent, near or far, the wyrms too
				for (auto& h : Director::AliveHandles()) {
					if (auto a = h.get(); a && !a->IsDead()) {
						a->KillImpl(a_player, 9999.0f, true, false);
						++n;
					}
				}
				for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {  // (and any stray of ours the list lost)
					if (auto a = h.get(); a && !a->IsDead() && Director::IsRoundUndead(a.get())) {
						a->KillImpl(a_player, 9999.0f, true, false);
						++n;
					}
				}
				Points::Add(400);
				Director::Pause(20.0f);
				Forms::Play(gen::Sound::nuke);
				{
					std::scoped_lock l(Hud::Lock());
					Hud::Edit().nukeFlash = 1.0f, Director::g_nukeLeft = 20.0f;
				}
				logger::info("powerup: nuke took {}, 20 s before they rise again", n);
			} else if (a_kind == 4) {  // (the classes overhaul) Max Ammo: every quiver, bolt bundle and weapon full
				for (int g = 0; g < static_cast<int>(gen::kGuns.size()); ++g) {
					if (Gun::Owns(g)) Gun::RefillAmmo(g);
				}
				Bows::Restock();
				Forms::Play(gen::Sound::reload_mag);
			} else {
				g_timers[a_kind] = 45.0f;  // (Noah: Insta-Kill and Double Points last 45 s)
			}
		}

		void Tick(float a_delta)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			for (auto& t : g_timers) {
				t = std::max(0.0f, t - a_delta);
			}
			if (!player) {
				return;
			}
			for (int k : Net::TakeRemotePowerUps()) {  // taken by a friend in the party
				if (k >= 0 && k < 5) {
					Grab(k, player, true);
				}
			}
			for (auto& d : g_drops) {
				d.left -= a_delta;
				auto t = d.token.get();
				if (!t) {
					d.left = -1;
					continue;
				}
				// turning slowly, and blinking in its last five seconds
				if (auto* n = t->Get3D()) {
					const float yaw = d.left * 1.6f;
					n->local.rotate.SetEulerAnglesXYZ(0.0f, 0.0f, yaw);
					n->local.translate.z = std::sin(d.left * 3.0f) * 4.0f;
					const bool hidden = d.left < 5.0f && std::fmod(d.left, 0.5f) < 0.2f;
					if (hidden) {
						n->GetFlags().set(RE::NiAVObject::Flag::kHidden);
					} else {
						n->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
					}
					RE::NiUpdateData ud;
					n->Update(ud);
				}
				RE::NiPoint3 to = t->GetPosition() - player->GetPosition();
				to.z *= 0.4f;
				if (to.Length() < 110.0f) {
					Grab(d.kind, player);
					d.left = -1;
				}
			}
			for (auto& d : g_drops) {
				if (d.left <= 0) {
					Remove(d);
				}
			}
			std::erase_if(g_drops, [](const Drop& d) { return d.left <= 0; });
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.puInsta = g_timers[0], h.puDouble = g_timers[1], h.puFireSale = g_timers[3];
		}
	}

	namespace Director
	{
		std::vector<RE::FormID> Sheltered()
		{
			std::vector<RE::FormID> out;
			for (auto& h : g_sheltered) {
				if (auto r = h.get()) {
					out.push_back(r->GetFormID());
				}
			}
			return out;
		}
		void Unshelter(const std::vector<RE::FormID>& a_ids)
		{
			// (runs while the save loads: the references are re-enabled once they exist; the night hides them again)
			Tasks::Add([a_ids] {
				for (auto id : a_ids) {
					if (auto* r = id ? RE::TESForm::LookupByID<RE::TESObjectREFR>(id) : nullptr; r && r->IsDisabled()) {
						r->Enable(false);
					}
				}
			});
			g_sheltered.clear();
		}

		// The undead charge straight in and strike (Noah: they circled and strafed like draugr first): every one of
		// ours gets a combat style of its own with no circling, no falling back, no flanking, no stalking, no strafing.
		void HeadlongCombat()
		{
			// (Noah: "remove any taunting or roaring - the enemies' animations combat only") the taunts Skyrim's fighters
			// play with their combat taunt lines (a raised weapon, a beat of the chest) never come round
			if (auto* gs = RE::GameSettingCollection::GetSingleton()) {
				for (const char* n : { "fCombatDialogueTauntMinElapsedTime", "fCombatDialogueTauntMaxElapsedTime" }) {
					if (auto* st = gs->GetSetting(n)) st->data.f = 100000.0f;
				}
			}
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESCombatStyle>();
			if (!factory) {
				return;
			}
			std::unordered_map<RE::TESCombatStyle*, RE::TESCombatStyle*> made;
			int n = 0;
			auto give = [&](RE::TESNPC* a_npc) {
				if (!a_npc) {
					return;
				}
				// (Noah: saves crashed as they loaded) no combat style is made at run time any more: a made form isn't in
				// the save, yet the night's levelled undead (their copies are saved) pointed at it. Their own style is
				// changed in place instead
				auto* old = a_npc->GetCombatStyle();
				if (!old) {
					return;
				}
				auto& cs = made[old];
				if (!cs) {
					cs = old;
					MakeHeadlong(cs);
				}
				++n;
			};
			for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
				auto* f = Forms::Enemy(i);
				if (!f) {
					continue;
				}
				if (auto* npc = f->As<RE::TESNPC>()) {
					give(npc);
				} else if (auto* lv = f->As<RE::TESLevCharacter>()) {
					for (auto& e : lv->entries) {
						if (e.form) {
							give(e.form->As<RE::TESNPC>());
						}
					}
				}
			}
			logger::info("undead: {} of ours fight headlong (no circling or strafing)", n);
		}
	}
}
