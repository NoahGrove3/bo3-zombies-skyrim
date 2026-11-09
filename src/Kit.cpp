#include "BO3Z.h"
#include "CofferSpells.h"

// (Noah, the altar rework) "each class has its set starting gear that can be scaled as the player progresses through the
// game": a class's kit and its Armory, every item on the one tier curve - Base, I, II, III (Legendary), then Ascends -
// gated by the cities freed and paid for in Cursed Essence; every upgrade bought adds a little armour. Co-op: each class
// lends the party an aura.
// Co-save 'KIT1': per class its owned items, the tiers kept here (blades, flasks, armour) and its upgrades bought.
namespace bo3z::Kit
{
	namespace
	{
		enum class Kind : std::uint8_t { Ladder, Blade, Armour, Bomb };  // (Bomb: an Alchemist's flask - ref its element, "0" fire .. "5" blood)
		// (Noah: "remove Poison, Poison Breath and Blood Breath from the Armory") an item no longer offered: kept in the list
		// (its place is in the saves), never shown unless a save already owns it
		constexpr int kGone = 100;
		struct Def { Kind kind; const char* ref; int at; };  // ref: a ladder's label, a blade's id, a flask's element; at: cities freed (-1: the start)
		constexpr Def kSummoner[] = { { Kind::Blade, "riftblade", -1 }, { Kind::Ladder, "Beasts", -1 }, { Kind::Ladder, "The Dead", -1 },
			{ Kind::Ladder, "Daedra", 5 }, { Kind::Ladder, "The Wild", 3 }, { Kind::Ladder, "Dwemer", 1 },
			{ Kind::Armour, "armour", -1 }, { Kind::Ladder, "Atronachs", 2 }, { Kind::Ladder, "Ash", 4 }, { Kind::Ladder, "The Night", 7 }, { Kind::Ladder, "The Wrath", 9 } };  // (Noah: "I want them all to be 7")  // (Noah: the Dwarven Spider at 1 city freed, the Spriggan at 3, the Dremora - the strongest - at 5) (the list's order is the save's: the Armory shows them by when they open)
		constexpr Def kDestruction[] = { { Kind::Ladder, "Fire", -1 }, { Kind::Ladder, "Frost", -1 }, { Kind::Ladder, "Shock", 1 }, { Kind::Ladder, "Sun", 3 },
			{ Kind::Ladder, "Arcane", 5 }, { Kind::Ladder, "Runes", kGone },   /* (Noah: "I dont want to use runes or cloaks in the loot pool") */
			{ Kind::Armour, "armour", -1 }, { Kind::Ladder, "Cloaks", kGone }, { Kind::Ladder, "Walls", 4 }, { Kind::Ladder, "Storms", 9 },
			{ Kind::Blade, "carian", -1 } };  // (Noah: "destruction should get an elemental upgradable sword too" - last: the saves' order)
		// (Noah: "remove everything from the Ranger class ... and replace it with the Alchemist class") the knife, six flasks
		// opening with the cities freed, and the Drifter's outfit
		constexpr Def kAlchemist[] = { { Kind::Blade, "knife", -1 }, { Kind::Bomb, "0", -1 }, { Kind::Bomb, "1", 2 }, { Kind::Bomb, "2", 4 },
			{ Kind::Bomb, "3", 6 }, { Kind::Bomb, "4", 8 }, { Kind::Bomb, "5", 9 }, { Kind::Armour, "armour", -1 } };  // (Noah: "replace the crossbows with ... throwing daggers") (last: the saves' order)
		constexpr Def kSpellsword[] = { { Kind::Blade, "sword", -1 }, { Kind::Ladder, "Fire", -1 }, { Kind::Ladder, "Frost", 1 }, { Kind::Ladder, "Flame Breath", kGone },
			{ Kind::Ladder, "Shock", 3 }, { Kind::Ladder, "Frost Breath", kGone }, { Kind::Ladder, "Sun", 5 }, { Kind::Ladder, "Storm Breath", kGone },
			{ Kind::Ladder, "Arcane", 7 }, { Kind::Ladder, "Poison", kGone }, { Kind::Ladder, "Blood Breath", kGone }, { Kind::Ladder, "Poison Breath", kGone },
			{ Kind::Armour, "armour", -1 } };
		std::span<const Def> Defs(int a_cls)
		{
			switch (a_cls) {
			case 0: return kSummoner;
			case 1: return kDestruction;
			case 2: return kAlchemist;
			case 3: return kSpellsword;
			default: return {};
			}
		}
		int ArmourIndex(int a_cls)
		{
			const auto d = Defs(a_cls);
			for (int i = 0; i < static_cast<int>(d.size()); ++i) if (d[i].kind == Kind::Armour) return i;
			return -1;
		}
		constexpr int kMaxItems = 16, kOldRelics = 16;  // (kOldRelics: the format-1 save's relic arrays, read past)
		struct ClassState
		{
			std::uint32_t                       owned{ 0 };           // bits over Defs(cls)
			std::array<std::uint8_t, kMaxItems> tier{};               // a blade's, a flask's or the armour's tiers
			std::uint8_t                        upgrades{ 0 };        // bought, all told: the armour
		};
		std::array<ClassState, 4> g_cls{};
		bool g_loaded = false;

		// (co-op) who plays what: heard from the party every few seconds
		std::mutex g_partyLock;
		std::unordered_map<std::uint64_t, std::pair<int, std::chrono::steady_clock::time_point>> g_party;

		int Cls() { return Classes::Get(); }
		ClassState* Me() { const int c = Cls(); return c >= 0 && c < 4 ? &g_cls[c] : nullptr; }
		int Freed()
		{
			int n = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		bool Testing() { return Gun::TestKitOn(); }

		// a ladder of the class's: its first rung's index (-1)
		int LadderRung1(int a_cls, std::string_view a_label)
		{
			for (int i = 0; i < static_cast<int>(coffer::kSpells.size()); ++i) {
				const auto& s = coffer::kSpells[i];
				if (s.cls == a_cls && s.rung == 1 && a_label == s.ladderLabel) return i;
			}
			return -1;
		}
		// the rung of that ladder you know (-1 none)
		int LadderKnown(int a_cls, std::string_view a_label)
		{
			int best = -1;
			for (int i = 0; i < static_cast<int>(coffer::kSpells.size()); ++i) {
				const auto& s = coffer::kSpells[i];
				if (s.cls == a_cls && a_label == s.ladderLabel && Arcana::Known(i) && (best < 0 || s.rung > coffer::kSpells[best].rung)) best = i;
			}
			return best;
		}
		int Elem(const Def& d) { return d.ref[0] - '0'; }
		// a class's blade: the Summoner's Riftblade, the Alchemist's knife, the Spellsword's Madness Sword
		RE::TESObjectWEAP* BladeOf(std::string_view a_ref) { return a_ref == "riftblade" ? Classes::Riftblade() : a_ref == "knife" ? Classes::AlchemistKnife() : a_ref == "carian" ? Classes::CarianBlade() : Classes::SpellswordBlade(); }

		std::string ItemName(int a_cls, int a_i)
		{
			const auto defs = Defs(a_cls);
			if (a_i < 0 || a_i >= static_cast<int>(defs.size())) return "";
			const auto& d = defs[a_i];
			switch (d.kind) {
			case Kind::Ladder: {
				const int k = a_cls == Cls() ? LadderKnown(a_cls, d.ref) : -1;
				const int r1 = LadderRung1(a_cls, d.ref);
				return std::string(Arcana::Name(k >= 0 ? k : r1));
			}
			case Kind::Blade: {
				auto* w = BladeOf(d.ref);
				return w && w->GetName() ? w->GetName() : "Blade";
			}
			case Kind::Armour: return Classes::ArmourSetName(a_cls, g_cls[a_cls].tier[a_i]);
			case Kind::Bomb: { auto* it = Classes::FlaskItem(Elem(d)); return it && it->GetName() ? it->GetName() : "Flask"; }
			}
			return "";
		}
		std::uint32_t ItemColor(int a_cls, int a_i)
		{
			const auto defs = Defs(a_cls);
			if (a_i < 0 || a_i >= static_cast<int>(defs.size())) return 0xC8B48C;
			const auto& d = defs[a_i];
			if (d.kind == Kind::Ladder) return Arcana::ElementColor(Arcana::ElementOf(LadderRung1(a_cls, d.ref)));
			if (d.kind == Kind::Armour) return 0xD2C8B4;
			if (d.kind == Kind::Bomb) { static constexpr std::uint32_t kC[6] = { 0xFF6A1E, 0x8FD8FF, 0xA88CFF, 0x7CFF3C, 0xFFD24A, 0xE0202A }; return kC[std::clamp(Elem(d), 0, 5)]; }
			return std::string_view(d.ref) == "riftblade" ? 0xFF5A28 : 0xB4C8FF;
		}

		// an item's upgrades: 0 Base, 1 I, 2 II, 3 III (Legendary), 4.. Ascends; -1 not owned
		int ItemTier(int a_cls, int a_i)
		{
			const auto defs = Defs(a_cls);
			if (a_cls < 0 || a_cls > 3 || a_i < 0 || a_i >= static_cast<int>(defs.size()) || !(g_cls[a_cls].owned >> a_i & 1)) return -1;
			const auto& d = defs[a_i];
			const auto& st = g_cls[a_cls];
			switch (d.kind) {
			case Kind::Ladder: {
				const int k = a_cls == Cls() ? LadderKnown(a_cls, d.ref) : -1;
				return k >= 0 ? Arcana::Ups(k) : 0;
			}
			case Kind::Blade: return st.tier[a_i];
			case Kind::Armour: return st.tier[a_i];
			case Kind::Bomb: return st.tier[a_i];
			}
			return 0;
		}
		bool Carried(int a_cls, int a_i)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			const auto defs = Defs(a_cls);
			if (!p || a_i < 0 || a_i >= static_cast<int>(defs.size())) return false;
			const auto& d = defs[a_i];
			switch (d.kind) {
			case Kind::Ladder: return LadderKnown(a_cls, d.ref) >= 0;
			case Kind::Blade: {
				auto* w = BladeOf(d.ref);
				return w && p->GetItemCount(w) > 0;
			}
			case Kind::Armour: return Classes::ArmourCarried(a_cls, g_cls[a_cls].tier[a_i]);
			case Kind::Bomb: { auto* it = Classes::FlaskItem(Elem(d)); return it && p->GetItemCount(it) > 0; }
			}
			return false;
		}
		// the price and the gate of an item's next upgrade (a_next: the tier it would reach)
		int UpgradeCost(int a_next)
		{
			const int base = a_next <= 1 ? 4000 : a_next == 2 ? 6000 : a_next == 3 ? 8000 : 10000 + 2000 * (a_next - 4);  // (Noah: 4,000 / 6,000 / 8,000, then 10,000 and 2,000 more each Ascend)
			return static_cast<int>(std::lround(base * Director::PriceScale() / 50.0f)) * 50;   // (Noah, the essence economy: 3% more a night after night 10)
		}
		int UpgradeGate(int a_next) { return a_next <= 1 ? 0 : a_next == 2 ? 2 : a_next == 3 ? 4 : 6; }
		constexpr int kMaxTier = 3 + 10;  // (ten Ascends at most)
		int ArmoryCost(int a_at)
		{
			const int base = a_at <= 3 ? 3000 : a_at <= 6 ? 6500 : 10000;
			return static_cast<int>(std::lround(base * Director::PriceScale() / 50.0f)) * 50;   // (Noah, the essence economy: 3% more a night after night 10)
		}
		const char* TierLabel(int t)
		{
			static constexpr const char* kL[4] = { "Base", "Tier I", "Tier II", "Tier III - Legendary" };
			return t >= 0 && t < 4 ? kL[t] : "Legendary";
		}

		// one step up the curve: the next rung, a reforge, or an Ascend
		bool Step(int a_cls, int a_i)
		{
			const auto defs = Defs(a_cls);
			if (a_cls < 0 || a_cls > 3 || a_cls != Cls() || a_i < 0 || a_i >= static_cast<int>(defs.size())) return false;
			auto&      st = g_cls[a_cls];
			const auto& d = defs[a_i];
			switch (d.kind) {
			case Kind::Ladder: {
				const int k = LadderKnown(a_cls, d.ref);
				if (k < 0) return false;
				// the next tier: the next rung once its own tier is reached (a short ladder's rungs are spread over the
				// four tiers), else one more step on this one
				const int want = Arcana::Ups(k) + 1;
				if (const int nx = Arcana::NextRung(k); nx >= 0 && Arcana::TierOfRung(nx) <= want) {
					Arcana::Replace(k, nx);
					Arcana::SetLevel(nx, want - Arcana::TierOfRung(nx));
				} else {
					Arcana::SetLevel(k, Arcana::Level(k) + 1);
				}
				return true;
			}
			case Kind::Blade: ++st.tier[a_i]; return true;
			case Kind::Bomb: ++st.tier[a_i]; return true;
			case Kind::Armour:
				++st.tier[a_i];
				Classes::GiveArmourSet(a_cls, st.tier[a_i], true);  // (the Battle-mage's set changes its look at each tier)
				return true;
			}
			return false;
		}
		// the class's own gear, handed over (bought, or taken out of its chest)
		void Grant(int a_cls, int a_i, bool a_inHand)
		{
			const auto defs = Defs(a_cls);
			if (a_i < 0 || a_i >= static_cast<int>(defs.size())) return;
			const auto& d = defs[a_i];
			switch (d.kind) {
			case Kind::Ladder: if (LadderKnown(a_cls, d.ref) < 0) Arcana::Give(LadderRung1(a_cls, d.ref), false); break;
			case Kind::Blade: {
				auto* w = BladeOf(d.ref);
				auto* p = RE::PlayerCharacter::GetSingleton();
				if (w && p && p->GetItemCount(w) <= 0) Classes::GiveGear(w, 1, a_inHand);
				break;
			}
			case Kind::Armour: Classes::GiveArmourSet(a_cls, g_cls[a_cls].tier[a_i], true); break;
			case Kind::Bomb: Classes::GiveFlask(Elem(d)); break;
			}
		}

		int TierOfItem(int a_cls, int a_item) { return std::max(0, ItemTier(a_cls, a_item)); }
	}

	namespace
	{
		std::string TierSuffix(int a_t)
		{
			static constexpr const char* kS[4] = { "", " I", " II", " III" };
			return a_t <= 3 ? kS[std::max(0, a_t)] : fmt::format(" III +{}", a_t - 3);
		}
		struct BaseLook { std::string name; std::uint16_t damage{ 0 }; RE::EnchantmentItem* ench{ nullptr }; };
		std::unordered_map<RE::FormID, BaseLook> g_base;
		void Look(RE::TESObjectWEAP* a_w, int a_tier)
		{
			if (!a_w || a_tier < 0) return;
			auto [it, fresh] = g_base.try_emplace(a_w->GetFormID());
			if (fresh) it->second = { a_w->GetName() ? a_w->GetName() : "", a_w->attackDamage, a_w->formEnchanting };
			const auto& b = it->second;
			const std::string name = b.name + TierSuffix(a_tier);
			if (name != (a_w->GetName() ? a_w->GetName() : "")) a_w->fullName = name;
			a_w->attackDamage = static_cast<std::uint16_t>(std::clamp<float>(std::round(b.damage * UpgradeMult(a_tier)), 1.0f, 9999.0f));
		}
	}
	// (Noah) the kit's weapons as their tiers make them: name, damage, and the Riftblade's fire
	bool g_lookPending = false;
	void SyncWeapons(bool a_reequip)
	{
		const int c = Cls();
		if (c < 0) return;
		const auto defs = Defs(c);
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
			if (!(g_cls[c].owned >> i & 1)) continue;
			const int t = ItemTier(c, i);
			switch (defs[i].kind) {
			case Kind::Blade: {
				auto* w = BladeOf(defs[i].ref);
				Look(w, t);
				// (Noah: "each sword should have an elemental animation that changes and grows as the sword grows in strength from
				// the tiers 1-3") the class swords wear their tier's look: an enchantment of looks only (Classes::BladeLook) - bare
				// at Base; never any charge to run down
				if (w) {
					// (Noah: "the sword itself in hand doesn't have the special FX") Skyrim draws no enchantment on a weapon out of
					// charge - and none at all counts as out: a little charge, and its looks cost none, so it never runs down
					w->amountofEnchantment = t >= 1 || w == Classes::CarianBlade() ? 100 : 0;   // (Destruction's wands cast at every tier)
					if (auto* p = RE::PlayerCharacter::GetSingleton()) {
						if (auto* inv = p->GetInventoryChanges(); inv && inv->entryList) {
							for (auto* e : *inv->entryList) {
								if (!e || e->object != w || !e->extraLists) continue;
								for (auto* x : *e->extraLists) if (x && x->HasType(RE::ExtraDataType::kCharge)) x->RemoveByType(RE::ExtraDataType::kCharge);
							}
						}
					}
					auto* want = Classes::BladeLook(w, t);
					const bool newLook = Classes::AdoptTierLook(w, t);
					if (w->formEnchanting != want || newLook) {
						w->formEnchanting = want;
						// (the blade in hand: taken off and on again, so its new look shows; in a menu, once it closes)
						auto* p = RE::PlayerCharacter::GetSingleton();
						auto* em = RE::ActorEquipManager::GetSingleton();
						if (p && em && p->GetEquippedObject(false) == w) {
							if (a_reequip && !ItemMenusOpen()) {
								em->UnequipObject(p, w, nullptr, 1, nullptr, false, false, false, true);
								em->EquipObject(p, w, nullptr, 1, nullptr, false, false, false, true);
							} else g_lookPending = true;
						}
					}
				}
				break;
			}
			default: break;
			}
		}
	}

	void OnKitGiven(int a_cls)
	{
		if (a_cls < 0 || a_cls > 3) return;
		const auto defs = Defs(a_cls);
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (defs[i].at < 0) g_cls[a_cls].owned |= 1u << i;
		g_loaded = true;
		logger::info("kit: {} - its starting items owned", Classes::Name(a_cls));
	}

	std::vector<int> Rows()
	{
		std::vector<int> out;
		const int c = Cls();
		if (c < 0) return out;
		const auto defs = Defs(c);
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (g_cls[c].owned >> i & 1) out.push_back(40 + i);
		std::vector<int> locked;
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (!(g_cls[c].owned >> i & 1) && defs[i].at < kGone) locked.push_back(i);
		std::stable_sort(locked.begin(), locked.end(), [&](int a, int b) { return defs[a].at < defs[b].at; });
		for (const int i : locked) out.push_back(100 + i);
		return out;
	}

	void Fill(Hud::State& a_h)
	{
		a_h.kitLines.clear(), a_h.armoryLines.clear();
		const int c = Cls();
		if (c < 0) return;
		const int  freed = Freed();
		const int  pts = Points::Get();
		const auto defs = Defs(c);
		const auto& st = g_cls[c];
		auto line = [&](int a_id, int a_item) {
			Hud::State::KitLine l;
			l.id = a_id;
			const int t = TierOfItem(c, a_item);
			l.name = ItemName(c, a_item);
			l.tier = std::min(t, 3), l.asc = std::max(0, t - 3);
			l.color = ItemColor(c, a_item);
			l.carried = Carried(c, a_item);
			if (!l.carried) {
				l.info = "In your class's chest - select to take it", l.cost = 0, l.can = true;
			} else if (t >= kMaxTier) {
				l.info = "Ascended as far as it goes";
			} else {
				const int next = t + 1, gate = UpgradeGate(next);
				l.cost = UpgradeCost(next);
				if (freed < gate && !Testing()) {
					l.info = fmt::format("{} - next at {} cities freed ({} of {})", TierLabel(t), gate, freed, gate);
					l.cost = -1;
				} else {
					l.info = std::string(TierLabel(t)) + "  ->  " + (next <= 3 ? std::string(TierLabel(next)) : "Ascend " + std::to_string(next - 3));
					l.can = pts >= l.cost;
				}
			}
			a_h.kitLines.push_back(std::move(l));
		};
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (st.owned >> i & 1) line(40 + i, i);
		std::vector<int> locked;
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (!(st.owned >> i & 1) && defs[i].at < kGone) locked.push_back(i);
		std::stable_sort(locked.begin(), locked.end(), [&](int a, int b) { return defs[a].at < defs[b].at; });  // (first to open, first)
		for (const int i : locked) {
			Hud::State::KitLine l;
			l.id = 100 + i;
			l.name = ItemName(c, i);
			l.color = ItemColor(c, i);
			if (freed >= defs[i].at || Testing()) {
				l.cost = ArmoryCost(defs[i].at), l.can = pts >= l.cost, l.info = "Open - buy it for your kit";
			} else {
				l.cost = -1, l.info = fmt::format("Locked - free {} {} ({} of {})", defs[i].at, defs[i].at == 1 ? "city" : "cities", freed, defs[i].at);
			}
			a_h.armoryLines.push_back(std::move(l));
		}
		a_h.armorBonus = ArmorResist();
	}

	bool Select(int a_id, RE::TESObjectREFR* a_altar)
	{
		const int c = Cls();
		if (c < 0) return false;
		auto&      st = g_cls[c];
		const auto defs = Defs(c);
		const int  freed = Freed();
		if (a_id >= 100) {  // the Armory: buy it
			const int i = a_id - 100;
			if (i < 0 || i >= static_cast<int>(defs.size()) || (st.owned >> i & 1) || defs[i].at >= kGone) return false;
			if (freed < defs[i].at && !Testing()) {
				Hud::Banner(fmt::format("{} opens once {} cities are freed ({} of {})", ItemName(c, i), defs[i].at, freed, defs[i].at), 3.0f);
				return false;
			}
			if (ItemMenusOpen() || !Points::Spend(ArmoryCost(defs[i].at))) return false;
			st.owned |= 1u << i;
			Grant(c, i, false);
			if (a_altar) Forms::PlayAt(gen::Sound::forge_upgrade, a_altar);
			Hud::Banner(ItemName(c, i) + "  -  yours", 4.0f);
			logger::info("kit: {} bought ({})", ItemName(c, i), Classes::Name(c));
			return true;
		}
		const int item = a_id - 40;
		if (item < 0 || item >= static_cast<int>(defs.size()) || !(st.owned >> item & 1)) return false;
		if (!Carried(c, item)) {  // out of the chest, into your hands (free)
			if (ItemMenusOpen()) return false;
			Grant(c, item, true);
			Hud::Banner(ItemName(c, item) + "  -  taken up", 2.5f);
			return true;
		}
		const int t = ItemTier(c, item);
		if (t >= kMaxTier) { Hud::Banner(ItemName(c, item) + " has ascended as far as it goes", 2.5f); return false; }
		if (freed < UpgradeGate(t + 1) && !Testing()) {
			Hud::Banner(fmt::format("{}'s next tier waits for {} cities freed ({} of {})", ItemName(c, item), UpgradeGate(t + 1), freed, UpgradeGate(t + 1)), 3.0f);
			return false;
		}
		if (ItemMenusOpen() || !Points::Spend(UpgradeCost(t + 1))) return false;
		const std::string was = ItemName(c, item);
		if (!Step(c, item)) { Points::Refund(UpgradeCost(t + 1)); return false; }
		st.upgrades = static_cast<std::uint8_t>(std::min(255, st.upgrades + 1));
		if (a_altar) Forms::PlayAt(gen::Sound::forge_upgrade, a_altar);
		SyncWeapons(true);
		const std::string now = ItemName(c, item);
		Hud::Banner((now != was ? was + "  ->  " + now : now) + "  -  " + (t + 1 <= 3 ? std::string(TierLabel(t + 1)) : "Ascend " + std::to_string(t + 1 - 3)) +
		                (defs[item].kind == Kind::Armour ? (ArmorAscendMult() > 1.001f ? fmt::format("   (damage reduction +{:.0f}%, damage +{:.0f}%)", ArmorResist() * 100.0f, (ArmorAscendMult() - 1.0f) * 100.0f) : fmt::format("   (damage reduction +{:.0f}%)", ArmorResist() * 100.0f)) : fmt::format("   (damage x{:.1f})", UpgradeMult(t + 1))), 4.5f);  // (Noah: "I upgraded my Fire Touch - it didn't seem like it changed anything": what it gained, said)
		logger::info("kit: {} -> tier {} ({})", now, t + 1, Classes::Name(c));
		return true;
	}

	// (Noah: "when I killed the Minotaur mini boss it said all wonder weapon parts acquired, 5,000 cursed essence instead -
	// that's still the old system ... bosses drop based on where the game's currently at") a mini-boss's spoils follow where
	// your class stands: the next Armory item that is open to you and not yet yours, free; failing that, a free upgrade of
	// your lowest-tier item that may climb (its freed-city gate met); with every item at its height, Cursed Essence that
	// grows with the night
	std::string MiniBossSpoils(int a_night)
	{
		const int c = Cls();
		if (c < 0) return {};
		auto&      st = g_cls[c];
		const auto defs = Defs(c);
		const int  freed = Freed();
		int best = -1;
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
			if ((st.owned >> i & 1) || defs[i].at >= kGone || (freed < defs[i].at && !Testing())) continue;
			if (best < 0 || defs[i].at < defs[best].at) best = i;
		}
		if (best >= 0) {
			st.owned |= 1u << best;
			Grant(c, best, false);
			logger::info("kit: a mini-boss's spoils - {} ({}) free", ItemName(c, best), Classes::Name(c));
			return ItemName(c, best) + " - added to your kit";
		}
		int low = -1;
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
			if (!(st.owned >> i & 1) || defs[i].at >= kGone) continue;
			const int t = ItemTier(c, i);
			if (t < 0 || t >= kMaxTier || (freed < UpgradeGate(t + 1) && !Testing())) continue;
			if (low < 0 || t < ItemTier(c, low)) low = i;
		}
		if (low >= 0) {
			const std::string was = ItemName(c, low);
			const int t = ItemTier(c, low);
			if (Step(c, low)) {
				st.upgrades = static_cast<std::uint8_t>(std::min(255, st.upgrades + 1));
				SyncWeapons(true);
				logger::info("kit: a mini-boss's spoils - {} up to tier {} ({})", was, t + 1, Classes::Name(c));
				return was + " - " + (t + 1 <= 3 ? std::string(TierLabel(t + 1)) : "Ascend " + std::to_string(t + 1 - 3)) + ", free";
			}
		}
		// (Noah, 2026-11-08, the essence economy) 5,000 + 500 a night, at most 20,000 (it was + 1,000 a night, no cap)
		const int pts = static_cast<int>(std::lround(std::min(20000, 5000 + 500 * std::max(0, a_night)) * Perks::MiniBossEssenceMult()));  // (Bounty Hunter's Ale: twice)
		Points::Add(pts);
		return fmt::format("{} Cursed Essence", pts);
	}
	// (Noah: "remove all the relics that drop from the Dragon Priest ... the Dragon Priest should drop 3,000 essence instead")
	// (Noah: "the drop should be all players receive the weapon seen from the pickup notification on the bottom right like
	// all other items, and a text pop up ... (\"The Priests Name\" has been slain. Then under it \"weapon name\"
	// received.)") the host's kill reaches every game in the party (Net::PriestSlain); each takes the weapon as any item
	// is picked up (Forms::Give), once - it is kept for the run - and shows the card
	void PriestDrop(int a_town)
	{
		if (a_town < 0 || a_town >= static_cast<int>(gen::kCityBosses.size())) return;
		const auto& b = gen::kCityBosses[a_town];
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* w = dh ? dh->LookupForm<RE::TESObjectWEAP>(b.weapon, gen::kPlugin) : nullptr;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!w || !p) { logger::warn("kit: {}'s weapon not found ({:03X})", b.name, b.weapon); return; }
		if (p->GetItemCount(w) <= 0) Forms::Give(p, w, 1);
		Hud::Slain(fmt::format("{} has been slain", b.name), fmt::format("{} received", b.weaponName), 7.0f);
		logger::info("kit: {} slain - {} received", b.name, b.weaponName);
	}
	void OnPriestSlain()
	{
		Points::Add(3000);
		Hud::Banner("THE DRAGON PRIEST FALLS - 3,000 CURSED ESSENCE", 5.0f);
		logger::info("kit: a Dragon Priest's 3,000 Essence");
	}

	int Tier(int a_item) { return TierOfItem(Cls(), a_item); }
	int TierOfWeapon(const RE::TESForm* a_w)
	{
		if (!a_w) return -1;
		const int c = Cls();
		if (c < 0) return -1;
		const auto defs = Defs(c);
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
			if (defs[i].kind != Kind::Blade || !(g_cls[c].owned >> i & 1)) continue;
			auto* w = BladeOf(defs[i].ref);
			if (w && w == a_w) return g_cls[c].tier[i];
		}
		return -1;
	}
	// (the Alchemist) a flask's tier (0 fire .. 5 blood), -1 not owned (not the Alchemist, or not yet)
	int FlaskTier(int a_el)
	{
		if (Cls() != 2) return -1;
		const auto defs = Defs(2);
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (defs[i].kind == Kind::Bomb && Elem(defs[i]) == a_el) return ItemTier(2, i);
		return -1;
	}
	int RiftTier() { return g_cls[0].owned & 1 ? g_cls[0].tier[0] : 0; }
	// (Noah: "the upgrade shouldn't increase armour rating"; "each set starts at 10 for the whole set, +10 for each upgrade
	// of the set") the class's set: its armour rating, and Skyrim's own reading of it (0.12% of a blow per point)
	int ArmourTier(int a_cls)
	{
		const int i = ArmourIndex(a_cls);
		return a_cls >= 0 && a_cls < 4 && i >= 0 && (g_cls[a_cls].owned >> i & 1) ? g_cls[a_cls].tier[i] : -1;
	}
	int ArmourRating(int a_cls) { const int t = ArmourTier(a_cls); return t < 0 ? 0 : 10 + 10 * t; }
	// (Noah: "I want the armor to say damage reduction +5% and go up by 5% per upgrade maxing at 20%") the class's set takes
	// 5% off every blow, 5% more a tier, 20% at Tier III (Ascends don't add more)
	float ArmorResist() { const int t = ArmourTier(Cls()); return t < 0 ? 0.0f : std::min(0.20f, 0.05f * static_cast<float>(t + 1)); }
	// (Noah: "the armor ascending should increase class damage by 5% and cost more each ascend like the other armor sets")
	// every Ascend of the set past Tier III: +5% to everything your side deals the dead (the price climbs as every Ascend's)
	float ArmorAscendMult() { return 1.0f + 0.05f * static_cast<float>(std::max(0, ArmourTier(Cls()) - 3)); }

	// ---- co-op auras ----
	bool PartyHas(int a_cls)
	{
		if (!Net::InLobby()) return false;
		std::scoped_lock l(g_partyLock);
		const auto now = std::chrono::steady_clock::now();
		for (auto& [id, e] : g_party) if (e.first == a_cls && now - e.second < std::chrono::seconds(20)) return true;
		return false;
	}
	void HeardClass(std::uint64_t a_from, int a_cls)
	{
		std::scoped_lock l(g_partyLock);
		g_party[a_from] = { a_cls, std::chrono::steady_clock::now() };
	}
	float AuraCostMult()
	{
		// (the Black Mage's set) spells cost 10% less from Tier I, 25% from Tier III
		const int t = Cls() == 1 ? ArmourTier(1) : -1;
		return (PartyHas(1) ? 0.9f : 1.0f) * (t >= 3 ? 0.75f : t >= 1 ? 0.9f : 1.0f);
	}
	float AuraMeleeTaken() { return PartyHas(3) ? 0.9f : 1.0f; }

	// your blow landed: (the Spellsword's set, Tier II) the blade's hits put magicka back
	void OnPlayerHit(RE::Actor* a_target, const RE::TESForm* a_source)
	{
		if (Cls() != 3 || !a_target || !a_source || a_source != Classes::SpellswordBlade() || ArmourTier(3) < 2) return;
		if (auto* p = RE::PlayerCharacter::GetSingleton()) {
			auto* av = p->AsActorValueOwner();
			av->RestoreActorValue(RE::ActorValue::kMagicka, av->GetPermanentActorValue(RE::ActorValue::kMagicka) * 0.03f);
		}
	}

	// (the class armour sets, once a second) a class from before them gets its set (owned, worn); the Spellsword's mending
	// and the Battle-mage's quickened magicka
	void TickSets()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		const int c = Cls();
		if (!p || c < 0 || !p->Is3DLoaded() || ItemMenusOpen()) return;
		if (const int i = ArmourIndex(c); i >= 0 && Classes::KitGiven(c) && !(g_cls[c].owned >> i & 1)) {
			g_cls[c].owned |= 1u << i;
			Classes::GiveArmourSet(c, 0, true);
			Hud::Banner(Classes::ArmourSetName(c, 0) + " - your class's armor, upgradable at the Echo Altar", 5.0f);
		}
		auto* av = p->AsActorValueOwner();
		if (c == 3 && ArmourTier(3) >= 1 && p->IsInCombat()) av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.006f);  // (Tier I: mending in a fight)
		if (c != 2 && PartyHas(2) && p->IsInCombat()) av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.005f);  // (an Alchemist in your party: you mend slowly in a fight)
		if (c == 1 && ArmourTier(1) >= 2) av->RestoreActorValue(RE::ActorValue::kMagicka, av->GetPermanentActorValue(RE::ActorValue::kMagicka) * 0.012f);          // (Tier II: magicka comes back faster)
	}
	void TickMadGlow(float a_delta);
	void Tick(float a_delta)
	{
		// (co-op) your class, told to the party now and then; a party aura that came or went sets the spells' costs again
		static float tell = 0;
		if ((tell -= a_delta) <= 0) {
			tell = 5.0f;
			if (Net::InLobby() && Cls() >= 0) Net::TellClass(Cls());
			static float costWas = 1.0f;
			if (const float cm = AuraCostMult(); cm != costWas) costWas = cm, Arcana::ReapplyAll();
			static bool robesWas = false;  // (the First Dragonborn's Regalia put on or taken off: the altar spells' price follows)
			if (const bool rb = Echo::WearsRobes(); rb != robesWas) robesWas = rb, Arcana::ReapplyAll();
		}
		// (Noah: "upgrading the Riftblade didn't change anything on the inventory stats", "the effects should enhance the
		// current detail") every kit weapon's card follows its tier - its name and its damage - and the Riftblade's own fire
		// burns brighter each tier
		TickMadGlow(a_delta);
		if (g_lookPending && !ItemMenusOpen()) { g_lookPending = false; SyncWeapons(true); }  // (a sword's new look, once the menu is shut)
		static float look = 0;
		if ((look -= a_delta) <= 0) {
			look = 1.0f;
			SyncWeapons(false);
			TickSets();
		}
	}
	// the Madness Sword's violet fire, on the blade in your hand while it is drawn: its tier's flame (I-III, brighter each),
	// laid on the weapon itself - in third person and in first - and renewed before it fades
	// (Noah: "I still want the Madness Sword to have an animated enchantment effect ... I have yet to see what that looks
	// like") the effect-shader fire never showed on the blade: its fire is a model hung on the blade now (WonderProps.inl
	// Props::TickMad), from Base, stronger each tier
	void TickMadGlow(float) {}
	int MadTier()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* w = Classes::SpellswordBlade();
		if (!p || !w || !p->Is3DLoaded() || !p->AsActorState()->IsWeaponDrawn() || (p->GetEquippedObject(false) != w && p->GetEquippedObject(true) != w)) return -1;
		int t = 0;
		const auto defs = Defs(3);
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (defs[i].kind == Kind::Blade && (g_cls[3].owned >> i & 1)) t = std::max(0, ItemTier(3, i));
		return t;
	}
	// ---- the co-save ----
	std::vector<std::uint8_t> State()
	{
		std::vector<std::uint8_t> v;
		auto put32 = [&](std::uint32_t x) { for (int b = 0; b < 4; ++b) v.push_back(static_cast<std::uint8_t>(x >> (8 * b))); };
		v.push_back(2);  // (format 2: the Infusions and relics gone)
		for (const auto& s : g_cls) {
			put32(s.owned);
			v.insert(v.end(), s.tier.begin(), s.tier.end());
			v.push_back(s.upgrades);
		}
		return v;
	}
	void SetState(const std::vector<std::uint8_t>& a_v)
	{
		Revert();
		std::size_t k = 0;
		auto get32 = [&]() { std::uint32_t x = 0; for (int b = 0; b < 4 && k < a_v.size(); ++b) x |= static_cast<std::uint32_t>(a_v[k++]) << (8 * b); return x; };
		auto getArr = [&](auto& arr) { for (auto& e : arr) e = k < a_v.size() ? a_v[k++] : 0; };
		if (a_v.empty()) return;
		const int fmt = a_v[k++];
		if (fmt != 1 && fmt != 2) return;
		for (auto& s : g_cls) {
			s.owned = get32();
			getArr(s.tier);
			if (fmt == 1) k += kMaxItems + 4 + 2 * kOldRelics;  // (an older save's Infusions and relics: read past)
			s.upgrades = k < a_v.size() ? a_v[k++] : 0;
		}
		g_loaded = true;
		logger::info("kit: loaded");
	}
	void Revert()
	{
		g_cls = {};
		g_loaded = false;
	}
	bool Loaded() { return g_loaded; }

	// dev (trailer filming, Noah: "give my character everything from all classes") every class's whole kit and Armory at its
	// highest tier, the four wonders and Alduin's Bane - your class stays as it is
	// dev (Noah: "for the full kit, when testing everything, give me everything at its base level ... so I can try out each tier
	// of each weapon") every class's whole kit and Armory owned at Base - each ladder at its first rung, each blade and flask
	// at Base, each set at its first look - to be raised at the altar one tier at a time
	void DevBase()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		int n = 0;
		for (int c = 0; c < 4; ++c) {
			const auto defs = Defs(c);
			auto&      st = g_cls[c];
			for (int i = 0; i < static_cast<int>(defs.size()) && i < kMaxItems; ++i) {
				const auto& d = defs[i];
				if (d.at == kGone) continue;
				st.owned |= 1u << i;
				st.tier[i] = 0;
				switch (d.kind) {
				case Kind::Ladder: {
					const int r1 = LadderRung1(c, d.ref);
					if (r1 < 0 || !Arcana::Spell(r1)) break;
					const int k = LadderKnown(c, d.ref);
					if (k >= 0 && k != r1) Arcana::Replace(k, r1);
					else if (k < 0) Arcana::Give(r1, false);
					Arcana::SetLevel(r1, 0);
					++n;
					break;
				}
				case Kind::Bomb:
				case Kind::Blade:
					Grant(c, i, false);
					++n;
					break;
				case Kind::Armour:
					Classes::GiveArmourSet(c, 0, false);
					++n;
					break;
				default: break;
				}
			}
		}
		SyncWeapons(false);
		logger::info("dev: everything from every class at Base - {} kit items", n);
	}
	// (Noah: "update the test kit with the new items") the test kit's share of the classes: every class's crystal sword and
	// flasks at Tier III (the arcs, the heavy attack's burst, its own power - all of it to try at once) and every class's Black
	// Mage robes in its own colours, Tier III - the spell ladders left to the kit's own altar spells
	// (Noah: "update the test kit with all weapons, spells, and armor for each class") every class's whole kit - its
	// spell ladders' top spells, sword, flasks and robes - at Legendary (Tier III) for the test kit, or
	// at the last Ascend for the dev command
	void DevGiveAll(int kTop);
	void DevTestKit() { DevGiveAll(3); }
	void DevAll() { DevGiveAll(kMaxTier); }
	void DevGiveAll(int kTop)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		int n = 0;
		for (int c = 0; c < 4; ++c) {
			const auto defs = Defs(c);
			auto&      st = g_cls[c];
			for (int i = 0; i < static_cast<int>(defs.size()) && i < kMaxItems; ++i) {
				const auto& d = defs[i];
				if (d.at == kGone) continue;
				st.owned |= 1u << i;
				switch (d.kind) {
				case Kind::Ladder: {
					int top = -1;
					for (int j = 0; j < static_cast<int>(coffer::kSpells.size()); ++j) {
						const auto& sp = coffer::kSpells[j];
						if (sp.cls == c && d.ref == std::string_view(sp.ladderLabel) && (top < 0 || sp.rung > coffer::kSpells[top].rung)) top = j;
					}
					if (top < 0 || !Arcana::Spell(top)) break;
					if (!Arcana::Known(top)) Arcana::Give(top, false);
					Arcana::SetLevel(top, std::max(0, kTop - Arcana::TierOfRung(top)));
					++n;
					break;
				}
				case Kind::Bomb:
				case Kind::Blade:
					st.tier[i] = static_cast<std::uint8_t>(kTop);
					Grant(c, i, false);
					++n;
					break;
				case Kind::Armour:
					st.tier[i] = 3;
					Classes::GiveArmourSet(c, 3, false);
					++n;
					break;
				}
			}
		}
		SyncWeapons(false);
		logger::info("dev: everything from every class - {} kit items at tier {}", n, kTop);
	}
}
