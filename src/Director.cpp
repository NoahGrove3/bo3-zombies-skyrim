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
		};

		Phase                   g_phase = Phase::Day;
		int                     g_current = 0;   // round being fought
		int                     g_toSpawn = 0;
		float                   g_spawnTimer = 0;
		float                   g_breakTimer = 0;
		bool                    g_special = false;
		std::vector<Spawned>    g_alive;
		std::deque<RE::ActorHandle> g_corpses;
		std::unordered_map<RE::FormID, std::size_t> g_ours;  // every undead we raised -> enemy row
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
			float hp = static_cast<float>(R.hpStart);
			for (int r = 2; r <= a_round; ++r) {
				hp = r <= R.hpLinearUntil ? hp + R.hpLinearPerRound : hp * R.hpMultAfter;
			}
			return hp / R.hpScale;  // BO3 health -> Skyrim health
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

		std::optional<std::size_t> PickEnemy()
		{
			int total = 0;
			for (const auto& e : gen::kEnemies) {
				if (e.specialRound == g_special && (g_special || e.minRound <= g_current)) {
					total += e.weight;
				}
			}
			if (total <= 0) {
				return std::nullopt;
			}
			int roll = std::uniform_int_distribution<int>(1, total)(g_rng);
			for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
				const auto& e = gen::kEnemies[i];
				if (e.specialRound == g_special && (g_special || e.minRound <= g_current) && (roll -= e.weight) <= 0) {
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

		bool SpawnOne(RE::PlayerCharacter* a_player)
		{
			const auto pick = PickEnemy();
			if (!pick) {
				return false;
			}
			auto* cell = a_player->GetParentCell();
			const bool interior = cell->IsInteriorCell();
			const auto& ring = interior ? R.spawnRingInterior : R.spawnRingExterior;
			// Mostly behind the player, like BO3 windows you can't see.
			const float angle = a_player->GetAngleZ() + RE::NI_PI + Uniform(-2.1f, 2.1f);
			const float dist = Uniform(ring[0], ring[1]);
			const auto  base = a_player->GetPosition();
			RE::NiPoint3 pos{ base.x + std::sin(angle) * dist, base.y + std::cos(angle) * dist, base.z + R.spawnHeightOffset };
			if (!interior) {
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
				av->SetActorValue(RE::ActorValue::kAggression, 1);  // aggressive only toward enemies: none, except the player we point them at
			}
			g_ours[actor->GetFormID()] = *pick;
			g_alive.push_back({ actor->GetHandle(), *pick });
			return true;
		}

		bool IsOurs(RE::Actor* a) { return a && g_ours.contains(a->GetFormID()); }

		void StartRound(int a_round)
		{
			g_current = a_round;
			g_special = a_round >= R.specialFirstRound && (a_round - R.specialFirstRound) % R.specialEveryRounds == 0;
			g_toSpawn = static_cast<int>(std::ceil(TotalFor(a_round) * gen::kSpawnMultiplier));
			g_spawnTimer = 0;
			g_phase = Phase::Fighting;
			Forms::Play(gen::Sound::round_start);
			Hud::Banner(g_special ? "ROUND " + std::to_string(a_round) + " - THE HOUNDS ARE LOOSE" : "ROUND " + std::to_string(a_round));
			logger::info("round {} starts: {} undead, {:.0f} Skyrim health each{}", a_round, g_toSpawn, HealthFor(a_round), g_special ? " (hound round)" : "");
		}

		void EndRound()
		{
			Save::Set(gen::kAV_round, g_current);
			Points::Add(gen::kPoints_round_survived);
			Forms::Play(gen::Sound::round_end);
			logger::info("round {} cleared", g_current);
			g_phase = Phase::Break;
			g_breakTimer = R.breakSeconds;
		}

		void Dawn()
		{
			int n = 0;
			for (auto& s : g_alive) {
				if (auto a = s.handle.get(); a && !a->IsDead()) {
					a->KillImmediate();  // no killer: no points
					++n;
				}
			}
			g_alive.clear();
			if (g_phase == Phase::Fighting) {
				Hud::Banner("DAWN - ROUND " + std::to_string(g_current) + " RETURNS AT NIGHTFALL", 5.0f);
				logger::info("dawn: round {} unfinished, {} undead crumbled", g_current, n);
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

		void KeepThemComing(RE::PlayerCharacter* a_player, float a_delta)
		{
			for (auto& s : g_alive) {
				if (!gen::kEnemies[s.enemy].forceCombat || (s.combatRetry -= a_delta) > 0) {
					continue;
				}
				s.combatRetry = 0.75f;
				auto a = s.handle.get();
				if (!a || !a->Is3DLoaded() || a->IsDead()) {
					continue;
				}
				auto target = a->GetActorRuntimeData().currentCombatTarget.get();
				if (a->IsInCombat() && target.get() == a_player) {
					s.inCombat = true;
					continue;
				}
				if (a->IsInCombat()) {
					a->StopCombat();  // fighting an NPC: back to the player
				}
				a->StartCombat(a_player);
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
		void Web(const RE::NiPoint3& a_at, float a_radius, float a_seconds)
		{
			for (auto& s : g_alive) {
				if (auto a = s.handle.get(); a && !a->IsDead() && a->GetPosition().GetDistance(a_at) <= a_radius && s.webbed <= 0) {
					a->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -60.0f);
					s.webbed = a_seconds;
				}
			}
		}

		void Shock(const RE::NiPoint3& a_at, float a_radius, float a_bo3Damage)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			for (auto& s : g_alive) {
				auto a = s.handle.get();
				if (!a || a->IsDead() || a->GetPosition().GetDistance(a_at) > a_radius) {
					continue;
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

		bool IsRoundUndead(RE::Actor* a_actor) { return a_actor && g_ours.contains(a_actor->GetFormID()); }

		std::string_view HeadNodeFor(RE::Actor* a_actor)
		{
			if (a_actor) {
				if (auto it = g_ours.find(a_actor->GetFormID()); it != g_ours.end()) {
					return gen::kEnemies[it->second].headNode;
				}
			}
			return "NPC Head [Head]";
		}

		void Tick(float a_delta)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			Prune();
			{
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.round = g_phase == Phase::Fighting ? g_current : Save::Get(gen::kAV_round);
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
				break;
			case Phase::Break:
				if ((g_breakTimer -= a_delta) <= 0) {
					StartRound(Save::Get(gen::kAV_round) + 1);
				}
				break;
			case Phase::Fighting:
				for (auto& s : g_alive) {
					if (s.webbed > 0 && (s.webbed -= a_delta) <= 0) {
						if (auto a = s.handle.get()) {
							a->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 60.0f);
						}
					}
				}
				KeepThemComing(player, a_delta);
				if (g_toSpawn > 0) {
					if ((g_spawnTimer -= a_delta) <= 0 && static_cast<int>(g_alive.size()) < R.maxAlive) {
						if (SpawnOne(player)) {
							--g_toSpawn;
						}
						g_spawnTimer = SpawnDelay(g_current);
					}
				} else if (g_alive.empty()) {
					EndRound();
				}
				break;
			}
		}
	}
}
