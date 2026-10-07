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
		void OnButton(const RE::ButtonEvent* a_event)
		{
			if (!a_event || !a_event->IsDown()) {
				return;
			}
			auto*       ue = RE::UserEvents::GetSingleton();
			auto*       player = RE::PlayerCharacter::GetSingleton();
			const auto& ev = a_event->QUserEvent();
			if (!ue || !player) {
				return;
			}
			if (ev == ue->sneak && (player->AsActorState()->IsSprinting() || g_applied >= gen::kRunBoost + gen::kSprintBoost * 0.9f)) {
				g_wantSlide = true;
			} else if (ev == ue->jump && g_slide > 0) {
				g_wantJumpCarry = true;
			}
		}

		void Reset()
		{
			g_applied = 0, g_slide = 0, g_carry = 0, g_air = 0;
			g_wasAir = false, g_wantSlide = false, g_wantJumpCarry = false, g_forcedSneak = false;
		}

		void Tick(float a_delta)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_delta <= 0) {
				return;
			}
			TuneGameSettings();
			auto*      st = player->AsActorState();
			auto*      cc = player->GetCharController();
			const bool sprint = st->IsSprinting();
			const bool air = cc && cc->context.currentState != RE::hkpCharacterStateType::kOnGround;
			g_air = air ? g_air + a_delta : 0;

			if (g_wantSlide) {
				g_wantSlide = false;
				if (!air) {
					g_slide = gen::kSlideSeconds;
					g_slideDir = Heading(player);
					if (auto w = WishDir(player); w.x != 0 || w.y != 0) {
						g_slideDir = w;
					}
					if (!st->IsSneaking()) {
						// Drop into a crouch for the slide: state flag + the behaviour graph's own sneak events.
						st->actorState1.sneaking = 1;
						player->SetGraphVariableBool("IsSneaking", true);
						player->NotifyAnimationGraph("SneakStart");
						g_forcedSneak = true;
					}
					Forms::Play(gen::Sound::knife);  // whoosh
				}
			}
			if (g_slide > 0) {
				g_carry = (gen::kSlideSpeed * std::pow(g_slide / gen::kSlideSeconds, 0.6f)) * gen::kSlideJump;
			}

			if (cc) {
				RE::NiPoint3 v;
				// Slide-jump: once we are actually off the ground, keep the slide's speed (horizontal only).
				if (g_wantJumpCarry && air && g_air > 0.08f) {
					g_wantJumpCarry = false;
					GetVel(cc, v);
					const float h = std::sqrt(v.x * v.x + v.y * v.y);
					if (g_carry > h && v.z > 0) {
						SetVel(cc, { g_slideDir.x * g_carry, g_slideDir.y * g_carry, v.z });
					}
					g_slide = 0;
				}
				// Optional air strafing (off by default): small horizontal nudges after takeoff, never touching vertical.
				if (air && gen::kAirControl > 0 && g_air > 0.3f) {
					if (auto w = WishDir(player); w.x != 0 || w.y != 0) {
						GetVel(cc, v);
						RE::NiPoint3 h{ v.x + w.x * gen::kAirControl * a_delta, v.y + w.y * gen::kAirControl * a_delta, 0 };
						const float  before = std::sqrt(v.x * v.x + v.y * v.y), after = std::sqrt(h.x * h.x + h.y * h.y);
						const float  cap = std::max(before, gen::kAirMax);
						if (after > cap) {
							h = h * (cap / after);
						}
						SetVel(cc, { h.x, h.y, v.z });
					}
				}
			}
			if (!air && !g_wantJumpCarry && g_slide <= 0) {
				g_carry = 0;
			}
			g_wasAir = air;
			const float before = g_slide;
			g_slide = std::max(0.0f, g_slide - a_delta);
			if (before > 0 && g_slide <= 0 && g_forcedSneak) {
				st->actorState1.sneaking = 0;  // stand back up
				player->SetGraphVariableBool("IsSneaking", false);
				player->NotifyAnimationGraph("SneakStop");
				g_forcedSneak = false;
			}

			float want = gen::kRunBoost;
			if (g_slide > 0) {
				want += gen::kSlideBoost * (g_slide / gen::kSlideSeconds);
			} else if (sprint) {
				want += gen::kSprintBoost;
			}
			want += gen::kAdsSpeed * Gun::AdsBlend();
			Apply(want);
		}
	}
}
