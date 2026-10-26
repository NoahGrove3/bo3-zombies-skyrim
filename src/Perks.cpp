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
		float ManaMult() { return Echo::WearsRobes() ? 0.75f : 1.0f; }  // (Stage 5: the Robes of the First Dragonborn)  // (staffs and bows run on nothing; tomes keep their plain cost)
		float RegenMult() { return (1.0f + 0.5f * N(gen::kPerk_hawkeye)) * (Echo::WordSilenced(2) ? 1.5f : 1.0f) * (Echo::WearsRobes() ? 1.25f : 1.0f); }  // (VO silenced: the curse undone)  // Hawkeye: clear sight, magicka back sooner
		float ChargeMult() { return Stack(gen::kPerk_quicksilver); }
		float FireDelayMult() { return std::max(0.3f, 1.0f - 0.25f * N(gen::kPerk_twinstrike)); }
		float DamageMult() { return Stack(gen::kPerk_twinstrike) * (Echo::WordSilenced(0) ? 1.15f : 1.0f); }  // (SLEN silenced: flesh remade)
		float HeadMult() { return Stack(gen::kPerk_hawkeye); }

		// (Noah: "each class should get its own perks") the altar's list: the shared draughts first, then your class's three
		std::vector<int> Menu()
		{
			std::vector<int> out;
			const int        cls = Classes::Get();
			// (the altar's rework) your class's own first, then the shared draughts
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
				if (gen::kPerkClass[i] >= 0 && gen::kPerkClass[i] == cls) out.push_back(i);
			}
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
				if (gen::kPerkClass[i] < 0) out.push_back(i);
			}
			return out;
		}
		int MenuAt(int a_pos)
		{
			const auto m = Menu();
			return m.empty() ? -1 : m[std::clamp(a_pos, 0, static_cast<int>(m.size()) - 1)];
		}
		// a class perk counts only while you are of its class (a switch keeps it, asleep, until you come back or die)
		bool Live(int a_perk) { return N(a_perk) > 0 && (gen::kPerkClass[a_perk] < 0 || gen::kPerkClass[a_perk] == Classes::Get()); }
		float DrawSpeedMult() { return Live(gen::kPerk_eagleeye) ? gen::kPerks[gen::kPerk_eagleeye].value : 1.0f; }
		float SwingSpeedMult() { return Live(gen::kPerk_steelwind) ? gen::kPerks[gen::kPerk_steelwind].value : 1.0f; }
		float WardMult()
		{
			if (!Live(gen::kPerk_dawnwatch)) return 1.0f;
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* held = p ? p->GetEquippedObject(false) : nullptr;
			auto* w = held ? held->As<RE::TESObjectWEAP>() : nullptr;
			return w && (w->IsBow() || w->IsCrossbow()) ? 1.0f - gen::kPerks[gen::kPerk_dawnwatch].value : 1.0f;
		}
		bool SummonMight() { return Live(gen::kPerk_bonebinder); }
		// a kill: what your class's perks make of it (how: 0 your summon, 1 a Destruction spell, 2 bow or crossbow, 3 melee)
		void OnClassKill(int a_how, RE::Actor* a_dying)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			auto* av = p->AsActorValueOwner();
			auto give = [&](RE::ActorValue a_av, float a_share) { av->RestoreActorValue(a_av, av->GetPermanentActorValue(a_av) * a_share); };
			if (a_how == 0 && Live(gen::kPerk_soultether)) give(RE::ActorValue::kHealth, gen::kPerks[gen::kPerk_soultether].value);
			if (a_how == 1 && Live(gen::kPerk_siphon)) give(RE::ActorValue::kMagicka, gen::kPerks[gen::kPerk_siphon].value);
			if (a_how == 1 && Live(gen::kPerk_emberheart) && a_dying) {
				Gun::AreaHurt(a_dying->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f }, 320.0f, gen::kPerks[gen::kPerk_emberheart].value, "fireball");
			}
			if (a_how == 3 && Live(gen::kPerk_spellbind)) give(RE::ActorValue::kMagicka, gen::kPerks[gen::kPerk_spellbind].value);
		}
		// a melee blow you landed on one of the dead (the Spellsword's Bladesong)
		void OnMeleeHit()
		{
			if (!Live(gen::kPerk_bladesong)) return;
			static auto last = std::chrono::steady_clock::now() - 1s;
			if (std::chrono::steady_clock::now() - last < 250ms) return;  // (one heal a swing, however many it cuts)
			last = std::chrono::steady_clock::now();
			auto* av = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
			av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * gen::kPerks[gen::kPerk_bladesong].value);
		}

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
			case 14: Mod(RE::ActorValue::kDestructionModifier, p.value); break;                          // Arcanist's Focus: Destruction costs less
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
				case 14: Mod(RE::ActorValue::kDestructionModifier, -p.value * n); break;
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
		std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_zombieHitAt;
		float SinceZombieHit(RE::FormID a_zombie)
		{
			auto it = g_zombieHitAt.find(a_zombie);
			return it == g_zombieHitAt.end() ? 1e9f : std::chrono::duration<float>(std::chrono::steady_clock::now() - it->second).count();
		}
		void OnZombieHit(RE::Actor* a_zombie)
		{
			auto& last = g_zombieHitAt;
			const auto now = std::chrono::steady_clock::now();
			auto& t = last[a_zombie->GetFormID()];
			if (now - t < 600ms) {
				return;
			}
			t = now;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (static int said = 0; said++ < 12 || said % 25 == 0) {  // (Noah: "I don't take damage from the Curse's enemies") every hit of theirs, traced
				logger::info("hurt: {} hits you (god {}, difficulty x{:.2f}, undead damage x{:.2f})", a_zombie->GetName(), RE::PlayerCharacter::IsGodMode(), Difficulty::Taken(), Settings::UndeadDamageMult());
			}
			if (RE::PlayerCharacter::IsGodMode()) {
				return;  // (console god mode: the dead's hands pass through)
			}
			// (Noah: "it feels really buggy ... make it fluid and fun") a mob's claws no longer land all in the same instant:
			// after a hit, the next one from anyone counts only once 0.45 s have passed (time to react, BO3's own rhythm)
			// (Noah: "enemies can still hit me when doing an assassination animation with melee weapons") untouchable while
			// a kill move plays, and for half a second after it
			static auto killMoveEnd = std::chrono::steady_clock::time_point{};
			if (player->IsInKillMove()) {
				killMoveEnd = now + 500ms;
				return;
			}
			if (now < killMoveEnd) {
				return;
			}
			static auto anyHit = std::chrono::steady_clock::time_point{};
			if (now - anyHit < 450ms) {
				return;
			}
			anyHit = now;
			auto* av = player->AsActorValueOwner();
			const float jug = gen::kPerks[gen::kPerk_trollshide].value * N(gen::kPerk_trollshide);
			const float baseHealth = std::max(50.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth) - jug);
			const bool elite = Director::IsElite(a_zombie);
			const bool boss = Director::IsBoss(a_zombie);  // his claws come in flurries: each rake takes less
			// each kind hits as hard as it is (Undead::DamageMult, carried on its attack-damage value)
			const float kindMult = boss || elite ? 1.0f : std::clamp(a_zombie->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAttackDamageMult) / std::max(0.001f, gen::kZombieWeaponDamageMult), 0.5f, 2.0f);
			// the first nights are gentler (a new character learns the dead): a quarter less on night 1, full by night 9
			const float early = std::clamp(0.75f + 0.03125f * static_cast<float>(Save::Get(gen::kAV_round)), 0.75f, 1.0f);
			DamagePlayer(baseHealth * (boss ? gen::boss::meleeShare : elite ? gen::elite::hitShare : gen::kHitDamageFraction * kindMult * early) * Settings::UndeadDamageMult());
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
		// (after a revive: a few seconds when you cannot fall again, so you are surely back on your feet)
		float g_grace = 0;
		std::chrono::steady_clock::time_point g_dodgeUntil{};  // a dodge's i-frames
		std::chrono::steady_clock::time_point g_last{};
		bool SelfRevive() { return Has(gen::kPerk_hearthfire) || g_grace > 0; }
		void Stand(RE::PlayerCharacter* a_player)
		{
			auto* st = a_player->AsActorState();
			a_player->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery);
			if (st->GetLifeState() == RE::ACTOR_LIFE_STATE::kEssentialDown || st->GetLifeState() == RE::ACTOR_LIFE_STATE::kBleedout) {
				st->actorState1.lifeState = RE::ACTOR_LIFE_STATE::kAlive;
			}
			a_player->NotifyAnimationGraph("BleedOutStop");
			a_player->NotifyAnimationGraph("bleedOutStop");
			a_player->NotifyAnimationGraph("IdleForceDefaultState");
		}
		// your own spells never hurt you: a harmful effect you cast that lands on yourself is lifted at once, and a hit
		// from your own blast gives back what it took (src/Events.cpp HitWatch calls SelfHit)
		float g_lastHp = -1;
		void GuardSelf(RE::PlayerCharacter* a_player)
		{
			if (auto* list = a_player->AsMagicTarget()->GetActiveEffectList()) {
				for (auto* ae : *list) {
					if (!ae || ae->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
						continue;
					}
					const auto* base = ae->GetBaseObject();
					if (!base || !(base->IsHostile() || base->IsDetrimental())) {
						continue;
					}
					// (Noah: burning, frozen, shocked corpses still hurt me) a harmful effect on you from yourself, from no
					// one (a lingering field), or from the dead is lifted the moment it lands
					auto c = ae->caster.get();
					if (!c || c.get() == a_player || c->IsDead()) {
						ae->Dispel(true);
					}
				}
			}
			g_lastHp = a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
		}
		void SelfHit()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || p->IsDead() || g_lastHp < 0) {
				return;
			}
			auto* av = p->AsActorValueOwner();
			if (const float hp = av->GetActorValue(RE::ActorValue::kHealth); hp < g_lastHp) {
				av->RestoreActorValue(RE::ActorValue::kHealth, g_lastHp - hp);
			}
		}
		// (Noah: after a revive the first-person hands were gone and spells wouldn't cast until re-equipped) a moment after
		// standing, the camera is flicked back to first person and what was in each hand is taken off and put back
		float          g_rearm = -1;
		bool           g_rearmLoad = false;  // (a load, not a revive: the camera is flicked only if it's in first person then)
		RE::TESForm*   g_handR = nullptr;
		RE::TESForm*   g_handL = nullptr;
		bool           g_wasFirst = false;
		// (Noah: after dying and loading back, my first-person hands were invisible until I put on gauntlets) a moment
		// after every load the camera is flicked and the hands' things put back, as after a revive
		void RearmAfterLoad()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			g_handR = p->GetEquippedObject(false);
			g_handL = p->GetEquippedObject(true);
			g_wasFirst = false, g_rearmLoad = true;
			g_rearm = 1.5f;
		}
		void Rearm(RE::PlayerCharacter* a_player)
		{
			auto* em = RE::ActorEquipManager::GetSingleton();
			auto* right = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F42);
			auto* left = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F43);
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && (g_wasFirst || (g_rearmLoad && cam->IsInFirstPerson()))) {
				cam->ForceThirdPerson();
				cam->ForceFirstPerson();
			}
			for (auto [form, slot] : { std::pair{ g_handR, right }, std::pair{ g_handL, left } }) {
				if (!form || !em) {
					continue;
				}
				// (Noah: "my character still thinks it's holding a weapon ... invisible") a new character's pack is emptied
				// between the load and this: what it held then (the start's Iron War Axe) is gone - never put back a thing
				// you no longer carry, or a spell you no longer know (an equipped ghost: draw sounds, nothing in the hand)
				if (auto* sp = form->As<RE::SpellItem>()) {
					if (a_player->HasSpell(sp)) em->EquipSpell(a_player, sp, slot);
				} else if (auto* obj = form->As<RE::TESBoundObject>(); obj && a_player->GetItemCount(obj) <= 0) {
					// (already a ghost - an older save: the hand still holds what isn't in the pack) lent for a moment so it
					// can be taken off properly, then taken back
					a_player->AddObjectToContainer(obj, nullptr, 1, nullptr);
					em->UnequipObject(a_player, obj, nullptr, 1, slot, false, true, false, true);
					a_player->RemoveItem(obj, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					logger::info("perk: a ghost in the hand taken off ({})", obj->GetName());
					continue;
				} else if (obj) {
					em->UnequipObject(a_player, obj, nullptr, 1, slot, false, true, false, true);
					em->EquipObject(a_player, obj, nullptr, 1, slot, false, false, false, true);
				}
			}
			if (!g_rearmLoad) a_player->DrawWeaponMagicHands(true);  // (after a revive the hands come up; a load leaves them as they were)
			logger::info("perk: revive - hands re-armed ({} / {})", g_handR ? g_handR->GetName() : "none", g_handL ? g_handL->GetName() : "none");
		}

		void Revive(RE::PlayerCharacter* a_player, bool a_falling)
		{
			// (Noah: after a revive the spells stopped doing damage) caught before the blow lands, you never fell: nothing
			// is forced on the body (no bleedout ended, no animation reset, no re-equip) - only health given back. Risen
			// from your knees: stood up, and the hands put back a moment later.
			const bool down = a_falling || a_player->AsActorState()->IsBleedingOut();
			if (down) {
				g_handR = a_player->GetEquippedObject(false);
				g_handL = a_player->GetEquippedObject(true);
				if (auto* cam = RE::PlayerCamera::GetSingleton()) {
					g_wasFirst = cam->IsInFirstPerson() || g_wasFirst;
				}
				g_rearm = 0.6f;
			}
			auto* av = a_player->AsActorValueOwner();
			const float max = av->GetPermanentActorValue(RE::ActorValue::kHealth);
			av->RestoreActorValue(RE::ActorValue::kHealth, max);
			if (down) {
				Stand(a_player);
			}
			g_grace = 3.0f;
			Director::Pause(2.0f);  // (the dead around you reel back a moment)
			Save::SetPerkStacks(gen::kPerk_hearthfire, N(gen::kPerk_hearthfire) - 1);  // one revive per draught
			Hud::Banner("THE HEARTH CALLS YOU BACK", 3.0f);
			logger::info("perk: hearthfire revive used{}", a_falling ? " (from the ground)" : " (before the blow could land)");
		}
		// every wound the night deals the player goes through here: a blow that would kill you is caught - by the
		// Hearthfire Tonic (you rise whole), in the moments after rising, or (in a party) by falling to your knees
		void Dodged(float a_seconds) { g_dodgeUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(a_seconds * 1000)); }

		struct HurtCheck { float hp = -1, t = 0; } g_hurtCheck;  // a blow's health, watched for a moment: put back by something?
		void DamagePlayer(float a_amount)
		{
			a_amount *= Difficulty::Taken();  // (the night's own blows follow Skyrim's difficulty)
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (a_amount <= 0 || std::chrono::steady_clock::now() < g_dodgeUntil) {
				if (static int said = 0; said++ < 8) logger::info("hurt: a blow of {:.1f} missed (dodging {})", a_amount, std::chrono::steady_clock::now() < g_dodgeUntil);
			}
			if (!p || p->IsDead() || a_amount <= 0 || std::chrono::steady_clock::now() < g_dodgeUntil) {
				return;  // (mid-roll: the blow misses)
			}
			if (Echo::WordSilenced(1)) {
				a_amount *= 0.8f;  // (TIID silenced: borrowed time)
			}
			a_amount *= WardMult();  // (the Ranger's Dawnwatch Tonic)
			Stats::OnHurt();
			auto*       av = p->AsActorValueOwner();
			const float hp = av->GetActorValue(RE::ActorValue::kHealth);
			if (hp - a_amount > 1.0f) {
				av->DamageActorValue(RE::ActorValue::kHealth, a_amount);
				g_hurtCheck = { av->GetActorValue(RE::ActorValue::kHealth), 0.6f };
				if (static int said = 0; said++ < 20 || said % 15 == 0) {  // (Noah: "after the health brew I took no damage") what a blow did, now and then
					logger::info("hurt: {:.1f} taken, health {:.0f} -> {:.0f} of {:.0f} (god {}, grace {:.1f})", a_amount, hp, av->GetActorValue(RE::ActorValue::kHealth),
						av->GetPermanentActorValue(RE::ActorValue::kHealth), RE::PlayerCharacter::IsGodMode(), g_grace);
				}
				return;
			}
			if (g_grace > 0) {
				return;
			}
			if (Has(gen::kPerk_hearthfire)) {
				Revive(p, false);
				return;
			}
			if (p->GetActorRuntimeData().boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential)) {
				av->DamageActorValue(RE::ActorValue::kHealth, std::max(0.0f, hp - 1.0f));  // (to the edge: the party's fall takes it from there)
				return;
			}
			av->DamageActorValue(RE::ActorValue::kHealth, a_amount);
		}
		void Tick()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && !player->IsDead()) {
				GuardSelf(player);
			}
			const auto now = std::chrono::steady_clock::now();
			const float dt = g_last.time_since_epoch().count() ? std::min(0.25f, std::chrono::duration<float>(now - g_last).count()) : 0.0f;
			g_last = now;
			if (!player || player->IsDead()) {
				g_grace = 0;
				return;
			}
			auto* av = player->AsActorValueOwner();
			const float max = av->GetPermanentActorValue(RE::ActorValue::kHealth);
			if (g_hurtCheck.t > 0 && (g_hurtCheck.t -= dt) <= 0) {
				if (const float now = av->GetActorValue(RE::ActorValue::kHealth); now > g_hurtCheck.hp + 0.1f * max) {
					logger::info("hurt: health went back up from {:.0f} to {:.0f} within a moment of the blow (grace {:.1f}, essential {})", g_hurtCheck.hp, now, g_grace,
						player->GetActorRuntimeData().boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential));
				}
			}
			{  // (Noah: the harpies' beam nearly one-shot me on Expert) with a harpy near, no moment takes more than a quarter
				static float prevHp = -1;
				const float hp = av->GetActorValue(RE::ActorValue::kHealth);
				if (prevHp > 0 && hp < prevHp && prevHp - hp > 0.25f * max && Director::HarpyNear(player->GetPosition(), 5000.0f)) {
					av->RestoreActorValue(RE::ActorValue::kHealth, (prevHp - hp) - 0.25f * max);
				}
				prevHp = av->GetActorValue(RE::ActorValue::kHealth);
			}
			// and their lasting spells (the beam) on you weakened as they take hold
			if (auto* list = player->AsMagicTarget()->GetActiveEffectList()) {
				static std::unordered_set<RE::ActiveEffect*> scaled;
				for (auto* ae : *list) {
					if (!ae || scaled.contains(ae)) continue;
					auto c = ae->caster.get();
					const float hm = c ? Director::HarmMult(c.get()) : 1.0f;
					if (hm < 0.999f && ae->magnitude > 0) {
						ae->magnitude *= hm;
						scaled.insert(ae);
					}
				}
				if (scaled.size() > 256) scaled.clear();
			}
			if (g_rearm > 0 && (g_rearm -= dt) <= 0 && !player->AsActorState()->IsBleedingOut()) {
				Rearm(player);
				g_wasFirst = false, g_rearmLoad = false;
				g_rearm = -10.0f;
			} else if (g_rearm <= 0 && g_rearm > -5.0f && player->AsActorState()->IsBleedingOut()) {
				g_rearm = 0.3f;  // (still on its knees: once it's up)
			}
			if (g_grace > 0) {  // the moments after rising: kept on your feet and whole
				g_grace -= dt;
				if (player->AsActorState()->IsBleedingOut()) {
					Stand(player);
				}
				if (av->GetActorValue(RE::ActorValue::kHealth) < max * 0.5f) {
					av->RestoreActorValue(RE::ActorValue::kHealth, max * 0.5f - av->GetActorValue(RE::ActorValue::kHealth));
				}
				return;
			}
			if (!Has(gen::kPerk_hearthfire)) {
				return;
			}
			const bool falling = player->AsActorState()->IsBleedingOut();
			if (max > 0 && (falling || av->GetActorValue(RE::ActorValue::kHealth) < max * 0.1f)) {
				Revive(player, falling);
			}
		}
	}
}
