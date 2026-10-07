#include "BO3Z.h"

// Perk-a-Colas (sheet: perks). Drinking the potion grants the perk; effects live in code.
namespace bo3z
{
	namespace
	{
		bool Has(int a_perk) { return Save::HasPerk(a_perk); }
		void Mod(RE::ActorValue a_av, float a_by)
		{
			RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, a_av, a_by);
		}
	}

	namespace Perks
	{
		float ReloadMult() { return Has(gen::kPerk_speedcola) ? gen::kPerks[gen::kPerk_speedcola].value : 1.0f; }
		float FireDelayMult() { return Has(gen::kPerk_doubletap) ? 0.75f : 1.0f; }
		float DamageMult() { return Has(gen::kPerk_doubletap) ? gen::kPerks[gen::kPerk_doubletap].value : 1.0f; }
		float HeadMult() { return Has(gen::kPerk_deadshot) ? gen::kPerks[gen::kPerk_deadshot].value : 1.0f; }

		void OnEffect(RE::FormID a_mgef)
		{
			const int perk = Forms::PerkIndexOfEffect(a_mgef);
			if (perk < 0) {
				return;
			}
			const auto& p = gen::kPerks[perk];
			if (Has(perk)) {
				Hud::Banner("You already have " + std::string(p.label), 2.0f);
				return;
			}
			if (Save::PerkCount() >= gen::kMaxPerks) {
				Hud::Banner("Perk limit reached", 2.0f);
				return;
			}
			Save::SetPerk(perk, true);
			switch (p.effect) {
			case 0: Mod(RE::ActorValue::kHealth, p.value); break;                                     // Juggernog
			case 4: Mod(RE::ActorValue::kSpeedMult, p.value); Mod(RE::ActorValue::kStamina, 100); break;  // Stamin-Up
			default: break;
			}
			Forms::Play(gen::Sound::round_end);
			Hud::Banner(std::string(p.label), 3.0f);
			logger::info("perk: {}", p.id);
		}

		// Quick Revive (solo): when health drops near zero, get back up once.
		void Tick()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || player->IsDead() || !Has(gen::kPerk_quickrevive)) {
				return;
			}
			auto* av = player->AsActorValueOwner();
			const float max = av->GetPermanentActorValue(RE::ActorValue::kHealth);
			if (max > 0 && av->GetActorValue(RE::ActorValue::kHealth) < max * 0.1f) {
				av->RestoreActorValue(RE::ActorValue::kHealth, max);
				Save::SetPerk(gen::kPerk_quickrevive, false);
				Hud::Banner("QUICK REVIVE", 3.0f);
				logger::info("perk: quick revive used");
			}
		}
	}
}
