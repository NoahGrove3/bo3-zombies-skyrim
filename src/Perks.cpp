#include "BO3Z.h"

// Blessings (sheet: perks). Bought at a stand, granted at once, one at a time, lost in death; effects live in code.
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
		float ManaMult() { return 1.0f; }  // (staffs and bows run on nothing; tomes keep their plain cost)
		float ChargeMult() { return Has(gen::kPerk_quicksilver) ? gen::kPerks[gen::kPerk_quicksilver].value : 1.0f; }
		float FireDelayMult() { return Has(gen::kPerk_twinstrike) ? 0.75f : 1.0f; }
		float DamageMult() { return Has(gen::kPerk_twinstrike) ? gen::kPerks[gen::kPerk_twinstrike].value : 1.0f; }
		float HeadMult() { return Has(gen::kPerk_hawkeye) ? gen::kPerks[gen::kPerk_hawkeye].value : 1.0f; }

		// Buying a blessing at its stand applies it at once (no potion to drink). One at a time: another can only be
		// chosen once death has taken this one.
		bool Grant(int a_perk)
		{
			if (a_perk < 0 || a_perk >= static_cast<int>(gen::kPerks.size())) {
				return false;
			}
			const auto& p = gen::kPerks[a_perk];
			if (Has(a_perk)) {
				Hud::Banner("You already have " + std::string(p.label), 2.0f);
				return false;
			}
			if (Save::PerkCount() >= 1) {
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
					if (Has(i)) {
						Hud::Banner("You carry " + std::string(gen::kPerks[i].label) + ". One blessing at a time: death takes it", 3.0f);
					}
				}
				return false;
			}
			Save::SetPerk(a_perk, true);
			switch (p.effect) {
			case 0: Mod(RE::ActorValue::kHealth, p.value); break;                                     // Juggernog
			case 4: Mod(RE::ActorValue::kSpeedMult, p.value); Mod(RE::ActorValue::kStamina, 100); break;  // Stamin-Up
			default: break;
			}
			Forms::Play(gen::Sound::drink);
			Hud::Banner(std::string(p.label), 3.0f);
			logger::info("perk: {} granted", p.id);
			return true;
		}

		// Death takes every blessing (and what they added to you).
		void LoseAll()
		{
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
				if (!Has(i)) {
					continue;
				}
				const auto& p = gen::kPerks[i];
				switch (p.effect) {
				case 0: Mod(RE::ActorValue::kHealth, -p.value); break;
				case 4: Mod(RE::ActorValue::kSpeedMult, -p.value); Mod(RE::ActorValue::kStamina, -100); break;
				default: break;
				}
				Save::SetPerk(i, false);
				logger::info("perk: {} lost in death", p.id);
			}
		}

		// an old potion still in a pack (bought before blessings applied at once): drinking it grants it the same way
		void OnEffect(RE::FormID a_mgef)
		{
			const int perk = Forms::PerkIndexOfEffect(a_mgef);
			if (perk < 0) {
				return;
			}
			logger::info("perk: drank {} (owned {:09b}, count {})", gen::kPerks[perk].id, Save::PerkBits(), Save::PerkCount());
			Grant(perk);
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
			if (RE::PlayerCharacter::IsGodMode()) {
				return;  // (console god mode: the dead's hands pass through)
			}
			auto* av = player->AsActorValueOwner();
			const float jug = Has(gen::kPerk_trollshide) ? gen::kPerks[gen::kPerk_trollshide].value : 0.0f;
			const float baseHealth = std::max(50.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth) - jug);
			const bool elite = Director::IsElite(a_zombie);
			const bool boss = Director::IsBoss(a_zombie);  // his claws come in flurries: each rake takes less
			// each kind hits as hard as it is (Undead::DamageMult, carried on its attack-damage value)
			const float kindMult = boss || elite ? 1.0f : std::clamp(a_zombie->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAttackDamageMult) / std::max(0.001f, gen::kZombieWeaponDamageMult), 0.5f, 2.0f);
			av->DamageActorValue(RE::ActorValue::kHealth, baseHealth * (boss ? gen::boss::meleeShare : elite ? gen::elite::hitShare : gen::kHitDamageFraction * kindMult));
			if (elite) {  // Life Drain
				auto* zav = a_zombie->AsActorValueOwner();
				zav->RestoreActorValue(RE::ActorValue::kHealth, zav->GetPermanentActorValue(RE::ActorValue::kHealth) * gen::elite::drainShare);
			}
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().hurtTime = 1.0f;
			}
			if (Has(gen::kPerk_frostbite)) {
				Director::Web(player->GetPosition(), 350.0f, gen::kPerks[gen::kPerk_frostbite].value, 60.0f, true);  // ice spikes show it
			}
		}

		// Stormcrown: every 12th cast looses a lightning burst around you.
		void OnCast(int a_shots)
		{
			if (Has(gen::kPerk_stormcrown) && a_shots % 12 == 0) {
				Director::Shock(RE::PlayerCharacter::GetSingleton()->GetPosition(), 320.0f, gen::kPerks[gen::kPerk_stormcrown].value);
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
