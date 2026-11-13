#include "BO3Z.h"

// (Noah: "something like the mystery box from call of duty zombies" - the Tablet of Mysteries; docs/TABLET_OF_MYSTERIES.md)
// (Noah, 2026-11-11: "accessible from night 1 ... only appears at night time ... a spawning in and leaving animation ... a beacon
// of light that shoots high"; "it should start at riverwood and only moves when the dragon priest mask is rolled"; "the base of
// the tablet model should be visible at all times where the tablet spawns are set") every city's tablet spot keeps its plinth;
// the tablet itself stands in one city at a time - Riverwood first - and is there only from the night's first wave to dawn:
// its beacon (Skyrim's wayshrine beam, in the tablet's amber) lights over the spot and the tablet comes down it and lands;
// at dawn it is drawn back up. The Dragon Priest's mask (once the rolls are up) rises from the pool, laughs, and the tablet
// goes up the beam and comes down at once in another city (named as it goes). An empty plinth, looked at or used, says where
// the tablet is. It is animated here, on its own model (tools/tablet.py), by its nodes' names:
//   idle  - the slab bobs and sways a little; its glyph rings drift; (in the model) the glyphs breathe, the pool swirls
//   roll  - the rings grind round unevenly, stalling and lurching; the slab shudders; the eight gems light one by one;
//           sparks spit from the cracks; the weapons' ghosts flicker in the pool; a battered win: gold light and a bell
// A roll (1,500, +500 a spin or a repair, back at dawn) gives a donated weapon (sheet: tablet): it rises out of the pool
// and waits 12 s to be taken; taken, it is equipped (the hands' old things back in the pack) and worn down by use - its
// wear in its name and on the HUD - mended at the tablet for a spin's price, lost if you fall. After a random 4-8 rolls
// the skull comes instead: the essence back, and the tablet moves to another freed city.
// Saved with the altars' rolls (Shops::RollState): the city, the rolls since it last moved, tonight's price steps, the
// weapon you carry and its uses.
namespace bo3z::Tablet
{
	namespace
	{
		std::mt19937 g_rng{ std::random_device{}() };
		int   g_town = -1;          // where it stands (-1: no city freed yet)
		int   g_rolls = 0;          // rolls since it last moved
		int   g_moveAfter = 6;      // (4..8) after this many the skull can come
		int   g_steps = 0, g_night = -1;  // tonight's spins and repairs (the price's steps)
		enum class Phase { Idle, Rolling, Offering, Mask } g_phase = Phase::Idle;
		// (its nights) down in its city now; coming down the beam / going up it (seconds in, -1 not); the city it goes on to
		bool  g_present = false;
		float g_arriveT = -1.0f, g_leaveT = -1.0f;
		int   g_moveTo = -1;
		RE::ObjectRefHandle g_beacon;   // the beam over it
		int   g_beaconTown = -1;
		float g_sweep = 0.0f;           // (the other cities' plinths: their tablets kept hidden)
		float g_lookCool = 0.0f;
		RE::FormID g_lookedAt = 0;
		constexpr float kBeamLead = 0.6f, kDropTime = 1.3f, kRiseTime = 1.6f, kDropHeight = 2600.0f, kMaskTime = 2.8f;
		float g_t = 0.0f;           // into the phase
		float g_clock = 0.0f;       // its own time (the idle's sway)
		float g_ringO = 0.0f, g_ringI = 0.0f;      // the rings' turn (radians)
		float g_stallO = 0.0f, g_stallI = 0.0f;    // (grinding: a ring's moment of sticking)
		bool  g_gold = false;       // this roll is a battered (stronger) win
		bool  g_bell = false;
		RE::ObjectRefHandle g_ref;  // the one being animated
		int   g_prize = -1;         // the weapon offered (kTabletWeapons), -1 none / the skull
		bool  g_skull = false;
		RE::ObjectRefHandle g_show; // its model, rising out of the pool
		bool  g_showGlow = false;   // (its rarity glow laid on once its model is up)
		// (Noah: "the roll animation should cycle through all the weapons with a mini model of the weapon visibile in the
		// center of the tablet with green being a common weapon and gold being a legendary") the reel
		RE::ObjectRefHandle g_reel;
		int   g_reelAt = -1;        // the weapon it shows (kTabletWeapons)
		int   g_reelStep = 0;
		float g_reelNext = 0.0f;
		bool  g_reelGlow = false;
		std::vector<int> g_reelOrder;
		int   g_held = -1;          // the donated weapon you carry (kTabletWeapons), -1 none
		int   g_uses = 0;           // its uses left
		int   g_paid = 0;           // the last spin's price (the skull gives it back)

		// the model's measures (tools/tablet.py): the pool's centre on the face (x 0, z Z0 + 42)
		constexpr float kPoolZ = 116.0f;
		// (Noah: "the weapon rolled should dissapear after 10 seconds if not picked up")
		constexpr float kRollTime = 3.6f, kOfferTime = 10.0f;
		// (Noah: "Holding the action key on the tablet should start the roll and holding action on the weapon rolled should
		// pick it up") how long Activate is held; a hold done must be let go before the next
		constexpr float kHoldTime = 0.6f;
		constexpr float kReach = 145.0f;   // (2 m, from you to its plinth)
		constexpr float kFront = 48.0f;    // (Noah: "have the rolling animation a little bit in front of the tablet so nothing pokes through")
		RE::ObjectRefHandle g_lamp;        // (Noah: "It needs more lighting so you can see what you roll") a white light before its face
		float g_holdT = 0.0f;
		bool  g_holdLatched = false;
		std::chrono::steady_clock::time_point g_holdDoneAt{};

		// a city with a tablet spot for it (not the one it leaves) - freed or not
		bool HasSpot(int a_town)
		{
			for (const auto& loc : gen::kLocations) if (loc.town == a_town && gen::kObjects[loc.object].kind == 7) return true;
			return false;
		}
		int PickTown(int a_not)
		{
			std::vector<int> ok;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				if (HasSpot(t) && t != a_not) ok.push_back(t);
			}
			if (ok.empty()) return a_not;
			return ok[std::uniform_int_distribution<int>(0, static_cast<int>(ok.size()) - 1)(g_rng)];
		}
		void Night()
		{
			const int night = Save::Get(gen::kAV_round);
			if (night != g_night) g_night = night, g_steps = 0;
		}

		RE::NiAVObject* Find(RE::NiAVObject* a_root, const char* a_name) { return a_root ? a_root->GetObjectByName(a_name) : nullptr; }
		void Show(RE::NiAVObject* a_root, const char* a_name, bool a_on)
		{
			if (auto* o = Find(a_root, a_name); o && o->GetAppCulled() == a_on) o->SetAppCulled(!a_on);
		}
		// a ring's node turned a_ang about the pool's centre, in the face's plane (about the Y axis through x 0, z kPoolZ)
		void TurnRing(RE::NiAVObject* a_node, float a_ang)
		{
			if (!a_node) return;
			const float c = std::cos(a_ang), s = std::sin(a_ang);
			RE::NiMatrix3 r;
			r.entry[0][0] = c, r.entry[0][1] = 0, r.entry[0][2] = s;
			r.entry[1][0] = 0, r.entry[1][1] = 1, r.entry[1][2] = 0;
			r.entry[2][0] = -s, r.entry[2][1] = 0, r.entry[2][2] = c;
			a_node->local.rotate = r;
			// (turned about the pool's centre, not the model's origin: the centre stays where it was)
			a_node->local.translate = RE::NiPoint3{ -s * kPoolZ, 0.0f, kPoolZ - c * kPoolZ };
		}

		RE::TESForm* FormOf(int a_w) { return a_w >= 0 && a_w < static_cast<int>(gen::kTabletWeapons.size()) ? Forms::Ours(gen::kTabletWeapons[a_w].form) : nullptr; }
		// its name in the pack with its wear: "Gravebell Mace (62%)"
		RE::TESObjectWEAP* LeftOf(int a_w);
		std::unordered_map<RE::FormID, std::string> g_leftWas;
		std::unordered_map<RE::FormID, RE::EnchantmentItem*> g_leftEnchWas;  // (Animated Armoury's left claws: their own names, put back when ours go)
		void Rename(int a_w, int a_uses)
		{
			auto* f = FormOf(a_w);
			if (!f) return;
			const auto& d = gen::kTabletWeapons[a_w];
			const int   pct = std::clamp(static_cast<int>(std::ceil(100.0f * a_uses / std::max(1, d.uses))), 0, 100);
			const std::string nm = fmt::format("{} ({}%)", d.label, pct);
			if (auto* w = f->As<RE::TESObjectWEAP>()) w->fullName = nm;
			else if (auto* sp = f->As<RE::SpellItem>()) sp->fullName = nm;
			// (Noah: "It should be Cinderclaws Left, not Daedric Claws Left") the claws' other hand under the same name
			if (auto* l = a_w == g_held ? LeftOf(a_w) : nullptr) {
				if (!g_leftWas.contains(l->GetFormID())) g_leftWas[l->GetFormID()] = l->fullName.c_str();
				l->fullName = fmt::format("{} Left", d.label);
				// (its colour from Visual Animated Enchants, as the right claw wears it: src/Classes.cpp VaeWeapons)
				if (!g_leftEnchWas.contains(l->GetFormID())) g_leftEnchWas[l->GetFormID()] = l->formEnchanting;
				if (auto* rw = f ? f->As<RE::TESObjectWEAP>() : nullptr; rw && rw->formEnchanting) l->formEnchanting = rw->formEnchanting, l->amountofEnchantment = 0;
			}
		}
		// the donated weapon taken off you and out of your pack (and the bows' arrows)
		// (Noah: "real hand wraps or elemental brass knuckles") the fists' gloves, worn in the hands' slot while they are held
		// (tools/tabletgloves.py); whatever gloves you wore before put back on when they go
		RE::FormID g_prevGloves = 0;
		RE::TESObjectARMO* GlovesOf(int a_w)
		{
			if (a_w < 0 || a_w >= static_cast<int>(gen::kTabletWeapons.size()) || !gen::kTabletWeapons[a_w].gloves) return nullptr;
			auto* f = Forms::Ours(gen::kTabletWeapons[a_w].gloves);
			return f ? f->As<RE::TESObjectARMO>() : nullptr;
		}
		// (the claws: Animated Armoury's own left-hand claw worn in the other hand with ours)
		RE::TESObjectWEAP* LeftOf(int a_w)
		{
			if (a_w < 0 || a_w >= static_cast<int>(gen::kTabletWeapons.size()) || !gen::kTabletWeapons[a_w].left) return nullptr;
			auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(gen::kTabletWeapons[a_w].left, gen::kTabletWeapons[a_w].lookPlugin);
			return f ? f->As<RE::TESObjectWEAP>() : nullptr;
		}
		void GiveLeft(RE::PlayerCharacter* p, int a_w)
		{
			auto* l = LeftOf(a_w);
			auto* em = RE::ActorEquipManager::GetSingleton();
			if (!l || !p || !em) return;
			if (p->GetItemCount(l) < 1) Forms::Give(p, l, 1);
			em->EquipObject(p, l, nullptr, 1, RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F43), false, false, false, true);
		}
		void TakeAway(int a_w)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* f = FormOf(a_w);
			if (!p || !f) return;
			if (auto* l = LeftOf(a_w); l && p->GetItemCount(l) > 0) p->RemoveItem(l, p->GetItemCount(l), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			if (auto* l = LeftOf(a_w)) if (auto it = g_leftWas.find(l->GetFormID()); it != g_leftWas.end()) l->fullName = it->second;
			if (auto* l = LeftOf(a_w)) if (auto it = g_leftEnchWas.find(l->GetFormID()); it != g_leftEnchWas.end()) l->formEnchanting = it->second;
			if (auto* gl = GlovesOf(a_w); gl && p->GetItemCount(gl) > 0) {
				p->RemoveItem(gl, p->GetItemCount(gl), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				if (auto* prev = g_prevGloves ? RE::TESForm::LookupByID<RE::TESObjectARMO>(g_prevGloves) : nullptr; prev && p->GetItemCount(prev) > 0) {
					if (auto* em = RE::ActorEquipManager::GetSingleton()) em->EquipObject(p, prev, nullptr, 1, nullptr, false, false, false, true);
				}
				g_prevGloves = 0;
			}
			if (auto* sp = f->As<RE::SpellItem>()) {
				if (p->HasSpell(sp)) p->RemoveSpell(sp);
			} else if (auto* b = f->As<RE::TESBoundObject>(); b && p->GetItemCount(b) > 0) {
				p->RemoveItem(b, p->GetItemCount(b), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			if (gen::kTabletWeapons[a_w].type == 4) {
				if (auto* ar = Forms::Ours(gen::kTabletArrow) ? Forms::Ours(gen::kTabletArrow)->As<RE::TESBoundObject>() : nullptr; ar && p->GetItemCount(ar) > 0) {
					p->RemoveItem(ar, p->GetItemCount(ar), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
			}
		}
		// (Noah: "equip on pickup and un equip any current weapons held ... also in the players inventory")
		void Give(int a_w)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* f = FormOf(a_w);
			auto* em = RE::ActorEquipManager::GetSingleton();
			if (!p || !f || !em) return;
			if (g_held >= 0) TakeAway(g_held);
			const auto& d = gen::kTabletWeapons[a_w];
			g_held = a_w, g_uses = d.uses;
			Rename(a_w, g_uses);
			auto* right = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F42);
			for (const bool left : { false, true }) {   // whatever the hands held goes back to the pack
				if (auto* o = p->GetEquippedObject(left)) {
					// (a spell in hand is simply replaced by what is equipped over it)
					if (auto* b = o->As<RE::TESBoundObject>()) em->UnequipObject(p, b, nullptr, 1, nullptr, false, true, false, true);
				}
			}
			if (auto* sp = f->As<RE::SpellItem>()) {
				p->AddSpell(sp);
				em->EquipSpell(p, sp, right);
			} else if (auto* b = f->As<RE::TESBoundObject>()) {
				Forms::Give(p, b, 1);
				em->EquipObject(p, b, nullptr, 1, d.type == 4 || d.type == 3 || d.type == 6 || d.type == 8 ? nullptr : right, false, false, false, true);
				GiveLeft(p, a_w);
				if (auto* gl = GlovesOf(a_w)) {   // (the fists: their gloves on)
					if (auto* worn = p->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kHands); worn && worn != gl) g_prevGloves = worn->GetFormID();
					Forms::Give(p, gl, 1);
					em->EquipObject(p, gl, nullptr, 1, nullptr, false, false, false, true);
				}
				if (d.type == 4) {   // a bow: its uses are its arrows
					if (auto* ar = Forms::Ours(gen::kTabletArrow) ? Forms::Ours(gen::kTabletArrow)->As<RE::TESBoundObject>() : nullptr) {
						p->AddObjectToContainer(ar, nullptr, d.uses, nullptr);
						em->EquipObject(p, ar, nullptr, d.uses, nullptr, false, false, false, true);
					}
				}
			}
			p->DrawWeaponMagicHands(true);
			logger::info("tablet: {} taken ({} uses)", d.id, d.uses);
		}
		// (Noah: "an actual spell book in hand") the Codex open on your right palm while it is the spell in that hand, in the body
		// you see and the other (tools/tabletweapons.py codex_r.nif, made in the hand bone's own frame)
		RE::NiPointer<RE::NiAVObject> g_book[2];
		void TickBook(RE::PlayerCharacter* a_p)
		{
			bool on = false;
			if (g_held >= 0 && gen::kTabletWeapons[g_held].type == 7) {
				auto* f = FormOf(g_held);
				on = f && a_p->GetEquippedObject(false) == f;
			}
			for (int b = 0; b < 2; ++b) {
				auto* root = a_p->Get3D(b == 1);
				auto* hand = on && root ? root->GetObjectByName("NPC R Hand [RHnd]") : nullptr;
				auto* node = hand ? hand->AsNode() : nullptr;
				auto& bk = g_book[b];
				if (!node) {
					if (bk && bk->parent) bk->parent->DetachChild2(bk.get());
					continue;
				}
				if (!bk) bk = Gun::LoadModel("CurseOfAlduin\\tablet\\codex_r.nif");
				if (bk && bk->parent != node) {
					if (bk->parent) bk->parent->DetachChild2(bk.get());
					node->AttachChild(bk.get(), true);
				}
			}
		}
		void Break()
		{
			if (g_held < 0) return;
			const auto& d = gen::kTabletWeapons[g_held];
			TakeAway(g_held);
			if (auto* p = RE::PlayerCharacter::GetSingleton()) Forms::PlayAt(gen::Sound::boss_land, p);
			Hud::Banner(std::string(d.label) + (d.type == 7 ? " burns to ash in your hand" : d.type == 4 ? " splinters - its last arrow loosed" : " falls apart in your hands"), 3.5f);
			logger::info("tablet: {} broke", d.id);
			g_held = -1, g_uses = 0;
		}
		// a roll's result: the skull (once its rolls are up), else a weapon - 15% one of the battered, else one of the worn -
		// never the one you already carry
		int Pick()
		{
			// (Noah: "Tablet worn weapons from night 4, battered from night 8") before night 8 only the worn
			const bool battered = (Gun::TestKitOn() || Save::Get(gen::kAV_round) + 1 >= 8) && std::uniform_int_distribution<int>(0, 99)(g_rng) < 15;
			std::vector<int> pool;
			for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) {
				if (gen::kTabletWeapons[i].battered == battered && i != g_held) pool.push_back(i);
			}
			if (pool.empty()) return 0;
			return pool[std::uniform_int_distribution<int>(0, static_cast<int>(pool.size()) - 1)(g_rng)];
		}
		void ClearReel()
		{
			if (auto r = g_reel.get()) r->Disable(), r->SetDelete(true);
			g_reel = {}, g_reelAt = -1, g_reelGlow = false;
		}
		void ClearShow()
		{
			if (auto r = g_show.get()) r->Disable(), r->SetDelete(true);
			g_show = {}, g_showGlow = false;
			ClearReel();
		}
		// a weapon's rarity glow: worn green, battered gold
		void Glow(RE::TESObjectREFR* a_r, int a_w, bool& a_done)
		{
			if (a_done || !a_r || a_w < 0 || !a_r->Is3DLoaded()) return;
			auto* sh = Forms::Ours(gen::kTabletWeapons[a_w].battered ? gen::kTabletGold : gen::kTabletGreen);
			if (auto* e = sh ? sh->As<RE::TESEffectShader>() : nullptr) a_r->ApplyEffectShader(e, -1.0f);
			a_done = true;
		}
		// the reel: every weapon in turn, a little one turning in the pool in its rarity's glow - quick at first, slowing,
		// and landing on the one won (the mask's roll: it runs out and the mask rises instead)
		void TickReel(RE::TESObjectREFR* a_tab, RE::NiAVObject* a_body, float a_k, float a_delta)
		{
			if (g_phase != Phase::Rolling || a_k >= 0.97f) { ClearReel(); return; }
			if (g_reelOrder.empty()) {
				for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) g_reelOrder.push_back(i);
				std::shuffle(g_reelOrder.begin(), g_reelOrder.end(), g_rng);
			}
			g_reelNext -= a_delta;
			int want = g_reelAt;
			if (g_reelAt < 0 || g_reelNext <= 0.0f) {
				// (the last turns: the won weapon itself, so the reel lands on it)
				const bool last = a_k > 0.78f && g_prize >= 0;
				want = last ? g_prize : g_reelOrder[(g_reelStep++) % g_reelOrder.size()];
				if (!last && want == g_reelAt) want = g_reelOrder[(g_reelStep++) % g_reelOrder.size()];
				g_reelNext = last ? 99.0f : 0.09f + 0.32f * a_k * a_k;
			}
			if (want != g_reelAt) {
				ClearReel();
				auto* base = Forms::Ours(gen::kTabletWeapons[want].show) ? Forms::Ours(gen::kTabletWeapons[want].show)->As<RE::TESBoundObject>() : nullptr;
				if (auto ref = base ? a_tab->PlaceObjectAtMe(base, false) : nullptr) {
					ref->GetReferenceRuntimeData().refScale = 32;   // (a little one: a third of its size)
					g_reel = ref->CreateRefHandle(), g_reelAt = want;
				}
			}
			auto r = g_reel.get();
			if (!r) return;
			// (Noah: "the larger weapons ... poke through the tablet") before its face, stood on end (a weapon's model lies along
			// its length: tipped up, it turns about itself and never sweeps into the slab)
			r->SetPosition(a_body->world * RE::NiPoint3{ 0.0f, -kFront, kPoolZ - 22.0f });
			r->SetAngle({ -RE::NI_HALF_PI, 0.0f, a_tab->GetAngleZ() + g_clock * 2.4f });
			Glow(r.get(), g_reelAt, g_reelGlow);
		}
		// the won weapon's model rising out of the pool and turning there (placed in the world before the tablet's face)
		void PoseShow(RE::TESObjectREFR* a_tab, float a_rise)
		{
			auto* root = a_tab ? a_tab->Get3D() : nullptr;
			auto* body = root ? root->GetObjectByName("TabletBody") : nullptr;
			if (!body) return;
			// (Noah: "we can have the weapon standing upright in front of the tablet to be claimed") a weapon rises on end before
			// the slab, its grip at the pool's foot; the mask still comes out of the pool itself
			const bool mask = g_phase == Phase::Mask;
			const RE::NiPoint3 at = mask ? body->world * RE::NiPoint3{ 0.0f, -6.0f - 10.0f * a_rise, kPoolZ - 6.0f + 6.0f * a_rise }
			                             : body->world * RE::NiPoint3{ 0.0f, -kFront - 8.0f, kPoolZ - 95.0f + 40.0f * a_rise };
			auto r = g_show.get();
			const std::uint32_t showId = g_phase == Phase::Mask ? gen::kTabletMask : g_prize >= 0 ? gen::kTabletWeapons[g_prize].show : 0;
			if (!r && showId) {
				if (auto* base = Forms::Ours(showId) ? Forms::Ours(showId)->As<RE::TESBoundObject>() : nullptr) {
					if (auto ref = a_tab->PlaceObjectAtMe(base, false)) g_show = ref->CreateRefHandle(), r = ref;
				}
			}
			if (!r) return;
			r->SetPosition(at);
			// (the mask faces you out of the pool, turning a little; a weapon turns round)
			r->SetAngle({ mask ? 0.0f : -RE::NI_HALF_PI, 0.0f, mask ? a_tab->GetAngleZ() + RE::NI_PI + 0.35f * std::sin(g_clock * 1.7f) : a_tab->GetAngleZ() + g_clock * 0.9f });
			if (g_phase == Phase::Offering) Glow(r.get(), g_prize, g_showGlow);
		}

		// (Noah: "it's very hard to see what the item is. Are you able to make it glow around the item that it lands on? ... It needs
		// more lighting so you can see what you roll") through a roll and while the win waits: a bright white light stands
		// before the slab's face, and round the weapon won, motes of its rarity's colour rise (worn green, battered gold)
		void TickLamp(RE::TESObjectREFR* a_tab, float a_delta)
		{
			const bool want = a_tab && a_tab->Is3DLoaded() && (g_phase == Phase::Rolling || g_phase == Phase::Offering || g_phase == Phase::Mask);
			auto lamp = g_lamp.get();
			if (!want) {
				if (lamp) lamp->Disable(), lamp->SetDelete(true);
				g_lamp = {};
				return;
			}
			auto* body = a_tab->Get3D() ? a_tab->Get3D()->GetObjectByName("TabletBody") : nullptr;
			if (!body) return;
			const RE::NiPoint3 at = body->world * RE::NiPoint3{ 0.0f, -kFront - 70.0f, kPoolZ + 10.0f };
			if (!lamp) {
				auto* base = Forms::Ours(gen::kTabletLamp) ? Forms::Ours(gen::kTabletLamp)->As<RE::TESBoundObject>() : nullptr;
				if (auto l = base ? a_tab->PlaceObjectAtMe(base, false) : nullptr) l->SetPosition(at), g_lamp = l->CreateRefHandle(), lamp = l;
			} else if (lamp->GetPosition().GetDistance(at) > 20.0f) {
				lamp->SetPosition(at);
			}
			static float next = 0.0f;
			if ((next -= a_delta) > 0.0f) return;
			next = 0.45f;
			const int w = g_phase == Phase::Offering ? g_prize : g_phase == Phase::Rolling ? g_reelAt : -1;
			auto sh = g_phase == Phase::Offering ? g_show.get() : g_reel.get();
			if (w >= 0 && sh) Gun::Motes(gen::kTabletWeapons[w].battered ? 8 : 7, sh->GetPosition() + RE::NiPoint3{ 0, 0, g_phase == Phase::Offering ? 40.0f : 10.0f }, g_phase == Phase::Offering ? 0.45f : 0.25f, 1.1f);
		}
		// how high over its plinth the tablet is: coming down the beam (falling faster and faster), going up it
		float BodyLift()
		{
			if (g_arriveT >= 0) {
				const float u = std::clamp((g_arriveT - kBeamLead) / kDropTime, 0.0f, 1.0f);
				return kDropHeight * (1.0f - u * u);
			}
			if (g_leaveT >= 0) {
				const float u = std::clamp(g_leaveT / kRiseTime, 0.0f, 1.0f);
				return kDropHeight * u * u;
			}
			return 0.0f;
		}
		// the tablet itself seen: down, or on its way down or up the beam
		bool BodyShown() { return (g_present && (g_arriveT < 0 || g_arriveT >= kBeamLead)) || (g_leaveT >= 0 && g_leaveT < kRiseTime); }
		int TownOfRef(const RE::TESObjectREFR* a_ref)
		{
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) if (a_ref && Shops::Placed(t, 7) == a_ref) return t;
			return -1;
		}
		std::string WhereLine(int a_here)
		{
			const std::string town = g_town >= 0 ? std::string(gen::kTowns[g_town]) : std::string("Riverwood");
			if (g_town == a_here) return "The Tablet of Mysteries comes down here when the night's first wave begins";
			return "The Tablet of Mysteries is in " + town + (g_present ? " tonight - follow its beam" : " - it comes down there at the night's first wave");
		}
		void Animate(RE::TESObjectREFR* a_ref, float a_delta)
		{
			auto* root = a_ref ? a_ref->Get3D() : nullptr;
			auto* body = Find(root, "TabletBody");
			if (!body) return;
			g_clock += a_delta;
			const bool rolling = g_phase == Phase::Rolling;
			const float k = rolling ? std::clamp(g_t / kRollTime, 0.0f, 1.0f) : 0.0f;
			// the slab: a slow bob and sway; in a roll it shudders, harder as the reel slows to its end
			float sx = 0.0f, sz = 0.0f;
			if (rolling) {
				const float amp = 0.25f + 0.55f * k;
				sx = std::uniform_real_distribution<float>(-amp, amp)(g_rng), sz = std::uniform_real_distribution<float>(-amp, amp)(g_rng);
			}
			body->local.translate = RE::NiPoint3{ sx, 0.0f, 2.6f * std::sin(g_clock * 0.9f) + sz + BodyLift() };
			const float yaw = 0.055f * std::sin(g_clock * 0.33f);
			RE::NiMatrix3 by;
			by.entry[0][0] = std::cos(yaw), by.entry[0][1] = -std::sin(yaw), by.entry[0][2] = 0;
			by.entry[1][0] = std::sin(yaw), by.entry[1][1] = std::cos(yaw), by.entry[1][2] = 0;
			by.entry[2][0] = 0, by.entry[2][1] = 0, by.entry[2][2] = 1;
			body->local.rotate = by;
			// the rings: idle, a slow drift; rolling, they grind - fast and uneven, sticking a moment and lurching on, the
			// two the opposite ways, slowing to a stop with the reel
			float vo = 0.06f, vi = -0.09f;
			if (rolling) {
				const float sp = 7.0f * (1.0f - k * k) + 0.4f;
				if ((g_stallO -= a_delta) < -0.6f && std::uniform_real_distribution<float>(0, 1)(g_rng) < a_delta * 2.5f) g_stallO = 0.12f;
				if ((g_stallI -= a_delta) < -0.5f && std::uniform_real_distribution<float>(0, 1)(g_rng) < a_delta * 3.0f) g_stallI = 0.10f;
				vo = g_stallO > 0 ? 0.0f : sp * (0.8f + 0.4f * std::sin(g_t * 9.0f));
				vi = g_stallI > 0 ? 0.0f : -sp * 1.3f * (0.8f + 0.4f * std::sin(g_t * 11.0f + 1.0f));
			}
			g_ringO += vo * a_delta, g_ringI += vi * a_delta;
			TurnRing(Find(root, "RingOuter"), g_ringO);
			TurnRing(Find(root, "RingInner"), g_ringI);
			// the gems: lit one by one through the roll, all lit while the win waits, dark at rest
			const int lit = rolling ? static_cast<int>(k * 8.0f + 0.5f) : g_phase == Phase::Offering ? 8 : 0;
			for (int i = 0; i < 8; ++i) Show(root, fmt::format("Gem{}_bb", i).c_str(), i < lit);
			// sparks spitting from the cracks: in bursts through the roll
			for (int i = 0; i < 6; ++i) {
				const bool on = rolling && std::fmod(g_t * (1.3f + 0.21f * i) + i * 0.37f, 1.0f) < 0.35f;
				Show(root, fmt::format("Spark{}_bb", i).c_str(), on);
			}
			Show(root, "Ghost", false);   // (the old white glyph reel: the little weapons now, TickReel)
			Show(root, "GoldFlare_bb", g_gold && (g_phase == Phase::Offering || (rolling && k > 0.85f)));
			RE::NiUpdateData ud;
			body->Update(ud);
			TickReel(a_ref, body, k, a_delta);
		}
	}

	namespace
	{
		void EndBeacon()
		{
			if (auto b = g_beacon.get()) b->Disable(), b->SetDelete(true);
			g_beacon = {}, g_beaconTown = -1;
		}
		// (Noah: "a beacon of light that shoots high so it can be seen in the distance", "The tablet's own glow") Skyrim's
		// wayshrine beam over the spot, in the tablet's amber, while it is there (and while it comes and goes)
		void TickBeacon(RE::TESObjectREFR* a_tab)
		{
			const bool want = a_tab && (g_present || g_leaveT >= 0);
			if (!want || g_beaconTown != g_town) EndBeacon();
			if (!want || g_beacon.get() || !a_tab->Is3DLoaded()) return;
			auto* base = Forms::Ours(gen::kTabletBeacon) ? Forms::Ours(gen::kTabletBeacon)->As<RE::TESBoundObject>() : nullptr;
			if (!base) return;
			if (auto b = a_tab->PlaceObjectAtMe(base, false)) {
				b->SetPosition(a_tab->GetPosition() + RE::NiPoint3{ 0, 0, 120.0f });
				b->GetReferenceRuntimeData().refScale = 45;   // (0.45: high over the roofs, a hand wide)
				Gun::TintRef(b.get(), 1.0f, 0.78f, 0.45f);
				g_beacon = b->CreateRefHandle(), g_beaconTown = g_town;
			}
		}
		// the tablet's own node shown or hidden on a plinth (every city's plinth stands; the tablet only where it is, when it is)
		void ShowBody(RE::TESObjectREFR* a_ref, bool a_on)
		{
			auto* root = a_ref && a_ref->Is3DLoaded() ? a_ref->Get3D() : nullptr;
			if (auto* body = root ? root->GetObjectByName("TabletBody") : nullptr; body && body->GetAppCulled() == a_on) body->SetAppCulled(!a_on);
		}
		// (Noah's crash: "my game crashed when I tried to use the Tablet") the tablet's model has no collision, so Skyrim's
		// crosshair never finds it (no prompt, no activation): it is aimed at the way the Echo Altar's orb is - the camera's
		// line passing close by its face or its plinth, within reach
		RE::TESObjectREFR* AimedTablet(RE::PlayerCharacter* a_p)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!a_p || !cam || !cam->cameraRoot) return nullptr;
			const auto& w = cam->cameraRoot->world;
			const RE::NiPoint3 eye = w.translate, fwd{ w.rotate.entry[0][1], w.rotate.entry[1][1], w.rotate.entry[2][1] };
			RE::TESObjectREFR* best = nullptr;
			float bestMiss = 1e9f;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				auto* r = Shops::Placed(t, 7);
				// (Noah: "I want the hold X to roll icon to pop up when you get within 2 meters of the tablet instead of far away")
				if (!r || !r->Is3DLoaded() || r->IsDisabled()) continue;
				if (const auto d = r->GetPosition() - a_p->GetPosition(); std::hypot(d.x, d.y) > kReach) continue;
				const float s = r->GetScale();
				for (const float z : { 125.0f, 40.0f }) {   // (its face, its plinth)
					const RE::NiPoint3 at = r->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, z * s };
					const RE::NiPoint3 to = at - eye;
					const float along = to.Dot(fwd);
					if (along <= 0 || along > 600.0f) continue;
					const float miss = (eye + fwd * along).GetDistance(at);
					if (miss < 75.0f * s && miss < bestMiss) bestMiss = miss, best = r;
				}
			}
			return best;
		}
		void TickNights(RE::PlayerCharacter* a_p, float a_delta)
		{
			// (Noah: "From the first wave to dawn") down when the night's fighting starts; drawn back up at dawn (once a roll or
			// a waiting weapon is done)
			const bool night = Director::Fighting();
			if (night && !g_present && g_leaveT < 0) {
				g_present = true, g_arriveT = 0.0f;
				logger::info("tablet: comes down in {}", g_town >= 0 ? gen::kTowns[g_town] : "?");
			}
			if (!night && g_present && g_leaveT < 0 && g_phase == Phase::Idle) {
				g_leaveT = 0.0f, g_arriveT = -1.0f, g_moveTo = -1;
				if (auto* r = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr) Forms::PlayAt(gen::Sound::coffer_charge, r);
				logger::info("tablet: drawn up at dawn from {}", g_town >= 0 ? gen::kTowns[g_town] : "?");
			}
			auto* ref = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr;
			if (g_arriveT >= 0) {
				const bool before = g_arriveT < kBeamLead + kDropTime;
				g_arriveT += a_delta;
				if (before && g_arriveT >= kBeamLead + kDropTime) {   // it lands: the ground cracks, a burst of its light
					g_arriveT = -1.0f;
					if (ref && ref->Is3DLoaded() && a_p->GetPosition().GetDistance(ref->GetPosition()) < 8000.0f) {
						Gun::FxBlast("giantslam", ref->GetPosition());
						Gun::QueueFx("CurseOfAlduin\\fx\\philosopher_burst.nif", ref->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f }, 1.2f, 0.0f, 1.0f, false);
						Forms::PlayAt(gen::Sound::boss_land, ref);
						Forms::PlayAt(gen::Sound::coffer_land, ref);
					}
				}
			}
			if (g_leaveT >= 0 && (g_leaveT += a_delta) >= kRiseTime + 0.5f) {   // up and gone (the beam lingers a breath)
				g_leaveT = -1.0f;
				if (ref) ShowBody(ref, false);
				if (g_moveTo >= 0 && g_moveTo != g_town) {   // (Noah: "Right away, same night") down at once in the next city
					g_town = g_moveTo;
					g_present = Director::Fighting(), g_arriveT = g_present ? 0.0f : -1.0f;
					Hud::Banner(fmt::format("The Tablet of Mysteries comes down in {}", gen::kTowns[g_town]), 4.0f);
				} else {
					g_present = false;
				}
				g_moveTo = -1;
				EndBeacon();
				ref = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr;
			}
			TickBeacon(ref);
			if (ref) ShowBody(ref, BodyShown());
			// every other plinth: its tablet hidden (twice a second is plenty)
			if ((g_sweep -= a_delta) <= 0) {
				g_sweep = 0.5f;
				for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
					if (t == g_town) continue;
					if (auto* o = Shops::Placed(t, 7)) ShowBody(o, false);
				}
			}
			// (Noah: "when looking at the base with no tablet it shows a message saying where the tablet location is")
			g_lookCool -= a_delta;
			{
				auto* tgt = AimedTablet(a_p);
				const int here = tgt ? TownOfRef(tgt) : -1;
				const RE::FormID id = here >= 0 ? tgt->GetFormID() : 0;
				if (id && id != g_lookedAt && g_lookCool <= 0 && (here != g_town || !g_present)) {
					Hud::Banner(WhereLine(here), 3.5f);
					g_lookCool = 4.0f;
				}
				g_lookedAt = id;
			}
		}
	}

	bool Present() { return g_present && g_arriveT < 0 && g_leaveT < 0; }
	int Town() { return g_town; }
	int Price()
	{
		Night();
		// (Noah: 1,500, +500 for every spin or repair, back to 1,500 at dawn) (the essence economy: 3% more a night after night 10)
		return static_cast<int>(std::lround((1500 + 500 * g_steps) * Director::PriceScale() / 50.0f)) * 50;
	}

	bool Carrying()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* f = FormOf(g_held);
		return p && f && (p->GetEquippedObject(false) == f || p->GetEquippedObject(true) == f);
	}

	void OnActivate(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) return;
		// (Noah: "if the tablet is not in that city ... it shows a message saying where the tablet location is") an empty plinth
		if (const int here = TownOfRef(a_ref); here != g_town || !g_present || g_arriveT >= 0 || g_leaveT >= 0) {
			if (here == g_town && (g_arriveT >= 0 || g_leaveT >= 0)) return;   // (on its way down or up)
			Hud::Banner(WhereLine(here), 3.5f);
			return;
		}
		if (g_phase == Phase::Mask) return;
		if (g_phase == Phase::Rolling) { Hud::Banner("The tablet is still turning", 1.5f); return; }
		// a press alone does nothing now: the roll and the taking are held (TickHold) - a tap is told so
		if (std::chrono::steady_clock::now() - g_holdDoneAt < std::chrono::milliseconds(1200)) return;   // (the hold's own release)
		if (g_phase == Phase::Offering) Hud::Banner("Hold Activate to take it", 1.5f);
		else Hud::Banner("Hold Activate - " + Num(Price()) + " Cursed Essence", 1.5f);
	}

	// the hold done: the roll, the mending or the taking
	void Act(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref || !Present() || g_phase == Phase::Mask || g_phase == Phase::Rolling) return;
		logger::info("tablet: hold done ({})", g_phase == Phase::Offering ? "take" : "roll");
		if (g_phase == Phase::Offering) {   // the won weapon taken
			if (g_prize >= 0) {
				Give(g_prize);
				Forms::PlayAt(gen::Sound::coffer_take, a_ref);
				Hud::Banner(std::string(gen::kTabletWeapons[g_prize].label) + "  -  yours, until it wears out or you fall", 3.5f);
			}
			ClearShow();
			g_phase = Phase::Idle, g_t = 0.0f, g_gold = false, g_prize = -1;
			return;
		}
		const int price = Price();
		// (Noah: "pay to refill the ammo or repair weapon by paying the same price as it would be to spin") holding a worn
		// donated weapon, the tablet mends it
		if (g_held >= 0 && Carrying() && g_uses < gen::kTabletWeapons[g_held].uses) {
			if (!Points::Spend(price)) { Hud::Banner("Mending it costs " + Num(price) + " Cursed Essence", 2.0f); return; }
			++g_steps;
			const auto& d = gen::kTabletWeapons[g_held];
			if (d.type == 4) {
				auto* p = RE::PlayerCharacter::GetSingleton();
				if (auto* ar = Forms::Ours(gen::kTabletArrow) ? Forms::Ours(gen::kTabletArrow)->As<RE::TESBoundObject>() : nullptr; ar && p) {
					p->AddObjectToContainer(ar, nullptr, std::max(0, d.uses - p->GetItemCount(ar)), nullptr);
				}
			}
			g_uses = d.uses;
			Rename(g_held, g_uses);
			Forms::PlayAt(gen::Sound::forge_upgrade, a_ref);
			Hud::Banner(std::string(d.label) + "  -  mended  (next: " + Num(Price()) + ")", 3.0f);
			logger::info("tablet: {} mended for {}", d.id, price);
			return;
		}
		if (!Points::Spend(price)) { Hud::Banner("The tablet asks " + Num(price) + " Cursed Essence", 2.0f); return; }
		++g_steps, ++g_rolls;
		g_paid = price;
		g_ref = a_ref->CreateRefHandle();
		g_phase = Phase::Rolling, g_t = 0.0f, g_bell = false;
		ClearReel(), g_reelOrder.clear(), g_reelStep = 0, g_reelNext = 0.0f;
		// (Noah: after a random 4-8 rolls the skull can come: the essence back, and the tablet moves)
		g_skull = g_rolls > g_moveAfter;
		g_prize = g_skull ? -1 : Pick();
		g_gold = !g_skull && gen::kTabletWeapons[g_prize].battered;
		Forms::PlayAt(gen::Sound::coffer_charge, a_ref);
		Forms::PlayAt(gen::Sound::coffer_spin, a_ref);
		logger::info("tablet: rolled in {} for {} - {}", g_town >= 0 ? gen::kTowns[g_town] : "?", price, g_skull ? "the skull" : gen::kTabletWeapons[g_prize].id);
	}

	// your blow, shot or cast: a donated weapon's use spent (melee and fists by their hits; staffs and the Codex by casts;
	// a bow by its arrows, read from the pack)
	void OnHit(RE::FormID a_source)
	{
		auto* f = FormOf(g_held);
		if (!f || f->GetFormID() != a_source || gen::kTabletWeapons[g_held].type == 4) return;
		const int t = gen::kTabletWeapons[g_held].type;
		if (t == 5 || t == 7) return;   // (casters: their casts count)
		static auto last = std::chrono::steady_clock::time_point{};
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(150)) return;   // (one swing cutting several: one use)
		last = now;
		if (Gun::TestKitOn()) return;   // (the test kit's: they never wear)
		if (--g_uses <= 0) Break(); else Rename(g_held, g_uses);
	}
	void CastPower();
	void OnCast(RE::FormID a_spell)
	{
		auto* f = FormOf(g_held);
		if (!f) return;
		const int t = gen::kTabletWeapons[g_held].type;
		bool mine = false;
		if (t == 7) mine = f->GetFormID() == a_spell;
		else if (t == 5) if (auto* w = f->As<RE::TESObjectWEAP>(); w && w->formEnchanting && w->formEnchanting->GetFormID() == a_spell) mine = true;
		if (!mine) return;
		if (gen::kTabletWeapons[g_held].id == "wanderstar") return;   // (a wand now: its stars are loosed while the cast is held, TickStarWand)
		CastPower();
		if (Gun::TestKitOn()) return;   // (the test kit's: they never wear)
		if (--g_uses <= 0) Break(); else Rename(g_held, g_uses);
	}

	// ================================================================== the donated weapons' powers (Noah's picks; the
	// design: docs/TABLET_OF_MYSTERIES.md). Each blow's share is the weapon's own (Share: one or two tiers over your class
	// sword); what a power adds is a part of it
	namespace
	{
		float g_pclock = 0;            // (seconds, for the powers' timers)
		int   g_count = 0, g_countFor = -1;   // blows or casts with the weapon held (the bell's fifth, the wraps' fourth, the sun and moon by turns, the Codex's tenth)
		float g_slamCool = 0, g_eclipseCool = 0, g_splitCool = 0;
		std::unordered_map<RE::FormID, float> g_salt;   // (struck by the sickle: they take 30% more until then)
		RE::NiPoint3 Mid(RE::Actor* a) { return a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }; }
		std::vector<RE::Actor*> Others(RE::Actor* a_not, const RE::NiPoint3& a_at, float a_r, std::size_t a_n)
		{
			auto v = Gun::FoesNear(a_at, a_r);
			std::erase_if(v, [&](RE::Actor* f) { return !f || f == a_not || f->IsDead(); });
			std::sort(v.begin(), v.end(), [&](RE::Actor* x, RE::Actor* y) { return x->GetPosition().GetDistance(a_at) < y->GetPosition().GetDistance(a_at); });
			if (v.size() > a_n) v.resize(a_n);
			return v;
		}
		// where you aim: the one of the dead nearest your crosshair ahead (within a narrow cone), else a point ahead on the ground
		RE::NiPoint3 AimPoint(RE::Actor** a_target)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			const RE::NiPoint3 from = Gun::SwingFrom(), dir = Gun::SwingDir();
			RE::Actor* best = nullptr;
			float bestA = 0.2f;   // (about 11 degrees)
			for (auto* f : Gun::FoesNear(from + dir * 1500.0f, 1600.0f)) {
				if (!f || f->IsDead()) continue;
				RE::NiPoint3 d = Mid(f) - from;
				const float l = d.Length();
				if (l < 1.0f || l > 3000.0f) continue;
				d = d * (1.0f / l);
				const float a = std::acos(std::clamp(d.Dot(dir), -1.0f, 1.0f));
				if (a < bestA) bestA = a, best = f;
			}
			if (a_target) *a_target = best;
			if (best) return best->GetPosition();
			RE::NiPoint3 at = from + dir * 1400.0f;
			if (p) at.z = p->GetPosition().z;
			return at;
		}
		// a bolt leaping from one of the dead to the next nearest not yet struck, up to a_n of them
		void Chain(RE::Actor* a_first, const RE::NiPoint3& a_from, int a_n, float a_share, int a_elem)
		{
			std::vector<RE::Actor*> hit;
			RE::NiPoint3 at = a_from;
			RE::Actor* cur = a_first;
			for (int k = 0; k < a_n && cur; ++k) {
				Gun::LightningArc(at, Mid(cur), 0.07f * k);
				Gun::ElemBurst(a_elem, Mid(cur), 0.45f, 0.07f * k, 0.6f);
				Gun::HitShare(cur, a_share);
				hit.push_back(cur);
				at = Mid(cur);
				RE::Actor* next = nullptr;
				for (auto* f : Others(cur, cur->GetPosition(), 650.0f, 12)) {
					if (std::find(hit.begin(), hit.end(), f) == hit.end()) { next = f; break; }
				}
				cur = next;
			}
		}
		// (the Codex) each cast a random chapter; every tenth all four
		void Chapter(int a_ch, float S)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			RE::Actor* t = nullptr;
			const RE::NiPoint3 at = AimPoint(&t);
			switch (a_ch) {
			case 0:   // Fire: a meteor where you aim
				Gun::Meteor(at, false, 1.0f, [S](const RE::NiPoint3& land) {
					Gun::AreaHurt(land, 380.0f, S * 3.0f, "");
					for (auto* f : Gun::FoesNear(land, 380.0f)) Gun::Burn(f, Director::RoundHealth() * S * 0.25f, 4.0f, 0);
				});
				break;
			case 1: {   // Frost: a line of ice bursting out of the ground toward the target, freezing all it hits
				const RE::NiPoint3 from = p ? p->GetPosition() : at;
				RE::NiPoint3 d = at - from; d.z = 0;
				const float l = std::max(1.0f, d.Length());
				d = d * (1.0f / l);
				const RE::NiPoint3 to = from + d * std::min(1100.0f, std::max(600.0f, l + 150.0f));
				for (int k = 1; k <= 10; ++k) Gun::FxBlast("icevolley", from + (to - from) * (k / 10.0f), 0.05f * k);
				for (auto* f : Gun::LineHurt(from + RE::NiPoint3{ 0, 0, 40.0f }, to + RE::NiPoint3{ 0, 0, 40.0f }, 120.0f, S * 2.0f)) Gun::Freeze(f, 3.0f);
				break;
			}
			case 2:   // Storm: a bolt through up to 10 of the dead
				if (!t) { auto v = Gun::FoesNear(at, 500.0f); if (!v.empty()) t = v.front(); }
				Gun::FxBlast("shockmass", at);
				Chain(t, Gun::SwingFrom(), 10, S * 2.0f, 2);
				break;
			default:  // Void: a black orb that pulls the dead together and crushes them
				Gun::Singularity(at + RE::NiPoint3{ 0, 0, 60.0f }, 450.0f, 2.0f, S * 3.5f, true);
				break;
			}
		}
	}
	float TakenMult(RE::Actor* a_actor)
	{
		if (!a_actor) return 1.0f;
		const auto it = g_salt.find(a_actor->GetFormID());
		return it != g_salt.end() && it->second > g_pclock ? 1.3f : 1.0f;
	}
	// a blow or an arrow of yours landed with the donated weapon in hand: its power
	namespace
	{
		// an orb of lantern-light that pulls the dead in, then bursts
		// (Noah: "its way too powerful") its pull smaller, its burst under half what it was
		void LanternOrb(float S)
		{
			RE::Actor* t = nullptr;
			const RE::NiPoint3 at = AimPoint(&t);
			RE::NiPoint3 d = (at + RE::NiPoint3{ 0, 0, 60.0f }) - Gun::SwingFrom();
			if (d.Length() > 1e-3f) d = d * (1.0f / d.Length());
			Gun::Lob("CurseOfAlduin\\fx\\lightorb.nif", Gun::SwingFrom(), d * 1500.0f + RE::NiPoint3{ 0, 0, 120.0f }, 0.8f, [S](const RE::NiPoint3& land) {
				Gun::Singularity(land + RE::NiPoint3{ 0, 0, 50.0f }, 280.0f, 1.1f, S * 0.9f, false);
			}, 4.0f, 500.0f);
		}
	}
	namespace
	{
		// (Noah: "Wyrm spine spike heavy attack should be a shockwave that staggers enemies within 8 meters. Same heavy attack should
		// be used on the wyrm tooth scythe") the ground struck: a ring of force out from you, the dead within 8 m staggered and hurt
		void Shockwave(float a_share)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			const RE::NiPoint3 at = p->GetPosition();
			Gun::FxBlast("giantslam", at);
			Gun::Ring("white", at, 40.0f, 560.0f, 0.6f);
			Gun::AreaHurt(at, 560.0f, a_share, "");
			for (auto* o : Gun::FoesNear(at, 560.0f)) Gun::Knock(o, 1.0f);
		}
	}
	void OnStrike(RE::Actor* a_target, RE::FormID a_source, bool a_power, bool a_arrow)
	{
		// (Noah: "the storm lion talons don't do anything" - the test kit hands you every donated weapon, but only the one the
		// tablet gave you was asked about) whichever donated weapon struck - the one held from the tablet, or any other
		int held = -1;
		for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()) && held < 0; ++i) {
			auto* fi = FormOf(i);
			auto* li = LeftOf(i);
			if ((fi && fi->GetFormID() == a_source) || (li && li->GetFormID() == a_source)) held = i;
		}
		if (held < 0) return;
		auto* f = FormOf(held);
		if (!a_target || !f || !Director::IsFoe(a_target)) return;
		auto* p = RE::PlayerCharacter::GetSingleton();
		const float S = Share();
		const RE::NiPoint3 tp = a_target->GetPosition();
		const std::string_view id = gen::kTabletWeapons[held].id;
		if (g_countFor != held) g_countFor = held, g_count = 0;   // (a new weapon counts afresh)
		++g_count;
		if (id == "stormtalons") {   // every 3rd hit jumps lightning to 2 more; a heavy hit stuns the one struck
			if (g_count % 3 == 0) for (auto* o : Others(a_target, tp, 420.0f, 2)) { Gun::LightningArc(Mid(a_target), Mid(o), 0.0f); Gun::HitShare(o, S * 0.6f); }
			if (a_power) { Gun::Freeze(a_target, 1.5f); Gun::ElemBurst(2, Mid(a_target), 0.6f, 0.0f, 0.6f); }
		} else if (id == "talons") {   // the bleed runs twice as deep in the dead; each kill heals you a little
			Gun::Burn(a_target, S * 0.08f, 4.0f, -1);
			Gun::ElemBurst(11, Mid(a_target), 0.4f, 0.0f, 0.5f);
			if (a_target->IsDead() || a_target->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0) {
				if (auto* av = p ? p->AsActorValueOwner() : nullptr) av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.03f);
			}
		} else if (id == "cinderclaws") {   // every swipe sets the dead burning; a heavy swipe flings a fan of embers
			Gun::Burn(a_target, S * 0.06f, 4.0f, 0);
			if (a_power && g_slamCool <= 0 && p) {
				g_slamCool = 1.2f;
				RE::NiPoint3 d = Gun::SwingDir(); d.z = 0;
				if (d.Length() > 1e-3f) d = d * (1.0f / d.Length());
				const RE::NiPoint3 from = p->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f };
				for (int k = -2; k <= 2; ++k) {
					const float a = k * 0.28f;
					const RE::NiPoint3 dir{ d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a), 0 };
					for (auto* o : Gun::LineHurt(from, from + dir * 520.0f, 70.0f, S * 0.5f)) Gun::Burn(o, S * 0.06f, 4.0f, 0);
					Gun::ElemBurst(0, from + dir * 260.0f, 0.5f, 0.05f * (k + 2), 0.6f);
				}
			}
		} else if (id == "silverscythe") {   // (Noah: "too strong for a weaker weapon" - "Light silver burst") every 4th hit flashes
			// silver, the one struck slowed a moment
			if (g_count % 4 == 0) {
				Gun::ElemBurst(10, Mid(a_target), 0.45f, 0.0f, 0.5f);
				Director::Web(a_target->GetPosition(), 45.0f, 1.5f, 50.0f, false);
			}
		} else if (id == "bonereaper") {   // one kill in four rises to fight for you
			static std::mt19937 rng{ std::random_device{}() };
			if (std::uniform_int_distribution<int>(0, 3)(rng) == 0) Classes::QueueThrall(a_target, 0);   // (it rises only if this blow kills)
			Gun::ElemBurst(6, Mid(a_target), 0.3f, 0.0f, 0.4f);
		} else if (id == "wyrmtooth") {   // its hits bleed; a heavy sweep - the shockwave, the dead within 8 m staggered
			Gun::Burn(a_target, S * 0.07f, 4.0f, -1);
			Gun::ElemBurst(11, Mid(a_target), 0.35f, 0.0f, 0.5f);
			if (a_power && g_splitCool <= 0 && p) {
				g_splitCool = 1.5f;
				Shockwave(S * 1.0f);
			}
		} else if (id == "bloodmoon") {   // each hit drains a little of the dead's blood to you; a power sweep drains all it touches
			auto heal = [&](float a_frac) { if (auto* av = p ? p->AsActorValueOwner() : nullptr) av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * a_frac); };
			heal(0.005f);
			Gun::ElemBurst(11, Mid(a_target), 0.3f, 0.0f, 0.4f);
			if (a_power && g_slamCool <= 0 && p) {
				g_slamCool = 1.2f;
				int n = 0;
				for (auto* o : Gun::FoesNear(p->GetPosition(), 280.0f)) {
					if (o == a_target) continue;
					Gun::HitShare(o, S * 0.6f), Gun::ElemBurst(11, Mid(o), 0.4f, 0.0f, 0.5f), ++n;
				}
				heal(0.005f * n);
			}
		} else if (id == "wyrmspine") {   // thrusts pierce through 2 more in a line; a heavy thrust drives a spike of bone up ahead
			if (p) {
				RE::NiPoint3 d = Gun::SwingDir(); d.z = 0;
				if (d.Length() > 1e-3f) d = d * (1.0f / d.Length());
				int n = 0;
				for (auto* o : Gun::LineHurt(tp + RE::NiPoint3{ 0, 0, 50.0f }, tp + d * 300.0f + RE::NiPoint3{ 0, 0, 50.0f }, 70.0f, 0.0f)) {
					if (o == a_target || n >= 2) continue;
					Gun::HitShare(o, S * 0.7f), ++n;
				}
				if (a_power && g_splitCool <= 0) {
					g_splitCool = 1.5f;
					Shockwave(S * 1.2f);
				}
			}
		} else if (id == "frostmourn") {   // thrusts slow everything they touch; a heavy thrust freezes a ring around you
			Gun::ElemBurst(1, Mid(a_target), 0.35f, 0.0f, 0.5f);
			if (auto* av = a_target->AsActorValueOwner()) {
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -40.0f);
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
				Tasks::Later(3.0f, [h = a_target->GetHandle()] { if (auto x = h.get()) { auto* v = x->AsActorValueOwner(); v->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 40.0f); v->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f); v->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f); } });
			}
			if (a_power && g_eclipseCool <= 0 && p) {
				g_eclipseCool = 2.0f;
				Gun::ElemBurst(1, p->GetPosition() + RE::NiPoint3{ 0, 0, 30.0f }, 1.4f, 0.0f, 1.0f);
				for (auto* o : Gun::FoesNear(p->GetPosition(), 330.0f)) Gun::Freeze(o, 3.0f);
			}
		} else if (id == "stormknuckles") {   // each punch arcs lightning to 2 more; a heavy punch slams the ground in a ring of shock
			for (auto* o : Others(a_target, tp, 380.0f, 2)) { Gun::LightningArc(Mid(a_target), Mid(o), 0.0f); Gun::HitShare(o, S * 0.5f); }
			if (a_power && g_slamCool <= 0 && p) {
				g_slamCool = 1.0f;
				Gun::Ring("shock", p->GetPosition(), 30.0f, 400.0f, 0.6f);
				Gun::FxBlast("shockmass", p->GetPosition());
				for (auto* o : Gun::FoesNear(p->GetPosition(), 400.0f)) { Gun::HitShare(o, S * 1.2f); Gun::Knock(o, 1.0f); }
			}
		} else if (id == "gravebell") {   // every fifth blow tolls it: the dead within 5 m stunned
			if (g_count % 5 == 0) {
				Gun::Ring("gold", tp, 30.0f, 360.0f, 0.7f);
				Gun::FxBlast("turnundead", tp + RE::NiPoint3{ 0, 0, 40.0f });
				if (p) Forms::PlayAt(gen::Sound::shout_toll, p);
				for (auto* o : Gun::FoesNear(tp, 360.0f)) { Gun::Knock(o, 1.5f); Gun::Freeze(o, 1.5f); }
			}
		} else if (id == "saltrice") {   // the struck take 30% more from everything for 4 s
			g_salt[a_target->GetFormID()] = g_pclock + 4.0f;
			Gun::Motes(4, tp, 0.4f, 1.0f);
		} else if (id == "wanderstar") {   // (Noah: "the Wandering Star ... needs the quarterstaff attack animations") a heavy blow that lands lets the star out
			if (a_power && g_slamCool <= 0) {
				g_slamCool = 2.5f;
				Gun::FxBlast("light", Mid(a_target));
				Chain(a_target, Mid(a_target), 8, S * 1.4f, 4);
			}
		} else if (id == "lanternwick") {   // (Noah: "start a blizzard on heavy attack and freeze on light attack") light: frozen 3 s;
			// heavy: a blizzard 10 m round where it lands, 6 s - the dead in it slowed, chilled and frozen by turns
			if (!a_power) {
				Gun::Freeze(a_target, 3.0f);
				Gun::ElemBurst(1, Mid(a_target), 0.5f, 0.0f, 0.6f);
			} else if (g_slamCool <= 0) {
				g_slamCool = 6.5f;
				const RE::NiPoint3 at = tp;
				Gun::CycloneSpec cs;
				cs.model = "CurseOfAlduin\\fx\\cyclone_frost.nif";
				cs.from = at, cs.fwd = { 0, 1, 0 }, cs.dist = 0.0f, cs.life = 6.0f, cs.size = 3.2f, cs.fec = 1, cs.share = S * 0.12f, cs.noPull = true;
				Gun::ElemCyclone(std::move(cs));
				Director::Web(at, 700.0f, 6.0f, 55.0f, true);
				for (int k = 0; k < 6; ++k) {
					Tasks::Later(1.0f * k, [at, S]() {
						Gun::AreaHurt(at, 700.0f, S * 0.12f, "");
						Gun::Motes(1, at, 2.6f, 1.2f);
					});
				}
				Gun::BlastSound("frost", at, 0.9f);
			}
		} else if (id == "riverstone") {   // hits knock the dead back; a heavy attack splits the ground in a line
			Gun::Knock(a_target, 1.2f);
			if (a_power && g_splitCool <= 0 && p) {
				g_splitCool = 1.2f;
				const RE::NiPoint3 from = p->GetPosition();
				RE::NiPoint3 d = Gun::SwingDir(); d.z = 0;
				if (d.Length() > 1e-3f) d = d * (1.0f / d.Length());
				const RE::NiPoint3 to = from + d * 700.0f;
				for (int k = 1; k <= 6; ++k) Gun::FxBlast("giantslam", from + (to - from) * (k / 6.0f), 0.05f * k);
				for (auto* o : Gun::LineHurt(from + RE::NiPoint3{ 0, 0, 40.0f }, to + RE::NiPoint3{ 0, 0, 40.0f }, 110.0f, S * 1.5f)) Gun::Knock(o, 1.5f);
			}
		} else if (id == "rattlebone") {   // every arrow bounces to a second of the dead
			for (auto* o : Others(a_target, tp, 650.0f, 1)) { Gun::LightningArc(Mid(a_target), Mid(o), 0.05f); Gun::FxBlast("priestsmall", Mid(o), 0.1f); Gun::HitShare(o, S); }
		} else if (id == "emberclaw") {   // punches set the dead burning; every fourth throws a fireball
			Gun::Burn(a_target, Director::RoundHealth() * S * 0.2f, 4.0f, 0);
			if (g_count % 4 == 0) {
				RE::Actor* t = nullptr;
				const RE::NiPoint3 at = AimPoint(&t);
				RE::NiPoint3 d = (t ? Mid(t) : at + RE::NiPoint3{ 0, 0, 60.0f }) - Gun::SwingFrom();
				if (d.Length() > 1e-3f) d = d * (1.0f / d.Length());
				Gun::Lob("CurseOfAlduin\\fx\\meteor_fire.nif", Gun::SwingFrom(), d * 1900.0f + RE::NiPoint3{ 0, 0, 50.0f }, 0.35f, [S](const RE::NiPoint3& land) {
					Gun::AreaHurt(land, 280.0f, S * 1.5f, "fireball");
					for (auto* o : Gun::FoesNear(land, 280.0f)) Gun::Burn(o, Director::RoundHealth() * S * 0.2f, 4.0f, 0);
				}, 0.0f, 120.0f);
			}
		} else if (id == "eclipse") {   // (Noah) frost and sun: light strikes by turns frost and sun; a heavy attack - an ice shockwave,
			// then a sun one, 10 m round where it lands
			if (g_count % 2) {
				Gun::HitShare(a_target, S * 0.3f);
				Gun::ElemBurst(4, Mid(a_target), 0.6f, 0.0f, 0.6f);
				Gun::BlastSound("light", tp, 0.4f);
			} else {
				Gun::HitShare(a_target, S * 0.3f);
				Gun::Freeze(a_target, 0.8f);
				Gun::ElemBurst(1, Mid(a_target), 0.6f, 0.0f, 0.6f);
				Gun::BlastSound("frost", tp, 0.4f);
			}
			if (a_power && g_eclipseCool <= 0 && p) {
				g_eclipseCool = 3.0f;
				const RE::NiPoint3 at = tp;
				Gun::Ring("frost", at, 40.0f, 700.0f, 0.7f);
				Gun::ElemBurst(1, at + RE::NiPoint3{ 0, 0, 40.0f }, 2.4f, 0.0f, 0.9f);
				Gun::BlastSound("frost", at, 0.9f);
				Gun::AreaHurt(at, 700.0f, S * 0.9f, "");
				for (auto* o : Gun::FoesNear(at, 700.0f)) Gun::Freeze(o, 1.0f);
				Tasks::Later(0.45f, [at, S]() {
					Gun::Ring("gold", at, 40.0f, 700.0f, 0.7f);
					Gun::ElemBurst(4, at + RE::NiPoint3{ 0, 0, 40.0f }, 2.6f, 0.0f, 0.9f);
					Gun::BlastSound("lightmassive", at, 0.9f);
					Gun::AreaHurt(at, 700.0f, S * 0.9f, "");
					for (auto* o : Gun::FoesNear(at, 700.0f)) Gun::Knock(o, 0.8f);
				});
			}
		} else if (id == "whisperwood") {   // arrows root the dead; each held heals you a little
			Gun::Freeze(a_target, 3.0f);
			Gun::Puddle(3, tp, 0.4f, 3.0f);
			if (p) {
				auto* av = p->AsActorValueOwner();
				const float mx = av->GetPermanentActorValue(RE::ActorValue::kHealth);
				av->RestoreActorValue(RE::ActorValue::kHealth, mx * 0.02f);
			}
		}
	}
	// a donated staff's or the Codex's cast: what it casts (the use is counted in OnCast)
	void CastPower()
	{
		if (g_held < 0) return;
		const float S = Share();
		const std::string_view id = gen::kTabletWeapons[g_held].id;
		if (g_countFor != g_held) g_countFor = g_held, g_count = 0;
		++g_count;
		if (id == "lanternwick") {
			LanternOrb(S);
		} else if (id == "wanderstar") {   // a dim caged star that bounces between up to 8 of the dead
			RE::Actor* t = nullptr;
			const RE::NiPoint3 at = AimPoint(&t);
			if (!t) { auto v = Gun::FoesNear(at, 600.0f); if (!v.empty()) t = v.front(); }
			Gun::FxBlast("light", Gun::SwingFrom() + Gun::SwingDir() * 60.0f);
			Chain(t, Gun::SwingFrom(), 8, S * 1.4f, 4);
		} else if (id == "codex") {   // a random chapter; every tenth cast all four ("a chapter's end")
			if (g_count % 10 == 0) {
				Hud::Banner("THE CODEX'S CHAPTER ENDS", 2.0f);
				for (int c = 0; c < 4; ++c) Chapter(c, S);
			} else {
				static const char* kName[4] = { "Fire", "Frost", "Storm", "Void" };
				const int c = std::uniform_int_distribution<int>(0, 3)(g_rng);
				Hud::Banner(std::string("The Codex turns to ") + kName[c], 1.2f);
				Chapter(c, S);
			}
		}
	}
	// (Noah: "on death") falling takes it
	void OnFell()
	{
		if (g_held < 0 || Gun::TestKitOn()) return;   // (the test kit keeps them)
		TakeAway(g_held);
		Hud::Banner(std::string(gen::kTabletWeapons[g_held].label) + " is lost", 3.0f);
		g_held = -1, g_uses = 0;
	}
	// (Noah: the test kit - "include the tablet items") every donated weapon in your pack (the Codex learned), 200 of the
	// bows' arrows; equip one to try it (Tick)
	void DevKit()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		int n = 0;
		for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) {
			auto* f = FormOf(i);
			if (!f) continue;
			Rename(i, gen::kTabletWeapons[i].uses);
			if (auto* sp = f->As<RE::SpellItem>()) { if (!p->HasSpell(sp)) p->AddSpell(sp); }
			else if (auto* b = f->As<RE::TESBoundObject>(); b && p->GetItemCount(b) <= 0) p->AddObjectToContainer(b, nullptr, 1, nullptr);
			++n;
		}
		// (no bows any more: an older kit's arrows taken back)
		if (auto* ar = Forms::Ours(gen::kTabletArrow) ? Forms::Ours(gen::kTabletArrow)->As<RE::TESBoundObject>() : nullptr; ar && p->GetItemCount(ar) > 0) {
			p->RemoveItem(ar, p->GetItemCount(ar), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		logger::info("testkit: the Tablet's {} donated weapons", n);
	}
	bool IsTablet(const RE::TESForm* a_w) { return a_w && g_held >= 0 && FormOf(g_held) == a_w; }
	bool IsTabletSpell(const RE::TESForm* a_f)
	{
		auto* f = FormOf(g_held);
		if (!a_f || !f) return false;
		if (f == a_f) return true;   // (the Codex)
		auto* w = f->As<RE::TESObjectWEAP>();
		return w && w->formEnchanting && w->formEnchanting == a_f;
	}
	// (Noah: "one or two tiers above your kit") the class sword's tier, one higher for a worn weapon, two for a battered
	int PowerTier()
	{
		RE::TESObjectWEAP* sword = nullptr;
		switch (Classes::Get()) {
		case 0: sword = Classes::Riftblade(); break;
		case 1: sword = Classes::CarianBlade(); break;
		case 2: sword = Classes::AlchemistKnife(); break;
		case 3: sword = Classes::SpellswordBlade(); break;
		default: break;
		}
		const int kit = std::max(0, Kit::TierOfWeapon(sword));
		return kit + (g_held >= 0 && gen::kTabletWeapons[g_held].battered ? 2 : 1);
	}
	float Share() { return gen::kShareMelee * UpgradeMult(PowerTier()); }
	float Wear() { return g_held >= 0 ? static_cast<float>(g_uses) / std::max(1, gen::kTabletWeapons[g_held].uses) : -1.0f; }
	std::string_view HeldName() { return g_held >= 0 ? gen::kTabletWeapons[g_held].label : std::string_view{}; }

	// (Noah: "the wandering star weapon should be a wand that casts star looking magic" - "Hold to cast, like the Carian Wand")
	// held: a star every half second from the wand's tip, bouncing on to two more of the dead
	namespace
	{
		bool g_starButton = false;
		int StarIndex()
		{
			for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) if (gen::kTabletWeapons[i].id == "wanderstar") return i;
			return -1;
		}
		// (Noah: "This wand should shoot a small golden ball that does a TON of knock back every 5th hit on enemies. Naturally it
		// does base damage that bounces to up to 4 enemies if they're standing close together. The projectile needs to be seen
		// bouncing to each enemy") a ball of golden light from the wand's tip (Fulcimentum's wood wand: its point 63 down its -Y)
		constexpr float kStarTipY = -63.0f;
		int g_starHits = 0;
		void StarHop(const RE::NiPoint3& a_from, const RE::NiPoint3& a_dir, float a_range, float a_share, int a_left, std::shared_ptr<std::vector<RE::FormID>> a_hit)
		{
			Gun::SlashSpec sl;
			sl.model = "CurseOfAlduin\\fx\\star_ball.nif";
			sl.from = a_from, sl.dir = a_dir;
			sl.roll = Gun::SwingRoll(a_dir);
			sl.range = a_range, sl.speed = 1500.0f, sl.radius = 70.0f, sl.scale = 1.0f;
			sl.bolt = true, sl.stopOnHit = true, sl.share = a_share;
			sl.keepFrom = true;   // (from the tip, then from each of the dead it bounces off)
			sl.onHit = [a_share, a_left, a_hit, a_from](RE::Actor* a, bool) {
				if (!a) return;
				a_hit->push_back(a->GetFormID());
				const RE::NiPoint3 at = Mid(a);
				Gun::QueueFx("CurseOfAlduin\\fx\\philosopher_burst.nif", at, 0.35f, 0.0f, 0.6f, false);   // (a small burst of gold)
				Gun::BlastSound("light", at, 0.5f);
				// every fifth of the dead it strikes is thrown far
				if (++g_starHits % 5 == 0) {
					RE::NiPoint3 back = a->GetPosition() - a_from;
					back.z = 0;
					if (back.Length() > 1.0f) back.Unitize();
					const RE::ActorHandle h = a->GetHandle();
					const RE::NiPoint3 push = a->GetPosition() - back * 150.0f - RE::NiPoint3{ 0, 0, 60.0f };
					Tasks::Add([h, push] { if (auto t = h.get(); t && !t->IsDead()) Gun::Fling(t.get(), push, 14.0f); });
					Gun::QueueFx("CurseOfAlduin\\fx\\philosopher_burst.nif", at, 0.9f, 0.0f, 0.8f, false);
				}
				if (a_left <= 1) return;
				// on to the next of the dead standing close by (never one it already struck)
				RE::Actor* next = nullptr;
				for (auto* f : Others(a, a->GetPosition(), 450.0f, 8)) {
					if (std::find(a_hit->begin(), a_hit->end(), f->GetFormID()) == a_hit->end()) { next = f; break; }
				}
				if (!next) return;
				RE::NiPoint3 d = Mid(next) - at;
				const float len = d.Length();
				if (len < 1.0f) return;
				d.Unitize();
				const RE::NiPoint3 from = at + d * std::min(80.0f, len * 0.4f);
				const float range = len + 60.0f;
				const float share = a_share;
				const int left = a_left - 1;
				auto hit = a_hit;
				Tasks::Add([from, d, range, share, left, hit] { StarHop(from, d, range, share, left, hit); });
			};
			Gun::Slash(sl);
		}
		void TickStarWand(RE::PlayerCharacter* a_p, float a_delta)
		{
			static float heldFor = 0, next = 0;
			next = std::max(0.0f, next - a_delta);
			const bool inHand = StarWandInHand();
			const bool held = g_starButton && inHand;
			heldFor = held ? heldFor + a_delta : 0.0f;
			Classes::WandOrb(1, kStarTipY, inHand, held, a_delta);   // (Noah: "The tip of this wand also needs to have the animated blue glowing tip")
			if (!held || heldFor < 0.3f || next > 0.0f) return;
			next = 0.5f;
			const int si = StarIndex();
			const float S = Share() * 0.7f;
			Classes::WandKick(1);
			const RE::NiPoint3 tip = Classes::WandTipAt(kStarTipY);
			Gun::SetSwingFrom(&tip);
			RE::Actor* t = nullptr;
			AimPoint(&t);
			RE::NiPoint3 dir = t ? Mid(t) - tip : Gun::SwingDir();
			Gun::SetSwingFrom(nullptr);
			if (dir.Length() < 1.0f) dir = Gun::SwingDir();
			dir.Unitize();
			StarHop(tip, dir, 2200.0f, S, 4, std::make_shared<std::vector<RE::FormID>>());
			Gun::BlastSound("light", tip, 0.35f);
			if (si >= 0 && si == g_held && !Gun::TestKitOn()) {
				if (--g_uses <= 0) Break(); else Rename(g_held, g_uses);
			}
			(void)a_p;
		}
	}
	bool StarWandInHand()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		const int si = StarIndex();
		auto* f = si >= 0 ? FormOf(si) : nullptr;
		return p && f && p->GetEquippedObject(false) == f && p->AsActorState()->IsWeaponDrawn();
	}
	void StarButton(bool a_down) { g_starButton = a_down; }

	// holding Activate on the tablet (or the weapon risen from it): a ring fills under the crosshair, and full, it acts
	void TickHold(float a_delta)
	{
		RE::TESObjectREFR* on = nullptr;
		if (Present() && (g_phase == Phase::Idle || g_phase == Phase::Offering) && !ItemMenusOpen()) {
			auto* tab = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr;
			if (tab && AimedTablet(RE::PlayerCharacter::GetSingleton()) == tab) on = tab;
		}
		const bool held = on && Party::ActivateHeld();
		if (!held) g_holdLatched = false;
		float frac = -1.0f;
		std::string what;
		if (on && !g_holdLatched) {   // (aimed at: the prompt and its empty ring; held: the ring fills)
			g_holdT = held ? g_holdT + a_delta : 0.0f;
			frac = std::clamp(g_holdT / kHoldTime, 0.0f, 1.0f);
			what = g_phase == Phase::Offering && g_prize >= 0 ? "to take the " + std::string(gen::kTabletWeapons[g_prize].label)
				: g_held >= 0 && Carrying() && g_uses < gen::kTabletWeapons[g_held].uses ? "to mend it  (" + Num(Price()) + ")"
				: "to roll  (" + Num(Price()) + ")";
			if (held && g_holdT >= kHoldTime) {
				g_holdLatched = true, g_holdT = 0.0f, frac = -1.0f;
				g_holdDoneAt = std::chrono::steady_clock::now();
				Act(on);
			}
		} else {
			g_holdT = 0.0f;
		}
		std::scoped_lock l(Hud::Lock());
		auto& h = Hud::Edit();
		h.holdFrac = frac, h.holdText = std::move(what);
	}
	void Tick(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		TickStarWand(p, a_delta);
		g_pclock += a_delta;
		g_slamCool -= a_delta, g_eclipseCool -= a_delta, g_splitCool -= a_delta;
		if (g_salt.size() > 64) std::erase_if(g_salt, [](const auto& e) { return e.second < g_pclock; });
		Night();
		// where it stands: Riverwood first (Noah: "it should start at riverwood"), freed or not
		if (g_town < 0) {
			g_town = HasSpot(0) ? 0 : PickTown(-1), g_rolls = 0, g_moveAfter = std::uniform_int_distribution<int>(4, 8)(g_rng);
			logger::info("tablet: stands in {}", g_town >= 0 ? gen::kTowns[g_town] : "nowhere");
		}
		auto* ref = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr;
		if (g_phase != Phase::Idle) {
			g_t += a_delta;
			if (g_phase == Phase::Rolling && g_t >= kRollTime) {
				g_t = 0.0f;
				auto r = g_ref.get();
				if (g_skull) {   // (Noah: "Yes, the mask rises") the Dragon Priest's mask rises out of the pool and laughs
					if (r) Forms::PlayAt(gen::Sound::shout_quake, r.get());
					g_phase = Phase::Mask;
				} else {
					g_phase = Phase::Offering;
					if (r) {
						Forms::PlayAt(gen::Sound::coffer_land, r.get());
						if (g_gold) Forms::PlayAt(gen::Sound::shout_toll, r.get());   // (a battered win: the tablet rings like a bell)
					}
					Hud::Banner(std::string(g_gold ? "THE TABLET RINGS GOLD - " : "") + std::string(gen::kTabletWeapons[g_prize].label) + "  -  Activate the tablet to take it", 4.0f);
				}
			} else if (g_phase == Phase::Offering && g_t >= kOfferTime) {   // (left too long: it sinks back - nothing returned)
				ClearShow();
				g_phase = Phase::Idle, g_t = 0.0f, g_gold = false, g_prize = -1;
			}
			if (g_phase == Phase::Mask && g_t >= kMaskTime) {   // the essence back, and the tablet goes up its beam to another city
				ClearShow();
				Points::Refund(g_paid);
				const int from = g_town;
				g_moveTo = PickTown(from), g_rolls = 0, g_moveAfter = std::uniform_int_distribution<int>(4, 8)(g_rng);
				g_leaveT = 0.0f, g_arriveT = -1.0f;
				if (auto r = g_ref.get()) Forms::PlayAt(gen::Sound::coffer_charge, r.get());
				Hud::Banner(fmt::format("A DRAGON PRIEST'S LAUGH - the tablet leaves for {}  (+{})", g_moveTo >= 0 ? gen::kTowns[g_moveTo] : "?", Num(g_paid)), 5.0f);
				logger::info("tablet: the mask - leaving {} for {}", from >= 0 ? gen::kTowns[from] : "?", g_moveTo >= 0 ? gen::kTowns[g_moveTo] : "?");
				g_phase = Phase::Idle, g_t = 0.0f, g_skull = false, g_prize = -1;
			}
			if (g_phase == Phase::Offering) if (auto r = g_ref.get()) PoseShow(r.get(), std::clamp(g_t / 1.5f, 0.0f, 1.0f));
			if (g_phase == Phase::Mask) if (auto r = g_ref.get()) PoseShow(r.get(), std::clamp(g_t / 1.2f, 0.0f, 1.0f));
		}
		TickNights(p, a_delta);
		TickHold(a_delta);
		ref = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr;
		if (ref && ref->Is3DLoaded() && ref->GetPosition().GetDistance(p->GetPosition()) < 6000.0f) Animate(ref, a_delta);
		TickLamp(ref, a_delta);
		TickBook(p);
		// (Noah: the test kit has every donated weapon) whichever of them you equip is the one carried - its power, the Codex
		// on your palm, the fists' gloves - at its full uses, never wearing
		if (Gun::TestKitOn() && !ItemMenusOpen()) {
			for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) {
				auto* f = FormOf(i);
				if (!f || i == g_held || (p->GetEquippedObject(false) != f && p->GetEquippedObject(true) != f)) continue;
				if (g_held >= 0) {   // (the one before: its gloves off, the weapon kept)
					if (auto* gl = GlovesOf(g_held); gl && p->GetItemCount(gl) > 0) p->RemoveItem(gl, p->GetItemCount(gl), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
				g_held = i, g_uses = gen::kTabletWeapons[i].uses;
				Rename(i, g_uses);
				if (LeftOf(i) && p->GetEquippedObject(true) != LeftOf(i)) GiveLeft(p, i);
				if (auto* gl = GlovesOf(i)) {
					Forms::Give(p, gl, 1);
					if (auto* em = RE::ActorEquipManager::GetSingleton()) em->EquipObject(p, gl, nullptr, 1, nullptr, false, false, false, true);
				}
				break;
			}
			if (g_held >= 0 && gen::kTabletWeapons[g_held].type == 4) {   // (its arrows kept full)
				if (auto* ar = Forms::Ours(gen::kTabletArrow) ? Forms::Ours(gen::kTabletArrow)->As<RE::TESBoundObject>() : nullptr; ar && p->GetItemCount(ar) < 20) {
					p->AddObjectToContainer(ar, nullptr, gen::kTabletWeapons[g_held].uses, nullptr);
				}
			}
		}
		// a donated bow's uses are its arrows; a weapon you no longer have (sold, dropped, a load) is let go
		if (g_held >= 0 && !Gun::TestKitOn()) {
			auto* f = FormOf(g_held);
			if (gen::kTabletWeapons[g_held].type == 4) {
				auto* ar = Forms::Ours(gen::kTabletArrow) ? Forms::Ours(gen::kTabletArrow)->As<RE::TESBoundObject>() : nullptr;
				if (const int n = ar ? p->GetItemCount(ar) : 0; n != g_uses) {
					g_uses = n;
					if (g_uses <= 0) Break(); else Rename(g_held, g_uses);
				}
			} else if (f && !ItemMenusOpen()) {
				const bool have = f->As<RE::SpellItem>() ? p->HasSpell(f->As<RE::SpellItem>()) : p->GetItemCount(f->As<RE::TESBoundObject>()) > 0;
				if (!have) g_held = -1, g_uses = 0;
			}
		}
		{
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.tabletWear = Wear(), h.tabletName = std::string(HeldName());
		}
	}

	std::vector<std::uint8_t> State()
	{
		return { static_cast<std::uint8_t>(g_town + 1), static_cast<std::uint8_t>(std::clamp(g_rolls, 0, 255)), static_cast<std::uint8_t>(g_moveAfter),
			static_cast<std::uint8_t>(std::clamp(g_steps, 0, 255)), static_cast<std::uint8_t>(g_held + 1), static_cast<std::uint8_t>(g_uses & 0xFF),
			static_cast<std::uint8_t>((g_uses >> 8) & 0xFF) };
	}
	void SetState(const std::uint8_t* a_v, std::size_t a_n)
	{
		g_town = -1, g_rolls = 0, g_moveAfter = 6, g_steps = 0, g_night = -1;
		g_phase = Phase::Idle, g_t = 0.0f, g_gold = false, g_prize = -1, g_held = -1, g_uses = 0;
		g_present = false, g_arriveT = -1.0f, g_leaveT = -1.0f, g_moveTo = -1, g_beacon = {}, g_beaconTown = -1;
		ClearShow();
		if (a_n < 4) return;
		g_town = static_cast<int>(a_v[0]) - 1;
		if (g_town >= static_cast<int>(gen::kTowns.size())) g_town = -1;
		g_rolls = a_v[1], g_moveAfter = std::clamp<int>(a_v[2], 4, 8), g_steps = a_v[3];
		g_night = Save::Get(gen::kAV_round);
		g_held = -1, g_uses = 0;
		if (a_n >= 7) {
			g_held = static_cast<int>(a_v[4]) - 1;
			if (g_held >= static_cast<int>(gen::kTabletWeapons.size())) g_held = -1;
			g_uses = a_v[5] | (a_v[6] << 8);
			if (g_held >= 0) g_uses = std::min(g_uses, gen::kTabletWeapons[g_held].uses);   // (a weapon made rarer since: its fewer uses)
			if (g_held >= 0) Rename(g_held, g_uses);
		}
	}

	// dev: tablet [town|roll|gold]: move it to a city, or roll it now (gold: a battered roll)
	void Dev(std::string_view a_what, int a_town)
	{
		if (a_what == "town" && a_town >= 0 && a_town < static_cast<int>(gen::kTowns.size())) {
			g_town = a_town, g_rolls = 0;
			logger::info("dev: the tablet moved to {}", gen::kTowns[a_town]);
		} else if (a_what == "roll" || a_what == "gold") {
			if (auto* r = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr) {
				g_phase = Phase::Idle;
				Points::Refund(Price());   // (a dev roll is free)
				Act(r);
				if (a_what == "gold" && g_prize >= 0) {
					std::vector<int> b;
					for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) if (gen::kTabletWeapons[i].battered) b.push_back(i);
					g_prize = b[std::uniform_int_distribution<int>(0, static_cast<int>(b.size()) - 1)(g_rng)], g_gold = true;
				}
			}
		}
	}
	// (Noah: "Make Eclipse edge the glass pike", "for 2. use the glass staff #112. 4. use #76 ... 5. use #60", the five new ones
	// from Animated Armoury) a Tablet weapon that wears another mod's weapon (sheets/tablet.json look) takes its models, how it's
	// held and swung, its reach and its sounds; its pedestal shows it
	void AdoptLooks()
	{
		for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) {
			const auto& d = gen::kTabletWeapons[i];
			if (!d.look) continue;
			auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(d.look, d.lookPlugin);
			auto* s = f ? f->As<RE::TESObjectWEAP>() : nullptr;
			auto* w = Forms::Ours(d.form) ? Forms::Ours(d.form)->As<RE::TESObjectWEAP>() : nullptr;
			if (!s || !w) { logger::warn("tablet: {} can't wear {:06X} of {} - is the mod installed?", d.label, d.look, d.lookPlugin); continue; }
			Classes::AdoptLook(w, s);
			// (Noah: "the wandering star weapon should be a wand") Fulcimentum's wood wand, held and cast as a wand (its held cast: TickStarWand)
			if (auto* show = Forms::Ours(d.show) ? Forms::Ours(d.show)->As<RE::TESObjectSTAT>() : nullptr) show->SetModel(s->GetModel());
			logger::info("tablet: the {} takes the look of {}", d.label, s->GetName());
		}
	}

}
