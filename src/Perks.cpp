#include "BO3Z.h"

// Brews (sheet: perks). Bought at the Echo Altar for a fixed price, one of each, up to nine at once; (Noah: "permanent
// even through death") never lost in death; any can be refunded at the altar for half its price. Effects live in code.
namespace bo3z
{
	namespace
	{
		int  N(int a_perk) { return Save::PerkStacks(a_perk); }
		void Mod(RE::ActorValue a_av, float a_by)
		{
			RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, a_av, a_by);
		}
		// what a brew adds to you itself (the rest are read where they act): on (+1) or off (-1)
		void Apply(int a_perk, int a_sign)
		{
			const auto& p = gen::kPerks[a_perk];
			const float s = static_cast<float>(a_sign);
			switch (p.effect) {
			case 0:  // Troll's Hide: more health
				// (the audit: a wounded switch took the health above it and could kill you at the altar) taken off, the wound is
				// healed by as much as the cap drops first
				if (a_sign < 0) {
					if (auto* pl = RE::PlayerCharacter::GetSingleton()) pl->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, p.value);
				}
				Mod(RE::ActorValue::kHealth, s * p.value);
				break;
			case 1: Mod(RE::ActorValue::kMagicka, s * p.value); break;                  // Wellspring: more magicka
			case 2: Mod(RE::ActorValue::kSpeedMult, s * p.value); break;                // Stormrunner: faster on foot
			case 6: Mod(RE::ActorValue::kShoutRecoveryMult, -s * p.value); break;       // Dragonsblood: shouts recharge sooner
			default: break;
			}
		}
		std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_catalyzed;  // (Catalyst Philter) -> until
	}

	namespace Perks
	{
		float ManaMult() { return Echo::WearsRobes() ? 0.75f : 1.0f; }  // (Stage 5: the Robes of the First Dragonborn)
		// a class brew counts only while you are of its class (a switch keeps it, asleep, until you come back)
		bool Live(int a_perk) { return N(a_perk) > 0 && (gen::kPerkClass[a_perk] < 0 || gen::kPerkClass[a_perk] == Classes::Get()); }
		float RegenMult()
		{
			return (Live(gen::kPerk_manafont) ? gen::kPerks[gen::kPerk_manafont].value : 1.0f) * (Echo::WordSilenced(2) ? 1.5f : 1.0f) * (Echo::WearsRobes() ? 1.25f : 1.0f);  // (VO silenced: the curse undone)
		}

		// (Noah: "each class should get its own perks") the altar's list: your class's first, then the shared brews
		std::vector<int> Menu()
		{
			std::vector<int> out;
			const int        cls = Classes::Get();
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
		float FlaskCostMult() { return Live(gen::kPerk_quickhand) ? gen::kPerks[gen::kPerk_quickhand].value : 1.0f; }
		float FlaskRadiusMult() { return Live(gen::kPerk_volatile) ? gen::kPerks[gen::kPerk_volatile].value : 1.0f; }
		bool  SummonMight() { return Live(gen::kPerk_gravefury); }
		float SummonMightMult() { return SummonMight() ? gen::kPerks[gen::kPerk_gravefury].value : 1.0f; }
		float BladeSpellMult() { return Live(gen::kPerk_spellbinder) ? gen::kPerks[gen::kPerk_spellbinder].value : 1.0f; }
		// (Bounty Hunter's Ale, Gilded Tongue Tonic) a kill's Cursed Essence
		float KillEssenceMult(RE::Actor* a_dying)
		{
			float m = Live(gen::kPerk_giltongue) ? gen::kPerks[gen::kPerk_giltongue].value : 1.0f;
			if (a_dying && Live(gen::kPerk_bountyhunter) && (Director::IsElite(a_dying) || Director::IsBoss(a_dying) || Director::IsGreat(a_dying))) m *= gen::kPerks[gen::kPerk_bountyhunter].value;
			return m;
		}
		float MiniBossEssenceMult() { return Live(gen::kPerk_bountyhunter) ? gen::kPerks[gen::kPerk_bountyhunter].value : 1.0f; }
		// (Catalyst Philter) a flask's burst marks those it catches: they take more for a few seconds
		void Catalyze(RE::Actor* a_target)
		{
			if (!a_target || !Live(gen::kPerk_catalyst)) return;
			if (g_catalyzed.size() > 300) g_catalyzed.clear();
			g_catalyzed[a_target->GetFormID()] = std::chrono::steady_clock::now() + 5s;
		}
		float TakenMult(RE::Actor* a_target)
		{
			if (!a_target || g_catalyzed.empty()) return 1.0f;
			auto it = g_catalyzed.find(a_target->GetFormID());
			return it != g_catalyzed.end() && it->second > std::chrono::steady_clock::now() ? gen::kPerks[gen::kPerk_catalyst].value : 1.0f;
		}
		// (Bone Burst Brew) one of your summons fell: it bursts on the dead round it
		void OnSummonDied(RE::Actor* a_summon)
		{
			if (!a_summon || !Live(gen::kPerk_boneburst)) return;
			Gun::AreaHurt(a_summon->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f }, 320.0f, gen::kPerks[gen::kPerk_boneburst].value, "dark");
		}
		// (Overflow Tea) every third Destruction spell you cast is free: its magicka given back
		void OnDestructionCast(RE::SpellItem* a_spell)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !a_spell || !Live(gen::kPerk_overflow)) return;
			if (a_spell->GetAssociatedSkill() != RE::ActorValue::kDestruction) return;
			static int casts = 0;
			if (++casts % std::max(1, static_cast<int>(gen::kPerks[gen::kPerk_overflow].value)) != 0) return;
			p->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, a_spell->CalculateMagickaCost(p));
		}
		// a kill: what your class's brews make of it (how: 0 your summon, 1 a Destruction spell, 3 melee)
		void OnClassKill(int a_how, RE::Actor* a_dying)
		{
			if (a_how == 1 && Live(gen::kPerk_emberecho) && a_dying) {
				Gun::AreaHurt(a_dying->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f }, 320.0f, gen::kPerks[gen::kPerk_emberecho].value, "fireball");
			}
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

		// Buying a brew applies it at once (no potion to drink); one of each
		bool Grant(int a_perk)
		{
			if (a_perk < 0 || a_perk >= static_cast<int>(gen::kPerks.size()) || N(a_perk) > 0) {
				return false;
			}
			const auto& p = gen::kPerks[a_perk];
			Save::SetPerkStacks(a_perk, 1);
			Apply(a_perk, 1);
			Forms::Play(gen::Sound::drink);
			Hud::Banner(std::string(p.label), 3.0f);
			logger::info("perk: {} granted", p.id);
			return true;
		}
		// (Noah: "the option to refund perks at 50% return") a brew given back at the altar: off you, half its price back
		bool Refund(int a_perk)
		{
			if (a_perk < 0 || a_perk >= static_cast<int>(gen::kPerks.size()) || N(a_perk) <= 0) return false;
			const auto& p = gen::kPerks[a_perk];
			Apply(a_perk, -1);
			Save::SetPerkStacks(a_perk, 0);
			const int back = p.cost / 2;
			Points::Refund(back);
			Forms::Play(gen::Sound::ui_close);
			Hud::Banner(fmt::format("{} refunded  (+{})", p.label, Num(back)), 3.0f);
			logger::info("perk: {} refunded ({})", p.id, back);
			return true;
		}

		// (Noah: "When switching from one class to another, if perks were bought on the class that's being switched out for,
		// it should automatically refund those perks ... and 3 slots opened ... The universal perks stay though"; at half
		// price, like a refund by hand) every class brew not of the class you take up comes off you, half its price back
		int RefundOtherClasses(int a_keep)
		{
			int n = 0, back = 0;
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
				if (N(i) <= 0 || gen::kPerkClass[i] < 0 || gen::kPerkClass[i] == a_keep) continue;
				Apply(i, -1);
				Save::SetPerkStacks(i, 0);
				back += gen::kPerks[i].cost / 2;
				++n;
				logger::info("perk: {} refunded on a class switch ({})", gen::kPerks[i].id, gen::kPerks[i].cost / 2);
			}
			if (n > 0) {
				Points::Refund(back);
				Hud::Banner(fmt::format("{} class perk{} refunded  (+{})", n, n == 1 ? "" : "s", Num(back)), 4.0f);
			}
			return n;
		}

		// (Noah: "the perks as well for classes when switching to another class"; "the universal perks should stay when
		// switching classes") a class you leave keeps its own brews: they come off you and are handed back when you play that
		// class again; the shared brews stay on you
		std::vector<std::uint8_t> TakeAll()
		{
			std::vector<std::uint8_t> v(gen::kPerks.size(), 0);
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
				if (N(i) <= 0 || gen::kPerkClass[i] < 0) continue;
				Apply(i, -1);
				Save::SetPerkStacks(i, 0);
				v[i] = 1;
			}
			logger::info("perk: the class's brews put away");
			return v;
		}
		void GiveBack(const std::vector<std::uint8_t>& a_v)
		{
			for (int i = 0; i < static_cast<int>(std::min(a_v.size(), gen::kPerks.size())); ++i) {
				if (a_v[i] == 0 || N(i) > 0) continue;
				Save::SetPerkStacks(i, 1);
				Apply(i, 1);
			}
			logger::info("perk: the class's brews handed back");
		}

		// an old potion still in a pack (bought before brews applied at once): drinking it grants it the same way
		void OnEffect(RE::FormID a_mgef)
		{
			const int perk = Forms::PerkIndexOfEffect(a_mgef);
			if (perk < 0) {
				return;
			}
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
			{  // (Noah: "a sound cue for things that hit you from behind") a blow from outside your view: its side marked, a low thud
				const auto  zp = a_zombie->GetPosition(), pp = player->GetPosition();
				const float face = std::atan2(zp.x - pp.x, zp.y - pp.y);
				float       rel = std::fmod(face - player->GetAngleZ() + 3.0f * RE::NI_PI, 2.0f * RE::NI_PI) - RE::NI_PI;
				if (std::abs(rel) > 0.9f) {
					std::scoped_lock l(Hud::Lock());
					Hud::Edit().hurtDir = rel, Hud::Edit().hurtDirT = 1.0f;
					Audio::Ui(gen::Sound::flesh_hit, 0.45f);
				}
			}
			auto* av = player->AsActorValueOwner();
			const float jug = gen::kPerks[gen::kPerk_trollshide].value * N(gen::kPerk_trollshide);
			const float baseHealth = std::max(50.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth) - jug);
			const bool elite = Director::IsElite(a_zombie);
			const bool boss = Director::IsBoss(a_zombie);  // his claws come in flurries: each rake takes less
			// each kind hits as hard as it is (Undead::DamageMult, carried on its attack-damage value)
			const float kindMult = boss || elite ? 1.0f : std::clamp(a_zombie->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAttackDamageMult) / std::max(0.001f, gen::kZombieWeaponDamageMult), 0.5f, 2.0f);
			// (Noah: "the shield should also work against the Curse of Alduin enemies, same as Skyrim") a blow taken on a raised
			// guard, from in front: a shield stops most of it (80%), a blade or a staff about half; the guard costs stamina
			float guard = 1.0f;
			if (player->IsBlocking()) {
				auto       to = a_zombie->GetPosition() - player->GetPosition();
				const float yaw = player->GetAngleZ();
				to.z = 0;
				if (to.Unitize() > 0.0f && to.x * std::sin(yaw) + to.y * std::cos(yaw) > 0.35f) {  // (within about 70 degrees of where you face)
					auto*       lh = player->GetEquippedObject(true);
					const bool  shield = lh && lh->Is(RE::FormType::Armor);
					guard = shield ? 0.2f : 0.5f;
					if (!Live(gen::kPerk_spellguard)) av->DamageActorValue(RE::ActorValue::kStamina, shield ? 12.0f : 18.0f);  // (Spellguard Ale: blocking costs none)
					static int told = 0;
					if (told++ < 5) logger::info("hurt: blocked with {} - {:.0f}% of the blow taken", shield ? "a shield" : "a weapon", guard * 100.0f);
				}
			}
			// the first nights are gentler (a new character learns the dead): a quarter less on night 1, full by night 9
			const float early = std::clamp(0.75f + 0.03125f * static_cast<float>(Save::Get(gen::kAV_round)), 0.75f, 1.0f);
			DamagePlayer(baseHealth * (boss ? gen::boss::meleeShare : elite ? gen::elite::hitShare : gen::kHitDamageFraction * kindMult * early) * Settings::UndeadDamageMult() * guard);
			if (elite) {  // Life Drain
				auto* zav = a_zombie->AsActorValueOwner();
				zav->RestoreActorValue(RE::ActorValue::kHealth, zav->GetPermanentActorValue(RE::ActorValue::kHealth) * gen::elite::drainShare);
			}
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().hurtTime = 1.0f;
			}
		}

		// (after rising: a few seconds when you cannot fall again, so you are surely back on your feet)
		float g_grace = 0;
		std::chrono::steady_clock::time_point g_dodgeUntil{};  // a dodge's i-frames
		std::chrono::steady_clock::time_point g_last{};
		bool SelfRevive() { return g_grace > 0; }
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

		// every wound the night deals the player goes through here: a blow that would kill you is caught in the moments
		// after rising, or (in a party) by falling to your knees
		void Dodged(float a_seconds) { g_dodgeUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(a_seconds * 1000)); }

		struct HurtCheck { float hp = -1, t = 0; } g_hurtCheck;  // a blow's health, watched for a moment: put back by something?
		void DamagePlayer(float a_amount)
		{
			a_amount *= Difficulty::Taken();  // (the night's own blows follow Skyrim's difficulty)
			a_amount *= 0.7f;  // (Noah: "enemy health increased but their damage decreased") every blow of the night 30% softer
			a_amount *= (1.0f - Kit::ArmorResist()) * Kit::AuraMeleeTaken();
			if (Echo::WearsRobes()) a_amount *= 0.75f;  // (the First Dragonborn's Regalia: every blow a quarter softer)  // (the altar rework: your class's armour from its upgrades; a Spellsword's aura)
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
			Stats::OnHurt();
			auto*       av = p->AsActorValueOwner();
			// (the audit: a Gloomstalker's swipe took 91% on Legendary) no single blow of the night takes more than 45% of your health
			a_amount = std::min(a_amount, 0.45f * av->GetPermanentActorValue(RE::ActorValue::kHealth));
			const float hp = av->GetActorValue(RE::ActorValue::kHealth);
			if (Live(gen::kPerk_laststand) && hp < gen::kPerks[gen::kPerk_laststand].value * av->GetPermanentActorValue(RE::ActorValue::kHealth)) {
				a_amount *= 1.0f - gen::kPerks[gen::kPerk_laststand].value;  // (Last Stand Ale: low on health, every blow softer)
			}
			if (a_amount > 0.15f * av->GetPermanentActorValue(RE::ActorValue::kHealth)) {  // (hit feedback: a big blow is felt)
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().kickT = 0.35f;
			}
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
			if (player->IsBlocking() && Live(gen::kPerk_spellguard)) {  // (Spellguard Ale: a raised guard never tires you)
				av->RestoreActorValue(RE::ActorValue::kStamina, av->GetPermanentActorValue(RE::ActorValue::kStamina));
			}
		}
	}
}
