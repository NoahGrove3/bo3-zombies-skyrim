#include "BO3Z.h"

// The arsenal (sheet: weapons). Wands are staff-type weapons with a free concentration enchantment; tomes and
// grimoires are concentration spells held in the right hand. Skyrim plays the cast (staff thrust / open hand) from
// the attack button; this file does the shots: hitscan from the camera with spread, pellets, bursts, element bolts
// and each wand's ability. Wands never run dry; tomes and grimoires spend magicka per shot. Two slots, three with
// Mammoth Back Porter.
namespace bo3z
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		bool  g_trigger = false, g_adsHeld = false;
		float g_ads = 0, g_cooldown = 0, g_reload = 0, g_knifeCd = 0, g_burstTimer = 0;
		int   g_burstLeft = 0;
		Clock::time_point g_swapDown{};
		bool  g_swapHeld = false;
		std::unordered_map<RE::FormID, std::pair<Clock::time_point, bool>> g_hits;
		std::mt19937 g_rng{ std::random_device{}() };
		bool  g_loggedAim = false;

		int CurrentGun()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p ? Forms::GunIndexOf(p->GetEquippedObject(false)) : -1;
		}
		bool Drawn()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p && p->AsActorState()->IsWeaponDrawn();
		}
		int SlotOf(int a_gun)
		{
			for (int s = 0; s < Gun::SlotCount(); ++s) {
				if (Save::GetSlot(s).gun == a_gun) {
					return s;
				}
			}
			return -1;
		}
		bool GunOut() { return CurrentGun() >= 0 && Drawn() && !Intro::CrewScreenOpen(); }

		void Equip(int a_gun)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* w = Forms::Gun(a_gun);
			if (!p || (!w && !Forms::GunSpell(a_gun))) {
				return;
			}
			if (auto* sp = Forms::GunSpell(a_gun)) {
				if (!p->HasSpell(sp)) {
					p->AddSpell(sp);
				}
				auto* right = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F42);  // RightHand equip slot
				RE::ActorEquipManager::GetSingleton()->EquipSpell(p, sp, right);
			} else {
				RE::ActorEquipManager::GetSingleton()->EquipObject(p, w);
			}
			if (!Drawn()) {
				p->DrawWeaponMagicHands(true);
			}
		}

		struct InputBlock
		{
			static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
			{
				if (GunOut()) {
					// The fire button goes through: Skyrim plays the staff/hand cast. Right mouse (left-hand
					// attack/block) stays ours for aiming.
					auto* b = a_event ? a_event->AsButtonEvent() : nullptr;
					if (b && b->GetDevice() == RE::INPUT_DEVICE::kMouse && b->GetIDCode() == gen::kKey2_fire) {
						return func(a_this, a_event);
					}
					return false;
				}
				return func(a_this, a_event);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct ReadyBlock
		{
			static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
			{
				if (GunOut()) {
					return false;
				}
				return func(a_this, a_event);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		float Rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }

		RE::NiPoint3 Spread(const RE::NiPoint3& a_dir, float a_deg)
		{
			if (a_deg <= 0) {
				return a_dir;
			}
			const float r = std::tan(a_deg * RE::NI_PI / 180.0f) * std::sqrt(Rand(0, 1)), t = Rand(0, 2 * RE::NI_PI);
			RE::NiPoint3 up{ 0, 0, 1 };
			RE::NiPoint3 right = a_dir.Cross(up);
			if (right.Length() < 1e-3f) {
				right = { 1, 0, 0 };
			}
			right.Unitize();
			RE::NiPoint3 u = right.Cross(a_dir);
			RE::NiPoint3 d = a_dir + right * (r * std::cos(t)) + u * (r * std::sin(t));
			d.Unitize();
			return d;
		}

		std::pair<float, float> RaySegment(const RE::NiPoint3& o, const RE::NiPoint3& d, const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			const RE::NiPoint3 v = b - a;
			const float        C = v.Dot(v);
			float t = C > 1e-6f ? std::clamp((o - a).Dot(v) / C, 0.0f, 1.0f) : 0.0f;
			float s = std::max(0.0f, (a + v * t - o).Dot(d));
			for (int i = 0; i < 2; ++i) {
				t = C > 1e-6f ? std::clamp((o + d * s - a).Dot(v) / C, 0.0f, 1.0f) : 0.0f;
				s = std::max(0.0f, (a + v * t - o).Dot(d));
			}
			return { ((o + d * s) - (a + v * t)).Length(), s };
		}

		bool Targetable(RE::Actor* a, RE::PlayerCharacter* player)
		{
			return a && a != player && !a->IsDead() && a->Is3DLoaded() && (Director::IsRoundUndead(a) || (!Director::NightNow() && a->IsHostileToActor(player)));  // nights: only the undead
		}

		RE::NiPoint3 HeadPos(RE::Actor* a)
		{
			if (auto* node = a->GetNodeByName(std::string(Director::HeadNodeFor(a)))) {
				return node->world.translate;
			}
			return a->GetPosition() + RE::NiPoint3{ 0, 0, 110 };
		}

		bool Damage(RE::Actor* a_target, float a_bo3Damage, bool a_head, bool a_melee, bool a_quiet = false)
		{
			auto*       player = RE::PlayerCharacter::GetSingleton();
			auto*       av = a_target->AsActorValueOwner();
			const float damage = a_bo3Damage / gen::kRounds.hpScale;
			av->DamageActorValue(RE::ActorValue::kHealth, damage);
			const bool ours = Director::IsRoundUndead(a_target);
			g_hits[a_target->GetFormID()] = { Clock::now(), a_head && !a_melee };
			const bool kill = av->GetActorValue(RE::ActorValue::kHealth) <= 0;
			if (kill) {
				if (a_melee) {
					g_hits.erase(a_target->GetFormID());  // knife kills pay the melee bonus
				}
				a_target->KillImpl(player, damage, true, false);
			} else {
				if (ours && !a_melee) {
					Points::Add(gen::kPoints_hit);
				}
				if (!a_target->IsInCombat()) {
					a_target->StartCombat(player);
				}
			}
			if (!a_quiet || kill) {
				Forms::Play(a_head && !a_melee ? gen::Sound::headshot : gen::Sound::hitmarker);
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().hitTime = 0.2f;
				Hud::Edit().hitKill = kill;
			}
			return kill;
		}

		// Venom wand: damage over time (BO3 units per second) on top of the hit.
		struct Dot
		{
			RE::ActorHandle target;
			float           perSecond, left;
		};
		std::vector<Dot> g_dots;
		void TickDots(float a_delta)
		{
			for (auto& d : g_dots) {
				auto a = d.target.get();
				if (!a || a->IsDead()) {
					d.left = 0;
					continue;
				}
				const float step = std::min(a_delta, d.left);
				Damage(a.get(), d.perSecond * step, false, false, true);
				d.left -= step;
			}
			std::erase_if(g_dots, [](const Dot& d) { return d.left <= 0; });
		}

		void Stagger(RE::Actor* a, float a_mag)
		{
			a->SetGraphVariableFloat("staggerMagnitude", a_mag);
			a->NotifyAnimationGraph("staggerStart");
		}

		float CrewMult(int a_passive)
		{
			const int c = Save::Crew();
			return c >= 0 && gen::kCrew[c].passive == a_passive ? gen::kCrew[c].value : 1.0f;
		}

		int g_shots = 0;

		void Cast(int a_spell, RE::Actor* a_target)
		{
			auto* m = Forms::Spell(a_spell);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!m || !player) {
				return;
			}
			if (auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
				caster->CastSpellImmediate(m, false, a_target, 1.0f, false, 0.0f, player);
			}
		}

		float AngleDeg(const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			const float d = std::clamp(a.Dot(b) / std::max(1e-4f, a.Length() * b.Length()), -1.0f, 1.0f);
			return std::acos(d) * 180.0f / RE::NI_PI;
		}

		std::vector<std::pair<RE::ObjectRefHandle, float>> g_flashes;  // muzzle lights, removed after a blink

		void FireOnce(const gen::GunDef& g)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->cameraRoot) {
				return;
			}
			const auto&  world = cam->cameraRoot->world;
			const auto   origin = world.translate;
			RE::NiPoint3 aim{ world.rotate.entry[0][1], world.rotate.entry[1][1], world.rotate.entry[2][1] };
			aim.Unitize();
			if (!g_loggedAim) {
				logger::info("aim check: camera dir ({:.2f},{:.2f},{:.2f}) yaw {:.2f} pitch {:.2f}", aim.x, aim.y, aim.z, player->GetAngleZ(), player->GetAngleX());
				g_loggedAim = true;
			}
			std::vector<RE::Actor*> candidates;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				if (auto a = h.get(); a && Targetable(a.get(), player) && a->GetPosition().GetDistance(origin) <= g.range) {
					candidates.push_back(a.get());
				}
			}
			const float spread = g.spreadHip + (g.spreadAds - g.spreadHip) * g_ads;
			const float headMult = g.headMult * Perks::HeadMult();
			const float headRadius = g.headRadius * (Perks::HeadMult() > 1.0f ? 1.5f : 1.0f);
			const int   lvl = SlotOf(CurrentGun()) >= 0 ? Save::GetSlot(SlotOf(CurrentGun())).level : 0;
			const float dmg = g.damage * Perks::DamageMult() * CrewMult(0) * (1.0f + gen::kForgeStep * lvl);
			++g_shots;
			if (g.special == 3) {  // cone blast (Thu'um Cannon): everything in front of you, harder up close
				bool los = false;
				for (auto* a : candidates) {
					const auto to = HeadPos(a) - origin;
					if (!a->IsDead() && AngleDeg(aim, to) <= g.spreadHip && player->HasLineOfSight(a, los)) {
						const float fall = 1.0f - 0.6f * std::min(1.0f, to.Length() / g.range);
						Damage(a, dmg * fall, false, false);
					}
				}
				if (g.spell >= 0) {
					Cast(g.spell, nullptr);
				}
				return;
			}
			RE::Actor* firstHit = nullptr;
			const auto sp = static_cast<gen::Special>(g.special);
			auto*      pav = player->AsActorValueOwner();
			for (int p = 0; p < g.pellets; ++p) {
				const auto dir = Spread(aim, spread);
				// everything this ray passes through, nearest first (pierce wands keep going)
				struct Hit { RE::Actor* a; float t; bool head; };
				std::vector<Hit> hits;
				for (auto* a : candidates) {
					const auto hp = HeadPos(a);
					if (auto [hd, ht] = RaySegment(origin, dir, hp, hp); hd <= headRadius) {
						hits.push_back({ a, ht, true });
					} else if (auto [bd, bt] = RaySegment(origin, dir, a->GetPosition() + RE::NiPoint3{ 0, 0, 15 }, hp); bd <= g.hitRadius) {
						hits.push_back({ a, bt, false });
					}
				}
				std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.t < y.t; });
				const int pierce = sp == gen::Special::Pierce ? std::max(1, static_cast<int>(g.abilityValue)) : 1;
				int       taken = 0;
				for (auto& h : hits) {
					if (taken >= pierce) {
						break;
					}
					bool los = false;
					if (!player->HasLineOfSight(h.a, los)) {
						continue;
					}
					++taken;
					RE::Actor* best = h.a;
					const bool head = h.head;
					const float bestT = h.t;
					if (!firstHit) {
						firstHit = best;
					}
					if (g.spell >= 0 && (g.spellOn == 1 || (g.spellOn == 3 && g_shots % 4 == 0)) && p == 0 && taken == 1) {
						Cast(g.spell, best);  // spell visuals/effects on the target (Wabbajack: its own madness)
					}
					const float hitDmg = dmg * (head ? headMult : 1.0f) * (taken > 1 ? 0.8f : 1.0f);
					bool        killed = false;
					if (!best->IsDead()) {
						killed = Damage(best, hitDmg, head, false);
					}
					switch (sp) {
					case gen::Special::Poison:
						if (!killed) {
							g_dots.push_back({ best->GetHandle(), hitDmg * g.abilityValue / 3.0f, 3.0f });
						}
						break;
					case gen::Special::Soul:
						if (killed) {
							Points::Add(static_cast<int>(100 * g.abilityValue));  // soul harvest
						}
						break;
					case gen::Special::Slow:
						if (!killed) {
							Director::Web(best->GetPosition(), 40.0f, 2.0f, 100.0f * g.abilityValue);
						}
						break;
					case gen::Special::Lifesteal:
						pav->RestoreActorValue(RE::ActorValue::kHealth, pav->GetPermanentActorValue(RE::ActorValue::kHealth) * g.abilityValue * 0.25f);
						break;
					case gen::Special::Stagger:
						if (!killed && Rand(0, 1) < g.abilityValue) {
							Stagger(best, 0.8f);
						}
						break;
					case gen::Special::Stasis:
						if (!killed && Rand(0, 1) < g.abilityValue) {
							Director::Web(best->GetPosition(), 40.0f, 2.5f, 95.0f);  // locked in amber
						}
						break;
					default:
						break;
					}
					if (sp == gen::Special::Chain) {  // chain (Skyforge): jumps between nearby dead
						std::vector<RE::Actor*> chained{ best };
						RE::Actor*              cur = best;
						for (int k = 0; k < 6; ++k) {
							RE::Actor* next = nullptr;
							float      nd = g.splashRadius;
							for (auto* a : candidates) {
								if (a->IsDead() || std::find(chained.begin(), chained.end(), a) != chained.end()) {
									continue;
								}
								if (const float d = a->GetPosition().GetDistance(cur->GetPosition()); d < nd) {
									nd = d, next = a;
								}
							}
							if (!next) {
								break;
							}
							Damage(next, g.splashDamage * Perks::DamageMult(), false, false);
							chained.push_back(next);
							cur = next;
						}
					}
					if (sp == gen::Special::Splash && g.splashRadius > 0) {  // splash (Auriel's Dawn, Embers)
						const auto at = origin + dir * bestT;
						for (auto* a : candidates) {
							if (a != best && !a->IsDead() && a->GetPosition().GetDistance(at) <= g.splashRadius) {
								Damage(a, g.splashDamage * Perks::DamageMult(), false, false);
							}
						}
					}
				}
			}
			if (g.spell >= 0 && g.spellOn == 2) {
				Cast(g.spell, firstHit);  // fired spell flies at what you hit (or straight ahead)
			}
			if (g.bulletSpell) {  // the visible element bolt
				if (auto* f = Forms::Ours(g.bulletSpell); f) {
					if (auto* m = f->As<RE::MagicItem>(); m) {
						if (auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
							caster->CastSpellImmediate(m, true, firstHit, 1.0f, false, 0.0f, player);
						}
					}
				}
			}
			Perks::OnCast(g_shots);
		}

		float g_manaQuiet = 0;  // seconds since the last magicka shot (regen waits for it)

		void TryFire(bool a_fromTrigger)
		{
			const int gun = CurrentGun(), slot = SlotOf(gun);
			if (gun < 0 || slot < 0 || g_cooldown > 0) {
				return;
			}
			const auto& g = gen::kGuns[gun];
			auto*       player = RE::PlayerCharacter::GetSingleton();
			if (g.mana > 0) {  // tomes and grimoires run on magicka
				auto*       av = player->AsActorValueOwner();
				const float cost = g.mana * Perks::ManaMult();
				if (av->GetActorValue(RE::ActorValue::kMagicka) < cost) {
					if (a_fromTrigger) {
						Forms::Play(g.emptySnd);
						Hud::Banner("Not enough magicka", 1.2f);
					}
					g_burstLeft = 0;
					g_cooldown = 0.35f;
					return;
				}
				av->DamageActorValue(RE::ActorValue::kMagicka, cost);
				g_manaQuiet = 0;
			}
			Forms::Play(g.fireSnd);
			FireOnce(g);
			const float delay = g.fireDelayMs / 1000.0f * Perks::FireDelayMult();
			if (g.fire == gen::Fire::Burst) {
				if (a_fromTrigger) {
					g_burstLeft = g.burst - 1;
				}
				g_burstTimer = 0.07f;
				g_cooldown = g_burstLeft > 0 ? 0.07f : delay;
			} else {
				g_cooldown = delay;
			}
		}

		void Knife()
		{
			if (g_knifeCd > 0) {
				return;
			}
			g_knifeCd = gen::kKnife.cooldownMs / 1000.0f;
			auto* player = RE::PlayerCharacter::GetSingleton();
			const float yaw = player->GetAngleZ();
			const RE::NiPoint3 fwd{ std::sin(yaw), std::cos(yaw), 0 };
			RE::Actor* best = nullptr;
			float      bestD = gen::kKnife.range;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!a || !Targetable(a.get(), player)) {
					continue;
				}
				RE::NiPoint3 to = a->GetPosition() - player->GetPosition();
				to.z = 0;
				const float d = to.Length();
				if (d > bestD || d < 1) {
					continue;
				}
				to.Unitize();
				if (to.Dot(fwd) < std::cos(gen::kKnife.coneDeg * RE::NI_PI / 180.0f)) {
					continue;
				}
				best = a.get(), bestD = d;
			}
			Forms::Play(gen::Sound::knife);
			if (best) {
				Damage(best, gen::kKnife.damage * CrewMult(2), false, true);
			}
		}

		void Cycle()
		{
			const int n = Gun::SlotCount();
			int       cur = SlotOf(CurrentGun());
			for (int k = 1; k <= n; ++k) {
				const int s = ((cur < 0 ? Save::ActiveSlot() - 1 : cur) + k + n) % n;
				if (const auto sl = Save::GetSlot(s); sl.gun >= 0) {
					Save::SetActiveSlot(s);
					Equip(sl.gun);
					g_reload = 0;
					return;
				}
			}
		}

		void Holster()
		{
			if (auto* p = RE::PlayerCharacter::GetSingleton(); p && Drawn()) {
				p->DrawWeaponMagicHands(false);
			}
		}

		// Keep slots and inventory in step: a gun dropped or sold leaves its slot.
		void SyncSlots()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) {
				return;
			}
			for (int s = 0; s < 3; ++s) {
				auto sl = Save::GetSlot(s);
				if (sl.gun < 0) {
					continue;
				}
				if (auto* sp = Forms::GunSpell(sl.gun)) {
					if (!p->HasSpell(sp) || s >= Gun::SlotCount()) {
						p->RemoveSpell(sp);
						Save::SetSlot(s, {});
					}
					continue;
				}
				auto* w = Forms::Gun(sl.gun);
				const auto counts = p->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
				if (counts.empty() || s >= Gun::SlotCount()) {
					if (s >= Gun::SlotCount() && !counts.empty()) {
						p->RemoveItem(w, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);  // lost the third slot
					}
					Save::SetSlot(s, {});
				}
			}
		}
	}

	namespace Gun
	{
		int   SlotCount() { return Save::HasPerk(gen::kPerk_mammoth) ? 3 : 2; }
		float AdsBlend() { return g_ads; }
		bool  Owns(int a_gun) { return SlotOf(a_gun) >= 0; }

		void RefillAmmo(int a_gun)
		{
			(void)a_gun;  // wands never run dry; tomes run on magicka
		}

		bool Give(int a_gun)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* w = Forms::Gun(a_gun);
			auto* spell = Forms::GunSpell(a_gun);
			if (!p || (!w && !spell)) {
				return false;
			}
			if (Owns(a_gun)) {
				Equip(a_gun);
				return true;
			}
			int slot = -1;
			for (int s = 0; s < SlotCount() && slot < 0; ++s) {
				if (Save::GetSlot(s).gun < 0) {
					slot = s;
				}
			}
			if (slot < 0) {  // full: replace the gun in hand (or the active slot)
				slot = SlotOf(CurrentGun());
				if (slot < 0) {
					slot = Save::ActiveSlot() % SlotCount();
				}
				const int oldGun = Save::GetSlot(slot).gun;
				if (auto* oldSpell = Forms::GunSpell(oldGun)) {
					p->RemoveSpell(oldSpell);
				} else if (auto* old = Forms::Gun(oldGun)) {
					p->RemoveItem(old, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
			}
			Save::SetSlot(slot, { a_gun, gen::kGuns[a_gun].clip, gen::kGuns[a_gun].reserve });
			Save::SetActiveSlot(slot);
			if (spell) {
				p->AddSpell(spell);
			} else {
				p->AddObjectToContainer(w, nullptr, 1, nullptr);
			}
			Equip(a_gun);
			logger::info("gun: {} -> slot {}", gen::kGuns[a_gun].id, slot);
			return true;
		}

		bool g_migrated = false;

		// The old Dwemer guns are retired: take them out of the inventory (slots follow) and make sure
		// the player has the Apprentice's Tome.
		void MigrateRetired()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || g_migrated) {
				return;
			}
			g_migrated = true;
			int removed = 0;
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (!gen::kGuns[i].retired) {
					continue;
				}
				auto* w = Forms::Gun(i);
				const auto counts = p->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
				for (auto& [obj, n] : counts) {
					p->RemoveItem(obj, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					++removed;
				}
			}
			for (int s = 0; s < 3; ++s) {
				auto sl = Save::GetSlot(s);
				if (sl.gun >= 0 && gen::kGuns[sl.gun].retired) {
					Save::SetSlot(s, {});
				}
			}
			// Tomes and grimoires used to be carried as staff items; now they live in your hands as spells.
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				auto* sp = Forms::GunSpell(i);
				auto* w = Forms::Gun(i);
				if (!sp || !w) {
					continue;
				}
				const auto counts = p->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
				for (auto& [obj, n] : counts) {
					p->RemoveItem(obj, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					++removed;
				}
				if (SlotOf(i) >= 0 && !p->HasSpell(sp)) {
					p->AddSpell(sp);
				}
			}
			bool any = false;
			for (int s = 0; s < 3; ++s) {
				any |= Save::GetSlot(s).gun >= 0;
			}
			if (Save::Flag(Save::kFlagStarted) && !any) {
				Gun::Give(gen::kGunIdx_sidearm);
			}
			if (removed) {
				logger::info("gun: retired {} old guns from the inventory", removed);
				Hud::Banner("Old weapons swapped for the new arsenal.", 4.0f);
			}
		}

		void HideVanillaBars()
		{
			static auto next = std::chrono::steady_clock::now();
			if (std::chrono::steady_clock::now() < next) {
				return;
			}
			next = std::chrono::steady_clock::now() + 1s;
			auto* ui = RE::UI::GetSingleton();
			auto  hud = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
			if (!hud || !hud->uiMovie) {
				return;
			}
			for (const char* bar : { "_root.HUDMovieBaseInstance.Health._alpha", "_root.HUDMovieBaseInstance.Magica._alpha", "_root.HUDMovieBaseInstance.Stamina._alpha" }) {
				hud->uiMovie->SetVariable(bar, RE::GFxValue(0.0));
			}
		}

		void Reset()
		{
			g_migrated = false;
			g_hits.clear();
			g_trigger = g_adsHeld = false;
			g_reload = g_cooldown = 0;
			g_burstLeft = 0;
		}

		// Atronach Reforge: the weapon in your hands goes up a level (more damage, full ammo).
		int ReforgeCost()
		{
			const int gun = CurrentGun(), slot = SlotOf(gun);
			if (gun < 0 || slot < 0) {
				return -1;
			}
			const int lvl = Save::GetSlot(slot).level;
			return lvl >= gen::kForgeMax ? 0 : gen::kForgeCost[lvl];
		}
		bool Reforge()
		{
			const int gun = CurrentGun(), slot = SlotOf(gun);
			if (gun < 0 || slot < 0) {
				Hud::Banner("Hold the tome you want reforged", 2.5f);
				return false;
			}
			auto s = Save::GetSlot(slot);
			if (s.level >= gen::kForgeMax) {
				Hud::Banner(std::string(gen::kGuns[gun].label) + " cannot hold more power", 2.5f);
				return false;
			}
			if (!Points::Spend(gen::kForgeCost[s.level])) {
				return false;
			}
			s.level += 1;
			s.clip = gen::kGuns[gun].clip;
			s.reserve = gen::kGuns[gun].reserve;
			Save::SetSlot(slot, s);
			logger::info("forge: {} -> level {}", gen::kGuns[gun].id, s.level);
			Hud::Banner(std::string(gen::kGuns[gun].label) + " reforged  " + std::string(s.level, '|') + "  (+" + std::to_string(static_cast<int>(gen::kForgeStep * 100 * s.level)) + "% damage)", 4.0f);
			return true;
		}

		bool OnButton(const RE::ButtonEvent* a_event)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || Intro::CrewScreenOpen()) {
				return false;
			}
			const auto dev = a_event->GetDevice();
			const auto code = a_event->GetIDCode();
			const bool down = a_event->IsDown(), up = !a_event->IsPressed();
			if (dev == RE::INPUT_DEVICE::kKeyboard && code == gen::kKey2_swap) {
				if (down) {
					g_swapDown = Clock::now();
					g_swapHeld = true;
				} else if (up && g_swapHeld) {
					g_swapHeld = false;
					if (Clock::now() - g_swapDown > 400ms) {
						Holster();
					} else {
						Cycle();
					}
				}
				return true;
			}
			if (dev == RE::INPUT_DEVICE::kKeyboard && code == gen::kKnife.key && down) {
				Knife();
				return true;
			}
			if (!GunOut()) {
				g_trigger = g_adsHeld = false;
				return false;
			}
			if (dev == RE::INPUT_DEVICE::kMouse && code == gen::kKey2_fire) {
				g_trigger = !up;
				if (down) {
					TryFire(true);
				}
				return true;
			}
			if (dev == RE::INPUT_DEVICE::kMouse && code == gen::kKey2_ads) {
				g_adsHeld = !up;
				return true;
			}
			return false;
		}

		// Night magic: magicka comes back fast once you stop casting (Quicksilver doubles it).
		void TickMana(float a_delta)
		{
			g_manaQuiet += a_delta;
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !Director::NightNow() || g_manaQuiet < gen::kManaRegenDelay) {
				return;
			}
			auto*       av = p->AsActorValueOwner();
			const float max = av->GetPermanentActorValue(RE::ActorValue::kMagicka), cur = av->GetActorValue(RE::ActorValue::kMagicka);
			if (cur < max) {
				av->RestoreActorValue(RE::ActorValue::kMagicka, std::min(max - cur, max * gen::kManaRegenPerSecond / Perks::ManaMult() * a_delta));
			}
		}

		// ---- aim down sights: pull the first-person gun onto the view line so you look down its barrel ----
		struct Quat { float w, x, y, z; };
		Quat ToQuat(const RE::NiMatrix3& m)
		{
			const auto& e = m.entry;
			const float tr = e[0][0] + e[1][1] + e[2][2];
			Quat q{};
			if (tr > 0) {
				const float s = std::sqrt(tr + 1.0f) * 2;
				q = { 0.25f * s, (e[2][1] - e[1][2]) / s, (e[0][2] - e[2][0]) / s, (e[1][0] - e[0][1]) / s };
			} else if (e[0][0] > e[1][1] && e[0][0] > e[2][2]) {
				const float s = std::sqrt(1.0f + e[0][0] - e[1][1] - e[2][2]) * 2;
				q = { (e[2][1] - e[1][2]) / s, 0.25f * s, (e[0][1] + e[1][0]) / s, (e[0][2] + e[2][0]) / s };
			} else if (e[1][1] > e[2][2]) {
				const float s = std::sqrt(1.0f + e[1][1] - e[0][0] - e[2][2]) * 2;
				q = { (e[0][2] - e[2][0]) / s, (e[0][1] + e[1][0]) / s, 0.25f * s, (e[1][2] + e[2][1]) / s };
			} else {
				const float s = std::sqrt(1.0f + e[2][2] - e[0][0] - e[1][1]) * 2;
				q = { (e[1][0] - e[0][1]) / s, (e[0][2] + e[2][0]) / s, (e[1][2] + e[2][1]) / s, 0.25f * s };
			}
			return q;
		}
		RE::NiMatrix3 FromQuat(Quat q)
		{
			const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
			q = { q.w / n, q.x / n, q.y / n, q.z / n };
			RE::NiMatrix3 m;
			m.entry[0][0] = 1 - 2 * (q.y * q.y + q.z * q.z), m.entry[0][1] = 2 * (q.x * q.y - q.z * q.w), m.entry[0][2] = 2 * (q.x * q.z + q.y * q.w);
			m.entry[1][0] = 2 * (q.x * q.y + q.z * q.w), m.entry[1][1] = 1 - 2 * (q.x * q.x + q.z * q.z), m.entry[1][2] = 2 * (q.y * q.z - q.x * q.w);
			m.entry[2][0] = 2 * (q.x * q.z - q.y * q.w), m.entry[2][1] = 2 * (q.y * q.z + q.x * q.w), m.entry[2][2] = 1 - 2 * (q.x * q.x + q.y * q.y);
			return m;
		}
		Quat Nlerp(Quat a, Quat b, float t)
		{
			if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0) {
				b = { -b.w, -b.x, -b.y, -b.z };
			}
			return { a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
		}

		RE::NiPointer<RE::NiAVObject> g_adsModel;
		RE::NiTransform               g_adsRest;

		void AimViewmodel(RE::PlayerCharacter* a_player, float a_t)
		{
			auto* fp = a_player ? a_player->Get3D(true) : nullptr;
			auto* bone = fp ? fp->GetObjectByName("WEAPON") : nullptr;
			auto* boneNode = bone ? bone->AsNode() : nullptr;
			RE::NiAVObject* model = nullptr;
			if (boneNode) {
				for (auto& c : boneNode->GetChildren()) {
					if (c) {
						model = c.get();
						break;
					}
				}
			}
			if (model != g_adsModel.get()) {  // gun swapped: put the old one back, remember the new one's rest pose
				if (g_adsModel) {
					g_adsModel->local = g_adsRest;
				}
				g_adsModel.reset(model);
				if (model) {
					g_adsRest = model->local;
				}
			}
			if (!model) {
				return;
			}
			if (a_t <= 0.001f) {
				model->local = g_adsRest;
				return;
			}
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->cameraRoot) {
				return;
			}
			// Camera basis from the view angles (pitch +down): forward f, up u, right r.
			const float pitch = a_player->data.angle.x, yaw = a_player->data.angle.z;
			const RE::NiPoint3 f{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
			const RE::NiPoint3 u{ std::sin(yaw) * std::sin(pitch), std::cos(yaw) * std::sin(pitch), std::cos(pitch) };
			const RE::NiPoint3 r{ std::cos(yaw), -std::sin(yaw), 0 };
			// Wand models: tip toward +Y, up +Z (point it where you look).
			RE::NiMatrix3 want;
			for (int i = 0; i < 3; ++i) {
				const float rv[3] = { r.x, r.y, r.z }, fv[3] = { f.x, f.y, f.z }, uv[3] = { u.x, u.y, u.z };
				want.entry[i][0] = rv[i], want.entry[i][1] = fv[i], want.entry[i][2] = uv[i];
			}
			const auto& pw = bone->world;
			const float  scale = pw.scale * g_adsRest.scale;
			const RE::NiPoint3 eye = cam->cameraRoot->world.translate;
			const RE::NiPoint3 at = eye + f * gen::kAdsEyeDistance - u * (gen::kAdsSightHeight * scale);
			const RE::NiMatrix3 inv = pw.rotate.Transpose();
			RE::NiTransform aim;
			aim.rotate = inv * want;
			aim.translate = (inv * (at - pw.translate)) / std::max(0.001f, pw.scale);
			aim.scale = g_adsRest.scale;
			// Ease in/out so the raise looks like a motion, not a snap.
			const float t = a_t * a_t * (3 - 2 * a_t);
			model->local.rotate = FromQuat(Nlerp(ToQuat(g_adsRest.rotate), ToQuat(aim.rotate), t));
			model->local.translate = g_adsRest.translate + (aim.translate - g_adsRest.translate) * t;
			model->local.scale = g_adsRest.scale;
		}

		void Tick(float a_delta)
		{
			static bool hooked = false;
			if (!hooked) {
				REL::Relocation<std::uintptr_t> ab{ RE::VTABLE_AttackBlockHandler[0] };
				InputBlock::func = ab.write_vfunc(0x1, InputBlock::thunk);
				REL::Relocation<std::uintptr_t> rw{ RE::VTABLE_ReadyWeaponHandler[0] };
				ReadyBlock::func = rw.write_vfunc(0x1, ReadyBlock::thunk);
				hooked = true;
				logger::info("gun: attack/ready input hooks installed");
			}
			MigrateRetired();
			HideVanillaBars();
			for (auto& [h, t] : g_flashes) {
				if ((t -= a_delta) <= 0) {
					if (auto r = h.get()) {
						r->Disable();
						r->SetDelete(true);
					}
				}
			}
			std::erase_if(g_flashes, [](const auto& e) { return e.second <= 0; });
			g_cooldown = std::max(0.0f, g_cooldown - a_delta);
			g_knifeCd = std::max(0.0f, g_knifeCd - a_delta);
			TickDots(a_delta);
			TickMana(a_delta);
			static float sync = 0;
			if ((sync -= a_delta) <= 0) {
				sync = 1.0f;
				SyncSlots();

			}
			const bool out = GunOut();
			const int  gun = CurrentGun();
			if (out) {
				const auto& g = gen::kGuns[gun];
				if (g_burstLeft > 0 && (g_burstTimer -= a_delta) <= 0) {
					--g_burstLeft;
					TryFire(false);
				} else if (g_trigger && g.fire == gen::Fire::Auto) {
					TryFire(false);
				}
			} else {
				g_trigger = g_adsHeld = false;
			}
			// Aim down sights: blend the field of view.
			const float target = out && g_adsHeld ? 1.0f : 0.0f;
			const float step = a_delta / std::max(0.01f, gen::kAdsSeconds);
			g_ads = target > g_ads ? std::min(target, g_ads + step) : std::max(target, g_ads - step);
			if (auto* cam = RE::PlayerCamera::GetSingleton()) {
				// Wider base view (sheet: movement.world_fov / viewmodel_fov), narrowing while aiming.
				auto& rd = cam->GetRuntimeData2();
				rd.worldFOV = gen::kWorldFov + (gen::kAdsFov - gen::kWorldFov) * g_ads;
				rd.firstPersonFOV = gen::kViewmodelFov + (gen::kAdsViewmodelFov - gen::kViewmodelFov) * g_ads;
			}
			AimViewmodel(RE::PlayerCharacter::GetSingleton(), out && gen::kGuns[gun].kind == gen::kKindWand ? g_ads : 0.0f);  // hand spells: just the zoom
			const auto now = Clock::now();
			std::erase_if(g_hits, [&](const auto& kv) { return now - kv.second.first > 2s; });

			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.gunOut = out;
			h.ads = g_ads;
			h.reloading = false;
			if (out) {
				const auto s = Save::GetSlot(SlotOf(gun));
				h.gunName = std::string(gen::kGuns[gun].label);
				h.gunLevel = SlotOf(gun) >= 0 ? Save::GetSlot(SlotOf(gun)).level : 0;
				h.clip = s.clip;
				h.reserve = s.reserve;
				h.gunKind = gen::kGuns[gun].kind;
				h.manaCost = gen::kGuns[gun].mana * Perks::ManaMult();
				h.gunElement = std::string(gen::kGuns[gun].abilityText);
				h.gunColor = gen::kGuns[gun].color;
			}
			h.points = Points::Get();
			h.perks = Save::PerkBits();
			{
				auto* pav = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
				h.hp = pav->GetActorValue(RE::ActorValue::kHealth), h.hpMax = pav->GetPermanentActorValue(RE::ActorValue::kHealth);
				h.mp = pav->GetActorValue(RE::ActorValue::kMagicka), h.mpMax = pav->GetPermanentActorValue(RE::ActorValue::kMagicka);
				h.sp = pav->GetActorValue(RE::ActorValue::kStamina), h.spMax = pav->GetPermanentActorValue(RE::ActorValue::kStamina);
			}
		}

		std::pair<bool, bool> RecentHit(RE::FormID a_actor)
		{
			if (auto it = g_hits.find(a_actor); it != g_hits.end() && Clock::now() - it->second.first < 500ms) {
				return { true, it->second.second };
			}
			return { false, false };
		}
	}
}
