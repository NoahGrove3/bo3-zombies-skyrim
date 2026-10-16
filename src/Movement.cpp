#include "BO3Z.h"

// Fast, smooth movement on Skyrim's controller (sheet: movement), aiming for an arena-shooter feel:
//  - quicker run and sprint (SpeedMult), cheaper sprinting, a bit more jump;
//  - slide: crouch while sprinting -> you shoot forward in the direction you were running and ease off;
//  - slide-jump: jumping out of a slide carries the speed into the air;
//  - air strafing: steer a little while airborne.
// Speed changes use a temporary SpeedMult (CarryWeight nudged so the game picks it up at once);
// the slide and air control also write the character controller's velocity directly.
namespace bo3z
{
	namespace
	{
		constexpr float kHavok = 0.0142875f;  // Skyrim units -> Havok units

		float        g_applied = 0;
		float        g_slide = 0;
		bool         g_wasSneaking = false;
		RE::NiPoint3 g_slideDir{};
		float        g_carry = 0;   // horizontal speed carried into a jump from a slide (units/s)
		bool         g_wasAir = false;
		bool         g_gmst = false;
		bool         g_wantSlide = false;   // crouch pressed while sprinting
		bool         g_wantJumpCarry = false;
		float        g_air = 0;             // seconds airborne
		bool         g_forcedSneak = false;
		float        g_smooth = 0;          // SpeedMult boost, eased toward what we want (no jolts sprint <-> run <-> strafe)
		float        g_slideSpeed = 0;      // this slide's starting speed
		RE::NiPoint3 g_groundVel{};         // last horizontal velocity on the ground (carried into jumps)
		RE::NiPoint3 g_takeoff{};           // horizontal velocity to keep through the start of a jump
		bool         g_movt = false;

		void Apply(float a_want)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || std::abs(a_want - g_applied) < 0.5f) {
				return;
			}
			auto* av = player->AsActorValueOwner();
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, a_want - g_applied);
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
			g_applied = a_want;
		}

		void TuneGameSettings()
		{
			if (g_gmst) {
				return;
			}
			g_gmst = true;
			auto* gs = RE::GameSettingCollection::GetSingleton();
			if (!gs) {
				return;
			}
			auto scale = [&](const char* a_name, float a_mult) {
				if (auto* s = gs->GetSetting(a_name)) {
					const float before = s->GetFloat();
					s->data.f = before * a_mult;
					logger::info("movement: {} {:.2f} -> {:.2f}", a_name, before, s->data.f);
				}
			};
			scale("fSprintStaminaDrainMult", gen::kSprintStamina);
			scale("fJumpHeightMin", gen::kJumpHeight);
			// no fall damage: a fall never hurts (parkour, the shrine's heights, SkyParkour's climbs)
			for (auto [name, v] : { std::pair{ "fJumpFallHeightMin", 1.0e6f }, std::pair{ "fJumpFallHeightMult", 0.0f } }) {
				if (auto* s = gs->GetSetting(name)) {
					s->data.f = v;
					logger::info("movement: {} -> {} (no fall damage)", name, v);
				}
			}
		}

		// Skyrim runs slower sideways and backwards than forwards, and turns stiffly while running: strafing
		// left/right felt like wading. The humanoid movement types get Apex-like strafe/back speeds and quicker turns.
		void TuneMovementTypes()
		{
			if (g_movt) {
				return;
			}
			g_movt = true;
			using D = RE::Movement::SPEED_DIRECTIONS;
			int n = 0;
			// Skyrim.esm: NPC_Default, Sneaking, Sprinting, 1HM, 2HM, Bow, BowDrawn, Magic, MagicCasting, Attacking, Blocking
			for (const RE::FormID id : { 0x03580D, 0x03580B, 0x034D9C, 0x069CD8, 0x069CD9, 0x069CDA, 0x03580C, 0x069CDB, 0x069CDC, 0x0A0BC2, 0x035B4C }) {
				auto* mt = RE::TESForm::LookupByID<RE::BGSMovementType>(id);
				if (!mt) {
					continue;
				}
				const std::string name(mt->movementTypeData.typeName.c_str());
				auto& sp = mt->movementTypeData.defaultData;
				for (int k = 0; k < 2; ++k) {
					const float fwd = sp.speeds[D::kForward][k];
					sp.speeds[D::kLeft][k] = std::max(sp.speeds[D::kLeft][k], fwd * gen::kStrafeSpeed);
					sp.speeds[D::kRight][k] = std::max(sp.speeds[D::kRight][k], fwd * gen::kStrafeSpeed);
					sp.speeds[D::kBack][k] = std::max(sp.speeds[D::kBack][k], fwd * gen::kBackSpeed);
				}
				sp.rotateWhileMovingRun *= gen::kTurnRate;
				logger::info("movement: {} run fwd {:.0f} side {:.0f} back {:.0f}, turn {:.2f}", name, sp.speeds[D::kForward][1], sp.speeds[D::kLeft][1], sp.speeds[D::kBack][1], sp.rotateWhileMovingRun);
				++n;
			}
			if (!n) {
				logger::warn("movement: no humanoid movement types found");
			}
		}

		RE::NiPoint3 Heading(RE::PlayerCharacter* a_player)
		{
			const float a = a_player->GetAngleZ();
			return { std::sin(a), std::cos(a), 0 };
		}

		// Camera-relative wish direction from WASD / left stick.
		RE::NiPoint3 WishDir(RE::PlayerCharacter* a_player)
		{
			auto* pc = RE::PlayerControls::GetSingleton();
			if (!pc) {
				return {};
			}
			const auto in = pc->data.moveInputVec;  // x = strafe, y = forward
			if (in.x * in.x + in.y * in.y < 0.01f) {
				return {};
			}
			const float  a = a_player->GetAngleZ();
			RE::NiPoint3 fwd{ std::sin(a), std::cos(a), 0 }, right{ std::cos(a), -std::sin(a), 0 };
			auto         d = fwd * in.y + right * in.x;
			const float  len = std::sqrt(d.x * d.x + d.y * d.y);
			return len > 0 ? d / len : RE::NiPoint3{};
		}

		bool GetVel(RE::bhkCharacterController* a_cc, RE::NiPoint3& a_out)
		{
			RE::hkVector4 v;
			a_cc->GetLinearVelocityImpl(v);
			float q[4];
			_mm_storeu_ps(q, v.quad);
			a_out = { q[0] / kHavok, q[1] / kHavok, q[2] / kHavok };
			return true;
		}
		void SetVel(RE::bhkCharacterController* a_cc, const RE::NiPoint3& a_v)
		{
			RE::hkVector4 v(a_v.x * kHavok, a_v.y * kHavok, a_v.z * kHavok, 0.0f);
			a_cc->SetLinearVelocityImpl(v);
		}
	}

	namespace Movement
	{
		// Skyrim's Paraglider: everyone carries the glider (Paragliding.esp's "Paraglider", local 0x802) - its own way
		// of getting one is a little quest; here it is in your pack. Its script sets the behavior flag the glide needs
		// when it enters your inventory; the flag is also kept set here (a load or a new body can drop it).
		void Glider(RE::PlayerCharacter* a_player, float a_delta)
		{
			static float      every = 0;
			static RE::TESBoundObject* item = nullptr;
			static int        tries = 0;
			if ((every -= a_delta) > 0) {
				return;
			}
			every = 3.0f;
			if (!item && tries < 40) {  // (looked for again until found: the first look can come before the plugins are in)
				++tries;
				if (auto* dh = RE::TESDataHandler::GetSingleton()) {
					const auto* mod = dh->LookupModByName("Paragliding.esp");
					item = dh->LookupForm<RE::TESBoundObject>(0x802, "Paragliding.esp");
					if (!item && mod) {  // by its name among the misc items of that plugin
						for (auto* m : dh->GetFormArray<RE::TESObjectMISC>()) {
							if (m && m->GetFile(0) == mod && m->GetName() && std::string_view(m->GetName()).find("aragl") != std::string_view::npos) {
								item = m;
								break;
							}
						}
					}
					if (item || tries == 1 || tries == 40) {
						logger::info("glider: Paragliding.esp {} (loaded {}, index {:X}), the glider {:08X}", item ? "found" : "not found", mod != nullptr,
							mod ? (mod->IsLight() ? 0xFE : mod->GetCompileIndex()) : 0xFF, item ? item->GetFormID() : 0);
					}
				}
			}
			if (!item || !a_player->Is3DLoaded()) {
				return;
			}
			const auto have = a_player->GetInventoryCounts([](RE::TESBoundObject& o) { return &o == item; });
			if (have.empty()) {
				a_player->AddObjectToContainer(item, nullptr, 1, nullptr);
				logger::info("glider: the paraglider put in your pack");
			}
			std::int32_t on = 0;
			if (!a_player->GetGraphVariableInt("hasparaglider", on) || on != 1) {
				a_player->SetGraphVariableInt("hasparaglider", 1);
			}
		}

		bool Riding()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p && p->IsOnMount();
		}

		void OnButton(const RE::ButtonEvent* a_event)
		{
			// TK Dodge on a controller: it only knows its keyboard key (Left Alt), so a quick tap of the pad's sprint
			// button (LB / L1) presses that key for it - a tap dodges, holding still sprints
			if (a_event && a_event->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
				static std::chrono::steady_clock::time_point down{};
				static bool held = false;
				auto* ue0 = RE::UserEvents::GetSingleton();
				if (ue0 && a_event->QUserEvent() == ue0->sprint) {
					if (a_event->IsDown()) {
						down = std::chrono::steady_clock::now(), held = true;
					} else if (a_event->IsUp() && held) {
						held = false;
						if (std::chrono::steady_clock::now() - down < std::chrono::milliseconds(220) && !Riding()) {
							DevTools::PressKey(RE::INPUT_DEVICE::kKeyboard, 0x38, 0.05f);  // (TK Dodge RE.ini DodgeHotkey = 56)
						}
					}
				}
			}
			if (!a_event || !a_event->IsDown()) {
				return;
			}
			auto*       ue = RE::UserEvents::GetSingleton();
			auto*       player = RE::PlayerCharacter::GetSingleton();
			const auto& ev = a_event->QUserEvent();
			if (!ue || !player) {
				return;
			}
			// (the slide is gone: crouch is Skyrim's crouch again)
			if (ev == ue->jump && g_slide > 0) {
				g_wantJumpCarry = true;
			}
		}

		void Reset()
		{
			g_applied = 0, g_slide = 0, g_carry = 0, g_air = 0, g_smooth = 0;
			g_wasAir = false, g_wantSlide = false, g_wantJumpCarry = false, g_forcedSneak = false;
		}

		void Tick(float a_delta)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_delta <= 0) {
				return;
			}
			TuneGameSettings();
			TuneMovementTypes();
			auto*      st = player->AsActorState();
			auto*      cc = player->GetCharController();
			const bool sprint = st->IsSprinting();
			Glider(player, a_delta);
			if (cc) {  // no fall damage at all: the fall is always measured from where you are now, so every landing is from nowhere
				// (the fall's time is left alone: Better Jumping allows a jump while it is short - zeroing it gave endless jumps)
				cc->fallStartHeight = player->GetPositionZ();
			}
			const bool air = cc && cc->context.currentState != RE::hkpCharacterStateType::kOnGround;
			g_air = air ? g_air + a_delta : 0;
			RE::NiPoint3 v{};
			if (cc) {
				GetVel(cc, v);
			}
			const RE::NiPoint3 hv{ v.x, v.y, 0 };
			const float        hs = std::sqrt(hv.x * hv.x + hv.y * hv.y);

			// ---- slide: sprint + crouch. You keep (and boost) your speed, can steer a little, and it eases off.
			if (g_wantSlide) {
				g_wantSlide = false;
				if (!air && cc) {
					g_slide = gen::kSlideSeconds;
					g_slideDir = hs > 50.0f ? hv / hs : Heading(player);
					if (auto w = WishDir(player); w.x != 0 || w.y != 0) {
						g_slideDir = w;
					}
					g_slideSpeed = std::max(gen::kSlideSpeed, hs * 1.2f);
					// the crouch key also toggles Skyrim's sneak: the slide owns that crouch and stands you up after
					st->actorState1.sneaking = 1;
					player->SetGraphVariableBool("IsSneaking", true);
					player->NotifyAnimationGraph("SneakStart");
					g_forcedSneak = true;
					Forms::Play(gen::Sound::knife);  // whoosh
				}
			}
			const bool sliding = g_slide > 0;
			if (sliding && cc && !air) {
				// steer toward the stick, slowly (about 70 degrees a second)
				if (auto w = WishDir(player); w.x != 0 || w.y != 0) {
					const float cross = g_slideDir.x * w.y - g_slideDir.y * w.x, dot = g_slideDir.x * w.x + g_slideDir.y * w.y;
					const float turn = std::clamp(std::atan2(cross, dot), -1.2f * a_delta, 1.2f * a_delta);
					const float c = std::cos(turn), sn = std::sin(turn);
					g_slideDir = { g_slideDir.x * c - g_slideDir.y * sn, g_slideDir.x * sn + g_slideDir.y * c, 0 };
				}
				// ease out: full speed first, settling to ~60% by the end (a smooth curve, no step)
				const float t = 1.0f - g_slide / gen::kSlideSeconds;
				const float speed = g_slideSpeed * (1.0f - 0.4f * t * t);
				SetVel(cc, { g_slideDir.x * speed, g_slideDir.y * speed, std::min(v.z, 0.0f) });
			}

			// ---- momentum: whatever speed you leave the ground with (run, sprint or slide), you keep in the air
			if (!air) {
				g_groundVel = sliding ? g_slideDir * (g_slideSpeed * (1.0f - 0.4f * std::pow(1.0f - g_slide / gen::kSlideSeconds, 2.0f))) : hv;
			}
			if (air && !g_wasAir) {
				g_takeoff = g_groundVel * gen::kSlideJump;
				if (sliding) {  // jumping cancels the slide (stand up in the air)
					g_slide = 0.0001f;
				}
			}
			if (cc && air) {
				const float ts = std::sqrt(g_takeoff.x * g_takeoff.x + g_takeoff.y * g_takeoff.y);
				RE::NiPoint3 h = hv;
				// Skyrim's jump drops your run speed on takeoff: the first moments of the jump keep what you had
				if (g_air < 0.25f && hs < ts * 0.97f) {
					h = g_takeoff;
				}
				// air strafing: small steering, never faster than you took off (or the sheet's cap)
				if (gen::kAirControl > 0 && g_air > 0.12f) {
					if (auto w = WishDir(player); w.x != 0 || w.y != 0) {
						h = { h.x + w.x * gen::kAirControl * a_delta, h.y + w.y * gen::kAirControl * a_delta, 0 };
						const float cap = std::max({ gen::kAirMax, ts, 300.0f }), after = std::sqrt(h.x * h.x + h.y * h.y);
						if (after > cap) {
							h = h * (cap / after);
						}
					}
				}
				if (std::abs(h.x - hv.x) + std::abs(h.y - hv.y) > 1.0f) {
					SetVel(cc, { h.x, h.y, v.z });
				}
			}
			g_wasAir = air;
			g_wantJumpCarry = false;

			const float before = g_slide;
			g_slide = std::max(0.0f, g_slide - a_delta);
			if (before > 0 && g_slide <= 0 && g_forcedSneak) {
				st->actorState1.sneaking = 0;  // stand back up
				player->SetGraphVariableBool("IsSneaking", false);
				player->NotifyAnimationGraph("SneakStop");
				g_forcedSneak = false;
			}

			// ---- run/sprint speed, eased so switching sprint <-> run <-> strafe never jolts
			float want = gen::kRunBoost + (sprint ? gen::kSprintBoost : 0.0f) + gen::kAdsSpeed * Gun::AdsBlend();
			if (g_slide > 0) {
				want += gen::kSprintBoost;  // the crouch would otherwise drag the controller back to sneak speed
			}
			g_smooth += (want - g_smooth) * std::min(1.0f, a_delta * gen::kGroundAccel * 0.5f);
			Apply(g_smooth);
		}
	}
}
