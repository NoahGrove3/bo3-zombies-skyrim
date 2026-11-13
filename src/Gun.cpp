#include "BO3Z.h"
#include "CofferSpells.h"
#include <sstream>

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

		std::unordered_map<RE::FormID, std::pair<Clock::time_point, bool>> g_hits;
		std::mt19937 g_rng{ std::random_device{}() };
		bool  g_loggedAim = false;




		// (Noah: "using melee attack weapons should cost stamina, just like how magic costs mana - otherwise I could just
		// infinitely swing until whatever I'm trying to kill dies") every swing of a blade, axe, mace or fist costs stamina
		// (a dagger 8, a one-handed weapon 12, a two-handed one 20, a fist 6; a power attack Skyrim's own cost instead); with
		// too little left for the next swing the attack doesn't start, the stamina bar flashes - like a spell without magicka
		// a hand's melee weapon (or bare fist) and what a swing of it costs; false for spells, bows, staves, shields, blocking
		bool MeleeCost(RE::PlayerCharacter* a_p, bool a_left, float& a_cost)
		{
			auto* obj = a_p->GetEquippedObject(a_left);
			if (obj && obj->GetFormType() != RE::FormType::Weapon) return false;  // (a spell, a shield, a torch)
			auto* w = obj ? obj->As<RE::TESObjectWEAP>() : nullptr;
			if (w) {
				if (w->IsBow() || w->IsCrossbow() || w->IsStaff()) return false;
				const bool two = w->IsTwoHandedSword() || w->IsTwoHandedAxe();
				if (a_left && two) return false;  // (the left button with a two-hander is its block)
				a_cost = w->IsHandToHandMelee() ? 6.0f : w->IsOneHandedDagger() ? 8.0f : two ? 20.0f : 12.0f;
				return true;
			}
			if (a_left && a_p->GetEquippedObject(false)) return false;  // (an empty left hand beside a weapon: the block)
			a_cost = 6.0f;
			return true;
		}
		bool PowerAttacking(RE::PlayerCharacter* a_p)
		{
			auto* proc = a_p->GetActorRuntimeData().currentProcess;
			auto* hd = proc ? proc->high : nullptr;
			return hd && hd->attackData && hd->attackData->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);
		}
		float Stamina(RE::PlayerCharacter* a_p) { return a_p->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina); }
		// the swing itself (heard from the animation, Bows.inl's sink): its stamina taken
		void MeleeSwing(bool a_left)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			float cost = 0;
			if (!p || !MeleeCost(p, a_left, cost) || PowerAttacking(p)) return;
			static std::chrono::steady_clock::time_point last[2]{};  // (one swing heard from both bodies' graphs, first and third person: paid once)
			const auto now = std::chrono::steady_clock::now();
			if (now - last[a_left] < std::chrono::milliseconds(150)) return;
			last[a_left] = now;
			p->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kStamina, std::min(cost, std::max(0.0f, Stamina(p))));
		}
		// any swing of the weapon in hand, heard from the animation graph (Alduin's Bane's and the swords' slashes, the swing's
		// stamina)
		std::atomic<int>  g_devSwing{ 0 };          // (dev: `swing 0|1` - a light or heavy swing's effects, no animation needed)
		std::atomic<bool> g_weaponSwing{ false };
		class SwingWatch : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_e, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (a_e && a_e->tag.c_str()) {
					std::string t(a_e->tag.c_str());
					std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (t == "weaponswing") {
						g_weaponSwing = true;
						MeleeSwing(false);
						if (auto* pp = RE::PlayerCharacter::GetSingleton(); pp && PowerAttacking(pp) && Classes::MutagenInHand()) Classes::MutagenPower();
					} else if (t == "weaponleftswing") {
						MeleeSwing(true);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		SwingWatch      g_swingWatch;
		RE::NiAVObject* g_swingBody = nullptr;
		void TickSwingWatch()   // (a new body after a load or a race change: listen to its graph - the third-person one only)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !p->Is3DLoaded()) return;
			if (auto* root = p->Get3D(false); root != g_swingBody) {
				p->RemoveAnimationGraphEventSink(&g_swingWatch);
				p->AddAnimationGraphEventSink(&g_swingWatch);
				g_swingBody = root;
			}
		}
		// the attack button: refused while there isn't stamina for the swing
		bool MeleeRefused(RE::InputEvent* a_event)
		{
			auto* b = a_event ? a_event->AsButtonEvent() : nullptr;
			auto* ue = RE::UserEvents::GetSingleton();
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!b || !ue || !p || b->IsUp() || !b->IsPressed()) return false;
			const auto& ev = b->QUserEvent();
			const bool right = ev == ue->rightAttack, left = ev == ue->leftAttack;
			if ((!right && !left) || !p->AsActorState()->IsWeaponDrawn()) return false;
			float cost = 0;
			if (!MeleeCost(p, left, cost) || Stamina(p) >= cost) return false;
			static auto flashed = std::chrono::steady_clock::now() - std::chrono::seconds(5);
			if (std::chrono::steady_clock::now() - flashed > std::chrono::milliseconds(600)) {
				flashed = std::chrono::steady_clock::now();
				RE::HUDMenu::FlashMeter(RE::ActorValue::kStamina);
			}
			return true;
		}

		struct InputBlock
		{
			static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
			{
				// (Noah: "make the swing speed match the delayed casting speed so it only swings when the next cast is ready") a
				// flask's hand swings only when its next throw is ready
				if (auto* b = a_event ? a_event->AsButtonEvent() : nullptr) {
					auto* uev = RE::UserEvents::GetSingleton();
					const auto& ue = b->QUserEvent();
					if (uev && (ue == uev->leftAttack || ue == uev->rightAttack) && Classes::FlaskSwingBlocked(ue == uev->leftAttack)) return false;
				}
				if (Echo::Eats(a_event)) return false;  // (the hall: a left click takes what's in your sights, no swing)
				if (MeleeRefused(a_event)) return false;
				return func(a_this, a_event);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Controllers (Xbox, and PlayStation through Steam Input, which Skyrim reads as an Xbox pad): RT the right hand's
		// attack, LT the left's
		constexpr std::uint32_t kPadRT = 0x000A, kPadLT = 0x0009;
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

		struct FireButton
		{
			static void thunk(RE::PlayerInputHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
			{
				// the mouse's hands the natural way round: the left button is the left hand, the right button the right
				// (Skyrim has them crossed). Not on a controller.
				if (a_event && a_event->GetDevice() == RE::INPUT_DEVICE::kMouse) {
					if (auto* ue = RE::UserEvents::GetSingleton()) {
						if (a_event->QUserEvent() == ue->rightAttack) {
							a_event->SetUserEvent(ue->leftAttack);
						} else if (a_event->QUserEvent() == ue->leftAttack) {
							a_event->SetUserEvent(ue->rightAttack);
						}
					}
				}
				func(a_this, a_event, a_data);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct FavBlock  // the shrine's and menus' keys never reach the hotkeys
		{
			static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
			{
				if (Shops::MenuEats(a_event) || Classes::MenuEats(a_event) || Director::RestEats(a_event) || Loot::Eats(a_event) || Echo::Eats(a_event) || Director::BedEats(a_event)) {  // the shrine's keys (1-4, D-pad) and the loot list's never reach the hotkeys
					return false;
				}
				return func(a_this, a_event);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// While the shrine's menu is open its keys belong to it: Activate, jump, the menu key, the camera's wheel zoom.
		template <int N>
		struct MenuEater
		{
			static bool thunk(void* a_this, RE::InputEvent* a_event)
			{
				if (Shops::MenuEats(a_event) || Classes::MenuEats(a_event) || Director::RestEats(a_event) || Loot::Eats(a_event) || Echo::Eats(a_event) || Director::BedEats(a_event)) {
					return false;
				}
				if constexpr (N == 2) {  // (the menu-open handler) the Wait key opens the Curse's rest screen - Skyrim's "enemies nearby" never stops it
					if (Director::WaitKey(a_event)) return false;
				}
				return func(a_this, a_event);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ReadyBlock
		{
			static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
			{
				if (Shops::MenuEats(a_event) || Classes::MenuEats(a_event) || Director::RestEats(a_event) || Loot::Eats(a_event) || Echo::Eats(a_event) || Director::BedEats(a_event)) {
					return false;
				}
				return func(a_this, a_event);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		float Rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }


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
			return a && a != player && !a->IsDead() && a->Is3DLoaded() && Director::IsFoe(a);  // nights: the undead and real enemies, never townsfolk
		}

		// A body's head node by name; for a creature whose skeleton names it otherwise ("auto" rows: the High Fantasy
		// Pack's), the first node with "head" in its name, remembered per race.
		RE::NiAVObject* FindHead(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 40) return nullptr;
			std::string n = a_obj->name.c_str();
			std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (n.find("head") != std::string::npos && n.find("track") == std::string::npos && n.find("target") == std::string::npos && n.find("look") == std::string::npos) {
				return a_obj;
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& c : node->GetChildren()) {
					if (auto* f = FindHead(c.get(), a_depth + 1)) return f;
				}
			}
			return nullptr;
		}
		RE::NiPoint3 HeadPos(RE::Actor* a)
		{
			if (auto* node = a->GetNodeByName(std::string(Director::HeadNodeFor(a)))) {
				return node->world.translate;
			}
			if (auto* root = a->Get3D()) {
				static std::unordered_map<RE::FormID, std::string> byRace;
				const RE::FormID race = a->GetRace() ? a->GetRace()->GetFormID() : 0;
				auto it = byRace.find(race);
				if (it == byRace.end()) {
					auto* h = FindHead(root);
					it = byRace.emplace(race, h ? std::string(h->name.c_str()) : std::string()).first;
					logger::info("head of {}: {}", a->GetDisplayFullName(), it->second.empty() ? "none found" : it->second);
				}
				if (!it->second.empty()) {
					if (auto* node = root->GetObjectByName(it->second)) {
						return node->world.translate;
					}
				}
			}
			return a->GetPosition() + RE::NiPoint3{ 0, 0, 110 };
		}

		// Stormblast (Noah: the shout that charges your weapon did nothing): its words gather lightning in whatever you
		// hold - gun, bow, spell, blade or fists - and the next blows you land let it go on the one you struck and leap to
		// the dead around it. Thunderchild's Stormblast charges the same way (its own enchanting needs a plain weapon).
		struct StormCharge
		{
			int   charges = 0;
			int   level = 1;
			float left = 0;
			float spark = 0;
		};
		StormCharge g_storm;
		bool        g_stormBusy = false;   // (while the lightning or a shout itself is dealing damage: no charge spent)
		struct Arc
		{
			RE::NiPoint3 from, to;
			float        delay;
		};
		std::vector<Arc> g_arcs;           // (Stormblast's lightning, drawn by TickStorm once Spellcraft is in reach)
		void        StormStrike(RE::Actor* a_target);

		// (Noah: headshots make their heads explode, then they die) a headshot kill bursts the head in blood: Skyrim's own
		// decapitation where the body has one (the draugr, the men and mer), else the head bone shrunk away; blood sprays
		// from the neck in every direction, the body's own gore explosion goes off there, and a wet crunch
		void HeadBurst(RE::Actor* a)
		{
			if (!a || !Settings::Blood()) {
				return;
			}
			const RE::NiPoint3 hp = HeadPos(a);
			const std::string  node(Director::HeadNodeFor(a));
			bool cut = false;
			if (auto* race = a->GetRace(); race && race->bodyPartData && race->bodyPartData->parts[RE::BGSBodyPartDefs::LIMB_ENUM::kHead] &&
											  !race->bodyPartData->parts[RE::BGSBodyPartDefs::LIMB_ENUM::kHead]->goreObjectName.empty()) {
				cut = a->Decapitate();
			}
			if (!cut) {
				if (auto* root = a->Get3D()) {
					RE::NiAVObject* h = root->GetObjectByName(node);
					if (!h) h = FindHead(root);
					if (h) {
						h->local.scale = 0.001f;
						RE::NiUpdateData ud;
						ud.time = 0.0f;
						h->Update(ud);
					}
				}
			}
			static auto* spray = RE::TESForm::LookupByID<RE::BGSImpactDataSet>(0x01F82A);  // BloodSprayImpactSetRed
			if (auto* im = RE::BGSImpactManager::GetSingleton(); im && spray) {
				for (int i = 0; i < 6; ++i) {
					const float ang = i * 1.047f;
					RE::NiPoint3 dir{ std::cos(ang), std::sin(ang), i % 2 ? 0.6f : -0.3f };
					im->PlayImpactEffect(a, spray, node.c_str(), dir, 120.0f, false, false);
				}
			}
			if (auto* race = a->GetRace(); race && race->bodyPartData) {
				for (int part : { static_cast<int>(RE::BGSBodyPartDefs::LIMB_ENUM::kHead), static_cast<int>(RE::BGSBodyPartDefs::LIMB_ENUM::kTorso) }) {
					auto* bp = race->bodyPartData->parts[part];
					if (bp && bp->data.dismemberExplosion) {
						if (auto* m = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B)) {  // an XMarker at the neck
							if (auto mk = a->PlaceObjectAtMe(m, false)) {
								mk->SetPosition(hp);
								mk->PlaceObjectAtMe(bp->data.dismemberExplosion, false);
								mk->Disable();
								mk->SetDelete(true);
							}
						}
						break;
					}
				}
			}
			Audio::Game(0x0DAB82, a);  // NPCKillGore
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().headBurstT = 0.18f;
		}

		// (Noah: Frozen Electrocuted Combustion's death effects on our own kills too) the element of the blow being struck
		// (Spellcraft::Strike sets it): a killing blow lays that element's touch spell on the body just before it dies, so
		// FEC sees a fire / frost / shock / sun / drain / poison death
		int g_fecElem = -1;  // a coffer::Element, -1 none
		void FecMark(RE::Actor* a_target, int a_elem)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !a_target || a_elem < 0) return;
			static std::array<RE::SpellItem*, 8> cache{};
			static std::array<bool, 8>           looked{};
			if (a_elem >= 8) return;
			if (!looked[a_elem]) {
				looked[a_elem] = true;
				for (int i = 0; i < Arcana::Count(); ++i) {
					if (Arcana::ClassOf(i) == 3 && Arcana::RungOf(i) == 1 && Arcana::ElementOf(i) == a_elem) cache[a_elem] = Arcana::Spell(i);
				}
			}
			if (auto* sp = cache[a_elem]; sp) {
				Forms::CastOnce(p, sp, a_target, 1.0f, p);
			}
		}
		int FecOfEffect(int a_fx)
		{
			switch (a_fx) {
			case gen::kFxIgnite: return 0;   // fire
			case gen::kFxFreeze:
			case gen::kFxShatter: return 1;  // frost
			case gen::kFxChain: return 2;    // shock
			case gen::kFxPoison: return 3;   // poison
			case gen::kFxHoly: return 5;     // sun
			case gen::kFxDrain: return 6;    // blood (drain)
			default: return -1;
			}
		}


		bool Damage(RE::Actor* a_target, float a_bo3Damage, bool a_head, bool a_melee, bool a_quiet = false)
		{
			if (!a_quiet && !g_stormBusy && g_storm.charges > 0 && a_target && !a_target->IsDead()) {
				StormStrike(a_target);
			}
			auto*       player = RE::PlayerCharacter::GetSingleton();
			auto*       av = a_target->AsActorValueOwner();
			const auto  tid = a_target->GetFormID();
			// (the difficulty reaches the night's dead through their health alone - Settings::UndeadHealthMult; Skyrim's
			// own enemies take our scripted blows scaled by it here, as Skyrim scales yours)
			float       damage = a_bo3Damage * Eggs::DamageMult() * Perks::TakenMult(a_target) * Difficulty::Dealt() / gen::kRounds.hpScale;  // (Catalyst Philter)
			if (!Director::IsRoundUndead(a_target)) {
				// Skyrim's own enemies (bandits, draugr, dragons) are tuned to the player's level, not to the night: our
				// weapons hit them like gear of your level would (half strength at level 1, full at 25, 1.5x at 50+).
				damage *= std::clamp(0.5f + 0.02f * static_cast<float>(player->GetLevel()), 0.5f, 1.5f);
				damage /= Difficulty::FoeToughness();
			}
			// (Noah: each common kind its own trick) the Volatile's hide turns half of every blow - but not one to the head,
			// and not one from behind it
			if (!a_head && Director::IsRoundUndead(a_target) && Undead::IsBrute(a_target)) {
				const float z = a_target->GetAngleZ();
				RE::NiPoint3 to = player->GetPosition() - a_target->GetPosition();
				to.z = 0;
				const float l = to.Length();
				const bool behind = l > 1.0f && (to.x * std::sin(z) + to.y * std::cos(z)) / l < -0.3f;
				if (!behind) damage *= 0.5f;
			}
			if (Undead::IsDancing(a_target)) damage *= 1.5f;  // (the Dance Of Death: half again as hard)
			if (Classes::Gilded(a_target)) damage *= 2.0f;  // (Noah: the Philosopher's Flask's gold statues - "taking double damage")
			damage *= Director::TakenMult(a_target) * Kit::ArmorAscendMult() * Echo::SetDamageMult();  // (a brute dazed or berserk; your armor's Ascends; the First Dragonborn's Regalia)
			av->DamageActorValue(RE::ActorValue::kHealth, damage);
			const bool ours = Director::IsRoundUndead(a_target);
			g_hits[a_target->GetFormID()] = { Clock::now(), a_head && !a_melee };
			if (ours) {
				Curse::NoteHit(tid, a_melee);  // (the knife counts for the Trial of Steel; spells and wands don't)
			}
			const bool kill = av->GetActorValue(RE::ActorValue::kHealth) <= 0;
			if (kill) {
				if (a_melee) {
					g_hits.erase(a_target->GetFormID());  // knife kills pay the melee bonus
				}
				if (ours) {
					Audio::Voice(a_target, Audio::VoiceEvent::kDeath);  // its death cry the instant the blow lands
				}
				if (!a_target->IsDead()) {
					if (g_fecElem >= 0) FecMark(a_target, g_fecElem);  // (FEC: the element's death)
					a_target->KillImpl(player, damage, true, false);
				}
				if (a_head && !a_melee) {
					HeadBurst(a_target);
				}
				if (Coop::Client() && ours) {
					Curse::OnUndeadDeath(a_target);  // (the host's game prunes its own dead; here the kill is the count)
				}
			} else {
				if (ours && !a_melee) {
					Points::Add(gen::kPoints_hit);
				}
				if (ours && !a_quiet) {
					Audio::Voice(a_target, Audio::VoiceEvent::kHurt);  // its own cry (limited per body)
				}
				if (ours) {
					Audio::Flesh(a_target, false);  // the flesh taking the hit (the spell's own impact plays where it lands)
					Undead::OnHit(a_target);         // (its flinch, now and then)
				}
				if (!a_target->IsInCombat()) {
					SafeCombat(a_target, player);
				}
			}
			if (!a_quiet || kill) {
				Forms::Play(a_head && !a_melee ? gen::Sound::headshot : gen::Sound::hitmarker);
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().hitTime = kill ? 0.3f : 0.2f;
				Hud::Edit().hitKill = kill;
				Hud::Edit().hitHead = a_head && !a_melee;
			}
			return kill;
		}

		// Venom wand: damage over time (BO3 units per second) on top of the hit.
		struct Dot
		{
			RE::ActorHandle target;
			float           perSecond, left;
			int             fec = -1;  // (Noah: "their death effect animations should use the mod ... from dying by fire") a burn kills by fire (0)
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
				const int was = g_fecElem;
				g_fecElem = d.fec;
				Damage(a.get(), d.perSecond * step, false, false, true);
				g_fecElem = was;
				d.left -= step;
			}
			std::erase_if(g_dots, [](const Dot& d) { return d.left <= 0; });
		}

		void Stagger(RE::Actor* a, float a_mag)
		{
			a->SetGraphVariableFloat("staggerMagnitude", a_mag);
			a->NotifyAnimationGraph("staggerStart");
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


		// ================================================================ the effects map
		// (Noah: "I want the effects for all the weapons and their attacks/abilities to be compiled of Skyrim mods from nexus or
		// base Skyrim stuff ... Don't make your stuff from scratch it's higher quality from the mods") every one of our effect
		// models can be stood in for by Skyrim's own (or an installed magic mod's) - one or several of them, each coloured by
		// one of our greyscale-to-palette ramps, sized, turned and raised as it needs - named in
		// Data/SKSE/Plugins/CurseOfAlduin/fxmap.ini, one line a model:
		//     CurseOfAlduin\fx\arc_purple.nif = magic\voiceflamewaveproject01.nif pal=violet s=0.6 r=0,0,90 ; effects\x.nif s=2
		// (pal=<fire|violet|gold|...> our ramp textures\CurseOfAlduin\fx\grad_<name>.dds, or a texture path; col=r,g,b its colour
		// instead; s= its size; r= degrees about x,y,z; z= raised; play: its own animation started; `none`: nothing drawn).
		// Read at load and again on the dev command `fxreload`, so a look can be tried in the running game.
		int BlastIndex(std::string_view a_name);
		struct FxPart { std::string model, pal; RE::NiPoint3 col{ -1, -1, -1 }; float scale = 1.0f, dz = 0.0f; RE::NiMatrix3 rot; bool play = false, none = false, noparticles = false; int blast = -1; float orbitR = 0, orbitSpeed = 0, phase = 0; };
		std::unordered_map<std::string, std::vector<FxPart>> g_fxMap;
		std::string FxKey(std::string a)
		{
			std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
			if (a.rfind("meshes\\", 0) == 0) a.erase(0, 7);
			return a;
		}
		std::string Trim(std::string a)
		{
			const auto b = a.find_first_not_of(" \t\r\n"), e = a.find_last_not_of(" \t\r\n");
			return b == std::string::npos ? std::string{} : a.substr(b, e - b + 1);
		}
		int FxMapLoad()
		{
			g_fxMap.clear();
			std::ifstream in("Data/SKSE/Plugins/CurseOfAlduin/fxmap.ini");
			std::string line;
			int n = 0;
			while (std::getline(in, line)) {
				if (const auto c = line.find('#'); c != std::string::npos) line.erase(c);
				const auto eq = line.find('=');
				if (eq == std::string::npos) continue;
				const std::string src = FxKey(Trim(line.substr(0, eq)));
				std::vector<FxPart> parts;
				std::stringstream all(line.substr(eq + 1));
				std::string one;
				while (std::getline(all, one, ';')) {
					std::stringstream ws(one);
					std::string tok;
					FxPart fp;
					float rx = 0, ry = 0, rz = 0;
					while (ws >> tok) {
						if (fp.model.empty() && tok.find('=') == std::string::npos) {
							fp.model = tok;
							fp.none = tok == "none";
							if (tok.rfind("blast:", 0) == 0) {   // (one of Skyrim's own explosions, whole: its look, light and sound)
								fp.blast = BlastIndex(tok.substr(6));
								if (fp.blast < 0) logger::warn("fxmap: no blast called {}", tok.substr(6)), fp.none = true;
							}
							continue;
						}
						const auto q = tok.find('=');
						const std::string k = q == std::string::npos ? tok : tok.substr(0, q), v = q == std::string::npos ? "" : tok.substr(q + 1);
						try {
							if (k == "pal") fp.pal = v.find('\\') == std::string::npos ? "textures\\CurseOfAlduin\\fx\\pal_" + v + ".dds" : v;
							else if (k == "s") fp.scale = std::stof(v);
							else if (k == "z") fp.dz = std::stof(v);
							else if (k == "noplay") fp.play = false;
							else if (k == "play") fp.play = true;
							else if (k == "noparticles") fp.noparticles = true;
							else if (k == "orbit") std::sscanf(v.c_str(), "%f,%f,%f", &fp.orbitR, &fp.orbitSpeed, &fp.phase);   // (circling its middle: radius, turns a second, where it starts)   // (its particles hidden: a whirlwind's thrown debris, flickering all round)   // (its controller manager's first sequence started: only where a model stands still without it)
							else if (k == "col") std::sscanf(v.c_str(), "%f,%f,%f", &fp.col.x, &fp.col.y, &fp.col.z);
							else if (k == "r") std::sscanf(v.c_str(), "%f,%f,%f", &rx, &ry, &rz);
						} catch (...) {}
					}
					if (fp.model.empty()) continue;
					constexpr float kDeg = 0.01745329f;
					fp.rot.SetEulerAnglesXYZ(rx * kDeg, ry * kDeg, rz * kDeg);
					parts.push_back(std::move(fp));
				}
				if (!src.empty() && !parts.empty()) g_fxMap[src] = std::move(parts), ++n;
			}
			logger::info("fxmap: {} of our effect models stood in for by Skyrim's own or installed mods'", n);
			return n;
		}
		// the stand-ins for one of our models (itself, plain, when it has none)
		std::vector<FxPart> FxParts(const std::string& a_model)
		{
			static bool loaded = false;
			if (!loaded) loaded = true, FxMapLoad();
			if (auto it = g_fxMap.find(FxKey(a_model)); it != g_fxMap.end()) return it->second;
			FxPart fp;
			fp.model = a_model;
			fp.play = false;
			return { fp };
		}
		// a model's effect shaders coloured: by a greyscale-to-palette ramp (every material its own copy - the one it came with
		// is shared by every copy of the model in the game) or by a colour laid over its own
		void TintTree(RE::NiAVObject* a_root, const FxPart& a_part)
		{
			if (!a_root || (a_part.pal.empty() && a_part.col.x < 0)) return;
			static std::unordered_map<std::string, RE::NiPointer<RE::NiTexture>> pals;
			RE::NiPointer<RE::NiTexture> pal;
			if (!a_part.pal.empty()) {
				auto& t = pals[a_part.pal];
				if (!t) RE::BSShaderManager::GetTexture(a_part.pal.c_str(), true, t, false);
				pal = t;
			}
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* g) {
				auto* ep = netimmerse_cast<RE::BSEffectShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get());
				auto* m = ep ? static_cast<RE::BSEffectShaderMaterial*>(ep->material) : nullptr;
				if (!m) return RE::BSVisit::BSVisitControl::kContinue;
				// (Noah: a Dragon Slayer kill "my whole screen just blew up with random lighting effects") a heat-shimmer layer bends
				// what's behind it: coloured by a ramp it painted the whole screen - left as it was
				if (ep->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kRefraction)) return RE::BSVisit::BSVisitControl::kContinue;
				if (auto* nm = static_cast<RE::BSEffectShaderMaterial*>(m->Create())) {
					nm->CopyMembers(m);
					if (pal) {
						nm->greyscaleTexture = RE::NiPointer<RE::NiSourceTexture>(static_cast<RE::NiSourceTexture*>(pal.get()));
						nm->greyscaleTexturePath = a_part.pal.c_str();
					}
					if (a_part.col.x >= 0) nm->baseColor = { a_part.col.x, a_part.col.y, a_part.col.z, nm->baseColor.alpha };
					ep->SetMaterial(nm, true);
					nm->~BSEffectShaderMaterial();
					RE::free(nm);
				}
				if (pal) ep->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteColor);
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		// its own animation run from the start (Skyrim's effect models keep theirs in a controller manager: placed as an object
		// it would stand at its first frame)
		float PlayOwn(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
		{
			auto* node = a_root ? a_root->AsNode() : nullptr;
			if (!a_ref || !node) return 0.0f;
			for (auto* c = node->GetControllers(); c; c = c->next.get()) {
				if (auto* mgr = netimmerse_cast<RE::NiControllerManager*>(c)) {
					RE::NiControllerSequence* pick = nullptr;
					for (const char* nm : { "mIdle", "Idle", "Play", "Start", "mStart", "Loop" })
						if (!pick) pick = mgr->GetSequenceByName(nm);
					if (!pick && mgr->sequenceArray.size() > 0) pick = mgr->sequenceArray[0].get();
					if (pick) a_ref->PlayAnimation(mgr, pick, pick);   // (from itself: the game reads the sequence it leaves)
					return pick ? std::max(0.0f, pick->endKeyTime - pick->beginKeyTime) : 0.0f;
				}
			}
			return 0.0f;
		}
		// placed references whose models aren't in yet: coloured (and set going) the moment they are
		struct TintWatch { RE::ObjectRefHandle h; FxPart part; bool play = false; float age = 0; };
		std::vector<TintWatch> g_tintWatch;
		void WatchTint(RE::TESObjectREFR* a_ref, const FxPart& a_part, bool a_play = false)
		{
			a_play = a_play && a_part.play;
			if (a_ref && (!a_part.pal.empty() || a_part.col.x >= 0 || a_play) && g_tintWatch.size() < 96) g_tintWatch.push_back({ a_ref->GetHandle(), a_part, a_play });
		}
		void TickTints(float a_delta)
		{
			for (auto& w : g_tintWatch) {
				w.age += a_delta;
				auto r = w.h.get();
				auto* o = r ? r->Get3D() : nullptr;
				if (!o) continue;
				TintTree(o, w.part);
				if (w.play) PlayOwn(r.get(), o);
				w.age = 99.0f;
			}
			std::erase_if(g_tintWatch, [](const TintWatch& w) { return w.age > 3.0f; });
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
			RE::BGSExplosion* form = nullptr;  // (FxFrom: another mod's burst, quieted)
			bool              exact = false;   // (our own models: the size asked, never shrunk at your feet)
			std::shared_ptr<FxPart> part;      // (a stand-in for one of ours: its colour, set on it once it's in)
		};
		// Skyrim's explosions are built for dragons and Dragonborn shouts: ours are shrunk so a hit reads as a hit,
		// not a screen-filling burst. Shots use the small size, signature moves the larger one.
		constexpr float kTapBlastScale = 0.35f, kBlastScale = 0.42f;  // compact: a burst the size of a cart, never the screen
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
						(void)nm;
						ex->imageSpaceModifying = nullptr;  // no blast ever tints the screen
						ex->data.imageSpaceRadius = 0.0f;
						ex->data.radius = std::min(src->data.radius, 220.0f);
						// Skyrim's explosion sounds are built for dragons (and roar over everything): ours are silent,
						// the spell's own (quieter, layered) sound plays instead
						ex->data.sound1 = nullptr;
						ex->data.sound2 = nullptr;
						// (Noah: "all blood/life drain magic to be a dark red pulse/explosion, not the flesh explosion") the
						// Dragonborn gore burst wears our dark-red pulse (tools/elemburst.py, blood) everywhere it's used
						if (nm == "bloodplague") ex->SetModel("CurseOfAlduin\\fx\\burst_blood.nif"), ex->data.impactDataSet = nullptr, ex->data.impactPlacedObject = nullptr;
						made[a_index] = ex;
					}
				}
				logger::info("blast: {} ({:06X}) {}", gen::kBlasts[a_index].first, gen::kBlasts[a_index].second, made[a_index] ? "ready" : "missing");
			}
			return made[a_index];
		}

		// any explosion, quieted as the templates are (no damage, no knockdown, no screen tint, no dragon-loud sound)
		RE::BGSExplosion* QuietClone(RE::BGSExplosion* a_src)
		{
			static std::unordered_map<RE::BGSExplosion*, RE::BGSExplosion*> made;
			if (!a_src) return nullptr;
			if (auto it = made.find(a_src); it != made.end()) return it->second;
			RE::BGSExplosion* ex = nullptr;
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSExplosion>(); factory && (ex = factory->Create())) {
				ex->boundData = a_src->boundData;
				ex->SetModel(a_src->GetModel());
				ex->data = a_src->data;
				ex->data.damage = 0.0f;
				ex->data.force = std::min(a_src->data.force, 40.0f);
				ex->data.flags.reset(RE::BGSExplosionData::Flag::kKnockDown_Always, RE::BGSExplosionData::Flag::kKnockDown_ByFormula);
				ex->data.spawnProjectile = nullptr;
				ex->formEnchanting = nullptr;
				ex->imageSpaceModifying = nullptr;
				ex->data.imageSpaceRadius = 0.0f;
			}
			made[a_src] = ex;
			return ex;
		}
		// (Noah: "I can see the impact of the flasks hitting things but ... I can't see the flasks flying"; the bursts and pools
		// hung loose in the scene were dropped by the renderer) our effect models go into the world the way Skyrim's own do:
		// a burst is an explosion wearing our model, a pool a hazard (it lies there its lifetime, then the game clears it)
		int BlastIndex(std::string_view a_name);
		RE::BGSExplosion* ModelExplosion(const std::string& a_path)
		{
			static std::unordered_map<std::string, RE::BGSExplosion*> made;
			if (auto it = made.find(a_path); it != made.end()) return it->second;
			RE::BGSExplosion* ex = nullptr;
			auto* tpl = BlastForm(BlastIndex("fireball"));
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSExplosion>(); tpl && factory && (ex = factory->Create())) {
				ex->boundData = tpl->boundData;
				ex->SetModel(a_path.c_str());
				ex->data = tpl->data;
				ex->data.light = nullptr, ex->data.sound1 = nullptr, ex->data.sound2 = nullptr;
				ex->data.impactDataSet = nullptr, ex->data.impactPlacedObject = nullptr, ex->data.spawnProjectile = nullptr;
				ex->data.damage = 0.0f, ex->data.force = 0.0f, ex->data.radius = 1.0f, ex->data.imageSpaceRadius = 0.0f;
				ex->formEnchanting = nullptr, ex->imageSpaceModifying = nullptr;
			}
			logger::info("blast: {} as an explosion {}", a_path, ex ? "ready" : "MISSING");
			made[a_path] = ex;
			return ex;
		}
		RE::BGSHazard* ModelHazard(const std::string& a_path, float a_life, bool a_drop = true)
		{
			static std::map<std::pair<std::string, int>, RE::BGSHazard*> made;
			const auto key = std::make_pair(a_path, static_cast<int>(a_life * 10.0f) * 2 + (a_drop ? 1 : 0));
			if (auto it = made.find(key); it != made.end()) return it->second;
			RE::BGSHazard* hz = nullptr;
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSHazard>(); factory && (hz = factory->Create())) {
				hz->SetModel(a_path.c_str());
				// (Noah: the game crashed a second after a flask landed - and twice in Dragonsreach: the pool's hazard went to cast
				// its spell on whoever stood in it, and it had none) it carries the flasks' own do-nothing spell, and never
				// looks for anyone to cast it on within its lifetime (the hurting is ours)
				hz->data.limit = 24, hz->data.radius = 1.0f, hz->data.lifetime = a_life, hz->data.imageSpaceRadius = 0.0f, hz->data.targetInterval = a_life + 60.0f;
				if (a_drop) hz->data.flags.set(RE::BGSHazardData::BGSHazardFlags::kDropToGround);  // (the hall's glows hang where they're set)
				hz->data.spell = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(gen::kFlaskSpell[0], gen::kPlugin);
				hz->data.light = nullptr, hz->data.impactDataSet = nullptr, hz->data.sound = nullptr;
				hz->boundData.boundMin.x = hz->boundData.boundMin.y = -8, hz->boundData.boundMin.z = -2;
				hz->boundData.boundMax.x = hz->boundData.boundMax.y = hz->boundData.boundMax.z = 8;
			}
			logger::info("hazard: {} ({:.1f} s) {}", a_path, a_life, hz ? "ready" : "MISSING");
			made[key] = hz;
			return hz;
		}
		// anything placed at a point (the blast marker carried there first)
		RE::NiPointer<RE::TESObjectREFR> PlaceAtPoint(RE::TESBoundObject* a_obj, const RE::NiPoint3& a_at)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !a_obj || !player->GetParentCell()) return {};
			auto m = g_blastMarker.get();
			if (!m || m->IsDeleted() || m->GetParentCell() != player->GetParentCell()) {
				if (m) m->Disable(), m->SetDelete(true);
				auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);  // XMarker
				auto  r = xm ? player->PlaceObjectAtMe(xm, false) : nullptr;
				if (!r) return {};
				g_blastMarker = r->GetHandle();
				m = r;
			}
			m->SetPosition(a_at);
			return m->PlaceObjectAtMe(a_obj, false);
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
			Director::Noise(2.0f);  // (the Volatiles) a spell bursting is heard far off
			if (a_expl >= 0 && g_blasts.size() < 32) {
				g_blasts.push_back({ a_at, a_delay, a_expl, a_scale });
			}
		}

		// (Noah: "make sure all the flames are purple" - a fireball coloured violet still left Skyrim's own orange fire where it
		// struck: its impact set and the fire it places) a copy of one of Skyrim's explosions that leaves nothing of its own
		// behind - only its own model, which takes the colour
		RE::BGSExplosion* TintableBlast(int a_expl)
		{
			static std::unordered_map<int, RE::BGSExplosion*> made;
			if (auto it = made.find(a_expl); it != made.end()) return it->second;
			auto* src = BlastForm(a_expl);
			RE::BGSExplosion* ex = nullptr;
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSExplosion>(); src && factory && (ex = factory->Create())) {
				ex->boundData = src->boundData;
				ex->SetModel(src->GetModel());
				ex->data = src->data;
				ex->data.damage = 0.0f, ex->data.force = std::min(src->data.force, 40.0f);
				ex->data.flags.reset(RE::BGSExplosionData::Flag::kKnockDown_Always, RE::BGSExplosionData::Flag::kKnockDown_ByFormula);
				ex->data.impactDataSet = nullptr, ex->data.impactPlacedObject = nullptr, ex->data.spawnProjectile = nullptr;
				ex->data.light = nullptr;   // (its orange light too)
				ex->formEnchanting = nullptr, ex->imageSpaceModifying = nullptr, ex->data.imageSpaceRadius = 0.0f;
			}
			made[a_expl] = ex;
			return ex;
		}
		void Detonate(const PendingBlast& b)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* ex = b.form ? b.form : BlastForm(b.expl);
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
				// (Noah: "the visual effects are still overwhelming, especially when casting spells at the feet") every burst
				// a fifth smaller, and one at your own feet smaller again
				const float atFeet = std::clamp((b.at.GetDistance(player->GetPosition()) - 150.0f) / 350.0f, 0.0f, 1.0f);
				const float sc = b.exact ? b.scale : b.scale * 0.8f * (0.6f + 0.4f * atFeet);
				r->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(sc, 0.1f, 5.0f) * 100.0f);
				if (b.part) WatchTint(r.get(), *b.part);
				g_shrink.push_back({ r->CreateRefHandle(), { sc, 0.6f } });
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
			// (the performance pass) at most six bursts placed a frame: a crowd's worth of them spreads over a few frames
			// instead of one long hitch (each one is a new reference, its model and its light)
			constexpr std::size_t kPerFrame = 6;
			for (std::size_t i = kPerFrame; i < now.size(); ++i) {
				auto b = now[i];
				b.delay = 0.016f * static_cast<float>(1 + (i - kPerFrame) / kPerFrame);
				g_blasts.push_back(b);
			}
			if (now.size() > kPerFrame) now.resize(kPerFrame);
			for (const auto& b : now) {
				Detonate(b);
			}
			TickShrink(a_delta);
			TickTints(a_delta);
			Gun::TickLooks(a_delta);
		}



		// Black holes (Event Horizon): pull every undead in toward the centre, then collapse.
		struct Well
		{
			RE::NiPoint3 at;
			float        left, base, pulse;
			float        reach = 950.0f, pull = 340.0f, burst = 420.0f;  // (the Subsuming Vortex's holes: their own size and pull)
			bool         vortex = false;
			RE::ObjectRefHandle fx{};  // its black hole, placed in the world
			float        life = 0;       // how long it has stood (the look swells in, then spins)
		};
		std::vector<Well> g_wells;
		struct VFlash { RE::ObjectRefHandle fx; float t, scale; };
		std::vector<VFlash> g_vflash;  // (the Vortex's burst: its hole swelling out as a shockwave, then gone)
		struct PFire { RE::ObjectRefHandle fx; float t, life, delay, scale; RE::NiPoint3 at; std::uint32_t form = 0; };  // (form: 0 the violet fire, else an element's burst)
		std::vector<PFire> g_pfire;  // (the Horn's dragon's violet breath: bursts placed along its line, gone a moment later)

		float g_manaQuiet = 0;  // seconds since the last magicka shot (regen waits for it)




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
		void StormStrike(RE::Actor* a_target)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			static auto last = Clock::now() - std::chrono::seconds(5);
			if (!p || g_storm.charges <= 0 || Clock::now() - last < std::chrono::milliseconds(250)) {
				return;  // (one blow is one charge, however many ways the game reports it)
			}
			last = Clock::now();
			--g_storm.charges;
			g_stormBusy = true;
			const int   L = g_storm.level;
			const float k = L == 1 ? 1.0f : L == 2 ? 1.8f : 3.2f;
			const float base = Director::RoundHealth() * gen::kRounds.hpScale * 0.9f * k;
			const auto  at = a_target->GetPosition();
			// (Noah: the bodies hung in the air, and the strike was only a flash) the push only on the living, and the
			// lightning seen: a bolt down from the sky, arcs crawling all round the one struck, arcs leaping to the rest
			const RE::NiPoint3 chest = at + RE::NiPoint3{ 0, 0, 80.0f };
			g_arcs.push_back({ chest + RE::NiPoint3{ 0, 0, 1300.0f }, at, 0.0f });
			for (int k = 0; k < 7; ++k) {
				const float a = 0.9f * k;
				g_arcs.push_back({ chest + RE::NiPoint3{ std::cos(a) * 30.0f, std::sin(a) * 30.0f, 20.0f * (k % 3) - 20.0f },
					chest + RE::NiPoint3{ std::cos(a + 1.9f) * 110.0f, std::sin(a + 1.9f) * 110.0f, 40.0f * ((k + 1) % 3) - 60.0f }, 0.05f * k });
			}
			// (Noah: the bolts and chains were invisible) Skyrim's own lightning on top of ours: a bolt of it to each one
			// struck, the storm's crack on the ground under the first
			Cast(gen::kSpell_thunderbolt, a_target);
			Damage(a_target, base * 1.4f, false, false, true);
			Blast(BlastIndex("skybolt"), at, 0.0f, 0.7f);
			Blast(BlastIndex("shockmass"), at, 0.05f, 0.6f);
			if (!a_target->IsDead()) {
				if (auto* proc = p->GetActorRuntimeData().currentProcess; proc && L == 3) {
					proc->KnockExplosion(a_target, at + RE::NiPoint3{ 0, 0, 140.0f }, 1.6f);
				} else {
					Stagger(a_target, 0.7f);
				}
			}
			int n = 0;
			for (auto* f : Foes(at, 450.0f + 150.0f * L)) {
				if (f == a_target || n >= 2 + 2 * L) {
					continue;
				}
				++n;
				g_arcs.push_back({ chest, f->GetPosition() + RE::NiPoint3{ 0, 0, 80.0f }, 0.06f * n });
				Cast(gen::kSpell_lightningbolt, f);
				Blast(BlastIndex("runeshock"), f->GetPosition(), 0.06f * n, 0.5f);
				Damage(f, base * 0.7f, false, false, true);
				if (!f->IsDead()) {
					Stagger(f, 0.5f);
				}
			}
			g_stormBusy = false;
			logger::info("shout: Stormblast let go on {:08X} (+{} around it), {} charges left", a_target->GetFormID(), n, g_storm.charges);
		}







		void TickWells(float a_delta)
		{
			for (auto& w : g_wells) {
				w.left -= a_delta;
				w.pulse -= a_delta;
				w.life += a_delta;
				for (auto* f : Foes(w.at, w.reach)) {
					auto       pos = f->GetPosition();
					RE::NiPoint3 to = w.at - pos;
					to.z = 0;
					const float d = to.Length();
					if (d > 70.0f) {
						// dragged toward the centre (the Vortex's pull grows as they near it)
						const float pull = w.vortex ? w.pull * (1.0f + 1.2f * (1.0f - std::min(1.0f, d / w.reach))) : w.pull;
						pos += to * (std::min(d - 60.0f, pull * a_delta) / d);
						f->SetPosition(pos, true);
					}
				}
				if (auto r = w.fx.get()) {  // the hole: swells in, spins, and shrinks to a point just before it bursts
					const float grow = std::min(1.0f, w.life / 0.35f) * (w.left < 0.25f ? std::max(0.05f, w.left / 0.25f) : 1.0f);
					// (Noah: the black hole too small and flat to read) twice the size, its disc tipped toward you (a third of the way
					// up) so it reads as the render does - a black heart in a burning violet ring - still spinning
					r->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(2.2f * grow * w.burst / 420.0f * (1.0f + 0.06f * std::sin(w.life * 9.0f)), 0.05f, 9.0f) * 100.0f);
					float tip = 0.0f, face = 0.0f;
					if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
						const auto d = pc->GetPosition() - w.at;
						face = std::atan2(d.x, d.y);
						tip = 0.55f;
					}
					r->SetAngle({ tip * std::cos(face), tip * std::sin(face), w.life * 2.4f });
					if (auto* n = r->Get3D()) {
						n->local.scale = r->GetReferenceRuntimeData().refScale / 100.0f;
						RE::NiUpdateData ud;
						n->Update(ud);
					}
				}
				if (w.pulse <= 0) {
					w.pulse = 0.5f;
					if (!w.vortex) Blast(BlastIndex("dark"), w.at + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, kBlastScale);  // (the Vortex: its hole is the look - no blue puffs over it)
					for (auto* f : Foes(w.at, w.vortex ? w.burst * 0.55f : 260.0f)) {
						Damage(f, w.base * 0.4f, false, false, true);
					}
				}
				if (w.left <= 0) {  // collapse
					// (Noah: "the after explosion effect looks fine but should be colored purple instead of red") the Vortex's burst in
					// the void's own violet: the Daedric orb's blast, swelled, with the starfield's flash inside it
					if (w.vortex) {
						const float sc = std::clamp(w.burst / 420.0f, 0.5f, 2.0f);
						// the hole itself flares out as a violet shockwave (its own model, so its own colour), the starfield's flash inside
						Blast(BlastIndex("constellation"), w.at + RE::NiPoint3{ 0, 0, 60.0f }, 0.05f, std::clamp(w.burst / 500.0f, 0.4f, 1.6f));
						if (w.fx.get()) g_vflash.push_back({ w.fx, 0.0f, sc });
						Gun::PurpleFire(w.at, 2.6f * sc, 0.0f, 0.9f);
						Gun::PurpleFire(w.at + RE::NiPoint3{ 0, 0, 40.0f }, 1.6f * sc, 0.12f, 0.8f);
						w.fx = {};
					} else {
						Blast(BlastIndex("darkmassive"), w.at + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, kBlastScale);
					}
					Forms::Play(gen::Sound::cast_heavy);
					for (auto* f : Foes(w.at, w.burst)) {
						Damage(f, w.base * 6.0f, false, false);
						if (!f->IsDead()) {
							Stagger(f, 1.0f);
						}
					}
					if (auto r = w.fx.get()) {
						r->Disable();
						r->SetDelete(true);
					}
					w.fx = {};
				}
			}
			std::erase_if(g_wells, [](const Well& w) { return w.left <= 0; });
			for (auto& f : g_vflash) {
				f.t += a_delta;
				auto r = f.fx.get();
				if (!r) { f.t = 9.0f; continue; }
				if (f.t > 0.45f) {
					r->Disable();
					r->SetDelete(true);
					continue;
				}
				const float k = f.scale * (1.0f + 6.0f * f.t / 0.45f);
				r->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(k, 0.05f, 30.0f) * 100.0f);
				if (auto* n = r->Get3D()) {
					n->local.scale = k;
					RE::NiUpdateData ud;
					n->Update(ud);
				}
			}
			std::erase_if(g_vflash, [](const VFlash& f) { return f.t > 0.45f; });
			for (auto& f : g_pfire) {
				f.t += a_delta;
				if (f.t < f.delay) continue;
				if (!f.fx.get()) {
					auto* p = RE::PlayerCharacter::GetSingleton();
					auto* base = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESBoundObject>(f.form ? f.form : gen::kPurpleFire, gen::kPlugin);
					auto  ref = p && base ? p->PlaceObjectAtMe(base, false) : nullptr;
					if (!ref) { f.t = 99.0f; continue; }
					ref->SetPosition(f.at);
					ref->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(f.scale, 0.1f, 9.0f) * 100.0f);
					f.fx = ref->CreateRefHandle();
				}
				if (f.t > f.delay + f.life) {
					if (auto r = f.fx.get()) { r->Disable(); r->SetDelete(true); }
					f.t = 99.0f;
				}
			}
			std::erase_if(g_pfire, [](const PFire& f) { return f.t >= 99.0f; });
		}

		float EaseOut(float x) { x = std::clamp(x, 0.0f, 1.0f); return 1 - (1 - x) * (1 - x) * (1 - x); }
		float EaseInOut(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }
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

#include "Spellcraft.inl"
		// (Noah, clips 300-303: the slashes, the Shadowflame cyclone, the heavy attacks' rings and pillars never showed - hung loose
		// in the scene the renderer drops them, as the flasks once were) one of our models standing in the world as a real placed
		// object (a nameless activator wearing it, made once per model), posed every frame where it should be
		RE::TESObjectACTI* ModelActivator(const std::string& a_model)
		{
			static std::unordered_map<std::string, RE::TESObjectACTI*> forms;
			auto& f = forms[a_model];
			if (!f) {
				if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESObjectACTI>(); factory && (f = factory->Create())) f->SetModel(a_model.c_str());
			}
			return f;
		}
		struct WorldFx
		{
			struct Piece { RE::ObjectRefHandle ref; FxPart part; bool drawn = false; float len = 0; std::chrono::steady_clock::time_point played{}; };
			std::vector<Piece> pieces;
			std::vector<FxPart> blasts;
			std::chrono::steady_clock::time_point born = std::chrono::steady_clock::now();
			std::string        model;
			bool               ok = false, drawn = false;
			WorldFx() = default;
			explicit WorldFx(std::string a_model) : model(std::move(a_model))
			{
				// (the effects map: Skyrim's own models, or an installed mod's, standing in for ours)
				for (auto& fp : FxParts(model)) {
					if (fp.none) { ok = true; continue; }
					if (fp.blast >= 0) { ok = true; blasts.push_back(fp); continue; }   // (a whole explosion: set off where it's first placed)
					if (Spellcraft::LoadPath(fp.model) && ModelActivator(fp.model)) pieces.push_back({ {}, std::move(fp) }), ok = true;   // (in memory before it's placed: drawn the first frame)
					else if (static int said = 0; said++ < 12) logger::warn("worldfx: {} (for {}) is missing", fp.model, model);
				}
			}
			RE::NiAVObject* Obj() const
			{
				for (auto& pc : pieces) if (auto r = pc.ref.get()) if (auto* o = r->Get3D()) return o;
				return nullptr;
			}
			template <class F> void Each(F&& a_fn) const
			{
				for (auto& pc : pieces) if (auto r = pc.ref.get()) if (auto* o = r->Get3D()) a_fn(o);
			}
			void Place(const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_scale)
			{
				if (!ok) return;
				for (auto& fp : blasts) {
					PendingBlast pb{ a_at + RE::NiPoint3{ 0, 0, fp.dz }, 0.0f, fp.blast, fp.scale };
					pb.exact = true;
					if (!fp.pal.empty() || fp.col.x >= 0) pb.form = TintableBlast(fp.blast);
					pb.part = std::make_shared<FxPart>(fp);
					g_blasts.push_back(pb);
				}
				blasts.clear();
				const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - born).count();
				for (auto& pc : pieces) {
					RE::NiPoint3 at = a_at + RE::NiPoint3{ 0, 0, pc.part.dz * a_scale };
					if (pc.part.orbitR > 0) {   // (a piece circling the middle - a tornado's flames wound round it)
						const float an = pc.part.phase * 6.2832f + pc.part.orbitSpeed * 6.2832f * age;
						at += RE::NiPoint3{ std::cos(an), std::sin(an), 0 } * (pc.part.orbitR * a_scale);
					}
					if (!pc.ref) {
						if (auto r = PlaceAtPoint(ModelActivator(pc.part.model), at)) pc.ref = r->GetHandle();
						else { if (static int said = 0; said++ < 8) logger::warn("worldfx: {} could not be placed", pc.part.model); continue; }
					}
					auto r = pc.ref.get();
					if (!r) continue;
					r->SetPosition(at);
					if (auto* o = r->Get3D()) {
						if (!pc.drawn) {
							pc.drawn = drawn = true;
							TintTree(o, pc.part);
							if (pc.part.play) pc.len = PlayOwn(r.get(), o), pc.played = std::chrono::steady_clock::now();
							if (pc.part.noparticles) RE::BSVisit::TraverseScenegraphObjects(o, [](RE::NiAVObject* x) {
								if (netimmerse_cast<RE::NiParticleSystem*>(x)) x->GetFlags().set(RE::NiAVObject::Flag::kHidden);
								return RE::BSVisit::BSVisitControl::kContinue;
							});
							if (static int said = 0; said++ < 40) logger::info("worldfx: {}{} drawn at ({:.0f},{:.0f},{:.0f}) scale {:.2f}", pc.part.model, pc.part.model == model ? "" : " (for " + model + ")", at.x, at.y, at.z, a_scale * pc.part.scale);
						}
						// (Noah: "the tornado disappears while the pillar stays") a model whose own animation plays once - Skyrim's
						// Cyclone whirlwind - set going again each time it ends, for as long as it's held
						if (pc.part.play && pc.len > 0.2f && std::chrono::duration<float>(std::chrono::steady_clock::now() - pc.played).count() > pc.len * 0.92f)
							PlayOwn(r.get(), o), pc.played = std::chrono::steady_clock::now();
						o->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw, RE::NiAVObject::Flag::kIgnoreFade);
						if (auto* fn = o->AsFadeNode()) fn->GetRuntimeData().currentFade = 1.0f;
						o->local.translate = at;
						o->local.rotate = a_rot * pc.part.rot;
						o->local.scale = std::max(0.0001f, a_scale * pc.part.scale);
						RE::NiUpdateData ud;
						ud.time = 0.0f;
						o->Update(ud);
					}
				}
			}
			void Detach()
			{
				for (auto& pc : pieces) if (auto r = pc.ref.get()) r->Disable(), r->SetDelete(true);
				for (auto& pc : pieces) pc.ref = {};
			}
		};
		namespace Props
		{
			void Tornado(int a_kind, const RE::NiPoint3& a_at, RE::NiPoint3 a_dir, float a_seconds, float a_damage, bool a_chase, int a_row);
		}
		// ---- lingering blows on the ground (the meteors' strikes, the Thu'um's shadow and singularity) and the frozen
		struct Zone
		{
			int          kind;  // 4 a strike (once), 5 Shadow Step's shadow, 6 Arcane Singularity
			RE::NiPoint3 at;
			float        r, left, tick, dmg, fx;
			bool         hunt;
			float        crush;  // 5 / 6: seconds until it bursts (then it ends)
		};
		std::vector<Zone> g_zones;
		std::unordered_map<RE::FormID, float> g_frozenUntil;  // frozen solid until (seconds on g_zoneClock)
		float g_zoneClock = 0;
		void FreezeSolid(RE::Actor* a, float a_s)
		{
			g_frozenUntil[a->GetFormID()] = g_zoneClock + a_s;
			Director::Web(a->GetPosition(), 45.0f, a_s, 97.0f, true);
		}
		void PullTo(const RE::NiPoint3& c, float r, float strength)
		{
			for (auto* f : Foes(c, r)) {
				RE::NiPoint3 off = f->GetPosition() - c; off.z = 0;
				if (off.Length() > 90.0f) Spellcraft::Fling(f, c, -strength);
			}
		}
		void TickZones(float a_delta)
		{
			g_zoneClock += a_delta;
			if (g_frozenUntil.size() > 64) std::erase_if(g_frozenUntil, [](const auto& e) { return e.second < g_zoneClock; });
			for (auto& z : g_zones) {
				z.left -= a_delta;
				z.fx -= a_delta;
				const bool tick = (z.tick -= a_delta) <= 0;
				switch (z.kind) {
				case 4:  // a meteor's strike: once
					if (tick && z.tick > -1.0f) {
						z.tick = -100.0f;
						Blast(BlastIndex("firestorm"), z.at + RE::NiPoint3{ 0, 0, 20.0f }, 0.0f, z.r > 300.0f ? 1.0f : 0.6f);
						for (auto* f : Foes(z.at, z.r)) { if (!Damage(f, z.dmg, false, false, true)) Stagger(f, 0.8f); }
						z.left = 0;
					}
					break;
				case 5:  // Shadow Step's shadow: draws the dead to it, then bursts
				case 6:  // Arcane Singularity: pulls them in, then detonates
					z.crush -= a_delta;
					if (z.fx <= 0) { z.fx = 0.35f; Blast(BlastIndex(z.kind == 5 ? "dark" : "constellation"), z.at + RE::NiPoint3{ 0, 0, 50.0f }, 0.0f, 0.45f); }
					if (tick) { z.tick = 0.3f; PullTo(z.at, z.r, z.kind == 5 ? 0.9f : 1.5f); }
					if (z.crush <= 0) {
						Blast(BlastIndex(z.kind == 5 ? "darkmassive" : "lightmassive"), z.at + RE::NiPoint3{ 0, 0, 50.0f }, 0.0f, 0.9f);
						for (auto* f : Foes(z.at, 340.0f)) { if (!Damage(f, z.dmg, false, false, true)) Stagger(f, 1.0f); }
						z.left = 0;
					}
					break;
				default: z.left = 0; break;
				}
			}
			std::erase_if(g_zones, [](const Zone& z) { return z.left <= 0; });
		}
#include "WonderProps.inl"

		// (Noah: "an underhand throwing animation"; "realistic throwing animations and sound fx") a flask thrown underhand, the
		// whole body in it: the arm drawn back and down with the elbow bent, the wrist cocked and the shoulders turned with it,
		// then the arm swung through - the shoulders turning back, the elbow straightening - and the wrist flicked as the flask
		// leaves the hand ahead of you; a follow-through up and out, then home. The glass clinks as it is drawn back, the air
		// whips past as it goes. Laid over the animation in the body you're seen in; the flask in the hand hidden from the
		// release until the next is in it
		struct ThrowPose { bool on = false, left = false, released = false, clinked = false, whooshed = false; float t = 0; std::function<void()> release; };
		ThrowPose g_throw;
		constexpr float kThrowLen = 0.95f, kThrowRelease = 0.46f;   // (Noah: "a little more weight" - a slower draw back, a heavier swing)
		float Ease(float k) { k = std::clamp(k, 0.0f, 1.0f); return k * k * (3.0f - 2.0f * k); }
		// the throw's channels at time t: the upper arm about your right (+ forward and up), the elbow's bend, the wrist's
		// cock (- back, + the flick), the shoulders' turn (+ toward the throwing side, back)
		struct ThrowKeys { float arm, elbow, wrist, turn; };
		ThrowKeys ThrowAt(float t)
		{
			constexpr float tBack = 0.30f, tRel = kThrowRelease, tFollow = 0.62f;
			if (t < tBack) {             // the draw back: arm back and down, elbow bending, wrist cocked, shoulders turning
				const float k = Ease(t / tBack);
				return { -1.05f * k, 0.55f * k, -0.55f * k, 0.32f * k };
			}
			if (t < tRel) {              // the swing through, quickening: arm forward, elbow straightening, wrist flicking at the end
				const float k = (t - tBack) / (tRel - tBack), e = k * k;
				return { -1.05f + 2.25f * Ease(k), 0.55f - 0.5f * Ease(k), -0.55f + 1.0f * e, 0.32f - 0.52f * Ease(k) };
			}
			if (t < tFollow) {           // the follow-through: up and out, the wrist loose
				const float k = Ease((t - tRel) / (tFollow - tRel));
				return { 1.2f + 0.25f * k, 0.05f + 0.2f * k, 0.45f - 0.15f * k, -0.2f - 0.06f * k };
			}
			const float k = Ease((t - tFollow) / (kThrowLen - tFollow));   // and home
			return { 1.45f * (1 - k), 0.25f * (1 - k), 0.3f * (1 - k), -0.26f * (1 - k) };
		}
		// (Noah: "im seeing after images of the potion in my hand") a frame the animation does not rewrite a bone leaves our
		// last turn on it - turned again on top, the hand jumped between two poses frame by frame. Every bone we turn is kept
		// with what it was before us and what we left it: found as we left it, it is put back first (by name, in the body it
		// is in - never a stale pointer)
		struct KeptRot { std::string name; bool first; RE::NiMatrix3 orig, written; };
		std::unordered_map<RE::NiAVObject*, KeptRot> g_kept, g_keptFrame;
		bool g_turnFirst = false;
		void PoseBegin(RE::PlayerCharacter* a_player)
		{
			RE::NiUpdateData ud;
			ud.time = 0.0f;
			for (auto& [bone, k] : g_kept) {
				auto* root = a_player->Get3D(k.first);
				auto* now = root ? root->GetObjectByName(k.name) : nullptr;
				if (now != bone) continue;
				if (std::memcmp(&bone->local.rotate, &k.written, sizeof(RE::NiMatrix3)) == 0) {
					bone->local.rotate = k.orig;
					bone->UpdateDownwardPass(ud, 0);
				}
			}
			g_keptFrame.clear();
		}
		void PoseEnd()
		{
			for (auto& [bone, k] : g_keptFrame) k.written = bone->local.rotate;
			g_kept.swap(g_keptFrame);
			g_keptFrame.clear();
		}
		void TurnBone(RE::NiAVObject* a_bone, const RE::NiMatrix3& a_world)
		{
			if (!a_bone || !a_bone->parent) return;
			if (!g_keptFrame.contains(a_bone)) g_keptFrame[a_bone] = { a_bone->name.c_str(), g_turnFirst, a_bone->local.rotate, {} };
			const RE::NiMatrix3 P = a_bone->parent->world.rotate;
			a_bone->local.rotate = P.Transpose() * a_world * P * a_bone->local.rotate;
			RE::NiUpdateData ud;
			ud.time = 0.0f;
			a_bone->UpdateDownwardPass(ud, 0);
		}
		RE::NiPoint3 BoneZ(const RE::NiAVObject* o) { const auto& r = o->world.rotate; return { r.entry[0][2], r.entry[1][2], r.entry[2][2] }; }
		void PoseThrow(RE::PlayerCharacter* a_player)
		{
			if (!g_throw.on) return;
			auto* cam = RE::PlayerCamera::GetSingleton();
			const bool first = cam && cam->IsInFirstPerson();
			auto* root = a_player->Get3D(first);
			if (!root) return;
			g_turnFirst = first;
			const ThrowKeys k = ThrowAt(g_throw.t);
			const float yaw = a_player->data.angle.z;
			const RE::NiPoint3 r{ std::cos(yaw), -std::sin(yaw), 0 };
			const RE::NiPoint3 up{ 0, 0, 1 };
			const bool L = g_throw.left;
			const float side = L ? -1.0f : 1.0f;
			const float m = first ? 0.8f : 1.0f;
			// the shoulders' turn (third person: the first-person body is only arms)
			if (!first) TurnBone(root->GetObjectByName("NPC Spine2 [Spn2]"), AxisAngle(up, side * k.turn * 0.6f));
			auto* ua = root->GetObjectByName(L ? "NPC L UpperArm [LUar]" : "NPC R UpperArm [RUar]");
			auto* fa = root->GetObjectByName(L ? "NPC L Forearm [LLar]" : "NPC R Forearm [RLar]");
			auto* hd = root->GetObjectByName(L ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]");
			TurnBone(ua, AxisAngle(r, k.arm * m));
			if (fa && hd) {
				// the elbow bends about the line across the arm's own bend (or your right, the arm straight)
				RE::NiPoint3 hinge = r;
				if (ua) {
					RE::NiPoint3 x = BoneZ(ua).Cross(BoneZ(fa));
					if (x.Length() > 0.05f) hinge = x * (1.0f / x.Length());
				}
				TurnBone(fa, AxisAngle(hinge, k.elbow * m));   // (about the bend's own line: more bend - it bends only one way)
				TurnBone(hd, AxisAngle(r, k.wrist * m));
			}
			// the flask in that hand: hidden from the release until the throw is over (the next one is in your hand)
			for (const bool p1 : { false, true }) {
				auto* rt = a_player->Get3D(p1);
				if (auto* node = rt ? rt->GetObjectByName(g_throw.left ? "SHIELD" : "WEAPON") : nullptr) node->SetAppCulled(g_throw.released && g_throw.t < kThrowLen);
			}
		}
		// (Noah: "my players hands should be holding the flask from the circle bowl part of the flask not the snout/cork area. the
		// players palm should be facing up") a flask drawn sits upright in an open hand turned palm up: the forearm rolled over
		// (most of the turn in the forearm, the rest in the wrist and the twist bone between, so the skin turns with it), the
		// fingers eased open round the bottle's belly, and the bottle set on the palm, its neck up out of it
		constexpr bool kPalmHold = false;
		float g_holdW[2]{};
		const char* HandBone(bool a_left, const char* a_what)
		{
			static std::unordered_map<std::string, std::string> names;
			const std::string key = std::string(a_left ? "L" : "R") + a_what;
			auto it = names.find(key);
			if (it != names.end()) return it->second.c_str();
			const char S = a_left ? 'L' : 'R';
			std::string n;
			const std::string w = a_what;
			if (w == "fa") n = fmt::format("NPC {} Forearm [{}Lar]", S, S);
			else if (w == "hand") n = fmt::format("NPC {} Hand [{}Hnd]", S, S);
			else if (w == "twist") n = fmt::format("NPC {} ForearmTwist1 [{}Lt1]", S, S);
			else n = fmt::format("NPC {} Finger{} [{}F{}]", S, w, S, w);   // (w: "20" - the finger, the joint)
			return names.emplace(key, n).first->second.c_str();
		}
		// the palm's side of the hand: the hand bone's own -Y (both hands, both bodies - the skeleton's rest has each palm turned
		// in to the thigh along it), its fingers along its +Z
		// (Noah: "the potions in hand are broken" - read off the fingers' curl it came out sideways: the bottle lay on its side)
		bool PalmNormal(RE::NiAVObject* a_root, bool a_left, RE::NiPoint3& a_n, RE::NiPoint3& a_fingers, RE::NiPoint3& a_centre)
		{
			auto* hand = a_root->GetObjectByName(HandBone(a_left, "hand"));
			auto* f1 = a_root->GetObjectByName(HandBone(a_left, "10"));
			auto* f2 = a_root->GetObjectByName(HandBone(a_left, "20"));
			auto* f4 = a_root->GetObjectByName(HandBone(a_left, "40"));
			if (!hand || !f1 || !f2 || !f4) return false;
			const auto& r = hand->world.rotate;
			RE::NiPoint3 n{ -r.entry[0][1], -r.entry[1][1], -r.entry[2][1] };
			RE::NiPoint3 ax{ r.entry[0][2], r.entry[1][2], r.entry[2][2] };
			if (n.Length() < 1e-3f || ax.Length() < 1e-3f) return false;
			n.Unitize(), ax.Unitize();
			a_n = n, a_fingers = ax;
			const RE::NiPoint3 knuckles = (f1->world.translate + f2->world.translate + f4->world.translate) * (1.0f / 3.0f);
			a_centre = hand->world.translate * 0.45f + knuckles * 0.55f + n * (1.2f * hand->world.scale);
			return true;
		}
		void PoseFlaskHand(RE::PlayerCharacter* a_player, float a_delta)
		{
			for (const bool L : { false, true }) {
				const bool want = Classes::FlaskInHand(L) && !Movement::Gliding();
				float& w = g_holdW[L ? 1 : 0];
				w = std::clamp(w + (want ? 1.0f : -1.0f) * a_delta * 5.0f, 0.0f, 1.0f);
				if (w <= 0.001f) continue;
				const float ww = Ease(w);
				for (const bool first : { false, true }) {
					auto* root = a_player->Get3D(first);
					if (!root) continue;
					g_turnFirst = first;
					auto* fa = root->GetObjectByName(HandBone(L, "fa"));
					auto* hand = root->GetObjectByName(HandBone(L, "hand"));
					if (!fa || !hand) continue;
					RE::NiPoint3 n, fingers, centre;
					if (!PalmNormal(root, L, n, fingers, centre)) continue;
					// the roll: the palm turned to face up, about the forearm's line
					RE::NiPoint3 a = hand->world.translate - fa->world.translate;
					if (a.Length() > 1e-3f) {
						a.Unitize();
						const RE::NiPoint3 U{ 0, 0, 1 };
						RE::NiPoint3 up = U - a * a.Dot(U), np = n - a * a.Dot(n);
						if (up.Length() > 0.3f && np.Length() > 0.3f) {
							up.Unitize(), np.Unitize();
							float phi = std::atan2(a.Dot(np.Cross(up)), np.Dot(up));
							phi = std::clamp(phi, -2.6f, 2.6f) * ww;
							TurnBone(fa, AxisAngle(a, phi * 0.6f));
							TurnBone(root->GetObjectByName(HandBone(L, "twist")), AxisAngle(a, phi * 0.2f));
							TurnBone(hand, AxisAngle(a, phi * 0.4f));
						}
					}
					// the fingers eased open round the bottle (the knuckles a little, the joints past them more), the thumb a touch
					for (int i = 0; i < 5; ++i) {
						for (int j = 0; j < 3; ++j) {
							const char id[3] = { static_cast<char>('0' + i), static_cast<char>('0' + j), 0 };
							auto* bone = root->GetObjectByName(HandBone(L, id));
							if (!bone) continue;
							RE::NiAVObject* prev = hand;
							if (j > 0) {
								const char pid[3] = { static_cast<char>('0' + i), static_cast<char>('0' + j - 1), 0 };
								prev = root->GetObjectByName(HandBone(L, pid));
							}
							if (!prev) continue;
							const RE::NiPoint3 pd = BoneZ(prev), sd = BoneZ(bone);
							RE::NiPoint3 ax = pd.Cross(sd);
							const float sn = ax.Length();
							if (sn < 1e-3f) continue;
							ax = ax * (1.0f / sn);
							const float ang = std::atan2(sn, pd.Dot(sd));
							const float k = (i == 0 ? 0.45f : j == 0 ? 0.65f : 0.85f) * ww;   // (Noah: "open palming the flask bottle not closed in a fist")
							TurnBone(bone, AxisAngle(ax, -ang * k));
						}
					}
				}
			}
		}
		// the bottle on the palm (after the throw's swing, so it rides the hand through it): its base where the palm is, its
		// neck up out of the hand - the held model hangs from its neck (tools/flasks.py), its base this far down it
		float FlaskBase(int a_el) { return a_el == 6 ? 15.76f : a_el == 0 ? 11.56f : 10.67f; }
		void PlaceHeldFlask(RE::PlayerCharacter* a_player)
		{
			for (const bool L : { false, true }) {
				const float w = g_holdW[L ? 1 : 0];
				if (w <= 0.001f) continue;
				const int el = Classes::FlaskElementInHand(L);
				if (el < 0) continue;
				const float ww = Ease(w);
				for (const bool first : { false, true }) {
					auto* root = a_player->Get3D(first);
					auto* node = root ? root->GetObjectByName(L ? "SHIELD" : "WEAPON") : nullptr;
					auto* nn = node ? node->AsNode() : nullptr;
					if (!nn) continue;
					RE::NiPoint3 n, fingers, centre;
					if (!PalmNormal(root, L, n, fingers, centre)) continue;
					RE::NiPoint3 z = fingers - n * fingers.Dot(n);
					if (z.Length() < 1e-3f) continue;
					z.Unitize();
					const RE::NiPoint3 x = n.Cross(z);
					RE::NiMatrix3 R;   // (the model's axes in the world: its +Y the bottle's neck)
					R.entry[0][0] = x.x, R.entry[1][0] = x.y, R.entry[2][0] = x.z;
					R.entry[0][1] = n.x, R.entry[1][1] = n.y, R.entry[2][1] = n.z;
					R.entry[0][2] = z.x, R.entry[1][2] = z.y, R.entry[2][2] = z.z;
					const auto& W = nn->world;
					const float ws = std::max(0.01f, W.scale);
					const RE::NiPoint3 at = centre + n * (FlaskBase(el) * ws);
					const RE::NiMatrix3 Wt = W.rotate.Transpose();
					const RE::NiMatrix3 lr = Wt * R;
					const RE::NiPoint3 lt = (Wt * (at - W.translate)) * (1.0f / ws);
					for (auto& kid : nn->GetChildren()) {
						if (!kid) continue;
						kid->local.rotate = ww >= 0.999f ? lr : Movement::SlerpRot(RE::NiMatrix3{}, lr, ww);
						kid->local.translate = lt * ww;
						kid->local.scale = 1.0f;
						RE::NiUpdateData ud;
						ud.time = 0.0f;
						kid->Update(ud);
					}
				}
			}
		}
		void TickThrow(float a_delta)
		{
			if (!g_throw.on) return;
			g_throw.t += a_delta;
			// (Noah: "the throwing sound also sounds like a blade cutting through the air like a dagger. make the throw sound a cork
			// popping") the cork pulled as the flask is drawn back - no blade's whoosh
			if (!g_throw.clinked) {
				g_throw.clinked = true;
				if (auto* pl = RE::PlayerCharacter::GetSingleton()) Audio::At(gen::Sound::alch_corkpop, pl, 0.9f);
			}
			if (!g_throw.released && g_throw.t >= kThrowRelease) {
				g_throw.released = true;
				if (g_throw.release) g_throw.release();
			}
			if (g_throw.t >= kThrowLen + 0.05f) {
				auto* p = RE::PlayerCharacter::GetSingleton();
				for (const bool p1 : { false, true }) {
					auto* rt = p ? p->Get3D(p1) : nullptr;
					if (auto* node = rt ? rt->GetObjectByName(g_throw.left ? "SHIELD" : "WEAPON") : nullptr) node->SetAppCulled(false);
				}
				g_throw = {};
			}
		}
		void StartThrowImpl(bool a_left, std::function<void()> a_release)
		{
			if (g_throw.on) return;
			g_throw.on = true, g_throw.left = a_left, g_throw.released = false, g_throw.t = 0, g_throw.release = std::move(a_release);
		}
		bool ThrowingImpl() { return g_throw.on; }

		struct PoseHook
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				try {
						PoseBegin(a_this);
					// (Noah: "the new potion holding is still broken lets go back to holding the flask snout") the flask in the fist
					// by its neck again (tools/flasks.py: the neck drawn out to fill the hand) - the palm-up hold is set aside
					if constexpr (kPalmHold) PoseFlaskHand(a_this, a_delta);
					PoseThrow(a_this);
					PoseEnd();
					if constexpr (kPalmHold) PlaceHeldFlask(a_this);
					Movement::PoseGlideArms(a_this);
					Movement::SwingBody(a_this);
					Classes::PoseWand(a_this);
				} catch (...) {
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};




	}

	namespace Gun
	{
		RE::NiPointer<RE::NiAVObject> LoadModel(const std::string& a_path) { return Spellcraft::LoadPath(a_path); }
		// (the Tablet's donated weapons, src/Tablet.cpp) the zones' and the blows' own pieces, for others
		std::vector<RE::Actor*> FoesNear(const RE::NiPoint3& a_at, float a_radius) { return Foes(a_at, a_radius); }
		void HitShare(RE::Actor* a_target, float a_share) { if (a_target && !a_target->IsDead()) Damage(a_target, Director::RoundHealth() * gen::kRounds.hpScale * a_share, false, false, true); }
		void Freeze(RE::Actor* a_target, float a_seconds) { if (a_target && !a_target->IsDead()) FreezeSolid(a_target, a_seconds); }
		// (Noah: "the heavy attack for Sealbreaker doesn't do anything" - with Precision and MCO the hit's power-attack flag never
		// came: no "(heavy)" in the log all night) a power attack told by the swing your body is making
		bool PlayerPowerAttack() { auto* p = RE::PlayerCharacter::GetSingleton(); return p && PowerAttacking(p); }
		void Singularity(const RE::NiPoint3& a_at, float a_radius, float a_crush, float a_share, bool a_dark)
		{
			g_zones.push_back({ a_dark ? 5 : 6, a_at, a_radius, a_crush + 0.5f, 0.0f, Director::RoundHealth() * gen::kRounds.hpScale * a_share, 0.0f, false, a_crush });
		}
		void  FxBlast(std::string_view a_name, const RE::NiPoint3& a_at, float a_delay) { Blast(BlastIndex(a_name), a_at, a_delay, 0.6f); }
		void  FxBlastSized(std::string_view a_name, const RE::NiPoint3& a_at, float a_scale) { Blast(BlastIndex(a_name), a_at, 0.0f, a_scale); }
		void  FxFrom(RE::BGSExplosion* a_src, const RE::NiPoint3& a_at, float a_scale)
		{
			if (auto* q = QuietClone(a_src); q && g_blasts.size() < 32) g_blasts.push_back({ a_at, 0.0f, -1, a_scale, q });
		}
		RE::BGSExplosion* Quiet(RE::BGSExplosion* a_src) { return QuietClone(a_src); }
		// (Noah: "use the spell sound effect that the potion is casting - if it's fire it'll sound like fire") a Skyrim spell
		// explosion's own sound (its impact: the fireball's roar, the frost's crackle, the rune's thunder), carried on you at a
		// volume set by how far away it went off (a sound set at a bare point played to no one)
		void  BlastSound(std::string_view a_name, const RE::NiPoint3& a_at, float a_vol)
		{
			auto* ex = BlastForm(BlastIndex(a_name));
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!ex || !p) return;
			const float d = p->GetPosition().GetDistance(a_at);
			const float v = std::clamp(1.0f - (d - 350.0f) / 2400.0f, 0.25f, 1.0f) * a_vol;
			for (auto* snd : { ex->data.sound1, ex->data.sound2 }) if (snd) Audio::Game(snd->GetFormID(), p, std::min(1.0f, v));
		}
		// (the class perks: Ember Heart) the night's dead round a point lose a share of a whole zombie's health
		// (Noah: the Alchemist's Shadowflame mace) a burn: a_perSecond of damage for a_seconds, the Gun's own damage-over-time
		void  Burn(RE::Actor* a_target, float a_perSecond, float a_seconds, int a_fec)
		{
			if (a_target && !a_target->IsDead() && a_perSecond > 0 && a_seconds > 0) g_dots.push_back({ a_target->GetHandle(), a_perSecond, a_seconds, a_fec });
		}

		void SetGauntlet(const GauntletState& a_s) { Props::g_gs = a_s; }
		bool GauntletBeam(int a_el, float a_width, RE::NiPoint3& a_from, RE::NiPoint3& a_to)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->cameraRoot) return false;
			RE::NiPoint3 palm;
			if (!Props::PalmOrb(palm)) return false;
			// toward what the crosshair is on: along your view to the first wall or ground, 40 m at most
			const auto& cw = cam->cameraRoot->world;
			RE::NiPoint3 f{ cw.rotate.entry[0][1], cw.rotate.entry[1][1], cw.rotate.entry[2][1] };
			f.Unitize();
			const RE::NiPoint3 eye = cw.translate, farPt = eye + f * 2800.0f;
			const float hit = Spellcraft::WorldHit(eye + f * 40.0f, farPt);
			RE::NiPoint3 to = hit <= 1.0f ? (eye + f * 40.0f) + (farPt - (eye + f * 40.0f)) * hit : farPt;
			// (from the palm: no wall between it and the hand's own reach)
			a_from = palm, a_to = to;
			if (a_width > 0.0f) Props::GBeamLay(a_el, palm, to, a_width);   // (none drawn: the staff's own beam shows)
			return true;
		}
		bool StaffBeam(int a_el, float a_width, const RE::NiPoint3& a_from, RE::NiPoint3& a_to)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->cameraRoot) return false;
			const auto& cw = cam->cameraRoot->world;
			RE::NiPoint3 f{ cw.rotate.entry[0][1], cw.rotate.entry[1][1], cw.rotate.entry[2][1] };
			f.Unitize();
			const RE::NiPoint3 eye = cw.translate, farPt = eye + f * 2800.0f;
			const float hit = Spellcraft::WorldHit(eye + f * 40.0f, farPt);
			a_to = hit <= 1.0f ? (eye + f * 40.0f) + (farPt - (eye + f * 40.0f)) * hit : farPt;
			if (a_width > 0.0f) Props::GBeamLay(a_el, a_from, a_to, a_width);
			return true;
		}
		void GauntletBeamOff() { Props::GBeamOff(); }
		std::vector<RE::Actor*> LineHurt(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_radius, float a_share)
		{
			std::vector<RE::Actor*> out;
			const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
			const RE::NiPoint3 mid = (a_from + a_to) * 0.5f;
			const float half = (a_to - a_from).Length() * 0.5f;
			for (auto* f : Foes(mid, half + a_radius + 100.0f)) {
				if (!f || f->IsDead()) continue;
				const RE::NiPoint3 c = f->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f };
				const float d = std::min({ Spellcraft::PointSeg(c, a_from, a_to), Spellcraft::PointSeg(f->GetPosition() + RE::NiPoint3{ 0, 0, 20.0f }, a_from, a_to),
					Spellcraft::PointSeg(f->GetPosition() + RE::NiPoint3{ 0, 0, 120.0f }, a_from, a_to) });
				if (d > a_radius + 30.0f) continue;   // (a body's own girth: about 30)
				Damage(f, whole * a_share, false, false, true);
				out.push_back(f);
			}
			return out;
		}
		void  AreaHurt(const RE::NiPoint3& a_at, float a_radius, float a_share, std::string_view a_blast)
		{
			const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
			for (auto* f : Foes(a_at, a_radius)) {
				Damage(f, whole * a_share, false, false, true);
			}
			if (!a_blast.empty()) Blast(BlastIndex(a_blast), a_at, 0.0f, 0.45f);
		}
		// (Noah: "the dragon's current attacks are invisible ... the lingering acid is invisible") the bursts placed as world
		// objects never showed: they are hung in the scene by hand now (as the spells' and the bows' own looks are), each
		// its model, its place and scale, gone when its time is up
		struct EBurst { Spellcraft::Fx fx; std::string path; RE::NiPoint3 at; float t = 0, delay = 0, life = 1, scale = 1; bool flat = false; };
		std::vector<EBurst> g_eb;
		void QueueFx(std::string a_path, const RE::NiPoint3& a_at, float a_scale, float a_delay, float a_life, bool a_flat)
		{
			// a burst: an explosion wearing our model; a pool: a hazard lying its lifetime (see ModelExplosion) - or the
			// stand-ins named for it in the effects map, each its own
			const auto parts = FxParts(a_path);
			bool placed = false;
			for (const auto& fp : parts) {
				if (fp.none) { placed = true; continue; }
				const RE::NiPoint3 at = a_at + RE::NiPoint3{ 0, 0, fp.dz * a_scale };
				const float sc = a_scale * fp.scale;
				auto part = std::make_shared<FxPart>(fp);
				if (fp.blast >= 0) {
					if (g_blasts.size() < 48) {
						PendingBlast pb{ at, a_delay, fp.blast, sc };
						pb.exact = true;
						pb.part = part;
						if (!fp.pal.empty() || fp.col.x >= 0) pb.form = TintableBlast(fp.blast);   // (Noah: "make sure all the flames are purple")
						g_blasts.push_back(pb);
					}
					placed = true;
				} else if (!a_flat) {
					if (auto* ex = ModelExplosion(fp.model); ex && g_blasts.size() < 48) {
						PendingBlast pb{ at, a_delay, -1, sc, ex };
						pb.exact = true;
						pb.part = part;
						g_blasts.push_back(pb);
						placed = true;
					}
				} else if (auto* hz = ModelHazard(fp.model, a_life)) {
					auto place = [hz, at, sc, part]() {
						if (auto r = PlaceAtPoint(hz, at)) r->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(sc, 0.1f, 9.0f) * 100.0f), WatchTint(r.get(), *part, true);
					};
					if (a_delay <= 0) place(); else Tasks::Later(a_delay, place);
					placed = true;
				}
			}
			if (placed) return;
			if (g_eb.size() > 90) return;  // (never a screen of it)
			EBurst b;
			b.path = std::move(a_path), b.at = a_at, b.delay = a_delay, b.life = a_life, b.scale = a_scale, b.flat = a_flat;
			g_eb.push_back(std::move(b));
		}
		void TickEBursts(float a_delta)
		{
			for (auto& b : g_eb) {
				b.t += a_delta;
				if (b.t < b.delay) continue;
				if (!b.fx.obj) {
					b.fx = Spellcraft::Fx{ Spellcraft::LoadPath(b.path) };
					if (!b.fx.obj) { b.t = 1.0e9f; continue; }
					b.fx.Attach(Spellcraft::WorldRoot());
					b.fx.Place(b.at, RE::NiMatrix3{}, b.scale);
				}
				if (b.t > b.delay + b.life) {
					b.fx.Detach();
					b.t = 1.0e9f;
				}
			}
			std::erase_if(g_eb, [](const EBurst& b) { return b.t >= 1.0e9f; });
		}
		// (Noah: "change the belt lantern to a small golden orb that sits just above my head and lights up my surroundings within
		// 15 meters ... animated circulating and showing light beams, visually lighting up my surroundings") the Light Orb:
		// its model (tools/lightorb.py) hung in the scene a little over your head, drifting round in a slow circle and bobbing,
		// and the lantern's real light (shadow-casting, 15 m) carried inside it
		struct LightOrb { Spellcraft::Fx fx; RE::NiPointer<RE::NiLight> light; bool want = false; float t = 0; RE::NiPoint3 at; bool placed = false; };
		LightOrb g_orb;
		void OrbDown()
		{
			auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
			if (g_orb.light) {
				if (ssn) ssn->RemoveLight(g_orb.light.get());
				if (auto* parent = g_orb.light->parent) parent->DetachChild(g_orb.light.get());
				g_orb.light.reset();
			}
			g_orb.fx.Detach();
			g_orb.fx = {};
			g_orb.placed = false;
		}
		// (Noah: the mace's crystal "to have purple flames that grow stronger each upgrade ... the Madness Sword should also have this
		// same effect style ... a dark red flame surrounding the blade") the class weapon in your right hand, drawn, burns: its
		// tier's fire (tools/bladefx.py) hung on the weapon itself - its WEAPON node, the weapon's own space - in both views
		struct WeaponFlame { Spellcraft::Fx fx[2]; std::string path; };
		WeaponFlame g_wflame;
		// (Noah: "tier 1 sends out arc slashes from the blade ... that pierce enemies, swing in the color of the sword"; the
		// Bane's "white arc aligning with the blade swing angle") a crescent flying out from a swing: placed in the world each
		// frame (tools/arcfx.py's models, hung on your body's root), turned to the angle the blade swept, cutting through every
		// one of the dead it passes - each once - then fading at the end of its run
		struct LiveSlash { WorldFx fx; Gun::SlashSpec s; RE::NiPoint3 at; float gone = 0, wait = 0.08f; std::unordered_set<RE::FormID> hit; std::unordered_map<RE::BSEffectShaderMaterial*, float> base; };
		std::vector<LiveSlash> g_slashes;
		RE::NiMatrix3 RotY(float a)
		{
			RE::NiMatrix3 m;
			const float c = std::cos(a), sn = std::sin(a);
			m.entry[0][0] = c, m.entry[0][1] = 0, m.entry[0][2] = sn;
			m.entry[1][0] = 0, m.entry[1][1] = 1, m.entry[1][2] = 0;
			m.entry[2][0] = -sn, m.entry[2][1] = 0, m.entry[2][2] = c;
			return m;
		}
		RE::NiMatrix3 RotZ(float a)
		{
			RE::NiMatrix3 m;
			const float c = std::cos(a), sn = std::sin(a);
			m.entry[0][0] = c, m.entry[0][1] = -sn, m.entry[0][2] = 0;
			m.entry[1][0] = sn, m.entry[1][1] = c, m.entry[1][2] = 0;
			m.entry[2][0] = 0, m.entry[2][1] = 0, m.entry[2][2] = 1;
			return m;
		}
		RE::NiMatrix3 SlashTurn(const LiveSlash& l)
		{
			RE::NiMatrix3 r = Spellcraft::Facing(l.s.dir) * RotY(l.s.roll);
			if (l.s.sideways) r = r * RotZ(-1.5708f);   // (a blade laid across its flight: the spectral greatsword sweeping)
			return r;
		}
		// who a slash cuts: the night's dead and real enemies, and anyone else who is fighting you - never your own side
		bool SlashTarget(RE::Actor* a, RE::PlayerCharacter* p)
		{
			if (!a || a == p || a->IsDead() || !a->Is3DLoaded() || a->IsPlayerTeammate() || Party::IsFriend(a)) return false;
			if (a->IsCommandedActor() && a->GetCommandingActor().get() == p) return false;
			if (Director::IsFoe(a)) return true;
			if (a->IsHostileToActor(p)) return true;
			auto t = a->GetActorRuntimeData().currentCombatTarget.get();
			return t && t.get() == p;
		}
		// (Noah: "the slash from the weapons looks strange when sending it at the ground and when moving around while sending the
		// slash. It needs to feel like a real magic slash that breaks on impact with the ground or walls but goes through enemies
		// and has infinite pierce damage through enemies or anyone trying to fight my character") each slash flies in the world
		// itself (not hung on your body, so your own running never drags it along), leaves from where the blade is at the moment it
		// leaves, cuts every one it passes through - no limit - and shatters where it meets the ground, a wall or a rock
		void TickSlashes(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* world = Spellcraft::WorldRoot();
			const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
			for (auto& l : g_slashes) {
				if (!world || !p || !p->Is3DLoaded()) { l.gone = l.s.range + 1; continue; }
				// (a breath into the swing before it leaves the blade: its angle is the sweep the blade really made, and it sets
				// out from where you are now, not where you were when the swing began)
				if (l.wait > 0) {
					if ((l.wait -= a_delta) > 0) continue;
					l.s.roll = SwingRoll(l.s.dir);
					if (!l.s.sideways && !l.s.keepFrom) l.at = SwingFrom();
				}
				if (!l.fx.ok) {
					l.fx = WorldFx(l.s.model);
					if (!l.fx.ok) { l.gone = l.s.range + 1; continue; }
				}
				// (Noah: "a little faster ... stay the same shape the whole flight time then fades out") its size held the whole
				// way, its light dying over the last quarter
				const float step = l.s.speed * 1.35f * a_delta;
				const RE::NiPoint3 from = l.at;
				RE::NiPoint3 to = l.at + l.s.dir * step;
				// the ground or a wall in the way: it breaks there (after the first few centimetres - never on the blade itself)
				bool broke = false, stopped = false;
				if (l.gone > 20.0f) {
					if (const float wf = Spellcraft::WorldHit(from, to + l.s.dir * 25.0f); wf <= 1.0f) {
						to = from + (to + l.s.dir * 25.0f - from) * wf;
						broke = true;
						if (static int said = 0; said++ < 20) logger::info("slash: shattered on the world {:.0f} units out", l.gone);
					}
				}
				// (Noah: "make it smaller then grow in size before it disappears") it leaves the blade a third of its size and grows
				// as it flies, quickly at first; it cuts as wide as it's drawn
				const float f0 = std::min(1.0f, l.gone / std::max(1.0f, l.s.range));
				const float grow = l.s.bolt ? 1.0f : 0.32f + 0.83f * (1.0f - (1.0f - f0) * (1.0f - f0));   // (a bolt: its size from the first frame - a third of a small bolt was never seen)
				const float reach = l.s.radius * (0.45f + 0.55f * grow);
				// anyone in its way who isn't fighting you (a guard, a passer-by): it breaks on them as on a wall - only your
				// own side lets it through
				if (!broke) {
					for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
						auto ap = h.get();
						auto* a = ap.get();
						if (!a || a == p || a->IsDead() || !a->Is3DLoaded() || SlashTarget(a, p)) continue;
						if (a->IsPlayerTeammate() || Party::IsFriend(a) || Classes::IsOwnSummon(a) || (a->IsCommandedActor() && a->GetCommandingActor().get() == p)) continue;
						const RE::NiPoint3 c = a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f };
						if (c.GetDistance(to) > reach + step + 200.0f) continue;
						if (const float d = Spellcraft::PointSeg(c, from, to); d < reach * 0.6f) {
							to = from + l.s.dir * std::max(0.0f, (c - from).Dot(l.s.dir) - 30.0f);
							broke = true;
							if (static int said = 0; said++ < 20) logger::info("slash: broke on {} ({:.0f} units out)", a->GetName(), l.gone);
							break;
						}
					}
				}
				l.at = to;
				l.gone += broke ? l.s.range + 1 : step;
				const float f = std::min(1.0f, l.gone / std::max(1.0f, l.s.range));
				l.fx.Place(l.at, SlashTurn(l), l.s.scale * grow);
				const float fade = f > 0.72f ? std::max(0.0f, 1.0f - (f - 0.72f) / 0.28f) : 1.0f;
				// (each slash is its own placed copy of the model, with materials of its own: its first brightness is remembered
				// on the slash itself, never in a table keyed by a pointer an old slash may have freed)
				l.fx.Each([&](RE::NiAVObject* o3) { RE::BSVisit::TraverseScenegraphGeometries(o3, [&](RE::BSGeometry* g) {
					if (auto* ep = netimmerse_cast<RE::BSEffectShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get())) {
						if (auto* m = static_cast<RE::BSEffectShaderMaterial*>(ep->material)) {
							auto [it, fresh] = l.base.try_emplace(m, m->baseColorScale);
							m->baseColorScale = it->second * fade;
						}
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				}); });
				// everyone the blade passed through this frame (swept from where it was to where it is, so a fast one misses no one)
				for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
					auto ap = h.get();
					auto* a = ap.get();
					if (!SlashTarget(a, p)) continue;
					const RE::NiPoint3 c = a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f };
					if (c.GetDistance(to) > reach + step + 200.0f) continue;
					if (Spellcraft::PointSeg(c, from, to) > reach) continue;
					if (!l.hit.insert(a->GetFormID()).second) continue;
					const float dmg = l.s.damage > 0 ? l.s.damage : whole * l.s.share;
					// (Noah: "send enemies flying if the slash kills them") a blow that will kill: thrown along the slash's flight
					// first, so the body goes flying as it dies
					if (l.s.flingKill > 0 && Director::IsRoundUndead(a) && dmg / gen::kRounds.hpScale >= a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth))
						Spellcraft::Fling(a, a->GetPosition() - l.s.dir * 120.0f - RE::NiPoint3{ 0, 0, 70.0f }, l.s.flingKill);
					const int was = g_fecElem;
					if (l.s.fec >= 0) g_fecElem = l.s.fec;
					const bool killed = Damage(a, dmg, false, false, true);
					g_fecElem = was;
					if (l.s.onHit) l.s.onHit(a, killed);
					if (l.s.stopOnHit) { stopped = true; break; }   // (a fireball: it bursts on the first it strikes)
				}
				if (stopped) broke = true, l.gone = l.s.range + 1.0f;
				if (broke) {   // its shatter where it struck: a burst of its own colour
					static const std::unordered_map<std::string_view, int> kElem{ { "purple", 9 }, { "carian", 1 }, { "white", 4 }, { "red", 0 }, { "fire", 0 }, { "frost", 1 },
						{ "shock", 2 }, { "sun", 4 }, { "blood", 5 }, { "arcane", 5 }, { "gold", 8 }, { "bane", 3 } };
					int el = 4;
					for (auto& [k, e] : kElem) if (l.s.model.find(std::string("arc_") + std::string(k) + ".nif") != std::string::npos) el = e;
					ElemBurst(el, l.at, 0.8f * l.s.scale * grow, 0.0f, 0.45f);
				}
			}
			for (auto& l : g_slashes) if (l.gone > l.s.range) l.fx.Detach();
			std::erase_if(g_slashes, [](const LiveSlash& l) { return l.gone > l.s.range; });
		}
		// (Noah, the Summoner's Tier III: "three white light blades circle you and strike the dead that come near") blades of
		// light turning round you at chest height; each in turn darts at the nearest of the dead in reach
		struct HaloState { std::vector<Spellcraft::Fx> blades; std::string model; float scale = 1, period = 1.6f, t = 0, next = 0; int turn = 0; std::function<void(RE::Actor*)> strike; };
		HaloState g_halo;
		void TickHalo(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			auto* root = p && p->Is3DLoaded() ? p->Get3D(cam && cam->IsInFirstPerson()) : nullptr;
			if (g_halo.blades.empty()) return;
			if (!root || !root->AsNode()) { for (auto& b : g_halo.blades) b.Detach(); return; }
			g_halo.t += a_delta;
			const RE::NiPoint3 c = p->GetPosition() + RE::NiPoint3{ 0, 0, 95.0f };
			const int n = static_cast<int>(g_halo.blades.size());
			for (int i = 0; i < n; ++i) {
				auto& b = g_halo.blades[i];
				// (the effects map: its stand-in's model, colour and size - blades of light, not steel)
				const auto parts = FxParts(g_halo.model);
				const FxPart& hp = parts.front();
				if (!b.obj) {
					b = Spellcraft::Fx{ Spellcraft::LoadPath(hp.none || hp.blast >= 0 ? g_halo.model : hp.model) };
					if (b.obj) TintTree(b.obj.get(), hp);
				}
				if (!b.obj) continue;
				b.Attach(root->AsNode());
				const float a = g_halo.t * 1.4f + 6.2832f * i / n;
				const RE::NiPoint3 at = c + RE::NiPoint3{ std::cos(a) * 85.0f, std::sin(a) * 85.0f, std::sin(g_halo.t * 2.0f + i) * 8.0f };
				// the blade upright-ish, its point leading round the circle
				const RE::NiPoint3 tangent{ -std::sin(a), std::cos(a), 0.35f };
				b.Place(at, Spellcraft::Facing(tangent) * hp.rot, g_halo.scale * hp.scale);
			}
			if ((g_halo.next -= a_delta) <= 0) {
				RE::Actor* best = nullptr;
				float bd = 650.0f;
				for (auto* f : Foes(p->GetPosition(), 650.0f)) if (const float d = f->GetPosition().GetDistance(p->GetPosition()); d < bd) bd = d, best = f;
				if (best && g_halo.strike) {
					g_halo.strike(best);
					g_halo.next = g_halo.period / static_cast<float>(n);
					++g_halo.turn;
				} else g_halo.next = 0.25f;
			}
		}
		void  Slash(const SlashSpec& a_s)
		{
			if (g_slashes.size() > 24) return;
			if (static int said = 0; said++ < 30) logger::info("slash: {} from ({:.0f},{:.0f},{:.0f}) dir ({:.2f},{:.2f},{:.2f}) range {:.0f} scale {:.2f}", a_s.model, a_s.from.x, a_s.from.y, a_s.from.z, a_s.dir.x, a_s.dir.y, a_s.dir.z, a_s.range, a_s.scale);
			LiveSlash l;
			l.s = a_s;
			if (l.s.dir.Length() < 1e-3f) l.s.dir = { 0, 1, 0 };
			l.s.dir.Unitize();
			l.at = a_s.from;
			g_slashes.push_back(std::move(l));
		}
		// (Noah: "the slashes ... should shoot wherever the player is looking matching the angle of the blade during the attack
		// swing") a swing flies at the crosshair - up and down too - from just ahead of your eyes in first person, from your
		// chest in third (aimed at the crosshair's point far ahead, so it crosses the view where you look)
		RE::NiPoint3 CamFwd(RE::NiPoint3* a_pos = nullptr)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			RE::NiPoint3 d{ 0, 1, 0 };
			if (cam && cam->cameraRoot) {
				const auto& w = cam->cameraRoot->world;
				d = { w.rotate.entry[0][1], w.rotate.entry[1][1], w.rotate.entry[2][1] };
				if (a_pos) *a_pos = w.translate;
			}
			if (d.Length() < 1e-3f) d = { 0, 1, 0 };
			d.Unitize();
			return d;
		}
		// (Noah: "The spells should not be coming from the center of my screen") while a wand casts, every swing-born look and
		// bolt starts at its tip
		bool         g_fromSet = false;
		RE::NiPoint3 g_fromAt{};
		RE::NiPoint3 SwingFrom()
		{
			if (g_fromSet) return g_fromAt;
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			RE::NiPoint3 cp{};
			const RE::NiPoint3 d = CamFwd(&cp);
			if (!p) return cp;
			if (cam && cam->IsInFirstPerson() && cp.GetDistance(p->GetPosition()) < 250.0f) return cp + d * 55.0f + RE::NiPoint3{ 0, 0, -14.0f };
			RE::NiPoint3 flat{ d.x, d.y, 0 };
			if (flat.Length() > 1e-3f) flat.Unitize();
			return p->GetPosition() + RE::NiPoint3{ 0, 0, 100.0f } + flat * 70.0f;
		}
		RE::NiPoint3 SwingDir()
		{
			RE::NiPoint3 cp{};
			const RE::NiPoint3 d = CamFwd(&cp);
			RE::NiPoint3 to = (cp + d * 2500.0f) - SwingFrom();
			if (to.Length() < 1e-3f) to = d;
			to.Unitize();
			to.z = std::clamp(to.z, -0.9f, 0.9f);
			to.Unitize();
			return to;
		}
		// the blade's tip, frame by frame (the body you're seen in): how it moved the moment before a slash is the swing's
		// sweep - a crescent lies along it
		struct TipSample { RE::NiPoint3 at; float t; bool first; };
		std::deque<TipSample> g_tips;
		float g_tipClock = 0;
		void TrackBlade(float a_delta)
		{
			g_tipClock += a_delta;
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			const bool first = cam && cam->IsInFirstPerson();
			auto* root = p && p->Is3DLoaded() ? p->Get3D(first) : nullptr;
			auto* node = root ? root->GetObjectByName("WEAPON") : nullptr;
			if (!node) { g_tips.clear(); return; }
			const auto& w = node->world;
			RE::NiPoint3 tip = w.translate + RE::NiPoint3{ w.rotate.entry[0][1], w.rotate.entry[1][1], w.rotate.entry[2][1] } * (70.0f * w.scale);
			// (in first person the whole arm turns with your view: kept in the view's own axes, so the sweep is the blade's own
			// and not the view's turn; in third your own run is taken off)
			if (first && cam->cameraRoot) tip = cam->cameraRoot->world.rotate.Transpose() * (tip - cam->cameraRoot->world.translate);
			else tip = tip - p->GetPosition();
			if (!g_tips.empty() && g_tips.back().first != first) g_tips.clear();
			g_tips.push_back({ tip, g_tipClock, first });
			while (g_tips.size() > 2 && g_tipClock - g_tips.front().t > 0.45f) g_tips.pop_front();
		}
		// the angle the blade swept, about the flight: its tip's sweep over the last moment laid on the plane across the flight
		// (a flat sweep lies the crescent level, an overhead chop stands it upright, a diagonal cut lies it on its slant) -
		// with no sweep yet, the blade's own line
		float SwingRoll(const RE::NiPoint3& a_dir)
		{
			RE::NiPoint3 d = a_dir; d.Unitize();
			const auto f = Spellcraft::Facing(d);
			const RE::NiPoint3 x{ f.entry[0][0], f.entry[1][0], f.entry[2][0] }, z{ f.entry[0][2], f.entry[1][2], f.entry[2][2] };
			// (Noah, twice: the slashes faced the wrong way) the cut itself: the fastest the blade's tip moved in the last third of
			// a second (the sweep, never the wind-up or the recovery around it), its direction laid across the flight - the
			// crescent lies along it. With no sweep seen, the blade's own line
			RE::NiPoint3 b{};
			if (g_tips.size() >= 4) {
				const float tEnd = g_tips.back().t;
				float best = 0; std::size_t bi = 0;
				for (std::size_t i = 1; i < g_tips.size(); ++i) {
					if (tEnd - g_tips[i].t > 0.33f) continue;
					const float dt = std::max(1e-3f, g_tips[i].t - g_tips[i - 1].t);
					const float sp = (g_tips[i].at - g_tips[i - 1].at).Length() / dt;
					if (sp > best) best = sp, bi = i;
				}
				if (bi > 0 && best > 250.0f) {
					const std::size_t i0 = bi >= 2 ? bi - 2 : 0, i1 = std::min(g_tips.size() - 1, bi + 1);
					RE::NiPoint3 v = g_tips[i1].at - g_tips[i0].at;
					auto* cam = RE::PlayerCamera::GetSingleton();
					if (g_tips.back().first && cam && cam->cameraRoot) v = cam->cameraRoot->world.rotate * v;   // (the view's axes back into the world's)
					b = v - d * v.Dot(d);
				}
			}
			if (b.Length() < 1.0f) {
				auto* p = RE::PlayerCharacter::GetSingleton();
				auto* cam = RE::PlayerCamera::GetSingleton();
				auto* root = p ? p->Get3D(cam && cam->IsInFirstPerson()) : nullptr;
				auto* node = root ? root->GetObjectByName("WEAPON") : nullptr;
				if (!node) return 0.0f;
				const auto& r = node->world.rotate;
				b = RE::NiPoint3{ r.entry[0][1], r.entry[1][1], r.entry[2][1] };
				b = b - d * b.Dot(d);
				if (b.Length() < 1e-3f) return 0.0f;
			}
			float roll = std::atan2(b.Dot(z), b.Dot(x));
			if (roll > 1.5708f) roll -= 3.14159f; else if (roll < -1.5708f) roll += 3.14159f;   // (a crescent bows forward either way)
			return -roll;
		}
		void  Halo(int a_count, const std::string& a_model, float a_scale, float a_period, std::function<void(RE::Actor*)> a_strike)
		{
			if (a_count <= 0) { for (auto& b : g_halo.blades) b.Detach(); g_halo.blades.clear(); return; }
			if (static_cast<int>(g_halo.blades.size()) == a_count && g_halo.model == a_model) { g_halo.strike = std::move(a_strike); g_halo.period = a_period; return; }
			for (auto& b : g_halo.blades) b.Detach();
			g_halo.blades.assign(a_count, {});
			g_halo.model = a_model, g_halo.scale = a_scale, g_halo.period = a_period, g_halo.strike = std::move(a_strike);
		}
		float g_wflameScale = 1.0f;
		void TickWeaponFlame()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			std::string want;
			if (p && p->Is3DLoaded() && Save::SinceWorldReady() > 4.0f && p->AsActorState()->IsWeaponDrawn()) {
				auto* eq = p->GetEquippedObject(false);
				const bool mace = eq && eq == Classes::AlchemistKnife(), mad = eq && eq == Classes::SpellswordBlade();
				if (mace || mad) {
					const int t = std::clamp(Kit::TierOfWeapon(eq), 0, 3);
					// (Noah: "the flames should look like real fire that are purple in color") the mace burns with Skyrim's own purple
					// fire - Nocturnal's, from the game's files - on its crystal; the Madness Sword keeps its sheath of red flame
					// (Noah: "a weird circle thing in the mace head" - Nocturnal's fire brings its bed of embers; its flames left behind in
					// the world in gaps as you swung) the mace's own violet fire, in its own space: it moves with every swing
					// (the class swords: their looks are enchantments now - Classes::BladeLook - nothing hung on the blade)
					(void)mace; (void)mad; (void)t;
					g_wflameScale = mace ? 0.19f + 0.04f * static_cast<float>(t) : 1.0f;  // (Noah: "I can only see the after images of the effect" - at 0.11 it was a smudge)
				}
			}
			if (want != g_wflame.path) {
				for (auto& f : g_wflame.fx) { f.Detach(); f = {}; }
				g_wflame.path = want;
				if (!want.empty()) {
					for (auto& f : g_wflame.fx) f = Spellcraft::Fx{ Spellcraft::LoadPath(want) };
					logger::info("weapon fire: {}", want);
				}
			}
			if (want.empty()) return;
			for (int k = 0; k < 2; ++k) {
				auto* root = p->Get3D(k == 1);
				auto* node = root ? root->GetObjectByName("WEAPON") : nullptr;
				g_wflame.fx[k].Attach(node ? node->AsNode() : nullptr);
				// the mace's fire: on the crystal (48.4 up the haft), sized by tier, and always burning upward as real fire does,
				// however the mace is held or swung
				if (auto* o = g_wflame.fx[k].obj.get(); o && node && want.starts_with("effects\\")) {
					const auto& pw = node->world;
					o->local.rotate = pw.rotate.Transpose();
					o->local.translate = RE::NiPoint3{ 0.0f, 36.3f, 0.0f };  // (the crystal, the mace 25% shorter)
					o->local.scale = g_wflameScale / std::max(0.01f, pw.scale);
					RE::NiUpdateData u;
					o->Update(u);
				}
			}
		}
		void TickLightOrb(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* root = p && p->Is3DLoaded() ? p->Get3D(false) : nullptr;
			if (!g_orb.want || !root) { if (g_orb.fx.obj || g_orb.light) OrbDown(); return; }
			// (Noah's game crashed twice the moment a save came up, in the renderer: the orb and its light were hung on the body
			// that same first frame) it waits until the world has settled
			if (Save::SinceWorldReady() < 4.0f) return;
			if (!g_orb.fx.obj) {
				g_orb.fx = Spellcraft::Fx{ Spellcraft::LoadPath("CurseOfAlduin\\fx\\lightorb.nif") };
				if (!g_orb.fx.obj) return;
			}
			// (hung loose in the scene it was dropped by the renderer in third person) it rides your body's own root, drawn
			// whenever you are; Fx::Attach does nothing when it's already there, and hangs it again on a rebuilt body
			if (auto* body = root->AsNode()) g_orb.fx.Attach(body);
			g_orb.t += a_delta;
			RE::NiPoint3 head = p->GetPosition() + RE::NiPoint3{ 0, 0, 128.0f };
			if (auto* h = root->GetObjectByName("NPC Head [Head]")) head = h->world.translate;
			const float t = g_orb.t;
			// (Noah: "make the animation and orb size smaller in 3rd person I think it's distracting when fighting") in third
			// person a smaller orb, in a tighter circle with a smaller bob
			auto*       cam = RE::PlayerCamera::GetSingleton();
			const bool  third = !(cam && cam->IsInFirstPerson());
			const float circ = third ? 3.5f : 9.0f, bob = third ? 1.0f : 2.5f;
			const RE::NiPoint3 want = head + RE::NiPoint3{ std::cos(t * 0.9f) * circ, std::sin(t * 0.9f) * circ, (third ? 40.0f : 52.0f) + std::sin(t * 1.7f) * bob };  // (at 30 it sat in the hair: unseen)
			g_orb.at = g_orb.placed ? g_orb.at + (want - g_orb.at) * std::min(1.0f, a_delta * 12.0f) : want;  // (it follows you a touch softly)
			g_orb.placed = true;
			RE::NiMatrix3 turn;
			turn.SetEulerAnglesXYZ(0.0f, 0.0f, t * 0.6f);
			g_orb.fx.Place(g_orb.at, turn, third ? 0.55f : 1.05f);  // (Noah: "a little bit smaller" - 1.5 before; its light is its own, the same 15 m)
			// (Noah: the game crashed twice a few seconds after loading into Dragonsreach - the light had been moved onto a node of
			// our own hung on the scene's root) the light rides your body's own root, as the belt lantern's did for weeks without
			// trouble, a little over your head in the body's frame; hung again when the game takes it down or the body is rebuilt
			auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
			auto* body = root->AsNode();
			const bool dropped = g_orb.light && ssn && !ssn->GetLight(g_orb.light.get());
			if (g_orb.light && (dropped || g_orb.light->parent != body)) {
				if (ssn) ssn->RemoveLight(g_orb.light.get());
				if (auto* parent = g_orb.light->parent) parent->DetachChild(g_orb.light.get());
				g_orb.light.reset();
			}
			if (!g_orb.light && body) {
				auto* ligh = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectLIGH>(gen::kLanternLight, gen::kPlugin);
				if (auto* l = ligh ? ligh->GenDynamic(p, body, 1, 1, 0) : nullptr) {
					l->local.translate = RE::NiPoint3{ 0.0f, 0.0f, 175.0f };
					g_orb.light.reset(l);
					logger::info("light orb: lit (radius {})", ligh->data.radius);
				}
			}
		}
		void  SetLightOrb(bool a_on) { g_orb.want = a_on; }
		// (testing) a model hung still in front of you for 30 s, under the scene root (w), the cell (c) or the player (p)
		struct DevFxItem { Spellcraft::Fx fx; float left = 30.0f; };
		std::vector<DevFxItem> g_devFx;
		void DevFx(const std::string& a_path, const std::string& a_root, float a_side)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			DevFxItem it;
			it.fx = Spellcraft::Fx{ Spellcraft::LoadPath(a_path) };
			RE::NiNode* host = Spellcraft::WorldRoot();
			if (a_root == "c" && p->GetParentCell() && p->GetParentCell()->GetRuntimeData().loadedData) host = p->GetParentCell()->GetRuntimeData().loadedData->cell3D.get();
			if (a_root == "p" && p->Get3D(false)) host = p->Get3D(false)->AsNode();
			if (it.fx.obj && host) it.fx.Attach(host);
			const float yaw = p->GetAngleZ();
			const RE::NiPoint3 fwd{ std::sin(yaw), std::cos(yaw), 0 }, side{ std::cos(yaw), -std::sin(yaw), 0 };
			RE::NiPoint3 at = p->GetPosition() + fwd * 220.0f + side * a_side + RE::NiPoint3{ 0, 0, 110.0f };
			if (a_root == "h") {  // (over the head, as the Light Orb hangs: side is the height above the head)
				host = Spellcraft::WorldRoot();
				it.fx.Attach(host);
				auto* r3 = p->Get3D(false);
				auto* h = r3 ? r3->GetObjectByName("NPC Head [Head]") : nullptr;
				at = (h ? h->world.translate : p->GetPosition() + RE::NiPoint3{ 0, 0, 120.0f }) + RE::NiPoint3{ 0, 0, a_side };
			}
			RE::NiMatrix3 rot;
			rot.SetEulerAnglesXYZ(0, 0, 0);
			it.fx.Place(at, rot, 1.0f);
			logger::info("dev: fx {} under {} ({}) at ({:.0f},{:.0f},{:.0f})", a_path, a_root, it.fx.obj ? "loaded" : "MISSING", at.x, at.y, at.z);
			g_devFx.push_back(std::move(it));
		}
		// (dev: trying Skyrim's own effects) `look <model> [w|x|h] [scale] [pal] [seconds]` - a model set down 3.5 m ahead of you
		// on the ground, as a placed object (w), an explosion (x) or a hazard (h), coloured by a ramp; `fxreload` reads the
		// effects map again
		struct Look { WorldFx fx; float t = 0, life = 4; RE::NiPoint3 at; float scale = 1; };
		std::vector<Look> g_looks;
		void DevLook(const std::string& a_model, const std::string& a_mode, float a_scale, const std::string& a_pal, float a_secs)
		{
			(void)FxParts("");   // (the map read first: never read over the look's own entry)
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			const float yaw = p->GetAngleZ();
			RE::NiPoint3 at = p->GetPosition() + RE::NiPoint3{ std::sin(yaw), std::cos(yaw), 0 } * 350.0f;
			if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(at, land) && std::abs(land - at.z) < 300.0f) at.z = land;
			FxPart fp;
			fp.model = a_model;
			if (!a_pal.empty() && a_pal != "-") fp.pal = a_pal.find('\\') == std::string::npos ? "textures\\CurseOfAlduin\\fx\\pal_" + a_pal + ".dds" : a_pal;
			auto part = std::make_shared<FxPart>(fp);
			if (a_mode == "x") {
				if (auto* ex = ModelExplosion(a_model)) {
					PendingBlast pb{ at, 0.0f, -1, a_scale, ex };
					pb.exact = true, pb.part = part;
					g_blasts.push_back(pb);
				}
			} else if (a_mode == "h") {
				if (auto* hz = ModelHazard(a_model, a_secs, true)) if (auto r = PlaceAtPoint(hz, at)) r->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(a_scale, 0.1f, 9.0f) * 100.0f), WatchTint(r.get(), fp, true);
			} else {
				// (a one-off copy of the effects-map lookup: the model asked for, as it is, coloured as asked)
				g_fxMap.erase("__look__");
				fp.play = a_mode == "wp";
				g_fxMap["__look__"] = { fp };
				Look l{ WorldFx("__look__"), 0.0f, a_secs, at, a_scale };
				g_fxMap.erase("__look__");
				if (l.fx.ok) g_looks.push_back(std::move(l));
			}
			logger::info("dev: look {} as {} scale {:.2f} pal {} for {:.1f} s at ({:.0f},{:.0f},{:.0f}) - {}", a_model, a_mode, a_scale, fp.pal.empty() ? "-" : fp.pal, a_secs, at.x, at.y, at.z,
				Spellcraft::LoadPath(a_model) ? "loaded" : "MISSING");
		}
		void TickLooks(float a_delta)
		{
			for (auto& l : g_looks) {
				l.t += a_delta;
				RE::NiMatrix3 r;
				l.fx.Place(l.at, r, l.scale);
				if (l.t > l.life) l.fx.Detach();
			}
			std::erase_if(g_looks, [](const Look& l) { return l.t > l.life; });
		}
		// (the Dragon Slayer's art round its blade) every effect shader under a node coloured by the gold ramp - each only once
		void TintGold(RE::NiAVObject* a_root) { TintPal(a_root, "textures\\CurseOfAlduin\\fx\\pal_gold.dds", true); }
		// (Noah, clip 9: "the green visual effects on the blade should be white in color") any palette, the flames kept or hidden
		void TintPal(RE::NiAVObject* a_root, const std::string& pal, bool a_hideFlames)
		{
			if (!a_root) return;
			// (Noah: "remove the flames on the sword itself") the fire art a fire spell in the other hand lays on the blade
			// (Skyrim's flame enchantment: its flames, glow and heat shimmer) is hidden - only the gold circling stays
			if (a_hideFlames) {
				RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* g) {
					const std::string_view n(g->name.c_str() ? g->name.c_str() : "");
					if (n == "pFlames" || n == "pIncandescence" || n == "RefractionShape") g->GetFlags().set(RE::NiAVObject::Flag::kHidden);
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				for (const char* n : { "pFlames", "pIncandescence", "RefractionShape" }) if (auto* o = a_root->GetObjectByName(n)) o->GetFlags().set(RE::NiAVObject::Flag::kHidden);
			}
			bool any = false;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* g) {
				auto* ep = netimmerse_cast<RE::BSEffectShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get());
				auto* m = ep ? static_cast<RE::BSEffectShaderMaterial*>(ep->material) : nullptr;
				if (m && !ep->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kRefraction) && std::string_view(m->greyscaleTexturePath.c_str() ? m->greyscaleTexturePath.c_str() : "") != pal) any = true;
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (!any) return;
			FxPart fp;
			fp.pal = pal;
			TintTree(a_root, fp);
			if (static int said = 0; said++ < 4) {
				std::string names;
				RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* g) { names += std::string(g->name.c_str()) + " "; return RE::BSVisit::BSVisitControl::kContinue; });
				logger::info("swords: blade art tinted ({}): {}", pal, names);
			}
		}
		// (Noah: "Cast the real Sunbeam" - "animated and hyper realistic") Mysticism's own Sunbeam, cast down out of the sky onto
		// a point: its beam (a concentration spell's: held by launching it again every tenth of a second) from high overhead into
		// the ground, Mysticism's massive sunbeam explosion where it lands; without Mysticism, the pillar of sunlit fire
		// (fxmap.ini sun_strike)
		struct SkyBeamLive { RE::NiPoint3 at; float left = 0, next = 0; };
		std::vector<SkyBeamLive> g_skyBeams;
		RE::SpellItem* SunbeamSpell()
		{
			static RE::SpellItem* sp = [] {
				auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(0x7388D3, "MysticismMagic.esp");
				auto* s = f ? f->As<RE::SpellItem>() : nullptr;
				logger::info("skybeam: Mysticism's Sunbeam {}", s ? "found" : "not found - the pillar of sunlit fire instead");
				return s;
			}();
			return sp;
		}
		bool SkyBeam(const RE::NiPoint3& a_at, float a_secs)
		{
			// (Noah: "I want the light beams to be animated" - "Strikes down from the sky", "Swirling light inside", "Pulses, then
			// fades", "Golden motes rising") a bolt of sunlight drops out of the sky; where it lands Mysticism's sunburst goes off, the
			// pillar of sunlit fire rises turning (a cyclone that stands: it spins, swells as it ends, and is gone), and golden sparks
			// rise round its foot (fxmap.ini sun_bolt, sun_burst, sun_pillar, sun_motes) - about half a second from the call to the strike
			const float life = std::max(1.2f, a_secs);
			Lob("CurseOfAlduin\\fx\\sun_bolt.nif", a_at + RE::NiPoint3{ 0, 0, 1800.0f }, RE::NiPoint3{ 0, 0, -4200.0f }, 1.0f, [life](const RE::NiPoint3& at) {
				QueueFx("CurseOfAlduin\\fx\\sun_burst.nif", at, 1.0f, 0.0f, 2.0f, false);
				CycloneSpec cs;
				cs.model = "CurseOfAlduin\\fx\\sun_pillar.nif";
				cs.from = at, cs.fwd = { 0, 1, 0 }, cs.dist = 0.0f, cs.life = life, cs.size = 1.0f, cs.fec = -1, cs.share = 0.0f, cs.noPull = true;
				ElemCyclone(std::move(cs));
				for (int k = 0; k < 3; ++k) QueueFx("CurseOfAlduin\\fx\\sun_motes.nif", at, 1.0f, 0.15f + 0.55f * static_cast<float>(k), 1.5f, false);
			}, 0.0f, 0.0f);
			return true;
		}
		void TickSkyBeams(float a_delta)
		{
			if (g_skyBeams.empty()) return;
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* sp = SunbeamSpell();
			auto* proj = sp && !sp->effects.empty() && sp->effects[0] && sp->effects[0]->baseEffect ? sp->effects[0]->baseEffect->data.projectileBase : nullptr;
			for (auto& b : g_skyBeams) {
				b.left -= a_delta;
				if (!p || !proj || (b.next -= a_delta) > 0) continue;
				b.next = 0.0f;   // (Noah: "the beam of light from the sky is broken" - launched every tenth of a second it flickered in dashes: every frame now)
				const RE::NiPoint3 from = b.at + RE::NiPoint3{ 25.0f, 0, 1700.0f };
				RE::Projectile::ProjectileRot rot{ RE::NI_HALF_PI * 0.985f, 0.0f };   // (straight down out of the sky)
				RE::Projectile::LaunchData ld(proj, p, from, rot);
				ld.useOrigin = true;
				ld.autoAim = false;
				ld.spell = sp;
				ld.castingSource = RE::MagicSystem::CastingSource::kInstant;
				RE::ProjectileHandle h;
				RE::Projectile::Launch(&h, ld);
				if (static int said = 0; said++ < 3) { auto pr = h.get(); logger::info("skybeam: launched {} at ({:.0f},{:.0f},{:.0f})", pr ? "a beam" : "nothing", b.at.x, b.at.y, b.at.z); }
			}
			std::erase_if(g_skyBeams, [](const SkyBeamLive& b) { return b.left <= 0; });
		}
		void DevFxReload() { FxMapLoad(); }
		void SetSwingFrom(const RE::NiPoint3* a_at) { g_fromSet = a_at != nullptr; if (a_at) g_fromAt = *a_at; }
		void Fling(RE::Actor* a_target, const RE::NiPoint3& a_from, float a_mag) { Spellcraft::Fling(a_target, a_from, a_mag); }
		// a placed model's effect shaders laid over with a colour, the moment its model is in (the Tablet of Mysteries' beacon)
		void TintRef(RE::TESObjectREFR* a_ref, float a_r, float a_g, float a_b)
		{
			FxPart fp;
			fp.col = { a_r, a_g, a_b };
			WatchTint(a_ref, fp);
		}
		void DevSwing(int a_power) { g_devSwing = a_power ? 2 : 1; }
		void DevNodes()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			for (const bool first : { false, true }) {
				auto* rt = p->Get3D(first);
				for (const char* n : { "WEAPON", "SHIELD" }) {
					auto* node = rt ? rt->GetObjectByName(n) : nullptr;
					std::string kids;
					if (auto* nn = node ? node->AsNode() : nullptr) for (auto& c : nn->GetChildren()) if (c) kids += fmt::format("{}{} ", c->name.c_str(), c->GetAppCulled() ? "(culled)" : "");
					logger::info("dev: {} {} node {} culled {} children: {}", first ? "1st" : "3rd", n, node ? "found" : "missing", node && node->GetAppCulled(), kids);
				}
			}
			if (g_orb.fx.obj) {
				const auto& w = g_orb.fx.obj->world.translate;
				const auto& wb = g_orb.fx.obj->worldBound;
				logger::info("dev: orb scale {:.2f} bound c({:.0f},{:.0f},{:.0f}) r {:.1f} flags {:08X} app-culled kids {}", g_orb.fx.obj->world.scale, wb.center.x, wb.center.y, wb.center.z, wb.radius,
					g_orb.fx.obj->GetFlags().underlying(), g_orb.fx.obj->AsNode() ? g_orb.fx.obj->AsNode()->GetChildren().size() : 0);
				logger::info("dev: orb at ({:.0f},{:.0f},{:.0f}) player ({:.0f},{:.0f},{:.0f}) parent {} culled {} light {}", w.x, w.y, w.z,
					p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), g_orb.fx.obj->parent ? g_orb.fx.obj->parent->name.c_str() : "none", g_orb.fx.obj->GetAppCulled(), g_orb.light ? "on" : "off");
			} else logger::info("dev: orb not hung (want {})", g_orb.want);
		}
		void  StartThrow(bool a_left, std::function<void()> a_release) { StartThrowImpl(a_left, std::move(a_release)); }
		bool  Throwing() { return ThrowingImpl(); }
		// (Noah: the Alchemist's flasks "explode right in front of my face - there's no travel, it doesn't arc") a thrown
		// flask is flown here: its model hung in the scene, carried along its arc under gravity, tumbling; it comes down on
		// the first body or wall or ground its path crosses, and its burst is called there
		struct Lobbed { Spellcraft::Fx fx; RE::ObjectRefHandle ref; RE::NiPoint3 p, v; float t = 0, spin = 12.0f, scale = 1.0f; std::function<void(const RE::NiPoint3&)> land; bool done = false; bool meteor = false; RE::NiPoint3 target; RE::NiMatrix3 turn; std::vector<std::pair<RE::ObjectRefHandle, FxPart>> trail; RE::ActorHandle chase; float grav = 900.0f; };
		std::vector<Lobbed> g_lobs;
		void TickLobs(float a_delta)
		{
			if (g_lobs.empty()) return;
			const float dt = std::min(a_delta, 0.05f);
			std::vector<RE::Actor*> foes;
			for (auto h : Director::AliveHandles()) if (auto a = h.get(); a && !a->IsDead()) foes.push_back(a.get());
			std::vector<std::pair<std::function<void(const RE::NiPoint3&)>, RE::NiPoint3>> landed;
			for (auto& l : g_lobs) {
				l.t += dt;
				// (Noah: "The Dragon Slayer's meteors should be homing ... realistic, smaller fireballs that home in on enemies") one
				// chasing one of the dead: steered onto it as it moves, no falling
				if (auto ca = l.chase.get(); ca && !ca->IsDead()) {
					l.target = ca->GetPosition() + RE::NiPoint3{ 0, 0, 40.0f };
					RE::NiPoint3 want = l.target - l.p;
					const float d = want.Length();
					if (d > 1.0f) {
						const float sp = std::max(900.0f, l.v.Length());
						want = want * (sp / d);
						const float k = std::min(1.0f, 5.0f * dt);
						l.v = l.v + (want - l.v) * k;
					}
				} else l.v.z -= l.grav * dt;
				const RE::NiPoint3 next = l.p + l.v * dt;
				RE::NiPoint3 hit = next;
				bool down = l.t > 8.0f;
				// (Noah, clip 291: the gold meteors burst in the treetops) a meteor falls onto its point: nothing stops it until the
				// last few metres, and it comes down there even if it would miss the ground by a hair
				if (l.meteor && !down) {
					if (next.z <= l.target.z + 25.0f || next.GetDistance(l.target) < 60.0f) { hit = l.target; down = true; }
					else if (l.p.z > l.target.z + 260.0f) { l.p = next; goto placed; }
				}
				if (!down && l.t > 0.04f) {  // (out of your own hand first)
					if (const float wf = Spellcraft::WorldHit(l.p, next); wf <= 1.0f) hit = l.p + (next - l.p) * wf, down = true;
					for (auto* f : foes) {
						if (down) break;
						const RE::NiPoint3 c = f->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f };
						if (c.GetDistance(l.p) > 400.0f) continue;
						if (Spellcraft::PointSeg(c, l.p, next) < 62.0f) hit = c, down = true;
					}
				}
				if (down) {
					l.fx.Detach();
					if (auto r = l.ref.get()) r->Disable(), r->SetDelete(true);
					for (auto& [h, fp] : l.trail) if (auto r = h.get()) r->Disable(), r->SetDelete(true);
					l.done = true;
					landed.push_back({ l.land, hit });
					continue;
				}
				l.p = next;
			placed:
				// (Noah, the flasks: "Keep arc, add a trail") what flies with it - its element streaming behind - carried along too
				for (auto& [h, fp] : l.trail) if (auto r = h.get()) {
					r->SetPosition(l.p);
					if (auto* o = r->Get3D()) {
						o->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw, RE::NiAVObject::Flag::kIgnoreFade);
						if (auto* fn = o->AsFadeNode()) fn->GetRuntimeData().currentFade = 1.0f;
						o->local.translate = l.p;
						o->local.rotate = Spellcraft::Facing(l.v) * fp.rot;
						o->local.scale = std::max(0.01f, fp.scale);
						RE::NiUpdateData ud;
						o->Update(ud);
					}
				}
				if (auto r = l.ref.get()) {
					RE::NiMatrix3 tumble;
					tumble.SetEulerAnglesXYZ(l.t * l.spin, 0.0f, 0.0f);
					r->SetPosition(l.p);
					if (auto* o = r->Get3D()) {
						// (Noah: "visible but only at halfway flights as it fades in") drawn whole from the hand: no fade-in
						o->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw, RE::NiAVObject::Flag::kIgnoreFade);
						if (auto* fn = o->AsFadeNode()) fn->GetRuntimeData().currentFade = 1.0f;
						o->local.translate = l.p;
						o->local.rotate = Spellcraft::Facing(l.v) * tumble * l.turn;
						o->local.scale = l.scale;
						RE::NiUpdateData ud;
						ud.time = 0.0f;
						o->Update(ud);
					}
				} else if (l.fx.obj) {
					RE::NiMatrix3 tumble;
					tumble.SetEulerAnglesXYZ(l.t * l.spin, 0.0f, 0.0f);
					l.fx.Place(l.p, Spellcraft::Facing(l.v) * tumble, l.scale);
				}
			}
			std::erase_if(g_lobs, [](const Lobbed& l) { return l.done; });
			for (auto& [fn, at] : landed) if (fn) fn(at);
		}
		void  Lob(const std::string& a_model0, const RE::NiPoint3& a_from, const RE::NiPoint3& a_vel, float a_scale0, std::function<void(const RE::NiPoint3&)> a_land, float a_spin, float a_grav)
		{
			if (g_lobs.size() > 40) return;
			Lobbed l;
			// (the effects map: the first stand-in named for it flies in its place)
			const auto parts = FxParts(a_model0);
			const FxPart& fp = parts.front();
			const std::string a_model = fp.none || fp.blast >= 0 ? a_model0 : fp.model;
			const float a_scale = a_scale0 * fp.scale;
			l.turn = fp.rot;
			// (Noah: "I can't see the flasks flying through the air" - hung in the scene by hand it never drew) it flies as a
			// real placed object wearing the flask's model (a nameless activator made once per model), carried along the arc
			static std::unordered_map<std::string, RE::TESObjectACTI*> forms;
			auto& f = forms[a_model];
			if (!f) {
				if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESObjectACTI>(); factory && (f = factory->Create())) f->SetModel(a_model.c_str());
			}
			(void)Spellcraft::LoadPath(a_model);  // (its model in memory before it's placed: drawn the first frame, not a beat later)
			if (auto r = f ? PlaceAtPoint(f, a_from) : RE::NiPointer<RE::TESObjectREFR>{}) l.ref = r->GetHandle(), WatchTint(r.get(), fp, true);
			for (std::size_t i = 1; i < parts.size(); ++i) {   // (the rest of its stand-ins fly with it: a trail)
				const auto& tp = parts[i];
				if (tp.none || tp.blast >= 0 || !Spellcraft::LoadPath(tp.model)) continue;
				if (auto r = PlaceAtPoint(ModelActivator(tp.model), a_from)) l.trail.push_back({ r->GetHandle(), tp }), WatchTint(r.get(), tp, true);
			}
			l.p = a_from, l.v = a_vel, l.scale = a_scale, l.spin = a_spin, l.land = std::move(a_land), l.grav = a_grav;
			logger::info("lob: {} ({})", a_model, l.ref ? "placed" : "NOT PLACED");
			g_lobs.push_back(std::move(l));
		}
		void Hazard(const std::string& a_model, const RE::NiPoint3& a_at, float a_scale, float a_life, float a_yaw)
		{
			for (const auto& fp : FxParts(a_model)) {
				if (fp.none) continue;
				if (fp.blast >= 0) { Blast(fp.blast, a_at, 0.0f, a_scale * fp.scale); continue; }
				if (auto* hz = ModelHazard(fp.model, a_life, false)) {
					if (auto r = PlaceAtPoint(hz, a_at + RE::NiPoint3{ 0, 0, fp.dz * a_scale })) {
						r->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(a_scale * fp.scale, 0.1f, 9.0f) * 100.0f);
						if (a_yaw != 0.0f) r->SetAngle({ 0.0f, 0.0f, a_yaw });
						WatchTint(r.get(), fp, true);
					}
				}
			}
		}
		// (Noah: the heavy attacks redone) one of the heavy attacks' shapes of light set on the ground (tools/ringfx.py,
		// tools/heavyfx.py): grown from s0 to s1 over its first `grow` seconds (fast, then settling), held, its light dying from
		// `fadeFrom` to the end of its life - a shockwave ring is grown its whole life and fades as it goes
		struct GrowFx { WorldFx fx; RE::NiPoint3 at; float yaw, s0, s1, grow, life, fadeFrom, t = 0; std::unordered_map<RE::BSEffectShaderMaterial*, float> base; };
		std::vector<GrowFx> g_grows;
		void Grow(const std::string& a_model, const RE::NiPoint3& a_at, float a_yaw, float a_s0, float a_s1, float a_grow, float a_life, float a_fadeFrom)
		{
			if (g_grows.size() > 48) return;
			GrowFx r{ WorldFx(a_model), a_at, a_yaw, a_s0, a_s1, std::max(0.01f, a_grow), std::max(0.1f, a_life), std::clamp(a_fadeFrom, 0.0f, a_life) };
			if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(r.at, land) && std::abs(land - r.at.z) < 200.0f) r.at.z = land + 2.0f;
			if (r.fx.ok) g_grows.push_back(std::move(r));
		}
		void Ring(const char* a_colour, const RE::NiPoint3& a_at, float a_r0, float a_r1, float a_life)
		{
			Grow(std::string("CurseOfAlduin\\fx\\ring_") + a_colour + ".nif", a_at, 0.0f, a_r0 / 50.0f, a_r1 / 50.0f, a_life, a_life, 0.0f);
		}
		void TickGrows(float a_delta)
		{
			for (auto& r : g_grows) {
				r.t += a_delta;
				const float g = std::min(1.0f, r.t / r.grow), e = 1.0f - (1.0f - g) * (1.0f - g);
				const float light = r.t <= r.fadeFrom ? 1.0f : std::max(0.0f, 1.0f - (r.t - r.fadeFrom) / std::max(0.01f, r.life - r.fadeFrom));
				RE::NiMatrix3 turn;
				turn.SetEulerAnglesXYZ(0, 0, -r.yaw);
				r.fx.Place(r.at, turn, r.s0 + (r.s1 - r.s0) * e);
				r.fx.Each([&](RE::NiAVObject* o3) { RE::BSVisit::TraverseScenegraphGeometries(o3, [&](RE::BSGeometry* geo) {
					if (auto* ep = netimmerse_cast<RE::BSEffectShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get()))
						if (auto* m = static_cast<RE::BSEffectShaderMaterial*>(ep->material)) {
							auto [it, fresh] = r.base.try_emplace(m, m->baseColorScale);
							m->baseColorScale = it->second * light;
						}
					return RE::BSVisit::BSVisitControl::kContinue;
				}); });
			}
			for (auto& r : g_grows) if (r.t > r.life) r.fx.Detach();
			std::erase_if(g_grows, [](const GrowFx& r) { return r.t > r.life; });
		}
		void LightningArc(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_delay) { g_arcs.push_back({ a_from, a_to, a_delay }); }
		void RedStrikeImpl(const RE::NiPoint3& a_at);
		void RedBeamImpl(const RE::NiPoint3& a_at);
		void SoulFallImpl(const RE::NiPoint3& a_at, float a_secs);
		void RedBeam(const RE::NiPoint3& a_at) { RedBeamImpl(a_at); }
		void RedStrike(const RE::NiPoint3& a_at) { RedStrikeImpl(a_at); }
		void SoulFall(const RE::NiPoint3& a_at, float a_secs) { SoulFallImpl(a_at, a_secs); }
		void Knock(RE::Actor* a_a, float a_mag) { if (a_a && !a_a->IsDead()) Stagger(a_a, a_mag); }
		int  FecElem(int a_elem) { const int was = g_fecElem; g_fecElem = a_elem; return was; }
		void FireDeath(RE::Actor* a_a, int a_elem) { FecMark(a_a, a_elem); }
		// (Noah, the Shadowflame Whip: "Fire cyclone") a spinning tornado of violet fire (tools/firecyclone.py) carried along the
		// ground from a point, slowing as it goes: the dead near it are dragged in, those it touches caught (a_touch, once
		// each) and those in its heart burned every beat; at its end it bursts (a_burst)
		struct Cyclone
		{
			WorldFx fx; RE::NiPoint3 from, fwd, at; float dist, life, share, t = 0, beat = 0;
			std::function<void(RE::Actor*)> touch; std::function<void(const RE::NiPoint3&)> burst; std::unordered_set<RE::FormID> seen;
			float size = 1.0f; int fec = 0; bool dragons = false, noPull = false;   // (its size; how its dead die; twice the harm to dragons)
			float trail = 0;   // (the time to the next of what it leaves burning on the ground behind it)
			bool  follow = false, push = false;   // (on you the whole while; the dead thrown out, not drawn in)
			std::unordered_map<RE::FormID, float> thrown;   // (push: when each was last thrown)
		};
		std::vector<Cyclone> g_cyclones;
		void FireCyclone(const RE::NiPoint3& a_from, const RE::NiPoint3& a_fwd, float a_dist, float a_life, float a_share,
			std::function<void(RE::Actor*)> a_touch, std::function<void(const RE::NiPoint3&)> a_burst)
		{
			ElemCyclone(CycloneSpec{ "CurseOfAlduin\\fx\\firecyclone.nif", a_from, a_fwd, a_dist, a_life, a_share, 1.0f, 0, false, false, std::move(a_touch), std::move(a_burst) });
		}
		// (Noah: every class sword's Tier II heavy a small cyclone, its Tier III a large one, each in the class's element; the Dragon
		// Slayer's in golden flame, twice the harm to dragons) any cyclone: its look (an effects-map model), size, path and harm
		void  ElemCyclone(CycloneSpec a_s)
		{
			if (g_cyclones.size() > 4) return;
			Cyclone c{ WorldFx(a_s.model), a_s.from, a_s.fwd, a_s.from, a_s.dist, std::max(0.3f, a_s.life), a_s.share };
			c.touch = std::move(a_s.touch), c.burst = std::move(a_s.burst);
			c.size = std::max(0.1f, a_s.size), c.fec = a_s.fec, c.dragons = a_s.dragons, c.noPull = a_s.noPull || a_s.push;
			c.follow = a_s.follow, c.push = a_s.push;
			if (c.fx.ok) g_cyclones.push_back(std::move(c));
			else logger::warn("cyclone: {} has no look", a_s.model);
		}
		void TickCyclones(float a_delta)
		{
			if (g_cyclones.empty()) return;
			auto* world = Spellcraft::WorldRoot();
			const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
			std::vector<std::pair<std::function<void(const RE::NiPoint3&)>, RE::NiPoint3>> burst;
			for (auto& c : g_cyclones) {
				c.t += a_delta;
				const float f = std::min(1.0f, c.t / c.life), go = 1.0f - std::pow(1.0f - f, 1.6f);
				c.at = c.from + c.fwd * (c.dist * go);
				if (c.follow) if (auto* pl = RE::PlayerCharacter::GetSingleton()) c.at = pl->GetPosition();
				if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(c.at, land) && std::abs(land - c.at.z) < 160.0f) c.at.z = land;
				if (world) {
					RE::NiMatrix3 spin;
					spin.SetEulerAnglesXYZ(0, 0, c.t * 9.0f);
					const float grow = std::min(1.0f, c.t / 0.25f) * (f > 0.9f ? 1.0f + (f - 0.9f) * 3.0f : 1.0f);   // (swelling as it's about to burst)
					c.fx.Place(c.at, spin, c.size * grow * (1.0f + 0.05f * std::sin(c.t * 13.0f)));
				}
				// (Noah: "hyper realistic ... in the element") the ground it passes over left burning in its element - Skyrim's own real
				// fire, frost, light (fxmap.ini cyclone_<element>_trail.nif), every third of a second
				if (!c.follow && (c.trail -= a_delta) <= 0) {
					c.trail = 0.33f;
					static std::unordered_map<std::string, std::string> trails;
					auto [it, fresh] = trails.try_emplace(c.fx.model);
					if (fresh) {
						std::string k = c.fx.model;
						if (auto d = k.rfind(".nif"); d != std::string::npos) k.insert(d, "_trail");
						it->second = g_fxMap.count(FxKey(k)) ? k : std::string{};
					}
					if (!it->second.empty()) QueueFx(it->second, c.at, c.size, 0.0f, 2.5f, true);
				}
				const float pull = 420.0f * c.size;
				for (auto* a : Foes(c.at, pull)) {
					auto pos = a->GetPosition();
					RE::NiPoint3 to = c.at - pos;
					to.z = 0;
					const float d = to.Length();
					if (d > 55.0f && !c.noPull) {   // (dragged in, harder the nearer)
						pos += to * (std::min(d - 45.0f, 560.0f * (1.0f + 1.5f * (1.0f - d / pull)) * a_delta) / d);
						a->SetPosition(pos, true);
					}
					if (c.push) {   // (a tornado turned outward: each that comes near thrown off it - again if it comes back after a second)
						if (auto it = c.thrown.find(a->GetFormID()); (it == c.thrown.end() || c.t - it->second > 1.0f) && d < pull) {
							c.thrown[a->GetFormID()] = c.t;
							if (c.touch) c.touch(a);
						}
						continue;
					}
					if (d < 260.0f * c.size && c.seen.insert(a->GetFormID()).second && c.touch) c.touch(a);
				}
				if ((c.beat -= a_delta) <= 0) {
					c.beat = 0.3f;
					if (static int said = 0; said++ < 40) logger::info("cyclone: t {:.2f} at ({:.0f},{:.0f},{:.0f}) fwd ({:.2f},{:.2f}) drawn {}", c.t, c.at.x, c.at.y, c.at.z, c.fwd.x, c.fwd.y, c.fx.drawn);
					const int was = g_fecElem;
					g_fecElem = c.fec;   // (they die as its element kills: burning, frozen, shocked)
					static auto* dragonKw = RE::TESForm::LookupByID<RE::BGSKeyword>(0x035D59);   // ActorTypeDragon
					for (auto* a : Foes(c.at, 170.0f * std::max(1.0f, c.size))) {
						auto* race = a->GetRace();
						const bool dragon = c.dragons && dragonKw && race && race->HasKeyword(dragonKw);
						Damage(a, whole * c.share * (dragon ? 2.0f : 1.0f), false, false, true);
					}
					g_fecElem = was;
				}
				if (c.t >= c.life) burst.push_back({ c.burst, c.at });
			}
			for (auto& c : g_cyclones) if (c.t >= c.life) c.fx.Detach();
			std::erase_if(g_cyclones, [](const Cyclone& c) { return c.t >= c.life; });
			for (auto& [fn, at] : burst) if (fn) fn(at);
		}
		// (Noah, the meteors: "Big single impacts") where one comes down: a blinding flash of its fire, a shockwave ring racing
		// out across the ground, and the ground left scorched and burning a few seconds
		void MeteorImpact(const RE::NiPoint3& a_at, bool a_gold, float a_size)
		{
			const int el = a_gold ? 8 : 0;
			ElemBurst(el, a_at + RE::NiPoint3{ 0, 0, 40.0f }, 2.0f * a_size, 0.0f, 0.8f);
			ElemBurst(4, a_at + RE::NiPoint3{ 0, 0, 60.0f }, 1.1f * a_size, 0.0f, 0.3f);   // (the white flash at its heart)
			FxBlast(a_gold ? "dragonfire" : "fireball", a_at);
			Ring(a_gold ? "gold" : "fire", a_at, 40.0f, 400.0f * a_size, 0.7f);
			Ring("white", a_at, 20.0f, 220.0f * a_size, 0.35f);
			Puddle(0, a_at, 0.9f * a_size, 4.0f);   // (the scorch, burning)
			BlastSound("fireball", a_at, 0.85f);
		}
		// (Noah: the Bane's and the Dragon Slayer's meteors) one of ours falling from high over the point, a little slanted, nose
		// first; a_land where it comes down (the first body, wall or ground it meets)
		void MeteorAt(RE::Actor* a_target, bool a_gold, float a_scale, std::function<void(const RE::NiPoint3&)> a_land)
		{
			if (!a_target) return;
			Meteor(a_target->GetPosition(), a_gold, a_scale, std::move(a_land));
			if (!g_lobs.empty()) g_lobs.back().chase = a_target->GetHandle();
		}
		void Meteor(const RE::NiPoint3& a_at, bool a_gold, float a_scale, std::function<void(const RE::NiPoint3&)> a_land)
		{
			const float a = static_cast<float>(std::rand() % 628) / 100.0f;
			const RE::NiPoint3 from = a_at + RE::NiPoint3{ std::cos(a) * 420.0f, std::sin(a) * 420.0f, 1500.0f };
			RE::NiPoint3 v = a_at - from;
			const float d = v.Length();
			if (d < 1.0f) return;
			// (falling under the lob's gravity: started fast enough to reach it in about 0.9 s)
			constexpr float t = 0.9f;
			v = v * (1.0f / t) + RE::NiPoint3{ 0, 0, 0.5f * 900.0f * t };
			Lob(a_gold ? "CurseOfAlduin\\fx\\meteor_gold.nif" : "CurseOfAlduin\\fx\\meteor_fire.nif", from, v, a_scale, std::move(a_land), 0.0f);
			if (!g_lobs.empty()) g_lobs.back().meteor = true, g_lobs.back().target = a_at;
		}
		RE::ObjectRefHandle PlaceAt(RE::TESBoundObject* a_obj, const RE::NiPoint3& a_at, float a_yaw)
		{
			auto r = PlaceAtPoint(a_obj, a_at);
			if (!r) return {};
			r->SetAngle({ 0, 0, a_yaw });
			return r->GetHandle();
		}
		void  PurpleFire(const RE::NiPoint3& a_at, float a_scale, float a_delay, float a_life)
		{
			QueueFx("CurseOfAlduin\\fx\\purplefire.nif", a_at, a_scale, a_delay, a_life, false);
		}
		void  ElemBurst(int a_elem, const RE::NiPoint3& a_at, float a_scale, float a_delay, float a_life)
		{
			static constexpr const char* kName[12] = { "fire", "frost", "shock", "wind", "sun", "blood", "void", "poison", "gold", "shadowfire", "holy", "crimson" };
			if (a_elem < 0 || a_elem >= 12) return;
			QueueFx(std::string("CurseOfAlduin\\fx\\burst_") + kName[a_elem] + ".nif", a_at, a_scale, a_delay, a_life, false);
		}
		// a pool of the element on the ground (tools/puddle.py): the Alchemist's flasks' Tier I
		void  Puddle(int a_elem, const RE::NiPoint3& a_at, float a_scale, float a_life)
		{
			static constexpr const char* kName[6] = { "fire", "frost", "shock", "poison", "sun", "blood" };
			if (a_elem < 0 || a_elem >= 6) return;
			RE::NiPoint3 at = a_at;
			if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(at, land) && land > at.z - 250.0f && land < at.z + 80.0f) at.z = land + 2.0f;
			// (Noah: "remove the puddle effect on the floor and just keep it elemental effects") its element burning low over
			// the ground - fire, frost-mist, arcs, sun, blood - not a pool of liquid (tools/motes.py ground_*)
			QueueFx(std::string("CurseOfAlduin\\fx\\ground_") + kName[a_elem] + ".nif", at, a_scale, 0.0f, a_life, true);
			static constexpr int kMote[6] = { 0, 1, 2, 7, 4, 5 };
			Motes(kMote[a_elem], at, a_scale * 0.9f, a_life);  // (fine motes of it rising off the pool)
		}
		// (Noah: "very small animated hyper realistic particles from the area of damage coming from the ground in the colour of
		// the effect") tools/motes.py: a field of tiny motes rising off the ground over the area, its faint glow beneath
		void  Motes(int a_elem, const RE::NiPoint3& a_at, float a_scale, float a_life)
		{
			static constexpr const char* kName[10] = { "fire", "frost", "shock", "wind", "sun", "blood", "void", "poison", "gold", "shadowfire" };
			if (a_elem < 0 || a_elem >= 10) return;
			RE::NiPoint3 at = a_at;
			if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(at, land) && land > at.z - 250.0f && land < at.z + 80.0f) at.z = land + 1.0f;
			QueueFx(std::string("CurseOfAlduin\\fx\\motes_") + kName[a_elem] + ".nif", at, a_scale, 0.0f, a_life, true);
		}


		// a Falmer staff's own bolt struck something: our numbers and its element's work (Staffs.inl StaffTap)
		bool TestKitOn();

		// (Noah: a test kit) every wonder weapon and Echo Altar tome, every Coffer spell, every elemental bow, every
		// city's shout with Dragon's Updraft and the dragon's three bonds, and 100,000 septims - for trying it all out
		// (dev command: testkit). Tomes beyond your slots are known all the same: equip one from the Magic menu and it
		// takes the slot in hand (SlotUnslotted).
		// (Noah: Winterhold's hidden quest) the Eye's Reflection: a beam of the Eye's light along your view, 35 m, through
		// every one of the dead it crosses - most of a zombie's night health each, a boss's a little
		void EyeBeam()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!p || !cam || !cam->cameraRoot) {
				return;
			}
			const auto&  m = cam->cameraRoot->world.rotate;
			RE::NiPoint3 dir{ m.entry[0][1], m.entry[1][1], m.entry[2][1] };
			dir = dir / std::max(0.001f, dir.Length());
			const RE::NiPoint3 from = cam->cameraRoot->world.translate - RE::NiPoint3{ 0, 0, 25.0f } + dir * 60.0f;
			const RE::NiPoint3 to = from + dir * 3500.0f;
			int row = 0;
			for (int i = 0; i < static_cast<int>(gen::kSpellcraft.size()); ++i) {
				if (gen::kSpellcraft[i].id == "aetherium") row = i;
			}
			for (int k = 0; k < 3; ++k) {
				Spellcraft::StreakAt(row, from, to, 3.2f - k, 0.7f + 0.15f * k, k > 0);
			}
			FxBlast("lightmassive", to);
			Forms::Play(gen::Sound::cast_heavy);
			int hit = 0;
			for (auto* f : Foes(from + dir * 1750.0f, 1850.0f)) {
				const RE::NiPoint3 q = f->GetPosition() + RE::NiPoint3{ 0, 0, 80.0f } - from;
				const float        along = q.Dot(dir);
				if (along < 0 || along > 3500.0f || (q - dir * along).Length() > 130.0f) continue;
				Damage(f, Director::RoundHealth() * gen::kRounds.hpScale * (Director::IsBoss(f) ? 0.15f : 0.9f), false, false);
				++hit;
			}
			logger::info("eggs: the Eye's Reflection ({} struck)", hit);
		}

		bool g_kitPending = false;
		constexpr float kKitVersion = 8.0f;   // (8: the Tablet of Mysteries' weapons)  // (raised whenever the kit gains something: test characters get the new things on load)
		// (Noah: every spell from my mods that imbues a weapon or does direct damage, to choose the Echo Altar's from)
		// every castable spell of a mod (not the base game's own) with a damaging effect on health, or one that imbues a
		// weapon; each named for its element first ("Fire: ...") so the Magic menu lists them by element. The names are
		// only changed while the test kit is on (UN_TestKit), and again after each load.
		std::string ElementOfSpell(RE::SpellItem* a_sp)
		{
			auto has = [](RE::EffectSetting* m, RE::FormID kw) {
				return m && m->HasKeywordID(kw);
			};
			for (auto* e : a_sp->effects) {
				auto* m = e ? e->baseEffect : nullptr;
				if (!m) continue;
				switch (m->data.resistVariable) {
				case RE::ActorValue::kResistFire: return "Fire";
				case RE::ActorValue::kResistFrost: return "Frost";
				case RE::ActorValue::kResistShock: return "Shock";
				case RE::ActorValue::kPoisonResist: return "Poison";
				case RE::ActorValue::kResistDisease: return "Disease";
				default: break;
				}
				if (has(m, 0x0001CEAD)) return "Fire";   // MagicDamageFire
				if (has(m, 0x0001CEAE)) return "Frost";  // MagicDamageFrost
				if (has(m, 0x0001CEAF)) return "Shock";  // MagicDamageShock
			}
			std::string n = a_sp->GetName();
			std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			const std::pair<const char*, const char*> kWords[] = { { "fire", "Fire" }, { "flame", "Fire" }, { "burn", "Fire" }, { "ember", "Fire" }, { "inferno", "Fire" }, { "magma", "Fire" },
				{ "frost", "Frost" }, { "ice", "Frost" }, { "cold", "Frost" }, { "blizzard", "Frost" }, { "freez", "Frost" }, { "shock", "Shock" }, { "lightning", "Shock" },
				{ "thunder", "Shock" }, { "storm", "Shock" }, { "spark", "Shock" }, { "sun", "Sun" }, { "holy", "Sun" }, { "light", "Sun" }, { "radian", "Sun" },
				{ "poison", "Poison" }, { "venom", "Poison" }, { "acid", "Poison" }, { "blood", "Blood" }, { "shadow", "Shadow" }, { "dark", "Shadow" },
				{ "void", "Shadow" }, { "wind", "Wind" }, { "earth", "Earth" }, { "stone", "Earth" }, { "water", "Water" } };
			for (auto& [w, el] : kWords) {
				if (n.find(w) != std::string::npos) return el;
			}
			return "Arcane";
		}
		std::vector<std::pair<RE::SpellItem*, std::string>> ModAttackSpells()
		{
			static constexpr std::string_view kBase[] = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm", "CurseOfAlduin.esp" };
			std::vector<std::pair<RE::SpellItem*, std::string>> out;
			for (auto* sp : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::SpellItem>()) {
				auto* file = sp ? sp->GetFile(0) : nullptr;
				if (!file || sp->GetSpellType() != RE::MagicSystem::SpellType::kSpell || !sp->GetFullName() || !*sp->GetFullName()) continue;
				const std::string_view fn = file->GetFilename();
				if (std::find(std::begin(kBase), std::end(kBase), fn) != std::end(kBase) || fn.substr(0, 2) == "cc") continue;
				bool damage = false, imbue = false;
				for (auto* e : sp->effects) {
					auto* m = e ? e->baseEffect : nullptr;
					if (!m) continue;
					const auto a = m->GetArchetype();
					if ((a == RE::EffectArchetypes::ArchetypeID::kValueModifier || a == RE::EffectArchetypes::ArchetypeID::kDualValueModifier ||
							a == RE::EffectArchetypes::ArchetypeID::kPeakValueModifier || a == RE::EffectArchetypes::ArchetypeID::kAbsorb) &&
						m->data.primaryAV == RE::ActorValue::kHealth && m->IsDetrimental()) {
						damage = true;
					}
					if (a == RE::EffectArchetypes::ArchetypeID::kEnhanceWeapon) imbue = true;
				}
				if (!imbue && sp->GetDelivery() == RE::MagicSystem::Delivery::kSelf) {
					std::string n = sp->GetFullName();
					std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					imbue = n.find("imbue") != std::string::npos || n.find("weapon") != std::string::npos || n.find("blade") != std::string::npos || n.find("enchant") != std::string::npos;
				}
				if (damage && sp->GetDelivery() == RE::MagicSystem::Delivery::kSelf && !imbue) damage = false;  // (a curse on yourself is no attack)
				if (damage || imbue) out.push_back({ sp, ElementOfSpell(sp) + (imbue && !damage ? " (Imbue)" : "") });
			}
			return out;
		}
		RE::TESGlobal* KitGlobal() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(gen::kTestKitGlob, gen::kPlugin); }
		std::unordered_map<RE::SpellItem*, std::string> g_origNames;
		// (the old kit named every mod spell by its element: those names put back)
		void RestoreNames()
		{
			for (auto& [sp, nm] : g_origNames) sp->fullName = nm.c_str();
			g_origNames.clear();
		}
		void NameByElement()
		{
			auto& original = g_origNames;
			int n = 0;
			for (auto& [sp, el] : ModAttackSpells()) {
				auto it = original.find(sp);
				if (it == original.end()) it = original.emplace(sp, sp->GetFullName()).first;
				sp->fullName = (el + ": " + it->second).c_str();
				++n;
			}
			logger::info("testkit: {} spells named by element", n);
		}
		void TestKit()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !p->Is3DLoaded() || !p->GetParentCell() || !Forms::IntroDone()) {
				g_kitPending = true;  // (asked for at the main menu or a loading screen: given once you are in the world)
				return;
			}
			g_kitPending = false;
			if (auto* g = KitGlobal()) g->value = kKitVersion;  // (the glider unlocked, the spells named by element; the kit's version)
			// (Noah: the kit is only the Echo Altar's 17 spells, to test them) every other spell you were taught - Skyrim's
			// tomes, the mods' spells, the old kit's hundreds - is taken away (its race's own two stay: they can't be)
			int spells = 0, removed = 0;
			std::map<std::string, int> byEl;
			{
				std::vector<RE::SpellItem*> drop;
				for (auto* sp : p->GetActorRuntimeData().addedSpells) {
					if (sp && sp->GetSpellType() == RE::MagicSystem::SpellType::kSpell && Arcana::IndexOf(sp) < 0) drop.push_back(sp);
				}
				auto* em = RE::ActorEquipManager::GetSingleton();
				for (auto* sp : drop) {
					for (bool left : { false, true }) {
						if (p->GetEquippedObject(left) == sp && em) {
							em->UnequipObject(p, sp, nullptr, 1, RE::TESForm::LookupByID<RE::BGSEquipSlot>(left ? 0x13F43 : 0x13F42), false, true, false, true);
						}
					}
					p->RemoveSpell(sp);
					++removed;
				}
				RestoreNames();
			}
			if (auto* bane = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kFangWeap, gen::kPlugin); bane && p->GetItemCount(bane) <= 0) {
				p->AddObjectToContainer(bane, nullptr, 1, nullptr);  // Alduin's Bane
			}
			// (the story's overhaul: the Falmer staffs are cut, and the altars teach no shouts - Updraft comes with the dragon below)
			for (int i = 0; i < 64; ++i) {  // every Echo Altar spell (all 17)
				if (Arcana::Spell(i)) {
					if (!Arcana::Known(i)) Arcana::Give(i, false);
					++spells, ++byEl[std::string(Arcana::Name(i))];
				}
			}
			if (auto* bane = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kFangWeap, gen::kPlugin)) {  // Alduin's Bane, in your hands
				RE::ActorEquipManager::GetSingleton()->EquipObject(p, bane);
			}
			Movement::TestKit();  // (Dragon's Updraft, the third bond, the glider)
			Director::LearnDance(false);  // (Noah: "all the echo alter rewards like the shout dance of death")
			Kit::DevTestKit();    // (the four crystal swords and every flask at Tier III, the four Black Mage robes)
			Classes::KitWonders();   // (the four wonders)
			Tablet::DevKit();        // (Noah: the Tablet of Mysteries' donated weapons)
			if (auto* sl = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kSlayerWeap, gen::kPlugin); sl && p->GetItemCount(sl) <= 0) {
				p->AddObjectToContainer(sl, nullptr, 1, nullptr);  // the Dragon Slayer greatsword
			}
			// (Noah: "including all the wonders and boss weapons") the ten Dragon Priests' weapons
			for (const auto& b : gen::kCityBosses) {
				// (Noah: "remove the bows from the test kit since we aren't using any bows anymore")
				auto* w = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(b.weapon, gen::kPlugin);
				if (w && (w->IsBow() || w->IsCrossbow())) { if (const int n = p->GetItemCount(w); n > 0) p->RemoveItem(w, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr); continue; }   // (an older kit's bows taken back)
				if (w && p->GetItemCount(w) <= 0) p->AddObjectToContainer(w, nullptr, 1, nullptr);
			}
			if (const int have = Points::Get(); have < 150000) {
				Points::Refund(150000 - have);
			}
			// (Noah: "give the test kit the light orb so I can see") the Light Orb, worn
			if (auto* orb = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectARMO>(gen::kLanternItem, gen::kPlugin)) {
				if (p->GetItemCount(orb) <= 0) p->AddObjectToContainer(orb, nullptr, 1, nullptr);
				RE::ActorEquipManager::GetSingleton()->EquipObject(p, orb, nullptr, 1, nullptr, false, false, false, true);
			}
			// (Noah: "increase my players weight I cant move with the test kit") room for all of it: carry weight 10,000 or more
			if (auto* av = p->AsActorValueOwner(); av && av->GetActorValue(RE::ActorValue::kCarryWeight) < 10000.0f) {
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kCarryWeight, 10000.0f - av->GetActorValue(RE::ActorValue::kCarryWeight));
			}
			Hud::Banner(fmt::format("TEST KIT: the Echo Altar's {} spells, every class's spells, weapons, flasks and robes (Tier III), the four wonders, Dragon's Updraft and the Dance of Death, the ten Dragon Priest weapons, the Tablet's weapons, Alduin's Bane, the glider, 150,000 Cursed Essence", spells), 8.0f);
			std::string list;
			for (auto& [el, c] : byEl) list += fmt::format("{} {}, ", el, c);
			logger::info("testkit: {} altar spells ({}) - {} others taken away; essence {}", spells, list, removed, Points::Get());
		}
		bool TestKitOn()
		{
			auto* g = KitGlobal();
			return g && g->value > 0.5f;
		}
		void TestKitIfFlagged()
		{
			auto dir = SKSE::log::log_directory();
			std::error_code ec;
			if (dir && std::filesystem::exists(*dir / "CurseOfAlduin-testkit.flag", ec)) {
				logger::info("testkit: a new character - the test kit is on its way");
				TestKit();
			}
		}
		// a test character loaded again: its spells take their element names back
		void KitOnLoad()
		{
			static RE::NiAVObject* seen = nullptr;
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* root = p ? p->Get3D(false) : nullptr;
			if (!root || root == seen) return;
			seen = root;
			if (auto* g = KitGlobal(); g && g->value > 0.5f) {
				if (g->value < kKitVersion - 0.5f) {  // (Noah: the kit grew - the staffs, the Bane, the altar's spells, tier-3 bows) topped up
					logger::info("testkit: version {} -> {}: topping the kit up", g->value, kKitVersion);
					g_kitPending = true;
				}
			}
		}

		// (Noah: the test kit - unlimited magicka, so the spells can be tried) kept full while the kit is on
		void KitMagicka(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !TestKitOn()) return;
			// (Noah: the test spells cost nothing) every kit spell and every altar spell at a fixed cost of 0 - kept so each
			// second, as a form's cost isn't saved and the altar sets its spells' costs again each night
			static float zeroAt = 0;
			if ((zeroAt -= a_delta) <= 0) {
				zeroAt = 1.0f;
				const auto zero = [](RE::SpellItem* sp) {
					if (!sp) return;
					sp->data.costOverride = 0;
					sp->data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);
				};
				for (int i = 0; i < 64; ++i) zero(Arcana::Spell(i));
			}
			auto* av = p->AsActorValueOwner();
			const float have = av->GetActorValue(RE::ActorValue::kMagicka);
			const float max = av->GetPermanentActorValue(RE::ActorValue::kMagicka);
			if (have < max) {
				av->RestoreActorValue(RE::ActorValue::kMagicka, max - have);
			}
		}

		bool g_migrated = false;


		void HideVanillaBars()
		{
			if (Hud::kEdgeHud && !Hud::kOwnBars) return;  // (Edge UI's own bars show)
			static auto next = std::chrono::steady_clock::now();
			if (std::chrono::steady_clock::now() < next) {
				return;
			}
			next = std::chrono::steady_clock::now() + 250ms;
			auto* ui = RE::UI::GetSingleton();
			auto  hud = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
			if (!hud || !hud->uiMovie) {
				return;
			}
			for (const char* bar : { "_root.HUDMovieBaseInstance.Health._alpha", "_root.HUDMovieBaseInstance.Magica._alpha", "_root.HUDMovieBaseInstance.Stamina._alpha" }) {
				hud->uiMovie->SetVariable(bar, RE::GFxValue(0.0));
			}
			// (Noah: "just keep the Edge's shout meter") the shout meter is left alone - Skyrim's and Edge UI's
			// (Noah: "bring back the health, stamina and mana bars we made instead of Edge's") Edge UI's own bars may be
			// clips of other names: any of the HUD's clips named for health, magicka or stamina (not an enemy's) is hidden
			static std::set<std::string> told;
			if (static bool listed = false; !listed) {  // (once: every clip on the HUD, by name - to find the rest of Edge UI's pieces)
				RE::GFxValue base;
				if (hud->uiMovie->GetVariable(&base, "_root.HUDMovieBaseInstance") && base.IsObject()) {
					listed = true;
					std::string names;
					base.VisitMembers([&](const char* a_name, const RE::GFxValue& a_v) {
						if (a_name && a_v.IsDisplayObject()) names += std::string(a_name) + " ";
					});
					logger::info("hud: the HUD's clips: {}", names);
				}
			}
			for (const char* root : { "_root.HUDMovieBaseInstance", "_root" }) {
				RE::GFxValue obj;
				if (!hud->uiMovie->GetVariable(&obj, root) || !obj.IsObject()) continue;
				obj.VisitMembers([&](const char* a_name, const RE::GFxValue& a_v) {
					if (!a_name || !a_v.IsDisplayObject()) return;
					std::string low(a_name);
					std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					const bool vital = low.find("health") != std::string::npos || low.find("magica") != std::string::npos || low.find("stamina") != std::string::npos;
					if (!vital || low.find("enemy") != std::string::npos || low.find("boss") != std::string::npos) return;
					RE::GFxValue::DisplayInfo off;
					off.SetVisible(false);
					const_cast<RE::GFxValue&>(a_v).SetDisplayInfo(off);
					if (told.insert(std::string(root) + "." + a_name).second) logger::info("hud: Skyrim's / Edge UI's {}.{} hidden (our own bars instead)", root, a_name);
				});
			}
		}

		void Reset()
		{
			g_migrated = false;
			g_hits.clear();
			Spellcraft::Reset();
			OrbDown();  // (the Light Orb and its light hung again on the new game's body, never carried over a load)
			// (the audit: a load mid-unleash let the old game's stars fall, wells pull and storms crackle in the new one)
			g_arcs.clear(), g_dots.clear(), g_shrink.clear(), g_blasts.clear(), g_wells.clear(), g_vflash.clear(), g_pfire.clear(), (void)std::for_each(g_eb.begin(), g_eb.end(), [](EBurst& b) { b.fx.Detach(); }), g_eb.clear(), (void)std::for_each(g_lobs.begin(), g_lobs.end(), [](Lobbed& l) { l.fx.Detach(); }), g_lobs.clear();
			for (auto& l : g_slashes) l.fx.Detach();
			for (auto& r : g_grows) r.fx.Detach();
			for (auto& c : g_cyclones) c.fx.Detach();
			g_slashes.clear(), g_grows.clear(), g_cyclones.clear();
			g_zones.clear(), g_frozenUntil.clear();
			Props::g_twisters.clear();
			g_storm.charges = 0;
		}


		// Atronach Reforge: the weapon in your hands goes up a level (more damage, full ammo).
		int ReforgeCost()
		{
			const auto hands = Arcana::Hands();
			const int  sp = hands[1] >= 0 ? hands[1] : hands[0];
			if (sp >= 0) return Arcana::NextRung(sp) < 0 ? 0 : gen::kForgeCost[std::clamp(Arcana::RungOf(sp) - 1, 0, 2)];
			return -1;
		}
		// (Noah: harder to grow too strong) each weapon or spell takes one reforge a night
		std::unordered_set<int> g_forgedTonight;
		int                     g_forgedNight = -1;
		bool ForgedTonight(int a_key, const std::string& a_name)
		{
			const int night = Save::Get(gen::kAV_round);
			if (night != g_forgedNight) g_forgedNight = night, g_forgedTonight.clear();
			if (g_forgedTonight.contains(a_key)) {
				Hud::Banner(a_name + " has been reforged tonight - the altar needs a night to forge it again", 3.0f);
				return true;
			}
			return false;
		}
		bool Reforge()
		{
			{
				// (the classes overhaul) a class spell in hand (the right first) climbs its ladder: Reforge for Destruction,
				// Bind Pact for the Summoner, Imbue for the Spellsword - the next rung's spell takes its place and its hands
				const auto hands = Arcana::Hands();
				const int  sp = hands[1] >= 0 ? hands[1] : hands[0];
				if (sp < 0) {
					const int c0 = Classes::Get();
					Hud::Banner(c0 == 0 ? "Hold the summon spell you want to Bind" : c0 == 3 ? "Hold the touch you want to Imbue" : "Hold the spell you want to Reforge", 2.5f);
					return false;
				}
				const int cls = Arcana::ClassOf(sp);
				const char* verb = cls == 0 ? "Bind Pact" : cls == 3 ? "Imbue" : "Reforge";
				const int next = Arcana::NextRung(sp);
				const std::string name(Arcana::Name(sp));
				int freed = 0;
				for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) freed += Curse::Freed(t) ? 1 : 0;
				if (next < 0) {
					// (the classes' growth: nothing came after a Legendary) Ascend - the top of a ladder grows three times more,
					// each a share more power (a summon family: harder-hitting summons), at six freed altars, 120 kills each
					const int lv = Arcana::Level(sp);
					if (lv >= gen::kForgeMax) {
						Hud::Banner(name + " has ascended as far as it can", 2.5f);
						return false;
					}
					if (freed < 6 && !TestKitOn()) {
						Hud::Banner(name + " ascends once six Echo Altars are freed (" + std::to_string(freed) + " of 6)", 3.0f);
						return false;
					}
					if (Kit::NightLocked(4)) {  // (Noah: the altar's spells on the kit's schedule - an Ascend from night 18)
						Hud::Banner(name + " can ascend from night " + std::to_string(Kit::NightGate(4)), 3.0f);
						return false;
					}
					if (const int have = Classes::Mastery(sp), want = Classes::MasteryNeed(sp); have < want) {
						Hud::Banner(name + ": " + std::to_string(have) + " / " + std::to_string(want) + " kills with it before it can ascend", 3.0f);
						return false;
					}
					if (ForgedTonight(Arcana::kIdBase + sp, name)) return false;
					if (!Points::Spend(gen::kForgeCost[std::clamp(lv, 0, gen::kForgeMax - 1)])) return false;
					g_forgedTonight.insert(Arcana::kIdBase + sp);
					Classes::SpendMastery(sp);
					Arcana::SetLevel(sp, lv + 1);
					logger::info("forge: {} {} ascends to {}", verb, name, lv + 1);
					Hud::Banner(name + "  -  ASCENDED  " + std::string(lv + 1, '|'), 4.5f);
					return true;
				}
				// (the consistency pass: the old gates sat under the forge's own three) every class alike: a third rung waits for
				// four freed altars, a Legendary for six - as the Summoner's Legendary pacts do
				const int need = Arcana::TierOf(next) >= 3 ? 6 : Arcana::RungOf(next) >= 3 ? 4 : 0;
				if (freed < need && !TestKitOn()) {
					Hud::Banner(std::string(Arcana::Name(next)) + " waits until " + std::to_string(need) + " Echo Altars are freed (" + std::to_string(freed) + " of " + std::to_string(need) + ")", 3.0f);
					return false;
				}
				// (Noah: "Altar spell upgrades follow the same schedule") the second rung Tier I's night (3), the third Tier II's (7),
				// a Legendary Tier III's (12)
				if (const int tier = Arcana::TierOf(next) >= 3 ? 3 : Arcana::RungOf(next) >= 3 ? 2 : 1; Kit::NightLocked(tier)) {
					Hud::Banner(std::string(Arcana::Name(next)) + " can be raised from night " + std::to_string(Kit::NightGate(tier)), 3.0f);
					return false;
				}
				if (const int have = Classes::Mastery(sp), want = Classes::MasteryNeed(sp); have < want) {
					Hud::Banner(name + ": " + std::to_string(have) + " / " + std::to_string(want) + " kills with it before it can climb", 3.0f);
					return false;
				}
				if (ForgedTonight(Arcana::kIdBase + sp, name)) {
					return false;
				}
				const int cost = Arcana::TierOf(next) >= 3 ? gen::kForgeCost[2] : gen::kForgeCost[std::clamp(Arcana::RungOf(sp) - 1, 0, 1)];  // (a Legendary always the dearest)
				if (!Points::Spend(cost)) {
					return false;
				}
				g_forgedTonight.insert(Arcana::kIdBase + next);
				Classes::SpendMastery(sp);
				Arcana::Replace(sp, next);
				logger::info("forge: {} {} -> {}", verb, name, Arcana::Name(next));
				Hud::Banner(std::string(verb) + ":  " + name + "  ->  " + std::string(Arcana::Name(next)) + (Arcana::TierOf(next) >= 3 ? "  -  LEGENDARY" : ""), 4.5f);
				return true;
			}
		}

		bool OnButton(const RE::ButtonEvent* a_event)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ItemMenusOpen()) {
				return false;  // (the audit: G or the knife key in the inventory equipped from under it)
			}
			const bool down = a_event->IsDown(), up = !a_event->IsPressed();
			// (Noah: the Alchemist's flasks "should feel like a weapon") a flask held in a hand: that hand's attack throws it
			if (IsFireButton(a_event) || IsAimButton(a_event)) {
				const bool left = IsAimButton(a_event);
				// (Noah: "the carian wand won't cast") the wand's attack held: its casts (Classes::TickWand)
				if (!left && Classes::WandInHand()) {
					if (down) Classes::WandButton(true);
					else if (up) Classes::WandButton(false);
					return false;
				}
				if (!left && Tablet::StarWandInHand()) {   // (the Wandering Star: a wand, its stars while the attack is held)
					if (down) Tablet::StarButton(true);
					else if (up) Tablet::StarButton(false);
					return false;
				}
				// (Noah: the Mutagen Staff "just like a quarter staff") swung as one; the block button held looses its beam
				if (Classes::MutagenInHand()) {
					if (left && down) Classes::MutagenBlock(true);
					else if (left && up) Classes::MutagenBlock(false);
					return false;
				}
				if (Classes::FlaskInHand(left)) {
					if (down) Classes::FlaskAttack(left);
					else if (up) Classes::FlaskAttackUp(left);
					// (the Mutagen Staff: Skyrim's own staff cast plays while the attack is held - the beam is ours)
					if (Classes::FlaskElementInHand(left) != 6) return true;
				}
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
			// Magicka: nothing comes back until kManaRegenDelay (5 s) after the last magicka spent on anything (our
			// spells or Skyrim's); then it comes back fast (Quicksilver doubles it). Skyrim's own trickle is taken
			// back while it waits; a potion (a big jump at once) is kept.
			{
				static float last = -1;
				// (Noah: "the battle mage class still recovers mana after 5 seconds without casting but it should be 1s after
				// not casting - this is the class perk, it's more loved by mana") the Destruction class: 1 second
				const float  delay = Classes::Get() == 1 ? 1.0f : gen::kManaRegenDelay;  // (1: Destruction - src/CofferSpells.h)
				const float  max = av->GetPermanentActorValue(RE::ActorValue::kMagicka), cur = av->GetActorValue(RE::ActorValue::kMagicka);
				if (last >= 0 && cur < last - 0.01f) {
					g_manaQuiet = 0;  // spent
				} else {
					g_manaQuiet += a_delta;
				}
				if (last >= 0 && cur > last + 0.001f && cur - last < 0.02f * max + 0.5f && g_manaQuiet < delay) {
					av->DamageActorValue(RE::ActorValue::kMagicka, cur - last);  // Skyrim's own regeneration: not yet
				}
				const float now = av->GetActorValue(RE::ActorValue::kMagicka);
				if (g_manaQuiet >= delay && now < max) {
					av->RestoreActorValue(RE::ActorValue::kMagicka, std::min(max - now, max * gen::kManaRegenPerSecond * Perks::RegenMult() / Perks::ManaMult() * a_delta));
				}
				last = av->GetActorValue(RE::ActorValue::kMagicka);
			}
			// more stamina for every character (+100, given once), and it comes back sooner and faster
			if (constexpr int kBonus = 100; Save::StaminaBonus() < kBonus && !p->IsDead()) {
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kStamina, static_cast<float>(kBonus - Save::StaminaBonus()));
				Save::SetStaminaBonus(kBonus);
			}
			static float lastSp = -1, spQuiet = 0;
			const float  spMax = av->GetPermanentActorValue(RE::ActorValue::kStamina), sp = av->GetActorValue(RE::ActorValue::kStamina);
			spQuiet = (lastSp >= 0 && sp < lastSp - 0.01f) || p->AsActorState()->IsSprinting() ? 0 : spQuiet + a_delta;
			// (Noah: "my stamina regenerates too quickly to meditate properly" - Thunderchild's meditation at Kyne's statue drains it
			// a little each moment, too little for one frame to see) stamina falling at all over the last half second, or you
			// seated or kneeling in something: none of ours comes back
			{
				static float ref = -1, since = 0;
				if ((since += a_delta) >= 0.5f) {
					if (ref >= 0 && sp < ref - 0.0005f) spQuiet = 0;
					ref = sp, since = 0;
				}
				if (p->AsActorState()->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) spQuiet = 0;
			}
			// (Noah: "It drains it in the beginning for a second, and then it just refills the rest") Thunderchild's meditation
			// (its MeditationInProgress effect, or you seated on its meditation marker): your stamina only goes down - whatever
			// gives it back (Skyrim's own regeneration, a perk, another mod) is taken back at once
			{
				static RE::EffectSetting* med = nullptr;
				static RE::TESFurniture*  marker = nullptr;
				static bool               looked = false;
				if (!looked) {
					looked = true;
					auto* dh = RE::TESDataHandler::GetSingleton();
					med = dh->LookupForm<RE::EffectSetting>(0x06CAED, "Thunderchild - Epic Shout Package.esp");
					marker = dh->LookupForm<RE::TESFurniture>(0x068E47, "Thunderchild - Epic Shout Package.esp");
					logger::info("stamina: Thunderchild's meditation {}", med ? "known" : "not installed");
				}
				static float held = -1;
				bool meditating = med && p->AsMagicTarget()->HasMagicEffect(med);
				if (!meditating && marker) {
					auto f = p->GetOccupiedFurniture().get();
					meditating = f && f->GetBaseObject() == marker;
				}
				if (meditating) {
					spQuiet = 0;
					const float now = av->GetActorValue(RE::ActorValue::kStamina);
					if (held >= 0 && now > held + 0.001f) av->DamageActorValue(RE::ActorValue::kStamina, now - held);
					held = std::min(held < 0 ? now : held, av->GetActorValue(RE::ActorValue::kStamina));
				} else {
					held = -1;
				}
			}
			if (spQuiet >= 1.5f && sp < spMax) {
				av->RestoreActorValue(RE::ActorValue::kStamina, std::min(spMax - sp, spMax * 0.36f * a_delta));  // (Noah: "decrease the stamina regen speed by 40%, it's way too fast": 0.6 -> 0.36 of it a second)
			}
			lastSp = av->GetActorValue(RE::ActorValue::kStamina);
		}

		// ---- aim down sights: pull the first-person gun onto the view line so you look down its barrel ----
		struct Quat { float w, x, y, z; };
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



		void FangHit(RE::Actor* a_target, bool a_power) { Props::FangHit(a_target, a_power); }
		// ---- (Noah: "all 10 of the weapons from the priests act as normal weapons please make sure they actually have their
		// abilities") the Dragon Priests' weapons (docs/DRAGON_PRIEST_WEAPONS.md): every hit carries the priest's theme, and a
		// power attack (for the two bows: every sixth arrow that strikes) looses his signature. Their strength sits between the
		// Echo Altar's weapons and the class wonders: shares of one of the night's dead's whole health
		namespace
		{
			int PriestOf(RE::FormID a_src)
			{
				auto* dh = RE::TESDataHandler::GetSingleton();
				for (int i = 0; i < static_cast<int>(gen::kCityBosses.size()); ++i) {
					if (dh && dh->LookupFormID(gen::kCityBosses[i].weapon, gen::kPlugin) == a_src) return i;
				}
				return -1;
			}
			// a value changed on one of the dead for a while, then given back
			void ForAWhile(RE::Actor* a, RE::ActorValue a_av, float a_delta, float a_secs)
			{
				if (!a || a->IsDead()) return;
				// (Noah, clip: "the mist blade ... not doing what it says") a speed change only takes once the carry weight is
				// touched - the priest weapons' slows never showed
				auto apply = [a_av](RE::Actor* x, float d) {
					auto* av = x->AsActorValueOwner();
					av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, a_av, d);
					if (a_av == RE::ActorValue::kSpeedMult) {
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
					}
				};
				apply(a, a_delta);
				Tasks::Later(a_secs, [h = a->GetHandle(), apply, a_delta]() { if (auto x = h.get()) apply(x.get(), -a_delta); });
			}
			void Sleep(RE::Actor* a, float a_secs)   // (Vokun) laid down asleep: paralysed where they stand
			{
				if (!a || a->IsDead()) return;
				a->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 1.0f);
				Tasks::Later(a_secs, [h = a->GetHandle()]() { if (auto x = h.get(); x && !x->IsDead()) x->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f); });
			}
			std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_frozen, g_poisoned;   // (Zahkriisos', Rahgot's marks)
			bool Marked(std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point>& a_m, RE::Actor* a)
			{
				auto it = a_m.find(a->GetFormID());
				return it != a_m.end() && it->second > std::chrono::steady_clock::now();
			}
			void Mark(std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point>& a_m, RE::Actor* a, float a_secs)
			{
				if (a_m.size() > 400) a_m.clear();
				a_m[a->GetFormID()] = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(a_secs * 1000));
			}
			// (the Glacier Greatsword) frozen solid a few seconds at a time, again and again, while it lives and is marked
			std::unordered_set<RE::FormID> g_glacierHeld;
			void GlacierChain(RE::ActorHandle a_h)
			{
				auto a = a_h.get();
				if (!a) return;
				if (a->IsDead() || !Marked(g_frozen, a.get())) {
					g_glacierHeld.erase(a->GetFormID());
					return;
				}
				Freeze(a.get(), 3.0f);
				Tasks::Later(2.8f, [a_h]() { GlacierChain(a_h); });
			}
			void GlacierHold(RE::ActorHandle a_h)
			{
				auto a = a_h.get();
				if (a && g_glacierHeld.insert(a->GetFormID()).second) GlacierChain(a_h);
			}
			std::array<int, 10> g_arrows{};   // (the bows: arrows struck toward the sixth)
			std::array<std::chrono::steady_clock::time_point, 10> g_lastProc{};
			RE::NiPoint3 Chest(RE::Actor* a) { return a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }; }
			// drawn toward a point over a second (Vahlok's chains)
			void Drag(RE::Actor* a, RE::NiPoint3 a_to)
			{
				for (int k = 1; k <= 10; ++k) {
					Tasks::Later(0.1f * static_cast<float>(k), [h = a->GetHandle(), a_to]() {
						auto x = h.get();
						if (!x || x->IsDead()) return;
						RE::NiPoint3 at = x->GetPosition(), d = a_to - at;
						d.z = 0;
						const float L = d.Length();
						if (L < 160.0f) return;
						x->SetPosition(at + d * (std::min(L - 150.0f, 90.0f) / L), true);
					});
				}
			}
		}
		void PriestHit(RE::Actor* a_t, RE::FormID a_src, bool a_power)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !a_t || a_t == p || !Director::IsFoe(a_t)) return;
			const int w = PriestOf(a_src);
			if (w < 0) return;
			const bool bow = false;   // (Noah: the bows out of the loot - Vahlok's and Vokun's are melee now)
			const auto now = std::chrono::steady_clock::now();
			if (now - g_lastProc[w] < std::chrono::milliseconds(bow ? 50 : 250)) return;   // (one swing, one proc)
			g_lastProc[w] = now;
			const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
			bool heavy = a_power;
			if (bow && ++g_arrows[w] >= 6) g_arrows[w] = 0, heavy = true;
			const auto tp = a_t->GetPosition(), pp = p->GetPosition();
			RE::NiPoint3 fwd{ std::sin(p->GetAngleZ()), std::cos(p->GetAngleZ()), 0.0f };
			// (Noah: "all 10 of the dragon priest weapons still act as normal weapons") every hit shows its power plainly: its
			// element bursting on the one struck, with its sound - the quieter work (slows, poison, sleep) is then seen to land
			struct HitLook { int elem; float scale; const char* sound; };
			static constexpr HitLook kLook[10] = { { 6, 1.0f, "lightmassive" }, { 3, 1.1f, "light" }, { 3, 1.0f, "light" }, { 1, 1.1f, "frost" }, { 2, 1.0f, "runeshock" },
				{ 5, 1.0f, "poisongas" }, { 6, 0.9f, "light" }, { 4, 1.1f, "runeshock" }, { 6, 1.0f, "light" }, { 6, 1.2f, "lightmassive" } };
			ElemBurst(kLook[w].elem, Chest(a_t), kLook[w].scale * (heavy ? 1.6f : 1.0f), 0.0f, 0.6f);
			BlastSound(kLook[w].sound, tp, heavy ? 0.9f : 0.45f);
			if (w == 0 || w == 6) LightningArc(pp + RE::NiPoint3{ 0, 0, 80.0f }, Chest(a_t), 0.0f);   // (the Jailor's chain; the Grief Blade drinking your foe)
			if (static int told = 0; told++ < 40) logger::info("priest weapon: {} struck {}{}", gen::kCityBosses[w].weaponName, a_t->GetName(), heavy ? " (heavy)" : "");
			switch (w) {
			case 0:   // (Noah) Vahlok's Jailor's Keyhammer: shackled in place 2 s; a power attack - chains yank the dead within 6 m together
				// (Noah: "the abilities also don't work for this weapon" - a speed taken off for a while is put back by the night's own
				// pacing of the dead) held as a web holds them, which the night respects
				Director::Web(a_t->GetPosition(), 45.0f, 2.0f, 97.0f, false);
				LightningArc(pp + RE::NiPoint3{ 0, 0, 80.0f }, Chest(a_t), 0.0f);
				ElemBurst(6, Chest(a_t), 0.4f, 0.0f, 0.5f);
				if (heavy) {
					for (auto* f : Foes(tp, 420.0f)) {
						if (f == a_t) continue;
						LightningArc(Chest(f), Chest(a_t), 0.0f);
						Drag(f, tp);
						Tasks::Later(0.6f, [h = f->GetHandle()] { if (auto x = h.get(); x && !x->IsDead()) Director::Web(x->GetPosition(), 45.0f, 2.0f, 97.0f, false); });
					}
					BlastSound("lightmassive", tp, 0.8f);
				}
				break;
			case 1:   // Nahkriin's Mistblade: blinded (slowed, their blows weak); heavy - a fog where they wander lost
				Director::Web(a_t->GetPosition(), 45.0f, 3.0f, 45.0f, false);
				ForAWhile(a_t, RE::ActorValue::kAttackDamageMult, -0.5f, 3.0f);
				// (Noah: you couldn't see it work) the blinded wrapped in pale mist while it lasts
				if (auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kHallGhostShader, gen::kPlugin)) a_t->ApplyEffectShader(fx, heavy ? 6.0f : 3.0f);
				Motes(3, tp, 0.5f, 2.0f);
				if (heavy) {
					for (auto* f : Foes(tp, 600.0f)) {
						Director::Web(f->GetPosition(), 45.0f, 6.0f, 60.0f, false);
						ForAWhile(f, RE::ActorValue::kAttackDamageMult, -0.6f, 6.0f);
						if (auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kHallGhostShader, gen::kPlugin)) f->ApplyEffectShader(fx, 6.0f);
						f->StopCombat();
					}
					Motes(3, tp, 2.2f, 6.0f);
					ElemBurst(3, tp, 1.4f, 0.0f, 1.0f);
				}
				break;
			case 2: {   // (Noah) Volsung's Windcutter: "send gust blasts on light attack that stagger enemies and heavy attack is a larger
				// gust blast that knocks enemies on the ground" - light: a gust 8 m on ahead; heavy: a wide cone of wind 12 m out
				const auto from = pp + RE::NiPoint3{ 0, 0, 80.0f };
				if (!heavy) {
					for (auto* f : LineHurt(from, from + fwd * 560.0f, 110.0f, 0.3f)) Stagger(f, 0.6f);
					// (Noah: "The windgale swords wind animations are way too dense and its overwhelming") one light gust
					ElemBurst(3, from + fwd * 260.0f, 0.45f, 0.04f, 0.4f);
					BlastSound("light", pp + fwd * 300.0f, 0.4f);
				} else {
					for (auto* f : Foes(pp, 840.0f)) {
						RE::NiPoint3 off = f->GetPosition() - pp; off.z = 0;
						const float len = off.Length();
						if (len < 1.0f || (off.x * fwd.x + off.y * fwd.y) / len < 0.5f) continue;   // (a cone about 120 degrees wide ahead of you)
						AreaHurt(f->GetPosition(), 40.0f, 0.6f, "");
						Spellcraft::Fling(f, pp, 3.0f);
					}
					// (lighter: three gusts down the middle of the cone, not fifteen)
					for (int k = 1; k <= 3; ++k) ElemBurst(3, from + fwd * (240.0f * k), 0.6f + 0.15f * k, 0.07f * k, 0.5f);
					BlastSound("lightmassive", pp + fwd * 400.0f, 0.8f);
				}
				break;
			}
			case 3:   // Zahkriisos' Glacier Greatsword: frozen and slowed; heavy - the frozen round you shatter
				// (Noah: "the opponent should be frozen until they die. This makes the power attack actually effective") a hit
				// freezes it solid, held so while it lives - until a power attack shatters it
				Mark(g_frozen, a_t, 3600.0f);
				GlacierHold(a_t->GetHandle());
				ElemBurst(1, Chest(a_t), 0.5f, 0.0f, 0.5f);
				if (heavy) {
					for (auto* f : Foes(pp, 600.0f)) {
						if (!Marked(g_frozen, f) && f != a_t) continue;
						AreaHurt(f->GetPosition(), 140.0f, 1.6f, "");
						ElemBurst(1, Chest(f), 1.0f, 0.0f, 0.6f);
						g_frozen.erase(f->GetFormID());
					}
					BlastSound("frost", pp, 0.9f);
				}
				break;
			case 4: {   // Otar's Storm Blade: lightning leaps to two more; heavy - lightning arcs out all round you
				int n = 0;
				for (auto* f : Foes(tp, 450.0f)) {
					if (f == a_t || n >= 2) continue;
					LightningArc(Chest(a_t), Chest(f), 0.05f * static_cast<float>(n++));
					AreaHurt(f->GetPosition(), 80.0f, 0.3f, "");
				}
				if (heavy) {
					int k = 0;
					for (auto* f : Foes(pp, 650.0f)) {
						LightningArc(pp + RE::NiPoint3{ 0, 0, 90.0f }, Chest(f), 0.03f * static_cast<float>(k++));
						AreaHurt(f->GetPosition(), 90.0f, 1.0f, "");
						Knock(f, 0.5f);
					}
					BlastSound("runeshock", pp, 0.9f);
				}
				break;
			}
			case 5:   // Rahgot's Sacrifice Knife: poisoned - and the poison passes on to the next; heavy - the poisoned burst
				// (Noah: "killing enemies with the sacrifice knife they would explode into fire") its poison kills in poison, not fire
				Burn(a_t, whole * 0.05f, 4.0f, 3);
				Mark(g_poisoned, a_t, 6.0f);
				Tasks::Later(2.0f, [h = a_t->GetHandle(), whole]() {
					auto x = h.get();
					if (!x) return;
					for (auto* f : Foes(x->GetPosition(), 400.0f)) {
						if (f == x.get() || Marked(g_poisoned, f)) continue;
						Burn(f, whole * 0.04f, 4.0f, 3), Mark(g_poisoned, f, 6.0f);
						break;
					}
				});
				if (heavy) {
					for (auto* f : Foes(pp, 700.0f)) {
						if (!Marked(g_poisoned, f) && f != a_t) continue;
						AreaHurt(f->GetPosition(), 180.0f, 1.2f, "");
						ElemBurst(5, Chest(f), 0.8f, 0.0f, 0.6f);
						g_poisoned.erase(f->GetFormID());
					}
					BlastSound("poisongas", pp, 0.8f);
				}
				break;
			case 6: {   // Krosis' Grief Blade: each hit heals you; heavy - a wail that weakens all near
				auto* av = p->AsActorValueOwner();
				// (Noah: "The grief blade heals too much. It's way too strong" - 1% a hit, from 3%)
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.01f);
				if (heavy) {
					for (auto* f : Foes(pp, 700.0f)) {
						ForAWhile(f, RE::ActorValue::kAttackDamageMult, -0.4f, 8.0f);
						Director::Web(f->GetPosition(), 45.0f, 8.0f, 30.0f, false);
						ForAWhile(f, RE::ActorValue::kDamageResist, -150.0f, 8.0f);
					}
					ElemBurst(4, pp + RE::NiPoint3{ 0, 0, 60.0f }, 1.6f, 0.0f, 0.8f);
					BlastSound("light", pp, 0.9f);
				}
				break;
			}
			case 7:   // Hevnoraak's Sealbreaker: armour shattered; heavy - a shockwave through the ground
				ForAWhile(a_t, RE::ActorValue::kDamageResist, -200.0f, 8.0f);
				if (heavy) {
					AreaHurt(pp, 520.0f, 1.4f, "");
					for (auto* f : Foes(pp, 520.0f)) Knock(f, 0.9f);
					MeteorImpact(pp, false, 0.8f);
				}
				break;
			case 8:   // (Noah) Vokun's Nightcaller's Scythe: asleep 1.5 s; a power sweep - a nightmare fog: the dead in it slowed and lost 6 s
				// (Noah: "the night caller scythe abilities don't work" - Paralysis set by hand lays no one down) held where they
				// stand, as a web holds them, wrapped in the dream's pale mist
				Director::Web(a_t->GetPosition(), 45.0f, 1.5f, 97.0f, false);
				if (auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kHallGhostShader, gen::kPlugin)) a_t->ApplyEffectShader(fx, 1.5f);
				Motes(6, tp, 0.5f, 2.0f);
				if (heavy) {
					const RE::NiPoint3 at = pp + fwd * 200.0f;
					for (auto* f : Foes(at, 420.0f)) {
						Director::Web(f->GetPosition(), 45.0f, 6.0f, 60.0f, false);
						ForAWhile(f, RE::ActorValue::kAttackDamageMult, -0.5f, 6.0f);
						if (auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kHallGhostShader, gen::kPlugin)) f->ApplyEffectShader(fx, 6.0f);
						f->StopCombat();
					}
					Motes(6, at, 2.2f, 6.0f);
					ElemBurst(6, at, 1.4f, 0.0f, 1.0f);
				}
				break;
			case 9:   // Morokei's Arcane Maul: magic bursts where it strikes; heavy - a magicka shockwave
				// (Noah: "the arcane maul doesn't have any effect for light attack") a burst of arcane light you can see, and its
				// sound, where it lands - the dead round it hurt and staggered
				AreaHurt(tp, 200.0f, 0.35f, "");
				QueueFx("CurseOfAlduin\\fx\\arcane_ring.nif", tp, 0.45f, 0.0f, 1.0f, true);
				ElemBurst(6, Chest(a_t), 1.0f, 0.0f, 0.6f);
				BlastSound("runeshock", tp, 0.5f);
				for (auto* f : Foes(tp, 200.0f)) Stagger(f, 0.4f);
				if (heavy) {
					AreaHurt(pp, 600.0f, 1.5f, "");
					QueueFx("CurseOfAlduin\\fx\\arcane_ring.nif", pp, 1.2f, 0.0f, 2.0f, true);
					p->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, 40.0f);
				}
				break;
			default: break;
			}
		}
		void StormHit(RE::Actor* a_target)
		{
			if (a_target && !g_stormBusy && g_storm.charges > 0 && !a_target->IsDead() && a_target != RE::PlayerCharacter::GetSingleton()) {
				StormStrike(a_target);
			}
		}
		int StormRow()
		{
			static int row = -1;
			for (int r = 0; row < 0 && r < static_cast<int>(gen::kSpellcraft.size()); ++r) {
				if (gen::kSpellcraft[r].id == "stormcaller") row = r;
			}
			return std::max(row, 0);
		}
		// (Noah: "I love the Red Lightning Strike and the Soulfall ... Soulfall for elites and bosses, and Red Lightning Strike
		// for all the other enemies. Don't make the lightning strike too big, otherwise it'll be distracting when a bunch of
		// enemies are spawning in at once") how the night's dead arrive
		int RedRow()
		{
			static int row = -1;
			for (int r = 0; row < 0 && r < static_cast<int>(gen::kSpellcraft.size()); ++r) {
				if (gen::kSpellcraft[r].id == "drill") row = r;   // (its colour: blood red)
			}
			return std::max(row, 0);
		}
		// (Noah, then: "the lightning strikes are elites and bosses, a red lightning strike") the great ones come down in a
		// blood-red bolt - three jagged strands, a burst, a blood ring racing out, the crack of it - a size above a common arrival
		void RedStrikeImpl(const RE::NiPoint3& a_at)
		{
			const int row = RedRow();
			const RE::NiPoint3 top = a_at + RE::NiPoint3{ static_cast<float>(std::rand() % 160 - 80), static_cast<float>(std::rand() % 160 - 80), 1500.0f };
			Spellcraft::StreakAt(row, top, a_at, 1.8f, 0.3f, true);
			Spellcraft::StreakAt(row, top + RE::NiPoint3{ 30.0f, -22.0f, -120.0f }, a_at, 1.0f, 0.24f, true);
			Spellcraft::StreakAt(row, top + RE::NiPoint3{ -26.0f, 30.0f, -60.0f }, a_at, 0.6f, 0.2f, true);
			Spellcraft::BurstAt(row, a_at + RE::NiPoint3{ 0, 0, 14.0f }, 34.0f, 0.35f);
			ElemBurst(11, a_at + RE::NiPoint3{ 0, 0, 30.0f }, 0.7f, 0.0f, 0.5f);   // (crimson)
			Ring("blood", a_at, 20.0f, 170.0f, 0.5f);
			Motes(5, a_at, 0.6f, 1.4f);
			Audio::AtPoint(gen::Sound::hit_stormcaller, a_at, 0.0f, 0.7f);
		}
		// (Noah: "I want the normal enemies to be the sunbeam from the sky animation that we made ... but painted red instead of
		// gold") the Sky Beam's bolt of light dropping out of the sky, its burst, its pillar and rising motes - all red, and kept
		// small and short so a crowd of arrivals stays readable; about 0.4 s from the call to the strike (kRedBeamFall)
		void RedBeamImpl(const RE::NiPoint3& a_at)
		{
			Lob("CurseOfAlduin\\fx\\red_bolt.nif", a_at + RE::NiPoint3{ 0, 0, 1800.0f }, RE::NiPoint3{ 0, 0, -4200.0f }, 0.8f, [](const RE::NiPoint3& at) {
				QueueFx("CurseOfAlduin\\fx\\red_burst.nif", at, 0.7f, 0.0f, 1.4f, false);
				if (g_cyclones.size() < 4) {
					CycloneSpec cs;
					cs.model = "CurseOfAlduin\\fx\\red_pillar.nif";
					cs.from = at, cs.fwd = { 0, 1, 0 }, cs.dist = 0.0f, cs.life = 0.9f, cs.size = 0.6f, cs.fec = -1, cs.share = 0.0f, cs.noPull = true;
					ElemCyclone(std::move(cs));
				}
				QueueFx("CurseOfAlduin\\fx\\red_motes.nif", at, 0.7f, 0.1f, 1.2f, false);
			}, 0.0f, 0.0f);
		}
		struct SoulFallFx { RE::NiPoint3 from, to, last; float t = 0, dur = 0.7f, ember = 0; };
		std::vector<SoulFallFx> g_soulFalls;
		void SoulFallImpl(const RE::NiPoint3& a_at, float a_secs)
		{
			if (g_soulFalls.size() >= 8) return;
			// (from the moon's side of the sky: high and well off to one side, so it reads as a falling star)
			const float a = static_cast<float>(std::rand() % 628) / 100.0f;
			const RE::NiPoint3 from = a_at + RE::NiPoint3{ std::cos(a) * 1400.0f, std::sin(a) * 1400.0f, 2600.0f };
			g_soulFalls.push_back({ from, a_at, from, 0.0f, std::max(0.2f, a_secs), 0.0f });
			Audio::AtPoint(gen::Sound::rise, a_at, 0.0f, 0.5f);
		}
		void TickSoulFalls(float a_delta)
		{
			if (g_soulFalls.empty()) return;
			const int row = RedRow();
			for (auto& f : g_soulFalls) {
				f.t += a_delta;
				const float u = std::min(1.0f, f.t / f.dur), e = u * u;   // (gathering speed as it falls)
				const RE::NiPoint3 p = f.from + (f.to - f.from) * e;
				Spellcraft::StreakAt(row, f.last, p, 1.6f, 0.4f, false);   // (its tail: each step's streak fading behind it)
				if ((f.ember -= a_delta) <= 0) f.ember = 0.05f, Spellcraft::BurstAt(row, p, 6.0f, 0.18f);   // (its burning head)
				f.last = p;
				if (u >= 1.0f) {
					ElemBurst(11, f.to + RE::NiPoint3{ 0, 0, 30.0f }, 0.8f, 0.0f, 0.6f);   // (crimson)
					Motes(5, f.to, 0.6f, 1.6f);                                               // (embers of blood rising)
					Ring("blood", f.to, 20.0f, 160.0f, 0.5f);
					Spellcraft::BurstAt(row, f.to + RE::NiPoint3{ 0, 0, 20.0f }, 30.0f, 0.35f);
					Audio::AtPoint(gen::Sound::boom_drill, f.to, 0.0f, 0.45f);
				}
			}
			std::erase_if(g_soulFalls, [](const SoulFallFx& f) { return f.t >= f.dur; });
		}
		void TickStorm(float a_delta)
		{
			for (auto& a : g_arcs) {
				if ((a.delay -= a_delta) <= 0) {
					Spellcraft::StreakAt(StormRow(), a.from, a.to, 1.3f, 0.45f, true);
					Spellcraft::BurstAt(StormRow(), a.to, 26.0f, 0.3f);
				}
			}
			std::erase_if(g_arcs, [](const Arc& a) { return a.delay <= 0; });
			if (g_storm.charges <= 0) {
				return;
			}
			if ((g_storm.left -= a_delta) <= 0) {
				g_storm.charges = 0;
				logger::info("shout: Stormblast charge faded");
				return;
			}
			// the charge crackles round your hands while it lasts
			if ((g_storm.spark -= a_delta) <= 0) {
				g_storm.spark = 0.35f;
				auto* p = RE::PlayerCharacter::GetSingleton();
				for (const char* hand : { "NPC R Hand [RHnd]", "NPC L Hand [LHnd]" }) {
					if (auto* root = p ? p->Get3D(Spellcraft::FirstPerson()) : nullptr) {
						if (auto* h = root->GetObjectByName(hand)) {
							Spellcraft::BurstAt(StormRow(), h->world.translate, 8.0f, 0.15f);
						}
					}
				}
			}
		}



		void Tick(float a_delta)
		{
			KitOnLoad();
			KitMagicka(a_delta);
			if (g_kitPending) {
				static float wait = 0;
				if ((wait += a_delta) > 5.0f) {
					wait = 0;
					TestKit();
				}
			}
			TickStorm(a_delta);
			TickSoulFalls(a_delta);
			Props::TickGauntlet(a_delta);
			Props::TickFang(a_delta);
			TickZones(a_delta);
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
				{
					REL::Relocation<std::uintptr_t> v0{ RE::VTABLE_ActivateHandler[0] };
					MenuEater<0>::func = v0.write_vfunc(0x1, MenuEater<0>::thunk);
					REL::Relocation<std::uintptr_t> v1{ RE::VTABLE_JumpHandler[0] };
					MenuEater<1>::func = v1.write_vfunc(0x1, MenuEater<1>::thunk);
					REL::Relocation<std::uintptr_t> v2{ RE::VTABLE_MenuOpenHandler[0] };
					MenuEater<2>::func = v2.write_vfunc(0x1, MenuEater<2>::thunk);
					REL::Relocation<std::uintptr_t> v3{ RE::VTABLE_FirstPersonState[1] };
					MenuEater<3>::func = v3.write_vfunc(0x1, MenuEater<3>::thunk);
					REL::Relocation<std::uintptr_t> v4{ RE::VTABLE_ThirdPersonState[1] };
					MenuEater<4>::func = v4.write_vfunc(0x1, MenuEater<4>::thunk);
					// (Noah: W A S D move through the altar's menu) the walk keys are the menu's while it is open
					REL::Relocation<std::uintptr_t> v5{ RE::VTABLE_MovementHandler[0] };
					MenuEater<5>::func = v5.write_vfunc(0x1, MenuEater<5>::thunk);
				}
				hooked = true;
				logger::info("gun: attack/ready input hooks installed");
			}
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
			TickDots(a_delta);
			TickMana(a_delta);
			TickSwingWatch();
			TickBlasts(a_delta);
			TickWells(a_delta);
			Spellcraft::Tick(a_delta);
			TickEBursts(a_delta);
			TickLobs(a_delta);
			TickGrows(a_delta);
			TickCyclones(a_delta);
			TickLightOrb(a_delta);
			TickWeaponFlame();
			TrackBlade(a_delta);
			TickSlashes(a_delta);
			TickSkyBeams(a_delta);
			TickHalo(a_delta);
			for (auto& d : g_devFx) if ((d.left -= a_delta) <= 0) d.fx.Detach();
			std::erase_if(g_devFx, [](auto& d) { return d.left <= 0; });
			TickThrow(a_delta);
			if (auto* cam = RE::PlayerCamera::GetSingleton()) {
				// the field of view is the player's own (Settings: first person and third person each)
				auto& rd = cam->GetRuntimeData2();
				rd.worldFOV = cam->IsInFirstPerson() ? Settings::FovFirst() : Settings::FovThird();
				// the hands follow the same slider: a wider view draws them smaller and further off, a narrower one closer
				rd.firstPersonFOV = std::clamp(gen::kViewmodelFov + (Settings::FovFirst() - 100.0f) * 0.6f, 55.0f, 110.0f);
			}
			// (Noah: "an ADS sensitivity slider") while you aim - a drawn bow - the look turns at
			// Skyrim's own sensitivity times ours; let go (or open a menu) and Skyrim's own comes back exactly as it was
			{
				static float skyrimSens = -1.0f;  // Skyrim's own while ours is applied (-1: not applied)
				auto*       pl = RE::PlayerCharacter::GetSingleton();
				float       aim = 0.0f;
				if (pl) {
					const auto as = pl->AsActorState()->GetAttackState();
					// (Noah: "the look sensitivity while holding the crossbow should be the same as the bow - it's very slow")
					// a crossbow sits loaded - Skyrim's "drawn" - the whole time it is in hand, so only a bow's draw counts as aiming
					auto* held = pl->GetEquippedObject(false);
					auto* hw = held ? held->As<RE::TESObjectWEAP>() : nullptr;
					const bool crossbow = hw && hw->IsCrossbow();
					if (!crossbow && (as == RE::ATTACK_STATE_ENUM::kBowDraw || as == RE::ATTACK_STATE_ENUM::kBowAttached || as == RE::ATTACK_STATE_ENUM::kBowDrawn)) aim = 1.0f;
				}
				auto* ui = RE::UI::GetSingleton();
				const bool menus = ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::JournalMenu::MENU_NAME) || ui->IsMenuOpen(RE::MainMenu::MENU_NAME));
				auto* prefs = RE::INIPrefSettingCollection::GetSingleton();
				auto* set = prefs ? prefs->GetSetting("fMouseHeadingSensitivity:Controls") : nullptr;
				if (set) {
					const float mult = Settings::AdsSensitivity();
					if (aim > 0.01f && !menus && std::abs(mult - 1.0f) > 0.001f) {
						if (skyrimSens < 0) skyrimSens = set->data.f;
						set->data.f = skyrimSens * (1.0f + (mult - 1.0f) * aim);
					} else if (skyrimSens >= 0) {
						set->data.f = skyrimSens, skyrimSens = -1.0f;
					}
				}
			}
			const auto now = Clock::now();
			std::erase_if(g_hits, [&](const auto& kv) { return now - kv.second.first > 2s; });

			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.points = Points::Get();
			h.perks = Save::PerkBits();
			h.perkSlots = Perks::SlotsNow();
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()) && i < 32; ++i) {
				h.perkStacks[i] = Save::PerkStacks(i);
			}
			h.handSpell = Arcana::Hands();  // the Coffer spells in hand (the spell dock)
			for (int i = 0; i < 2; ++i) {  // their real cost: the halved price, the reforge and your own skill and perks
				auto* sp = Arcana::Spell(h.handSpell[i]);
				h.handCost[i] = sp ? static_cast<int>(std::lround(sp->CalculateMagickaCost(RE::PlayerCharacter::GetSingleton()))) : 0;
			}
			{  // Skyrim's own (and other mods') equipment in hand, for the dock
				auto* pl = RE::PlayerCharacter::GetSingleton();
				for (int i = 0; i < 2; ++i) {
					auto* o = pl->GetEquippedObject(i == 0);
					h.skyKind[i] = 0, h.skyName[i].clear(), h.skyValue[i] = 0, h.skyColor[i] = 0xC8A060;
					if (!o || (o->GetFile(0) && o->GetFile(0)->GetFilename() == gen::kPlugin)) {
						continue;  // (ours: the altar's spells, bows and the night's weapons have docks of their own)
					}
					if (i == 0 && o == pl->GetEquippedObject(false)) {
						continue;  // (a two-handed thing: shown once, in the right hand)
					}
					h.skyName[i] = o->GetName();
					if (auto* w = o->As<RE::TESObjectWEAP>()) {
						const bool ranged = w->IsBow() || w->IsCrossbow();
						h.skyKind[i] = ranged ? 2 : w->IsStaff() ? 5 : 1;
						h.skyValue[i] = w->IsStaff() ? 0 : static_cast<int>(w->GetAttackDamage());
					} else if (auto* sp = o->As<RE::SpellItem>()) {
						h.skyKind[i] = 3;
						h.skyValue[i] = static_cast<int>(std::lround(sp->CalculateMagickaCost(pl)));
						if (auto* fx = sp->GetCostliestEffectItem(); fx && fx->baseEffect) {
							const auto rv = fx->baseEffect->data.resistVariable;
							h.skyColor[i] = rv == RE::ActorValue::kResistFire ? 0xFF7A2E : rv == RE::ActorValue::kResistFrost ? 0x8FD8FF : rv == RE::ActorValue::kResistShock ? 0x8FA8FF
							              : fx->baseEffect->data.associatedSkill == RE::ActorValue::kRestoration ? 0xFFE08A : fx->baseEffect->data.associatedSkill == RE::ActorValue::kConjuration ? 0xB98CFF
							              : fx->baseEffect->data.associatedSkill == RE::ActorValue::kIllusion ? 0xD08CFF : fx->baseEffect->data.associatedSkill == RE::ActorValue::kAlteration ? 0x9CE0C0 : 0xA0B8FF;
						}
					} else if (o->Is(RE::FormType::Armor)) {
						h.skyKind[i] = 4;
						if (auto* ar = o->As<RE::TESObjectARMO>()) h.skyValue[i] = static_cast<int>(ar->GetArmorRating());
					} else {
						h.skyKind[i] = 6;
					}
				}
			}
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

		// ================================================================ the cities' shouts (sheet: shouts)
		void Console(const std::string& a_cmd)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>(); factory && player) {
				if (auto* script = factory->Create()) {
					script->SetCommand(a_cmd);
					script->CompileAndRun(player);
					delete script;
				}
			}
		}
		// the city's shout: Thunderchild's (sheet: shouts plugin/shoutId) when it's installed, else our own
		RE::TESShout* ExtShout(int a_i)
		{
			if (a_i < 0 || a_i >= static_cast<int>(gen::kShouts.size()) || gen::kShouts[a_i].extPlugin.empty()) {
				return nullptr;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh->LookupModByName(gen::kShouts[a_i].extPlugin) ? dh->LookupForm<RE::TESShout>(gen::kShouts[a_i].extShout, gen::kShouts[a_i].extPlugin) : nullptr;
		}
		RE::TESShout* ShoutForm(int a_i)
		{
			if (auto* ext = ExtShout(a_i)) {
				return ext;
			}
			return a_i >= 0 && a_i < static_cast<int>(gen::kShouts.size()) ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESShout>(gen::kShouts[a_i].shout, gen::kPlugin) : nullptr;
		}
		bool KnowsShout(int a_i)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* sh = ShoutForm(a_i);
			return p && sh && p->HasShout(sh);
		}
		void LearnShout(int a_i)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* sh = ShoutForm(a_i);
			if (!p || !sh) {
				return;
			}
			// the three words learned and unlocked (no dragon souls asked), the shout in the Magic menu
			if (ExtShout(a_i) == sh) {
				for (auto& v : sh->variations) {
					if (v.word) {
						Console(fmt::format("player.teachword {:08X}", v.word->GetFormID()));
						Console(fmt::format("player.unlockword {:08X}", v.word->GetFormID()));
					}
				}
				p->AddShout(sh);
				// its mod's questline begins with it (Thunderchild: Acolyte of the Voice), unless it already has
				if (const auto q = gen::kShouts[a_i].quest; !q.empty()) {
					auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>(q);
					if (!quest || (!quest->IsEnabled() && !quest->IsCompleted())) {
						Console(fmt::format("startquest {}", q));
						logger::info("shout: started its questline {}", q);
					}
				}
				logger::info("shout: learned {} from {} ({:08X})", gen::kShouts[a_i].id, gen::kShouts[a_i].extPlugin, sh->GetFormID());
				Hud::ShoutLearned(gen::kShouts[a_i].label, std::string(gen::kShouts[a_i].w1) + " " + std::string(gen::kShouts[a_i].w2) + " " + std::string(gen::kShouts[a_i].w3));
				return;
			}
			for (auto w : gen::kShouts[a_i].words) {
				if (auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(w, gen::kPlugin)) {
					Console(fmt::format("player.teachword {:08X}", f->GetFormID()));
					Console(fmt::format("player.unlockword {:08X}", f->GetFormID()));
				}
			}
			p->AddShout(sh);
			logger::info("shout: learned {} ({:08X})", gen::kShouts[a_i].id, sh->GetFormID());
			Hud::ShoutLearned(gen::kShouts[a_i].label, std::string(gen::kShouts[a_i].w1) + " " + std::string(gen::kShouts[a_i].w2) + " " + std::string(gen::kShouts[a_i].w3));
		}
		int ShoutOfSpell(RE::FormID a_spell, int& a_level)
		{
			for (int i = 0; i < static_cast<int>(gen::kShouts.size()); ++i) {
				for (int n = 0; n < 3; ++n) {
					if (auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(gen::kShouts[i].spells[n], gen::kPlugin); f && f->GetFormID() == a_spell) {
						a_level = n + 1;
						return i;
					}
				}
				// Thunderchild's Stormblast: its spells are its own, but the charge is ours (its enchanting only takes on a
				// plain weapon, never a gun, bow or spell in hand)
				if (gen::kShouts[i].id == "gale") {
					if (auto* ext = ExtShout(i)) {
						for (int n = 0; n < 3; ++n) {
							if (auto* sp = ext->variations[n].spell; sp && sp->GetFormID() == a_spell) {
								a_level = n + 1;
								return i;
							}
						}
					}
				}
			}
			return -1;
		}

		// One word, two or all three: every effect grows with the words spoken.
		void Shout(int a_i, int a_level)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || a_i < 0 || a_i >= static_cast<int>(gen::kShouts.size())) {
				return;
			}
			const auto& sh = gen::kShouts[a_i];
			const int   L = std::clamp(a_level, 1, 3);
			const float pick[3] = { 1.0f, 1.8f, 3.2f };
			const float k = pick[L - 1];
			const float base = Director::RoundHealth() * gen::kRounds.hpScale * 0.8f * k;   // BO3 units: most of a zombie's night health per word
			const Aim   aim = AimAt(4000.0f);
			const auto  me = p->GetPosition();
			auto*       proc = p->GetActorRuntimeData().currentProcess;
			auto        blastAt = [&](std::string_view a_look, const RE::NiPoint3& a_at, float a_delay, float a_scale) { Blast(BlastIndex(a_look), a_at, a_delay, a_scale); };
			auto        burn = [&](RE::Actor* a, float a_perSec, float a_secs) { g_dots.push_back({ a->GetHandle(), a_perSec, a_secs }); };
			const std::string_view id = sh.id;
			struct Busy { Busy() { g_stormBusy = true; } ~Busy() { g_stormBusy = false; } } busy;   // (a shout's own blows spend no charge)
			// (Noah: every city's shout redone; all three words come with the city) - the damage follows the night (base) and
			// Skyrim's difficulty (Damage), each word wider and stronger
			if (id == "ember") {        // Skyforge Volley: a fan of spectral blades
				const int   n = 1 + 4 * L;                 // 5, 9, 13 blades
				const float spread = 0.35f + 0.12f * L;    // radians either side
				RE::NiPoint3 fwd = aim.dir; fwd.z = 0; if (fwd.Length() < 0.01f) fwd = { 0, 1, 0 }; fwd.Unitize();
				std::unordered_set<RE::Actor*> hit;
				for (int b = 0; b < n; ++b) {
					const float a = -spread + 2.0f * spread * b / std::max(1, n - 1);
					const RE::NiPoint3 d{ fwd.x * std::cos(a) - fwd.y * std::sin(a), fwd.x * std::sin(a) + fwd.y * std::cos(a), 0 };
					for (int st = 1; st <= 5; ++st) blastAt("light", me + RE::NiPoint3{ 0, 0, 70 } + d * (260.0f * st), 0.03f * st + 0.01f * b, 0.3f);
					Aim ray{ me + RE::NiPoint3{ 0, 0, 70 }, d, me + d * 1500.0f };
					for (auto* f : InLine(ray, 1300.0f + 200.0f * L, 70.0f)) {
						if (!hit.insert(f).second) continue;
						if (Damage(f, base * 0.9f, false, false)) continue;
						Director::Web(f->GetPosition(), 40.0f, 1.5f, 90.0f);  // pinned
						if (L >= 2) burn(f, base * 0.12f, 4.0f);              // bleeding
						if (L >= 3) {                                          // and the blades burst
							blastAt("shoutarea", f->GetPosition() + RE::NiPoint3{ 0, 0, 50 }, 0.4f, 0.4f);
							for (auto* g2 : Foes(f->GetPosition(), 180.0f)) Damage(g2, base * 0.5f, false, false);
						}
					}
				}
			} else if (id == "gale") {  // Thunder Lash: bolts on the nearest dead, each leaping to one more
				auto foes = Foes(me, 1800.0f);
				std::sort(foes.begin(), foes.end(), [&](RE::Actor* a, RE::Actor* b) { return a->GetPosition().GetDistance(me) < b->GetPosition().GetDistance(me); });
				const int n = L == 1 ? 3 : L == 2 ? 5 : 8;
				for (int k = 0; k < n && k < static_cast<int>(foes.size()); ++k) {
					auto* f = foes[k];
					Cast(gen::kSpell_thunderbolt, f);
					blastAt("skybolt", f->GetPosition(), 0.08f * k, 0.6f);
					Damage(f, base * 1.2f, false, false);
					for (auto* g2 : Foes(f->GetPosition(), 350.0f)) {
						if (g2 != f && !g2->IsDead()) { g_arcs.push_back({ f->GetPosition() + RE::NiPoint3{ 0, 0, 80 }, g2->GetPosition() + RE::NiPoint3{ 0, 0, 80 }, 0.08f * k + 0.05f }); Damage(g2, base * 0.5f, false, false); break; }
					}
				}
			} else if (id == "wail") {  // Raise the Fallen: the barrow's dead fight for you
				Echo::Allies(0, 2 * L, 12.0f + 4.0f * L, me);
				for (int k = 0; k < 2 * L; ++k) blastAt("dark", me + RE::NiPoint3{ std::cos(k * 1.3f) * 170.0f, std::sin(k * 1.3f) * 170.0f, 30 }, 0.05f * k, 0.4f);
			} else if (id == "rime") {  // Frost Nova: everything round you frozen solid (the frozen shatter)
				const float r = 450.0f + 200.0f * L, secs = L == 1 ? 3.0f : L == 2 ? 4.0f : 6.0f;
				for (auto* f : Foes(me, r)) {
					if (!Damage(f, base * 0.4f, false, false)) FreezeSolid(f, secs);
				}
				for (int k = 0; k < 10; ++k) blastAt("frost", me + RE::NiPoint3{ std::cos(k * 0.628f) * r * 0.7f, std::sin(k * 0.628f) * r * 0.7f, 30 }, 0.02f * k, 0.5f);
				blastAt("icevolley", me + RE::NiPoint3{ 0, 0, 30 }, 0.0f, 0.7f);
			} else if (id == "quake") {  // Dwemer Quake: the ground heaves in front of you
				const float range = 800.0f + 300.0f * L;
				for (auto* f : InCone(aim, range, 45.0f)) {
					if (Damage(f, base, false, false)) continue;
					if (proc) { RE::NiPoint3 away = f->GetPosition() - me; away.z = 0; away.Unitize(); proc->KnockExplosion(f, f->GetPosition() - away * 100.0f + RE::NiPoint3{ 0, 0, -60 }, 1.5f + 0.8f * L); }
					if (L >= 3) { blastAt("giantslam", f->GetPosition(), 0.3f, 0.4f); Damage(f, base * 0.8f, false, false); }  // stone spikes
				}
				for (int st = 1; st <= 6; ++st) blastAt("giantslam", me + RE::NiPoint3{ aim.dir.x, aim.dir.y, 0 } * (range * st / 6.0f), 0.06f * st, 0.45f);
			} else if (id == "shroud") {  // Shadow Step: blink forward, a shadow left behind draws the dead and bursts
				const RE::NiPoint3 from = me;
				RE::NiPoint3 d = aim.dir; d.z = 0; if (d.Length() < 0.01f) d = { 0, 1, 0 }; d.Unitize();
				const float want = std::min(500.0f + 200.0f * L, std::max(0.0f, std::hypot(aim.hit.x - me.x, aim.hit.y - me.y) - 60.0f));
				RE::NiPoint3 to = me + d * want;
				if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(to, land) && std::abs(land - me.z) < 400.0f) to.z = land;
				blastAt("dark", from + RE::NiPoint3{ 0, 0, 60 }, 0.0f, 0.5f);
				p->SetPosition(to, true);
				blastAt("dark", to + RE::NiPoint3{ 0, 0, 60 }, 0.0f, 0.4f);
				g_zones.push_back({ 5, from, 600.0f + 150.0f * L, 3.0f, 0.0f, base * (0.8f + 0.5f * L), 0, false, 2.6f });
			} else if (id == "toll") {  // Death's Harvest: a draining cone that heals you for each it touches
				int n = 0;
				for (auto* f : InCone(aim, 800.0f + 200.0f * L, 40.0f + 5.0f * L)) {
					Damage(f, base * 0.9f, false, false);
					blastAt("turnundead", f->GetPosition() + RE::NiPoint3{ 0, 0, 60 }, 0.04f * n, 0.3f);
					++n;
				}
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * std::min(0.6f, (L == 3 ? 0.06f : 0.04f) * n));
				if (L >= 3) Party::ReviveAt(me, 700.0f);
			} else if (id == "mire") {  // Mire Grasp: roots hold the dead where you look, and poison them
				const float r = 280.0f + 110.0f * L;
				for (auto* f : Foes(aim.hit, r)) {
					Director::Web(f->GetPosition(), 45.0f, 2.0f + 1.5f * L, 96.0f);
					burn(f, base * 0.15f, 6.0f);
				}
				blastAt("poisongas", aim.hit, 0.0f, 0.5f + 0.15f * L);
				for (int k = 0; k < 4 + 2 * L; ++k) blastAt("spiderspit", aim.hit + RE::NiPoint3{ Rand(-r, r) * 0.7f, Rand(-r, r) * 0.7f, 10 }, 0.05f * k, 0.35f);
			} else if (id == "dread") {  // Nightmare Howl: thrown back in terror, the weakest die of fright
				const float cut = 0.1f + 0.1f * L;
				for (auto* f : Foes(me, 600.0f + 300.0f * L)) {
					auto*       av = f->AsActorValueOwner();
					const float frac = av->GetActorValue(RE::ActorValue::kHealth) / std::max(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth));
					if (frac <= cut && !Director::IsBoss(f)) { Damage(f, 1.0e7f, false, false); continue; }
					if (proc) { RE::NiPoint3 away = f->GetPosition() - me; away.z = 0; away.Unitize(); proc->KnockExplosion(f, f->GetPosition() - away * 120.0f, 1.5f + 1.0f * L); }
					Director::Web(f->GetPosition(), 45.0f, 2.0f + 2.0f * L, 70.0f);
				}
				blastAt("dark", me + RE::NiPoint3{ 0, 0, 40 }, 0.0f, 0.6f + 0.1f * L);
			} else if (id == "shatter") {  // Arcane Singularity: a vortex pulls them in, then detonates
				g_zones.push_back({ 3, aim.hit + RE::NiPoint3{ 0, 0, 60 }, 450.0f + 150.0f * L, 2.4f, 0.0f, base * (1.2f + 0.8f * L), 0, false, 2.0f });
				g_zones.back().kind = 6;
				blastAt("constellation", aim.hit + RE::NiPoint3{ 0, 0, 60 }, 0.0f, 0.6f);
			}
			Forms::Play(static_cast<gen::Sound>(sh.sound));
			{  // the Dragonborn's own voice: a roar for each word spoken (one, two or all three)
				static constexpr std::string_view kVoiced[] = { "ember", "gale", "wail", "rime", "quake", "shroud", "toll", "mire", "dread", "shatter" };
				for (int v = 0; v < static_cast<int>(std::size(kVoiced)); ++v) {
					if (kVoiced[v] == sh.id) {
						const int snd = static_cast<int>(gen::Sound::shoutvoc_ember_1) + v * 3 + std::clamp(L, 1, 3) - 1;
						Audio::Ui(static_cast<gen::Sound>(snd), 1.0f);
					}
				}
			}
			Hud::Banner(std::string(L >= 1 ? sh.w1 : "") + (L >= 2 ? std::string(" ") + std::string(sh.w2) : "") + (L >= 3 ? std::string(" ") + std::string(sh.w3) + "!" : ""), 1.6f);
			logger::info("shout: {} level {}", sh.id, L);
		}
	}

}

namespace bo3z::Difficulty
{
	int Level()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		return p ? std::clamp(static_cast<int>(p->GetGameStatsData().difficulty), 0, 5) : 2;
	}
	// (Noah: "the difficulty slider in the Skyrim settings should change everything in the game so it's consistent with the
	// selected difficulty ... every weapon spell enemy boss elite ALL OF IT" - his table, Apprentice the normal one):
	//                 Novice  Apprentice  Adept  Expert  Master  Legendary
	// enemy health     0.6      1.0        1.4    1.9     2.6     3.5      (Settings::UndeadHealthMult, FoeToughness)
	// their damage     0.5      1.0        1.4    1.9     2.6     3.5      (Taken; Skyrim's own foes: fDiffMultHPToPC*)
	// your damage      1.5      1.0        0.9    0.8     0.7     0.6      (Dealt: every blow and spell of ours; Skyrim's own: fDiffMultHPByPC*)
	float Dealt() { static constexpr float k[6] = { 1.5f, 1.0f, 0.9f, 0.8f, 0.7f, 0.6f }; return k[Level()]; }
	float Taken() { static constexpr float k[6] = { 0.5f, 1.0f, 1.4f, 1.9f, 2.6f, 3.5f }; return Settings::FollowDifficulty() ? k[Level()] : 1.0f; }  // (the night's blows: the only place the difficulty touches them)
	float Mana() { static constexpr float k[6] = { 0.8f, 0.9f, 1.0f, 1.08f, 1.16f, 1.25f }; return k[Level()]; }
	int Freed()
	{
		static int  n = 0;
		static auto at = std::chrono::steady_clock::now() - std::chrono::seconds(10);
		if (std::chrono::steady_clock::now() - at > std::chrono::seconds(5)) {
			at = std::chrono::steady_clock::now(), n = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) n += Curse::Freed(t) ? 1 : 0;
		}
		return n;
	}
	// (Noah: "players are stronger than normal Skyrim characters - strengthen the normal Skyrim and modded enemies") how
	// much tougher Skyrim's own foes are against your class's weapons: by the difficulty, and more with each Echo Altar
	// you free (the stronger you get, the more they hold)
	float FoeToughness()
	{
		// (Noah: the difficulty pass) Skyrim's own and the modded foes, against your altar gear: the same steps as the dead,
		// and the same 30% more with each Echo Altar freed (your gear's upgrade curve)
		static constexpr float k[6] = { 0.6f, 1.0f, 1.4f, 1.9f, 2.6f, 3.5f };   // (Noah's table: enemy health)
		return k[Level()] * (1.0f + 0.3f * static_cast<float>(std::min(Freed(), 10)));
	}
	// and how much harder they hit: Skyrim's own damage-to-you settings (each difficulty's) raised with your progress
	void TickFoeDamage()
	{
		// (Noah's table replaces Skyrim's own difficulty steps) the damage Skyrim's own foes deal you, and that you deal them,
		// set to the table - not Skyrim's 0.5x .. 3x on top of ours
		static constexpr const char* kNames[6] = { "fDiffMultHPToPCVE", "fDiffMultHPToPCE", "fDiffMultHPToPCN", "fDiffMultHPToPCH", "fDiffMultHPToPCVH", "fDiffMultHPToPCL" };
		static constexpr const char* kBy[6] = { "fDiffMultHPByPCVE", "fDiffMultHPByPCE", "fDiffMultHPByPCN", "fDiffMultHPByPCH", "fDiffMultHPByPCVH", "fDiffMultHPByPCL" };
		static constexpr float       kTo[6] = { 0.5f, 1.0f, 1.4f, 1.9f, 2.6f, 3.5f };
		static constexpr float       kByV[6] = { 1.5f, 1.0f, 0.9f, 0.8f, 0.7f, 0.6f };
		static float applied = -1;
		auto* gs = RE::GameSettingCollection::GetSingleton();
		if (!gs) return;
		const float prog = 1.0f + 0.06f * static_cast<float>(std::min(Freed(), 12));
		if (std::abs(prog - applied) < 1e-3f) return;
		for (int i = 0; i < 6; ++i) {
			if (auto* st = gs->GetSetting(kNames[i])) st->data.f = kTo[i] * prog;
			if (auto* st = gs->GetSetting(kBy[i])) st->data.f = kByV[i];
		}
		if (applied < 0 || std::abs(prog - applied) > 1e-3f) logger::info("difficulty: Skyrim's foes hit x{:.2f} (on top of the difficulty's own), and hold x{:.2f} against your class's weapons", prog, FoeToughness());
		applied = prog;
	}
}
