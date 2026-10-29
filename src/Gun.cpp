#include "BO3Z.h"

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

		bool  g_trigger = false, g_adsHeld = false;
		bool  g_dualHeld = false;  // hand spells: right mouse held, both hands gathering the signature move
		float g_charge = 0, g_chargeCd = 0;  // hold-to-charge signature moves
		float g_ads = 0, g_cooldown = 0, g_reload = 0, g_knifeCd = 0, g_burstTimer = 0;
		int   g_burstLeft = 0;
		Clock::time_point g_swapDown{};
		bool  g_swapHeld = false;
		std::unordered_map<RE::FormID, std::pair<Clock::time_point, bool>> g_hits;
		std::mt19937 g_rng{ std::random_device{}() };
		bool  g_loggedAim = false;

		int CurrentGun()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p ? Forms::GunIndexOf(p->GetEquippedObject(false)) : -1;
		}
		bool Drawn()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p && p->AsActorState()->IsWeaponDrawn();
		}
		int SlotOf(int a_gun)
		{
			for (int s = 0; s < Gun::SlotCount(); ++s) {
				if (Save::GetSlot(s).gun == a_gun) {
					return s;
				}
			}
			return -1;
		}
		bool GunOut() { return CurrentGun() >= 0 && Drawn() && !Intro::CrewScreenOpen(); }

		// Bows: their elemental arrows never run out (topped up quietly, kept on the string).
		void TopUpArrows(int a_gun, bool a_equip)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || a_gun < 0 || gen::kGuns[a_gun].kind != gen::kKindBow || !gen::kGuns[a_gun].ammo) {
				return;
			}
			auto* ammo = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESAmmo>(gen::kGuns[a_gun].ammo, gen::kPlugin);
			if (!ammo) {
				return;
			}
			const auto counts = p->GetInventoryCounts([ammo](RE::TESBoundObject& o) { return &o == ammo; });
			const int  have = counts.empty() ? 0 : counts.begin()->second;
			if (have < 40) {
				p->AddObjectToContainer(ammo, nullptr, 80 - have, nullptr);
			}
			if (a_equip || p->GetCurrentAmmo() != ammo) {
				RE::ActorEquipManager::GetSingleton()->EquipObject(p, ammo, nullptr, static_cast<std::uint32_t>(std::max(have, 80)), nullptr, true, false, false);
			}
		}

		void Equip(int a_gun)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* w = Forms::Gun(a_gun);
			if (!p || (!w && !Forms::GunSpell(a_gun))) {
				return;
			}
			if (auto* sp = Forms::GunSpell(a_gun)) {
				if (!p->HasSpell(sp)) {
					p->AddSpell(sp);
				}
				auto* right = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F42);  // RightHand equip slot
				auto* left = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F43);   // LeftHand: both hands carry the spell (the dual-cast charge)
				RE::ActorEquipManager::GetSingleton()->EquipSpell(p, sp, right);
				RE::ActorEquipManager::GetSingleton()->EquipSpell(p, sp, left);
			} else {
				RE::ActorEquipManager::GetSingleton()->EquipObject(p, w);
				TopUpArrows(a_gun, true);
			}
			if (!Drawn()) {
				p->DrawWeaponMagicHands(true);
			}
		}

		struct InputBlock
		{
			static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
			{
				if (GunOut()) {
					// The fire button goes through: Skyrim plays the staff/hand cast. Right mouse (left-hand
					// attack/block) stays ours for aiming.
					auto* b = a_event ? a_event->AsButtonEvent() : nullptr;
					if (b && ((b->GetDevice() == RE::INPUT_DEVICE::kMouse && b->GetIDCode() == gen::kKey2_fire) ||
					             (b->GetDevice() == RE::INPUT_DEVICE::kGamepad && b->GetIDCode() == 0x000A))) {  // (RT on a pad)
						return func(a_this, a_event);
					}
					return false;
				}
				return func(a_this, a_event);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		// Staffs: Skyrim's own staff swing plays when the shot leaves, not when the button goes down. The press and
		// the hold never reach Skyrim (the hold is our charge-up, the staff held steady); on release, when a shot or
		// a signature move actually goes, Skyrim gets a short synthetic tap and plays its staff cast animation.
		RE::PlayerInputHandler* g_abHandler = nullptr;
		// (Noah: the bow loosed at any point of the draw) a bow only lets go at the peak of its draw: a release that
		// comes early is kept back and given to Skyrim the moment the string is all the way back (Bows.inl Tick)
		bool              g_bowDrawing = false;      // the string is coming back
		std::atomic<bool> g_bowFull{ false };        // the draw reached its peak (Skyrim's bowDrawn state/event)
		bool              g_bowHeldRelease = false;  // an early release, waiting for the peak
		RE::PlayerControlsData* g_abData = nullptr;
		float                   g_swingUp = -1.0f;  // >= 0: a synthetic tap is down; let go when it runs out
		bool                    g_swingWanted = false;
		bool                    g_dual = false;     // hand spells: the left hand has come up for the charge
		float                   g_tapT = -1.0f;   // >= 0: the staff's short thrust for a normal shot is playing
		bool IsNativeStaff(int a_gun)  // (Noah) the Falmer staffs: Skyrim casts them (its animation, its spell); ours on the hit
		{
			if (a_gun < 0) return false;
			const std::string_view id = gen::kGuns[a_gun].id;
			return id == "gravecaller" || id == "frostbinder" || id == "wolfcrown" || id == "barrowhorn";
		}
		bool IsStaff(int a_gun) { return a_gun >= 0 && gen::kGuns[a_gun].kind == gen::kKindWand && !IsNativeStaff(a_gun); }
		bool IsSpell(int a_gun) { return a_gun >= 0 && (gen::kGuns[a_gun].kind == gen::kKindTome || gen::kGuns[a_gun].kind == gen::kKindWonder); }

		// Controllers (Xbox, and PlayStation through Steam Input, which Skyrim reads as an Xbox pad):
		//   RT fire / charge, LT aim, D-pad right next weapon (hold: holster), D-pad down quick knife (Skyrim's hotkeys
		// on those two wait while a weapon of ours is out).
		constexpr std::uint32_t kPadRT = 0x000A, kPadLT = 0x0009, kPadRight = 0x0008, kPadDown = 0x0002;
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

		bool g_nativeCut = false;
		struct FireButton
		{
			static void thunk(RE::PlayerInputHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
			{
				// the mouse's hands the natural way round: the left button is the left hand, the right button the right
				// (Skyrim has them crossed). Not with a gun out (its fire button is its own) nor on a controller.
				if (!GunOut() && a_event && a_event->GetDevice() == RE::INPUT_DEVICE::kMouse) {
					if (auto* ue = RE::UserEvents::GetSingleton()) {
						if (a_event->QUserEvent() == ue->rightAttack) {
							a_event->SetUserEvent(ue->leftAttack);
						} else if (a_event->QUserEvent() == ue->leftAttack) {
							a_event->SetUserEvent(ue->rightAttack);
						}
					}
				}
				if (GunOut() && (IsStaff(CurrentGun()) || IsSpell(CurrentGun())) && IsFireButton(a_event)) {
					g_abHandler = a_this, g_abData = a_data;
					return;  // ours: the staff swing is played on release (TickSwing)
				}
				if (GunOut() && gen::kGuns[CurrentGun()].kind == gen::kKindBow && IsFireButton(a_event)) {
					if (a_event->IsDown()) {
						g_bowHeldRelease = false;
					} else if (a_event->IsUp() && g_bowDrawing && !g_bowFull && !RE::PlayerCharacter::GetSingleton()->IsInMidair() && !Movement::Dodging()) {  // (Noah: falling, jumping or dodging mid-draw broke the bow: then Skyrim has the release)
						g_abHandler = a_this, g_abData = a_data;
						g_bowHeldRelease = true;  // not yet at 80%: the draw is called off (Bows.inl Tick)
						return;
					}
				}
				if (GunOut() && gen::kGuns[CurrentGun()].kind != gen::kKindBow && !IsNativeStaff(CurrentGun()) && IsFireButton(a_event)) {
					auto& rd = a_event->GetRuntimeData();
					if (a_event->IsDown()) {
						g_nativeCut = false;
					} else if (g_nativeCut) {
						return;  // held on, or the real release after we already let go
					} else if (a_event->IsHeld() && rd.heldDownSecs >= 0.12f) {
						const float v = rd.value;
						rd.value = 0.0f;  // reads as a release
						func(a_this, a_event, a_data);
						rd.value = v;
						g_nativeCut = true;
						return;
					}
				}
				func(a_this, a_event, a_data);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void SendAttack(bool a_left, float a_value, float a_held);
		void SendFire(float a_value, float a_held) { SendAttack(false, a_value, a_held); }
		void SendAttack(bool a_left, float a_value, float a_held)
		{
			if (auto* pc = RE::PlayerControls::GetSingleton(); pc && (!g_abHandler || !g_abData)) {
				// our input handler takes the fire button before Skyrim's attack handler ever sees it, so the
				// handler is fetched from the player controls (it never had a press to remember it by)
				g_abHandler = reinterpret_cast<RE::PlayerInputHandler*>(pc->attackBlockHandler);
				g_abData = &pc->data;
			}
			if (!g_abHandler || !g_abData) {
				static bool warned = false;
				if (!warned) {
					warned = true;
					logger::warn("staff: no attack handler for the native swing");
				}
				return;
			}
			auto* ue = RE::UserEvents::GetSingleton();
			auto* ev = a_left ? RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue ? ue->leftAttack : RE::BSFixedString("Left Attack/Block"), gen::kKey2_ads, a_value, a_held)
			                  : RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue ? ue->rightAttack : RE::BSFixedString("Right Attack/Block"), gen::kKey2_fire, a_value, a_held);
			if (!ev) {
				return;
			}
			FireButton::func(g_abHandler, ev, g_abData);
			ev->~ButtonEvent();  // (the audit: its event-name string was never released)
			RE::free(ev);
			static int said = 0;
			if (said++ < 4) {
				logger::info("staff: native swing {} (Skyrim's staff cast plays with the shot)", a_value > 0 ? "down" : "up");
			}
		}

		// The staff cast animation, in step with the shot: press now, let go 0.35 s later (long enough for Skyrim to
		// commit to the staff cast).
		void TickSwing(float a_delta)
		{
			if (g_swingUp >= 0.0f) {
				if ((g_swingUp += a_delta) >= 0.35f) {
					SendFire(0.0f, 0.35f);
					g_swingUp = -1.0f;
				}
				return;
			}
			if (g_swingWanted) {
				// Staffs are held with a mace grip now (see gen_esp.py): the shot's motion is ours (a short thrust in
				// TickPose), Skyrim's own attack never plays (it would be a mace swing).
				g_swingWanted = false;
				g_tapT = 0.0f;
			}
		}

		struct FavBlock  // the pad's D-pad right / down are ours while a weapon of ours is drawn
		{
			static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
			{
				auto* b = a_event ? a_event->AsButtonEvent() : nullptr;
				if (GunOut() && b && b->GetDevice() == RE::INPUT_DEVICE::kGamepad && (b->GetIDCode() == kPadRight || b->GetIDCode() == kPadDown)) {
					return false;
				}
				if (Shops::MenuEats(a_event) || Classes::MenuEats(a_event) || Director::RestEats(a_event) || Loot::Eats(a_event)) {  // the shrine's keys (1-4, D-pad) and the loot list's never reach the hotkeys
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
				if (Shops::MenuEats(a_event) || Classes::MenuEats(a_event) || Director::RestEats(a_event) || Loot::Eats(a_event)) {
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
				if (GunOut() || Shops::MenuEats(a_event) || Classes::MenuEats(a_event) || Director::RestEats(a_event) || Loot::Eats(a_event)) {
					return false;
				}
				return func(a_this, a_event);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		float Rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }

		RE::NiPoint3 Spread(const RE::NiPoint3& a_dir, float a_deg)
		{
			if (a_deg <= 0) {
				return a_dir;
			}
			const float r = std::tan(a_deg * RE::NI_PI / 180.0f) * std::sqrt(Rand(0, 1)), t = Rand(0, 2 * RE::NI_PI);
			RE::NiPoint3 up{ 0, 0, 1 };
			RE::NiPoint3 right = a_dir.Cross(up);
			if (right.Length() < 1e-3f) {
				right = { 1, 0, 0 };
			}
			right.Unitize();
			RE::NiPoint3 u = right.Cross(a_dir);
			RE::NiPoint3 d = a_dir + right * (r * std::cos(t)) + u * (r * std::sin(t));
			d.Unitize();
			return d;
		}

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

		// Hexbinder / Plaguebringer marks (by form id): hexed take more damage, doomed explode on death, plagued pass it on.
		std::unordered_map<RE::FormID, float> g_hexed;              // -> seconds left
		std::unordered_map<RE::FormID, float> g_doomed;             // -> blast damage when it dies
		std::unordered_map<RE::FormID, float> g_plagued;            // -> plague damage per second
		struct Detonation
		{
			RE::NiPoint3 at;
			float        damage, delay;
			int          kind;  // 0 doom, 1 plague jump, 2 falling star
			int          gun = -1;
		};
		std::vector<Detonation> g_detonations;

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
		float       g_propKickT = -1.0f;  // a wonder weapon just fired (src/WonderProps.inl: its model's attack motion)
		bool        g_propKickBig = false;
		std::atomic<bool> g_weaponSwing{ false };  // (any swing of the weapon in hand, heard by Bows' animation sink: Alduin's Fang's charged swing)
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

		// (Noah: "the bows weren't causing the death effect mods") a bow's element for a blow that names none of its own -
		// so a storm arrow's kill crackles with lightning like the storm spells', a fire arrow's burns, a frost one freezes
		int FecOfRow(int a_row)
		{
			for (const auto& b : gen::kBows) {
				if (b.spellRow != a_row) continue;
				const std::string_view e = b.element;
				return e == "fire" ? 0 : e == "frost" ? 1 : e == "shock" ? 2 : e == "sun" ? 5 : e == "blood" ? 6 : -1;
			}
			return -1;
		}

		bool Damage(RE::Actor* a_target, float a_bo3Damage, bool a_head, bool a_melee, bool a_quiet = false)
		{
			if (!a_quiet && !g_stormBusy && g_storm.charges > 0 && a_target && !a_target->IsDead()) {
				StormStrike(a_target);
			}
			auto*       player = RE::PlayerCharacter::GetSingleton();
			auto*       av = a_target->AsActorValueOwner();
			const auto  tid = a_target->GetFormID();
			const float hexMult = g_hexed.contains(tid) ? 1.5f : 1.0f;
			// (the difficulty reaches the night's dead through their health alone - Settings::UndeadHealthMult; Skyrim's
			// own enemies take our scripted blows scaled by it here, as Skyrim scales yours)
			float       damage = a_bo3Damage * hexMult * Eggs::DamageMult() * (Director::IsRoundUndead(a_target) ? 1.0f : Difficulty::Dealt()) / gen::kRounds.hpScale;
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
			damage *= Director::TakenMult(a_target);  // (a brute dazed or berserk)
			if (PowerUps::InstaKill() && Director::IsRoundUndead(a_target)) {
				damage = av->GetActorValue(RE::ActorValue::kHealth) + 1.0f;  // Insta-Kill (every one of them, one hit)
			}
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
				if (auto it = g_doomed.find(tid); it != g_doomed.end()) {  // Doom Mark: it goes off, and takes the next ones with it
					g_detonations.push_back({ a_target->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, it->second, 0.15f, 0 });
					g_doomed.erase(it);
				}
				if (auto it = g_plagued.find(tid); it != g_plagued.end()) {  // the plague looks for its next host
					g_detonations.push_back({ a_target->GetPosition(), it->second, 0.3f, 1 });
					g_plagued.erase(it);
				}
				g_hexed.erase(tid);
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
					a_target->StartCombat(player);
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
				Damage(a.get(), d.perSecond * step, false, false, true);
				d.left -= step;
			}
			std::erase_if(g_dots, [](const Dot& d) { return d.left <= 0; });
		}

		void Stagger(RE::Actor* a, float a_mag)
		{
			a->SetGraphVariableFloat("staggerMagnitude", a_mag);
			a->NotifyAnimationGraph("staggerStart");
		}

		float CrewMult(int a_passive)
		{
			const int c = Save::Crew();
			return c >= 0 && gen::kCrew[c].passive == a_passive ? gen::kCrew[c].value : 1.0f;
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

		// Where the bolt leaves from: the wand's tip in first person, else the right hand's magic node, else the eyes.
		RE::NiPoint3 BoltOrigin(RE::PlayerCharacter* a_player, const gen::GunDef& g)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			RE::NiPoint3 eye = cam && cam->cameraRoot ? cam->cameraRoot->world.translate : a_player->GetPosition() + RE::NiPoint3{ 0, 0, 120 };
			for (bool first : { true, false }) {
				auto* root = a_player->Get3D(first);
				if (!root) {
					continue;
				}
				if (g.kind == gen::kKindWand) {
					if (auto* w = root->GetObjectByName("WEAPON"); w && w->AsNode()) {
						for (auto& c : w->AsNode()->GetChildren()) {
							if (c) {
								return c->world.translate + c->world.rotate * RE::NiPoint3{ 0, 62.0f, 0 } * c->world.scale;  // staff head (+Y, tools/staffs.py)
							}
						}
					}
				}
				if (auto* m = root->GetObjectByName("NPC R MagicNode [RMag]")) {
					return m->world.translate;
				}
			}
			return eye;
		}

		void LaunchBolt(RE::PlayerCharacter* a_player, const gen::GunDef& g, const RE::NiPoint3& a_to, const RE::NiPoint3* a_from = nullptr)
		{
			auto* raw = RE::TESDataHandler::GetSingleton()->LookupForm(g.bulletSpell, gen::kPlugin);
			auto* spell = raw ? raw->As<RE::SpellItem>() : nullptr;
			auto* proj = spell && !spell->effects.empty() && spell->effects[0] && spell->effects[0]->baseEffect ? spell->effects[0]->baseEffect->data.projectileBase : nullptr;
			if (!proj) {
				static int warned = 0;
				if (warned++ < 3) {
					logger::warn("bolt: {} has no projectile (spell {:03X} found {})", g.id, g.bulletSpell, spell != nullptr);
				}
				return;
			}
			const RE::NiPoint3 from = a_from ? *a_from : BoltOrigin(a_player, g);
			RE::NiPoint3       d = a_to - from;
			const float        flat = std::sqrt(d.x * d.x + d.y * d.y);
			RE::Projectile::ProjectileRot rot{ -std::atan2(d.z, std::max(1e-3f, flat)), std::atan2(d.x, d.y) };
			RE::Projectile::LaunchData    ld(proj, a_player, from, rot);
			ld.useOrigin = true;
			ld.autoAim = false;
			ld.spell = spell;
			ld.castingSource = RE::MagicSystem::CastingSource::kRightHand;
			RE::ProjectileHandle h;
			RE::Projectile::Launch(&h, ld);
			static int logged = 0;
			if (logged++ < 6) {
				auto pr = h.get();
				logger::info("bolt: {} proj {:08X} from ({:.0f},{:.0f},{:.0f}) to ({:.0f},{:.0f},{:.0f}) -> {}", g.id, proj->GetFormID(), from.x, from.y, from.z, a_to.x, a_to.y, a_to.z,
					pr ? fmt::format("launched {:08X} 3D {}", pr->GetFormID(), pr->Get3D() != nullptr) : std::string("no projectile"));
			}
		}

		// Where the crosshair lands (physics ray), and the eye and aim it was cast from.
		struct Aim { RE::NiPoint3 eye, dir, hit; };
		bool StaffUnleash(std::string_view a_id, const Aim& a_aim, float a_base);  // (Staffs.inl) the four Falmer staffs' held attacks
		void StaffTap(const gen::GunDef& g, const RE::NiPoint3& a_to, RE::Actor* a_hit, float a_dmg);
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
				r->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(b.scale, 0.1f, 5.0f) * 100.0f);
				g_shrink.push_back({ r->CreateRefHandle(), { b.scale, 0.6f } });
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
		}

		float BoltDelay(const gen::GunDef& g, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
		{
			float speed = g.kind == gen::kKindBow ? 5200.0f : 3000.0f;
			if (auto* raw = g.bulletSpell ? RE::TESDataHandler::GetSingleton()->LookupForm(g.bulletSpell, gen::kPlugin) : nullptr) {
				if (auto* sp = raw->As<RE::SpellItem>(); sp && !sp->effects.empty() && sp->effects[0] && sp->effects[0]->baseEffect && sp->effects[0]->baseEffect->data.projectileBase) {
					speed = std::max(500.0f, sp->effects[0]->baseEffect->data.projectileBase->data.speed);
				}
			}
			return std::min(1.5f, a_from.GetDistance(a_to) / speed);
		}

		// An elemental arrow (bows): the arrow's own projectile, launched from a point (the bow, or the sky).
		void LaunchArrow(RE::PlayerCharacter* a_player, const gen::GunDef& g, const RE::NiPoint3& a_to, const RE::NiPoint3* a_from = nullptr)
		{
			if (g.kind != gen::kKindBow) {  // (the bows became spells: their volleys fly as the spell's bolts)
				LaunchBolt(a_player, g, a_to, a_from);
				return;
			}
			auto* ammo = g.ammo ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESAmmo>(g.ammo, gen::kPlugin) : nullptr;
			auto* proj = ammo ? ammo->data.projectile : nullptr;
			if (!proj) {
				return;
			}
			const RE::NiPoint3 from = a_from ? *a_from : BoltOrigin(a_player, g);
			RE::NiPoint3       d = a_to - from;
			const float        flat = std::sqrt(d.x * d.x + d.y * d.y);
			RE::Projectile::ProjectileRot rot{ -std::atan2(d.z, std::max(1e-3f, flat)), std::atan2(d.x, d.y) };
			RE::Projectile::LaunchData    ld(proj, a_player, from, rot);
			ld.useOrigin = true;
			ld.autoAim = false;
			ld.ammoSource = ammo;
			RE::ProjectileHandle h;
			RE::Projectile::Launch(&h, ld);
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

		// Time Stop (Chronos Hourglass): frozen zombies take the stored blow when time starts again.
		struct Frozen
		{
			RE::ActorHandle who;
			float           left, damage;
		};
		std::vector<Frozen> g_frozen;

		// Prism Sentinel (Prismheart): a crystal hanging in the air that beams the nearest zombies.
		struct Prism
		{
			RE::NiPoint3 at;
			float        left, tick, base;
			int          gun;
		};
		std::vector<Prism> g_prisms;

		void FireOnce(const gen::GunDef& g)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->cameraRoot) {
				return;
			}
			const auto&  world = cam->cameraRoot->world;
			const auto   origin = world.translate;
			RE::NiPoint3 aim{ world.rotate.entry[0][1], world.rotate.entry[1][1], world.rotate.entry[2][1] };
			aim.Unitize();
			if (!g_loggedAim) {
				logger::info("aim check: camera dir ({:.2f},{:.2f},{:.2f}) yaw {:.2f} pitch {:.2f}", aim.x, aim.y, aim.z, player->GetAngleZ(), player->GetAngleX());
				g_loggedAim = true;
			}
			std::vector<RE::Actor*> candidates;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				if (auto a = h.get(); a && Targetable(a.get(), player) && a->GetPosition().GetDistance(origin) <= g.range) {
					candidates.push_back(a.get());
				}
			}
			const float spread = g.spreadHip + (g.spreadAds - g.spreadHip) * g_ads;
			const float headMult = g.headMult * Perks::HeadMult();
			const float headRadius = g.headRadius * (Perks::HeadMult() > 1.0f ? 1.5f : 1.0f);
			const int   lvl = SlotOf(CurrentGun()) >= 0 ? Save::GetSlot(SlotOf(CurrentGun())).level : 0;
			const float dmg = g.damage * Perks::DamageMult() * CrewMult(0) * UpgradeMult(lvl);
			++g_shots;
			if (g.special == 3) {  // cone blast (Thu'um Cannon): everything in front of you, harder up close
				bool los = false;
				for (auto* a : candidates) {
					const auto to = HeadPos(a) - origin;
					if (!a->IsDead() && AngleDeg(aim, to) <= g.spreadHip && player->HasLineOfSight(a, los)) {
						const float fall = 1.0f - 0.6f * std::min(1.0f, to.Length() / g.range);
						Damage(a, dmg * fall, false, false);
					}
				}
				if (g.spell >= 0) {
					Cast(g.spell, nullptr);
				}
				return;
			}
			RE::Actor* firstHit = nullptr;
			const auto sp = static_cast<gen::Special>(g.special);
			auto*      pav = player->AsActorValueOwner();
			for (int p = 0; p < g.pellets; ++p) {
				const auto dir = Spread(aim, spread);
				// everything this ray passes through, nearest first (pierce wands keep going)
				struct Hit { RE::Actor* a; float t; bool head; };
				std::vector<Hit> hits;
				for (auto* a : candidates) {
					const auto hp = HeadPos(a);
					if (auto [hd, ht] = RaySegment(origin, dir, hp, hp); hd <= headRadius) {
						hits.push_back({ a, ht, true });
					} else if (auto [bd, bt] = RaySegment(origin, dir, a->GetPosition() + RE::NiPoint3{ 0, 0, 15 }, hp); bd <= g.hitRadius) {
						hits.push_back({ a, bt, false });
					}
				}
				std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.t < y.t; });
				const int pierce = sp == gen::Special::Pierce ? std::max(1, static_cast<int>(g.abilityValue)) : 1;
				int       taken = 0;
				for (auto& h : hits) {
					if (taken >= pierce) {
						break;
					}
					bool los = false;
					if (!player->HasLineOfSight(h.a, los)) {
						continue;
					}
					++taken;
					RE::Actor* best = h.a;
					const bool head = h.head;
					const float bestT = h.t;
					if (!firstHit) {
						firstHit = best;
					}
					if (g.spell >= 0 && (g.spellOn == 1 || (g.spellOn == 3 && g_shots % 4 == 0)) && p == 0 && taken == 1) {
						Cast(g.spell, best);  // spell visuals/effects on the target (Wabbajack: its own madness)
					}
					const float hitDmg = dmg * (head ? headMult : 1.0f) * (taken > 1 ? 0.8f : 1.0f);
					bool        killed = false;
					if (!best->IsDead()) {
						killed = Damage(best, hitDmg, head, false);
					}
					switch (sp) {
					case gen::Special::Poison:
						if (!killed) {
							g_dots.push_back({ best->GetHandle(), hitDmg * g.abilityValue / 3.0f, 3.0f });
						}
						break;
					case gen::Special::Soul:
						if (killed) {
							Points::Add(static_cast<int>(100 * g.abilityValue));  // soul harvest
						}
						break;
					case gen::Special::Slow:
						if (!killed) {
							Director::Web(best->GetPosition(), 40.0f, 2.0f, 100.0f * g.abilityValue);
						}
						break;
					case gen::Special::Lifesteal:
						pav->RestoreActorValue(RE::ActorValue::kHealth, pav->GetPermanentActorValue(RE::ActorValue::kHealth) * g.abilityValue * 0.25f);
						break;
					case gen::Special::Stagger:
						if (!killed && Rand(0, 1) < g.abilityValue) {
							Stagger(best, 0.8f);
						}
						break;
					case gen::Special::Stasis:
						if (!killed && Rand(0, 1) < g.abilityValue) {
							Director::Web(best->GetPosition(), 40.0f, 2.5f, 95.0f);  // locked in amber
						}
						break;
					case gen::Special::Hex:
						if (!killed) {
							g_hexed[best->GetFormID()] = 5.0f;
							Audio::At(gen::Sound::linger_hex, best, 0.8f);
						}
						break;
					case gen::Special::Plague:
						if (!killed) {
							const float dps = hitDmg * g.abilityValue / 3.0f;
							g_dots.push_back({ best->GetHandle(), dps, 3.0f });
							g_plagued[best->GetFormID()] = dps;
							Audio::At(gen::Sound::linger_plague, best, 0.8f);
						}
						break;
					case gen::Special::Daze:
						if (!killed) {
							Stagger(best, 0.6f);
							if (Rand(0, 1) < g.abilityValue) {
								Director::Web(best->GetPosition(), 40.0f, 1.5f, 60.0f);
							}
						}
						break;
					default:
						break;
					}
					if (sp == gen::Special::Chain) {  // chain (Skyforge): jumps between nearby dead
						std::vector<RE::Actor*> chained{ best };
						RE::Actor*              cur = best;
						for (int k = 0; k < 6; ++k) {
							RE::Actor* next = nullptr;
							float      nd = g.splashRadius;
							for (auto* a : candidates) {
								if (a->IsDead() || std::find(chained.begin(), chained.end(), a) != chained.end()) {
									continue;
								}
								if (const float d = a->GetPosition().GetDistance(cur->GetPosition()); d < nd) {
									nd = d, next = a;
								}
							}
							if (!next) {
								break;
							}
							Damage(next, g.splashDamage * Perks::DamageMult(), false, false);
							chained.push_back(next);
							cur = next;
						}
					}
				}
			}
			if (g.spell >= 0 && g.spellOn == 2) {
				Cast(g.spell, firstHit);  // fired spell flies at what you hit (or straight ahead)
			}
			{
				// one bolt per shot from the wand tip, and the element's blast where it lands
				const Aim          a = AimAt(std::min(g.range, 6000.0f));
				const bool         landed = firstHit || a.hit.GetDistance(a.eye) < std::min(g.range, 6000.0f) - 5.0f;
				const RE::NiPoint3 to = firstHit ? HeadPos(firstHit) - RE::NiPoint3{ 0, 0, 30 } : a.hit;
				if (g.bulletSpell) {
					LaunchBolt(player, g, to);
				}
				if (const int gi = static_cast<int>(&g - gen::kGuns.data()); landed && gi >= 0 && gi < static_cast<int>(gen::kGuns.size()) && gen::kGunSounds[gi].hit >= 0) {
					Audio::AtPoint(static_cast<gen::Sound>(gen::kGunSounds[gi].hit), to, BoltDelay(g, origin, to), 0.9f);  // the bolt's impact, when it gets there
				}
				if (landed && g.kind != gen::kKindTome && g.kind != gen::kKindWonder) {  // a quick spell is a bolt, not a bomb: only the gathered cast bursts
					Blast(g.tapBlast, to, BoltDelay(g, origin, to), kTapBlastScale);
					StaffTap(g, to, firstHit, dmg);
					// every bolt and arrow splashes a little where it lands: the burst weapons (Splash) hard and wide,
					// the rest a third of the hit within an arm's reach
					const bool  burst = sp == gen::Special::Splash && g.splashRadius > 0;
					const float r = burst ? g.splashRadius : (g.kind == gen::kKindBow ? 70.0f : 90.0f);
					const float sd = burst ? g.splashDamage * Perks::DamageMult() : dmg * 0.33f;
					for (auto* a : candidates) {
						if (a != firstHit && !a->IsDead() && a->GetPosition().GetDistance(to) <= r + 20.0f) {
							Damage(a, sd, false, false);
						}
					}
				}
			}
			Perks::OnCast(g_shots);
		}

		float g_manaQuiet = 0;  // seconds since the last magicka shot (regen waits for it)

		void TryFire(bool a_fromTrigger)
		{
			const int gun = CurrentGun(), slot = SlotOf(gun);
			if (gun < 0 || slot < 0 || g_cooldown > 0) {
				return;
			}
			const auto& g = gen::kGuns[gun];
			auto*       player = RE::PlayerCharacter::GetSingleton();
			if (g.mana > 0) {  // tomes and grimoires run on magicka
				auto*       av = player->AsActorValueOwner();
				const float cost = g.mana * Perks::ManaMult() * Difficulty::Mana();
				if (av->GetActorValue(RE::ActorValue::kMagicka) < cost) {
					if (a_fromTrigger) {
						Forms::Play(g.emptySnd);
						Hud::Banner("Not enough magicka", 1.2f);
					}
					g_burstLeft = 0;
					g_cooldown = 0.35f;
					return;
				}
				av->DamageActorValue(RE::ActorValue::kMagicka, cost);
				g_manaQuiet = 0;
			}
			Forms::Play(g.fireSnd);
			if (static int said = 0; said++ < 3) {
				logger::info("gun: {} cast sound {}", g.id, static_cast<int>(g.fireSnd));
			}
			FireOnce(g);
			const float delay = g.fireDelayMs / 1000.0f * Perks::FireDelayMult();
			if (g.fire == gen::Fire::Burst) {
				if (a_fromTrigger) {
					g_burstLeft = g.burst - 1;
				}
				g_burstTimer = 0.07f;
				g_cooldown = g_burstLeft > 0 ? 0.07f : delay;
			} else {
				g_cooldown = delay;
			}
		}


		// ================================================================ signature moves (hold, then release)
		float g_unleashMult = 1.0f;  // Ascendant moves (Reforge level 3): 1.25 for the strike, 0.75 for its echo
		int   ForgeLevel() { return SlotOf(CurrentGun()) >= 0 ? Save::GetSlot(SlotOf(CurrentGun())).level : 0; }
		float BaseDamage(const gen::GunDef& g)
		{
			return g.damage * Perks::DamageMult() * CrewMult(0) * UpgradeMult(ForgeLevel()) * g_unleashMult;
		}

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


		void Bolts(RE::PlayerCharacter* p, const gen::GunDef& g, const std::vector<RE::Actor*>& a_to, int a_max)
		{
			int n = 0;
			for (auto* f : a_to) {
				if (n++ >= a_max) {
					break;
				}
				if (g.bulletSpell) {
					LaunchBolt(p, g, HeadPos(f) - RE::NiPoint3{ 0, 0, 20 });
				} else if (g.spell >= 0) {
					Cast(g.spell, f);
				}
			}
		}

		// The signature move of each weapon (sheet: weapons.chargeName/chargeText).
		void Unleash(int a_gun, bool a_echo = false)
		{
			const auto& g = gen::kGuns[a_gun];
			auto*       p = RE::PlayerCharacter::GetSingleton();
			const float base = BaseDamage(g);
			const Aim   aim = AimAt(std::min(g.range * 1.5f, 8000.0f));
			auto        hit = [&](RE::Actor* f, float a_mult) { if (!f->IsDead()) { Damage(f, base * a_mult, false, false); } };
			const std::string_view id = g.id;
			Forms::Play(g.chargeSnd);
			if (!a_echo) {
				Hud::Banner(std::string(g.chargeName) + (ForgeLevel() >= gen::kForgeMax ? "  -  ASCENDANT" : ""), 1.2f);
			}
			logger::info("charge: {} unleashes {}", g.id, g.chargeName);

			if (StaffUnleash(id, aim, base)) {
				// (the Falmer staffs: Staffs.inl)
			} else if (id == "sidearm" || id == "apprentice" || id == "marksman") {  // lances: a beam of force, everything in the line
				auto line = InLine(aim, g.range * 1.5f, 75.0f);
				for (auto* f : line) { hit(f, id == "marksman" ? 5.0f : 4.0f); Stagger(f, 0.9f); }
				Bolts(p, g, line, 6);
				LaunchBolt(p, g, aim.hit);
				// the lance itself: bursts racing down the line from the staff to where it lands
				const float len = std::min(aim.hit.GetDistance(aim.eye), g.range * 1.5f);
				int         n = 0;
				for (float t = 160.0f; t < len - 60.0f && n < 14; t += 170.0f, ++n) {
					Blast(BlastIndex("light"), aim.eye + aim.dir * t - RE::NiPoint3{ 0, 0, 25.0f }, 0.025f * n);
				}
			} else if (id == "centurion") {  // pestilence: a plague burst that spreads from body to body
				for (int k = 0; k < 5; ++k) {  // a spreading cloud of rot
					const float a = k * 1.2566f;
					Blast(g.chargeBlast, aim.hit + RE::NiPoint3{ std::cos(a) * 260.0f, std::sin(a) * 260.0f, 20.0f }, 0.15f + 0.08f * k);
				}
				for (auto* f : Foes(aim.hit, 700.0f)) {
					hit(f, 1.0f);
					g_dots.push_back({ f->GetHandle(), base * 0.8f, 6.0f });
					g_plagued[f->GetFormID()] = base * 0.8f;
				}
				Bolts(p, g, Foes(aim.hit, 700.0f), 6);
			} else if (id == "triad") {  // soul harvest: homing souls to the five nearest
				auto foes = Foes(p->GetPosition(), 1600.0f);
				if (foes.size() > 5) foes.resize(5);
				for (std::size_t i = 0; i < foes.size(); ++i) {
					hit(foes[i], 3.0f);
					Blast(g.chargeBlast, foes[i]->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.25f + 0.1f * i);  // the soul torn out
				}
				Bolts(p, g, foes, 5);
				if (!foes.empty()) {  // the harvest returns to you
					auto* av = p->AsActorValueOwner();
					av->RestoreActorValue(RE::ActorValue::kMagicka, 15.0f * foes.size());
				}
			} else if (id == "spider") {  // spore bloom around the impact
				Director::Web(aim.hit, 320.0f, 4.0f, 70.0f);
				for (auto* f : Foes(aim.hit, 320.0f)) hit(f, 2.0f);
				LaunchBolt(p, g, aim.hit);
			} else if (id == "drill") {  // blood siphon around you
				auto foes = Foes(p->GetPosition(), 450.0f);
				for (auto* f : foes) hit(f, 2.0f);
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * std::min(0.6f, 0.08f * foes.size()));
				Bolts(p, g, foes, 6);
			} else if (id == "steampump") {  // bone prison: cone root + stagger
				auto cone = InCone(aim, 700.0f, 35.0f);
				for (auto* f : cone) {
					hit(f, 4.0f);
					Stagger(f, 1.0f);
					Director::Web(f->GetPosition(), 40.0f, 3.0f, 95.0f);
				}
				Bolts(p, g, cone, 6);
			} else if (id == "animunculi") {  // doom mark: every zombie nearby hexed and doomed to explode
				Audio::AtPoint(gen::Sound::linger_hex, aim.hit);
				for (auto* f : Foes(aim.hit, 1300.0f)) {
					g_hexed[f->GetFormID()] = 10.0f;
					g_doomed[f->GetFormID()] = base * 3.0f;
					hit(f, 0.5f);
				}
				LaunchBolt(p, g, aim.hit);
			} else if (id == "kagrenac") {  // mass frenzy: they turn on their own kind
				for (auto* f : Foes(aim.hit, 1100.0f)) {
					Director::Frenzy(f, 8.0f);
					Stagger(f, 0.5f);
					Blast(BlastIndex("paralyze"), f->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f }, 0.2f);  // their minds break
				}
				LaunchBolt(p, g, aim.hit);
			} else if (id == "mammothlmg") {  // black hole: a singularity drags them in, then collapses
				g_wells.push_back({ aim.hit, 3.0f, base, 0.0f });
				LaunchBolt(p, g, aim.hit);
			} else if (id == "longshot") {  // orbital strike: six stars fall one after another
				for (int k = 0; k < 6; ++k) {
					const float a = k * 2.1f;
					const float r = 60.0f + 45.0f * k;
					g_detonations.push_back({ aim.hit + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, base * 3.0f, 0.25f + 0.32f * k, 2, a_gun });
				}
			} else if (id == "aetherium") {  // auriel's wrath: huge sunburst
				for (auto* f : Foes(aim.hit, 450.0f)) { hit(f, 2.5f); Stagger(f, 1.0f); }
				LaunchBolt(p, g, aim.hit);
				for (int k = 0; k < 5; ++k) {  // Auriel's sky-bolts ring the sunburst
					const float a = k * 1.2566f + 0.4f;
					const RE::NiPoint3 at = aim.hit + RE::NiPoint3{ std::cos(a) * 300.0f, std::sin(a) * 300.0f, 0 };
					Blast(BlastIndex("skybolt"), at, 0.3f + 0.12f * k);
					for (auto* f : Foes(at, 200.0f)) hit(f, 1.0f);
				}
			} else if (id == "flamerepeater") {  // meteor arrow: a burning star falls where it lands
				const RE::NiPoint3 sky = aim.hit + RE::NiPoint3{ 120.0f, 80.0f, 1500.0f };
				LaunchArrow(p, g, aim.hit, &sky);
				for (auto* f : Foes(aim.hit, 460.0f)) {
					hit(f, 5.0f);
					g_dots.push_back({ f->GetHandle(), base * 0.8f, 4.0f });  // burning
					Stagger(f, 1.0f);
				}
			} else if (id == "frostlancer") {  // glacier arrow: everything near the impact freezes solid
				for (int k = 0; k < 6; ++k) {
					const float a = k * RE::NI_PI / 3.0f;
					const RE::NiPoint3 at = aim.hit + RE::NiPoint3{ std::cos(a) * 170.0f, std::sin(a) * 170.0f, 0 };
					const RE::NiPoint3 sky = at + RE::NiPoint3{ 0, 0, 1200.0f };
					LaunchArrow(p, g, at, &sky);
				}
				for (auto* f : Foes(aim.hit, 420.0f)) { hit(f, 3.0f); Director::Web(f->GetPosition(), 40.0f, 4.0f, 96.0f, true); }
			} else if (id == "stormcaller") {  // thunderstrike: lightning chains through ten
				auto foes = Foes(aim.hit, 1400.0f);
				if (foes.size() > 10) foes.resize(10);
				for (auto* f : foes) {
					hit(f, 2.5f);
					Stagger(f, 0.8f);
					const RE::NiPoint3 at = f->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f };
					const RE::NiPoint3 sky = at + RE::NiPoint3{ 30.0f, 0, 1300.0f };
					LaunchArrow(p, g, at, &sky);
				}
			} else if (id == "embermortar") {  // Gale Volley: a fan of wind arrows; every dead thing in the spray is knocked flat where it stands
				for (int k = -4; k <= 4; ++k) {
					const float yaw = k * 0.075f, c = std::cos(yaw), sn = std::sin(yaw);
					const RE::NiPoint3 d{ aim.dir.x * c - aim.dir.y * sn, aim.dir.x * sn + aim.dir.y * c, aim.dir.z };
					LaunchArrow(p, g, aim.eye + d * 2200.0f);
				}
				auto* proc = p->GetActorRuntimeData().currentProcess;
				int   n = 0;
				for (auto* f : InCone(aim, 2000.0f, 32.0f)) {
					hit(f, 3.0f);
					if (proc && !f->IsDead()) {
						// pushed from straight above: down they go on the spot, no flying across the field
						proc->KnockExplosion(f, f->GetPosition() + RE::NiPoint3{ 0, 0, 140.0f }, 1.2f);
						if (n++ < 8) {
							Blast(BlastIndex("priestsmall"), f->GetPosition() + RE::NiPoint3{ 0, 0, 20.0f }, BoltDelay(g, aim.eye, f->GetPosition()), 0.4f);
						}
					}
				}
			} else if (id == "wabbajack") {  // mass madness
				std::uniform_real_distribution<float> roll(0, 1);
				for (auto* f : Foes(aim.hit, 600.0f)) {
					if (roll(g_rng) < 0.5f) hit(f, 100.0f); else { hit(f, 1.0f); Stagger(f, 1.0f); }
				}
				Bolts(p, g, Foes(aim.hit, 600.0f), 6);
			} else if (id == "thuumcannon") {  // the full shout
				auto cone = InCone(aim, 1600.0f, 40.0f);
				for (auto* f : cone) hit(f, 1.5f);
				if (g.spell >= 0) Cast(g.spell, nullptr);
			} else if (id == "skyforgearc") {  // storm call: chain through twelve
				auto foes = Foes(aim.hit, 2000.0f);
				if (foes.size() > 12) foes.resize(12);
				for (auto* f : foes) hit(f, 1.2f);
				Bolts(p, g, foes, 8);
			}
			// ---- the city relics (rewards of Alduin's Curse) ----
			else if (id == "hearth") {  // Riverwood, Hearthfire Nova: six meteors round you, everything near you burns
				const auto pp = p->GetPosition();
				for (int k = 0; k < 6; ++k) {
					const float a = k * RE::NI_PI / 3.0f;
					g_detonations.push_back({ pp + RE::NiPoint3{ std::cos(a) * 360.0f, std::sin(a) * 360.0f, 0 }, base * 4.0f, 0.2f + 0.15f * k, 2, a_gun });
				}
				for (auto* f : Foes(pp, 700.0f)) {
					hit(f, 1.5f);
					g_dots.push_back({ f->GetHandle(), base * 0.8f, 5.0f });
				}
				Blast(BlastIndex("firestorm"), pp);
			} else if (id == "gildergreen") {  // Whiterun, Kyne's Bloom: living wood roots them, you are healed whole
				const auto pp = p->GetPosition();
				for (auto* f : Foes(pp, 900.0f)) {
					hit(f, 3.0f);
					Director::Web(f->GetPosition(), 40.0f, 5.0f, 96.0f);
				}
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth));
				Blast(BlastIndex("lightmassive"), pp);
				Bolts(p, g, Foes(pp, 900.0f), 6);
			} else if (id == "lorkhan") {  // Markarth, Tonal Stillness: time stops for them, then they shatter
				for (auto* f : Foes(p->GetPosition(), 1500.0f)) {
					Director::Web(f->GetPosition(), 40.0f, 4.0f, 100.0f);
					g_frozen.push_back({ f->GetHandle(), 4.0f, base * 5.0f });
				}
				Blast(BlastIndex("shockmass"), p->GetPosition());
			} else if (id == "arkay") {  // Falkreath, Return to the Grave: Arkay's judgement on every undead near you
				auto foes = Foes(p->GetPosition(), 1200.0f);
				for (auto* f : foes) hit(f, 6.0f);
				Bolts(p, g, foes, 8);
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.3f);
				Blast(BlastIndex("priestlarge"), p->GetPosition());
			} else if (id == "mirestaff") {  // Morthal, Drowning Mire: a bog drags them in and the plague runs through them
				g_wells.push_back({ aim.hit, 4.0f, base * 1.5f, 0.0f });
				Audio::AtPoint(gen::Sound::linger_plague, aim.hit);
				for (auto* f : Foes(aim.hit, 900.0f)) {
					g_dots.push_back({ f->GetHandle(), base * 0.8f, 6.0f });
					g_plagued[f->GetFormID()] = base * 0.8f;
				}
				LaunchBolt(p, g, aim.hit);
			} else if (id == "magnus") {  // Winterhold, Gaze of Magnus: three crystal eyes burn them for eight seconds
				for (int k = 0; k < 3; ++k) {
					const float a = k * 2.0944f;
					g_prisms.push_back({ aim.hit + RE::NiPoint3{ std::cos(a) * 220.0f, std::sin(a) * 220.0f, 140.0f }, 8.0f, 0.1f * k, base * 1.5f, a_gun });
				}
				LaunchBolt(p, g, aim.hit);
			} else if (id == "wolfqueen") {  // Solitude, Potema's Requiem: nine arrows, and the dead they strike turn on their own
				for (int k = -4; k <= 4; ++k) {
					const float yaw = k * 0.09f, c = std::cos(yaw), sn = std::sin(yaw);
					const RE::NiPoint3 d{ aim.dir.x * c - aim.dir.y * sn, aim.dir.x * sn + aim.dir.y * c, aim.dir.z };
					LaunchArrow(p, g, aim.eye + d * 2600.0f);
				}
				for (auto* f : InCone(aim, 2600.0f, 40.0f)) {
					hit(f, 2.5f);
					Director::Frenzy(f, 10.0f);
				}
			} else if (id == "ysgramor") {  // Windhelm, Wrath of Ysgramor: ice and lightning on twelve at once
				auto foes = Foes(aim.hit, 1600.0f);
				if (foes.size() > 12) foes.resize(12);
				for (auto* f : foes) {
					hit(f, 3.5f);
					Director::Web(f->GetPosition(), 40.0f, 4.0f, 96.0f, true);
					const RE::NiPoint3 at = f->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f };
					const RE::NiPoint3 sky = at + RE::NiPoint3{ 30.0f, 0, 1300.0f };
					LaunchArrow(p, g, at, &sky);
				}
				Blast(BlastIndex("icevolley"), aim.hit);
			} else if (id == "nightingale") {  // Riften, Twilight's Eclipse: all in sight marked; each that falls takes the next
				for (auto* f : Foes(p->GetPosition(), 2200.0f)) {
					g_hexed[f->GetFormID()] = 12.0f;
					g_doomed[f->GetFormID()] = base * 4.0f;
					hit(f, 0.5f);
				}
				LaunchArrow(p, g, aim.hit);
				Blast(BlastIndex("darkmassive"), p->GetPosition());
			} else if (id == "nightmare") {  // Dawnstar, Nightmare Rain: ten burning stars from a dreaming sky
				for (int k = 0; k < 10; ++k) {
					const float a = k * 2.4f, r = 40.0f + 38.0f * k;
					g_detonations.push_back({ aim.hit + RE::NiPoint3{ std::cos(a) * r, std::sin(a) * r, 0 }, base * 4.0f, 0.2f + 0.22f * k, 2, a_gun });
				}
			}
		}


		void TickMarks(float a_delta)
		{
			for (auto it = g_hexed.begin(); it != g_hexed.end();) {
				it = (it->second -= a_delta) <= 0 ? g_hexed.erase(it) : std::next(it);
			}
			std::vector<Detonation> now;
			for (auto& d : g_detonations) {
				if ((d.delay -= a_delta) <= 0) {
					now.push_back(d);
				}
			}
			std::erase_if(g_detonations, [](const Detonation& d) { return d.delay <= 0; });
			auto* p = RE::PlayerCharacter::GetSingleton();
			for (auto& d : now) {
				if (d.kind == 0) {         // a doomed one goes off: the dark reaches up out of it
					Blast(BlastIndex("tentacle"), d.at);
					Audio::AtPoint(gen::Sound::linger_doom, d.at);
					for (auto* f : Foes(d.at, 230.0f)) {
						Damage(f, d.damage, false, false, true);
					}
				} else if (d.kind == 1) {  // the plague jumps to the two nearest
					auto foes = Foes(d.at, 300.0f);
					if (!foes.empty()) {
						Audio::AtPoint(gen::Sound::linger_jump, d.at);
					}
					for (std::size_t i = 0; i < foes.size() && i < 2; ++i) {
						if (!g_plagued.contains(foes[i]->GetFormID())) {
							g_dots.push_back({ foes[i]->GetHandle(), d.damage, 3.0f });
							g_plagued[foes[i]->GetFormID()] = d.damage;
						}
					}
				} else if (d.kind == 2 && d.gun >= 0) {  // a star falls
					const RE::NiPoint3 sky = d.at + RE::NiPoint3{ 80.0f, 40.0f, 1400.0f };
					if (gen::kGuns[d.gun].kind == gen::kKindBow) {
						LaunchArrow(p, gen::kGuns[d.gun], d.at, &sky);
					} else {
						LaunchBolt(p, gen::kGuns[d.gun], d.at, &sky);
					}
					Blast(gen::kGuns[d.gun].chargeBlast >= 0 ? gen::kGuns[d.gun].chargeBlast : BlastIndex("shockmass"), d.at, 0.2f);  // each weapon's own fall
					for (auto* f : Foes(d.at, 260.0f)) {
						Damage(f, d.damage, false, false, true);
						if (!f->IsDead()) {
							Stagger(f, 0.7f);
						}
					}
				}
			}
		}

		void TickTimeAndPrisms(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			for (auto& f : g_frozen) {
				if ((f.left -= a_delta) <= 0) {
					if (auto a = f.who.get(); a && !a->IsDead()) {
						Blast(BlastIndex("light"), a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f });
						Damage(a.get(), f.damage, false, false);
						if (!a->IsDead()) {
							Stagger(a.get(), 1.0f);
						}
					}
				}
			}
			std::erase_if(g_frozen, [](const Frozen& f) { return f.left <= 0; });
			for (auto& pr : g_prisms) {
				pr.left -= a_delta;
				if ((pr.tick -= a_delta) > 0) {
					continue;
				}
				pr.tick = 0.3f;
				Blast(BlastIndex("light"), pr.at);
				auto foes = Foes(pr.at, 1100.0f);
				if (foes.size() > 3) {
					foes.resize(3);
				}
				for (auto* f : foes) {
					LaunchBolt(p, gen::kGuns[pr.gun], HeadPos(f) - RE::NiPoint3{ 0, 0, 20.0f }, &pr.at);
					Damage(f, pr.base * 0.9f, false, false, true);
				}
			}
			std::erase_if(g_prisms, [](const Prism& pr) { return pr.left <= 0; });
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
					r->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::clamp(grow * w.burst / 420.0f * (1.0f + 0.06f * std::sin(w.life * 9.0f)), 0.05f, 9.0f) * 100.0f);
					r->SetAngle({ 0.0f, 0.0f, w.life * 2.4f });
					if (auto* n = r->Get3D()) {
						n->local.scale = r->GetReferenceRuntimeData().refScale / 100.0f;
						RE::NiUpdateData ud;
						n->Update(ud);
					}
				}
				if (w.pulse <= 0) {
					w.pulse = 0.5f;
					Blast(BlastIndex(w.vortex ? "daedraball" : "dark"), w.at + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, w.vortex ? 0.35f : kBlastScale);
					for (auto* f : Foes(w.at, w.vortex ? w.burst * 0.55f : 260.0f)) {
						Damage(f, w.base * 0.4f, false, false, true);
					}
				}
				if (w.left <= 0) {  // collapse
					Blast(BlastIndex("darkmassive"), w.at + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, w.vortex ? std::clamp(w.burst / 420.0f, 0.5f, 2.0f) : kBlastScale);
					if (w.vortex) {
						Blast(BlastIndex("constellation"), w.at + RE::NiPoint3{ 0, 0, 60.0f }, 0.05f, std::clamp(w.burst / 500.0f, 0.4f, 1.6f));
						Blast(BlastIndex("daedraball"), w.at + RE::NiPoint3{ 0, 0, 80.0f }, 0.1f, 0.6f);
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
		}

		void StartRelease(const gen::GunDef& g);
		// The finale every signature move shares: big blasts where it lands and on who it hit (or a ring around you
		// for the sweeping moves), and the element's master spell on top.
		void Finale(const gen::GunDef& g, const RE::NiPoint3& a_eye, const RE::NiPoint3& a_hit)
		{
			auto*                     p = RE::PlayerCharacter::GetSingleton();
			const bool                around = g.chargeStyle == gen::kSweep;
			const RE::NiPoint3        centre = around ? p->GetPosition() : a_hit;
			std::vector<RE::NiPoint3> spots;
			if (around) {
				for (int k = 0; k < 6; ++k) {
					const float a = k * RE::NI_PI / 3.0f;
					spots.push_back(centre + RE::NiPoint3{ std::cos(a) * 280.0f, std::sin(a) * 280.0f, 40.0f });
				}
			} else {
				spots.push_back(a_hit);
			}
			auto foes = Foes(centre, around ? 650.0f : 520.0f);
			if (foes.size() > 5) {
				foes.resize(5);
			}
			for (std::size_t i = 0; i < foes.size() && i < 2; ++i) {  // one burst where it lands and a couple on the caught - not a carpet
				spots.push_back(foes[i]->GetPosition() + RE::NiPoint3{ 0, 0, 60 });
			}
			for (std::size_t i = 0; i < spots.size(); ++i) {
				Blast(g.chargeBlast, spots[i], (around ? 0.0f : BoltDelay(g, a_eye, spots[i])) + 0.07f * i);
			}
			if (const int gi = static_cast<int>(&g - gen::kGuns.data()); gi >= 0 && gi < static_cast<int>(gen::kGuns.size()) && gen::kGunSounds[gi].boom >= 0 && !spots.empty()) {
				Audio::AtPoint(static_cast<gen::Sound>(gen::kGunSounds[gi].boom), spots[0], around ? 0.0f : BoltDelay(g, a_eye, spots[0]));  // the burst where it lands
			}
			if (g.chargeSpell >= 0) {
				if (foes.empty()) {
					Cast(g.chargeSpell, nullptr);
				}
				for (std::size_t i = 0; i < foes.size() && i < 3; ++i) {
					Cast(g.chargeSpell, foes[i]);
				}
			}
		}

		int   g_echoGun = -1;
		float g_echoT = 0;
		void TickEcho(float a_delta)
		{
			if (g_echoGun < 0 || (g_echoT -= a_delta) > 0) {
				return;
			}
			const int gun = g_echoGun;
			g_echoGun = -1;
			if (gun != CurrentGun() || !GunOut()) {
				return;
			}
			g_unleashMult = 0.75f;
			Unleash(gun, true);
			g_unleashMult = 1.0f;
		}

		bool TryUnleash(bool a_released)
		{
			const int gun = CurrentGun();
			if (gun < 0 || !a_released) {
				return false;
			}
			const auto& g = gen::kGuns[gun];
			if (g_charge < g.chargeSeconds || g_chargeCd > 0) {
				return false;
			}
			if (g.mana > 0) {
				auto*       av = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
				const float cost = g.mana * g.chargeMana * Perks::ManaMult() * Difficulty::Mana();
				if (av->GetActorValue(RE::ActorValue::kMagicka) < cost) {
					Forms::Play(g.emptySnd);
					Hud::Banner("Not enough magicka", 1.2f);
					return true;
				}
				av->DamageActorValue(RE::ActorValue::kMagicka, cost);
				g_manaQuiet = 0;
			}
			const bool ascendant = ForgeLevel() >= gen::kForgeMax;
			g_chargeCd = g.chargeCooldown * (ascendant ? 0.75f : 1.0f) * Perks::ChargeMult();
			StartRelease(g);
			g_unleashMult = ascendant ? 1.25f : 1.0f;
			Unleash(gun);
			g_unleashMult = 1.0f;
			if (ascendant) {  // the echo: the same move again, a heartbeat later
				g_echoGun = gun;
				g_echoT = 0.7f;
			}
			{
				const Aim aim = AimAt(std::min(g.range * 1.5f, 8000.0f));
				Finale(g, aim.eye, aim.hit);
			}
			return true;
		}

		// ================================================================ signature-move animation
		// The charge-up and release of every signature move have their own motion of the casting arm (and wand):
		// thrust (lances), slam (impact moves), sweep (around-you moves), twirl (wand spin), raise (call from
		// the sky) and surge (wonder blasts). Built on top of Skyrim's animation each frame, after it ran.
		float g_relT = -1.0f, g_spin = 0.0f, g_fovKick = 0.0f;
		int   g_relStyle = 0;
		bool  g_chargeSaid = false, g_readySaid = false, g_armLive = false;
		struct PoseOut
		{
			float pitch = 0, yaw = 0, roll = 0, push = 0, lift = 0, shake = 0, spinRate = 0;
		};
		PoseOut g_pose;
		float   g_jx = 0, g_jy = 0;  // this frame's tremble (rolled on the main thread)

		PoseOut Scale(const PoseOut& a, float k) { return { a.pitch * k, a.yaw * k, a.roll * k, a.push * k, a.lift * k, a.shake * k, a.spinRate * k }; }
		PoseOut Lerp(const PoseOut& a, const PoseOut& b, float t)
		{
			return { a.pitch + (b.pitch - a.pitch) * t, a.yaw + (b.yaw - a.yaw) * t, a.roll + (b.roll - a.roll) * t, a.push + (b.push - a.push) * t,
				a.lift + (b.lift - a.lift) * t, a.shake + (b.shake - a.shake) * t, a.spinRate + (b.spinRate - a.spinRate) * t };
		}
		float EaseOut(float x) { x = std::clamp(x, 0.0f, 1.0f); return 1 - (1 - x) * (1 - x) * (1 - x); }
		float EaseInOut(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }

		//                                     pitch  yaw   roll   push  lift  shake  spin
		constexpr PoseOut kChargePose[6] = { { 0.10f, 0.05f, 0.35f, -12, 0, 0.010f, 0 },     // thrust: drawn back, twisted
			{ 0.75f, -0.10f, -0.25f, -4, 6, 0.012f, 0 },                                          // slam: raised high
			{ 0.15f, -0.65f, 0.45f, -3, 0, 0.010f, 0 },                                           // sweep: wound up to the right
			{ 0.40f, 0.0f, 0.0f, 0, 4, 0.006f, 16.0f },                                           // twirl: raised, spinning
			{ 1.05f, 0.0f, 0.0f, 6, 10, 0.016f, 0 },                                              // raise: up to the sky
			{ -0.05f, 0.0f, 0.0f, -14, -2, 0.030f, 0 } };                                         // surge: pulled in, shaking
		constexpr PoseOut kStrikePose[6] = { { 0.0f, 0.0f, 0.0f, 26, 0, 0.004f, 0 },
			{ -0.45f, 0.10f, 0.15f, 8, -4, 0.010f, 0 },
			{ 0.10f, 0.90f, -0.40f, 4, 0, 0.004f, 0 },
			{ 0.10f, 0.0f, 0.0f, 10, 0, 0.004f, 34.0f },
			{ -0.30f, 0.0f, 0.0f, 10, -6, 0.020f, 0 },
			{ -0.12f, 0.0f, 0.0f, 30, 0, 0.050f, 0 } };
		// Staffs keep Skyrim's staff stance: the charge only draws the staff in a little and lifts its head, with the
		// power trembling through it; the release drives it forward (no spinning, no hand-casting gestures).
		// Charge: the staff drawn back and raised, its head lifted, the power trembling through it. Release: driven
		// forward, the head dipping toward the target. Tap shot: a short jab.
		constexpr PoseOut kStaffCharge{ 0.38f, 0.0f, 0.12f, -16, 9, 0.010f, 0 };
		constexpr PoseOut kStaffStrike{ -0.22f, 0.0f, -0.05f, 34, -2, 0.008f, 0 };
		constexpr PoseOut kStaffTap{ -0.12f, 0.0f, 0.0f, 20, 0, 0.0f, 0 };
		// Bows: drawn in toward the cheek as the shot charges, a sharp kick when the arrow leaves.
		constexpr PoseOut kBowDraw{ 0.06f, 0.04f, 0.0f, -7, 2, 0.006f, 0 };
		constexpr PoseOut kBowLoose{ 0.16f, -0.05f, 0.0f, -12, 4, 0.0f, 0 };
		constexpr float kFovKick[6] = { -9.0f, 7.0f, 5.0f, 4.0f, 6.0f, 12.0f };

		// While charging, the element's storm wraps the caster (Skyrim's fire storm / blizzard / lightning storm cast look).
		int g_aura = -1;
		void SetAura(int a_spell)
		{
			if (a_spell == g_aura) {
				return;
			}
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (g_aura >= 0) {
				if (auto* sp = Forms::Spell(g_aura) ? Forms::Spell(g_aura)->As<RE::SpellItem>() : nullptr) {
					p->RemoveSpell(sp);
				}
			}
			g_aura = a_spell;
			if (a_spell >= 0) {
				if (auto* sp = Forms::Spell(a_spell) ? Forms::Spell(a_spell)->As<RE::SpellItem>() : nullptr) {
					p->AddSpell(sp);
				}
			}
		}

		void StartRelease(const gen::GunDef& g)
		{
			g_relT = 0.0f;
			g_relStyle = std::clamp(g.chargeStyle, 0, 5);
			g_fovKick = kFovKick[g_relStyle];
		}

		void TickPose(float a_delta)
		{
			const int gun = CurrentGun();
			PoseOut   out;
			if (gun >= 0 && GunOut()) {
				const auto& g = gen::kGuns[gun];
				const int   style = std::clamp(g.chargeStyle, 0, 5);
				const bool  bow = g.kind == gen::kKindBow;  // the draw is the animation
				const bool  charging = (IsSpell(gun) ? g_dualHeld : g_trigger) && g.chargeSeconds > 0 && g_chargeCd <= 0 && g_charge > 0.18f;
				const float c = charging ? EaseInOut((g_charge - 0.18f) / std::max(0.1f, g.chargeSeconds - 0.18f)) : 0.0f;
				if (charging && !g_chargeSaid) {
					const int st = gen::kGunSounds[gun].start;
					Audio::Ui(st >= 0 ? static_cast<gen::Sound>(st) : gen::Sound::charge_up);  // the spell's own swell as its power begins to gather
					if (const int hum = gen::kGunSounds[gun].charge; hum >= 0) {
						Audio::LoopStart(Audio::kLoopCharge, static_cast<gen::Sound>(hum));  // the spell's own hum while it gathers
					}
					g_chargeSaid = true;
				}
				if (charging && c >= 1.0f && !g_readySaid) {
					const int fu = gen::kGunSounds[gun].full;
					Audio::Ui(fu >= 0 ? static_cast<gen::Sound>(fu) : gen::Sound::charge_ready);  // full: its own peak
					g_readySaid = true;
				}
				if (!charging && Audio::Looping(Audio::kLoopCharge)) {
					Audio::LoopStop(Audio::kLoopCharge, 250);
				}
				if (!(IsSpell(gun) ? g_dualHeld : g_trigger)) {
					g_chargeSaid = g_readySaid = false;
				}
				SetAura(charging && !IsSpell(gun) ? g.chargeAura : -1);  // (the storm cloaks filled the first-person screen)
				const bool staff = g.kind == gen::kKindWand;
				const bool spell = IsSpell(gun);  // hand spells: Skyrim's own casting animations, nothing laid on top
				out = spell ? PoseOut{} : bow ? Scale(kBowDraw, c) : staff ? Scale(kStaffCharge, c) : Scale(kChargePose[style], c);
				if (spell) {
					g_relT = -1.0f;
				}
				out.shake *= 0.5f + c;  // trembles harder as it fills
				if (g_relT >= 0 && bow) {
					g_relT = -1.0f;
					g_tapT = 0.0f;  // the loose
				}
				if ((staff || bow) && g_tapT >= 0 && g_relT < 0 && !charging) {
					const float u = g_tapT / 0.32f;   // in fast, settle back
					const PoseOut& tap = bow ? kBowLoose : kStaffTap;
					out = u < 0.25f ? Lerp(PoseOut{}, tap, EaseOut(u / 0.25f)) : Lerp(tap, PoseOut{}, EaseInOut((u - 0.25f) / 0.75f));
					if ((g_tapT += a_delta) >= 0.32f) {
						g_tapT = -1.0f;
					}
				}
				if (g_relT >= 0) {
					g_tapT = -1.0f;
					const int   rs = g_relStyle;
					const float dur = rs == gen::kRaise ? 0.75f : 0.55f;
					float       u = g_relT / dur;
					if (rs == gen::kRaise) {  // hold up high for a beat, then bring it down
						u = u < 0.25f ? 0.0f : (u - 0.25f) / 0.75f;
					}
					constexpr float k = 0.22f;
					const PoseOut& from = staff ? kStaffCharge : kChargePose[rs];
					const PoseOut& to = staff ? kStaffStrike : kStrikePose[rs];
					out = u < k ? Lerp(from, to, EaseOut(u / k)) : Lerp(to, PoseOut{}, EaseInOut((u - k) / (1 - k)));
					if ((g_relT += a_delta) >= dur) {
						g_relT = -1.0f;
					}
				}
			} else {
				g_relT = -1.0f;
				SetAura(-1);
			}
			g_spin += out.spinRate * a_delta;
			if (out.spinRate <= 0.01f) {  // settle the spin back to rest the short way
				const float rest = std::round(g_spin / (2 * RE::NI_PI)) * 2 * RE::NI_PI;
				g_spin += (rest - g_spin) * std::min(1.0f, a_delta * 10.0f);
			}
			g_fovKick += (0.0f - g_fovKick) * std::min(1.0f, a_delta * 6.0f);
			g_jx = out.shake > 0 ? Rand(-out.shake, out.shake) : 0.0f;
			g_jy = out.shake > 0 ? Rand(-out.shake, out.shake) : 0.0f;
			g_pose = out;
		}

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
		struct ArmState
		{
			RE::NiTransform orig, written;
			RE::NiAVObject* bone = nullptr;
		};
		ArmState g_arm1st, g_arm3rd;
		void PoseArm(RE::PlayerCharacter* a_player)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam) {
				return;
			}
			const bool first = cam->IsInFirstPerson();
			auto*      root = a_player->Get3D(first);
			if (!root) {
				return;
			}
			ArmState& st = first ? g_arm1st : g_arm3rd;
			auto*     bone = root->GetObjectByName("NPC R UpperArm [RUar]");
			if (!bone || !bone->parent) {
				return;
			}
			if (bone == st.bone && std::memcmp(&bone->local, &st.written, sizeof(RE::NiTransform)) == 0) {
				bone->local = st.orig;  // the animation did not rewrite it this frame: start from the clean pose
			}
			st.bone = bone;
			PoseOut o = g_pose;
			if (!first) {  // the third-person body is seen from outside: the same motion, a little broader
				o.pitch *= 1.25f, o.yaw *= 1.25f, o.roll *= 1.25f, o.push *= 0.8f, o.lift *= 0.8f;
			}
			if (std::abs(o.pitch) + std::abs(o.yaw) + std::abs(o.roll) + std::abs(o.push) + std::abs(o.lift) + o.shake < 1e-3f) {
				st.bone = nullptr;
				return;
			}
			const float jx = g_jx, jy = g_jy;
			const float pitch = a_player->data.angle.x, yaw = a_player->data.angle.z;
			const RE::NiPoint3 f{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
			const RE::NiPoint3 u{ std::sin(yaw) * std::sin(pitch), std::cos(yaw) * std::sin(pitch), std::cos(pitch) };
			const RE::NiPoint3 r{ std::cos(yaw), -std::sin(yaw), 0 };
			const RE::NiMatrix3 D = AxisAngle(u, o.yaw + jx) * AxisAngle(r, o.pitch + jy) * AxisAngle(f, o.roll);
			const RE::NiMatrix3 P = bone->parent->world.rotate;
			const RE::NiMatrix3 Pt = P.Transpose();
			st.orig = bone->local;
			bone->local.rotate = Pt * D * P * bone->local.rotate;
			bone->local.translate += Pt * (f * o.push + u * o.lift) * (1.0f / std::max(0.01f, bone->parent->world.scale));
			st.written = bone->local;
			RE::NiUpdateData ud;
			ud.time = 0.0f;
			bone->UpdateDownwardPass(ud, 0);
			if (!g_armLive) {
				g_armLive = true;
				logger::info("pose: arm animation live ({} person)", first ? "first" : "third");
			}
		}

#include "Spellcraft.inl"
		namespace Props
		{
			void Tornado(int a_kind, const RE::NiPoint3& a_at, RE::NiPoint3 a_dir, float a_seconds, float a_damage, bool a_chase, int a_row);
		}
#include "Bows.inl"
#include "Staffs.inl"
#include "WonderProps.inl"

		struct PoseHook
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				try {
					PoseArm(a_this);
					Spellcraft::OnPose(a_this);
					Movement::PoseGlideArms(a_this);
					Movement::SwingBody(a_this);
				} catch (...) {
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void Knife()
		{
			if (g_knifeCd > 0) {
				return;
			}
			g_knifeCd = gen::kKnife.cooldownMs / 1000.0f;
			auto* player = RE::PlayerCharacter::GetSingleton();
			const float yaw = player->GetAngleZ();
			const RE::NiPoint3 fwd{ std::sin(yaw), std::cos(yaw), 0 };
			RE::Actor* best = nullptr;
			float      bestD = gen::kKnife.range;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!a || !Targetable(a.get(), player)) {
					continue;
				}
				RE::NiPoint3 to = a->GetPosition() - player->GetPosition();
				to.z = 0;
				const float d = to.Length();
				if (d > bestD || d < 1) {
					continue;
				}
				to.Unitize();
				if (to.Dot(fwd) < std::cos(gen::kKnife.coneDeg * RE::NI_PI / 180.0f)) {
					continue;
				}
				best = a.get(), bestD = d;
			}
			Forms::Play(gen::Sound::knife);
			if (best) {
				Damage(best, gen::kKnife.damage * CrewMult(2), false, true);
			}
		}

		void Cycle()
		{
			const int n = Gun::SlotCount();
			int       cur = SlotOf(CurrentGun());
			for (int k = 1; k <= n; ++k) {
				const int s = ((cur < 0 ? Save::ActiveSlot() - 1 : cur) + k + n) % n;
				if (const auto sl = Save::GetSlot(s); sl.gun >= 0) {
					Save::SetActiveSlot(s);
					Equip(sl.gun);
					g_reload = 0;
					return;
				}
			}
		}

		void Holster()
		{
			if (auto* p = RE::PlayerCharacter::GetSingleton(); p && Drawn()) {
				p->DrawWeaponMagicHands(false);
			}
		}

		// Keep slots and inventory in step: a gun dropped or sold leaves its slot.
		void SyncSlots()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) {
				return;
			}
			for (int s = 0; s < 3; ++s) {
				auto sl = Save::GetSlot(s);
				if (sl.gun < 0) {
					continue;
				}
				if (auto* sp = Forms::GunSpell(sl.gun)) {
					if (!p->HasSpell(sp) || s >= Gun::SlotCount()) {
						p->RemoveSpell(sp);
						Save::SetSlot(s, {});
					}
					continue;
				}
				auto* w = Forms::Gun(sl.gun);
				const auto counts = p->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
				if (counts.empty() || s >= Gun::SlotCount()) {
					if (s >= Gun::SlotCount() && !counts.empty() && !ItemMenusOpen()) {
						p->RemoveItem(w, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);  // lost the third slot
					}
					Save::SetSlot(s, {});
				}
			}
		}
	}

	namespace Gun
	{
		int   SlotCount() { return Save::HasPerk(gen::kPerk_mammoth) ? 3 : 2; }
		void  FxBlast(std::string_view a_name, const RE::NiPoint3& a_at, float a_delay) { Blast(BlastIndex(a_name), a_at, a_delay, 0.6f); }
		void  FxFrom(RE::BGSExplosion* a_src, const RE::NiPoint3& a_at, float a_scale)
		{
			if (auto* q = QuietClone(a_src); q && g_blasts.size() < 32) g_blasts.push_back({ a_at, 0.0f, -1, a_scale, q });
		}
		RE::BGSExplosion* Quiet(RE::BGSExplosion* a_src) { return QuietClone(a_src); }
		// (the class perks: Ember Heart) the night's dead round a point lose a share of a whole zombie's health
		void  AreaHurt(const RE::NiPoint3& a_at, float a_radius, float a_share, std::string_view a_blast)
		{
			const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
			for (auto* f : Foes(a_at, a_radius)) {
				Damage(f, whole * a_share, false, false, true);
			}
			if (!a_blast.empty()) Blast(BlastIndex(a_blast), a_at, 0.0f, 0.45f);
		}
		// (Noah: the Ranger's wonder, the Subsuming Vortex) "a purple black hole that sucks in enemies then explodes - its
		// Legendary form a larger black hole with stronger pull": a hole opens where the arrow struck, drags the dead in for
		// 2.2 s (3 s Legendary), gnawing at those it holds, then bursts. At most three stand at once.
		void  BlackHole(const RE::NiPoint3& a_at, bool a_legendary)
		{
			if (std::count_if(g_wells.begin(), g_wells.end(), [](const Well& w) { return w.vortex; }) >= 3) return;
			auto* p = RE::PlayerCharacter::GetSingleton();
			RE::NiPoint3 at = a_at;
			if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(at, land) && land > at.z - 300.0f && land < at.z + 60.0f) {
				at.z = std::max(at.z, land + 70.0f);  // (hanging just over the ground, never in it)
			}
			const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
			Well w{ at, a_legendary ? 3.0f : 2.2f, whole * (a_legendary ? 0.32f : 0.22f), 0.15f };
			w.vortex = true;
			w.reach = a_legendary ? 1300.0f : 800.0f;
			w.pull = a_legendary ? 620.0f : 360.0f;
			w.burst = a_legendary ? 620.0f : 400.0f;
			auto* base = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESBoundObject>(gen::kVortexFx, gen::kPlugin);
			if (auto ref = p && base ? p->PlaceObjectAtMe(base, false) : nullptr) {
				ref->SetPosition(at);
				ref->GetReferenceRuntimeData().refScale = 5;
				w.fx = ref->CreateRefHandle();
			}
			Blast(BlastIndex("daedramouth"), at, 0.0f, 0.4f);
			Audio::AtPoint(gen::Sound::unleash_hearth, at);
			g_wells.push_back(std::move(w));
			logger::info("vortex: a black hole opens{}", a_legendary ? " (Legendary)" : "");
		}
		float AdsBlend() { return g_ads; }
		bool  Owns(int a_gun) { return SlotOf(a_gun) >= 0; }

		void RefillAmmo(int a_gun)
		{
			(void)a_gun;  // wands never run dry; tomes run on magicka
		}

		// a Falmer staff's own bolt struck something: our numbers and its element's work (Staffs.inl StaffTap)
		bool TestKitOn();
		bool OnStaffHit(RE::Actor* a_target, RE::FormID a_source)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			const int gun = CurrentGun();
			if (!p || !a_target || !IsNativeStaff(gun)) return false;
			auto* w = Forms::Gun(gun);
			if (!w || (a_source != w->GetFormID() && !(w->formEnchanting && a_source == w->formEnchanting->GetFormID()))) return false;
			static std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> last;  // (an area spell reports each target once)
			const auto now = std::chrono::steady_clock::now();
			if (auto it = last.find(a_target->GetFormID()); it != last.end() && now - it->second < std::chrono::milliseconds(150)) return true;
			last[a_target->GetFormID()] = now;
			const auto& g = gen::kGuns[gun];
			const float dmg = g.damage * Perks::DamageMult() * UpgradeMult(ForgeLevel());
			if (Director::IsFoe(a_target)) Damage(a_target, dmg, false, false, true);
			if (g.id == "gravecaller" && Director::IsFoe(a_target) && !a_target->IsDead()) Classes::MarkForSummons(a_target);  // (the Conjurer's Staff)
			StaffTap(g, a_target->GetPosition(), a_target, dmg);
			return true;
		}
		void OnStaffCast(RE::FormID a_spell)  // a Falmer staff cast: its magicka (staffs run on it here)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			const int gun = CurrentGun();
			auto* w = IsNativeStaff(gun) ? Forms::Gun(gun) : nullptr;
			if (!p || !w || !w->formEnchanting || w->formEnchanting->GetFormID() != a_spell) return;
			if (!TestKitOn()) p->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kMagicka, gen::kGuns[gun].mana * Perks::ManaMult() * Difficulty::Mana());
		}
		int LevelOf(int a_gun) { const int s = SlotOf(a_gun); return s >= 0 ? Save::GetSlot(s).level : -1; }
		void SetLevelOf(int a_gun, int a_level)
		{
			if (const int s = SlotOf(a_gun); s >= 0) {
				auto sl = Save::GetSlot(s);
				sl.level = std::clamp(a_level, 0, gen::kForgeMax);
				Save::SetSlot(s, sl);
			}
		}
		bool IsStaffWonder(int)
		{
			return false;  // (Noah: the staff wonders are removed - each class has its own wonder now)
		}
		int TakeAway(int a_gun)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			const int s = SlotOf(a_gun);
			if (!p || s < 0) return -1;
			const int lv = Save::GetSlot(s).level;
			if (auto* w = Forms::Gun(a_gun); w && p->GetItemCount(w) > 0) {
				p->RemoveItem(w, p->GetItemCount(w), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			Save::SetSlot(s, { -1, 0, 0, 0 });
			return lv;
		}
		bool Give(int a_gun)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* w = Forms::Gun(a_gun);
			auto* spell = Forms::GunSpell(a_gun);
			if (!p || (!w && !spell)) {
				return false;
			}
			if (Owns(a_gun)) {
				Equip(a_gun);
				return true;
			}
			int slot = -1;
			for (int s = 0; s < SlotCount() && slot < 0; ++s) {
				if (Save::GetSlot(s).gun < 0) {
					slot = s;
				}
			}
			if (slot < 0) {  // full: replace the gun in hand (or the active slot)
				slot = SlotOf(CurrentGun());
				if (slot < 0) {
					slot = Save::ActiveSlot() % SlotCount();
				}
				const int oldGun = Save::GetSlot(slot).gun;
				if (auto* oldSpell = Forms::GunSpell(oldGun)) {
					p->RemoveSpell(oldSpell);
				} else if (auto* old = Forms::Gun(oldGun)) {
					p->RemoveItem(old, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
			}
			Save::SetSlot(slot, { a_gun, gen::kGuns[a_gun].clip, gen::kGuns[a_gun].reserve });
			Save::SetActiveSlot(slot);
			if (spell) {
				p->AddSpell(spell);
			} else {
				p->AddObjectToContainer(w, nullptr, 1, nullptr);
			}
			Equip(a_gun);
			logger::info("gun: {} -> slot {}", gen::kGuns[a_gun].id, slot);
			return true;
		}

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
		constexpr float kKitVersion = 4.0f;  // (raised whenever the kit gains something: test characters get the new things on load)
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
			// the weapons: every elemental bow (with its arrows)
			for (int i = Bows::Count() - 1; i >= 0; --i) {
				if (auto* w = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kBows[i].weap, gen::kPlugin); w && p->GetItemCount(w) <= 0) {
					p->AddObjectToContainer(w, nullptr, 1, nullptr);
				}
			}
			if (auto* bane = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kFangWeap, gen::kPlugin); bane && p->GetItemCount(bane) <= 0) {
				p->AddObjectToContainer(bane, nullptr, 1, nullptr);  // Alduin's Bane
			}
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {  // the four Falmer staffs (the wonder weapons)
				const std::string_view id = gen::kGuns[i].id;
				if (id == "gravecaller" || id == "frostbinder" || id == "wolfcrown" || id == "barrowhorn") {
					if (auto* w = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kGuns[i].weapId, gen::kPlugin); w && p->GetItemCount(w) <= 0) {
						p->AddObjectToContainer(w, nullptr, 1, nullptr);
					}
					if (LevelOf(i) >= 0) SetLevelOf(i, gen::kForgeMax);  // (Noah: tier 3 staffs to test; unslotted ones get it as they're taken up)
				}
			}
			for (int i = 0; i < static_cast<int>(gen::kShouts.size()); ++i) {
				if (!KnowsShout(i)) {
					LearnShout(i);
				}
			}
			for (int i = 0; i < 64; ++i) {  // every Echo Altar spell (all 17)
				if (Arcana::Spell(i)) {
					if (!Arcana::Known(i)) Arcana::Give(i, false);
					++spells, ++byEl[std::string(Arcana::Name(i))];
				}
			}
			Bows::SetLevels(std::vector<std::uint8_t>(BowsImpl::kN, static_cast<std::uint8_t>(gen::kForgeMax)));  // every bow at tier 3
			if (auto* bane = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kFangWeap, gen::kPlugin)) {  // Alduin's Bane, in your hands
				RE::ActorEquipManager::GetSingleton()->EquipObject(p, bane);
			}
			Movement::TestKit();  // (Dragon's Updraft, the third bond, the glider)
			if (const int have = Points::Get(); have < 150000) {
				Points::Refund(150000 - have);
			}
			Hud::Banner(fmt::format("TEST KIT: the Echo Altar's {} spells, every bow, every shout, the glider, 150,000 Cursed Essence", spells), 7.0f);
			std::string list;
			for (auto& [el, c] : byEl) list += fmt::format("{} {}, ", el, c);
			logger::info("testkit: {} altar spells ({}) - {} others taken away; bows {}, shouts {}, essence {}", spells, list, removed, Bows::Count(), gen::kShouts.size(), Points::Get());
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

		// The old Dwemer guns are retired: take them out of the inventory (slots follow) and make sure
		// the player has the Apprentice's Tome.
		void MigrateRetired()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || g_migrated) {
				return;
			}
			g_migrated = true;
			int removed = 0;
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (!gen::kGuns[i].retired || IsStaffWonder(i)) {  // (the Falmer staffs are out of the box, not retired)
					continue;
				}
				auto* w = Forms::Gun(i);
				const auto counts = p->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
				for (auto& [obj, n] : counts) {
					p->RemoveItem(obj, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					++removed;
				}
			}
			for (int s = 0; s < 3; ++s) {
				auto sl = Save::GetSlot(s);
				if (sl.gun >= 0 && gen::kGuns[sl.gun].retired && !IsStaffWonder(sl.gun)) {
					Save::SetSlot(s, {});
				}
			}
			// Tomes and grimoires used to be carried as staff items; now they live in your hands as spells.
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				auto* sp = Forms::GunSpell(i);
				auto* w = Forms::Gun(i);
				if (!sp || !w) {
					continue;
				}
				const auto counts = p->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
				for (auto& [obj, n] : counts) {
					p->RemoveItem(obj, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					++removed;
				}
				if (SlotOf(i) >= 0 && !p->HasSpell(sp)) {
					p->AddSpell(sp);
				}
			}
			// The four tomes became the elemental bows: a slot that held one gets the bow (and its arrows).
			for (int s = 0; s < 3; ++s) {
				const int gun = Save::GetSlot(s).gun;
				if (gun >= 0 && gen::kGuns[gun].kind == gen::kKindBow) {
					if (auto* w = Forms::Gun(gun)) {
						const auto counts = p->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
						if (counts.empty()) {
							p->AddObjectToContainer(w, nullptr, 1, nullptr);
							TopUpArrows(gun, false);
							++removed;
						}
					}
				}
			}
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) {
				if (auto* sp = gen::kGuns[i].retired ? Forms::GunSpell(i) : nullptr; sp && p->HasSpell(sp)) {
					p->RemoveSpell(sp);
					++removed;
				}
			}
			bool any = false;
			for (int s = 0; s < 3; ++s) {
				any |= Save::GetSlot(s).gun >= 0;
			}
			bool knowsCoffer = false;
			for (int i = 0; i < 400 && !knowsCoffer; ++i) {
				knowsCoffer = Arcana::Known(i);
			}
			if (Save::Flag(Save::kFlagStarted) && !any && !knowsCoffer) {
				Arcana::GiveStarter();  // (a character with nothing in hand gets a Coffer spell to start with)
			}
			if (removed) {
				logger::info("gun: retired {} old guns from the inventory", removed);
				Hud::Banner("Old weapons swapped for the new arsenal.", 4.0f);
			}
		}

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
			g_trigger = g_adsHeld = g_dualHeld = false;
			g_reload = g_cooldown = 0;
			g_burstLeft = 0;
			Spellcraft::Reset();
			BowsImpl::Reset();
			// (the audit: a load mid-unleash let the old game's stars fall, wells pull and storms crackle in the new one)
			g_detonations.clear(), g_arcs.clear(), g_dots.clear(), g_shrink.clear(), g_blasts.clear(), g_wells.clear(), g_frozen.clear(), g_prisms.clear();
			g_hexed.clear(), g_doomed.clear(), g_plagued.clear();
			g_zones.clear(), g_chill.clear(), g_frozenUntil.clear();
			Props::g_world.clear(), Props::g_twisters.clear();
			g_storm.charges = 0;
		}

		// dev channel: 'cast tap|full|ult|orb' with the spell in hand (synthetic mouse input does not cast)
		void DevCast(const std::string& a_mode)
		{
			const int gun = CurrentGun();
			if (!Spellcraft::Is(gun)) {
				logger::info("cast: no spellcraft spell in hand (gun {})", gun);
				return;
			}
			if (a_mode == "tap" || a_mode == "full") {
				Spellcraft::g_cd = 0;
				Spellcraft::Tap(gun, a_mode == "full");
			} else if (a_mode == "ult") {
				Spellcraft::g_ultCd = 0;
				Spellcraft::Ult(gun);
			} else if (a_mode == "orb") {
				Spellcraft::g_orbHeld = !Spellcraft::g_orbHeld;
				Spellcraft::g_orbT = 0;
			} else if (a_mode == "probe") {
				Spellcraft::SpawnProbe(Spellcraft::Row(gun));
			} else if (a_mode == "hand") {
				Spellcraft::g_tapHeld = !Spellcraft::g_tapHeld;
				Spellcraft::g_tapT = 0;
			}
			logger::info("cast: {} {} ({} shots in flight)", gen::kGuns[gun].id, a_mode, Spellcraft::g_shots2.size());
		}

		// Atronach Reforge: the weapon in your hands goes up a level (more damage, full ammo).
		int ReforgeCost()
		{
			const int gun = CurrentGun(), slot = SlotOf(gun);
			if (const int b = BowsImpl::Held(); b >= 0) {  // the altar's bow
				return BowsImpl::g_level[b] >= gen::kForgeMax ? 0 : gen::kForgeCost[BowsImpl::g_level[b]];
			}
			if (gun < 0 || slot < 0) {
				const auto hands = Arcana::Hands();
				const int  sp = hands[1] >= 0 ? hands[1] : hands[0];
				if (sp >= 0) return Arcana::NextRung(sp) < 0 ? 0 : gen::kForgeCost[std::clamp(Arcana::RungOf(sp) - 1, 0, 2)];
				return -1;
			}
			const int lvl = Save::GetSlot(slot).level;
			return lvl >= gen::kForgeMax ? 0 : gen::kForgeCost[lvl];
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
			const int gun = CurrentGun(), slot = SlotOf(gun);
			if (const int b = BowsImpl::Held(); b >= 0) {  // the altar's bow in hand: more power, then piercing, then three arrows
				const int lv = BowsImpl::g_level[b];
				const std::string name(gen::kBows[b].label);
				if (lv >= gen::kForgeMax) {
					Hud::Banner(name + " cannot hold more power", 2.5f);
					return false;
				}
				if (const int have = Classes::BowMastery(b), want = Classes::BowMasteryNeed(b); have < want) {  // (Noah: the Ranger's bows need kills too)
					Hud::Banner(name + ": " + std::to_string(have) + " / " + std::to_string(want) + " kills with it before it can be reforged", 3.0f);
					return false;
				}
				if (ForgedTonight(Bows::kIdBase + b, name)) {
					return false;
				}
				if (!Points::Spend(gen::kForgeCost[lv])) {
					return false;
				}
				Classes::SpendBowMastery(b);
				g_forgedTonight.insert(Bows::kIdBase + b);
				BowsImpl::g_level[b] = static_cast<std::uint8_t>(lv + 1);
				BowsImpl::ApplySpeed(b);
				Hud::Banner(name + " reforged  " + std::string(lv + 1, '|') + "  - " + std::string(Bows::TierName(b, lv + 1)) + ", a faster draw", 4.0f);
				logger::info("forge: {} -> level {}", name, lv + 1);
				return true;
			}
			if (Classes::Get() == 2) {  // (the classes' growth) a crossbow in hand is reforged on its own kills
				int freedX = 0;
				for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) freedX += Curse::Freed(t) ? 1 : 0;
				bool spent = false;
				if (const int r = Classes::ReforgeCrossbow(freedX, spent); r >= 0) return spent;
			}
			if (gun < 0 || slot < 0) {
				// (the classes overhaul) a class spell in hand (the right first) climbs its ladder: Reforge for Destruction,
				// Bind Pact for the Summoner, Imbue for the Spellsword - the next rung's spell takes its place and its hands
				const auto hands = Arcana::Hands();
				const int  sp = hands[1] >= 0 ? hands[1] : hands[0];
				if (sp < 0) {
					const int c0 = Classes::Get();
					Hud::Banner(c0 == 2 ? "Hold an elemental bow to reforge it" : c0 == 0 ? "Hold the summon spell you want to Bind" : c0 == 3 ? "Hold the touch you want to Imbue" : "Hold the spell you want to Reforge", 2.5f);
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
				if (cls == 3) Classes::ImbueWeapon();  // (Noah's plan, the Spellsword) Imbue also steps the held spell-weapon up a material
				logger::info("forge: {} {} -> {}", verb, name, Arcana::Name(next));
				Hud::Banner(std::string(verb) + ":  " + name + "  ->  " + std::string(Arcana::Name(next)) + (Arcana::TierOf(next) >= 3 ? "  -  LEGENDARY" : ""), 4.5f);
				return true;
			}
			auto s = Save::GetSlot(slot);
			if (IsStaffWonder(gun) && s.level >= gen::kForgeMax - 1 && s.level < gen::kForgeMax) {  // (Noah: the staffs' last step is a ritual)
				Hud::Banner(std::string(gen::kGuns[gun].label) + ": its last power can't be bought - set it on its pedestal in High Hrothgar by day and feed it 50 souls", 6.0f);
				return false;
			}
			if (s.level >= gen::kForgeMax) {
				Hud::Banner(std::string(gen::kGuns[gun].label) + " cannot hold more power", 2.5f);
				return false;
			}
			if (ForgedTonight(gun, std::string(gen::kGuns[gun].label))) {
				return false;
			}
			if (!Points::Spend(gen::kForgeCost[s.level])) {
				return false;
			}
			g_forgedTonight.insert(gun);
			s.level += 1;
			s.clip = gen::kGuns[gun].clip;
			s.reserve = gen::kGuns[gun].reserve;
			Save::SetSlot(slot, s);
			logger::info("forge: {} -> level {}", gen::kGuns[gun].id, s.level);
			if (s.level >= gen::kForgeMax) {
				Hud::Banner(std::string(gen::kGuns[gun].label) + " ASCENDED  |||  " + std::string(gen::kGuns[gun].chargeName) + " now strikes twice", 5.0f);
			} else {
				Hud::Banner(std::string(gen::kGuns[gun].label) + " reforged  " + std::string(s.level, '|') + "  (x" + fmt::format("{:.1f}", UpgradeMult(s.level)) + " damage)", 4.0f);
			}
			return true;
		}

		bool OnButton(const RE::ButtonEvent* a_event)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || Intro::CrewScreenOpen() || ItemMenusOpen()) {
				return false;  // (the audit: G or the knife key in the inventory equipped from under it)
			}
			const auto dev = a_event->GetDevice();
			const auto code = a_event->GetIDCode();
			const bool down = a_event->IsDown(), up = !a_event->IsPressed();
			const bool pad = dev == RE::INPUT_DEVICE::kGamepad;
			if ((dev == RE::INPUT_DEVICE::kKeyboard && code == gen::kKey2_swap) || (pad && code == kPadRight && (GunOut() || Save::GetSlot(0).gun >= 0))) {
				if (down) {
					g_swapDown = Clock::now();
					g_swapHeld = true;
				} else if (up && g_swapHeld) {
					g_swapHeld = false;
					if (Clock::now() - g_swapDown > 400ms) {
						Holster();
					} else {
						Cycle();
					}
				}
				return true;
			}
			if (((dev == RE::INPUT_DEVICE::kKeyboard && code == gen::kKnife.key) || (pad && code == kPadDown && GunOut())) && down) {
				Knife();
				return true;
			}
			if (!GunOut()) {
				g_trigger = g_adsHeld = g_dualHeld = false;
				return false;
			}
			if (const int cg = CurrentGun(); IsNativeStaff(cg)) {  // a Falmer staff: Skyrim casts it; a long hold also looses its held attack
				if (IsFireButton(a_event)) {
					if (down) g_charge = 0;
					if (up && g_trigger) TryUnleash(true);
					g_trigger = !up;
				}
				return false;
			}
			if (const int cg = CurrentGun(); Spellcraft::Is(cg)) {  // the ten spells (Spellcraft.inl)
				if (IsFireButton(a_event)) {
					Spellcraft::Left(cg, down, up);
					return true;
				}
				if (IsAimButton(a_event)) {
					Spellcraft::Right(cg, down, up);
					g_adsHeld = false;
					return true;
				}
			}
			if (IsFireButton(a_event)) {
				// One or the other, on release: a full charge unleashes the signature move, anything shorter is the
				// weapon's normal shot (never both).
				const bool spell = IsSpell(CurrentGun());
				if (spell) {  // left mouse: a quick cast from the right hand (the charge is right mouse's)
					if (down && !g_dualHeld) {
						SendAttack(false, 1.0f, 0.0f);
					}
					if (up && g_trigger && !g_dualHeld) {
						SendAttack(false, 0.0f, 0.1f);
						TryFire(true);
					}
					g_trigger = !up;
					return true;
				}
				if (down) {
					g_charge = 0;
				}
				if (up && g_trigger) {
					const bool bow = gen::kGuns[CurrentGun()].kind == gen::kKindBow;
					const float cdBefore = g_cooldown;
					if (TryUnleash(true)) {
						g_swingWanted = IsStaff(CurrentGun());
					} else {
						if (!bow) {
							TryFire(true);
							g_swingWanted = IsStaff(CurrentGun()) && g_cooldown > cdBefore;  // a shot actually left
						} else if (g_charge >= 0.5f) {
							TryFire(true);  // a quick loose (Skyrim's own arrow flies; the hit and its splash are ours)
						}
					}
					g_charge = 0;
				}
				g_trigger = !up;
				return true;
			}
			if (IsAimButton(a_event)) {
				if (IsSpell(CurrentGun())) {  // right mouse: both hands up, the power gathers; let go to unleash it
					if (down) {
						g_charge = 0;
						SendAttack(false, 1.0f, 0.0f);
						SendAttack(true, 1.0f, 0.0f);
						g_dualHeld = true;
					} else if (up && g_dualHeld) {
						SendAttack(false, 0.0f, std::max(0.05f, g_charge));
						SendAttack(true, 0.0f, std::max(0.05f, g_charge));
						const int cur = CurrentGun();
						if (!TryUnleash(true)) {
							if (g_charge < 0.35f) {
								TryFire(true);  // a quick dual tap is still a shot
							} else {
								const int fz = cur >= 0 ? gen::kGunSounds[cur].fizzle : -1;
								Audio::Ui(fz >= 0 ? static_cast<gen::Sound>(fz) : gen::Sound::charge_fizzle);  // let go too early: this spell's power gutters out
							}
						} else {
							Audio::Ui(gen::Sound::release_power, 0.8f);  // a full release lands with weight under the spell's own unleash
						}
						Audio::LoopStop(Audio::kLoopCharge, 150);
						g_dualHeld = false;
						g_charge = 0;
					}
					g_adsHeld = false;
					return true;
				}
				g_adsHeld = !up;
				return true;
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
				const float  max = av->GetPermanentActorValue(RE::ActorValue::kMagicka), cur = av->GetActorValue(RE::ActorValue::kMagicka);
				if (last >= 0 && cur < last - 0.01f) {
					g_manaQuiet = 0;  // spent
				} else {
					g_manaQuiet += a_delta;
				}
				if (last >= 0 && cur > last + 0.001f && cur - last < 0.02f * max + 0.5f && g_manaQuiet < gen::kManaRegenDelay) {
					av->DamageActorValue(RE::ActorValue::kMagicka, cur - last);  // Skyrim's own regeneration: not yet
				}
				const float now = av->GetActorValue(RE::ActorValue::kMagicka);
				if (g_manaQuiet >= gen::kManaRegenDelay && now < max) {
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
			if (spQuiet >= 1.5f && sp < spMax) {
				av->RestoreActorValue(RE::ActorValue::kStamina, std::min(spMax - sp, spMax * 0.6f * a_delta));
			}
			lastSp = av->GetActorValue(RE::ActorValue::kStamina);
		}

		// ---- aim down sights: pull the first-person gun onto the view line so you look down its barrel ----
		struct Quat { float w, x, y, z; };
		Quat ToQuat(const RE::NiMatrix3& m)
		{
			const auto& e = m.entry;
			const float tr = e[0][0] + e[1][1] + e[2][2];
			Quat q{};
			if (tr > 0) {
				const float s = std::sqrt(tr + 1.0f) * 2;
				q = { 0.25f * s, (e[2][1] - e[1][2]) / s, (e[0][2] - e[2][0]) / s, (e[1][0] - e[0][1]) / s };
			} else if (e[0][0] > e[1][1] && e[0][0] > e[2][2]) {
				const float s = std::sqrt(1.0f + e[0][0] - e[1][1] - e[2][2]) * 2;
				q = { (e[2][1] - e[1][2]) / s, 0.25f * s, (e[0][1] + e[1][0]) / s, (e[0][2] + e[2][0]) / s };
			} else if (e[1][1] > e[2][2]) {
				const float s = std::sqrt(1.0f + e[1][1] - e[0][0] - e[2][2]) * 2;
				q = { (e[0][2] - e[2][0]) / s, (e[0][1] + e[1][0]) / s, 0.25f * s, (e[1][2] + e[2][1]) / s };
			} else {
				const float s = std::sqrt(1.0f + e[2][2] - e[0][0] - e[1][1]) * 2;
				q = { (e[1][0] - e[0][1]) / s, (e[0][2] + e[2][0]) / s, (e[1][2] + e[2][1]) / s, 0.25f * s };
			}
			return q;
		}
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
		Quat Nlerp(Quat a, Quat b, float t)
		{
			if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0) {
				b = { -b.w, -b.x, -b.y, -b.z };
			}
			return { a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
		}

		RE::NiPointer<RE::NiAVObject> g_adsModel;
		RE::NiTransform               g_adsRest;

		void AimViewmodel(RE::PlayerCharacter* a_player, float a_t)
		{
			auto* fp = a_player ? a_player->Get3D(true) : nullptr;
			auto* bone = fp ? fp->GetObjectByName("WEAPON") : nullptr;
			auto* boneNode = bone ? bone->AsNode() : nullptr;
			RE::NiAVObject* model = nullptr;
			if (boneNode) {
				for (auto& c : boneNode->GetChildren()) {
					if (c) {
						model = c.get();
						break;
					}
				}
			}
			if (model != g_adsModel.get()) {  // gun swapped: put the old one back, remember the new one's rest pose
				if (g_adsModel) {
					g_adsModel->local = g_adsRest;
				}
				g_adsModel.reset(model);
				if (model) {
					g_adsRest = model->local;
				}
			}
			if (!model) {
				return;
			}
			if (a_t <= 0.001f) {
				model->local = g_adsRest;
				return;
			}
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->cameraRoot) {
				return;
			}
			// Camera basis from the view angles (pitch +down): forward f, up u, right r.
			const float pitch = a_player->data.angle.x, yaw = a_player->data.angle.z;
			const RE::NiPoint3 f{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
			const RE::NiPoint3 u{ std::sin(yaw) * std::sin(pitch), std::cos(yaw) * std::sin(pitch), std::cos(pitch) };
			const RE::NiPoint3 r{ std::cos(yaw), -std::sin(yaw), 0 };
			// Wand models: tip toward +Y, up +Z (point it where you look).
			RE::NiMatrix3 want;
			for (int i = 0; i < 3; ++i) {
				const float rv[3] = { r.x, r.y, r.z }, fv[3] = { f.x, f.y, f.z }, uv[3] = { u.x, u.y, u.z };
				want.entry[i][0] = rv[i], want.entry[i][1] = fv[i], want.entry[i][2] = uv[i];
			}
			const auto& pw = bone->world;
			const float  scale = pw.scale * g_adsRest.scale;
			const RE::NiPoint3 eye = cam->cameraRoot->world.translate;
			const RE::NiPoint3 at = eye + f * gen::kAdsEyeDistance - u * (gen::kAdsSightHeight * scale);
			const RE::NiMatrix3 inv = pw.rotate.Transpose();
			RE::NiTransform aim;
			aim.rotate = inv * want;
			aim.translate = (inv * (at - pw.translate)) / std::max(0.001f, pw.scale);
			aim.scale = g_adsRest.scale;
			// Ease in/out so the raise looks like a motion, not a snap.
			const float t = a_t * a_t * (3 - 2 * a_t);
			model->local.rotate = FromQuat(Nlerp(ToQuat(g_adsRest.rotate), ToQuat(aim.rotate), t));
			model->local.translate = g_adsRest.translate + (aim.translate - g_adsRest.translate) * t;
			model->local.scale = g_adsRest.scale;
		}

		void FangHit(RE::Actor* a_target, bool a_power) { Props::FangHit(a_target, a_power); }
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


		// a tome you know but carry in no slot (the test kit's), equipped from the Magic menu: it takes the slot in your
		// hand, the one there before stays known
		void SlotUnslotted()
		{
			const int gun = CurrentGun();
			if (gun < 0 || SlotOf(gun) >= 0 || gen::kGuns[gun].kind == gen::kKindBow || (!Forms::GunSpell(gun) && !IsStaffWonder(gun))) {
				return;
			}
			int slot = -1;
			for (int s = 0; s < SlotCount() && slot < 0; ++s) {
				if (Save::GetSlot(s).gun < 0) slot = s;
			}
			if (slot < 0) slot = Save::ActiveSlot() % SlotCount();
			// (Noah: the test kit's staffs are tier 3)
			Save::SetSlot(slot, { gun, gen::kGuns[gun].clip, gen::kGuns[gun].reserve, IsStaffWonder(gun) && TestKitOn() ? gen::kForgeMax : 0 });
			Save::SetActiveSlot(slot);
			logger::info("gun: {} taken up from the Magic menu / pack -> slot {}", gen::kGuns[gun].id, slot);
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
			SlotUnslotted();
			TickStorm(a_delta);
			Props::Tick(a_delta, GunOut() ? CurrentGun() : -1);
			Props::TickWorld(a_delta);
			Props::TickFang(a_delta);
			TickStaffs(a_delta);
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
				for (const auto& gg : gen::kGuns) {  // a charge aura saved mid-charge never sticks
					if (auto* m = gg.chargeAura >= 0 ? Forms::Spell(gg.chargeAura) : nullptr) {
						if (auto* sp = m->As<RE::SpellItem>()) {
							RE::PlayerCharacter::GetSingleton()->RemoveSpell(sp);
						}
					}
				}
			}
			MigrateRetired();
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
			g_cooldown = std::max(0.0f, g_cooldown - a_delta);
			g_knifeCd = std::max(0.0f, g_knifeCd - a_delta);
			TickDots(a_delta);
			TickMana(a_delta);
			static float sync = 0;
			if ((sync -= a_delta) <= 0) {
				sync = 1.0f;
				SyncSlots();
				if (GunOut()) {
					TopUpArrows(CurrentGun(), false);
				}

			}
			const bool out = GunOut();
			const int  gun = CurrentGun();
			g_chargeCd = std::max(0.0f, g_chargeCd - a_delta);
			// spells charge with right mouse (both hands), everything else by holding fire
			const bool held = IsSpell(gun) ? g_dualHeld : g_trigger;
			g_charge = out && held ? g_charge + a_delta / Perks::ChargeMult() : 0.0f;
			TickPose(a_delta);
			TickSwing(a_delta);
			TickEcho(a_delta);
			TickBlasts(a_delta);
			TickWells(a_delta);
			TickTimeAndPrisms(a_delta);
			TickMarks(a_delta);
			Spellcraft::Tick(a_delta);
			BowsImpl::Tick(a_delta);
			if (out) {
				const auto& g = gen::kGuns[gun];
				if (g_burstLeft > 0 && (g_burstTimer -= a_delta) <= 0) {
					--g_burstLeft;
					TryFire(false);
				} else if (g_trigger && g.fire == gen::Fire::Auto) {
					TryFire(false);
				}
			} else {
				g_trigger = g_adsHeld = g_dualHeld = false;
			}
			// Aim down sights: blend the field of view.
			const float target = out && g_adsHeld ? 1.0f : 0.0f;
			const float step = a_delta / std::max(0.01f, gen::kAdsSeconds);
			g_ads = target > g_ads ? std::min(target, g_ads + step) : std::max(target, g_ads - step);
			if (auto* cam = RE::PlayerCamera::GetSingleton()) {
				// Wider base view (sheet: movement.world_fov / viewmodel_fov), narrowing while aiming.
				auto& rd = cam->GetRuntimeData2();
				// the field of view is the player's own (Settings: first person and third person each), narrowing while aiming
				const float base = cam->IsInFirstPerson() ? Settings::FovFirst() : Settings::FovThird();
				rd.worldFOV = base + (gen::kAdsFov - base) * g_ads + g_fovKick;
				// the hands follow the same slider: a wider view draws them smaller and further off, a narrower one closer
				const float vm = std::clamp(gen::kViewmodelFov + (Settings::FovFirst() - 100.0f) * 0.6f, 55.0f, 110.0f);
				rd.firstPersonFOV = vm + (gen::kAdsViewmodelFov - vm) * g_ads;
			}
			// (Noah: "an ADS sensitivity slider") while you aim - down a staff's sights, or a drawn bow - the look turns at
			// Skyrim's own sensitivity times ours; let go (or open a menu) and Skyrim's own comes back exactly as it was
			{
				static float skyrimSens = -1.0f;  // Skyrim's own while ours is applied (-1: not applied)
				auto*       pl = RE::PlayerCharacter::GetSingleton();
				float       aim = g_ads;
				if (pl) {
					const auto as = pl->AsActorState()->GetAttackState();
					if (as == RE::ATTACK_STATE_ENUM::kBowDraw || as == RE::ATTACK_STATE_ENUM::kBowAttached || as == RE::ATTACK_STATE_ENUM::kBowDrawn) aim = 1.0f;
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
			AimViewmodel(RE::PlayerCharacter::GetSingleton(), 0.0f);  // staffs and bows stay in Skyrim's own pose; aiming is the zoom
			// (staffs never spin: the twirl was a wand thing)
			const auto now = Clock::now();
			std::erase_if(g_hits, [&](const auto& kv) { return now - kv.second.first > 2s; });

			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.gunOut = out;
			h.ads = g_ads;
			h.reloading = false;
			if (out) {
				const auto s = Save::GetSlot(SlotOf(gun));
				h.gunName = std::string(gen::kGuns[gun].label);
				h.gunLevel = SlotOf(gun) >= 0 ? Save::GetSlot(SlotOf(gun)).level : 0;
				h.clip = s.clip;
				h.reserve = s.reserve;
				h.gunKind = gen::kGuns[gun].kind;
				h.manaCost = gen::kGuns[gun].mana * Perks::ManaMult() * Difficulty::Mana();
				{
					std::string ab(gen::kGuns[gun].abilityText);
					if (!ab.empty() && ab.back() == '.') {
						ab.pop_back();
					}
					h.gunElement = ab + "   \xC2\xB7   Hold: " + std::string(gen::kGuns[gun].chargeName);
				}
				h.gunColor = gen::kGuns[gun].color;
				const auto& gg = gen::kGuns[gun];
				h.charge = g_charge > 0.2f ? std::min(1.0f, g_charge / gg.chargeSeconds) : 0.0f;
				h.chargeCd = gg.chargeCooldown > 0 ? g_chargeCd / gg.chargeCooldown : 0.0f;
				h.chargeName = std::string(gg.chargeName) + (ForgeLevel() >= gen::kForgeMax ? " ASCENDANT" : "");
			}
			h.points = Points::Get();
			h.perks = Save::PerkBits();
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()) && i < 32; ++i) {
				h.perkStacks[i] = Save::PerkStacks(i);
			}
			h.slotCount = SlotCount();
			for (int i = 0; i < 3; ++i) {
				const auto sl = i < h.slotCount ? Save::GetSlot(i) : Save::Slot{};
				h.slotGun[i] = i < h.slotCount && sl.gun >= 0 && sl.gun < static_cast<int>(gen::kGuns.size()) ? sl.gun : -1;
				h.slotLevel[i] = sl.level;
			}
			h.gunIndex = out ? gun : -1;
			h.handSpell = Arcana::Hands();  // the Coffer spells in hand (the spell dock)
			for (int i = 0; i < 2; ++i) {  // their real cost: the halved price, the reforge and your own skill and perks
				auto* sp = Arcana::Spell(h.handSpell[i]);
				h.handCost[i] = sp ? static_cast<int>(std::lround(sp->CalculateMagickaCost(RE::PlayerCharacter::GetSingleton()))) : 0;
			}
			h.activeSlot = out && SlotOf(gun) >= 0 ? SlotOf(gun) : (h.slotCount > 0 ? Save::ActiveSlot() % h.slotCount : -1);
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

	namespace Bows
	{
		int              Count() { return BowsImpl::kN; }
		std::string_view Name(int a_i) { return a_i >= 0 && a_i < BowsImpl::kN ? gen::kBows[a_i].label : ""; }
		std::uint32_t    Color(int a_i) { return a_i >= 0 && a_i < BowsImpl::kN ? gen::kBows[a_i].color : 0xFFFFFF; }
		std::string_view Element(int a_i) { return a_i >= 0 && a_i < BowsImpl::kN ? gen::kBows[a_i].element : ""; }
		int              Level(int a_i) { return a_i >= 0 && a_i < BowsImpl::kN ? BowsImpl::g_level[a_i] : 0; }
		std::string_view TierName(int a_i, int a_tier) { return a_i >= 0 && a_i < BowsImpl::kN ? BowsImpl::kTierNames[static_cast<int>(BowsImpl::KindOf(a_i))][std::clamp(a_tier, 0, 3)] : ""; }
		int              Held() { return BowsImpl::Held(); }
		int              Carried() { return BowsImpl::Carried(); }
		int              IndexOf(const RE::TESForm* a_form) { return BowsImpl::Of(a_form); }
		void             Give(int a_i) { BowsImpl::Give(a_i); }
		// a bow you don't carry, of the tiers the altars allow (the second tier once an altar is awake)
		int Roll()
		{
			int freed = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				freed += Curse::Freed(t) ? 1 : 0;
			}
			const int        maxTier = freed >= 1 ? 2 : 1;
			const int        have = BowsImpl::Carried();
			std::vector<int> pool;
			for (int i = 0; i < BowsImpl::kN; ++i) {
				if (i != have && gen::kBows[i].tier <= maxTier && BowsImpl::Weap(i)) {
					pool.push_back(i);
				}
			}
			return pool.empty() ? -1 : pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(g_rng)];
		}
		std::vector<std::uint8_t> Levels() { return { BowsImpl::g_level.begin(), BowsImpl::g_level.end() }; }
		int Restock() { return BowsImpl::Restock(); }
		int              SpecialKind() { return BowsImpl::g_specialKind; }
		int              SpecialLeft() { return BowsImpl::g_specialLeft; }
		std::string_view SpecialName(int a_k) { return a_k >= 0 && a_k < 4 ? BowsImpl::kSpecialName[a_k] : ""; }
		int              SpecialCost(int a_k) { return a_k >= 0 && a_k < 4 ? BowsImpl::kSpecialCost[a_k] / (PowerUps::FireSale() ? 2 : 1) : 0; }
		bool BuySpecial(int a_k)
		{
			if (a_k < 0 || a_k >= 4) return false;
			const int cost = SpecialCost(a_k);
			if (!Points::Spend(cost)) {
				Hud::Banner(fmt::format("{} ammo costs {} Cursed Essence", BowsImpl::kSpecialName[a_k], cost), 2.0f);
				return false;
			}
			if (BowsImpl::g_specialKind == a_k) BowsImpl::g_specialLeft += 10;
			else BowsImpl::g_specialKind = a_k, BowsImpl::g_specialLeft = 10;
			Forms::Play(gen::Sound::reload_mag);
			Hud::Banner(fmt::format("{} ammo  x{}  (-{}) - your next shots carry it", BowsImpl::kSpecialName[a_k], BowsImpl::g_specialLeft, cost), 3.0f);
			return true;
		}
		void CrossbowStrike(int a_elem, int a_tier, RE::Actor* a_hit, float a_damage) { BowsImpl::CrossbowStrike(a_elem, a_tier, a_hit, a_damage); }
		bool IsStrikeSpell(const RE::MagicItem* a_item)
		{
			if (!a_item) return false;
			static std::vector<const RE::MagicItem*> own = [] {
				std::vector<const RE::MagicItem*> v;
				for (int k = 0; k < 6; ++k) for (int tier = 2; tier <= 3; ++tier) if (auto* sp = BowsImpl::TierSpell(static_cast<BowsImpl::Kind>(k), tier)) v.push_back(sp);
				return v;
			}();
			for (auto* o : own) {
				if (o && o == a_item) return true;
			}
			return false;
		}
		void SetLevels(const std::vector<std::uint8_t>& a_levels)
		{
			for (int i = 0; i < BowsImpl::kN; ++i) {
				BowsImpl::g_level[i] = i < static_cast<int>(a_levels.size()) ? static_cast<std::uint8_t>(std::min<int>(a_levels[i], gen::kForgeMax)) : 0;
			}
			BowsImpl::ApplyAllSpeeds();
		}
		void SetLevel(int a_i, int a_level)
		{
			if (a_i < 0 || a_i >= BowsImpl::kN) return;
			BowsImpl::g_level[a_i] = static_cast<std::uint8_t>(std::clamp(a_level, 0, 3));
			BowsImpl::ApplyAllSpeeds();
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
	float Dealt() { static constexpr float k[6] = { 1.4f, 1.2f, 1.0f, 0.9f, 0.8f, 0.7f }; return k[Level()]; }
	float Taken() { static constexpr float k[6] = { 0.4f, 0.65f, 1.0f, 1.4f, 1.9f, 2.6f }; return Settings::FollowDifficulty() ? k[Level()] : 1.0f; }  // (the night's blows: the only place the difficulty touches them)
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
		static constexpr float k[6] = { 0.7f, 0.85f, 1.0f, 1.4f, 1.9f, 2.6f };
		return k[Level()] * (1.0f + 0.3f * static_cast<float>(std::min(Freed(), 10)));
	}
	// and how much harder they hit: Skyrim's own damage-to-you settings (each difficulty's) raised with your progress
	void TickFoeDamage()
	{
		static constexpr const char* kNames[6] = { "fDiffMultHPToPCVE", "fDiffMultHPToPCE", "fDiffMultHPToPCN", "fDiffMultHPToPCH", "fDiffMultHPToPCVH", "fDiffMultHPToPCL" };
		static constexpr float       kExtra[6] = { 0.8f, 0.9f, 1.0f, 1.15f, 1.3f, 1.5f };  // (on Skyrim's own 0.5x .. 3x)
		static float orig[6] = { -1, -1, -1, -1, -1, -1 };
		static float applied = -1;
		auto* gs = RE::GameSettingCollection::GetSingleton();
		if (!gs) return;
		const float prog = 1.0f + 0.06f * static_cast<float>(std::min(Freed(), 12));
		if (std::abs(prog - applied) < 1e-3f) return;
		for (int i = 0; i < 6; ++i) {
			auto* st = gs->GetSetting(kNames[i]);
			if (!st) continue;
			if (orig[i] < 0) orig[i] = st->GetFloat();
			st->data.f = orig[i] * kExtra[i] * prog;
		}
		if (applied < 0 || std::abs(prog - applied) > 1e-3f) logger::info("difficulty: Skyrim's foes hit x{:.2f} (on top of the difficulty's own), and hold x{:.2f} against your class's weapons", prog, FoeToughness());
		applied = prog;
	}
}
