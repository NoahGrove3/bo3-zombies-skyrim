#include "BO3Z.h"

// (Noah: "something like the mystery box from call of duty zombies" - the Tablet of Mysteries; docs/TABLET_OF_MYSTERIES.md)
// The tablet stands at one freed city at a time (its spot: the city's tablet location, src/Shops.cpp places it there and
// nowhere else) and is animated here, on its own model (tools/tablet.py), by its nodes' names:
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
		enum class Phase { Idle, Rolling, Offering } g_phase = Phase::Idle;
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
		int   g_held = -1;          // the donated weapon you carry (kTabletWeapons), -1 none
		int   g_uses = 0;           // its uses left
		int   g_paid = 0;           // the last spin's price (the skull gives it back)

		// the model's measures (tools/tablet.py): the pool's centre on the face (x 0, z Z0 + 42)
		constexpr float kPoolZ = 116.0f;
		constexpr float kRollTime = 3.6f, kOfferTime = 12.0f;

		int Freed()
		{
			int n = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		// a freed city for it (not the one it leaves, when there is another)
		int PickTown(int a_not)
		{
			std::vector<int> ok;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				if (Curse::Freed(t) && t != a_not) ok.push_back(t);
			}
			if (ok.empty()) return Curse::Freed(a_not) ? a_not : -1;
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
		void Rename(int a_w, int a_uses)
		{
			auto* f = FormOf(a_w);
			if (!f) return;
			const auto& d = gen::kTabletWeapons[a_w];
			const int   pct = std::clamp(static_cast<int>(std::ceil(100.0f * a_uses / std::max(1, d.uses))), 0, 100);
			const std::string nm = fmt::format("{} ({}%)", d.label, pct);
			if (auto* w = f->As<RE::TESObjectWEAP>()) w->fullName = nm;
			else if (auto* sp = f->As<RE::SpellItem>()) sp->fullName = nm;
		}
		// the donated weapon taken off you and out of your pack (and the bows' arrows)
		void TakeAway(int a_w)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* f = FormOf(a_w);
			if (!p || !f) return;
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
				em->EquipObject(p, b, nullptr, 1, d.type == 4 || d.type == 3 || d.type == 6 ? nullptr : right, false, false, false, true);
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
			const bool battered = std::uniform_int_distribution<int>(0, 99)(g_rng) < 15;
			std::vector<int> pool;
			for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) {
				if (gen::kTabletWeapons[i].battered == battered && i != g_held) pool.push_back(i);
			}
			if (pool.empty()) return 0;
			return pool[std::uniform_int_distribution<int>(0, static_cast<int>(pool.size()) - 1)(g_rng)];
		}
		void ClearShow()
		{
			if (auto r = g_show.get()) r->Disable(), r->SetDelete(true);
			g_show = {};
		}
		// the won weapon's model rising out of the pool and turning there (placed in the world before the tablet's face)
		void PoseShow(RE::TESObjectREFR* a_tab, float a_rise)
		{
			auto* root = a_tab ? a_tab->Get3D() : nullptr;
			auto* body = root ? root->GetObjectByName("TabletBody") : nullptr;
			if (!body) return;
			const RE::NiPoint3 at = body->world * RE::NiPoint3{ 0.0f, -6.0f - 10.0f * a_rise, kPoolZ - 6.0f + 6.0f * a_rise };
			auto r = g_show.get();
			if (!r && g_prize >= 0 && gen::kTabletWeapons[g_prize].show) {
				if (auto* base = Forms::Ours(gen::kTabletWeapons[g_prize].show) ? Forms::Ours(gen::kTabletWeapons[g_prize].show)->As<RE::TESBoundObject>() : nullptr) {
					if (auto ref = a_tab->PlaceObjectAtMe(base, false)) g_show = ref->CreateRefHandle(), r = ref;
				}
			}
			if (!r) return;
			r->SetPosition(at);
			r->SetAngle({ 0.0f, 0.0f, a_tab->GetAngleZ() + g_clock * 0.9f });
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
			body->local.translate = RE::NiPoint3{ sx, 0.0f, 2.6f * std::sin(g_clock * 0.9f) + sz };
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
			Show(root, "Ghost", rolling && k < 0.92f);           // (the reel's ghosts, until it lands)
			Show(root, "GoldFlare_bb", g_gold && (g_phase == Phase::Offering || (rolling && k > 0.85f)));
			RE::NiUpdateData ud;
			body->Update(ud);
		}
	}

	int Town() { return g_town; }
	int Price()
	{
		Night();
		return 1500 + 500 * g_steps;  // (Noah: 1,500, +500 for every spin or repair, back to 1,500 at dawn)
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
		if (g_phase == Phase::Rolling) { Hud::Banner("The tablet is still turning", 1.5f); return; }
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
		if (--g_uses <= 0) Break(); else Rename(g_held, g_uses);
	}
	void OnCast(RE::FormID a_spell)
	{
		auto* f = FormOf(g_held);
		if (!f) return;
		const int t = gen::kTabletWeapons[g_held].type;
		bool mine = false;
		if (t == 7) mine = f->GetFormID() == a_spell;
		else if (t == 5) if (auto* w = f->As<RE::TESObjectWEAP>(); w && w->formEnchanting && w->formEnchanting->GetFormID() == a_spell) mine = true;
		if (!mine) return;
		if (--g_uses <= 0) Break(); else Rename(g_held, g_uses);
	}
	// (Noah: "on death") falling takes it
	void OnFell()
	{
		if (g_held < 0) return;
		TakeAway(g_held);
		Hud::Banner(std::string(gen::kTabletWeapons[g_held].label) + " is lost", 3.0f);
		g_held = -1, g_uses = 0;
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

	void Tick(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		Night();
		// where it stands: a freed city (chosen when the first is freed, and again should its city fall)
		if ((g_town < 0 || !Curse::Freed(g_town)) && Freed() > 0) {
			g_town = PickTown(-1), g_rolls = 0, g_moveAfter = std::uniform_int_distribution<int>(4, 8)(g_rng);
			logger::info("tablet: stands in {}", g_town >= 0 ? gen::kTowns[g_town] : "nowhere");
		}
		auto* ref = g_town >= 0 ? Shops::Placed(g_town, 7) : nullptr;
		if (g_phase != Phase::Idle) {
			g_t += a_delta;
			if (g_phase == Phase::Rolling && g_t >= kRollTime) {
				g_t = 0.0f;
				auto r = g_ref.get();
				if (g_skull) {   // the skull: the essence back, and the tablet goes to another freed city
					Points::Refund(g_paid);
					if (r) Forms::PlayAt(gen::Sound::shout_quake, r.get());
					const int from = g_town;
					g_town = PickTown(from), g_rolls = 0, g_moveAfter = std::uniform_int_distribution<int>(4, 8)(g_rng);
					Hud::Banner(fmt::format("A DRAGON PRIEST'S LAUGH - the tablet is gone. It waits in {} now  (+{})", g_town >= 0 ? gen::kTowns[g_town] : "?", Num(g_paid)), 5.0f);
					logger::info("tablet: the skull - moved from {} to {}", from >= 0 ? gen::kTowns[from] : "?", g_town >= 0 ? gen::kTowns[g_town] : "?");
					g_phase = Phase::Idle, g_skull = false;
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
			if (g_phase == Phase::Offering) if (auto r = g_ref.get()) PoseShow(r.get(), std::clamp(g_t / 1.5f, 0.0f, 1.0f));
		}
		if (ref && ref->Is3DLoaded() && ref->GetPosition().GetDistance(p->GetPosition()) < 6000.0f) Animate(ref, a_delta);
		// a donated bow's uses are its arrows; a weapon you no longer have (sold, dropped, a load) is let go
		if (g_held >= 0) {
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
				OnActivate(r);
				if (a_what == "gold" && g_prize >= 0) {
					std::vector<int> b;
					for (int i = 0; i < static_cast<int>(gen::kTabletWeapons.size()); ++i) if (gen::kTabletWeapons[i].battered) b.push_back(i);
					g_prize = b[std::uniform_int_distribution<int>(0, static_cast<int>(b.size()) - 1)(g_rng)], g_gold = true;
				}
			}
		}
	}
}
