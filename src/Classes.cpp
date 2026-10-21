#include "BO3Z.h"
#include "CofferSpells.h"

// The four classes (Noah, the classes overhaul): Summoner, Destruction, Ranger, Spellsword. A new character picks one
// on a full-screen picker the moment it is out of the character creator; the pick hands over the class's starter kit.
// Any Echo Altar can switch class for septims: the class you leave keeps its spells and how far they have climbed
// (they come back when you return to it), the class you take up gets its kit the first time.
// Co-save 'CLAS': [class+1] then, per class, [kit given][n] and n x [spell index, level].
namespace bo3z::Classes
{
	namespace
	{
		std::atomic<int>  g_class{ -1 };
		std::atomic<bool> g_open{ false };
		std::atomic<int>  g_sel{ 0 };
		std::atomic<bool> g_switching{ false };
		std::array<bool, 4>                                    g_kit{};
		std::array<std::vector<std::pair<int, int>>, 4>        g_stash;  // the spells a class you left carried (index, level)
		std::chrono::steady_clock::time_point                  g_closedAt{};
		std::array<std::uint16_t, coffer::kLadders>            g_mastery{};  // kills made with each ladder's spell in hand (its summons' kills for the Summoner)

		RE::TESForm* Lookup(std::uint32_t a_id, std::string_view a_plugin)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm(a_id, a_plugin) : nullptr;
		}
		int Rung1(int a_cls, std::string_view a_ladderLabel)
		{
			for (int i = 0; i < static_cast<int>(coffer::kSpells.size()); ++i) {
				const auto& s = coffer::kSpells[i];
				if (s.cls == a_cls && s.rung == 1 && a_ladderLabel == s.ladderLabel) return i;
			}
			return -1;
		}
		void GiveItem(RE::PlayerCharacter* a_p, RE::TESForm* a_form, int a_count, bool a_equip)
		{
			auto* obj = a_form ? a_form->As<RE::TESBoundObject>() : nullptr;
			if (!obj) return;
			a_p->AddObjectToContainer(obj, nullptr, a_count, nullptr);
			if (a_equip) {
				if (auto* em = RE::ActorEquipManager::GetSingleton()) em->EquipObject(a_p, obj, nullptr, 1, nullptr, false, true, false, true);
			}
		}
		void GiveSpell(int a_index, bool a_left)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* sp = Arcana::Spell(a_index);
			if (!p || !sp) return;
			Arcana::Give(a_index, false);
			if (auto* em = RE::ActorEquipManager::GetSingleton()) {
				em->EquipSpell(p, sp, RE::TESForm::LookupByID<RE::BGSEquipSlot>(a_left ? 0x13F43 : 0x13F42));
			}
		}

		void GiveKit(int a_cls)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			const bool female = p->GetActorBase() && p->GetActorBase()->IsFemale();
			switch (a_cls) {
			case coffer::kSummoner:
				GiveSpell(Rung1(a_cls, "The Dead"), false);
				GiveSpell(Rung1(a_cls, "Beasts"), true);
				if (female) {
					for (std::uint32_t id : { 0xE58u, 0xE5Bu, 0xE5Du, 0xE5Fu, 0xE63u }) {
						GiveItem(p, Lookup(id, "Wandering Summoner Outfit.esp"), 1, true);
					}
				} else {  // (Noah: a male Summoner wears a man's summoner robes) the hooded necromancer's robes of conjuration and boots
					GiveItem(p, RE::TESForm::LookupByID(0x10900E), 1, true);
					GiveItem(p, RE::TESForm::LookupByID(0x0C36E8), 1, true);
				}
				break;
			case coffer::kDestruction:
				GiveSpell(Rung1(a_cls, "Fire"), false);
				GiveSpell(Rung1(a_cls, "Frost"), true);
				for (std::uint32_t id : { 0x808u, 0x809u, 0x80Bu, 0x811u, 0x80Du }) {
					GiveItem(p, Lookup(id, "ksws03.esp"), 1, true);
				}
				GiveItem(p, Lookup(0x812, "ksws03.esp"), 1, false);  // Sweeper of the Sorceress
				if (auto* tome = Lookup(0x817, "Equipable Tomes - Redone.esp")) GiveItem(p, tome, 1, true);  // a Destruction tome at the hip
				break;
			case coffer::kRanger:
				if (const int b = Bows::Roll(); b >= 0) Bows::Give(b);
				for (std::uint32_t id : { 0x857u, 0x858u, 0x859u }) {  // (Noah) the Drifter Outfit, light armoured
					GiveItem(p, Lookup(id, "DrifterArmorAndOutfitByXtudo.esp"), 1, true);
				}
				GiveItem(p, Lookup(0x002F9A, "Faction Crossbows.esp"), 1, false);  // Dawnguard Crossbow
				GiveItem(p, Lookup(0x020B04, "Faction Crossbows.esp"), 30, false);  // Dawnguard Bolts
				break;
			case coffer::kSpellsword:
				for (std::uint32_t id : { 0x800u, 0x802u, 0x805u }) {  // (Noah) the Practical Pirate outfit
					GiveItem(p, Lookup(id, "practical_pirate.esp"), 1, true);
				}
				GiveItem(p, Lookup(0x00B550, "Spellsword.esp"), 1, true);   // Steel Spellsword
				GiveItem(p, Lookup(0x00B569, "Spellsword.esp"), 1, false);  // Steel Spellshield
				if (auto* ww = Lookup(0x00087C, "The Wizard Warrior.esp")) {
					if (auto* sp = ww->As<RE::SpellItem>()) p->AddSpell(sp);
				}
				GiveSpell(Rung1(a_cls, "Fire"), true);
				break;
			}
			g_kit[a_cls] = true;
			logger::info("classes: {} starter kit given", coffer::kClassName[a_cls]);
		}

		// the class's spells off you (kept for when you come back to it), the new class's back on
		void Swap(int a_from, int a_to)
		{
			if (a_from >= 0 && a_from < 4) {
				g_stash[a_from].clear();
				for (int i : Arcana::Carried()) {
					g_stash[a_from].push_back({ i, Arcana::Level(i) });
				}
				for (auto [i, lv] : g_stash[a_from]) Arcana::Forget(i);
			}
			if (!g_kit[a_to]) {
				GiveKit(a_to);
			} else {
				bool left = false;
				for (auto [i, lv] : g_stash[a_to]) {
					GiveSpell(i, left);
					Arcana::SetLevel(i, lv);
					left = !left;
				}
			}
		}
	}

	int  Get() { return g_class.load(); }
	bool Open() { return g_open.load(); }
	int  Selected() { return g_sel.load(); }
	bool Switching() { return g_switching.load(); }
	std::string_view Name(int a_cls) { return a_cls >= 0 && a_cls < 4 ? coffer::kClassName[a_cls] : "None"; }
	int  SwitchCost() { return 2500; }

	void OpenPicker(bool a_switch)
	{
		g_switching = a_switch;
		g_sel = std::max(0, g_class.load());
		g_open = true;
		Hud::Banner(a_switch ? "Choose your new class" : "Choose your class", 3.0f);
	}

	void Choose(int a_cls)
	{
		if (a_cls < 0 || a_cls >= 4) return;
		const int from = g_class.load();
		if (g_switching && a_cls == from) {
			g_open = false;
			return;
		}
		if (g_switching && !Points::Spend(SwitchCost())) {
			return;
		}
		g_class = a_cls;
		Swap(from, a_cls);
		g_open = false;
		g_closedAt = std::chrono::steady_clock::now();
		Forms::Play(gen::Sound::quest_step);
		{ std::string u(coffer::kClassName[a_cls]); for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); Hud::Banner(u, 4.0f); }
		logger::info("classes: {} -> {}{}", Name(from), Name(a_cls), g_switching ? " (switched at an altar)" : "");
	}

	// a new character, out of the creator: the picker
	void OnNewCharacter()
	{
		if (g_class.load() < 0) OpenPicker(false);
	}
	// a save loaded with no class yet (older saves): the picker too
	void OnWorldReady()
	{
		if (g_class.load() < 0 && Save::Flag(Save::kFlagStarted)) OpenPicker(false);
	}

	// (each class grows its own way) a kill trains the ladders of what you hold: the spell in each hand for Destruction and
	// the Spellsword, and your summons' kills (or your own, a summon spell in hand) for the Summoner
	void OnKill(bool a_bySummon)
	{
		for (int h : Arcana::Hands()) {
			if (h < 0) continue;
			const int cls = Arcana::ClassOf(h);
			if (a_bySummon && cls != coffer::kSummoner) continue;
			auto& m = g_mastery[coffer::kSpells[h].ladder];
			if (m < 60000) ++m;
		}
	}
	int Mastery(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? g_mastery[coffer::kSpells[a_index].ladder] : 0; }
	// kills a ladder needs before its next rung can be raised: 25 to rung 2, 60 to rung 3, 120 to the Legendary
	int MasteryNeed(int a_index)
	{
		const int r = Arcana::RungOf(a_index);
		return r <= 1 ? 25 : r == 2 ? 60 : 120;
	}
	void SpendMastery(int a_index)
	{
		if (a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size())) g_mastery[coffer::kSpells[a_index].ladder] = 0;
	}

	// (the Summoner's command) how many summons you may hold: 1, then 2 / 3 / 4 as its ladders climb (each rung raised
	// past the first counts): our hidden perks set Skyrim's commanded-actor limit
	void Tick()
	{
		static auto last = std::chrono::steady_clock::now();
		if (std::chrono::steady_clock::now() - last < std::chrono::seconds(1)) return;
		last = std::chrono::steady_clock::now();
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!p || !dh) return;
		int climbs = 0;
		if (g_class.load() == coffer::kSummoner) {
			for (int i : Arcana::Carried()) {
				if (Arcana::ClassOf(i) == coffer::kSummoner) climbs += Arcana::RungOf(i) - 1;
			}
		}
		const int cap = g_class.load() != coffer::kSummoner ? 1 : climbs >= 6 ? 4 : climbs >= 3 ? 3 : climbs >= 1 ? 2 : 1;
		for (int n = 2; n <= 4; ++n) {
			auto* perk = dh->LookupForm<RE::BGSPerk>(gen::kSummonCapPerks[n - 2], gen::kPlugin);
			if (!perk) continue;
			const bool want = n == cap, have = p->HasPerk(perk);
			if (want && !have) {
				p->AddPerk(perk);
				Hud::Banner("Your command grows: " + std::to_string(n) + " summons at once", 3.0f);
				logger::info("classes: summon cap {}", n);
			} else if (!want && have) {
				p->RemovePerk(perk);
			}
		}
	}

	bool OnButton(const RE::ButtonEvent* a_b)
	{
		if (!g_open || !a_b || !a_b->IsDown()) return false;
		const auto code = a_b->GetIDCode();
		int sel = g_sel;
		if (a_b->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
			if (code >= 0x02 && code <= 0x05) { sel = static_cast<int>(code) - 0x02; g_sel = sel; Choose(sel); return true; }  // 1-4
			if (code == 0xCB || code == 0x1E) g_sel = (sel + 3) % 4;          // Left / A
			else if (code == 0xCD || code == 0x20) g_sel = (sel + 1) % 4;     // Right / D
			else if (code == 0x1C || code == 0x9C || code == 0x12) Choose(sel);  // Enter / E
			else if ((code == 0x0E || code == 0x01) && g_switching) g_open = false;  // Backspace / Esc: keep your class
			return true;
		}
		if (a_b->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
			if (code == 0x0004) g_sel = (sel + 3) % 4;       // D-pad left
			else if (code == 0x0008) g_sel = (sel + 1) % 4;  // D-pad right
			else if (code == 0x1000) Choose(sel);           // A
			else if (code == 0x2000 && g_switching) g_open = false;  // B
			return true;
		}
		if (a_b->GetDevice() == RE::INPUT_DEVICE::kMouse && code == 0) {
			Choose(sel);
			return true;
		}
		return false;
	}
	void Hover(int a_cls) { if (g_open && a_cls >= 0 && a_cls < 4) g_sel = a_cls; }
	bool MenuEats(RE::InputEvent* a_event)
	{
		if (!a_event || !a_event->AsButtonEvent()) return false;
		return g_open || std::chrono::steady_clock::now() - g_closedAt < std::chrono::milliseconds(350);
	}

	std::vector<std::uint8_t> State()
	{
		std::vector<std::uint8_t> v{ static_cast<std::uint8_t>(g_class.load() + 1) };
		v.push_back(static_cast<std::uint8_t>(coffer::kLadders));
		for (auto m : g_mastery) { v.push_back(static_cast<std::uint8_t>(m & 0xFF)); v.push_back(static_cast<std::uint8_t>(m >> 8)); }
		const int cur = g_class.load();
		for (int c = 0; c < 4; ++c) {
			std::vector<std::pair<int, int>> list = g_stash[c];
			if (c == cur) {
				list.clear();
				for (int i : Arcana::Carried()) list.push_back({ i, Arcana::Level(i) });
			}
			v.push_back(g_kit[c] ? 1 : 0);
			v.push_back(static_cast<std::uint8_t>(std::min<std::size_t>(list.size(), 255)));
			for (auto [i, lv] : list) {
				v.push_back(static_cast<std::uint8_t>(i));
				v.push_back(static_cast<std::uint8_t>(lv));
			}
		}
		return v;
	}
	void SetState(const std::vector<std::uint8_t>& a_v)
	{
		g_class = -1, g_kit.fill(false);
		for (auto& s : g_stash) s.clear();
		if (a_v.empty()) return;
		g_class = static_cast<int>(a_v[0]) - 1;
		std::size_t k = 1;
		g_mastery.fill(0);
		if (k < a_v.size()) {
			const int nl = a_v[k++];
			for (int l = 0; l < nl && k + 1 < a_v.size() + 1 && k + 2 <= a_v.size(); ++l, k += 2) {
				if (l < coffer::kLadders) g_mastery[l] = static_cast<std::uint16_t>(a_v[k] | (a_v[k + 1] << 8));
			}
		}
		for (int c = 0; c < 4 && k + 1 < a_v.size() + 1; ++c) {
			if (k + 2 > a_v.size()) break;
			g_kit[c] = a_v[k] != 0;
			const int n = a_v[k + 1];
			k += 2;
			for (int j = 0; j < n && k + 1 < a_v.size() + 1; ++j, k += 2) {
				if (k + 2 > a_v.size()) break;
				if (c != g_class) g_stash[c].push_back({ a_v[k], a_v[k + 1] });
			}
		}
		logger::info("classes: loaded ({})", Name(g_class));
	}
	void Revert() { SetState({}); g_open = false; g_mastery.fill(0); }
}
