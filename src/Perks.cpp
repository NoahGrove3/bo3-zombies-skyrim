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
		float ReloadMult() { return Has(gen::kPerk_quicksilver) ? gen::kPerks[gen::kPerk_quicksilver].value : 1.0f; }
		float FireDelayMult() { return Has(gen::kPerk_twinstrike) ? 0.75f : 1.0f; }
		float DamageMult() { return Has(gen::kPerk_twinstrike) ? gen::kPerks[gen::kPerk_twinstrike].value : 1.0f; }
		float HeadMult() { return Has(gen::kPerk_hawkeye) ? gen::kPerks[gen::kPerk_hawkeye].value : 1.0f; }

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
			Forms::Play(gen::Sound::drink);
			Hud::Banner(std::string(p.label), 3.0f);
			logger::info("perk: {}", p.id);
		}

		// BO3 hits: each zombie hit takes a fixed share of the player's health without Juggernog,
		// so Juggernog's extra health means more hits to go down.
		void OnZombieHit(RE::Actor* a_zombie)
		{
			static std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> last;
			const auto now = std::chrono::steady_clock::now();
			auto& t = last[a_zombie->GetFormID()];
			if (now - t < 600ms) {
				return;
			}
			t = now;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* av = player->AsActorValueOwner();
			const float jug = Has(gen::kPerk_trollshide) ? gen::kPerks[gen::kPerk_trollshide].value : 0.0f;
			const float baseHealth = std::max(50.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth) - jug);
			av->DamageActorValue(RE::ActorValue::kHealth, baseHealth * gen::kHitDamageFraction);
			if (Has(gen::kPerk_frostbite)) {
				Director::Web(player->GetPosition(), 350.0f, gen::kPerks[gen::kPerk_frostbite].value);
			}
		}

		void OnReload(float a_emptyFraction)
		{
			if (Has(gen::kPerk_stormcrown) && a_emptyFraction > 0.05f) {
				Director::Shock(RE::PlayerCharacter::GetSingleton()->GetPosition(), 120.0f + 200.0f * a_emptyFraction, gen::kPerks[gen::kPerk_stormcrown].value * a_emptyFraction);
			}
		}

		// Quick Revive (solo): when health drops near zero, get back up once.
		void Tick()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || player->IsDead() || !Has(gen::kPerk_hearthfire)) {
				return;
			}
			auto* av = player->AsActorValueOwner();
			const float max = av->GetPermanentActorValue(RE::ActorValue::kHealth);
			if (max > 0 && av->GetActorValue(RE::ActorValue::kHealth) < max * 0.1f) {
				av->RestoreActorValue(RE::ActorValue::kHealth, max);
				Save::SetPerk(gen::kPerk_hearthfire, false);
				Hud::Banner("QUICK REVIVE", 3.0f);
				logger::info("perk: quick revive used");
			}
		}
	}
}
