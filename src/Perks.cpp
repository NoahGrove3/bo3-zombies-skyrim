#include "BO3Z.h"

// Blessings (sheet: perks). Bought at the shrine, granted at once, as many of each as you can pay for (they stack),
// all lost in death; effects live in code.
namespace bo3z
{
	namespace
	{
		bool Has(int a_perk) { return Save::HasPerk(a_perk); }
		constexpr RE::ActorValue kSchoolCost[] = { RE::ActorValue::kAlterationModifier, RE::ActorValue::kConjurationModifier, RE::ActorValue::kDestructionModifier,
			RE::ActorValue::kIllusionModifier, RE::ActorValue::kRestorationModifier };
		constexpr RE::ActorValue kSchoolPower[] = { RE::ActorValue::kAlterationPowerModifier, RE::ActorValue::kConjurationPowerModifier, RE::ActorValue::kDestructionPowerModifier,
			RE::ActorValue::kIllusionPowerModifier, RE::ActorValue::kRestorationPowerModifier };
		int  N(int a_perk) { return Save::PerkStacks(a_perk); }
		// a multiplier blessing taken n times: each adds its step again (a 1.25x twice is 1.5x; a 0.8x twice is 0.6x)
		float Stack(int a_perk, float a_min = 0.2f, float a_max = 6.0f) { return std::clamp(1.0f + (gen::kPerks[a_perk].value - 1.0f) * N(a_perk), a_min, a_max); }
		void Mod(RE::ActorValue a_av, float a_by)
		{
			RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, a_av, a_by);
		}
	}

	namespace Perks
	{
		float ManaMult() { return 1.0f; }  // (staffs and bows run on nothing; tomes keep their plain cost)
		float RegenMult() { return 1.0f + 0.5f * N(gen::kPerk_hawkeye); }  // Hawkeye: clear sight, magicka back sooner
		float ChargeMult() { return Stack(gen::kPerk_quicksilver); }
		float FireDelayMult() { return std::max(0.3f, 1.0f - 0.25f * N(gen::kPerk_twinstrike)); }
		float DamageMult() { return Stack(gen::kPerk_twinstrike); }
		float HeadMult() { return Stack(gen::kPerk_hawkeye); }

		// Buying a blessing applies it at once (no potion to drink). Each can be bought again and again: they stack.
		bool Grant(int a_perk)
		{
			if (a_perk < 0 || a_perk >= static_cast<int>(gen::kPerks.size())) {
				return false;
			}
			const auto& p = gen::kPerks[a_perk];
			const int   n = N(a_perk) + 1;
			Save::SetPerkStacks(a_perk, n);
			switch (p.effect) {
			case 0: Mod(RE::ActorValue::kHealth, p.value); break;                                     // Troll's Hide: more health each time
			case 4: Mod(RE::ActorValue::kSpeedMult, p.value); Mod(RE::ActorValue::kStamina, 100); break;  // Windrunner
			case 1:                                                                                     // Quicksilver: spells cost 15% less each (to 75%)
				if (n <= 5) {
					for (auto av : kSchoolCost) Mod(av, 15.0f);
				}
				break;
			case 2: for (auto av : kSchoolPower) Mod(av, 50.0f); break;                                 // Twin Strike: spells 50% stronger each
			case 6: Mod(RE::ActorValue::kMagicka, 50.0f); break;                                         // Mammoth Back: +50 magicka each
			default: break;
			}
			Forms::Play(gen::Sound::drink);
			Hud::Banner(std::string(p.label) + (n > 1 ? "  x" + std::to_string(n) : ""), 3.0f);
			logger::info("perk: {} granted (now x{})", p.id, n);
			return true;
		}

		// Death takes every blessing (and what they added to you).
		void LoseAll()
		{
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
				const int n = N(i);
				if (n <= 0) {
					continue;
				}
				const auto& p = gen::kPerks[i];
				switch (p.effect) {
				case 0: Mod(RE::ActorValue::kHealth, -p.value * n); break;
				case 4: Mod(RE::ActorValue::kSpeedMult, -p.value * n); Mod(RE::ActorValue::kStamina, -100.0f * n); break;
				case 1: for (auto av : kSchoolCost) Mod(av, -15.0f * std::min(n, 5)); break;
				case 2: for (auto av : kSchoolPower) Mod(av, -50.0f * n); break;
				case 6: Mod(RE::ActorValue::kMagicka, -50.0f * n); break;
				default: break;
				}
				Save::SetPerkStacks(i, 0);
				logger::info("perk: {} x{} lost in death", p.id, n);
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
			const float jug = gen::kPerks[gen::kPerk_trollshide].value * N(gen::kPerk_trollshide);
			const float baseHealth = std::max(50.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth) - jug);
			const bool elite = Director::IsElite(a_zombie);
			const bool boss = Director::IsBoss(a_zombie);  // his claws come in flurries: each rake takes less
			// each kind hits as hard as it is (Undead::DamageMult, carried on its attack-damage value)
			const float kindMult = boss || elite ? 1.0f : std::clamp(a_zombie->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAttackDamageMult) / std::max(0.001f, gen::kZombieWeaponDamageMult), 0.5f, 2.0f);
			av->DamageActorValue(RE::ActorValue::kHealth, baseHealth * (boss ? gen::boss::meleeShare : elite ? gen::elite::hitShare : gen::kHitDamageFraction * kindMult) * Settings::UndeadDamageMult());
			if (elite) {  // Life Drain
				auto* zav = a_zombie->AsActorValueOwner();
				zav->RestoreActorValue(RE::ActorValue::kHealth, zav->GetPermanentActorValue(RE::ActorValue::kHealth) * gen::elite::drainShare);
			}
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().hurtTime = 1.0f;
			}
			if (Has(gen::kPerk_frostbite)) {
				Director::Web(player->GetPosition(), 350.0f + 60.0f * (N(gen::kPerk_frostbite) - 1), gen::kPerks[gen::kPerk_frostbite].value * N(gen::kPerk_frostbite), 60.0f, true);  // ice spikes show it
			}
		}

		// Stormcrown: every 12th cast looses a lightning burst around you.
		void OnCast(int a_shots)
		{
			const int n = N(gen::kPerk_stormcrown);
			if (n > 0 && a_shots % std::max(3, 12 / n) == 0) {  // more crowns: the burst comes sooner
				Director::Shock(RE::PlayerCharacter::GetSingleton()->GetPosition(), 320.0f, gen::kPerks[gen::kPerk_stormcrown].value * (1.0f + 0.25f * (n - 1)));
			}
		}

		// Quick Revive: while you carry it, a killing blow drops you to your knees instead (Skyrim's bleedout - the
		// player is held essential: src/Party.cpp), and you get straight back up at full health, once per draught.
		bool SelfRevive() { return Has(gen::kPerk_hearthfire); }
		void Tick()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || player->IsDead() || !Has(gen::kPerk_hearthfire)) {
				return;
			}
			auto* av = player->AsActorValueOwner();
			const float max = av->GetPermanentActorValue(RE::ActorValue::kHealth);
			const bool  falling = player->AsActorState()->IsBleedingOut();
			if (max > 0 && (falling || av->GetActorValue(RE::ActorValue::kHealth) < max * 0.1f)) {
				player->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery);
				av->RestoreActorValue(RE::ActorValue::kHealth, max);
				if (falling) {
					player->NotifyAnimationGraph("BleedOutStop");
					player->NotifyAnimationGraph("bleedOutStop");
				}
				Save::SetPerkStacks(gen::kPerk_hearthfire, N(gen::kPerk_hearthfire) - 1);  // one revive per draught
				Hud::Banner("THE HEARTH CALLS YOU BACK", 3.0f);
				logger::info("perk: quick revive used{}", falling ? " (from the ground)" : "");
			}
		}
	}
}
