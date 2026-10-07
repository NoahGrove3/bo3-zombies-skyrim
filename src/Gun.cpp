#include "BO3Z.h"

// BO3 arsenal (sheet: weapons). Each gun is a Skyrim weapon item from BO3Zombies.esp. While one is drawn,
// Skyrim's attack/block and ready-weapon input are blocked (input handler hooks) and the mouse and R drive
// the gun: hitscan from the camera with spread, pellets, bursts and splash. Two slots, three with Mule Kick.
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
			if (!p || !w) {
				return;
			}
			RE::ActorEquipManager::GetSingleton()->EquipObject(p, w);
			if (!Drawn()) {
				p->DrawWeaponMagicHands(true);
			}
		}

		struct InputBlock
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
			return a && a != player && !a->IsDead() && a->Is3DLoaded() && (Director::IsRoundUndead(a) || a->IsHostileToActor(player));
		}

		RE::NiPoint3 HeadPos(RE::Actor* a)
		{
			if (auto* node = a->GetNodeByName(std::string(Director::HeadNodeFor(a)))) {
				return node->world.translate;
			}
			return a->GetPosition() + RE::NiPoint3{ 0, 0, 110 };
		}

		void Damage(RE::Actor* a_target, float a_bo3Damage, bool a_head, bool a_melee)
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
			Forms::Play(a_head && !a_melee ? gen::Sound::headshot : gen::Sound::hitmarker);
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().hitTime = 0.2f;
			Hud::Edit().hitKill = kill;
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
			const float dmg = g.damage * Perks::DamageMult() * CrewMult(0);
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
			for (int p = 0; p < g.pellets; ++p) {
				const auto dir = Spread(aim, spread);
				RE::Actor* best = nullptr;
				float      bestT = FLT_MAX;
				bool       head = false;
				for (auto* a : candidates) {
					const auto hp = HeadPos(a);
					if (auto [hd, ht] = RaySegment(origin, dir, hp, hp); hd <= headRadius && ht < bestT) {
						best = a, bestT = ht, head = true;
						continue;
					}
					if (auto [bd, bt] = RaySegment(origin, dir, a->GetPosition() + RE::NiPoint3{ 0, 0, 15 }, hp); bd <= g.hitRadius && bt < bestT) {
						best = a, bestT = bt, head = false;
					}
				}
				bool los = false;
				if (!best || !player->HasLineOfSight(best, los)) {
					continue;
				}
				if (!firstHit) {
					firstHit = best;
				}
				if (g.spell >= 0 && (g.spellOn == 1 || (g.spellOn == 3 && g_shots % 4 == 0)) && p == 0) {
					Cast(g.spell, best);  // spell visuals/effects on the target (Wabbajack: its own madness)
				}
				if (!best->IsDead()) {
					Damage(best, dmg * (head ? headMult : 1.0f), head, false);
				}
				if (g.special == 2) {  // chain (Skyforge Arc): jumps between nearby dead
					std::vector<RE::Actor*> hit{ best };
					RE::Actor*              cur = best;
					for (int k = 0; k < 6; ++k) {
						RE::Actor* next = nullptr;
						float      nd = g.splashRadius;
						for (auto* a : candidates) {
							if (a->IsDead() || std::find(hit.begin(), hit.end(), a) != hit.end()) {
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
						hit.push_back(next);
						cur = next;
					}
				}
				if (g.special == 1 && g.splashRadius > 0) {  // splash (Aetherium Ray, Ember Mortar)
					const auto at = origin + dir * bestT;
					for (auto* a : candidates) {
						if (a != best && !a->IsDead() && a->GetPosition().GetDistance(at) <= g.splashRadius) {
							Damage(a, g.splashDamage * Perks::DamageMult(), false, false);
						}
					}
				}
			}
			if (g.spell >= 0 && g.spellOn == 2) {
				Cast(g.spell, firstHit);  // fired spell flies at what you hit (or straight ahead)
			}
		}

		void StartReload()
		{
			const int gun = CurrentGun(), slot = SlotOf(gun);
			if (gun < 0 || slot < 0 || g_reload > 0) {
				return;
			}
			const auto s = Save::GetSlot(slot);
			const auto& g = gen::kGuns[gun];
			if (s.clip >= g.clip || s.reserve <= 0) {
				return;
			}
			g_reload = g.reloadMs / 1000.0f * Perks::ReloadMult();
			g_burstLeft = 0;
			Forms::Play(g.reloadSnd);
			Perks::OnReload(1.0f - static_cast<float>(s.clip) / static_cast<float>(g.clip));
		}

		void FinishReload()
		{
			const int gun = CurrentGun(), slot = SlotOf(gun);
			if (gun < 0 || slot < 0) {
				return;
			}
			auto s = Save::GetSlot(slot);
			const int moved = std::min(gen::kGuns[gun].clip - s.clip, s.reserve);
			s.clip += moved;
			s.reserve -= moved;
			Save::SetSlot(slot, s);
		}

		void TryFire(bool a_fromTrigger)
		{
			const int gun = CurrentGun(), slot = SlotOf(gun);
			if (gun < 0 || slot < 0 || g_cooldown > 0 || g_reload > 0) {
				return;
			}
			const auto& g = gen::kGuns[gun];
			auto s = Save::GetSlot(slot);
			if (s.clip <= 0) {
				if (a_fromTrigger) {
					Forms::Play(g.emptySnd);
				}
				StartReload();
				return;
			}
			s.clip -= 1;
			Save::SetSlot(slot, s);
			Forms::Play(g.fireSnd);
			FireOnce(g);
			if (g.fire == gen::Fire::Burst) {
				if (a_fromTrigger) {
					g_burstLeft = std::min(g.burst - 1, s.clip);
				}
				g_burstTimer = 0.07f;
				g_cooldown = g_burstLeft > 0 ? 0.07f : g.fireDelayMs / 1000.0f * Perks::FireDelayMult();
			} else {
				g_cooldown = g.fireDelayMs / 1000.0f * Perks::FireDelayMult();
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
				auto* w = Forms::Gun(sl.gun);
				const auto counts = p->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
				if (counts.empty() || s >= Gun::SlotCount()) {
					if (s >= Gun::SlotCount() && !counts.empty()) {
						p->RemoveItem(w, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);  // lost Mule Kick
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
			if (const int s = SlotOf(a_gun); s >= 0) {
				auto sl = Save::GetSlot(s);
				sl.reserve = gen::kGuns[a_gun].reserve;
				Save::SetSlot(s, sl);
			}
		}

		bool Give(int a_gun)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* w = Forms::Gun(a_gun);
			if (!p || !w) {
				return false;
			}
			if (Owns(a_gun)) {
				auto sl = Save::GetSlot(SlotOf(a_gun));
				sl.clip = gen::kGuns[a_gun].clip;
				sl.reserve = gen::kGuns[a_gun].reserve;
				Save::SetSlot(SlotOf(a_gun), sl);
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
				if (auto* old = Forms::Gun(Save::GetSlot(slot).gun)) {
					p->RemoveItem(old, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
			}
			Save::SetSlot(slot, { a_gun, gen::kGuns[a_gun].clip, gen::kGuns[a_gun].reserve });
			Save::SetActiveSlot(slot);
			p->AddObjectToContainer(w, nullptr, 1, nullptr);
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
			bool any = false;
			for (int s = 0; s < 3; ++s) {
				any |= Save::GetSlot(s).gun >= 0;
			}
			if (Save::Crew() >= 0 && !any) {
				Gun::Give(gen::kGunIdx_apprentice);
			}
			if (removed) {
				logger::info("gun: retired {} old guns from the inventory", removed);
				Hud::Banner("Your guns crumble to dust. The tomes remain.", 4.0f);
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
			if (dev == RE::INPUT_DEVICE::kKeyboard && code == gen::kKey2_reload && down) {
				StartReload();
				return true;
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
			g_cooldown = std::max(0.0f, g_cooldown - a_delta);
			g_knifeCd = std::max(0.0f, g_knifeCd - a_delta);
			if (g_reload > 0 && (g_reload -= a_delta) <= 0) {
				g_reload = 0;
				FinishReload();
			}
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
				rd.firstPersonFOV = gen::kViewmodelFov;
			}
			const auto now = Clock::now();
			std::erase_if(g_hits, [&](const auto& kv) { return now - kv.second.first > 2s; });

			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.gunOut = out;
			h.ads = g_ads;
			h.reloading = g_reload > 0;
			if (out) {
				const auto s = Save::GetSlot(SlotOf(gun));
				h.gunName = std::string(gen::kGuns[gun].label);
				h.clip = s.clip;
				h.reserve = s.reserve;
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
