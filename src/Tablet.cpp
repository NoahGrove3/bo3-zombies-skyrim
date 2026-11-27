#include "CurseOfAlduin.h"

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
namespace coa::Tablet
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
		// (Noah, Nov 27 - his pick for the roll, "The Wheel": "The weapons circle the Tablet on a ring of runes, spinning fast and
		// easing to a stop with the won one in front; it floats in to you") six of the weapons riding a ring of runes round the
		// tablet at its pool's height (the won one among them), the ring spun and eased to a stop with it at the front
		std::vector<RE::ObjectRefHandle> g_wheel;
		std::vector<int> g_wheelW;
		std::vector<bool> g_wheelGlow;
		RE::ObjectRefHandle g_wheelRunes;
		float g_wheelTot = 0.0f;    // how far it turns in the roll (radians): whole turns, so the won one ends in front
		float g_wheelOff = 0.0f;    // (the skull's roll: it stops anywhere)
		constexpr int   kWheelN = 6;
		constexpr float kWheelR = 62.0f;   // (tools/tabletwheel.py: the rune band 54-70 round the slab, 28 half-wide)
		int   g_held = -1;          // the donated weapon you carry (kTabletWeapons), -1 none
		int   g_uses = 0;           // its uses left
		int   g_paid = 0;           // the last spin's price (the skull gives it back)

		// the model's measures (tools/tablet.py): the pool's centre on the face (x 0, z Z0 + 42)
		constexpr float kPoolZ = 116.0f;
		// (Noah: "the weapon rolled should dissapear after 10 seconds if not picked up")
		// (Noah: "The tablet of mysteries should feel just like the mystery box from Black Ops 3 Zombies" - "About 5 seconds",
		// "Box style": the won weapon hangs where it landed, turning, and sinks back into the tablet over about 12 seconds)
		constexpr float kRollTime = 5.0f, kOfferTime = 12.0f, kSinkTime = 2.5f;
		// (Noah: "Rise above the top", "Three-quarter size") the weapons rise out of the slab's top (tools/tablet.py: its arch's
		// top at Z1 158) and turn over it, three-quarters of their size
		constexpr float kTopZ = 158.0f;
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
			else if (auto* ar = f->As<RE::TESObjectARMO>()) ar->fullName = nm;   // (the Ward-Shield)
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
			if (d.type == 10) {   // (Nov 27, Noah: the Ward-Shield - "Keep my right-hand weapon") only the left hand's thing goes back to the pack
				if (auto* o = p->GetEquippedObject(true)) if (auto* b = o->As<RE::TESBoundObject>()) em->UnequipObject(p, b, nullptr, 1, nullptr, false, true, false, true);
				if (auto* b = f->As<RE::TESBoundObject>()) {
					Forms::Give(p, b, 1);
					em->EquipObject(p, b, nullptr, 1, nullptr, false, false, false, true);
				}
				p->DrawWeaponMagicHands(true);
				logger::info("tablet: {} taken ({} uses)", d.id, d.uses);
				return;
			}
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
				em->EquipObject(p, b, nullptr, 1, d.type == 4 || d.type == 3 || d.type == 6 || d.type == 8 || d.type == 11 ? nullptr : right, false, false, false, true);
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
		std::string g_bookFor;   // (whose book it is: the Codex's, the Black Book of the Grasp of Apocrypha)
		void TickBook(RE::PlayerCharacter* a_p)
		{
			bool on = false;
			if (g_held >= 0 && gen::kTabletWeapons[g_held].type == 7 && g_bookFor != gen::kTabletWeapons[g_held].id) {
				for (auto& bk : g_book) { if (bk && bk->parent) bk->parent->DetachChild2(bk.get()); bk.reset(); }
				g_bookFor = gen::kTabletWeapons[g_held].id;
			}
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
				if (!bk) bk = Gun::LoadModel(fmt::format("CurseOfAlduin\\tablet\\{}_r.nif", g_bookFor.empty() ? std::string("codex") : g_bookFor));
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
			// (Noah, Nov 27: "3 rarity weapons ... Rare Blue, Purple Epic, Gold Legendary" - "rarer = less often") 60 / 30 / 10;
			// no Legendary before night 8 (his old rule for the battered). Only his lineup rolls: the cut ones are kept as forms only
			const bool late = Gun::TestKitOn() || Save::Get(gen::kAV_round) + 1 >= 8;
			const int w[3] = { 60, 30, late ? 10 : 0 };
			std::vector<int> pool, wt;
			for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) {
				const auto& d = gen::kTabletWeapons[i];
				if (d.pool && i != g_held && w[d.rarity] > 0) pool.push_back(i), wt.push_back(w[d.rarity]);
			}
			if (pool.empty()) return 0;
			// (each rarity's share split among its weapons, so adding weapons to a rarity never changes how often the rarity comes up)
			int per[3] = {}; for (int i : pool) ++per[gen::kTabletWeapons[i].rarity];
			std::vector<double> ww; for (std::size_t k = 0; k < pool.size(); ++k) ww.push_back(static_cast<double>(wt[k]) / per[gen::kTabletWeapons[pool[k]].rarity]);
			std::discrete_distribution<int> pick(ww.begin(), ww.end());
			return pool[pick(g_rng)];
		}
		void ClearReel()
		{
			for (auto& h : g_wheel) if (auto r = h.get()) r->Disable(), r->SetDelete(true);
			g_wheel.clear(), g_wheelW.clear(), g_wheelGlow.clear();
			Gun::RemoveModel(g_wheelRunes);
			g_reel = {}, g_reelAt = -1, g_reelGlow = false;   // (g_reel: the one at the front now - one of the wheel's)
		}
		void ClearShow()
		{
			if (auto r = g_show.get()) r->Disable(), r->SetDelete(true);
			g_show = {}, g_showGlow = false;
			ClearReel();
		}
		// (Noah: "I also need the roll animation to have the glow of the rarity of the weapon it lands on and other weapons
		// rolling by") a weapon's rarity colour: worn green, battered (legendary) gold
		RE::NiColor RarityColour(int a_w)
		{
			if (a_w < 0) return { 1.0f, 1.0f, 1.0f };
			switch (gen::kTabletWeapons[a_w].rarity) {   // (Rare blue, Epic purple, Legendary gold)
			case 2:  return { 1.0f, 0.70f, 0.16f };
			case 1:  return { 0.66f, 0.30f, 1.0f };
			default: return { 0.24f, 0.55f, 1.0f };
			}
		}
		// its model lit from within in that colour (every lit shader of it given its own glow), breathing a little
		void RarityLight(RE::TESObjectREFR* a_r, int a_w, float a_k)
		{
			auto* root = a_r && a_w >= 0 ? a_r->Get3D() : nullptr;
			if (!root) return;
			const RE::NiColor c = RarityColour(a_w);
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
				if (auto* lp = netimmerse_cast<RE::BSLightingShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get())) {
					lp->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kOwnEmit, true);
					if (lp->emissiveColor) *lp->emissiveColor = c;
					lp->emissiveMult = a_k;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		// a weapon's rarity glow: worn green, battered gold
		void Glow(RE::TESObjectREFR* a_r, int a_w, bool& a_done)
		{
			if (a_done || !a_r || a_w < 0 || !a_r->Is3DLoaded()) return;
			const int r = gen::kTabletWeapons[a_w].rarity;
			auto* sh = Forms::Ours(r == 2 ? gen::kTabletGold : r == 1 ? gen::kSoulPurple : gen::kTabletGreen);   // (kTabletGreen is the Rare blue now)
			if (auto* e = sh ? sh->As<RE::TESEffectShader>() : nullptr) a_r->ApplyEffectShader(e, -1.0f);
			a_done = true;
		}
		// (Noah: "Box-style, measured" - "Flat, side-on, still") how each Tablet weapon's model is held on display, measured
		// from the model it shows (its own, or the other mod's it wears - tools/modelconv/nifrange.cpp: each model's reach
		// along x, y and z, a stray scrap of mesh not counted): which of its axes runs along it (to the tablet's right, its
		// head that way), which is its breadth (up: an axe's blade, a scythe's hook), which is its thinnest (toward you); the
		// middle of its reach; its length and breadth
		struct Display { std::string_view id; float m[9]; float c[3]; float len, breadth; };
		constexpr Display kDisplay[] = {
			{ "stormtalons", { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0.63f, -0.03f, 2.10f }, 23.6f, 13.6f },
			{ "gravebell", { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { 0.05f, 18.95f, -0.01f }, 56.4f, 10.6f },
			{ "talons", { 1, 0, 0, 0, 0, -1, 0, 1, 0 }, { 5.13f, 0.24f, 0.85f }, 48.7f, 12.9f },
			{ "riverstone", { 0, 1, 0, 0, 0, -1, -1, 0, 0 }, { -5.29f, 46.70f, -0.04f }, 141.8f, 21.1f },
			{ "wyrmspine", { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { 0.00f, 50.07f, 0.01f }, 155.2f, 11.1f },
			{ "lanternwick", { 0, 1, 0, 0, 0, -1, -1, 0, 0 }, { -0.14f, 19.74f, 0.02f }, 124.8f, 8.8f },
			{ "cinderclaws", { 1, 0, 0, 0, 0, -1, 0, 1, 0 }, { 5.08f, 1.40f, 3.78f }, 33.4f, 14.8f },
			{ "eclipse", { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { 0.00f, 52.87f, 0.00f }, 165.4f, 6.8f },
			{ "wanderstar", { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { 0.23f, 13.03f, -0.04f }, 49.2f, 2.8f },
			{ "frostmourn", { 0, 1, 0, 0, 0, -1, -1, 0, 0 }, { -0.38f, 27.38f, -0.06f }, 84.0f, 20.9f },
			{ "silverscythe", { 0, 1, 0, 0, 0, -1, -1, 0, 0 }, { -11.63f, 7.71f, -0.04f }, 101.6f, 31.8f },
			{ "bonereaper", { 0, 1, 0, 0, 0, -1, -1, 0, 0 }, { -0.54f, 16.11f, 0.28f }, 87.0f, 41.5f },
			{ "wyrmtooth", { 0, 1, 0, 0, 0, -1, -1, 0, 0 }, { -3.24f, 14.19f, 0.00f }, 94.0f, 64.0f },
			{ "bloodmoon", { 0, 1, 0, 0, 0, -1, -1, 0, 0 }, { -6.07f, 10.59f, -0.00f }, 91.7f, 47.0f },
			// (Nov 27: the eleven new ones, measured from Skyrim's own models and ours)
			{ "fushammer", { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { -1.06f, -0.48f, 0.00f }, 83.1f, 25.2f },
			{ "soulrender", { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { 0.02f, 12.99f, -0.05f }, 54.1f, 13.6f },
			{ "chaurus", { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { -0.89f, 21.38f, 0.00f }, 65.2f, 10.1f },
			{ "bound", { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { 0.46f, 17.29f, 0.58f }, 66.1f, 13.7f },
			{ "giantclub", { 0, 1, 0, -1, 0, 0, 0, 0, 1 }, { -0.19f, 46.42f, 0.69f }, 132.2f, 35.3f },
			{ "spriggan", { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { 0.00f, 11.22f, 0.00f }, 56.8f, 16.8f },
			{ "apocrypha", { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0.05f, -6.05f, 2.45f }, 20.7f, 18.1f },
			{ "sweetroll", { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { -0.30f, 0.03f, 7.68f }, 16.5f, 14.7f },
			{ "atronach", { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 1.37f, 0.03f, 0.38f }, 5.5f, 17.3f },
			{ "eggsac", { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { -0.05f, 0.07f, 0.07f }, 8.7f, 11.0f },
			{ "wardshield", { 1, 0, 0, 0, 0, 1, 0, -1, 0 }, { -0.15f, -0.15f, -4.70f }, 48.7f, 48.7f },
		};
		const Display* DisplayOf(int a_w)
		{
			if (a_w < 0 || a_w >= static_cast<int>(gen::kTabletWeapons.size())) return nullptr;
			for (const auto& d : kDisplay) if (d.id == gen::kTabletWeapons[a_w].id) return &d;
			return nullptr;
		}
		// its size on display: every weapon the same length over the slab (1.9 times its width), none taller than 55
		float DisplayScale(int a_w)
		{
			const auto* d = DisplayOf(a_w);
			if (!d) return 0.75f;
			return std::clamp(std::min(105.0f / d->len, 55.0f / std::max(d->breadth, 1.0f)), 0.35f, 2.2f);
		}
		// a weapon held over the slab's top, flat and side-on: its middle a_lift of the way from sunk in the slab to clear
		// over it, turned a_spin about the upright; the model's own transform set straight (a weapon's model points
		// whichever way its maker chose - Euler angles can't hold them all alike)
		void PoseDisplay(RE::TESObjectREFR* a_r, RE::NiAVObject* a_body, int a_w, float a_lift, float a_spin)
		{
			const auto* d = DisplayOf(a_w);
			const float bs = a_body->world.scale, s = DisplayScale(a_w) * bs;
			// (Noah: "displayed in front of the face of the tablet instead of above the tablet. Horizontal, just like the Black
			// Ops 3 mystery box") level with the pool, before the face: it slides out of the face toward you as it comes
			// (a_lift 0: in the slab, 1: clear before it), and back in as it goes
			const RE::NiPoint3 at = a_body->world * RE::NiPoint3{ 0.0f, -4.0f - 30.0f * a_lift, kPoolZ };
			RE::NiMatrix3 D, Z;
			for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) D.entry[i][j] = d ? d->m[i * 3 + j] : (i == j ? 1.0f : 0.0f);
			Z.entry[0][0] = std::cos(a_spin), Z.entry[0][1] = -std::sin(a_spin), Z.entry[0][2] = 0;
			Z.entry[1][0] = std::sin(a_spin), Z.entry[1][1] = std::cos(a_spin), Z.entry[1][2] = 0;
			Z.entry[2][0] = 0, Z.entry[2][1] = 0, Z.entry[2][2] = 1;
			const RE::NiMatrix3 R = a_body->world.rotate * (Z * D);
			const RE::NiPoint3 c = d ? RE::NiPoint3{ d->c[0], d->c[1], d->c[2] } : RE::NiPoint3{};
			const RE::NiPoint3 pos = at - R * (c * s);
			a_r->SetPosition(pos);
			if (auto* o = a_r->Get3D()) {
				o->local.rotate = R, o->local.translate = pos, o->local.scale = s;
				// (the crash of Nov 17, 16:04 - a worker's scene update read nothing: a full update here also carried this
				// model's bounds up through the town's own nodes while a worker job was updating them) only this model and
				// what hangs on it, nothing above
				RE::NiUpdateData u;
				o->UpdateDownwardPass(u, 0);
			}
		}
		// its size on the wheel: each weapon about 46 long, none taller than 30 (six of them round a ring 62 out)
		float WheelScale(int a_w)
		{
			const auto* d = DisplayOf(a_w);
			if (!d) return 0.5f;
			return std::clamp(std::min(46.0f / d->len, 30.0f / std::max(d->breadth, 1.0f)), 0.25f, 2.2f);
		}
		// a weapon a_r out from the slab's middle at the pool's height, a_ang round from the front (toward you), lying along the
		// ring - side-on and level, as it is held on display - turned a_spin more about the upright, a_s its size
		void PoseRing(RE::TESObjectREFR* a_r, RE::NiAVObject* a_body, int a_w, float a_ang, float a_rad, float a_z, float a_s, float a_spin)
		{
			const auto* d = DisplayOf(a_w);
			const float bs = a_body->world.scale, s = a_s * bs;
			const float c = std::cos(a_ang), sn = std::sin(a_ang);
			const RE::NiPoint3 at = a_body->world * RE::NiPoint3{ sn * a_rad, -c * a_rad, kPoolZ + a_z };
			RE::NiMatrix3 D, Z;
			for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) D.entry[i][j] = d ? d->m[i * 3 + j] : (i == j ? 1.0f : 0.0f);
			const float t = a_ang + a_spin;
			Z.entry[0][0] = std::cos(t), Z.entry[0][1] = -std::sin(t), Z.entry[0][2] = 0;
			Z.entry[1][0] = std::sin(t), Z.entry[1][1] = std::cos(t), Z.entry[1][2] = 0;
			Z.entry[2][0] = 0, Z.entry[2][1] = 0, Z.entry[2][2] = 1;
			const RE::NiMatrix3 R = a_body->world.rotate * (Z * D);
			const RE::NiPoint3 cc = d ? RE::NiPoint3{ d->c[0], d->c[1], d->c[2] } : RE::NiPoint3{};
			const RE::NiPoint3 pos = at - R * (cc * s);
			a_r->SetPosition(pos);
			if (auto* o = a_r->Get3D()) {
				o->local.rotate = R, o->local.translate = pos, o->local.scale = s;
				RE::NiUpdateData u;
				o->UpdateDownwardPass(u, 0);   // (only this model: see PoseDisplay)
			}
		}
		// how far round the wheel has turned a_k of the way through the roll: fast at first, easing to a stop
		float WheelTurn(float a_k)
		{
			const float u = std::clamp(a_k, 0.0f, 1.0f);
			return (g_wheelTot + g_wheelOff) * (1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u));
		}
		// the wheel through a roll: its weapons set out round the ring (the won one first), the ring of runes under them, all
		// turned together and eased to a stop with the won one at the front; each in its own rarity's light, the one passing the
		// front brightest
		void TickReel(RE::TESObjectREFR* a_tab, RE::NiAVObject* a_body, float a_k, float a_delta)
		{
			(void)a_delta;
			if (g_phase != Phase::Rolling) { ClearReel(); return; }
			if (g_wheel.empty()) {
				// (the won one first, then five more of the lineup at random - never the same one twice)
				std::vector<int> pool;
				for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) if (gen::kTabletWeapons[i].pool && i != g_prize) pool.push_back(i);
				std::shuffle(pool.begin(), pool.end(), g_rng);
				if (g_prize >= 0) g_wheelW.push_back(g_prize);
				for (int i : pool) { if (static_cast<int>(g_wheelW.size()) >= kWheelN) break; g_wheelW.push_back(i); }
				for (int w : g_wheelW) {
					auto* base = Forms::Ours(gen::kTabletWeapons[w].show) ? Forms::Ours(gen::kTabletWeapons[w].show)->As<RE::TESBoundObject>() : nullptr;
					auto ref = base ? a_tab->PlaceObjectAtMe(base, false) : nullptr;
					g_wheel.push_back(ref ? ref->CreateRefHandle() : RE::ObjectRefHandle{});
					g_wheelGlow.push_back(false);
				}
				g_wheelTot = 2.0f * RE::NI_PI * 3.0f;   // (three whole turns)
				g_wheelOff = g_prize >= 0 ? 0.0f : std::uniform_real_distribution<float>(0.0f, 2.0f * RE::NI_PI)(g_rng);   // (the skull's: anywhere)
				g_wheelRunes = Gun::PlaceModel("CurseOfAlduin\\tablet\\wheel_runes.nif", a_body->world * RE::NiPoint3{ 0, 0, kPoolZ - 16.0f });
			}
			const int n = static_cast<int>(g_wheel.size());
			const float turn = WheelTurn(a_k);
			// (the first half second the weapons come out of the ring's middle to it)
			const float out = std::clamp(g_t / 0.5f, 0.0f, 1.0f), grow = 1.0f - (1.0f - out) * (1.0f - out);
			int front = -1;
			float bestC = -2.0f;
			for (int i = 0; i < n; ++i) {
				auto r = g_wheel[i].get();
				if (!r) continue;
				const float ang = 2.0f * RE::NI_PI * i / std::max(1, n) + turn;
				PoseRing(r.get(), a_body, g_wheelW[i], ang, kWheelR * grow, 0.0f, WheelScale(g_wheelW[i]) * (0.3f + 0.7f * grow), 0.0f);
				bool done = g_wheelGlow[i];
				Glow(r.get(), g_wheelW[i], done);
				g_wheelGlow[i] = done;
				const float c = std::cos(ang);   // (1 at the front)
				if (c > bestC) bestC = c, front = i;
				RarityLight(r.get(), g_wheelW[i], 0.45f + 0.55f * std::max(0.0f, c));
			}
			if (front >= 0) g_reel = g_wheel[front], g_reelAt = g_wheelW[front];
			// the ring of runes turns with them (its band laid level under them)
			const float yaw = a_tab->GetAngleZ() + turn;
			Gun::PoseModel(g_wheelRunes, a_body->world * RE::NiPoint3{ 0, 0, kPoolZ - 16.0f }, RE::NiPoint3{ std::sin(yaw), std::cos(yaw), 0.0f }, a_body->world.scale * (0.4f + 0.6f * grow));
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
			                             : body->world * RE::NiPoint3{ 0.0f, -kFront - 8.0f, kPoolZ - 75.0f + 35.0f * a_rise };   // (Noah: "right in front of the tablet, not at the floor": its grip under the pool, the weapon over its face)
			auto r = g_show.get();
			const std::uint32_t showId = g_phase == Phase::Mask ? gen::kTabletMask : g_prize >= 0 ? gen::kTabletWeapons[g_prize].show : 0;
			if (!r && showId) {
				if (auto* base = Forms::Ours(showId) ? Forms::Ours(showId)->As<RE::TESBoundObject>() : nullptr) {
					if (auto ref = a_tab->PlaceObjectAtMe(base, false)) {
						if (!mask) ref->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::lround(DisplayScale(g_prize) * 100.0f));
						g_show = ref->CreateRefHandle(), r = ref;
					}
				}
			}
			if (!r) return;
			if (mask) r->SetPosition(at);
			else {
				// (the Wheel) it floats in to you off the ring's front, growing to its full size, and hangs there swaying a little;
				// its last seconds it drifts back to the ring and fades into it
				const float sink = std::clamp((g_t - (kOfferTime - kSinkTime)) / kSinkTime, 0.0f, 1.0f);
				const float in = std::clamp(g_t / 0.8f, 0.0f, 1.0f), e = (1.0f - (1.0f - in) * (1.0f - in)) * (1.0f - sink);
				const float sz = WheelScale(g_prize) + (DisplayScale(g_prize) * 0.85f - WheelScale(g_prize)) * e;
				PoseRing(r.get(), body, g_prize, 0.0f, kWheelR + 40.0f * e, 12.0f * e, sz * (1.0f - 0.9f * sink * sink), 0.12f * std::sin(g_t * 0.9f) * e);
			}
			// (the mask faces you out of the pool, turning a little; a weapon turns round)
			if (mask) r->SetAngle({ 0.0f, 0.0f, a_tab->GetAngleZ() + RE::NI_PI + 0.35f * std::sin(g_clock * 1.7f) });
			if (g_phase == Phase::Offering) {
				Glow(r.get(), g_prize, g_showGlow);
				RarityLight(r.get(), g_prize, 0.9f + 0.35f * std::sin(g_clock * 3.0f));   // (the won one brighter, breathing)
			}
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
			const RE::NiPoint3 at = body->world * RE::NiPoint3{ 0.0f, -kFront - 70.0f, kPoolZ + 30.0f };   // (before its face and a little over, where the weapons float)
			if (!lamp) {
				auto* base = Forms::Ours(gen::kTabletLamp) ? Forms::Ours(gen::kTabletLamp)->As<RE::TESBoundObject>() : nullptr;
				if (auto l = base ? a_tab->PlaceObjectAtMe(base, false) : nullptr) l->SetPosition(at), g_lamp = l->CreateRefHandle(), lamp = l;
			} else if (lamp->GetPosition().GetDistance(at) > 20.0f) {
				lamp->SetPosition(at);
			}
			// (its light the colour of the weapon over the slab: green for a worn one rolling by, gold for a legendary)
			const int lw = g_phase == Phase::Offering ? g_prize : g_phase == Phase::Rolling ? g_reelAt : -1;
			if (auto* xl = lamp ? lamp->extraList.GetByType<RE::ExtraLight>() : nullptr; xl && xl->lightData && xl->lightData->light) {
				const RE::NiColor c = RarityColour(lw);
				const float k = lw >= 0 ? 1.25f : 1.0f;
				xl->lightData->light->GetLightRuntimeData().diffuse = RE::NiColor{ c.red * k, c.green * k, c.blue * k };
			}
			static float next = 0.0f;
			if ((next -= a_delta) > 0.0f) return;
			next = 0.45f;
			const int w = lw;
			auto sh = g_phase == Phase::Offering ? g_show.get() : g_reel.get();
			if (w >= 0 && sh) {
				const auto* o = sh->Get3D();
				const RE::NiPoint3 mid = o && o->worldBound.radius > 1.0f ? o->worldBound.center : sh->GetPosition() + RE::NiPoint3{ 0, 0, 40.0f };
				Gun::Motes(gen::kTabletWeapons[w].rarity == 2 ? 8 : gen::kTabletWeapons[w].rarity == 1 ? 9 : 1, mid, g_phase == Phase::Offering ? 0.45f : 0.25f, 1.1f);
			}
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
				const float amp = 0.0f;   // (the box-style roll: the slab holds still under the weapons; was 0.25 + 0.55k)
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
		ClearReel(), g_reelOrder.clear(), g_reelStep = 0, g_reelNext = 0.0f, g_wheelTot = 0.0f;
		// (Noah: after a random 4-8 rolls the skull can come: the essence back, and the tablet moves)
		g_skull = g_rolls > g_moveAfter;
		g_prize = g_skull ? -1 : Pick();
		g_gold = !g_skull && gen::kTabletWeapons[g_prize].rarity == 2;   // (a Legendary win rings the bell)
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
		if (t == 5 || t == 7 || t == 9 || t == 10) return;   // (casters: their casts count; the throwables their throws, the Ward-Shield the blows it stops)
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
	// (Nov 27: the eleven new ones - Noah's picks) what they leave on the dead, each until its time (g_pclock)
	namespace
	{
		std::unordered_map<RE::FormID, float> g_poison;   // (the Chaurus Fang: poisoned - they burst into acid when they die)
		std::unordered_map<RE::FormID, float> g_brand;    // (the Mace of the Bound: branded - they burst into shadowfire)
		std::unordered_map<RE::FormID, float> g_webbed;   // (the Egg Sac: webbed - they take double)
		int   g_fusKills = 0;        // (Voice of the Mountain: kills toward FUS RO DAH)
		bool  g_fusArmed = false;
		std::vector<RE::TESNPC*> g_souls;   // (Soulrender: the souls in its gem - who they were)
		float g_throwCool = 0, g_bashCool = 0, g_heavyCool = 0, g_bugs = 0;
		constexpr int kSoulsFull = 10;   // (Noah: "10 kills, 3 warriors")
		bool Marked(const std::unordered_map<RE::FormID, float>& a_m, RE::Actor* a_a)
		{
			const auto it = a_a ? a_m.find(a_a->GetFormID()) : a_m.end();
			return it != a_m.end() && it->second > g_pclock;
		}
		void Mark(std::unordered_map<RE::FormID, float>& a_m, RE::Actor* a_a, float a_secs)
		{
			if (!a_a) return;
			if (a_m.size() > 200) std::erase_if(a_m, [](const auto& e) { return e.second < g_pclock; });
			a_m[a_a->GetFormID()] = g_pclock + a_secs;
		}
		// the dead within a_range ahead of a point, inside the cone whose half-angle has that cosine
		std::vector<RE::Actor*> Cone(const RE::NiPoint3& a_from, const RE::NiPoint3& a_fwd, float a_range, float a_cos)
		{
			std::vector<RE::Actor*> out;
			for (auto* f : Gun::FoesNear(a_from + a_fwd * (a_range * 0.5f), a_range)) {
				if (!f || f->IsDead()) continue;
				RE::NiPoint3 d = f->GetPosition() - a_from;
				d.z = 0;
				const float l = d.Length();
				if (l > a_range) continue;
				if (l > 40.0f && (d.x * a_fwd.x + d.y * a_fwd.y) / l < a_cos) continue;
				out.push_back(f);
			}
			return out;
		}
		RE::NiPoint3 Fwd(RE::Actor* a)
		{
			const float z = a->GetAngleZ();
			return { std::sin(z), std::cos(z), 0.0f };
		}
		// the Tablet weapon in your hands now (-1: none, or it is put away in the pack)
		int InHand()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* f = FormOf(g_held);
			if (!p || !f) return -1;
			return p->GetEquippedObject(false) == f || p->GetEquippedObject(true) == f ? g_held : -1;
		}
		void SpendUse()
		{
			if (g_held < 0 || Gun::TestKitOn()) return;   // (the test kit's: they never wear)
			if (--g_uses <= 0) Break(); else Rename(g_held, g_uses);
		}
	}
	float TakenMult(RE::Actor* a_actor)
	{
		if (!a_actor) return 1.0f;
		const auto it = g_salt.find(a_actor->GetFormID());
		float m = it != g_salt.end() && it->second > g_pclock ? 1.3f : 1.0f;
		if (Marked(g_webbed, a_actor)) m *= 2.0f;   // (Noah: "Webbed ones take double damage")
		return m;
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
		Gun::FxScope fxs("tab_" + std::string(id) + (a_power ? "_heavy" : "_hit"));   // (Noah, 2026-11-25: each weapon's effects its own)
		if (g_countFor != held) g_countFor = held, g_count = 0;   // (a new weapon counts afresh)
		++g_count;
		if (id == "stormtalons") {   // every 3rd hit jumps lightning to 2 more; a heavy hit stuns the one struck
			if (g_count % 3 == 0) for (auto* o : Others(a_target, tp, 420.0f, 2)) { Gun::LightningArc(Mid(a_target), Mid(o), 0.0f); Gun::HitShare(o, S * 0.6f); }
			// (Noah, Nov 26: "Stormtalons give ice instead of lightning") the heavy hit's stun held still with no frost on it -
			// it was Freeze, which coats the one struck in ice - and a shock burst with an arc to the ground
			if (a_power) {
				Director::Web(a_target->GetPosition(), 45.0f, 1.5f, 97.0f, false);
				Gun::ElemBurst(2, Mid(a_target), 0.8f, 0.0f, 0.6f);
				Gun::LightningArc(Mid(a_target) + RE::NiPoint3{ 0, 0, 90.0f }, Mid(a_target), 0.0f);
			}
		} else if (id == "talons") {   // the bleed runs twice as deep in the dead; each kill heals you a little
			Gun::Burn(a_target, S * 0.08f, 4.0f, -1);
			// (Noah, Nov 26: "Talons of Woe have no effects") its burst was drawn at 0.4 of a 0.17 model - a speck inside the
			// body: full size now, and the workshop's Blood Mist pick (Vampiric Drain, blood explosion, mist) in fxmap.ini
			Gun::ElemBurst(11, Mid(a_target), a_power ? 1.25f : 1.0f, 0.0f, 0.6f);
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
		// ---- (Nov 27, build 3: the eleven Noah added) each one's light; their heavies go off on the swing itself (OnSwing)
		} else if (id == "fushammer") {   // (Voice of the Mountain) every hit staggers the dead back a step
			Gun::Knock(a_target, 0.6f);
			Gun::ElemBurst(3, Mid(a_target), 0.4f, 0.0f, 0.5f);
		} else if (id == "soulrender") {   // (its kills fill the gem: OnDeath) a wisp of the soul it tears at
			Gun::ElemBurst(6, Mid(a_target), 0.35f, 0.0f, 0.5f);
		} else if (id == "chaurus") {   // (Chaurus Fang) hits poison the dead
			Gun::Burn(a_target, S * 0.06f, 5.0f, 3);
			Mark(g_poison, a_target, 6.0f);
			Gun::Puddle(3, tp, 0.25f, 1.5f);
		} else if (id == "bound") {   // (Mace of the Bound) hits brand the dead with a violet mark
			Mark(g_brand, a_target, 8.0f);
			if (auto* sh = Forms::Ours(gen::kSoulPurple) ? Forms::Ours(gen::kSoulPurple)->As<RE::TESEffectShader>() : nullptr) a_target->ApplyEffectShader(sh, 8.0f);
			Gun::ElemBurst(6, Mid(a_target), 0.35f, 0.0f, 0.5f);
		} else if (id == "giantclub") {   // (Giant's Toe Club) every hit sends the one struck flying into the sky
			if (p) {
				RE::NiPoint3 away = tp - p->GetPosition();
				away.z = 0;
				if (away.Length() > 1.0f) away.Unitize();
				const RE::ActorHandle h = a_target->GetHandle();
				const RE::NiPoint3 from = tp - away * 40.0f;
				Tasks::Add([h, from] { if (auto t = h.get(); t && !t->IsDead()) Gun::Fling(t.get(), from, 34.0f); });
			}
			Gun::ElemBurst(3, Mid(a_target), 0.5f, 0.0f, 0.5f);
		} else if (id == "spriggan") {   // (Spriggan's Heartroot) hits heal you a little
			if (p) {
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.015f);
			}
			Gun::Puddle(3, tp, 0.2f, 1.0f);
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
		} else if (id == "apocrypha") {   // (Nov 27, Noah: "Every cast, every 5th ring") a tentacle lashes up at the one you aim at;
			// every fifth cast a ring of them bursts round you and drags the dead within 6 m under for 5 s
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (g_count % 5 == 0 && p) {
				const RE::NiPoint3 me = p->GetPosition();
				for (int k = 0; k < 8; ++k) {
					const float a = 2.0f * RE::NI_PI * k / 8.0f;
					Gun::FxBlast("tentacle", me + RE::NiPoint3{ std::cos(a) * 330.0f, std::sin(a) * 330.0f, 0.0f }, 0.06f * k);
				}
				Director::Web(me, 600.0f, 5.0f, 97.0f, false);
				for (auto* o : Gun::FoesNear(me, 600.0f)) Gun::Burn(o, S * 0.3f, 5.0f, 3);
				Hud::Banner("Hermaeus Mora drags them under", 1.5f);
			} else {
				RE::Actor* t = nullptr;
				RE::NiPoint3 at = AimPoint(&t);
				if (!t) { auto v = Gun::FoesNear(at, 300.0f); if (!v.empty()) t = v.front(), at = t->GetPosition(); }
				Gun::FxBlast("tentacle", at);
				if (t) {
					Gun::HitShare(t, S * 1.6f);
					Gun::Knock(t, 0.6f);
				}
				Director::Web(at, 140.0f, 1.5f, 97.0f, false);
			}
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
	// ---- (Nov 27, build 3) the eleven new ones' heavies: on the swing itself, hit or miss
	void OnSwing(bool a_power)
	{
		const int w = InHand();
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (w < 0 || !p || !a_power || g_heavyCool > 0.0f) return;
		const std::string_view id = gen::kTabletWeapons[w].id;
		const float S = Share();
		const RE::NiPoint3 me = p->GetPosition(), fwd = Fwd(p);
		Gun::FxScope fxs("tab_" + std::string(id) + "_heavy");
		if (id == "fushammer") {
			g_heavyCool = 0.6f;
			if (g_fusArmed) {   // FUS RO DAH: every one of the dead 12 m ahead hurled off their feet (always more than a heavy)
				g_fusArmed = false, g_fusKills = 0;
				for (int k = 0; k < 4; ++k) Gun::FxBlast("shoutarea", me + fwd * (150.0f + 260.0f * k) + RE::NiPoint3{ 0, 0, 60.0f }, 0.06f * k);
				Forms::PlayAt(gen::Sound::shout_roar, p);
				for (auto* o : Cone(me, fwd, 1200.0f, 0.5f)) {
					Gun::HitShare(o, S * 1.5f);
					const RE::ActorHandle h = o->GetHandle();
					const float d = o->GetPosition().GetDistance(me);
					Tasks::Later(d / 2400.0f, [h, me] { if (auto t = h.get(); t && !t->IsDead()) Gun::Fling(t.get(), me, 40.0f); });
				}
				Hud::Banner("FUS RO DAH!", 1.5f);
			} else {   // FUS: a short roaring cone (5 m) that knocks down the dead before you
				Gun::FxBlast("shoutarea", me + fwd * 160.0f + RE::NiPoint3{ 0, 0, 60.0f });
				for (auto* o : Cone(me, fwd, 500.0f, 0.6f)) {
					Gun::HitShare(o, S * 0.8f);
					const RE::ActorHandle h = o->GetHandle();
					Tasks::Add([h, me] { if (auto t = h.get(); t && !t->IsDead()) Gun::Fling(t.get(), me, 12.0f); });
				}
			}
		} else if (id == "soulrender") {   // with a full gem: three of its souls rise as spectral warriors beside you for 12 s
			if (static_cast<int>(g_souls.size()) < kSoulsFull) return;
			g_heavyCool = 1.0f;
			const float hp = Director::RoundHealth() * gen::kRounds.hpScale * 2.5f;
			for (int k = 0; k < 3; ++k) {
				auto* base = g_souls[g_souls.size() - 1 - k];
				const float a = p->GetAngleZ() + (k - 1) * 0.9f;
				const RE::NiPoint3 at = me + RE::NiPoint3{ std::sin(a) * 170.0f, std::cos(a) * 170.0f, 0.0f };
				Gun::FxBlast("dark", at + RE::NiPoint3{ 0, 0, 50.0f }, 0.1f * k);
				Tasks::Later(0.25f + 0.1f * k, [base, at, hp] { Classes::RaiseFor(base, at, 12.0f, hp, 3, true); });
			}
			g_souls.clear();
			Hud::Banner("The souls in Soulrender rise", 1.5f);
		} else if (id == "chaurus") {   // a spray of venom in a cone
			g_heavyCool = 0.6f;
			Gun::FxBlast("spiderspit", me + fwd * 140.0f + RE::NiPoint3{ 0, 0, 70.0f });
			Gun::FxBlast("poisongas", me + fwd * 260.0f);
			for (auto* o : Cone(me, fwd, 450.0f, 0.6f)) {
				Gun::Burn(o, S * 0.08f, 5.0f, 3);
				Mark(g_poison, o, 6.0f);
			}
		} else if (id == "bound") {   // a slam that brands every one of the dead within 6 m
			g_heavyCool = 0.8f;
			Gun::FxBlast("giantslam", me);
			Gun::Ring("purple", me, 40.0f, 600.0f, 0.6f);
			Gun::PurpleFire(me, 0.6f, 0.0f, 0.8f);
			auto* sh = Forms::Ours(gen::kSoulPurple) ? Forms::Ours(gen::kSoulPurple)->As<RE::TESEffectShader>() : nullptr;
			for (auto* o : Gun::FoesNear(me, 600.0f)) {
				Mark(g_brand, o, 8.0f);
				if (sh) o->ApplyEffectShader(sh, 8.0f);
			}
		} else if (id == "giantclub") {   // a ground stomp that knocks down every one of the dead within 8 m
			g_heavyCool = 0.8f;
			Gun::FxBlast("giantslam", me);
			Gun::Ring("white", me, 40.0f, 800.0f, 0.7f);
			Gun::AreaHurt(me, 800.0f, S * 0.9f, "");
			for (auto* o : Gun::FoesNear(me, 800.0f)) Gun::Knock(o, 1.0f);
		} else if (id == "spriggan") {   // roots burst from the ground and hold every one of the dead within 6 m for 4 s
			g_heavyCool = 0.8f;
			Director::Web(me, 600.0f, 4.0f, 97.0f, false);
			int n = 0;
			for (auto* o : Gun::FoesNear(me, 600.0f)) if (n++ < 10) Gun::Puddle(3, o->GetPosition(), 0.45f, 4.0f);
			Gun::Puddle(3, me, 1.2f, 4.0f);
		}
	}
	// one of the dead fell: what the new ones leave on them goes off; your kills with one in hand counted
	void OnDeath(RE::Actor* a_dying, bool a_mine)
	{
		if (!a_dying) return;
		const RE::NiPoint3 at = a_dying->GetPosition();
		const float S = Share();
		if (Marked(g_poison, a_dying)) {   // (the Chaurus Fang) the poisoned burst into acid, poisoning the ones beside them - a chain
			g_poison.erase(a_dying->GetFormID());
			Gun::FxScope fxs("tab_chaurus_hit");
			Gun::Puddle(3, at, 0.7f, 3.0f);
			Gun::FxBlast("poisongas", at);
			for (auto* o : Others(a_dying, at, 260.0f, 6)) {
				Gun::Burn(o, S * 0.06f, 5.0f, 3);
				Mark(g_poison, o, 6.0f);
			}
		}
		if (Marked(g_brand, a_dying)) {   // (the Mace of the Bound) a branded one that dies bursts into violet shadowfire
			g_brand.erase(a_dying->GetFormID());
			Gun::FxScope fxs("tab_bound_hit");
			Gun::PurpleFire(at, 0.9f, 0.0f, 1.2f);
			Gun::AreaHurt(at + RE::NiPoint3{ 0, 0, 40.0f }, 280.0f, S * 0.6f, "");
		}
		g_webbed.erase(a_dying->GetFormID());
		if (!a_mine) return;
		const int w = InHand();
		if (w < 0) return;
		const std::string_view id = gen::kTabletWeapons[w].id;
		if (id == "fushammer" && !g_fusArmed && ++g_fusKills >= 20) {
			g_fusArmed = true;
			Hud::Banner("Voice of the Mountain: FUS RO DAH is ready - your next heavy", 3.0f);
		} else if (id == "soulrender" && static_cast<int>(g_souls.size()) < kSoulsFull) {
			if (auto* base = Classes::RealBase(a_dying); base && !Director::IsBoss(a_dying)) {
				g_souls.push_back(base);
				Gun::ElemBurst(6, at + RE::NiPoint3{ 0, 0, 70.0f }, 0.5f, 0.0f, 0.6f);
				if (static_cast<int>(g_souls.size()) == kSoulsFull) {
					if (auto* p = RE::PlayerCharacter::GetSingleton()) Forms::PlayAt(gen::Sound::charge_ready, p);
					Hud::Banner("Soulrender's gem is full - a heavy attack lets them out", 3.0f);
				}
			}
		}
	}
	// (the Dragonguard Ward-Shield) a blow stopped on it from the front
	void OnWardBlock()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (p) Gun::ElemBurst(4, p->GetPosition() + Fwd(p) * 50.0f + RE::NiPoint3{ 0, 0, 90.0f }, 0.3f, 0.0f, 0.4f);
		SpendUse();
	}
	// (its power bash) a shield charge: you are carried 6 m ahead and the dead in your path are flung aside
	void OnSwingTag(std::string_view a_tag)
	{
		if (a_tag != "bashpowerstart" && !(a_tag == "bashrelease" && Gun::PlayerPowerAttack())) return;
		const int w = InHand();
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (w < 0 || !p || gen::kTabletWeapons[w].type != 10 || g_bashCool > 0.0f) return;
		g_bashCool = 1.5f;
		const RE::NiPoint3 me = p->GetPosition(), fwd = Fwd(p);
		const float S = Share();
		Gun::FxScope fxs("tab_wardshield_heavy");
		Movement::Shove(fwd * 950.0f, 0.45f);
		Gun::ElemBurst(4, me + fwd * 60.0f + RE::NiPoint3{ 0, 0, 80.0f }, 0.8f, 0.0f, 0.6f);
		for (auto* o : Cone(me, fwd, 650.0f, 0.75f)) {
			const RE::ActorHandle h = o->GetHandle();
			const float d = o->GetPosition().GetDistance(me);
			Tasks::Later(std::max(0.0f, d - 80.0f) / 950.0f, [h, fwd, S] {
				auto t = h.get();
				auto* pl = RE::PlayerCharacter::GetSingleton();
				if (!t || t->IsDead() || !pl) return;
				Gun::HitShare(t.get(), S * 0.9f);
				// (flung aside: away from your path, and on)
				RE::NiPoint3 side = t->GetPosition() - pl->GetPosition();
				side.z = 0;
				const RE::NiPoint3 from = t->GetPosition() - (side.Length() > 1.0f ? side * (1.0f / side.Length()) : fwd) * 60.0f - fwd * 40.0f;
				Gun::Fling(t.get(), from, 18.0f);
				Gun::ElemBurst(4, t->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.45f, 0.0f, 0.4f);
			});
		}
	}
	// ---- the throwables (the Sweetroll, Lil' Atronach, the Egg Sac): held like the Alchemist's flasks, thrown underhand
	bool ThrowInHand(bool a_left)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* f = FormOf(g_held);
		return p && f && gen::kTabletWeapons[g_held].type == 9 && p->AsActorState()->IsWeaponDrawn() && p->GetEquippedObject(a_left) == f;
	}
	namespace
	{
		const char* ThrowModel(std::string_view a_id)
		{
			if (a_id == "sweetroll") return "Clutter\\Ingredients\\SweetRoll01.nif";
			if (a_id == "atronach") return "Clutter\\SoulGem\\SoulGemPetty01.nif";
			return "Clutter\\Ingredients\\SpiderEgg01.nif";
		}
		// (the Sweetroll) where it lands it sits; every one of the dead nearby runs to it; after 6 s it explodes in a fireball
		void Sweetroll(const RE::NiPoint3& a_at, float S)
		{
			auto h = Gun::PlaceModel(ThrowModel("sweetroll"), a_at);
			Gun::PoseModel(h, a_at, {}, 1.4f);
			if (auto r = h.get()) {
				auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
				int n = 0;
				for (auto* o : Gun::FoesNear(a_at, 1600.0f)) {
					if (!vm || n++ >= 24) break;
					const auto hd = vm->GetObjectHandlePolicy()->GetHandleForObject(o->GetFormType(), o);
					RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
					vm->DispatchMethodCall(hd, "Actor", "PathToReference", RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(r.get()), 1.0f), none);
				}
			}
			Gun::Motes(4, a_at, 0.3f, 6.0f);
			Tasks::Later(6.0f, [h, a_at, S]() mutable {
				Gun::RemoveModel(h);
				Gun::FxScope fxs("tab_sweetroll_hit");
				Gun::FxBlast("fireball", a_at + RE::NiPoint3{ 0, 0, 40.0f });
				Gun::AreaHurt(a_at + RE::NiPoint3{ 0, 0, 40.0f }, 450.0f, S * 3.0f, "");
				for (auto* o : Gun::FoesNear(a_at, 450.0f)) Gun::Burn(o, S * 0.15f, 4.0f, 0);
			});
		}
		// (Lil' Atronach) a knee-high Flame Atronach pops out and blasts the dead round it with fire for 10 s
		void Atronach(const RE::NiPoint3& a_at, float S)
		{
			Gun::FxBlast("atrofire", a_at + RE::NiPoint3{ 0, 0, 30.0f });
			auto* base = RE::TESForm::LookupByID<RE::TESNPC>(0x000204C0);   // (Skyrim's Flame Atronach)
			const float hp = Director::RoundHealth() * gen::kRounds.hpScale * 2.0f;
			auto* a = base ? Classes::RaiseFor(base, a_at, 10.0f, hp, 4, false) : nullptr;
			if (!a) return;
			a->SetScale(0.45f);
			const RE::ActorHandle h = a->GetHandle();
			for (int k = 1; k <= 10; ++k) {   // (its spin: a burst of fire round it every second)
				Tasks::Later(static_cast<float>(k), [h, S, k] {
					auto t = h.get();
					if (!t || t->IsDead() || t->IsDisabled()) return;
					const RE::NiPoint3 at = t->GetPosition();
					Gun::FxScope fxs("tab_atronach_hit");
					Gun::ElemBurst(0, at + RE::NiPoint3{ 0, 0, 40.0f }, 0.6f, 0.0f, 0.5f);
					Gun::AreaHurt(at + RE::NiPoint3{ 0, 0, 40.0f }, 300.0f, S * 0.5f, "");
					if (k == 10) Gun::FxBlast("atrofire", at + RE::NiPoint3{ 0, 0, 30.0f });   // (it dances when it's done)
				});
			}
		}
		// (the Egg Sac) it bursts into webs that hold the dead for 6 s while spiderlings bite them; the webbed take double
		void EggSac(const RE::NiPoint3& a_at, float S)
		{
			Gun::FxBlast("spiderspit", a_at + RE::NiPoint3{ 0, 0, 30.0f });
			Gun::Puddle(3, a_at, 0.9f, 6.0f);
			Director::Web(a_at, 350.0f, 6.0f, 97.0f, false);
			for (auto* o : Gun::FoesNear(a_at, 350.0f)) {
				Mark(g_webbed, o, 6.0f);
				Gun::Burn(o, S * 0.08f, 6.0f, 3);
			}
		}
		void ThrowRelease(bool a_left)
		{
			const int w = g_held;
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (w < 0 || !p || !cam || !cam->cameraRoot || gen::kTabletWeapons[w].type != 9) return;
			const std::string id(gen::kTabletWeapons[w].id);
			const auto& cw = cam->cameraRoot->world;
			RE::NiPoint3 dir{ cw.rotate.entry[0][1], cw.rotate.entry[1][1], cw.rotate.entry[2][1] };
			dir.Unitize();
			RE::NiPoint3 from = cw.translate + dir * 45.0f + RE::NiPoint3{ 0.0f, 0.0f, -14.0f }, aim = dir;
			// (out of the hand that threw it - in third person at the crosshair's point ahead, as the flasks fly)
			if (auto* root = p->Get3D(cam->IsInFirstPerson())) {
				if (auto* hand = root->GetObjectByName(a_left ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]"); hand && hand->world.translate.GetDistance(p->GetPosition()) < 250.0f) {
					from = hand->world.translate + dir * 10.0f;
					if (!cam->IsInFirstPerson()) {
						aim = (cw.translate + dir * 1800.0f) - from;
						if (aim.Length() > 1e-3f) aim.Unitize(); else aim = dir;
					}
				}
			}
			const float S = Share();
			Gun::FxScope fxs("tab_" + id + "_hit");
			Gun::Lob(ThrowModel(id), from, aim * 1500.0f + RE::NiPoint3{ 0.0f, 0.0f, 260.0f }, id == "eggsac" ? 1.6f : 1.3f, [id, S](const RE::NiPoint3& land) {
				if (id == "sweetroll") Sweetroll(land, S);
				else if (id == "atronach") Atronach(land, S);
				else EggSac(land, S);
			}, 8.0f, 900.0f);
			SpendUse();
		}
	}
	void ThrowPress(bool a_left)
	{
		if (Gun::Throwing() || g_throwCool > 0.0f || !ThrowInHand(a_left)) return;
		g_throwCool = 1.0f;
		Gun::StartThrow(a_left, [a_left]() { ThrowRelease(a_left); });
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
			if (auto* l = LeftOf(i); l && p->GetItemCount(l) <= 0) p->AddObjectToContainer(l, nullptr, 1, nullptr);   // (and its left-hand twin)
			++n;
		}
		// (no bows any more: an older kit's arrows taken back)
		if (auto* ar = Forms::Ours(gen::kTabletArrow) ? Forms::Ours(gen::kTabletArrow)->As<RE::TESBoundObject>() : nullptr; ar && p->GetItemCount(ar) > 0) {
			p->RemoveItem(ar, p->GetItemCount(ar), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		logger::info("testkit: the Tablet's {} donated weapons", n);
	}
	// (Noah, Nov 26: "the Tester's Kit gives only one claw") a claw equipped from the pack - the kit's, or one put away and taken
	// out again - brings its left-hand twin into the other hand, as the tablet's own hand-over does
	void PairClaw(RE::FormID a_right)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) {
			auto* f = FormOf(i);
			auto* l = LeftOf(i);
			if (!f || !l || f->GetFormID() != a_right) continue;
			if (p->GetEquippedObject(false) != f || p->GetEquippedObject(true) == l) return;
			GiveLeft(p, i);
			return;
		}
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
		// bouncing to each enemy") a ball of golden light from the wand's tip
		// (Noah, 2026-11-24: "Tip animation at the very tip of the wand ... is behind my character, not at the wand's tip") Fulcimentum's
		// wood wand (SSS\WoodWand1.nif) runs up its +Y: its point 37.7 out, the feathers at its foot (-15) - the orb and the casts
		// from there
		constexpr float kStarTipY = 37.5f;
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
		g_throwCool -= a_delta, g_bashCool -= a_delta, g_heavyCool -= a_delta;
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
			if (g_phase == Phase::Offering) if (auto r = g_ref.get()) PoseShow(r.get(), 1.0f);
			if (g_phase == Phase::Mask) if (auto r = g_ref.get()) PoseShow(r.get(), std::clamp(g_t / 1.2f, 0.0f, 1.0f));
		}
		TickNights(p, a_delta);
		TickHold(a_delta);
		ref = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr;
		if (ref && ref->Is3DLoaded() && ref->GetPosition().GetDistance(p->GetPosition()) < 6000.0f) Animate(ref, a_delta);
		TickLamp(ref, a_delta);
		TickBook(p);
		// (Spriggan's Heartroot: "Swarms of glowing insects circle it") motes of light round your hands while it's drawn
		if (const int w = InHand(); w >= 0 && gen::kTabletWeapons[w].id == "spriggan" && p->AsActorState()->IsWeaponDrawn() && (g_bugs -= a_delta) <= 0.0f) {
			g_bugs = 0.7f;
			Gun::Motes(4, p->GetPosition() + Fwd(p) * 40.0f + RE::NiPoint3{ 0, 0, 90.0f }, 0.18f, 1.2f);
		}
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
