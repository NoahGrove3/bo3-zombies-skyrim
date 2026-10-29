#include "BO3Z.h"
#include "CofferSpells.h"

// (Noah, the altar rework) "each class has its set starting gear that can be scaled as the player progresses through the
// game": a class's kit and its Armory, every item on the one tier curve - Base, I, II, III (Legendary), then Ascends -
// gated by the cities freed and paid for in Cursed Essence; the altar's roll is the Infusion on one of your items; every
// Dragon Priest that falls leaves a relic for your class (pick one of two at an altar); every upgrade bought adds a
// little armour. Co-op: each class lends the party an aura.
// Co-save 'KIT1': per class its owned items, the tiers kept here (blades, relics, Ascends past a weapon's three),
// Infusions, relics, armour, and the relic tokens waiting.
namespace bo3z::Kit
{
	namespace
	{
		enum class Kind : std::uint8_t { Ladder, Bow, Crossbow, Blade };
		struct Def { Kind kind; const char* ref; int at; };  // ref: a ladder's label, a bow's id, a crossbow's element ("0".."2"), a blade's id; at: cities freed (-1: the start)
		constexpr Def kSummoner[] = { { Kind::Blade, "riftblade", -1 }, { Kind::Ladder, "Beasts", -1 }, { Kind::Ladder, "The Dead", -1 },
			{ Kind::Ladder, "Daedra", 5 }, { Kind::Ladder, "The Wild", 3 }, { Kind::Ladder, "Dwemer", 1 } };  // (Noah: the Dwarven Spider at 1 city freed, the Spriggan at 3, the Dremora - the strongest - at 5) (the list's order is the save's: the Armory shows them by when they open)
		constexpr Def kDestruction[] = { { Kind::Ladder, "Fire", -1 }, { Kind::Ladder, "Frost", -1 }, { Kind::Ladder, "Shock", 1 }, { Kind::Ladder, "Sun", 3 },
			{ Kind::Ladder, "Blood", 5 }, { Kind::Ladder, "Runes", 7 } };
		constexpr Def kRanger[] = { { Kind::Crossbow, "0", -1 }, { Kind::Bow, "emberstring", -1 }, { Kind::Crossbow, "1", 1 }, { Kind::Bow, "rimehorn", 2 },
			{ Kind::Crossbow, "2", 3 }, { Kind::Bow, "stormreach", 4 }, { Kind::Bow, "skyhowl", 6 }, { Kind::Bow, "dawnspire", 8 }, { Kind::Bow, "thirstbow", 9 } };
		constexpr Def kSpellsword[] = { { Kind::Blade, "sword", -1 }, { Kind::Ladder, "Fire", -1 }, { Kind::Ladder, "Frost", 1 }, { Kind::Ladder, "Flame Breath", 2 },
			{ Kind::Ladder, "Shock", 3 }, { Kind::Ladder, "Frost Breath", 4 }, { Kind::Ladder, "Sun", 5 }, { Kind::Ladder, "Storm Breath", 6 },
			{ Kind::Ladder, "Blood", 7 }, { Kind::Ladder, "Poison", 8 }, { Kind::Ladder, "Blood Breath", 9 }, { Kind::Ladder, "Poison Breath", 10 } };
		std::span<const Def> Defs(int a_cls)
		{
			switch (a_cls) {
			case 0: return kSummoner;
			case 1: return kDestruction;
			case 2: return kRanger;
			case 3: return kSpellsword;
			default: return {};
			}
		}
		// the Dragon Priests' relics, by class: the Summoner's heroes and armies (Calling of the Conjurer), Destruction's support
		// spells (Mysticism), the Spellsword's legendary two-handers (Classes::TwoHander), the Ranger's relic quivers
		constexpr std::uint32_t kHeroes[] = { 0x019D7C, 0x0290A4, 0x0290A3, 0x0290A6, 0x0CB318, 0x103618, 0x019D66, 0x09370D, 0x0A2A1D, 0x0CB311, 0x0C61F9 };
		constexpr std::uint32_t kSupport[] = { 0x1901E5, 0x4B065B, 0x25FBFF, 0xFF19E0 };
		constexpr const char*   kQuivers[] = { "Quiver of Embers", "Quiver of Rime", "Quiver of Storms" };
		int RelicCount(int a_cls)
		{
			switch (a_cls) {
			case 0: return static_cast<int>(std::size(kHeroes));
			case 1: return static_cast<int>(std::size(kSupport));
			case 2: return static_cast<int>(std::size(kQuivers));
			case 3: return Classes::TwoHanderCount();
			default: return 0;
			}
		}
		RE::SpellItem* RelicSpell(int a_cls, int a_r)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh || a_r < 0) return nullptr;
			if (a_cls == 0 && a_r < static_cast<int>(std::size(kHeroes))) return dh->LookupForm<RE::SpellItem>(kHeroes[a_r], "Calling of the Conjurer.esp");
			if (a_cls == 1 && a_r < static_cast<int>(std::size(kSupport))) return dh->LookupForm<RE::SpellItem>(kSupport[a_r], "MysticismMagic.esp");
			return nullptr;
		}
		std::string RelicName(int a_cls, int a_r)
		{
			if (a_cls == 2) return a_r >= 0 && a_r < 3 ? kQuivers[a_r] : "";
			if (a_cls == 3) { auto* w = Classes::TwoHander(a_r); return w && w->GetName() ? w->GetName() : ""; }
			auto* sp = RelicSpell(a_cls, a_r);
			return sp && sp->GetName() ? sp->GetName() : "";
		}
		std::string RelicDesc(int a_cls)
		{
			switch (a_cls) {
			case 0: return "A Legendary pact: a hero or an army answers your call";
			case 1: return "A support spell of the old masters";
			case 2: return "Your bows' and crossbows' blasts reach wider";
			case 3: return "A legendary two-handed weapon";
			default: return "";
			}
		}

		constexpr int kMaxItems = 16, kMaxRelics = 16;
		struct ClassState
		{
			std::uint32_t                       owned{ 0 };           // bits over Defs(cls)
			std::array<std::uint8_t, kMaxItems> tier{};               // a blade's tiers; a bow's or crossbow's Ascends past its three
			std::array<std::uint8_t, kMaxItems> infusion{};           // 0 none, 1..kInfusions
			std::uint32_t                       relics{ 0 };          // bits over the class's relics
			std::array<std::uint8_t, kMaxRelics> relicTier{};
			std::array<std::uint8_t, kMaxRelics> relicInfusion{};
			std::uint8_t                        upgrades{ 0 };        // bought, all told: the armour
		};
		std::array<ClassState, 4> g_cls{};
		int  g_tokens = 0;          // relics waiting to be taken
		int  g_offer[2] = { -1, -1 };  // the relic offer: a relic index, or 100 + an item (a free upgrade)
		int  g_offerCls = -1;       // the class it was made for
		int  g_relicSel = 0;
		int  g_infuseSel = 0;       // the Infusion row's item (an index into InfusableItems())
		int  g_rollsTonight = 0, g_rollNight = -1;
		bool g_loaded = false;
		struct Spin { float left{ 0 }, flick{ 0 }; int item{ -1 }, result{ 0 }, shown{ 1 }, cls{ -1 }; };
		Spin g_spin;
		std::mt19937 g_rng{ std::random_device{}() };

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
		int BowIndex(std::string_view a_id)
		{
			for (int b = 0; b < static_cast<int>(gen::kBows.size()); ++b) if (gen::kBows[b].id == a_id) return b;
			return -1;
		}
		int Elem(const Def& d) { return d.ref[0] - '0'; }

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
			case Kind::Bow: { const int b = BowIndex(d.ref); return b >= 0 ? std::string(gen::kBows[b].label) : ""; }
			case Kind::Crossbow: { auto* w = Classes::CrossbowForm(Elem(d)); return w && w->GetName() ? w->GetName() : "Crossbow"; }
			case Kind::Blade: {
				auto* w = std::string_view(d.ref) == "riftblade" ? Classes::Riftblade() : Classes::SpellswordBlade();
				return w && w->GetName() ? w->GetName() : "Blade";
			}
			}
			return "";
		}
		std::uint32_t ItemColor(int a_cls, int a_i)
		{
			const auto defs = Defs(a_cls);
			if (a_i < 0 || a_i >= static_cast<int>(defs.size())) return 0xC8B48C;
			const auto& d = defs[a_i];
			if (d.kind == Kind::Ladder) return Arcana::ElementColor(Arcana::ElementOf(LadderRung1(a_cls, d.ref)));
			if (d.kind == Kind::Bow) { const int b = BowIndex(d.ref); return b >= 0 ? gen::kBows[b].color : 0xC8B48C; }
			if (d.kind == Kind::Crossbow) { static constexpr std::uint32_t kC[3] = { 0xFF6A1E, 0x6FC8FF, 0x8C7CFF }; return kC[std::clamp(Elem(d), 0, 2)]; }
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
			case Kind::Bow: { const int b = BowIndex(d.ref); return b >= 0 ? std::clamp(Bows::Level(b), 0, 3) + st.tier[a_i] : 0; }
			case Kind::Crossbow: return std::clamp(Classes::CrossbowLevel(Classes::CrossbowForm(Elem(d))), 0, 3) + st.tier[a_i];
			case Kind::Blade: return st.tier[a_i];
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
			case Kind::Bow: return Bows::Carried() == BowIndex(d.ref);
			case Kind::Crossbow: { auto* w = Classes::CrossbowForm(Elem(d)); return w && p->GetItemCount(w) > 0; }
			case Kind::Blade: {
				auto* w = std::string_view(d.ref) == "riftblade" ? Classes::Riftblade() : Classes::SpellswordBlade();
				return w && p->GetItemCount(w) > 0;
			}
			}
			return false;
		}
		// the price and the gate of an item's next upgrade (a_next: the tier it would reach)
		int UpgradeCost(int a_next)
		{
			const int base = a_next <= 1 ? 2000 : a_next == 2 ? 5000 : a_next == 3 ? 12000 : 15000 + 4000 * (a_next - 4);
			return PowerUps::FireSale() ? base / 2 : base;  // (Merchant's Blessing: half)
		}
		int UpgradeGate(int a_next) { return a_next <= 1 ? 0 : a_next == 2 ? 2 : a_next == 3 ? 4 : 6; }
		constexpr int kMaxTier = 3 + 10;  // (ten Ascends at most)
		int ArmoryCost(int a_at)
		{
			const int base = a_at <= 3 ? 3000 : a_at <= 6 ? 6500 : 10000;
			return PowerUps::FireSale() ? base / 2 : base;
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
			case Kind::Bow: {
				const int b = BowIndex(d.ref);
				if (b < 0) return false;
				if (Bows::Level(b) < 3) Bows::SetLevel(b, Bows::Level(b) + 1);
				else ++st.tier[a_i];
				return true;
			}
			case Kind::Crossbow: {
				auto* w = Classes::CrossbowForm(Elem(d));
				if (!w) return false;
				if (const int lv = Classes::CrossbowLevel(w); lv < 3) Classes::SetCrossbowLevel(w, lv + 1);
				else ++st.tier[a_i];
				return true;
			}
			case Kind::Blade: ++st.tier[a_i]; return true;
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
			case Kind::Bow: if (const int b = BowIndex(d.ref); b >= 0) Bows::Give(b); break;
			case Kind::Crossbow: Classes::TakeCrossbow(Elem(d)); break;
			case Kind::Blade: {
				auto* w = std::string_view(d.ref) == "riftblade" ? Classes::Riftblade() : Classes::SpellswordBlade();
				auto* p = RE::PlayerCharacter::GetSingleton();
				if (w && p && p->GetItemCount(w) <= 0) Classes::GiveGear(w, 1, a_inHand);
				break;
			}
			}
		}

		// ---- relics ----
		std::vector<int> RelicsLeft(int a_cls)
		{
			std::vector<int> out;
			for (int r = 0; r < RelicCount(a_cls) && r < kMaxRelics; ++r) {
				if (g_cls[a_cls].relics >> r & 1) continue;
				if ((a_cls == 0 || a_cls == 1) && !RelicSpell(a_cls, r)) continue;  // (its mod missing)
				if (a_cls == 3 && !Classes::TwoHander(r)) continue;
				out.push_back(r);
			}
			return out;
		}
		void MakeOffer()
		{
			const int c = Cls();
			g_offer[0] = g_offer[1] = -1;
			g_offerCls = c;
			if (c < 0 || g_tokens <= 0) return;
			auto left = RelicsLeft(c);
			std::shuffle(left.begin(), left.end(), g_rng);
			int n = 0;
			for (int r : left) if (n < 2) g_offer[n++] = r;
			if (n < 2) {  // (every relic of your class won: a free upgrade of one of your items instead)
				std::vector<int> items;
				for (int i = 0; i < static_cast<int>(Defs(c).size()); ++i) {
					const int t = ItemTier(c, i);
					if (t >= 0 && t < kMaxTier && (Freed() >= UpgradeGate(t + 1) || Testing())) items.push_back(i);
				}
				std::shuffle(items.begin(), items.end(), g_rng);
				for (int i : items) if (n < 2) g_offer[n++] = 100 + i;
			}
			g_relicSel = 0;
		}
		std::string OfferName(int a_o)
		{
			const int c = Cls();
			if (a_o < 0 || c < 0) return "";
			if (a_o >= 100) return "Blessing: " + ItemName(c, a_o - 100) + " - a free upgrade";
			return RelicName(c, a_o);
		}
		std::string OfferDesc(int a_o)
		{
			const int c = Cls();
			if (a_o < 0 || c < 0) return "";
			if (a_o >= 100) { const int t = ItemTier(c, a_o - 100); return std::string("Up to ") + (t + 1 <= 3 ? TierLabel(t + 1) : "Ascend " + std::to_string(t + 1 - 3)); }
			return RelicDesc(c);
		}

		// ---- the Infusions ----
		constexpr const char* kInfName[kInfusions + 1] = { "None", "Blast Furnace", "Dead Wire", "Thunder Wall", "Cryo-Lock", "Turned", "Cluster", "Piercing", "Soul Siphon" };
		constexpr const char* kInfWhat[kInfusions + 1] = { "", "kills burst into flame", "hits chain lightning to four", "hits hurl the dead back",
			"hits freeze all round", "the struck fight for you", "hits split into three blasts", "through every foe in a line", "kills restore magicka and health" };
		// the items an Infusion can be set on: your kit's owned items (0..) and your relics (kRelicBase..)
		std::vector<int> InfusableItems()
		{
			std::vector<int> out;
			const int c = Cls();
			if (c < 0) return out;
			for (int i = 0; i < static_cast<int>(Defs(c).size()); ++i) if (g_cls[c].owned >> i & 1) out.push_back(i);
			if (c != 2) for (int r = 0; r < RelicCount(c) && r < kMaxRelics; ++r) if (g_cls[c].relics >> r & 1) out.push_back(kRelicBase + r);  // (the quivers carry none)
			return out;
		}
		std::uint8_t* InfusionOf(int a_cls, int a_item)
		{
			if (a_cls < 0 || a_cls > 3) return nullptr;
			if (a_item >= kRelicBase && a_item - kRelicBase < kMaxRelics) return &g_cls[a_cls].relicInfusion[a_item - kRelicBase];
			if (a_item >= 0 && a_item < kMaxItems) return &g_cls[a_cls].infusion[a_item];
			return nullptr;
		}
		int TierOfItem(int a_cls, int a_item)
		{
			if (a_item >= kRelicBase) return a_cls >= 0 && a_cls < 4 ? g_cls[a_cls].relicTier[std::clamp(a_item - kRelicBase, 0, kMaxRelics - 1)] : 0;
			return std::max(0, ItemTier(a_cls, a_item));
		}
		std::string NameOfItem(int a_cls, int a_item) { return a_item >= kRelicBase ? RelicName(a_cls, a_item - kRelicBase) : ItemName(a_cls, a_item); }
		int InfusionCost()
		{
			const int night = Save::Get(gen::kAV_round);
			if (night != g_rollNight) g_rollNight = night, g_rollsTonight = 0;
			const int base = 4000 + 2000 * g_rollsTonight;
			return PowerUps::FireSale() ? base / 2 : base;
		}

		// what hit last: the target's id -> the item and when (an on-kill Infusion, the Legendary Riftblade's thralls)
		struct LastHit { int item{ -1 }; std::chrono::steady_clock::time_point at{}; };
		std::unordered_map<RE::FormID, LastHit> g_last;
		std::unordered_map<int, std::chrono::steady_clock::time_point> g_procCool;  // (one proc of a kind at a time)
		bool Ready(int a_kind, int a_ms)
		{
			const auto now = std::chrono::steady_clock::now();
			auto& t = g_procCool[a_kind];
			if (now - t < std::chrono::milliseconds(a_ms)) return false;
			t = now;
			return true;
		}
		float Roll01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(g_rng); }
		// a proc's share of one of the dead's health, grown by the item's tier
		float ProcShare(int a_tier) { return 0.12f * UpgradeMult(std::min(a_tier, 5)) * (0.7f + 0.3f * Arcana::Power()); }  // (capped: Ascends grow the item's own blows, not its procs past two)
		std::vector<RE::Actor*> FoesNear(const RE::NiPoint3& a_at, float a_r, RE::Actor* a_skip = nullptr, std::size_t a_max = 64)
		{
			std::vector<RE::Actor*> out;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!a || a.get() == a_skip || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded() || !Director::IsFoe(a.get())) continue;
				if (a->GetPosition().GetDistance(a_at) <= a_r) out.push_back(a.get());
				if (out.size() >= a_max) break;
			}
			return out;
		}
		void Push(RE::Actor* a_from, RE::Actor* a_who, float a_force)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm || !a_from || !a_who) return;
			const auto hd = vm->GetObjectHandlePolicy()->GetHandleForObject(a_from->GetFormType(), a_from);
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			vm->DispatchMethodCall(hd, "ObjectReference", "PushActorAway", RE::MakeFunctionArguments(static_cast<RE::Actor*>(a_who), static_cast<float>(a_force)), none);
		}
		void OnHitItem(int a_cls, int a_item, RE::Actor* a_target)
		{
			if (a_cls < 0 || a_item < 0 || !a_target || !Director::IsFoe(a_target)) return;
			if (g_last.size() > 400) g_last.clear();
			g_last[a_target->GetFormID()] = { a_item, std::chrono::steady_clock::now() };
			auto* inf = InfusionOf(a_cls, a_item);
			const int kind = inf ? *inf : 0;
			if (kind <= 0 || a_target->IsDead()) return;
			const int   t = TierOfItem(a_cls, a_item);
			const float share = ProcShare(t);
			const auto  at = a_target->GetPosition();
			const bool  boss = Director::IsBoss(a_target);
			switch (kind) {
			case 2:  // Dead Wire: lightning leaps to four more
				if (Roll01() < 0.20f && Ready(2, 250)) {
					Gun::FxBlast("runeshock", at + RE::NiPoint3{ 0, 0, 60.0f });
					for (auto* f : FoesNear(at, 550.0f, a_target, 4)) {
						Gun::AreaHurt(f->GetPosition(), 40.0f, share, "");
						Gun::FxBlast("runeshock", f->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f });
					}
				}
				break;
			case 3:  // Thunder Wall: a shockwave hurls them back
				if (Roll01() < 0.15f && Ready(3, 3000)) {
					Gun::FxBlast("shoutarea", at);
					Gun::AreaHurt(at, 350.0f, share * 0.5f, "");
					for (auto* f : FoesNear(at, 350.0f, nullptr, 10)) if (!Director::IsBoss(f)) Push(RE::PlayerCharacter::GetSingleton(), f, 4.0f);
				}
				break;
			case 4:  // Cryo-Lock: everything round it frozen
				if (Roll01() < 0.15f && Ready(4, 400)) Director::Web(at, 260.0f, 3.0f, 95.0f, true);
				break;
			case 5:  // Turned: it fights for you
				if (!boss && Director::IsRoundUndead(a_target) && Roll01() < 0.08f && Ready(5, 300)) {
					Director::Frenzy(a_target, 15.0f);
					Gun::FxBlast("dark", at + RE::NiPoint3{ 0, 0, 70.0f });
				}
				break;
			case 6:  // Cluster: three blasts round it
				if (Roll01() < 0.20f && Ready(6, 300)) {
					for (int k = 0; k < 3; ++k) {
						const float ang = 2.094f * k + Roll01();
						const RE::NiPoint3 c = at + RE::NiPoint3{ std::cos(ang) * 120.0f, std::sin(ang) * 120.0f, 0.0f };
						Gun::AreaHurt(c, 150.0f, share * 0.7f, "");
						Gun::FxBlast("fireball", c + RE::NiPoint3{ 0, 0, 40.0f }, 0.08f * k);
					}
				}
				break;
			case 7:  // Piercing: on through every foe behind it in a line
				if (Ready(7, 200)) {
					if (auto* p = RE::PlayerCharacter::GetSingleton()) {
						RE::NiPoint3 dir = at - p->GetPosition();
						dir.z = 0;
						if (const float l = dir.Length(); l > 1.0f) {
							dir = dir * (1.0f / l);
							for (auto* f : FoesNear(at, 820.0f, a_target, 12)) {
								RE::NiPoint3 v = f->GetPosition() - at;
								const float  along = v.x * dir.x + v.y * dir.y;
								const float  side = std::abs(v.x * dir.y - v.y * dir.x);
								if (along > 0.0f && along < 800.0f && side < 110.0f) Gun::AreaHurt(f->GetPosition(), 30.0f, share * 0.6f, "");
							}
						}
					}
				}
				break;
			default: break;  // (1 and 8: on the kill)
			}
		}
		void Ui(RE::FormID a_id, float a_vol = 1.0f)
		{
			auto* d = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(a_id);
			auto* am = RE::BSAudioManager::GetSingleton();
			RE::BSSoundHandle h;
			if (d && am && am->GetSoundHandle(h, static_cast<RE::BSISoundDescriptor*>(d))) {
				if (a_vol < 0.999f) h.SetVolume(a_vol);
				h.Play();
			}
		}
		constexpr RE::FormID kUiMove = 0x0006CC6F, kUiLand = 0x00068D83;
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
				const bool rift = std::string_view(defs[i].ref) == "riftblade";
				auto*      w = rift ? Classes::Riftblade() : Classes::SpellswordBlade();
				Look(w, t);
				// (the Riftblade's fire, the Madness Sword's violet fire: brighter each tier)
				const bool mad = !rift && w && w == RE::TESDataHandler::GetSingleton()->LookupForm(gen::kMadnessSword, gen::kPlugin);
				if ((rift || mad) && w) {
					// (Noah: "remove the charge bar - it doesn't even use charge") no charge on it, nor on the one you carry
					w->amountofEnchantment = 0;
					if (auto* p = RE::PlayerCharacter::GetSingleton()) {
						if (auto* inv = p->GetInventoryChanges(); inv && inv->entryList) {
							for (auto* e : *inv->entryList) {
								if (!e || e->object != w || !e->extraLists) continue;
								for (auto* x : *e->extraLists) if (x && x->HasType(RE::ExtraDataType::kCharge)) x->RemoveByType(RE::ExtraDataType::kCharge);
							}
						}
					}
					auto*       orig = g_base[w->GetFormID()].ench;
					auto*       want = t <= 0 ? orig : RE::TESDataHandler::GetSingleton()->LookupForm<RE::EnchantmentItem>((rift ? gen::kRiftEnch : gen::kMadEnch)[std::clamp(t, 1, 3) - 1], gen::kPlugin);
					if (want && w->formEnchanting != want) {
						w->formEnchanting = want;
						// (the blade in hand: taken off and on again, so its new fire shows)
						auto* p = RE::PlayerCharacter::GetSingleton();
						auto* em = RE::ActorEquipManager::GetSingleton();
						if (a_reequip && p && em && !ItemMenusOpen() && p->GetEquippedObject(false) == w) {
							em->UnequipObject(p, w, nullptr, 1, nullptr, false, false, false, true);
							em->EquipObject(p, w, nullptr, 1, nullptr, false, false, false, true);
						}
					}
				}
				break;
			}
			case Kind::Bow: if (const int b = BowIndex(defs[i].ref); b >= 0) if (auto* f = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kBows[b].weap, gen::kPlugin)) Look(f, t); break;
			case Kind::Crossbow: Look(Classes::CrossbowForm(Elem(defs[i])), t); break;
			default: break;
			}
		}
		if (c == 3) for (int r = 0; r < RelicCount(3) && r < kMaxRelics; ++r) if (g_cls[3].relics >> r & 1) Look(Classes::TwoHander(r), g_cls[3].relicTier[r]);
	}

	std::string_view InfusionName(int a_kind) { return a_kind >= 0 && a_kind <= kInfusions ? kInfName[a_kind] : "None"; }

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
		for (int r = 0; r < RelicCount(c) && r < kMaxRelics; ++r) if (g_cls[c].relics >> r & 1) out.push_back(40 + kRelicBase + r);
		std::vector<int> locked;
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (!(g_cls[c].owned >> i & 1)) locked.push_back(i);
		std::stable_sort(locked.begin(), locked.end(), [&](int a, int b) { return defs[a].at < defs[b].at; });
		for (const int i : locked) out.push_back(100 + i);
		return out;
	}

	void Fill(Hud::State& a_h)
	{
		a_h.kitLines.clear(), a_h.armoryLines.clear();
		a_h.relicTokens = 0, a_h.infuseItem.clear(), a_h.infuseNow.clear(), a_h.infuseSpin.clear(), a_h.infuseSpinning = false;
		const int c = Cls();
		if (c < 0) return;
		const int  freed = Freed();
		const int  pts = Points::Get();
		const auto defs = Defs(c);
		const auto& st = g_cls[c];
		auto line = [&](int a_id, int a_item, bool a_relic) {
			Hud::State::KitLine l;
			l.id = a_id, l.relic = a_relic;
			const int t = TierOfItem(c, a_item);
			l.name = NameOfItem(c, a_item);
			l.tier = std::min(t, 3), l.asc = std::max(0, t - 3);
			l.color = a_relic ? 0xE8C060 : ItemColor(c, a_item);
			if (auto* inf = InfusionOf(c, a_item); inf && *inf) l.infusion = kInfName[*inf];
			const bool quiver = a_relic && c == 2;
			l.carried = a_relic || Carried(c, a_item);
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
					l.info = std::string(TierLabel(t)) + "  ->  " + (next <= 3 ? std::string(TierLabel(next)) : "Ascend " + std::to_string(next - 3)) + (quiver ? "  (a wider reach)" : "");
					l.can = pts >= l.cost;
				}
			}
			a_h.kitLines.push_back(std::move(l));
		};
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (st.owned >> i & 1) line(40 + i, i, false);
		for (int r = 0; r < RelicCount(c) && r < kMaxRelics; ++r) if (st.relics >> r & 1) line(40 + kRelicBase + r, kRelicBase + r, true);
		std::vector<int> locked;
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (!(st.owned >> i & 1)) locked.push_back(i);
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
		// the Infusion row
		const auto inf = InfusableItems();
		if (!inf.empty()) {
			g_infuseSel = std::clamp(g_infuseSel, 0, static_cast<int>(inf.size()) - 1);
			const int it = inf[g_infuseSel];
			a_h.infuseItem = NameOfItem(c, it);
			if (auto* p = InfusionOf(c, it)) a_h.infuseNow = *p ? std::string(kInfName[*p]) + " - " + kInfWhat[*p] : std::string("no Infusion yet");
			a_h.infuseCost = InfusionCost();
			a_h.infuseCan = pts >= a_h.infuseCost && g_spin.left <= 0;
			if (g_spin.left > 0) a_h.infuseSpinning = true, a_h.infuseSpin = kInfName[std::clamp(g_spin.shown, 1, kInfusions)];
		}
		// the relic waiting
		a_h.relicTokens = g_tokens;
		if (g_tokens > 0) {
			if (g_offer[0] < 0 || g_offerCls != c) MakeOffer();
			a_h.relicA = OfferName(g_offer[0]), a_h.relicB = OfferName(g_offer[1]);
			a_h.relicDescA = OfferDesc(g_offer[0]), a_h.relicDescB = OfferDesc(g_offer[1]);
			if (g_offer[0] < 0) a_h.relicA = "Nothing to take yet", a_h.relicDescA = "your items wait for more freed cities - it keeps";
			a_h.relicSel = g_offer[1] < 0 ? 0 : g_relicSel;
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
			if (i < 0 || i >= static_cast<int>(defs.size()) || (st.owned >> i & 1)) return false;
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
		if (item >= kRelicBase) {  // a relic: one step up
			const int r = item - kRelicBase;
			if (r < 0 || r >= kMaxRelics || !(st.relics >> r & 1)) return false;
			const int t = st.relicTier[r];
			if (t >= kMaxTier) { Hud::Banner(RelicName(c, r) + " has ascended as far as it goes", 2.5f); return false; }
			if (freed < UpgradeGate(t + 1) && !Testing()) { Hud::Banner(fmt::format("Its next tier waits for {} cities freed ({} of {})", UpgradeGate(t + 1), freed, UpgradeGate(t + 1)), 3.0f); return false; }
			if (!Points::Spend(UpgradeCost(t + 1))) return false;
			++st.relicTier[r];
			st.upgrades = static_cast<std::uint8_t>(std::min(255, st.upgrades + 1));
			if (a_altar) Forms::PlayAt(gen::Sound::forge_upgrade, a_altar);
			Hud::Banner(RelicName(c, r) + "  -  " + (t + 1 <= 3 ? std::string(TierLabel(t + 1)) : "Ascend " + std::to_string(t + 1 - 3)), 4.0f);
			return true;
		}
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
		Hud::Banner((now != was ? was + "  ->  " + now : now) + "  -  " + (t + 1 <= 3 ? std::string(TierLabel(t + 1)) : "Ascend " + std::to_string(t + 1 - 3)) + fmt::format("   (armour +{:.0f}%)", ArmorResist() * 100.0f), 4.5f);
		logger::info("kit: {} -> tier {} ({})", now, t + 1, Classes::Name(c));
		return true;
	}

	void InfuseSide(int a_dir)
	{
		const auto inf = InfusableItems();
		if (inf.empty() || g_spin.left > 0) return;
		g_infuseSel = (g_infuseSel + a_dir + static_cast<int>(inf.size())) % static_cast<int>(inf.size());
	}
	bool Infuse()
	{
		const int  c = Cls();
		const auto inf = InfusableItems();
		if (c < 0 || inf.empty() || g_spin.left > 0) return false;
		const int it = inf[std::clamp(g_infuseSel, 0, static_cast<int>(inf.size()) - 1)];
		const int cost = InfusionCost();
		if (!Points::Spend(cost)) return false;
		++g_rollsTonight;
		auto* cur = InfusionOf(c, it);
		int   r = 1 + std::uniform_int_distribution<int>(0, kInfusions - 1)(g_rng);
		if (cur && r == *cur) r = r % kInfusions + 1;  // (never the same twice in a row)
		g_spin = { 1.6f, 0.0f, it, r, 1, c };
		Ui(kUiLand, 0.6f);
		return true;
	}
	void RelicSide(int a_dir)
	{
		if (g_offer[1] >= 0) g_relicSel = (g_relicSel + a_dir + 2) % 2;
	}
	bool TakeRelic()
	{
		const int c = Cls();
		if (c < 0 || g_tokens <= 0 || ItemMenusOpen()) return false;
		if (g_offer[0] < 0 || g_offerCls != c) MakeOffer();
		const int o = g_offer[std::clamp(g_offer[1] < 0 ? 0 : g_relicSel, 0, 1)];
		if (o < 0) return false;
		auto& st = g_cls[c];
		if (o >= 100) {  // a free upgrade
			const int i = o - 100;
			const std::string was = ItemName(c, i);
			if (!Step(c, i)) { MakeOffer(); return false; }
			st.upgrades = static_cast<std::uint8_t>(std::min(255, st.upgrades + 1));
			Hud::Banner("THE PRIEST'S BLESSING - " + was + " grows", 4.5f);
		} else {
			if (o >= RelicCount(c) || o >= kMaxRelics) { MakeOffer(); return false; }
			st.relics |= 1u << o;
			if (auto* sp = RelicSpell(c, o)) Classes::LearnSpell(sp);
			if (c == 3) if (auto* w = Classes::TwoHander(o)) Classes::GiveGear(w, 1, false);
			Forms::Play(gen::Sound::shout_learned);
			Hud::Banner("A DRAGON PRIEST'S RELIC - " + RelicName(c, o), 5.0f);
			logger::info("kit: relic {} taken ({})", RelicName(c, o), Classes::Name(c));
		}
		--g_tokens;
		g_offer[0] = g_offer[1] = -1;
		return true;
	}
	int  RelicTokens() { return g_tokens; }
	void OnPriestSlain()
	{
		++g_tokens;
		g_offer[0] = g_offer[1] = -1;
		Hud::Banner("THE DRAGON PRIEST'S RELIC - take it at an Echo Altar", 5.0f);
		logger::info("kit: a Dragon Priest's relic to take ({} waiting)", g_tokens);
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
			auto* w = std::string_view(defs[i].ref) == "riftblade" ? Classes::Riftblade() : Classes::SpellswordBlade();
			if (w && w == a_w) return g_cls[c].tier[i];
		}
		if (c == 3) {
			for (int r = 0; r < RelicCount(3) && r < kMaxRelics; ++r) {
				if ((g_cls[3].relics >> r & 1) && Classes::TwoHander(r) == a_w) return g_cls[3].relicTier[r];
			}
		}
		return -1;
	}
	int TierOfSpell(const RE::TESForm* a_sp)
	{
		const int c = Cls();
		if (!a_sp || (c != 0 && c != 1)) return -1;
		for (int r = 0; r < RelicCount(c) && r < kMaxRelics; ++r) {
			if ((g_cls[c].relics >> r & 1) && RelicSpell(c, r) == a_sp) return g_cls[c].relicTier[r];
		}
		return -1;
	}
	int TierOfSummon(RE::Actor* a_summon)
	{
		if (a_summon && Classes::IsRiftSummon(a_summon)) return RiftTier();  // (the Riftblade's atronachs hit by its tier)
		auto* base = a_summon ? a_summon->GetActorBase() : nullptr;
		if (!base || Cls() != 0) return -1;
		for (int r = 0; r < RelicCount(0) && r < kMaxRelics; ++r) {
			if (!(g_cls[0].relics >> r & 1)) continue;
			if (auto* sp = RelicSpell(0, r)) {
				for (auto* e : sp->effects) {
					auto* m = e ? e->baseEffect : nullptr;
					if (m && m->data.associatedForm && (m->data.associatedForm == base || (m->data.associatedForm->GetName() && base->GetName() && std::string_view(m->data.associatedForm->GetName()) == base->GetName()))) return g_cls[0].relicTier[r];
				}
			}
		}
		return -1;
	}
	int BowAscends(int a_bow)
	{
		const auto defs = Defs(2);
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (defs[i].kind == Kind::Bow && BowIndex(defs[i].ref) == a_bow) return g_cls[2].tier[i];
		return 0;
	}
	int CrossbowAscends(int a_elem)
	{
		const auto defs = Defs(2);
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if (defs[i].kind == Kind::Crossbow && Elem(defs[i]) == a_elem) return g_cls[2].tier[i];
		return 0;
	}
	int RiftTier() { return g_cls[0].owned & 1 ? g_cls[0].tier[0] : 0; }
	float QuiverRadiusMult()
	{
		if (Cls() != 2) return 1.0f;
		float m = 1.0f;
		for (int r = 0; r < 3; ++r) if (g_cls[2].relics >> r & 1) m += 0.15f + 0.05f * g_cls[2].relicTier[r];
		return std::min(m, 2.5f);
	}
	float ArmorResist() { auto* s = Me(); return s ? std::min(0.30f, 0.015f * s->upgrades) : 0.0f; }

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
	float AuraCostMult() { return PartyHas(1) ? 0.9f : 1.0f; }
	float AuraAmmoMult() { return PartyHas(2) ? 0.8f : 1.0f; }
	float AuraMeleeTaken() { return PartyHas(3) ? 0.9f : 1.0f; }

	// ---- the Infusions' hooks ----
	void OnPlayerHit(RE::Actor* a_target, const RE::TESForm* a_source)
	{
		const int c = Cls();
		if (c < 0 || !a_target || !a_source) return;
		const auto defs = Defs(c);
		if (auto* w = a_source->As<RE::TESObjectWEAP>()) {
			for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
				if (!(g_cls[c].owned >> i & 1)) continue;
				const auto& d = defs[i];
				const bool me = (d.kind == Kind::Blade && w == (std::string_view(d.ref) == "riftblade" ? Classes::Riftblade() : Classes::SpellswordBlade())) ||
				                (d.kind == Kind::Crossbow && w == Classes::CrossbowForm(Elem(d)));
				if (me) return OnHitItem(c, i, a_target);
			}
			if (c == 3) for (int r = 0; r < RelicCount(3) && r < kMaxRelics; ++r) if ((g_cls[3].relics >> r & 1) && Classes::TwoHander(r) == w) return OnHitItem(c, kRelicBase + r, a_target);
			return;
		}
		if (const int si = Arcana::IndexOf(a_source); si >= 0) {
			for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
				if ((g_cls[c].owned >> i & 1) && defs[i].kind == Kind::Ladder && Arcana::LadderName(si) == defs[i].ref && Arcana::ClassOf(si) == c) return OnHitItem(c, i, a_target);
			}
			return;
		}
		if (c == 1) for (int r = 0; r < RelicCount(1) && r < kMaxRelics; ++r) if ((g_cls[1].relics >> r & 1) && RelicSpell(1, r) == a_source) return OnHitItem(c, kRelicBase + r, a_target);
	}
	void OnBowHit(int a_bow, RE::Actor* a_target)
	{
		if (Cls() != 2) return;
		const auto defs = Defs(2);
		for (int i = 0; i < static_cast<int>(defs.size()); ++i) if ((g_cls[2].owned >> i & 1) && defs[i].kind == Kind::Bow && BowIndex(defs[i].ref) == a_bow) return OnHitItem(2, i, a_target);
	}
	void OnSummonHit(RE::Actor* a_summon, RE::Actor* a_target)
	{
		if (Cls() != 0 || !a_summon) return;
		if (const int fam = Classes::SummonFamily(a_summon); fam >= 0) {
			const auto defs = Defs(0);
			for (int i = 0; i < static_cast<int>(defs.size()); ++i) if ((g_cls[0].owned >> i & 1) && defs[i].kind == Kind::Ladder && Arcana::LadderName(fam) == defs[i].ref) return OnHitItem(0, i, a_target);
			return;
		}
		auto* base = a_summon->GetActorBase();
		for (int r = 0; r < RelicCount(0) && r < kMaxRelics && base; ++r) {
			if (!(g_cls[0].relics >> r & 1)) continue;
			if (auto* sp = RelicSpell(0, r)) for (auto* e : sp->effects) if (e && e->baseEffect && e->baseEffect->data.associatedForm == base) return OnHitItem(0, kRelicBase + r, a_target);
		}
	}
	void OnKill(RE::Actor* a_dying)
	{
		const int c = Cls();
		if (c < 0 || !a_dying) return;
		auto it = g_last.find(a_dying->GetFormID());
		if (it == g_last.end() || std::chrono::steady_clock::now() - it->second.at > std::chrono::seconds(3)) return;
		const int item = it->second.item;
		g_last.erase(it);
		auto* inf = InfusionOf(c, item);
		const int kind = inf ? *inf : 0;
		const float share = ProcShare(TierOfItem(c, item));
		const auto  at = a_dying->GetPosition();
		if (kind == 1 && Ready(1, 150)) {  // Blast Furnace
			Gun::AreaHurt(at, 300.0f, share * 1.5f, "");
			Gun::FxBlast("fireball", at + RE::NiPoint3{ 0, 0, 50.0f });
		} else if (kind == 8) {  // Soul Siphon
			if (auto* p = RE::PlayerCharacter::GetSingleton(); p && !p->IsDead()) {
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kMagicka, av->GetPermanentActorValue(RE::ActorValue::kMagicka) * 0.08f);
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.03f);
			}
		}
	}

	void Tick(float a_delta)
	{
		// the Infusion's spin: the names flicker, slowing, then it lands
		if (g_spin.left > 0) {
			g_spin.left -= a_delta;
			g_spin.flick -= a_delta;
			if (g_spin.flick <= 0) {
				g_spin.flick = 0.06f + 0.18f * (1.0f - std::clamp(g_spin.left / 1.6f, 0.0f, 1.0f));
				g_spin.shown = g_spin.shown % kInfusions + 1;
				Ui(kUiMove, 0.5f);
			}
			if (g_spin.left <= 0) {
				const int c = g_spin.cls;  // (the class it was rolled for, even if it changed mid-spin)
				if (auto* p = InfusionOf(c, g_spin.item)) *p = static_cast<std::uint8_t>(g_spin.result);
				g_spin.shown = g_spin.result;
				Forms::Play(gen::Sound::forge_upgrade);
				Hud::Banner(NameOfItem(c, g_spin.item) + "  -  INFUSED: " + std::string(kInfName[g_spin.result]) + " (" + kInfWhat[g_spin.result] + ")", 5.0f);
				logger::info("kit: {} infused with {}", NameOfItem(c, g_spin.item), kInfName[g_spin.result]);
				g_spin.left = 0;
			}
		}
		// (co-op) your class, told to the party now and then; a party aura that came or went sets the spells' costs again
		static float tell = 0;
		if ((tell -= a_delta) <= 0) {
			tell = 5.0f;
			if (Net::InLobby() && Cls() >= 0) Net::TellClass(Cls());
			static float costWas = 1.0f;
			if (const float cm = AuraCostMult(); cm != costWas) costWas = cm, Arcana::ReapplyAll();
		}
		// (Noah: "upgrading the Riftblade didn't change anything on the inventory stats", "the effects should enhance the
		// current detail") every kit weapon's card follows its tier - its name and its damage - and the Riftblade's own fire
		// burns brighter each tier
		static float look = 0;
		if ((look -= a_delta) <= 0) {
			look = 1.0f;
			SyncWeapons(false);
		}
	}
	// ---- the co-save ----
	std::vector<std::uint8_t> State()
	{
		std::vector<std::uint8_t> v;
		auto put32 = [&](std::uint32_t x) { for (int b = 0; b < 4; ++b) v.push_back(static_cast<std::uint8_t>(x >> (8 * b))); };
		v.push_back(1);  // (format)
		for (const auto& s : g_cls) {
			put32(s.owned);
			v.insert(v.end(), s.tier.begin(), s.tier.end());
			v.insert(v.end(), s.infusion.begin(), s.infusion.end());
			put32(s.relics);
			v.insert(v.end(), s.relicTier.begin(), s.relicTier.end());
			v.insert(v.end(), s.relicInfusion.begin(), s.relicInfusion.end());
			v.push_back(s.upgrades);
		}
		v.push_back(static_cast<std::uint8_t>(std::clamp(g_tokens, 0, 255)));
		return v;
	}
	void SetState(const std::vector<std::uint8_t>& a_v)
	{
		Revert();
		std::size_t k = 0;
		auto get32 = [&]() { std::uint32_t x = 0; for (int b = 0; b < 4 && k < a_v.size(); ++b) x |= static_cast<std::uint32_t>(a_v[k++]) << (8 * b); return x; };
		auto getArr = [&](auto& arr) { for (auto& e : arr) e = k < a_v.size() ? a_v[k++] : 0; };
		if (a_v.empty() || a_v[k++] != 1) return;
		for (auto& s : g_cls) {
			s.owned = get32();
			getArr(s.tier);
			getArr(s.infusion);
			s.relics = get32();
			getArr(s.relicTier);
			getArr(s.relicInfusion);
			s.upgrades = k < a_v.size() ? a_v[k++] : 0;
			for (auto& i : s.infusion) if (i > kInfusions) i = 0;
			for (auto& i : s.relicInfusion) if (i > kInfusions) i = 0;
		}
		g_tokens = k < a_v.size() ? a_v[k++] : 0;
		g_loaded = true;
		logger::info("kit: loaded ({} relics waiting)", g_tokens);
	}
	void Revert()
	{
		g_cls = {};
		g_tokens = 0, g_offer[0] = g_offer[1] = -1, g_offerCls = -1, g_relicSel = 0, g_infuseSel = 0, g_rollsTonight = 0, g_rollNight = -1, g_loaded = false;
		g_spin = {};
		g_last.clear(), g_procCool.clear();
	}
	bool Loaded() { return g_loaded; }
}
