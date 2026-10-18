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
			float           lastDist{ -1 };
			float           ability{ 4.0f };   // Iron Reaper: next reap / Necromancer: next summon
			float           windup{ -1.0f };   // Iron Reaper: seconds until the telegraphed sweep lands
			float           mounted{ 0 };      // the extra speed it has while the player rides (MountChase)
			float           hook{ 6.0f };      // Iron Reaper: next chain hook (mid range)
			float           hookWind{ -1.0f }; // Iron Reaper: the hook is thrown
			bool            enraged{ false };
			float           frenzy{ 0 };       // Mindbreaker: seconds left fighting its own kind
			float           tear{ 0 };
		};

		Phase                   g_phase = Phase::Day;
		int                     g_current = 0;   // round being fought
		int                     g_toSpawn = 0;
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
		float g_slowLeft = 0;  // Terror Shriek slow on the player  // short-lived effects (rise bursts)
		std::mt19937            g_rng{ std::random_device{}() };
		bool                    g_loggedBlocked = false;
		int                     g_event = -1;     // this night's event (sheet: events), -1 none
		bool                    g_weatherForced = false;
		float                   g_pause = 0;      // Nuke: seconds before the dead rise again
		float                   g_sanctuarySaid = 0;

		float Uniform(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }

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
			return hp / R.hpScale;  // design units -> Skyrim health
		}

		float SpawnDelay(int a_round)
		{
			return std::max(R.spawnDelayMin, R.spawnDelayStart * std::pow(R.spawnDelayDecay, static_cast<float>(a_round - 1)));
		}

		bool IsNight()
		{
			auto* cal = RE::Calendar::GetSingleton();
			if (!cal) {
				return false;
			}
			const float h = cal->GetHour();
			return R.nightStartHour > R.nightEndHour ? (h >= R.nightStartHour || h < R.nightEndHour) : (h >= R.nightStartHour && h < R.nightEndHour);
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
			if (e.elite && EliteCount() >= static_cast<int>(gen::elite::maxAlive)) {
				return 0;
			}
			if (g_event >= 0 && (e.kind == 1 || e.kind == 2)) {  // Night of Iron: the champions march
				return static_cast<int>(std::lround(e.weight * gen::kEvents[g_event].championWeight));
			}
			return e.weight;
		}

		std::optional<std::size_t> PickEnemy()
		{
			int total = 0;
			for (const auto& e : gen::kEnemies) {
				if (e.specialRound == g_special && (g_special || e.minRound <= g_current)) {
					total += Weight(e);
				}
			}
			if (total <= 0) {
				return std::nullopt;
			}
			int roll = std::uniform_int_distribution<int>(1, total)(g_rng);
			for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
				const auto& e = gen::kEnemies[i];
				if (e.specialRound == g_special && (g_special || e.minRound <= g_current) && Weight(e) > 0 && (roll -= Weight(e)) <= 0) {
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
		RE::NiPoint3 SpawnSpot(RE::PlayerCharacter* a_player)
		{
			const bool  interior = a_player->GetParentCell() && a_player->GetParentCell()->IsInteriorCell();
			const auto& ring = interior ? R.spawnRingInterior : R.spawnRingExterior;
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

		bool SpawnOne(RE::PlayerCharacter* a_player, int a_force = -1, const RE::NiPoint3* a_at = nullptr)
		{
			auto pick = a_force >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(a_force)) : PickEnemy();
			if (!pick) {
				return false;
			}
			// which of the risen dead it is: the Runner, the Wailer and the Bloater have bodies of their own
			std::optional<Undead::Type> rolled;
			if (gen::kEnemies[*pick].rig && gen::kEnemies[*pick].id == "zombie") {
				rolled = Undead::Roll(g_current);
				const std::string_view alt = *rolled == Undead::Type::Runner ? "runner" : *rolled == Undead::Type::Wailer ? "wailer" : *rolled == Undead::Type::Bloater ? "bloater" : "";
				for (std::size_t i = 0; !alt.empty() && i < gen::kEnemies.size(); ++i) {
					if (gen::kEnemies[i].id == alt && Forms::Enemy(i)) {
						pick = i;
					}
				}
			}
			auto* cell = a_player->GetParentCell();
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
			auto ref = a_player->PlaceObjectAtMe(npc, false);
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				logger::warn("spawn failed for {}", gen::kEnemies[*pick].id);
				return false;
			}
			actor->SetPosition(pos, true);
			actor->MoveToNearestNavmesh();

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
			} else if (rowId == "zombie") {
				type = Undead::Roll(g_current);
			} else if (rowId == "runner") {
				type = Undead::Type::Runner;
			} else if (rowId == "wailer") {
				type = Undead::Type::Wailer;
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
			const float want = HealthFor(g_current) * kindRow.healthMult * (type ? Undead::HealthMult(*type) : 1.0f) * Settings::UndeadHealthMult();
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, want - av->GetPermanentActorValue(RE::ActorValue::kHealth));

			// BO3 tuning: speed tier by round, damage comes from our hit rule (weapon damage turned down),
			// and no faction ties so they only go for players and NPCs leave them alone.
			float speed = 100.0f;
			for (const auto& tier : gen::kSpeedTiers) {
				if (g_current >= static_cast<int>(tier[0])) {
					speed = tier[1];
				}
			}
			speed *= kindRow.speedMult * (g_event >= 0 ? gen::kEvents[g_event].speedMult : 1.0f) * (type ? Undead::SpeedMult(*type) : 1.0f);
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
			const std::uint32_t    own = !kindRow.rig ? 0 : kindRow.kind == 1 ? gen::kReaperScythe : eid == "boss" ? gen::kWyrmClaws : eid == "shadow" ? gen::kShadowClaws : (eid == "zombie" || eid == "runner" || eid == "wailer" || eid == "bloater" || eid == "lich") ? gen::kZombieClaws : 0;
			if (own) {  // their own fighting style: the Reaper's scythe, Grimmvar's raking claws, the Gloomstalker's quick slashes
				if (auto* w = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(own, gen::kPlugin)) {
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
			g_alive.push_back({ actor->GetHandle(), *pick });
			if (type) {
				Undead::Assign(actor, *type);
			}
			if (kindRow.rig && rowId != "death_hound" && rowId != "skeleton") {
				Undead::SetEyes(actor, rowId);
			}
			Forms::PlayAt(gen::Sound::rise, actor);
			if (auto* fx = Forms::Ours(gen::kRiseFx)) {  // the ground bursts where they climb out
				if (auto puff = actor->PlaceObjectAtMe(fx, false)) {
					puff->SetPosition(actor->GetPosition());
					g_fx.push_back({ puff->CreateRefHandle(), 3.0f });
				}
			}
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
		int BossRow()
		{
			int rows[16], n = 0, every = 10;
			for (int i = 0; i < static_cast<int>(gen::kEnemies.size()) && n < 16; ++i) {
				if (gen::kEnemies[i].bossEvery > 0 && Forms::Enemy(i)) {
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
		void TickChampion()
		{
			auto c = g_champ.handle.get();
			if (!c) {
				return;
			}
			if (c->IsDead()) {
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
				Curse::OnChampionSlain(town);
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
					g_hazards.push_back({ OnGround(p), 0.45f, 150.0f, 0.10f, "tentacle", true });
					a->NotifyAnimationGraph("attackPowerStartForward");
					e.lunge = gen::elite::lungeEvery;
				}
				// Terror Shriek: slows and wounds the player nearby.
				if ((e.shriek -= a_delta) <= 0 && dist < gen::elite::shriekRange) {
					auto* pav = a_player->AsActorValueOwner();
					Perks::DamagePlayer(pav->GetPermanentActorValue(RE::ActorValue::kHealth) * gen::elite::shriekDamage * Settings::UndeadDamageMult());
					if (g_slowLeft <= 0) {
						pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -gen::elite::shriekSlow);
					}
					g_slowLeft = gen::elite::shriekSeconds;
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
					if (!a->IsInCombat() || !a->IsHostileToActor(a_player)) {
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
		void TickMist(float a_delta, bool a_on)
		{
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
			for (int ring = 0; ring < 4; ++ring) {
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
				const std::string_view id = ev.id;
				const auto bed = id == "bloodmoon" ? gen::Sound::amb_bloodmoon : id == "fog" ? gen::Sound::amb_fog : id == "ashstorm" ? gen::Sound::amb_ash : gen::Sound::amb_iron;
				Audio::LoopStart(Audio::kLoopNight, bed);
			}
			if (ev.boss && !Coop::Client() && g_boss.spawnIn < 0 && !g_boss.handle.get() && !g_boss.slainTonight) {
				g_boss.spawnIn = gen::kCurseHeraldDelay;
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
					Hud::Banner("NIGHT FALLS. THE DEAD RISE.", 4.0f);
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
				if (s.curse[t] > Curse::Raw(t)) {
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
			g_toSpawn = std::clamp(static_cast<int>(std::ceil(TotalFor(a_round) * gen::kSpawnMultiplier / 3.0f)), 12, R.maxAlive);
			g_spawnTimer = 0;
			g_phase = Phase::Fighting;
			g_boss.slainTonight = false;
			g_champ.slainTonight = false;
			if (const int row = BossRow(); !Coop::Client() && row >= 0 && a_round >= gen::kEnemies[row].minRound && a_round % gen::kEnemies[row].bossEvery == 0) {
				g_boss.spawnIn = gen::boss::spawnDelay;
			}
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().countdown = 0;
			}
			// Event nights: a one-in-four chance from night 10 on (never on a hound night).
			g_event = -1;
			constexpr bool kEventNights = false;  // (Noah: no special nights - no fog, blood moon, ash storm)
			if (kEventNights && !g_special && a_round >= 10 && std::uniform_real_distribution<float>(0, 1)(g_rng) < gen::kEventChance) {
				g_event = std::uniform_int_distribution<int>(0, static_cast<int>(gen::kEvents.size()) - 1)(g_rng);
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
				Hud::Banner(g_special ? "NIGHT " + std::to_string(a_round) + " - THE HOUNDS ARE LOOSE" : "NIGHT " + std::to_string(a_round) + " - SURVIVE UNTIL DAWN");
			}
			logger::info("round {} starts (lasts the night): up to {} undead at once, {:.0f} Skyrim health each{}", a_round, g_toSpawn, HealthFor(a_round), g_special ? " (hound night)" : "");
		}

		// Despawning: out of combat and hidden now, deleted a moment later (deleting an actor others are still
		// fighting mid-frame can crash the game).
		std::vector<std::pair<RE::ActorHandle, float>> g_purge;
		std::vector<std::pair<RE::ActorHandle, float>> g_fading;  // the fallen: seconds until the body crumbles
		void Vanish(RE::Actor* a)
		{
			g_ours.erase(a->GetFormID());
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
			Undead::Reset();  // no mind outlives the night (they reached for people while disabled)
			if (g_phase == Phase::Fighting) {
				// Dawn ends the round: one more night survived (one more tally on the board).
				Save::Set(gen::kAV_round, g_current);
				Points::Add(gen::kPoints_round_survived);
				Forms::Play(gen::Sound::night_end);
				Hud::Banner("DAWN - NIGHT " + std::to_string(g_current) + " SURVIVED", 6.0f);
				logger::info("dawn: night {} survived, {} undead crumbled", g_current, n);
				Party::DawnSave(g_current);  // every game in the party saves at this same dawn
			}
			g_phase = Phase::Day;
			g_toSpawn = 0;
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

		void TickChampions(RE::PlayerCharacter* a_player, float a_delta)
		{
			// a necromancer raises our own dead (never Skyrim's skeletons with their swords and maces)
			static int  minionRow = [] { for (int i = 0; i < static_cast<int>(gen::kEnemies.size()); ++i) if (gen::kEnemies[i].id == "zombie") return i; return -1; }();
			static std::vector<RE::ActorHandle> raised;
			std::erase_if(raised, [](const RE::ActorHandle& h) { auto a = h.get(); return !a || a->IsDead() || a->IsDeleted(); });
			std::vector<RE::NiPoint3> raise;
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
					// enraged below a third of its health: faster, and it reaps more often
					if (!s.enraged && av->GetActorValue(RE::ActorValue::kHealth) < av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.35f) {
						s.enraged = true;
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 35.0f);
						Forms::PlayAt(gen::Sound::champ_enrage, a.get());
					}
					if (s.windup >= 0) {
						if ((s.windup -= a_delta) < 0) {  // the sweep lands: everything in reach is cut and thrown
							if (dist < 300.0f && !a_player->IsDead()) {
								auto* pav = a_player->AsActorValueOwner();
								Perks::DamagePlayer(pav->GetPermanentActorValue(RE::ActorValue::kHealth) * (s.enraged ? 0.32f : 0.24f) * Settings::UndeadDamageMult());
								a_player->SetGraphVariableFloat("staggerMagnitude", 0.7f);
								a_player->NotifyAnimationGraph("staggerStart");
								Hud::Banner("REAPED", 0.8f);
							}
							Forms::PlayAt(gen::Sound::boss_land, a.get());
							s.ability = s.enraged ? 4.0f : 6.5f;
						}
					} else if ((s.ability -= a_delta) <= 0 && dist < 280.0f) {
						s.windup = 0.7f;  // telegraph: a heavy breath and a raised scythe before it falls
						a->NotifyAnimationGraph("attackPowerStartForward");
						Forms::PlayAt(gen::Sound::champ_telegraph, a.get());
					}
					// Chain Hook: from mid range it hurls a chain and drags you into scythe reach
					if (s.hookWind >= 0) {
						if ((s.hookWind -= a_delta) < 0 && dist > 300.0f && dist < 1300.0f && !a_player->IsDead()) {
							// the chain bites and staggers - it no longer drags you (it pulled people through bridges)
							const RE::NiPoint3 land = a_player->GetPosition();
							Director::HurtPlayer(0.08f, true);
							a_player->SetGraphVariableFloat("staggerMagnitude", 0.5f);
							a_player->NotifyAnimationGraph("staggerStart");
							Gun::FxBlast("dark", land + RE::NiPoint3{ 0, 0, 60 });
							Forms::PlayAt(gen::Sound::champ_chain, a_player);
							Hud::Banner("CHAINED", 0.9f);
							s.ability = std::min(s.ability, 0.6f);  // and the scythe comes right after
						}
					} else if ((s.hook -= a_delta) <= 0 && dist > 420.0f && dist < 1100.0f) {
						s.hookWind = 0.6f;
						s.hook = s.enraged ? 7.0f : 10.0f;
						a->NotifyAnimationGraph("attackStart");
						Forms::PlayAt(gen::Sound::champ_telegraph, a.get());
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

		void KeepThemComing(RE::PlayerCharacter* a_player, float a_delta)
		{
			SettleCorpses(a_player, a_delta);
			TickFading(a_delta);
			for (auto& s : g_alive) {
				if (!gen::kEnemies[s.enemy].forceCombat || (s.combatRetry -= a_delta) > 0) {
					continue;
				}
				constexpr float kStep = 0.75f;
				s.combatRetry = kStep;
				auto a = s.handle.get();
				if (!a || a->IsDead()) {
					continue;
				}
				if (s.frenzy > 0) {
					continue;  // turned on its own kind (TickFrenzy)
				}
				const bool  boss = gen::kEnemies[s.enemy].bossEvery > 0;
				const float dist = a->GetPosition().GetDistance(a_player->GetPosition());
				// Left behind (too far, or its cell unloaded): climb out of the ground again near the player, BO3-style.
				if (!boss && (dist > gen::kChaseRelocateDist || !a->Is3DLoaded())) {
					if ((s.behind += kStep) >= gen::kChaseRelocateSeconds) {
						a->StopCombat();
						a->MoveTo(a_player);
						a->SetPosition(SpawnSpot(a_player), true);
						a->MoveToNearestNavmesh();
						a->StartCombat(a_player);
						s.behind = 0, s.stall = 0, s.lastDist = -1;
						logger::info("night: relocated {} that fell behind ({:.0f} away)", gen::kEnemies[s.enemy].id, dist);
					}
					continue;
				}
				s.behind = 0;
				if (!a->Is3DLoaded()) {
					continue;
				}
				// Not closing in while still out of reach: it lost track of the player, so re-target from scratch.
				const bool closing = s.lastDist < 0 || dist < s.lastDist - 30.0f || dist < 350.0f || (gen::kEnemies[s.enemy].kind == 2 && dist < 2600.0f);  // casters hold their range
				s.lastDist = dist;
				s.stall = closing ? 0 : s.stall + kStep;
				auto target = a->GetActorRuntimeData().currentCombatTarget.get();
				if (a->IsInCombat() && target.get() == a_player && s.stall < gen::kChaseStallSeconds) {
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
					if (!npc || npc.get() == a_player || IsOurs(npc.get()) || !npc->IsInCombat()) {
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
			Audio::BossMusic(false);
			g_phase = Phase::Day;
			g_alive.clear();
			g_corpses.clear();
			g_ours.clear();
			g_toSpawn = 0;
			g_boss = {};
			g_champ = {};
			g_elites.clear();
			g_slowLeft = 0;
			g_pause = 0;
			EndEvent();
			Undead::Reset();
			Curse::Reset();
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
			logger::info("champion: {} risen in {} ({:.0f} health)", a_name, gen::kTowns[a_town], want);
			return true;
		}
		void EndNight()
		{
			auto* cal = RE::Calendar::GetSingleton();
			if (!cal || !IsNight() || Coop::Client()) {
				return;
			}
			// the clock jumps to first light; the night's round counts as survived (Dawn, from the Director's tick)
			const float h = cal->GetHour();
			const float hours = std::fmod(R.nightEndHour - h + 24.0f, 24.0f) + 0.05f;
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
				Hud::Banner("THE CITY IS FREE - DAWN BREAKS", 5.0f);
				logger::info("night: ended early by a freed city ({:.1f} hours skipped)", hours);
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
				SpawnOne(p, 0, &a_at);
			}
		}
		bool  IsBoss(RE::Actor* a_actor) { auto b = g_boss.handle.get(); return a_actor && b && b.get() == a_actor; }
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
		int   EventPointsMult() { return g_phase == Phase::Fighting && g_event >= 0 ? gen::kEvents[g_event].pointsMult : 1; }
		float EventDropMult() { return g_phase == Phase::Fighting && g_event >= 0 ? gen::kEvents[g_event].dropMult : 1.0f; }
		void  Pause(float a_seconds) { g_pause = std::max(g_pause, a_seconds); }
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
			const int fog = gen::kEvent_fog;
			if (g_event != fog) {
				EndEvent();
				g_event = fog;
				StartEvent(fog);
			}
			g_boss.spawnIn = gen::kCurseHeraldDelay;
			Hud::Banner("THE FOG ROLLS IN - THE HERALD OF THE DIRGE IS COMING", 6.0f);
		}
		// The curse lifted: a wave of light runs out from the shrine and each of the night's dead it reaches is struck
		// down (a flash of Turn Undead where it stands, the body falls), then crumbles to grave-dust a few seconds on -
		// nearest first, the far ones last. The corpses already lying about crumble with it.
		struct Lifted { RE::ActorHandle h; float strike, fade; bool struck; };
		std::vector<Lifted> g_lifted;

		void LiftWave(const RE::NiPoint3& a_from)
		{
			int  n = 0;
			for (auto& sp : g_alive) {
				auto a = sp.handle.get();
				if (!a || a->IsDisabled()) {
					continue;
				}
				const float d = a->GetPosition().GetDistance(a_from);
				const float delay = 0.6f + std::min(4.0f, d / 1500.0f) + 0.08f * (n % 7);
				g_lifted.push_back({ sp.handle, delay, 2.2f + 0.15f * (n % 9), a->IsDead() });
				++n;
			}
			g_alive.clear();
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
			if (Net::InLobby() || Coop::Client()) {
				return;  // in a party the other players' undead (Skyrim Together's copies) are here too: never ours to crumble
			}
			int n = 0;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				if (auto a = h.get(); a && a.get() != a_player && !g_ours.contains(a->GetFormID()) && IsOurBase(a.get()) && !a->IsDisabled()) {
					Vanish(a.get());
					++n;
				}
			}
			if (n) {
				logger::info("night: {} undead from an earlier session crumbled", n);
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
				if (!a || a.get() == a_player || a->IsDead() || a->IsDisabled() || a->IsPlayerTeammate() || IsOurs(a.get()) || Party::IsFriend(a.get())) {
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
					const bool spare = a->IsPlayerTeammate() || !a->IsHostileToActor(a_player);
					const bool ess = a->GetActorRuntimeData().boolFlags.all(RE::Actor::BOOL_FLAGS::kEssential);
					g_protected.push_back({ a->GetHandle(), ess, spare });
					if (spare) {
						a->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kEssential);
					}
					it = std::prev(g_protected.end());
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
			float        want = std::round(24.0f * 60.0f / gen::kCycleMinutes);  // (whole numbers: Skyrim Together's server runs the same 58)
			// (Noah: 15 minutes of day, 15 of night) the night's hours (20:00-05:36, 9.6 of the 24) pass at 38, the day's
			// (14.4) at 58 - in your own game; in a friend's party the shared clock keeps its pace
			if (IsNight() && !Coop::Client()) {
				want = 38.0f;
			}
			if (ts && std::abs(ts->value - want) > 0.01f) {
				logger::info("clock: timescale {:.1f} -> {:.1f} (a full day in {:.0f} minutes)", ts->value, want, gen::kCycleMinutes);
				ts->value = want;
			}
		}

		void Tick(float a_delta)
		{
			// a rest that runs into dusk ends there (the countdown has begun)
			if (auto* ui = RE::UI::GetSingleton(); ui && g_phase != Phase::Day && ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME)) {
				if (auto* q = RE::UIMessageQueue::GetSingleton()) {
					q->AddMessage(RE::SleepWaitMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				}
			}
			PowerUps::Tick(a_delta);
			Settings::Tick();
			TickFx(a_delta);
			TickMist(a_delta, g_phase == Phase::Fighting && g_event == gen::kEvent_fog);
			TickClock(a_delta);  // (Skyrim Together's server sets the same uTimeScale, 58: 15 minutes of day, 10 of night)
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
			TickShelter(player, a_delta);
			Prune();
			MountChase(player);
			{
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.round = g_phase == Phase::Fighting ? g_current : Save::Get(gen::kAV_round);
				h.nightActive = g_phase == Phase::Fighting;
				h.nightEvent = g_phase == Phase::Fighting ? g_event : -1;
				h.nukeLeft = g_pause;
			}
			g_pause = std::max(0.0f, g_pause - a_delta);
			Curse::Tick(a_delta);
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
				Hud::Banner("NIGHT FALLS. THE DEAD RISE.", 4.0f);
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
				for (auto& s : g_alive) {
					if (s.webbed > 0 && (s.webbed -= a_delta) <= 0) {
						if (auto a = s.handle.get()) {
							a->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, s.webAmount);
						}
					}
				}
				KeepThemComing(player, a_delta);
				TickChampions(player, a_delta);
				TickFrenzy(player, a_delta);
				TickBoss(player, a_delta);
				TickChampion();
				TickElites(player, a_delta);
				if (Coop::Client()) {
					break;  // the host raises the dead; the night here is for counting and the HUD
				}
				if (Curse::SafeHere(player)) {
					// a freed city: the dead cannot enter (any that followed you in fall where they stand)
					if ((g_sanctuarySaid -= a_delta) <= 0 && !g_alive.empty()) {
						CrumbleNear(player->GetPosition(), 1.0e9f);
						g_sanctuarySaid = 1.0f;
					}
					break;
				}
				{
					const float boost = Curse::SpawnBoost() * (g_event >= 0 ? gen::kEvents[g_event].spawnMult : 1.0f);
					const int   want = std::min(static_cast<int>(g_toSpawn * Curse::SpawnBoost()), R.maxAlive * 3 / 2);
					if (g_pause <= 0 && (g_spawnTimer -= a_delta) <= 0 && static_cast<int>(g_alive.size()) < want) {
						SpawnOne(player);
						g_spawnTimer = SpawnDelay(g_current) / std::max(0.25f, boost);
					}
				}
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
			constexpr const char* kNames[4] = { "INSTA-KILL", "DOUBLE POINTS", "NUKE", "FIRE SALE" };
		}

		bool InstaKill() { return g_timers[0] > 0; }
		float Timer(int a_kind) { return a_kind >= 0 && a_kind < 4 ? g_timers[a_kind] : 0.0f; }
		void  SetTimer(int a_kind, float a_seconds)
		{
			if (a_kind >= 0 && a_kind < 4) {
				g_timers[a_kind] = a_seconds;
			}
		}
		int  PointsMult() { return (g_timers[1] > 0 ? 2 : 1) * Director::EventPointsMult(); }
		bool FireSale() { return g_timers[3] > 0; }

		void MaybeDrop(const RE::NiPoint3& a_at)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !Director::NightNow()) {
				return;
			}
			const int night = Save::Get(gen::kAV_round);
			if (night != g_night) {
				g_night = night, g_dropsThisNight = 0;
			}
			// like BO3: a few per round, never two on the ground at once
			if (!g_drops.empty() || g_dropsThisNight >= 4 || std::uniform_real_distribution<float>(0, 1)(g_prng) > 0.035f * Director::EventDropMult()) {
				return;
			}
			const int kind = std::uniform_int_distribution<int>(0, 3)(g_prng);
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
					Hud::Edit().nukeFlash = 1.0f;
					return;
				}
				for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
					if (auto a = h.get(); a && !a->IsDead() && Director::IsRoundUndead(a.get())) {
						const int row = Director::EnemyRow(a.get());
						if (row < 0 || gen::kEnemies[row].bossEvery == 0) {  // the wyrm shrugs it off
							a->KillImpl(a_player, 9999.0f, true, false);
							++n;
						}
					}
				}
				Points::Add(400);
				Director::Pause(20.0f);
				Forms::Play(gen::Sound::nuke);
				{
					std::scoped_lock l(Hud::Lock());
					Hud::Edit().nukeFlash = 1.0f;
				}
				logger::info("powerup: nuke took {}, 20 s before they rise again", n);
			} else {
				g_timers[a_kind] = 30.0f;
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
				if (k >= 0 && k < 4) {
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
			SKSE::GetTaskInterface()->AddTask([a_ids] {
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
				auto* old = a_npc->GetCombatStyle();
				auto& cs = made[old];
				if (!cs) {
					cs = factory->Create();
					if (!cs) {
						return;
					}
					if (old) {
						cs->generalData = old->generalData, cs->meleeData = old->meleeData, cs->closeRangeData = old->closeRangeData;
						cs->longRangeData = old->longRangeData, cs->flightData = old->flightData, cs->flags = old->flags;
					}
					cs->generalData.offensiveMult = 1.0f, cs->generalData.defensiveMult = 0.0f, cs->generalData.groupOffensiveMult = 1.0f;
					cs->generalData.avoidThreatChance = 0.0f, cs->generalData.meleeScoreMult = std::max(1.0f, cs->generalData.meleeScoreMult);
					cs->closeRangeData.circleMult = 0.0f, cs->closeRangeData.fallbackMult = 0.0f;
					cs->closeRangeData.flankDistanceMult = 0.0f, cs->closeRangeData.stalkTimeMult = 0.0f;
					cs->longRangeData.strafeMult = 0.0f;
					// a dragon among them (the Hollow Wyrm, Diverse Dragons) lands to fight and stays down
					cs->flightData.groundAttackChance = 1.0f, cs->flightData.groundAttackTimeMult = 3.0f;
					cs->flightData.perchAttackChance = 0.0f, cs->flightData.diveBombChance = 0.0f, cs->flightData.hoverChance = 0.1f;
				}
				a_npc->SetCombatStyle(cs);
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
