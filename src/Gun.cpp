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
		float g_charge = 0, g_chargeCd = 0;  // hold-to-charge signature moves
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

		// Bows: their elemental arrows never run out (topped up quietly, kept on the string).
		void TopUpArrows(int a_gun, bool a_equip)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || a_gun < 0 || gen::kGuns[a_gun].kind != gen::kKindBow || !gen::kGuns[a_gun].ammo) {
				return;
			}
			auto* ammo = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESAmmo>(gen::kGuns[a_gun].ammo, gen::kPlugin);
			if (!ammo) {
				return;
			}
			const auto counts = p->GetInventoryCounts([ammo](RE::TESBoundObject& o) { return &o == ammo; });
			const int  have = counts.empty() ? 0 : counts.begin()->second;
			if (have < 40) {
				p->AddObjectToContainer(ammo, nullptr, 80 - have, nullptr);
			}
			if (a_equip || p->GetCurrentAmmo() != ammo) {
				RE::ActorEquipManager::GetSingleton()->EquipObject(p, ammo, nullptr, static_cast<std::uint32_t>(std::max(have, 80)), nullptr, true, false, false);
			}
		}

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
				auto* left = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F43);   // LeftHand: both hands carry the spell (the dual-cast charge)
				RE::ActorEquipManager::GetSingleton()->EquipSpell(p, sp, right);
				RE::ActorEquipManager::GetSingleton()->EquipSpell(p, sp, left);
			} else {
				RE::ActorEquipManager::GetSingleton()->EquipObject(p, w);
				TopUpArrows(a_gun, true);
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
					if (b && ((b->GetDevice() == RE::INPUT_DEVICE::kMouse && b->GetIDCode() == gen::kKey2_fire) ||
					             (b->GetDevice() == RE::INPUT_DEVICE::kGamepad && b->GetIDCode() == 0x000A))) {  // (RT on a pad)
						return func(a_this, a_event);
					}
					return false;
				}
				return func(a_this, a_event);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		// Staffs: Skyrim's own staff swing plays when the shot leaves, not when the button goes down. The press and
		// the hold never reach Skyrim (the hold is our charge-up, the staff held steady); on release, when a shot or
		// a signature move actually goes, Skyrim gets a short synthetic tap and plays its staff cast animation.
		RE::PlayerInputHandler* g_abHandler = nullptr;
		RE::PlayerControlsData* g_abData = nullptr;
		float                   g_swingUp = -1.0f;  // >= 0: a synthetic tap is down; let go when it runs out
		bool                    g_swingWanted = false;
		bool                    g_dual = false;     // hand spells: the left hand has come up for the charge
		float                   g_tapT = -1.0f;   // >= 0: the staff's short thrust for a normal shot is playing
		bool IsStaff(int a_gun) { return a_gun >= 0 && gen::kGuns[a_gun].kind == gen::kKindWand; }
		bool IsSpell(int a_gun) { return a_gun >= 0 && (gen::kGuns[a_gun].kind == gen::kKindTome || gen::kGuns[a_gun].kind == gen::kKindWonder); }

		// Controllers (Xbox, and PlayStation through Steam Input, which Skyrim reads as an Xbox pad):
		//   RT fire / charge, LT aim, D-pad right next weapon (hold: holster), D-pad down quick knife (Skyrim's hotkeys
		// on those two wait while a weapon of ours is out).
		constexpr std::uint32_t kPadRT = 0x000A, kPadLT = 0x0009, kPadRight = 0x0008, kPadDown = 0x0002;
		bool IsFireButton(const RE::ButtonEvent* b)
		{
			return b && ((b->GetDevice() == RE::INPUT_DEVICE::kMouse && b->GetIDCode() == gen::kKey2_fire) ||
			                (b->GetDevice() == RE::INPUT_DEVICE::kGamepad && b->GetIDCode() == kPadRT));
		}
		bool IsAimButton(const RE::ButtonEvent* b)
		{
			return b && ((b->GetDevice() == RE::INPUT_DEVICE::kMouse && b->GetIDCode() == gen::kKey2_ads) ||
			                (b->GetDevice() == RE::INPUT_DEVICE::kGamepad && b->GetIDCode() == kPadLT));
		}

		bool g_nativeCut = false;
		struct FireButton
		{
			static void thunk(RE::PlayerInputHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
			{
				if (GunOut() && (IsStaff(CurrentGun()) || IsSpell(CurrentGun())) && IsFireButton(a_event)) {
					g_abHandler = a_this, g_abData = a_data;
					return;  // ours: the staff swing is played on release (TickSwing)
				}
				if (GunOut() && gen::kGuns[CurrentGun()].kind != gen::kKindBow && IsFireButton(a_event)) {
					auto& rd = a_event->GetRuntimeData();
					if (a_event->IsDown()) {
						g_nativeCut = false;
					} else if (g_nativeCut) {
						return;  // held on, or the real release after we already let go
					} else if (a_event->IsHeld() && rd.heldDownSecs >= 0.12f) {
						const float v = rd.value;
						rd.value = 0.0f;  // reads as a release
						func(a_this, a_event, a_data);
						rd.value = v;
						g_nativeCut = true;
						return;
					}
				}
				func(a_this, a_event, a_data);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void SendAttack(bool a_left, float a_value, float a_held);
		void SendFire(float a_value, float a_held) { SendAttack(false, a_value, a_held); }
		void SendAttack(bool a_left, float a_value, float a_held)
		{
			if (auto* pc = RE::PlayerControls::GetSingleton(); pc && (!g_abHandler || !g_abData)) {
				// our input handler takes the fire button before Skyrim's attack handler ever sees it, so the
				// handler is fetched from the player controls (it never had a press to remember it by)
				g_abHandler = reinterpret_cast<RE::PlayerInputHandler*>(pc->attackBlockHandler);
				g_abData = &pc->data;
			}
			if (!g_abHandler || !g_abData) {
				static bool warned = false;
				if (!warned) {
					warned = true;
					logger::warn("staff: no attack handler for the native swing");
				}
				return;
			}
			auto* ue = RE::UserEvents::GetSingleton();
			auto* ev = a_left ? RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue ? ue->leftAttack : RE::BSFixedString("Left Attack/Block"), gen::kKey2_ads, a_value, a_held)
			                  : RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue ? ue->rightAttack : RE::BSFixedString("Right Attack/Block"), gen::kKey2_fire, a_value, a_held);
			if (!ev) {
				return;
			}
			FireButton::func(g_abHandler, ev, g_abData);
			RE::free(ev);
			static int said = 0;
			if (said++ < 4) {
				logger::info("staff: native swing {} (Skyrim's staff cast plays with the shot)", a_value > 0 ? "down" : "up");
			}
		}

		// The staff cast animation, in step with the shot: press now, let go 0.35 s later (long enough for Skyrim to
		// commit to the staff cast).
		void TickSwing(float a_delta)
		{
			if (g_swingUp >= 0.0f) {
				if ((g_swingUp += a_delta) >= 0.35f) {
					SendFire(0.0f, 0.35f);
					g_swingUp = -1.0f;
				}
				return;
			}
			if (g_swingWanted) {
				// Staffs are held with a mace grip now (see gen_esp.py): the shot's motion is ours (a short thrust in
				// TickPose), Skyrim's own attack never plays (it would be a mace swing).
				g_swingWanted = false;
				g_tapT = 0.0f;
			}
		}

		struct FavBlock  // the pad's D-pad right / down are ours while a weapon of ours is drawn
		{
			static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
			{
				auto* b = a_event ? a_event->AsButtonEvent() : nullptr;
				if (GunOut() && b && b->GetDevice() == RE::INPUT_DEVICE::kGamepad && (b->GetIDCode() == kPadRight || b->GetIDCode() == kPadDown)) {
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
			return a && a != player && !a->IsDead() && a->Is3DLoaded() && (Director::IsRoundUndead(a) || (a->IsHostileToActor(player) && !(Director::NightNow() && Director::IsSpared(a))));  // nights: the undead and real enemies, never townsfolk
		}

		RE::NiPoint3 HeadPos(RE::Actor* a)
		{
			if (auto* node = a->GetNodeByName(std::string(Director::HeadNodeFor(a)))) {
				return node->world.translate;
			}
			return a->GetPosition() + RE::NiPoint3{ 0, 0, 110 };
		}

		// Hexbinder / Plaguebringer marks (by form id): hexed take more damage, doomed explode on death, plagued pass it on.
		std::unordered_map<RE::FormID, float> g_hexed;              // -> seconds left
		std::unordered_map<RE::FormID, float> g_doomed;             // -> blast damage when it dies
		std::unordered_map<RE::FormID, float> g_plagued;            // -> plague damage per second
		struct Detonation
		{
			RE::NiPoint3 at;
			float        damage, delay;
			int          kind;  // 0 doom, 1 plague jump, 2 falling star
			int          gun = -1;
		};
		std::vector<Detonation> g_detonations;

		bool Damage(RE::Actor* a_target, float a_bo3Damage, bool a_head, bool a_melee, bool a_quiet = false)
		{
			auto*       player = RE::PlayerCharacter::GetSingleton();
			auto*       av = a_target->AsActorValueOwner();
			const auto  tid = a_target->GetFormID();
			const float hexMult = g_hexed.contains(tid) ? 1.5f : 1.0f;
			float       damage = a_bo3Damage * hexMult / gen::kRounds.hpScale;
			if (!Director::IsRoundUndead(a_target)) {
				// Skyrim's own enemies (bandits, draugr, dragons) are tuned to the player's level, not to the night: our
				// weapons hit them like gear of your level would (half strength at level 1, full at 25, 1.5x at 50+).
				damage *= std::clamp(0.5f + 0.02f * static_cast<float>(player->GetLevel()), 0.5f, 1.5f);
			}
			if (PowerUps::InstaKill() && Director::IsRoundUndead(a_target)) {
				const int row = Director::EnemyRow(a_target);
				if (row < 0 || gen::kEnemies[row].bossEvery == 0) {
					damage = av->GetActorValue(RE::ActorValue::kHealth) + 1.0f;  // Insta-Kill
				}
			}
			av->DamageActorValue(RE::ActorValue::kHealth, damage);
			const bool ours = Director::IsRoundUndead(a_target);
			g_hits[a_target->GetFormID()] = { Clock::now(), a_head && !a_melee };
			const bool kill = av->GetActorValue(RE::ActorValue::kHealth) <= 0;
			if (kill) {
				if (a_melee) {
					g_hits.erase(a_target->GetFormID());  // knife kills pay the melee bonus
				}
				if (!a_target->IsDead()) {
					a_target->KillImpl(player, damage, true, false);
				}
				if (auto it = g_doomed.find(tid); it != g_doomed.end()) {  // Doom Mark: it goes off, and takes the next ones with it
					g_detonations.push_back({ a_target->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, it->second, 0.15f, 0 });
					g_doomed.erase(it);
				}
				if (auto it = g_plagued.find(tid); it != g_plagued.end()) {  // the plague looks for its next host
					g_detonations.push_back({ a_target->GetPosition(), it->second, 0.3f, 1 });
					g_plagued.erase(it);
				}
				g_hexed.erase(tid);
				if (Coop::Client() && ours) {
					Curse::OnUndeadDeath(a_target);  // (the host's game prunes its own dead; here the kill is the count)
				}
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
				caster->CastSpellImmediate(m, false, a_target, 0.25f, false, 0.0f, player);  // the look; our numbers do the damage
			}
		}

		float AngleDeg(const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			const float d = std::clamp(a.Dot(b) / std::max(1e-4f, a.Length() * b.Length()), -1.0f, 1.0f);
			return std::acos(d) * 180.0f / RE::NI_PI;
		}

		std::vector<std::pair<RE::ObjectRefHandle, float>> g_flashes;  // muzzle lights, removed after a blink

		// Where the bolt leaves from: the wand's tip in first person, else the right hand's magic node, else the eyes.
		RE::NiPoint3 BoltOrigin(RE::PlayerCharacter* a_player, const gen::GunDef& g)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			RE::NiPoint3 eye = cam && cam->cameraRoot ? cam->cameraRoot->world.translate : a_player->GetPosition() + RE::NiPoint3{ 0, 0, 120 };
			for (bool first : { true, false }) {
				auto* root = a_player->Get3D(first);
				if (!root) {
					continue;
				}
				if (g.kind == gen::kKindWand) {
					if (auto* w = root->GetObjectByName("WEAPON"); w && w->AsNode()) {
						for (auto& c : w->AsNode()->GetChildren()) {
							if (c) {
								return c->world.translate + c->world.rotate * RE::NiPoint3{ 0, 62.0f, 0 } * c->world.scale;  // staff head (+Y, tools/staffs.py)
							}
						}
					}
				}
				if (auto* m = root->GetObjectByName("NPC R MagicNode [RMag]")) {
					return m->world.translate;
				}
			}
			return eye;
		}

		void LaunchBolt(RE::PlayerCharacter* a_player, const gen::GunDef& g, const RE::NiPoint3& a_to, const RE::NiPoint3* a_from = nullptr)
		{
			auto* raw = RE::TESDataHandler::GetSingleton()->LookupForm(g.bulletSpell, gen::kPlugin);
			auto* spell = raw ? raw->As<RE::SpellItem>() : nullptr;
			auto* proj = spell && !spell->effects.empty() && spell->effects[0] && spell->effects[0]->baseEffect ? spell->effects[0]->baseEffect->data.projectileBase : nullptr;
			if (!proj) {
				static int warned = 0;
				if (warned++ < 3) {
					logger::warn("bolt: {} has no projectile (spell {:03X} found {})", g.id, g.bulletSpell, spell != nullptr);
				}
				return;
			}
			const RE::NiPoint3 from = a_from ? *a_from : BoltOrigin(a_player, g);
			RE::NiPoint3       d = a_to - from;
			const float        flat = std::sqrt(d.x * d.x + d.y * d.y);
			RE::Projectile::ProjectileRot rot{ -std::atan2(d.z, std::max(1e-3f, flat)), std::atan2(d.x, d.y) };
			RE::Projectile::LaunchData    ld(proj, a_player, from, rot);
			ld.useOrigin = true;
			ld.autoAim = false;
			ld.spell = spell;
			ld.castingSource = RE::MagicSystem::CastingSource::kRightHand;
			RE::ProjectileHandle h;
			RE::Projectile::Launch(&h, ld);
			static int logged = 0;
			if (logged++ < 6) {
				auto pr = h.get();
				logger::info("bolt: {} proj {:08X} from ({:.0f},{:.0f},{:.0f}) to ({:.0f},{:.0f},{:.0f}) -> {}", g.id, proj->GetFormID(), from.x, from.y, from.z, a_to.x, a_to.y, a_to.z,
					pr ? fmt::format("launched {:08X} 3D {}", pr->GetFormID(), pr->Get3D() != nullptr) : std::string("no projectile"));
			}
		}

		// Where the crosshair lands (physics ray), and the eye and aim it was cast from.
		struct Aim { RE::NiPoint3 eye, dir, hit; };
		Aim AimAt(float a_range)
		{
			Aim  r;
			auto* cam = RE::PlayerCamera::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto& w = cam->cameraRoot->world;
			r.eye = w.translate;
			r.dir = { w.rotate.entry[0][1], w.rotate.entry[1][1], w.rotate.entry[2][1] };
			r.dir.Unitize();
			r.hit = r.eye + r.dir * a_range;
			if (auto* world = player->GetParentCell() ? player->GetParentCell()->GetbhkWorld() : nullptr) {
				// The ray skips your own body and staff (your collision group), and starts a step out from the
				// camera; a hit right in your face (your own capsule, the staff, a bush you stand in) is passed
				// through, so a shot never bursts at your feet.
				const float  s = RE::bhkWorld::GetWorldScale();
				RE::CFilter  self{};
				player->GetCollisionFilterInfo(self);
				float        start = 25.0f;
				for (int tries = 0; tries < 4 && start < a_range; ++tries) {
					RE::bhkPickData pick;
					pick.rayInput.from = RE::hkVector4((r.eye + r.dir * start) * s);
					pick.rayInput.to = RE::hkVector4((r.eye + r.dir * a_range) * s);
					pick.rayInput.filterInfo.SetSystemGroup(self.GetSystemGroup());
					pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
					{
						RE::BSReadLockGuard lock(world->worldLock);
						world->PickObject(pick);
					}
					if (!pick.rayOutput.HasHit()) {
						break;
					}
					const float d = start + (a_range - start) * pick.rayOutput.hitFraction;
					if (d >= 140.0f) {
						r.hit = r.eye + r.dir * d;
						break;
					}
					start = d + 15.0f;  // too close to be the target: look past it
				}
			}
			return r;
		}


		// ================================================================ spell blasts
		// Skyrim's own explosion looks (no-damage copies in our plugin) at a point: the impact of every shot and the
		// finale of every signature move. A marker is moved to the spot and the blast is placed on it.
		struct PendingBlast
		{
			RE::NiPoint3 at;
			float        delay;
			int          expl;
			float        scale;
		};
		// Skyrim's explosions are built for dragons and Dragonborn shouts: ours are shrunk so a hit reads as a hit,
		// not a screen-filling burst. Shots use the small size, signature moves the larger one.
		constexpr float kTapBlastScale = 0.45f, kBlastScale = 0.7f;
		std::vector<std::pair<RE::ObjectRefHandle, std::pair<float, float>>> g_shrink;  // explosion, (scale, seconds left)
		std::vector<PendingBlast> g_blasts;
		RE::ObjectRefHandle       g_blastMarker;

		// A no-damage copy of a Skyrim explosion, made once at runtime: same model, light, sounds and screen effect,
		// no enchantment, no damage, no knockdown (our code does the hurting).
		RE::BGSExplosion* BlastForm(int a_index)
		{
			static std::vector<RE::BGSExplosion*> made(gen::kBlasts.size(), nullptr);
			static std::vector<bool>              tried(gen::kBlasts.size(), false);
			if (a_index < 0 || a_index >= static_cast<int>(made.size())) {
				return nullptr;
			}
			if (!tried[a_index]) {
				tried[a_index] = true;
				auto* src = RE::TESForm::LookupByID<RE::BGSExplosion>(gen::kBlasts[a_index].second);
				auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSExplosion>();
				if (src && factory) {
					if (auto* ex = factory->Create()) {
						ex->boundData = src->boundData;
						ex->SetModel(src->GetModel());
						ex->data = src->data;
						ex->data.damage = 0.0f;
						ex->data.force = std::min(src->data.force, 40.0f);
						ex->data.flags.reset(RE::BGSExplosionData::Flag::kKnockDown_Always, RE::BGSExplosionData::Flag::kKnockDown_ByFormula);
						ex->data.spawnProjectile = nullptr;
						ex->formEnchanting = nullptr;
						// screen flashes only for the great storms; a shot's impact never whites out the screen
						const std::string_view nm = gen::kBlasts[a_index].first;
						const bool             big = nm.find("massive") != std::string_view::npos || nm.find("storm") != std::string_view::npos || nm == "skybolt";
						ex->imageSpaceModifying = big ? src->imageSpaceModifying : nullptr;
						made[a_index] = ex;
					}
				}
				logger::info("blast: {} ({:06X}) {}", gen::kBlasts[a_index].first, gen::kBlasts[a_index].second, made[a_index] ? "ready" : "missing");
			}
			return made[a_index];
		}

		int BlastIndex(std::string_view a_name)
		{
			for (std::size_t i = 0; i < gen::kBlasts.size(); ++i) {
				if (gen::kBlasts[i].first == a_name) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		void Blast(int a_expl, const RE::NiPoint3& a_at, float a_delay = 0.0f, float a_scale = kBlastScale)
		{
			if (a_expl >= 0 && g_blasts.size() < 32) {
				g_blasts.push_back({ a_at, a_delay, a_expl, a_scale });
			}
		}

		void Detonate(const PendingBlast& b)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* ex = BlastForm(b.expl);
			if (!player || !ex || !player->GetParentCell() || b.at.GetDistance(player->GetPosition()) > 9000.0f) {
				return;
			}
			auto m = g_blastMarker.get();
			if (!m || m->IsDeleted() || m->GetParentCell() != player->GetParentCell()) {
				if (m) {
					m->Disable();
					m->SetDelete(true);
				}
				auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);  // XMarker
				auto  r = xm ? player->PlaceObjectAtMe(xm, false) : nullptr;
				if (!r) {
					return;
				}
				g_blastMarker = r->GetHandle();
				m = r;
			}
			m->SetPosition(b.at);
			if (auto r = m->PlaceObjectAtMe(ex, false)) {
				r->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(b.scale, 0.1f, 5.0f) * 100.0f);
				g_shrink.push_back({ r->CreateRefHandle(), { b.scale, 0.6f } });
				if (g_shrink.size() > 64) {
					g_shrink.erase(g_shrink.begin());
				}
			}
		}
		// The explosion's model, once it has one, is held at the shrunken size for its first moments.
		void TickShrink(float a_delta)
		{
			for (auto& [h, v] : g_shrink) {
				v.second -= a_delta;
				if (auto r = h.get()) {
					if (auto* o = r->Get3D(); o && std::abs(o->local.scale - v.first) > 0.01f) {
						o->local.scale = v.first;
					}
				}
			}
			std::erase_if(g_shrink, [](const auto& e) { return e.second.second <= 0; });
		}

		void TickBlasts(float a_delta)
		{
			std::vector<PendingBlast> now;
			for (auto& b : g_blasts) {
				if ((b.delay -= a_delta) <= 0) {
					now.push_back(b);
				}
			}
			std::erase_if(g_blasts, [](const PendingBlast& b) { return b.delay <= 0; });
			for (const auto& b : now) {
				Detonate(b);
			}
			TickShrink(a_delta);
		}

		float BoltDelay(const gen::GunDef& g, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
		{
			float speed = g.kind == gen::kKindBow ? 5200.0f : 3000.0f;
			if (auto* raw = g.bulletSpell ? RE::TESDataHandler::GetSingleton()->LookupForm(g.bulletSpell, gen::kPlugin) : nullptr) {
				if (auto* sp = raw->As<RE::SpellItem>(); sp && !sp->effects.empty() && sp->effects[0] && sp->effects[0]->baseEffect && sp->effects[0]->baseEffect->data.projectileBase) {
					speed = std::max(500.0f, sp->effects[0]->baseEffect->data.projectileBase->data.speed);
				}
			}
			return std::min(1.5f, a_from.GetDistance(a_to) / speed);
		}

		// An elemental arrow (bows): the arrow's own projectile, launched from a point (the bow, or the sky).
		void LaunchArrow(RE::PlayerCharacter* a_player, const gen::GunDef& g, const RE::NiPoint3& a_to, const RE::NiPoint3* a_from = nullptr)
		{
			if (g.kind != gen::kKindBow) {  // (the bows became spells: their volleys fly as the spell's bolts)
				LaunchBolt(a_player, g, a_to, a_from);
				return;
			}
			auto* ammo = g.ammo ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESAmmo>(g.ammo, gen::kPlugin) : nullptr;
			auto* proj = ammo ? ammo->data.projectile : nullptr;
			if (!proj) {
				return;
			}
			const RE::NiPoint3 from = a_from ? *a_from : BoltOrigin(a_player, g);
			RE::NiPoint3       d = a_to - from;
			const float        flat = std::sqrt(d.x * d.x + d.y * d.y);
			RE::Projectile::ProjectileRot rot{ -std::atan2(d.z, std::max(1e-3f, flat)), std::atan2(d.x, d.y) };
			RE::Projectile::LaunchData    ld(proj, a_player, from, rot);
			ld.useOrigin = true;
			ld.autoAim = false;
			ld.ammoSource = ammo;
			RE::ProjectileHandle h;
			RE::Projectile::Launch(&h, ld);
		}

		// Black holes (Event Horizon): pull every undead in toward the centre, then collapse.
		struct Well
		{
			RE::NiPoint3 at;
			float        left, base, pulse;
		};
		std::vector<Well> g_wells;

		// Time Stop (Chronos Hourglass): frozen zombies take the stored blow when time starts again.
		struct Frozen
		{
			RE::ActorHandle who;
			float           left, damage;
		};
		std::vector<Frozen> g_frozen;

		// Prism Sentinel (Prismheart): a crystal hanging in the air that beams the nearest zombies.
		struct Prism
		{
			RE::NiPoint3 at;
			float        left, tick, base;
			int          gun;
		};
		std::vector<Prism> g_prisms;

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
					case gen::Special::Hex:
						if (!killed) {
							g_hexed[best->GetFormID()] = 5.0f;
						}
						break;
					case gen::Special::Plague:
						if (!killed) {
							const float dps = hitDmg * g.abilityValue / 3.0f;
							g_dots.push_back({ best->GetHandle(), dps, 3.0f });
							g_plagued[best->GetFormID()] = dps;
						}
						break;
					case gen::Special::Daze:
						if (!killed) {
							Stagger(best, 0.6f);
							if (Rand(0, 1) < g.abilityValue) {
								Director::Web(best->GetPosition(), 40.0f, 1.5f, 60.0f);
							}
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
				}
			}
			if (g.spell >= 0 && g.spellOn == 2) {
				Cast(g.spell, firstHit);  // fired spell flies at what you hit (or straight ahead)
			}
			{
				// one bolt per shot from the wand tip, and the element's blast where it lands
				const Aim          a = AimAt(std::min(g.range, 6000.0f));
				const bool         landed = firstHit || a.hit.GetDistance(a.eye) < std::min(g.range, 6000.0f) - 5.0f;
				const RE::NiPoint3 to = firstHit ? HeadPos(firstHit) - RE::NiPoint3{ 0, 0, 30 } : a.hit;
				if (g.bulletSpell) {
					LaunchBolt(player, g, to);
				}
				if (landed) {
					Blast(g.tapBlast, to, BoltDelay(g, origin, to), kTapBlastScale);
					// every bolt and arrow splashes a little where it lands: the burst weapons (Splash) hard and wide,
					// the rest a third of the hit within an arm's reach
					const bool  burst = sp == gen::Special::Splash && g.splashRadius > 0;
					const float r = burst ? g.splashRadius : (g.kind == gen::kKindBow ? 70.0f : 90.0f);
					const float sd = burst ? g.splashDamage * Perks::DamageMult() : dmg * 0.33f;
					for (auto* a : candidates) {
						if (a != firstHit && !a->IsDead() && a->GetPosition().GetDistance(to) <= r + 20.0f) {
							Damage(a, sd, false, false);
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
			if (static int said = 0; said++ < 3) {
				logger::info("gun: {} cast sound {}", g.id, static_cast<int>(g.fireSnd));
			}
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


		// ================================================================ signature moves (hold, then release)
		float g_unleashMult = 1.0f;  // Ascendant moves (Reforge level 3): 1.25 for the strike, 0.75 for its echo
		int   ForgeLevel() { return SlotOf(CurrentGun()) >= 0 ? Save::GetSlot(SlotOf(CurrentGun())).level : 0; }
		float BaseDamage(const gen::GunDef& g)
		{
			return g.damage * Perks::DamageMult() * CrewMult(0) * (1.0f + gen::kForgeStep * ForgeLevel()) * g_unleashMult;
		}

		std::vector<RE::Actor*> Foes(const RE::NiPoint3& a_at, float a_radius)
		{
			auto*                   player = RE::PlayerCharacter::GetSingleton();
			std::vector<RE::Actor*> out;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				if (auto a = h.get(); a && Targetable(a.get(), player) && a->GetPosition().GetDistance(a_at) <= a_radius) {
					out.push_back(a.get());
				}
			}
			std::sort(out.begin(), out.end(), [&](RE::Actor* x, RE::Actor* y) { return x->GetPosition().GetDistance(a_at) < y->GetPosition().GetDistance(a_at); });
			return out;
		}

		std::vector<RE::Actor*> InLine(const Aim& a, float a_range, float a_width)
		{
			auto*                   player = RE::PlayerCharacter::GetSingleton();
			std::vector<RE::Actor*> out;
			for (auto* f : Foes(a.eye, a_range)) {
				const auto hp = HeadPos(f);
				if (auto [d, t] = RaySegment(a.eye, a.dir, f->GetPosition() + RE::NiPoint3{ 0, 0, 15 }, hp); d <= a_width) {
					bool los = false;
					(void)player->HasLineOfSight(f, los);
					out.push_back(f);
				}
			}
			return out;
		}

		std::vector<RE::Actor*> InCone(const Aim& a, float a_range, float a_deg)
		{
			std::vector<RE::Actor*> out;
			for (auto* f : Foes(a.eye, a_range)) {
				if (AngleDeg(a.dir, HeadPos(f) - a.eye) <= a_deg) {
					out.push_back(f);
				}
			}
			return out;
		}

		void Bolts(RE::PlayerCharacter* p, const gen::GunDef& g, const std::vector<RE::Actor*>& a_to, int a_max)
		{
			int n = 0;
			for (auto* f : a_to) {
				if (n++ >= a_max) {
					break;
				}
				if (g.bulletSpell) {
					LaunchBolt(p, g, HeadPos(f) - RE::NiPoint3{ 0, 0, 20 });
				} else if (g.spell >= 0) {
					Cast(g.spell, f);
				}
			}
		}

		// The signature move of each weapon (sheet: weapons.chargeName/chargeText).
		void Unleash(int a_gun, bool a_echo = false)
		{
			const auto& g = gen::kGuns[a_gun];
			auto*       p = RE::PlayerCharacter::GetSingleton();
			const float base = BaseDamage(g);
			const Aim   aim = AimAt(std::min(g.range * 1.5f, 8000.0f));
			auto        hit = [&](RE::Actor* f, float a_mult) { if (!f->IsDead()) { Damage(f, base * a_mult, false, false); } };
			const std::string_view id = g.id;
			Forms::Play(g.chargeSnd);
			if (!a_echo) {
				Hud::Banner(std::string(g.chargeName) + (ForgeLevel() >= gen::kForgeMax ? "  -  ASCENDANT" : ""), 1.2f);
			}
			logger::info("charge: {} unleashes {}", g.id, g.chargeName);

			if (id == "sidearm" || id == "apprentice" || id == "marksman") {  // lances: a beam of force, everything in the line
				auto line = InLine(aim, g.range * 1.5f, 75.0f);
				for (auto* f : line) { hit(f, id == "marksman" ? 5.0f : 4.0f); Stagger(f, 0.9f); }
				Bolts(p, g, line, 6);
				LaunchBolt(p, g, aim.hit);
				// the lance itself: bursts racing down the line from the staff to where it lands
				const float len = std::min(aim.hit.GetDistance(aim.eye), g.range * 1.5f);
				int         n = 0;
				for (float t = 160.0f; t < len - 60.0f && n < 14; t += 170.0f, ++n) {
					Blast(BlastIndex("light"), aim.eye + aim.dir * t - RE::NiPoint3{ 0, 0, 25.0f }, 0.025f * n);
				}
			} else if (id == "centurion") {  // pestilence: a plague burst that spreads from body to body
				for (int k = 0; k < 5; ++k) {  // a spreading cloud of rot
					const float a = k * 1.2566f;
					Blast(g.chargeBlast, aim.hit + RE::NiPoint3{ std::cos(a) * 260.0f, std::sin(a) * 260.0f, 20.0f }, 0.15f + 0.08f * k);
				}
				for (auto* f : Foes(aim.hit, 700.0f)) {
					hit(f, 1.0f);
					g_dots.push_back({ f->GetHandle(), base * 0.8f, 6.0f });
					g_plagued[f->GetFormID()] = base * 0.8f;
				}
				Bolts(p, g, Foes(aim.hit, 700.0f), 6);
			} else if (id == "triad") {  // soul harvest: homing souls to the five nearest
				auto foes = Foes(p->GetPosition(), 1600.0f);
				if (foes.size() > 5) foes.resize(5);
				for (std::size_t i = 0; i < foes.size(); ++i) {
					hit(foes[i], 3.0f);
					Blast(g.chargeBlast, foes[i]->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.25f + 0.1f * i);  // the soul torn out
				}
				Bolts(p, g, foes, 5);
				if (!foes.empty()) {  // the harvest returns to you
					auto* av = p->AsActorValueOwner();
					av->RestoreActorValue(RE::ActorValue::kMagicka, 15.0f * foes.size());
				}
			} else if (id == "spider") {  // spore bloom around the impact
				Director::Web(aim.hit, 320.0f, 4.0f, 70.0f);
				for (auto* f : Foes(aim.hit, 320.0f)) hit(f, 2.0f);
				LaunchBolt(p, g, aim.hit);
			} else if (id == "drill") {  // blood siphon around you
				auto foes = Foes(p->GetPosition(), 450.0f);
				for (auto* f : foes) hit(f, 2.0f);
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * std::min(0.6f, 0.08f * foes.size()));
				Bolts(p, g, foes, 6);
			} else if (id == "steampump") {  // bone prison: cone root + stagger
				auto cone = InCone(aim, 700.0f, 35.0f);
				for (auto* f : cone) {
					hit(f, 4.0f);
					Stagger(f, 1.0f);
					Director::Web(f->GetPosition(), 40.0f, 3.0f, 95.0f);
				}
				Bolts(p, g, cone, 6);
			} else if (id == "animunculi") {  // doom mark: every zombie nearby hexed and doomed to explode
				for (auto* f : Foes(aim.hit, 1300.0f)) {
					g_hexed[f->GetFormID()] = 10.0f;
					g_doomed[f->GetFormID()] = base * 3.0f;
					hit(f, 0.5f);
				}
				LaunchBolt(p, g, aim.hit);
			} else if (id == "kagrenac") {  // mass frenzy: they turn on their own kind
				for (auto* f : Foes(aim.hit, 1100.0f)) {
					Director::Frenzy(f, 8.0f);
					Stagger(f, 0.5f);
					Blast(BlastIndex("paralyze"), f->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f }, 0.2f);  // their minds break
				}
				LaunchBolt(p, g, aim.hit);
			} else if (id == "mammothlmg") {  // black hole: a singularity drags them in, then collapses
				g_wells.push_back({ aim.hit, 3.0f, base, 0.0f });
				LaunchBolt(p, g, aim.hit);
			} else if (id == "longshot") {  // orbital strike: six stars fall one after another
				for (int k = 0; k < 6; ++k) {
					const float a = k * 2.1f;
					const float r = 60.0f + 45.0f * k;
					g_detonations.push_back({ aim.hit + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, base * 3.0f, 0.25f + 0.32f * k, 2, a_gun });
				}
			} else if (id == "aetherium") {  // auriel's wrath: huge sunburst
				for (auto* f : Foes(aim.hit, 450.0f)) { hit(f, 2.5f); Stagger(f, 1.0f); }
				LaunchBolt(p, g, aim.hit);
				for (int k = 0; k < 5; ++k) {  // Auriel's sky-bolts ring the sunburst
					const float a = k * 1.2566f + 0.4f;
					const RE::NiPoint3 at = aim.hit + RE::NiPoint3{ std::cos(a) * 300.0f, std::sin(a) * 300.0f, 0 };
					Blast(BlastIndex("skybolt"), at, 0.3f + 0.12f * k);
					for (auto* f : Foes(at, 200.0f)) hit(f, 1.0f);
				}
			} else if (id == "flamerepeater") {  // meteor arrow: a burning star falls where it lands
				const RE::NiPoint3 sky = aim.hit + RE::NiPoint3{ 120.0f, 80.0f, 1500.0f };
				LaunchArrow(p, g, aim.hit, &sky);
				for (auto* f : Foes(aim.hit, 460.0f)) {
					hit(f, 5.0f);
					g_dots.push_back({ f->GetHandle(), base * 0.8f, 4.0f });  // burning
					Stagger(f, 1.0f);
				}
			} else if (id == "frostlancer") {  // glacier arrow: everything near the impact freezes solid
				for (int k = 0; k < 6; ++k) {
					const float a = k * RE::NI_PI / 3.0f;
					const RE::NiPoint3 at = aim.hit + RE::NiPoint3{ std::cos(a) * 170.0f, std::sin(a) * 170.0f, 0 };
					const RE::NiPoint3 sky = at + RE::NiPoint3{ 0, 0, 1200.0f };
					LaunchArrow(p, g, at, &sky);
				}
				for (auto* f : Foes(aim.hit, 420.0f)) { hit(f, 3.0f); Director::Web(f->GetPosition(), 40.0f, 4.0f, 96.0f, true); }
			} else if (id == "stormcaller") {  // thunderstrike: lightning chains through ten
				auto foes = Foes(aim.hit, 1400.0f);
				if (foes.size() > 10) foes.resize(10);
				for (auto* f : foes) {
					hit(f, 2.5f);
					Stagger(f, 0.8f);
					const RE::NiPoint3 at = f->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f };
					const RE::NiPoint3 sky = at + RE::NiPoint3{ 30.0f, 0, 1300.0f };
					LaunchArrow(p, g, at, &sky);
				}
			} else if (id == "embermortar") {  // Gale Volley: a fan of wind arrows; every dead thing in the spray is knocked flat where it stands
				for (int k = -4; k <= 4; ++k) {
					const float yaw = k * 0.075f, c = std::cos(yaw), sn = std::sin(yaw);
					const RE::NiPoint3 d{ aim.dir.x * c - aim.dir.y * sn, aim.dir.x * sn + aim.dir.y * c, aim.dir.z };
					LaunchArrow(p, g, aim.eye + d * 2200.0f);
				}
				auto* proc = p->GetActorRuntimeData().currentProcess;
				int   n = 0;
				for (auto* f : InCone(aim, 2000.0f, 32.0f)) {
					hit(f, 3.0f);
					if (proc && !f->IsDead()) {
						// pushed from straight above: down they go on the spot, no flying across the field
						proc->KnockExplosion(f, f->GetPosition() + RE::NiPoint3{ 0, 0, 140.0f }, 1.2f);
						if (n++ < 8) {
							Blast(BlastIndex("priestsmall"), f->GetPosition() + RE::NiPoint3{ 0, 0, 20.0f }, BoltDelay(g, aim.eye, f->GetPosition()), 0.4f);
						}
					}
				}
			} else if (id == "wabbajack") {  // mass madness
				std::uniform_real_distribution<float> roll(0, 1);
				for (auto* f : Foes(aim.hit, 600.0f)) {
					if (roll(g_rng) < 0.5f) hit(f, 100.0f); else { hit(f, 1.0f); Stagger(f, 1.0f); }
				}
				Bolts(p, g, Foes(aim.hit, 600.0f), 6);
			} else if (id == "thuumcannon") {  // the full shout
				auto cone = InCone(aim, 1600.0f, 40.0f);
				for (auto* f : cone) hit(f, 1.5f);
				if (g.spell >= 0) Cast(g.spell, nullptr);
			} else if (id == "skyforgearc") {  // storm call: chain through twelve
				auto foes = Foes(aim.hit, 2000.0f);
				if (foes.size() > 12) foes.resize(12);
				for (auto* f : foes) hit(f, 1.2f);
				Bolts(p, g, foes, 8);
			}
			// ---- the city relics (rewards of the Undying Dirge) ----
			else if (id == "hearth") {  // Riverwood, Hearthfire Nova: six meteors round you, everything near you burns
				const auto pp = p->GetPosition();
				for (int k = 0; k < 6; ++k) {
					const float a = k * RE::NI_PI / 3.0f;
					g_detonations.push_back({ pp + RE::NiPoint3{ std::cos(a) * 360.0f, std::sin(a) * 360.0f, 0 }, base * 4.0f, 0.2f + 0.15f * k, 2, a_gun });
				}
				for (auto* f : Foes(pp, 700.0f)) {
					hit(f, 1.5f);
					g_dots.push_back({ f->GetHandle(), base * 0.8f, 5.0f });
				}
				Blast(BlastIndex("firestorm"), pp);
			} else if (id == "gildergreen") {  // Whiterun, Kyne's Bloom: living wood roots them, you are healed whole
				const auto pp = p->GetPosition();
				for (auto* f : Foes(pp, 900.0f)) {
					hit(f, 3.0f);
					Director::Web(f->GetPosition(), 40.0f, 5.0f, 96.0f);
				}
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth));
				Blast(BlastIndex("lightmassive"), pp);
				Bolts(p, g, Foes(pp, 900.0f), 6);
			} else if (id == "lorkhan") {  // Markarth, Tonal Stillness: time stops for them, then they shatter
				for (auto* f : Foes(p->GetPosition(), 1500.0f)) {
					Director::Web(f->GetPosition(), 40.0f, 4.0f, 100.0f);
					g_frozen.push_back({ f->GetHandle(), 4.0f, base * 5.0f });
				}
				Blast(BlastIndex("shockmass"), p->GetPosition());
			} else if (id == "arkay") {  // Falkreath, Return to the Grave: Arkay's judgement on every undead near you
				auto foes = Foes(p->GetPosition(), 1200.0f);
				for (auto* f : foes) hit(f, 6.0f);
				Bolts(p, g, foes, 8);
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.3f);
				Blast(BlastIndex("priestlarge"), p->GetPosition());
			} else if (id == "mirestaff") {  // Morthal, Drowning Mire: a bog drags them in and the plague runs through them
				g_wells.push_back({ aim.hit, 4.0f, base * 1.5f, 0.0f });
				for (auto* f : Foes(aim.hit, 900.0f)) {
					g_dots.push_back({ f->GetHandle(), base * 0.8f, 6.0f });
					g_plagued[f->GetFormID()] = base * 0.8f;
				}
				LaunchBolt(p, g, aim.hit);
			} else if (id == "magnus") {  // Winterhold, Gaze of Magnus: three crystal eyes burn them for eight seconds
				for (int k = 0; k < 3; ++k) {
					const float a = k * 2.0944f;
					g_prisms.push_back({ aim.hit + RE::NiPoint3{ std::cos(a) * 220.0f, std::sin(a) * 220.0f, 140.0f }, 8.0f, 0.1f * k, base * 1.5f, a_gun });
				}
				LaunchBolt(p, g, aim.hit);
			} else if (id == "wolfqueen") {  // Solitude, Potema's Requiem: nine arrows, and the dead they strike turn on their own
				for (int k = -4; k <= 4; ++k) {
					const float yaw = k * 0.09f, c = std::cos(yaw), sn = std::sin(yaw);
					const RE::NiPoint3 d{ aim.dir.x * c - aim.dir.y * sn, aim.dir.x * sn + aim.dir.y * c, aim.dir.z };
					LaunchArrow(p, g, aim.eye + d * 2600.0f);
				}
				for (auto* f : InCone(aim, 2600.0f, 40.0f)) {
					hit(f, 2.5f);
					Director::Frenzy(f, 10.0f);
				}
			} else if (id == "ysgramor") {  // Windhelm, Wrath of Ysgramor: ice and lightning on twelve at once
				auto foes = Foes(aim.hit, 1600.0f);
				if (foes.size() > 12) foes.resize(12);
				for (auto* f : foes) {
					hit(f, 3.5f);
					Director::Web(f->GetPosition(), 40.0f, 4.0f, 96.0f, true);
					const RE::NiPoint3 at = f->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f };
					const RE::NiPoint3 sky = at + RE::NiPoint3{ 30.0f, 0, 1300.0f };
					LaunchArrow(p, g, at, &sky);
				}
				Blast(BlastIndex("icevolley"), aim.hit);
			} else if (id == "nightingale") {  // Riften, Twilight's Eclipse: all in sight marked; each that falls takes the next
				for (auto* f : Foes(p->GetPosition(), 2200.0f)) {
					g_hexed[f->GetFormID()] = 12.0f;
					g_doomed[f->GetFormID()] = base * 4.0f;
					hit(f, 0.5f);
				}
				LaunchArrow(p, g, aim.hit);
				Blast(BlastIndex("darkmassive"), p->GetPosition());
			} else if (id == "nightmare") {  // Dawnstar, Nightmare Rain: ten burning stars from a dreaming sky
				for (int k = 0; k < 10; ++k) {
					const float a = k * 2.4f, r = 40.0f + 38.0f * k;
					g_detonations.push_back({ aim.hit + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, base * 4.0f, 0.2f + 0.22f * k, 2, a_gun });
				}
			}
		}


		void TickMarks(float a_delta)
		{
			for (auto it = g_hexed.begin(); it != g_hexed.end();) {
				it = (it->second -= a_delta) <= 0 ? g_hexed.erase(it) : std::next(it);
			}
			std::vector<Detonation> now;
			for (auto& d : g_detonations) {
				if ((d.delay -= a_delta) <= 0) {
					now.push_back(d);
				}
			}
			std::erase_if(g_detonations, [](const Detonation& d) { return d.delay <= 0; });
			auto* p = RE::PlayerCharacter::GetSingleton();
			for (auto& d : now) {
				if (d.kind == 0) {         // a doomed one goes off: the dark reaches up out of it
					Blast(BlastIndex("tentacle"), d.at);
					for (auto* f : Foes(d.at, 230.0f)) {
						Damage(f, d.damage, false, false, true);
					}
				} else if (d.kind == 1) {  // the plague jumps to the two nearest
					auto foes = Foes(d.at, 300.0f);
					for (std::size_t i = 0; i < foes.size() && i < 2; ++i) {
						if (!g_plagued.contains(foes[i]->GetFormID())) {
							g_dots.push_back({ foes[i]->GetHandle(), d.damage, 3.0f });
							g_plagued[foes[i]->GetFormID()] = d.damage;
						}
					}
				} else if (d.kind == 2 && d.gun >= 0) {  // a star falls
					const RE::NiPoint3 sky = d.at + RE::NiPoint3{ 80.0f, 40.0f, 1400.0f };
					if (gen::kGuns[d.gun].kind == gen::kKindBow) {
						LaunchArrow(p, gen::kGuns[d.gun], d.at, &sky);
					} else {
						LaunchBolt(p, gen::kGuns[d.gun], d.at, &sky);
					}
					Blast(gen::kGuns[d.gun].chargeBlast >= 0 ? gen::kGuns[d.gun].chargeBlast : BlastIndex("shockmass"), d.at, 0.2f);  // each weapon's own fall
					for (auto* f : Foes(d.at, 260.0f)) {
						Damage(f, d.damage, false, false, true);
						if (!f->IsDead()) {
							Stagger(f, 0.7f);
						}
					}
				}
			}
		}

		void TickTimeAndPrisms(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			for (auto& f : g_frozen) {
				if ((f.left -= a_delta) <= 0) {
					if (auto a = f.who.get(); a && !a->IsDead()) {
						Blast(BlastIndex("light"), a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f });
						Damage(a.get(), f.damage, false, false);
						if (!a->IsDead()) {
							Stagger(a.get(), 1.0f);
						}
					}
				}
			}
			std::erase_if(g_frozen, [](const Frozen& f) { return f.left <= 0; });
			for (auto& pr : g_prisms) {
				pr.left -= a_delta;
				if ((pr.tick -= a_delta) > 0) {
					continue;
				}
				pr.tick = 0.3f;
				Blast(BlastIndex("light"), pr.at);
				auto foes = Foes(pr.at, 1100.0f);
				if (foes.size() > 3) {
					foes.resize(3);
				}
				for (auto* f : foes) {
					LaunchBolt(p, gen::kGuns[pr.gun], HeadPos(f) - RE::NiPoint3{ 0, 0, 20.0f }, &pr.at);
					Damage(f, pr.base * 0.9f, false, false, true);
				}
			}
			std::erase_if(g_prisms, [](const Prism& pr) { return pr.left <= 0; });
		}

		void TickWells(float a_delta)
		{
			for (auto& w : g_wells) {
				w.left -= a_delta;
				w.pulse -= a_delta;
				for (auto* f : Foes(w.at, 950.0f)) {
					auto       pos = f->GetPosition();
					RE::NiPoint3 to = w.at - pos;
					to.z = 0;
					const float d = to.Length();
					if (d > 70.0f) {
						pos += to * (std::min(d - 60.0f, 340.0f * a_delta) / d);  // dragged toward the centre
						f->SetPosition(pos, true);
					}
				}
				if (w.pulse <= 0) {
					w.pulse = 0.5f;
					Blast(BlastIndex("dark"), w.at + RE::NiPoint3{ 0, 0, 60.0f });
					for (auto* f : Foes(w.at, 260.0f)) {
						Damage(f, w.base * 0.4f, false, false, true);
					}
				}
				if (w.left <= 0) {  // collapse
					Blast(BlastIndex("darkmassive"), w.at + RE::NiPoint3{ 0, 0, 60.0f });
					Forms::Play(gen::Sound::cast_heavy);
					for (auto* f : Foes(w.at, 420.0f)) {
						Damage(f, w.base * 6.0f, false, false);
						if (!f->IsDead()) {
							Stagger(f, 1.0f);
						}
					}
				}
			}
			std::erase_if(g_wells, [](const Well& w) { return w.left <= 0; });
		}

		void StartRelease(const gen::GunDef& g);
		// The finale every signature move shares: big blasts where it lands and on who it hit (or a ring around you
		// for the sweeping moves), and the element's master spell on top.
		void Finale(const gen::GunDef& g, const RE::NiPoint3& a_eye, const RE::NiPoint3& a_hit)
		{
			auto*                     p = RE::PlayerCharacter::GetSingleton();
			const bool                around = g.chargeStyle == gen::kSweep;
			const RE::NiPoint3        centre = around ? p->GetPosition() : a_hit;
			std::vector<RE::NiPoint3> spots;
			if (around) {
				for (int k = 0; k < 6; ++k) {
					const float a = k * RE::NI_PI / 3.0f;
					spots.push_back(centre + RE::NiPoint3{ std::cos(a) * 280.0f, std::sin(a) * 280.0f, 40.0f });
				}
			} else {
				spots.push_back(a_hit);
			}
			auto foes = Foes(centre, around ? 650.0f : 520.0f);
			if (foes.size() > 5) {
				foes.resize(5);
			}
			for (auto* f : foes) {
				spots.push_back(f->GetPosition() + RE::NiPoint3{ 0, 0, 60 });
			}
			for (std::size_t i = 0; i < spots.size(); ++i) {
				Blast(g.chargeBlast, spots[i], (around ? 0.0f : BoltDelay(g, a_eye, spots[i])) + 0.07f * i);
			}
			if (g.chargeSpell >= 0) {
				if (foes.empty()) {
					Cast(g.chargeSpell, nullptr);
				}
				for (std::size_t i = 0; i < foes.size() && i < 3; ++i) {
					Cast(g.chargeSpell, foes[i]);
				}
			}
		}

		int   g_echoGun = -1;
		float g_echoT = 0;
		void TickEcho(float a_delta)
		{
			if (g_echoGun < 0 || (g_echoT -= a_delta) > 0) {
				return;
			}
			const int gun = g_echoGun;
			g_echoGun = -1;
			if (gun != CurrentGun() || !GunOut()) {
				return;
			}
			g_unleashMult = 0.75f;
			Unleash(gun, true);
			g_unleashMult = 1.0f;
		}

		bool TryUnleash(bool a_released)
		{
			const int gun = CurrentGun();
			if (gun < 0 || !a_released) {
				return false;
			}
			const auto& g = gen::kGuns[gun];
			if (g_charge < g.chargeSeconds || g_chargeCd > 0) {
				return false;
			}
			if (g.mana > 0) {
				auto*       av = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
				const float cost = g.mana * g.chargeMana * Perks::ManaMult();
				if (av->GetActorValue(RE::ActorValue::kMagicka) < cost) {
					Forms::Play(g.emptySnd);
					Hud::Banner("Not enough magicka", 1.2f);
					return true;
				}
				av->DamageActorValue(RE::ActorValue::kMagicka, cost);
				g_manaQuiet = 0;
			}
			const bool ascendant = ForgeLevel() >= gen::kForgeMax;
			g_chargeCd = g.chargeCooldown * (ascendant ? 0.75f : 1.0f) * Perks::ChargeMult();
			StartRelease(g);
			g_unleashMult = ascendant ? 1.25f : 1.0f;
			Unleash(gun);
			g_unleashMult = 1.0f;
			if (ascendant) {  // the echo: the same move again, a heartbeat later
				g_echoGun = gun;
				g_echoT = 0.7f;
			}
			{
				const Aim aim = AimAt(std::min(g.range * 1.5f, 8000.0f));
				Finale(g, aim.eye, aim.hit);
			}
			return true;
		}

		// ================================================================ signature-move animation
		// The charge-up and release of every signature move have their own motion of the casting arm (and wand):
		// thrust (lances), slam (impact moves), sweep (around-you moves), twirl (wand spin), raise (call from
		// the sky) and surge (wonder blasts). Built on top of Skyrim's animation each frame, after it ran.
		float g_relT = -1.0f, g_spin = 0.0f, g_fovKick = 0.0f;
		int   g_relStyle = 0;
		bool  g_chargeSaid = false, g_readySaid = false, g_armLive = false;
		struct PoseOut
		{
			float pitch = 0, yaw = 0, roll = 0, push = 0, lift = 0, shake = 0, spinRate = 0;
		};
		PoseOut g_pose;
		float   g_jx = 0, g_jy = 0;  // this frame's tremble (rolled on the main thread)

		PoseOut Scale(const PoseOut& a, float k) { return { a.pitch * k, a.yaw * k, a.roll * k, a.push * k, a.lift * k, a.shake * k, a.spinRate * k }; }
		PoseOut Lerp(const PoseOut& a, const PoseOut& b, float t)
		{
			return { a.pitch + (b.pitch - a.pitch) * t, a.yaw + (b.yaw - a.yaw) * t, a.roll + (b.roll - a.roll) * t, a.push + (b.push - a.push) * t,
				a.lift + (b.lift - a.lift) * t, a.shake + (b.shake - a.shake) * t, a.spinRate + (b.spinRate - a.spinRate) * t };
		}
		float EaseOut(float x) { x = std::clamp(x, 0.0f, 1.0f); return 1 - (1 - x) * (1 - x) * (1 - x); }
		float EaseInOut(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }

		//                                     pitch  yaw   roll   push  lift  shake  spin
		constexpr PoseOut kChargePose[6] = { { 0.10f, 0.05f, 0.35f, -12, 0, 0.010f, 0 },     // thrust: drawn back, twisted
			{ 0.75f, -0.10f, -0.25f, -4, 6, 0.012f, 0 },                                          // slam: raised high
			{ 0.15f, -0.65f, 0.45f, -3, 0, 0.010f, 0 },                                           // sweep: wound up to the right
			{ 0.40f, 0.0f, 0.0f, 0, 4, 0.006f, 16.0f },                                           // twirl: raised, spinning
			{ 1.05f, 0.0f, 0.0f, 6, 10, 0.016f, 0 },                                              // raise: up to the sky
			{ -0.05f, 0.0f, 0.0f, -14, -2, 0.030f, 0 } };                                         // surge: pulled in, shaking
		constexpr PoseOut kStrikePose[6] = { { 0.0f, 0.0f, 0.0f, 26, 0, 0.004f, 0 },
			{ -0.45f, 0.10f, 0.15f, 8, -4, 0.010f, 0 },
			{ 0.10f, 0.90f, -0.40f, 4, 0, 0.004f, 0 },
			{ 0.10f, 0.0f, 0.0f, 10, 0, 0.004f, 34.0f },
			{ -0.30f, 0.0f, 0.0f, 10, -6, 0.020f, 0 },
			{ -0.12f, 0.0f, 0.0f, 30, 0, 0.050f, 0 } };
		// Staffs keep Skyrim's staff stance: the charge only draws the staff in a little and lifts its head, with the
		// power trembling through it; the release drives it forward (no spinning, no hand-casting gestures).
		// Charge: the staff drawn back and raised, its head lifted, the power trembling through it. Release: driven
		// forward, the head dipping toward the target. Tap shot: a short jab.
		constexpr PoseOut kStaffCharge{ 0.38f, 0.0f, 0.12f, -16, 9, 0.010f, 0 };
		constexpr PoseOut kStaffStrike{ -0.22f, 0.0f, -0.05f, 34, -2, 0.008f, 0 };
		constexpr PoseOut kStaffTap{ -0.12f, 0.0f, 0.0f, 20, 0, 0.0f, 0 };
		// Bows: drawn in toward the cheek as the shot charges, a sharp kick when the arrow leaves.
		constexpr PoseOut kBowDraw{ 0.06f, 0.04f, 0.0f, -7, 2, 0.006f, 0 };
		constexpr PoseOut kBowLoose{ 0.16f, -0.05f, 0.0f, -12, 4, 0.0f, 0 };
		constexpr float kFovKick[6] = { -9.0f, 7.0f, 5.0f, 4.0f, 6.0f, 12.0f };

		// While charging, the element's storm wraps the caster (Skyrim's fire storm / blizzard / lightning storm cast look).
		int g_aura = -1;
		void SetAura(int a_spell)
		{
			if (a_spell == g_aura) {
				return;
			}
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (g_aura >= 0) {
				if (auto* sp = Forms::Spell(g_aura) ? Forms::Spell(g_aura)->As<RE::SpellItem>() : nullptr) {
					p->RemoveSpell(sp);
				}
			}
			g_aura = a_spell;
			if (a_spell >= 0) {
				if (auto* sp = Forms::Spell(a_spell) ? Forms::Spell(a_spell)->As<RE::SpellItem>() : nullptr) {
					p->AddSpell(sp);
				}
			}
		}

		void StartRelease(const gen::GunDef& g)
		{
			g_relT = 0.0f;
			g_relStyle = std::clamp(g.chargeStyle, 0, 5);
			g_fovKick = kFovKick[g_relStyle];
		}

		void TickPose(float a_delta)
		{
			const int gun = CurrentGun();
			PoseOut   out;
			if (gun >= 0 && GunOut()) {
				const auto& g = gen::kGuns[gun];
				const int   style = std::clamp(g.chargeStyle, 0, 5);
				const bool  bow = g.kind == gen::kKindBow;  // the draw is the animation
				const bool  charging = g_trigger && g.chargeSeconds > 0 && g_chargeCd <= 0 && g_charge > 0.18f;
				const float c = charging ? EaseInOut((g_charge - 0.18f) / std::max(0.1f, g.chargeSeconds - 0.18f)) : 0.0f;
				if (charging && !g_chargeSaid) {
					Forms::Play(gen::Sound::charge_up);
					g_chargeSaid = true;
				}
				if (charging && c >= 1.0f && !g_readySaid) {
					Forms::Play(gen::Sound::charge_ready);
					g_readySaid = true;
				}
				if (!g_trigger) {
					g_chargeSaid = g_readySaid = false;
				}
				SetAura(charging ? g.chargeAura : -1);
				const bool staff = g.kind == gen::kKindWand;
				const bool spell = IsSpell(gun);  // hand spells: Skyrim's own casting animations, nothing laid on top
				out = spell ? PoseOut{} : bow ? Scale(kBowDraw, c) : staff ? Scale(kStaffCharge, c) : Scale(kChargePose[style], c);
				if (spell) {
					g_relT = -1.0f;
				}
				out.shake *= 0.5f + c;  // trembles harder as it fills
				if (g_relT >= 0 && bow) {
					g_relT = -1.0f;
					g_tapT = 0.0f;  // the loose
				}
				if ((staff || bow) && g_tapT >= 0 && g_relT < 0 && !charging) {
					const float u = g_tapT / 0.32f;   // in fast, settle back
					const PoseOut& tap = bow ? kBowLoose : kStaffTap;
					out = u < 0.25f ? Lerp(PoseOut{}, tap, EaseOut(u / 0.25f)) : Lerp(tap, PoseOut{}, EaseInOut((u - 0.25f) / 0.75f));
					if ((g_tapT += a_delta) >= 0.32f) {
						g_tapT = -1.0f;
					}
				}
				if (g_relT >= 0) {
					g_tapT = -1.0f;
					const int   rs = g_relStyle;
					const float dur = rs == gen::kRaise ? 0.75f : 0.55f;
					float       u = g_relT / dur;
					if (rs == gen::kRaise) {  // hold up high for a beat, then bring it down
						u = u < 0.25f ? 0.0f : (u - 0.25f) / 0.75f;
					}
					constexpr float k = 0.22f;
					const PoseOut& from = staff ? kStaffCharge : kChargePose[rs];
					const PoseOut& to = staff ? kStaffStrike : kStrikePose[rs];
					out = u < k ? Lerp(from, to, EaseOut(u / k)) : Lerp(to, PoseOut{}, EaseInOut((u - k) / (1 - k)));
					if ((g_relT += a_delta) >= dur) {
						g_relT = -1.0f;
					}
				}
			} else {
				g_relT = -1.0f;
				SetAura(-1);
			}
			g_spin += out.spinRate * a_delta;
			if (out.spinRate <= 0.01f) {  // settle the spin back to rest the short way
				const float rest = std::round(g_spin / (2 * RE::NI_PI)) * 2 * RE::NI_PI;
				g_spin += (rest - g_spin) * std::min(1.0f, a_delta * 10.0f);
			}
			g_fovKick += (0.0f - g_fovKick) * std::min(1.0f, a_delta * 6.0f);
			g_jx = out.shake > 0 ? Rand(-out.shake, out.shake) : 0.0f;
			g_jy = out.shake > 0 ? Rand(-out.shake, out.shake) : 0.0f;
			g_pose = out;
		}

		RE::NiMatrix3 AxisAngle(RE::NiPoint3 a, float t)
		{
			a.Unitize();
			const float c = std::cos(t), s = std::sin(t), C = 1 - c;
			RE::NiMatrix3 m;
			m.entry[0][0] = c + a.x * a.x * C, m.entry[0][1] = a.x * a.y * C - a.z * s, m.entry[0][2] = a.x * a.z * C + a.y * s;
			m.entry[1][0] = a.y * a.x * C + a.z * s, m.entry[1][1] = c + a.y * a.y * C, m.entry[1][2] = a.y * a.z * C - a.x * s;
			m.entry[2][0] = a.z * a.x * C - a.y * s, m.entry[2][1] = a.z * a.y * C + a.x * s, m.entry[2][2] = c + a.z * a.z * C;
			return m;
		}

		// One per skeleton (first person, third person): the arm as the animation left it, and as we wrote it.
		struct ArmState
		{
			RE::NiTransform orig, written;
			RE::NiAVObject* bone = nullptr;
		};
		ArmState g_arm1st, g_arm3rd;
		void PoseArm(RE::PlayerCharacter* a_player)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam) {
				return;
			}
			const bool first = cam->IsInFirstPerson();
			auto*      root = a_player->Get3D(first);
			if (!root) {
				return;
			}
			ArmState& st = first ? g_arm1st : g_arm3rd;
			auto*     bone = root->GetObjectByName("NPC R UpperArm [RUar]");
			if (!bone || !bone->parent) {
				return;
			}
			if (bone == st.bone && std::memcmp(&bone->local, &st.written, sizeof(RE::NiTransform)) == 0) {
				bone->local = st.orig;  // the animation did not rewrite it this frame: start from the clean pose
			}
			st.bone = bone;
			PoseOut o = g_pose;
			if (!first) {  // the third-person body is seen from outside: the same motion, a little broader
				o.pitch *= 1.25f, o.yaw *= 1.25f, o.roll *= 1.25f, o.push *= 0.8f, o.lift *= 0.8f;
			}
			if (std::abs(o.pitch) + std::abs(o.yaw) + std::abs(o.roll) + std::abs(o.push) + std::abs(o.lift) + o.shake < 1e-3f) {
				st.bone = nullptr;
				return;
			}
			const float jx = g_jx, jy = g_jy;
			const float pitch = a_player->data.angle.x, yaw = a_player->data.angle.z;
			const RE::NiPoint3 f{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
			const RE::NiPoint3 u{ std::sin(yaw) * std::sin(pitch), std::cos(yaw) * std::sin(pitch), std::cos(pitch) };
			const RE::NiPoint3 r{ std::cos(yaw), -std::sin(yaw), 0 };
			const RE::NiMatrix3 D = AxisAngle(u, o.yaw + jx) * AxisAngle(r, o.pitch + jy) * AxisAngle(f, o.roll);
			const RE::NiMatrix3 P = bone->parent->world.rotate;
			const RE::NiMatrix3 Pt = P.Transpose();
			st.orig = bone->local;
			bone->local.rotate = Pt * D * P * bone->local.rotate;
			bone->local.translate += Pt * (f * o.push + u * o.lift) * (1.0f / std::max(0.01f, bone->parent->world.scale));
			st.written = bone->local;
			RE::NiUpdateData ud;
			ud.time = 0.0f;
			bone->UpdateDownwardPass(ud, 0);
			if (!g_armLive) {
				g_armLive = true;
				logger::info("pose: arm animation live ({} person)", first ? "first" : "third");
			}
		}

		struct PoseHook
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				try {
					PoseArm(a_this);
				} catch (...) {
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

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
		void  FxBlast(std::string_view a_name, const RE::NiPoint3& a_at, float a_delay) { Blast(BlastIndex(a_name), a_at, a_delay, 0.6f); }
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
			// The four tomes became the elemental bows: a slot that held one gets the bow (and its arrows).
			for (int s = 0; s < 3; ++s) {
				const int gun = Save::GetSlot(s).gun;
				if (gun >= 0 && gen::kGuns[gun].kind == gen::kKindBow) {
					if (auto* w = Forms::Gun(gun)) {
						const auto counts = p->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
						if (counts.empty()) {
							p->AddObjectToContainer(w, nullptr, 1, nullptr);
							TopUpArrows(gun, false);
							++removed;
						}
					}
				}
			}
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (auto* sp = gen::kGuns[i].retired ? Forms::GunSpell(i) : nullptr; sp && p->HasSpell(sp)) {
					p->RemoveSpell(sp);
					++removed;
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
			if (s.level >= gen::kForgeMax) {
				Hud::Banner(std::string(gen::kGuns[gun].label) + " ASCENDED  |||  " + std::string(gen::kGuns[gun].chargeName) + " now strikes twice", 5.0f);
			} else {
				Hud::Banner(std::string(gen::kGuns[gun].label) + " reforged  " + std::string(s.level, '|') + "  (+" + std::to_string(static_cast<int>(gen::kForgeStep * 100 * s.level)) + "% damage)", 4.0f);
			}
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
			const bool pad = dev == RE::INPUT_DEVICE::kGamepad;
			if ((dev == RE::INPUT_DEVICE::kKeyboard && code == gen::kKey2_swap) || (pad && code == kPadRight && (GunOut() || Save::GetSlot(0).gun >= 0))) {
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
			if (((dev == RE::INPUT_DEVICE::kKeyboard && code == gen::kKnife.key) || (pad && code == kPadDown && GunOut())) && down) {
				Knife();
				return true;
			}
			if (!GunOut()) {
				g_trigger = g_adsHeld = false;
				return false;
			}
			if (IsFireButton(a_event)) {
				// One or the other, on release: a full charge unleashes the signature move, anything shorter is the
				// weapon's normal shot (never both).
				const bool spell = IsSpell(CurrentGun());
				if (down) {
					g_charge = 0;
					if (spell) {  // the right hand comes up with the spell
						SendAttack(false, 1.0f, 0.0f);
						g_dual = false;
					}
				}
				if (up && g_trigger && spell) {  // both hands let go together: the cast (a dual cast after a charge)
					SendAttack(false, 0.0f, std::max(0.05f, g_charge));
					if (g_dual) {
						SendAttack(true, 0.0f, std::max(0.05f, g_charge));
					}
				}
				if (up && g_trigger) {
					const bool bow = gen::kGuns[CurrentGun()].kind == gen::kKindBow;
					const float cdBefore = g_cooldown;
					if (TryUnleash(true)) {
						g_swingWanted = IsStaff(CurrentGun());
					} else {
						if (!bow) {
							TryFire(true);
							g_swingWanted = IsStaff(CurrentGun()) && g_cooldown > cdBefore;  // a shot actually left
						} else if (g_charge >= 0.5f) {
							TryFire(true);  // a quick loose (Skyrim's own arrow flies; the hit and its splash are ours)
						}
					}
					g_charge = 0;
				}
				g_trigger = !up;
				return true;
			}
			if (IsAimButton(a_event)) {
				g_adsHeld = !up;
				return true;
			}
			return false;
		}

		// Night magic: magicka comes back fast once you stop casting (Quicksilver doubles it).
		void TickMana(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) {
				return;
			}
			auto* av = p->AsActorValueOwner();
			// Magicka and stamina both come back fast once you stop spending them (Quicksilver doubles magicka).
			g_manaQuiet += a_delta;
			if (g_manaQuiet >= gen::kManaRegenDelay) {
				const float max = av->GetPermanentActorValue(RE::ActorValue::kMagicka), cur = av->GetActorValue(RE::ActorValue::kMagicka);
				if (cur < max) {
					av->RestoreActorValue(RE::ActorValue::kMagicka, std::min(max - cur, max * gen::kManaRegenPerSecond / Perks::ManaMult() * a_delta));
				}
			}
			static float lastSp = -1, spQuiet = 0;
			const float  spMax = av->GetPermanentActorValue(RE::ActorValue::kStamina), sp = av->GetActorValue(RE::ActorValue::kStamina);
			spQuiet = (lastSp >= 0 && sp < lastSp - 0.01f) || p->AsActorState()->IsSprinting() ? 0 : spQuiet + a_delta;
			if (spQuiet >= gen::kManaRegenDelay && sp < spMax) {
				av->RestoreActorValue(RE::ActorValue::kStamina, std::min(spMax - sp, spMax * gen::kManaRegenPerSecond * a_delta));
			}
			lastSp = av->GetActorValue(RE::ActorValue::kStamina);
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
				FireButton::func = ab.write_vfunc(0x4, FireButton::thunk);
				REL::Relocation<std::uintptr_t> pc{ RE::VTABLE_PlayerCharacter[0] };
				PoseHook::func = pc.write_vfunc(0x7D, PoseHook::thunk);
				REL::Relocation<std::uintptr_t> rw{ RE::VTABLE_ReadyWeaponHandler[0] };
				ReadyBlock::func = rw.write_vfunc(0x1, ReadyBlock::thunk);
				REL::Relocation<std::uintptr_t> fv{ RE::VTABLE_FavoritesHandler[0] };
				FavBlock::func = fv.write_vfunc(0x1, FavBlock::thunk);
				hooked = true;
				logger::info("gun: attack/ready input hooks installed");
				for (const auto& gg : gen::kGuns) {  // a charge aura saved mid-charge never sticks
					if (auto* m = gg.chargeAura >= 0 ? Forms::Spell(gg.chargeAura) : nullptr) {
						if (auto* sp = m->As<RE::SpellItem>()) {
							RE::PlayerCharacter::GetSingleton()->RemoveSpell(sp);
						}
					}
				}
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
				if (GunOut()) {
					TopUpArrows(CurrentGun(), false);
				}

			}
			const bool out = GunOut();
			const int  gun = CurrentGun();
			g_chargeCd = std::max(0.0f, g_chargeCd - a_delta);
			g_charge = out && g_trigger ? g_charge + a_delta / Perks::ChargeMult() : 0.0f;
			if (out && g_trigger && !g_dual && g_charge > 0.3f && IsSpell(CurrentGun()) && g_chargeCd <= 0) {
				SendAttack(true, 1.0f, 0.0f);  // held on: the other hand joins, the power gathers in both
				g_dual = true;
			}
			TickPose(a_delta);
			TickSwing(a_delta);
			TickEcho(a_delta);
			TickBlasts(a_delta);
			TickWells(a_delta);
			TickTimeAndPrisms(a_delta);
			TickMarks(a_delta);
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
				rd.worldFOV = gen::kWorldFov + (gen::kAdsFov - gen::kWorldFov) * g_ads + g_fovKick;
				rd.firstPersonFOV = gen::kViewmodelFov + (gen::kAdsViewmodelFov - gen::kViewmodelFov) * g_ads;
			}
			AimViewmodel(RE::PlayerCharacter::GetSingleton(), 0.0f);  // staffs and bows stay in Skyrim's own pose; aiming is the zoom
			// (staffs never spin: the twirl was a wand thing)
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
				{
					std::string ab(gen::kGuns[gun].abilityText);
					if (!ab.empty() && ab.back() == '.') {
						ab.pop_back();
					}
					h.gunElement = ab + "   \xC2\xB7   Hold: " + std::string(gen::kGuns[gun].chargeName);
				}
				h.gunColor = gen::kGuns[gun].color;
				const auto& gg = gen::kGuns[gun];
				h.charge = g_charge > 0.2f ? std::min(1.0f, g_charge / gg.chargeSeconds) : 0.0f;
				h.chargeCd = gg.chargeCooldown > 0 ? g_chargeCd / gg.chargeCooldown : 0.0f;
				h.chargeName = std::string(gg.chargeName) + (ForgeLevel() >= gen::kForgeMax ? " ASCENDANT" : "");
			}
			h.points = Points::Get();
			h.perks = Save::PerkBits();
			h.slotCount = SlotCount();
			for (int i = 0; i < 3; ++i) {
				const auto sl = i < h.slotCount ? Save::GetSlot(i) : Save::Slot{};
				h.slotGun[i] = i < h.slotCount && sl.gun >= 0 && sl.gun < static_cast<int>(gen::kGuns.size()) ? sl.gun : -1;
				h.slotLevel[i] = sl.level;
			}
			h.gunIndex = out ? gun : -1;
			h.activeSlot = out && SlotOf(gun) >= 0 ? SlotOf(gun) : (h.slotCount > 0 ? Save::ActiveSlot() % h.slotCount : -1);
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
