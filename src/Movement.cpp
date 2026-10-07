#include "BO3Z.h"

// BO3 movement on Skyrim's controller (sheet: movement): faster sprint, a slide (sprint + crouch), slower
// while aiming down sights. Applied as a temporary SpeedMult change; CarryWeight is nudged so the game
// picks the new speed up at once.
namespace bo3z
{
	namespace
	{
		float g_applied = 0;
		float g_slide = 0;
		bool  g_wasSneaking = false;

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
	}

	namespace Movement
	{
		void Reset() { g_applied = 0, g_slide = 0; }

		void Tick(float a_delta)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			auto*      st = player->AsActorState();
			const bool sprint = st->IsSprinting(), sneak = st->IsSneaking();
			if (sneak && !g_wasSneaking && (sprint || g_applied >= gen::kSprintBoost)) {
				g_slide = gen::kSlideSeconds;  // started crouching while sprinting: slide
			}
			g_wasSneaking = sneak;
			g_slide = std::max(0.0f, g_slide - a_delta);
			float want = 0;
			if (g_slide > 0) {
				want = gen::kSlideBoost * (g_slide / gen::kSlideSeconds);
			} else if (sprint) {
				want = gen::kSprintBoost;
			}
			want += gen::kAdsSpeed * Gun::AdsBlend();
			Apply(want);
		}
	}
}
