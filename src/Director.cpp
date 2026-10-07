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
		int Weight(const gen::Enemy& e) { return e.elite && EliteCount() >= static_cast<int>(gen::elite::maxAlive) ? 0 : e.weight; }

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
			const float angle = a_player->GetAngleZ() + RE::NI_PI + Uniform(-2.1f, 2.1f);
			const float dist = Uniform(ring[0], ring[1]);
			const auto  base = a_player->GetPosition();
			RE::NiPoint3 pos{ base.x + std::sin(angle) * dist, base.y + std::cos(angle) * dist, base.z + R.spawnHeightOffset };
			float land = 0;
			if (!interior && RE::TES::GetSingleton()->GetLandHeight(pos, land)) {
				pos.z = land + R.spawnHeightOffset;
			}
			return pos;
		}

		bool SpawnOne(RE::PlayerCharacter* a_player, int a_force = -1, const RE::NiPoint3* a_at = nullptr)
		{
			const auto pick = a_force >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(a_force)) : PickEnemy();
			if (!pick) {
				return false;
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
			const float want = HealthFor(g_current);
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, want - av->GetPermanentActorValue(RE::ActorValue::kHealth));

			// BO3 tuning: speed tier by round, damage comes from our hit rule (weapon damage turned down),
			// and no faction ties so they only go for players and NPCs leave them alone.
			float speed = 100.0f;
			for (const auto& tier : gen::kSpeedTiers) {
				if (g_current >= static_cast<int>(tier[0])) {
					speed = tier[1];
				}
			}
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kSpeedMult, speed - av->GetBaseActorValue(RE::ActorValue::kSpeedMult));
			av->SetActorValue(RE::ActorValue::kAttackDamageMult, gen::kZombieWeaponDamageMult);
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
			// Close range only: bows, crossbows, staves, arrows and attack spells go; swords, axes and maces stay.
			{
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
						if (sp && sp->GetCastingType() != RE::MagicSystem::CastingType::kConstantEffect && sp->GetDelivery() != RE::MagicSystem::Delivery::kSelf) {
							actor->RemoveSpell(sp);
						}
					}
				}
				actor->GetActorRuntimeData().selectedSpells[0] = nullptr;
				actor->GetActorRuntimeData().selectedSpells[1] = nullptr;
			}
			if (gen::kEnemies[*pick].elite) {
				const float want = HealthFor(g_current) * gen::elite::healthMult;
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, want - av->GetPermanentActorValue(RE::ActorValue::kHealth));
				g_elites[actor->GetFormID()] = { actor->GetHandle(), 3.0f, 5.0f, false };
			}
			g_ours[actor->GetFormID()] = *pick;
			g_alive.push_back({ actor->GetHandle(), *pick });
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

		// ---- Grimmvar, the Undying Wyrm (sheet: boss) ----
		struct BossState
		{
			RE::ActorHandle handle;
			float           spawnIn{ -1 };  // seconds until it rises this night (-1 = not tonight)
			float           breath{ 0 }, roar{ 0 }, summon{ 0 }, step{ 0 }, slam{ 0 };
			bool            enraged{ false };
			float           maxHp{ 1 };
		} g_boss;

		int BossRow()
		{
			for (int i = 0; i < static_cast<int>(gen::kEnemies.size()); ++i) {
				if (gen::kEnemies[i].bossEvery > 0) {
					return i;
				}
			}
			return -1;
		}

		void CastAt(RE::Actor* a_caster, int a_spell, RE::Actor* a_target)
		{
			auto* m = Forms::Spell(a_spell);
			if (m && a_caster) {
				if (auto* c = a_caster->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
					c->CastSpellImmediate(m, false, a_target, 1.0f, false, 0.0f, a_caster);
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
					Forms::PlayAt(gen::Sound::knife, a.get());
					if (auto* fx = Forms::Ours(gen::kRiseFx)) {
						if (auto puff = a->PlaceObjectAtMe(fx, false)) {
							g_fx.push_back({ puff->CreateRefHandle(), 2.0f });
						}
					}
					e.lunge = gen::elite::lungeEvery;
				}
				// Terror Shriek: slows and wounds the player nearby.
				if ((e.shriek -= a_delta) <= 0 && dist < gen::elite::shriekRange) {
					auto* pav = a_player->AsActorValueOwner();
					pav->DamageActorValue(RE::ActorValue::kHealth, pav->GetPermanentActorValue(RE::ActorValue::kHealth) * gen::elite::shriekDamage);
					if (g_slowLeft <= 0) {
						pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -gen::elite::shriekSlow);
					}
					g_slowLeft = gen::elite::shriekSeconds;
					Forms::PlayAt(gen::Sound::hound_night, a.get());
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

		void SpawnBoss(RE::PlayerCharacter* a_player)
		{
			const int row = BossRow();
			if (row < 0) {
				return;
			}
			const auto p = a_player->GetPosition();
			const float a = a_player->GetAngleZ();
			RE::NiPoint3 at{ p.x + std::sin(a) * 900.0f, p.y + std::cos(a) * 900.0f, p.z + 60.0f };  // in front: you should see it rise
			if (!SpawnOne(a_player, row, &at)) {
				return;
			}
			auto boss = g_alive.back().handle.get();
			if (!boss) {
				return;
			}
			auto* av = boss->AsActorValueOwner();
			const float want = HealthFor(g_current) * gen::boss::healthMult;
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, want - av->GetPermanentActorValue(RE::ActorValue::kHealth));
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kSpeedMult, 100.0f - av->GetActorValue(RE::ActorValue::kSpeedMult));
			g_boss.handle = boss->GetHandle();
			g_boss.maxHp = want;
			g_boss.enraged = false;
			g_boss.breath = 4, g_boss.roar = 7, g_boss.summon = 10, g_boss.step = gen::boss::stepEvery, g_boss.slam = 2;
			Forms::Play(gen::Sound::hound_night);
			Hud::Banner(std::string(gen::kEnemies[row].label).append(" rises!"), 5.0f);
			logger::info("boss: {} risen with {:.0f} health", gen::kEnemies[row].id, want);
		}

		void TickBoss(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (g_boss.spawnIn > 0 && (g_boss.spawnIn -= a_delta) <= 0) {
				g_boss.spawnIn = -1;
				SpawnBoss(a_player);
			}
			auto boss = g_boss.handle.get();
			if (!boss) {
				return;
			}
			if (boss->IsDead()) {
				Points::Add(static_cast<int>(gen::boss::killPoints));
				Forms::Play(gen::Sound::night_end);
				Hud::Banner("The Undying Wyrm falls! +" + std::to_string(static_cast<int>(gen::boss::killPoints)), 6.0f);
				logger::info("boss: slain");
				g_boss.handle = {};
				PublishBoss(nullptr);
				return;
			}
			auto* av = boss->AsActorValueOwner();
			const float hpFrac = av->GetActorValue(RE::ActorValue::kHealth) / std::max(1.0f, g_boss.maxHp);
			if (!g_boss.enraged && hpFrac <= gen::boss::enrageAt) {
				g_boss.enraged = true;
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kSpeedMult, gen::boss::enrageSpeed);
				CastAt(boss.get(), gen::kSpell_dragonforce, a_player);
				Hud::Banner("Grimmvar is ENRAGED!", 3.0f);
			}
			const float cd = g_boss.enraged ? gen::boss::enrageCooldownMult : 1.0f;
			const float dist = boss->GetPosition().GetDistance(a_player->GetPosition());
			if ((g_boss.breath -= a_delta) <= 0 && dist < gen::boss::breathRange && dist > gen::boss::slamRange) {
				CastAt(boss.get(), gen::kSpell_firebreath, a_player);
				g_boss.breath = gen::boss::breathEvery * cd;
			}
			if ((g_boss.roar -= a_delta) <= 0 && dist < gen::boss::roarRange) {
				CastAt(boss.get(), gen::kSpell_dragonforce, a_player);
				g_boss.roar = gen::boss::roarEvery * cd;
			}
			if ((g_boss.slam -= a_delta) <= 0 && dist < gen::boss::slamRange) {
				auto* pav = a_player->AsActorValueOwner();
				pav->DamageActorValue(RE::ActorValue::kHealth, pav->GetPermanentActorValue(RE::ActorValue::kHealth) * gen::boss::slamDamage);
				Forms::PlayAt(gen::Sound::rise, boss.get());
				{
					std::scoped_lock l(Hud::Lock());
					Hud::Edit().hurtTime = 1.0f;
				}
				g_boss.slam = gen::boss::slamEvery * cd;
			}
			if ((g_boss.summon -= a_delta) <= 0) {
				const auto bp = boss->GetPosition();
				for (int i = 0; i < static_cast<int>(gen::boss::summonCount); ++i) {
					const float a = i * 2.0f * RE::NI_PI / gen::boss::summonCount;
					RE::NiPoint3 at{ bp.x + std::sin(a) * 220.0f, bp.y + std::cos(a) * 220.0f, bp.z + 40.0f };
					SpawnOne(a_player, 0, &at);
				}
				Hud::Banner("The Wyrm calls the dead!", 2.5f);
				g_boss.summon = gen::boss::summonEvery * cd;
			}
			if ((g_boss.step -= a_delta) <= 0 && dist > 500.0f) {
				// Wyrm Step: sinks away and bursts out of the ground beside you.
				const auto p = a_player->GetPosition();
				const float a = a_player->GetAngleZ() + Uniform(-2.5f, 2.5f);
				boss->SetPosition({ p.x + std::sin(a) * 300.0f, p.y + std::cos(a) * 300.0f, p.z + 40.0f }, true);
				if (auto* fx = Forms::Ours(gen::kRiseFx)) {
					if (auto puff = boss->PlaceObjectAtMe(fx, false)) {
						g_fx.push_back({ puff->CreateRefHandle(), 3.0f });
					}
				}
				Forms::PlayAt(gen::Sound::rise, boss.get());
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

		void StartRound(int a_round)
		{
			g_current = a_round;
			g_special = a_round >= R.specialFirstRound && (a_round - R.specialFirstRound) % R.specialEveryRounds == 0;
			// One round per night: the dead keep coming until dawn. How many walk at once grows night by night.
			g_toSpawn = std::clamp(static_cast<int>(std::ceil(TotalFor(a_round) * gen::kSpawnMultiplier / 3.0f)), 12, R.maxAlive);
			g_spawnTimer = 0;
			g_phase = Phase::Fighting;
			if (const int row = BossRow(); row >= 0 && a_round >= gen::kEnemies[row].minRound && a_round % gen::kEnemies[row].bossEvery == 0) {
				g_boss.spawnIn = gen::boss::spawnDelay;
			}
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().countdown = 0;
			}
			Forms::Play(g_special ? gen::Sound::hound_night : gen::Sound::night_start);
			Hud::Banner(g_special ? "NIGHT " + std::to_string(a_round) + " - THE HOUNDS ARE LOOSE" : "NIGHT " + std::to_string(a_round) + " - SURVIVE UNTIL DAWN");
			logger::info("round {} starts (lasts the night): up to {} undead at once, {:.0f} Skyrim health each{}", a_round, g_toSpawn, HealthFor(a_round), g_special ? " (hound night)" : "");
		}

		void Dawn()
		{
			int n = 0;
			// Dawn: the dead (standing or fallen) are gone with the light.
			for (auto& s : g_alive) {
				if (auto a = s.handle.get()) {
					g_ours.erase(a->GetFormID());
					a->Disable();
					a->SetDelete(true);
					++n;
				}
			}
			g_alive.clear();
			for (auto& h : g_corpses) {
				if (auto a = h.get()) {
					g_ours.erase(a->GetFormID());
					a->Disable();
					a->SetDelete(true);
				}
			}
			g_corpses.clear();
			if (g_phase == Phase::Fighting) {
				// Dawn ends the round: one more night survived (one more tally on the board).
				Save::Set(gen::kAV_round, g_current);
				Points::Add(gen::kPoints_round_survived);
				Forms::Play(gen::Sound::night_end);
				Hud::Banner("DAWN - NIGHT " + std::to_string(g_current) + " SURVIVED", 6.0f);
				logger::info("dawn: night {} survived, {} undead crumbled", g_current, n);
			}
			g_phase = Phase::Day;
			g_toSpawn = 0;
		}

		void Prune()
		{
			for (auto it = g_alive.begin(); it != g_alive.end();) {
				auto a = it->handle.get();
				if (!a || a->IsDeleted()) {
					it = g_alive.erase(it);
				} else if (a->IsDead()) {
					g_corpses.push_back(it->handle);
					g_settle.push_back({ it->handle, 0.0f });
					it = g_alive.erase(it);
				} else {
					++it;
				}
			}
			while (static_cast<int>(g_corpses.size()) > R.maxCorpses) {
				if (auto a = g_corpses.front().get()) {
					g_ours.erase(a->GetFormID());
					a->Disable();
					a->SetDelete(true);
				}
				g_corpses.pop_front();
			}
		}

		// Corpses that died without going limp (killed mid-animation, the body stays standing): knock them over.
		void SettleCorpses(RE::PlayerCharacter* a_player, float a_delta)
		{
			auto* proc = a_player->GetActorRuntimeData().currentProcess;
			for (auto& c : g_settle) {
				c.age += a_delta;
				auto a = c.handle.get();
				if (!a || !proc || c.age < 0.5f) {
					continue;
				}
				if (a->Is3DLoaded() && !a->IsInRagdollState()) {
					const RE::NiPoint3 from = a->GetPosition() + (a->GetPosition() - a_player->GetPosition()) * -0.05f - RE::NiPoint3{ 0, 0, 20 };
					proc->KnockExplosion(a.get(), from, c.age < 1.0f ? 0.8f : 2.0f);
					if (c.tries++ == 0) {
						logger::info("night: knocked over a standing corpse");
					}
				} else {
					c.age = 99.0f;
				}
			}
			std::erase_if(g_settle, [](const Settle& c) { return c.age > 3.0f || c.tries > 3; });
		}

		void KeepThemComing(RE::PlayerCharacter* a_player, float a_delta)
		{
			SettleCorpses(a_player, a_delta);
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
				const bool closing = s.lastDist < 0 || dist < s.lastDist - 30.0f || dist < 350.0f;
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
			g_phase = Phase::Day;
			g_alive.clear();
			g_corpses.clear();
			g_ours.clear();
			g_toSpawn = 0;
			g_boss = {};
			g_elites.clear();
			g_slowLeft = 0;
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
		bool IsRoundUndead(RE::Actor* a_actor) { return a_actor && g_ours.contains(a_actor->GetFormID()); }
		bool NightNow() { return IsNight(); }

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
		struct Protected
		{
			RE::ActorHandle handle;
			bool            wasEssential;
		};
		std::vector<Protected> g_protected;
		float                  g_protectTimer = 0;

		void TickTruce(RE::PlayerCharacter* a_player, float a_delta)
		{
			if ((g_protectTimer -= a_delta) > 0) {
				return;
			}
			g_protectTimer = 0.2f;
			const bool night = IsNight();
			if (!night) {
				for (auto& p : g_protected) {
					if (auto a = p.handle.get(); a && !p.wasEssential) {
						a->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kEssential);
					}
				}
				g_protected.clear();
				return;
			}
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!a || a.get() == a_player || a->IsDead() || g_ours.contains(a->GetFormID())) {
					continue;
				}
				if (std::none_of(g_protected.begin(), g_protected.end(), [&](const Protected& p) { return p.handle.get() == a; })) {
					const bool ess = a->GetActorRuntimeData().boolFlags.all(RE::Actor::BOOL_FLAGS::kEssential);
					g_protected.push_back({ a->GetHandle(), ess });
					a->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kEssential);
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

		// Shorter days and nights (sheet: rounds.cycleMinutes): Skyrim's TimeScale global.
		void TickClock(float a_delta)
		{
			static float t = 0;
			if ((t -= a_delta) > 0) {
				return;
			}
			t = 5.0f;
			static auto* ts = RE::TESForm::LookupByID<RE::TESGlobal>(0x3A);  // TimeScale
			const float  want = 24.0f * 60.0f / gen::kCycleMinutes;
			if (ts && std::abs(ts->value - want) > 0.01f) {
				logger::info("clock: timescale {:.1f} -> {:.1f} (a full day in {:.0f} minutes)", ts->value, want, gen::kCycleMinutes);
				ts->value = want;
			}
		}

		void Tick(float a_delta)
		{
			TickFx(a_delta);
			TickClock(a_delta);
			auto* player = RE::PlayerCharacter::GetSingleton();
			// Night regen: after a few seconds without taking damage, health surges back (nights only).
			if (player) {
				static float last = -1, quiet = 0;
				auto*        av = player->AsActorValueOwner();
				const float  hp = av->GetActorValue(RE::ActorValue::kHealth);
				const float  maxHp = av->GetPermanentActorValue(RE::ActorValue::kHealth);
				quiet = (last >= 0 && hp < last - 0.01f) ? 0 : quiet + a_delta;
				if (IsNight() && quiet >= gen::kRegenDelay && hp < maxHp && !player->IsDead()) {
					av->RestoreActorValue(RE::ActorValue::kHealth, std::min(maxHp - hp, maxHp * gen::kRegenPerSecond * a_delta));
				}
				last = av->GetActorValue(RE::ActorValue::kHealth);
			}
			if (!player) {
				return;
			}
			TickTruce(player, a_delta);
			Prune();
			{
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.round = g_phase == Phase::Fighting ? g_current : Save::Get(gen::kAV_round);
				h.nightActive = g_phase == Phase::Fighting;
			}
			if (!Allowed(player)) {
				return;
			}
			if (!IsNight()) {
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
				TickBoss(player, a_delta);
				TickElites(player, a_delta);
				if ((g_spawnTimer -= a_delta) <= 0 && static_cast<int>(g_alive.size()) < g_toSpawn) {
					SpawnOne(player);
					g_spawnTimer = SpawnDelay(g_current);
				}
				break;
			}
		}
	}
}
