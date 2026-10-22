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
			if (a_count > 0) a_p->AddObjectToContainer(obj, nullptr, a_count, nullptr);
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

		void Console(RE::TESObjectREFR* a_ref, const char* a_cmd)
		{
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>(); factory && a_ref) {
				if (auto* script = factory->Create()) {
					script->SetCommand(a_cmd);
					script->CompileAndRun(a_ref);
					delete script;
				}
			}
		}
		// (Noah: a new character holds only its race's powers and its class's kit) the vanilla starting spells and the spells
		// other mods slip into every character (their config menus and toggles) are taken off; run on the class pick and
		// every few seconds after, since some of those mods hand theirs back on each load
		void Purge()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			const int cls = g_class.load();
			auto unwanted = [cls](RE::SpellItem* a_sp) {
				if (!a_sp) return false;
				const auto type = a_sp->GetSpellType();
				if (type != RE::MagicSystem::SpellType::kSpell && type != RE::MagicSystem::SpellType::kLesserPower && type != RE::MagicSystem::SpellType::kPower) return false;
				std::string n = a_sp->GetName();
				for (auto& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				if (auto* f = a_sp->GetFile(0); f && std::string_view(f->GetFilename()) == "Skyrim.esm" && (n == "flames" || n == "healing" || n == "sparks")) return true;
				for (std::string_view bad : { "diverse dragons spell menu", "azurite meditation" }) {
					if (n.find(bad) != std::string::npos) return true;
				}
				// (the Wizard Warrior's own spells are the Spellsword's kit: for the other classes they go)
				return (n.find("import mode") != std::string::npos || n.find("magical shout") != std::string::npos) && cls != coffer::kSpellsword;
			};
			std::vector<RE::SpellItem*> drop;
			for (auto* sp : p->GetActorRuntimeData().addedSpells) {
				if (unwanted(sp)) drop.push_back(sp);
			}
			for (auto* sp : drop) {
				p->RemoveSpell(sp);
				logger::info("classes: {} taken off (not of your race or class)", sp->GetName());
			}
			// (Noah: "Obtain the Spellsword Manual - what is that for?") the Spellsword mod's own start quest: its manual
			// is never needed here (the Spellsword's kit comes from the class)
			if (auto* q = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x009A24, "Spellsword.esp"); q && q->IsRunning()) {
				Console(p, "StopQuest _Spellsword_Start_Quest");
				logger::info("classes: the Spellsword mod's manual quest stopped");
			}
			if (auto* base = p->GetActorBase(); base && base->actorEffects) {
				std::vector<RE::SpellItem*> baseDrop;
				for (std::uint32_t i = 0; i < base->actorEffects->numSpells; ++i) {
					if (unwanted(base->actorEffects->spells[i])) baseDrop.push_back(base->actorEffects->spells[i]);
				}
				for (auto* sp : baseDrop) {
					base->actorEffects->RemoveSpell(sp);
					p->RemoveSpell(sp);
					logger::info("classes: {} taken off the character's base spells", sp->GetName());
				}
			}
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

	// ---- (Noah: each class its own wonder weapon, mid and end game) never rolled: once three cities are freed the
	// class's wonder trial opens - three steps, each in a different city, of the class's own kind of kill - and its last
	// step hands over the wonder. After Sovngarde (MQ305 done) each one takes its full, Legendary form.
	//   Summoner     Horn of the Spectral Wyrm  Calling of the Conjurer's Spectral Dragon, 60 s (Legendary: it stays)
	//   Destruction  Tome of the Six Fires      one cast looses all six element ladders' top spells round you (Legendary: their runes and storms too)
	//   Ranger       Auriel's Bow               Sunhallowed arrows that home and burst in sunfire (Legendary: every third hit calls Daybreak)
	//   Spellsword   Dragon Slayer              each hit carries the spell in your other hand (Legendary: a power attack sends it down the line)
	namespace
	{
		constexpr int kWonderSteps = 3, kStepKills = 40;
		constexpr const char* kWonderName[4] = { "Horn of the Spectral Wyrm", "Tome of the Six Fires", "Auriel's Bow", "Dragon Slayer" };
		constexpr const char* kWonderKill[4] = { "kills by your summons", "kills with Destruction spells", "kills with a bow or crossbow", "melee kills" };
		std::array<std::uint8_t, 4>  g_wStep{};    // per class: 0 not open, 1..3 the step under way, 4 earned
		std::array<std::uint16_t, 4> g_wKills{};
		std::array<std::uint32_t, 4> g_wTowns{};   // cities where a step was done (bits)
		std::unordered_map<RE::FormID, RE::FormID> g_lastHit;  // a foe -> what you last hit it with
		std::unordered_set<RE::FormID> g_powerHit;  // foes your last blow on was a power attack (the Spellsword's resonance)
		std::uint16_t g_boltKills = 0;  // (the Ranger's crossbow mastery) kills with a crossbow: fire bolts at 25, ice at 60, shock at 120
		constexpr std::uint32_t kBoltFamilies[] = { 0x020B04, 0x025A50, 0x025A58, 0x025A60, 0x025A68, 0x025A70 };  // Faction Crossbows' plain bolts (fire +1, ice +2, shock +3)
		constexpr int           kBoltNeed[3] = { 25, 60, 120 };
		constexpr const char*   kBoltElem[3] = { "fire", "ice", "shock" };
		int BoltsUnlocked() { int n = 0; for (int t : kBoltNeed) n += g_boltKills >= t ? 1 : 0; return n; }
		int TopUpBolts(int a_to)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!p || !dh) return 0;
			int n = 0;
			const int un = BoltsUnlocked();
			for (auto base : kBoltFamilies) {
				auto* plain = dh->LookupForm<RE::TESAmmo>(base, "Faction Crossbows.esp");
				if (!plain || p->GetItemCount(plain) <= 0) continue;  // (only the families you shoot)
				for (int e = 0; e < un; ++e) {
					if (auto* b = dh->LookupForm<RE::TESAmmo>(base + 1 + e, "Faction Crossbows.esp"); b && p->GetItemCount(b) < a_to) {
						p->AddObjectToContainer(b, nullptr, a_to - p->GetItemCount(b), nullptr);
						++n;
					}
				}
			}
			return n;
		}
		float g_tomeCool = 0, g_bladeCool = 0;
		int   g_aurielHits = 0;

		int FreedCount()
		{
			int n = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		bool AfterSovngarde() { return Story::AlduinFallen(); }
		RE::SpellItem* WyrmSpell() { auto* f = Lookup(0x019D74, "Calling of the Conjurer.esp"); return f ? f->As<RE::SpellItem>() : nullptr; }
		RE::SpellItem* TomeSpell() { auto* dh = RE::TESDataHandler::GetSingleton(); return dh ? dh->LookupForm<RE::SpellItem>(gen::kWonderTomeSpell, gen::kPlugin) : nullptr; }
		RE::TESObjectWEAP* Auriel() { auto* f = Lookup(0x000800, "Dawnguard.esm"); return f ? f->As<RE::TESObjectWEAP>() : nullptr; }
		RE::TESAmmo* Sunhallowed() { auto* f = Lookup(0x0098A1, "Dawnguard.esm"); return f ? f->As<RE::TESAmmo>() : nullptr; }
		RE::TESObjectWEAP* Slayer() { auto* f = Lookup(0x00083D, "Ultra Greatswords.esp"); return f ? f->As<RE::TESObjectWEAP>() : nullptr; }
		RE::SpellItem* Myst(std::uint32_t a_id) { auto* f = Lookup(a_id, "MysticismMagic.esp"); return f ? f->As<RE::SpellItem>() : nullptr; }

		// the horn's dragon: a minute mid-game, after Sovngarde until it falls
		void TuneWyrm()
		{
			if (auto* sp = WyrmSpell()) {
				sp->fullName = kWonderName[0];
				for (auto* e : sp->effects) {
					if (e) e->effectItem.duration = AfterSovngarde() ? 3600 : 60;
				}
			}
		}
		void CastFrom(RE::Actor* a_from, RE::SpellItem* a_sp, RE::Actor* a_at)
		{
			if (!a_from || !a_sp) return;
			if (auto* c = a_from->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) c->CastSpellImmediate(a_sp, false, a_at, 1.0f, false, 0.0f, a_from);
		}
		std::vector<RE::Actor*> NearFoes(const RE::NiPoint3& a_at, float a_r, std::size_t a_max)
		{
			std::vector<std::pair<float, RE::Actor*>> v;
			for (auto h : Director::AliveHandles()) {
				auto a = h.get();
				if (!a || a->IsDead()) continue;
				if (const float d = a->GetPosition().GetDistance(a_at); d < a_r) v.push_back({ d, a.get() });
			}
			std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first < y.first; });
			std::vector<RE::Actor*> out;
			for (auto& [d, a] : v) { if (out.size() >= a_max) break; out.push_back(a); }
			return out;
		}

		void GrantWonder(int a_cls)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			switch (a_cls) {
			case coffer::kSummoner:
				TuneWyrm();
				if (auto* sp = WyrmSpell()) p->AddSpell(sp);
				break;
			case coffer::kDestruction:
				if (auto* sp = TomeSpell()) p->AddSpell(sp);
				break;
			case coffer::kRanger:
				GiveItem(p, Auriel(), 1, true);
				GiveItem(p, Sunhallowed(), 60, true);
				break;
			case coffer::kSpellsword:
				GiveItem(p, Slayer(), 1, true);
				break;
			}
			g_wStep[a_cls] = kWonderSteps + 1;
			Forms::Play(gen::Sound::shout_learned);
			static constexpr const char* kText[4] = {
				"A horn of bone that answers with a dragon: Calling of the Conjurer's Spectral Dragon fights at your side for a minute. After Sovngarde it stays until it falls.",
				"One cast looses the top spell of all six of your element ladders on the dead round you. After Sovngarde each cast also leaves their runes and storms on the ground.",
				"Auriel's own bow. Its Sunhallowed arrows seek the dead and burst in sunfire. After Sovngarde every third hit calls down Daybreak. More arrows: Q at an Echo Altar, or a Max Ammo.",
				"The Akaviri dragon slayer. Every hit carries the spell in your other hand. After Sovngarde a power attack sends that spell down the line." };
			std::string up(kWonderName[a_cls]);
			for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
			Hud::Lore(up, kText[a_cls], 12.0f);
			logger::info("classes: wonder {} earned", kWonderName[a_cls]);
		}

		// a kill of the class's own kind counts toward the wonder trial's step - in a city not used for a step yet
		void WonderKill(int a_cls)
		{
			if (a_cls < 0 || a_cls > 3) return;
			auto& st = g_wStep[a_cls];
			if (st == 0 || st > kWonderSteps) return;
			const int town = Curse::ZoneTown();
			if (town < 0) return;  // (a step is fought in a city)
			if ((g_wTowns[a_cls] >> town) & 1) {
				static auto told = std::chrono::steady_clock::now() - std::chrono::seconds(120);
				if (std::chrono::steady_clock::now() - told > std::chrono::seconds(60)) {
					told = std::chrono::steady_clock::now();
					Hud::Banner("This city already gave a step of your wonder trial - the next step is in another city", 4.0f);
				}
				return;
			}
			const int n = ++g_wKills[a_cls];
			if (n < kStepKills) {
				if (n % 10 == 0) Hud::Banner(fmt::format("{} - step {} of {}: {} of {} {}", kWonderName[a_cls], st, kWonderSteps, n, kStepKills, kWonderKill[a_cls]), 3.0f);
				return;
			}
			g_wKills[a_cls] = 0;
			g_wTowns[a_cls] |= 1u << town;
			if (st == kWonderSteps) {
				GrantWonder(a_cls);
				return;
			}
			++st;
			Forms::Play(gen::Sound::quest_step);
			Hud::Banner(fmt::format("{} - step {} done in {}. Step {} waits in another city", kWonderName[a_cls], st - 1, gen::kTowns[town], st), 5.0f);
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
		Purge();
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
	void OnKill(bool a_bySummon, RE::Actor* a_dying)
	{
		int killHow = -1;
		// the wonder trial: what made the kill
		if (const int c = g_class.load(); c >= 0) {
			int& how = killHow;  // 0 summon, 1 destruction spell, 2 bow or crossbow, 3 melee
			auto* p = RE::PlayerCharacter::GetSingleton();
			RE::TESForm* src = nullptr;
			if (a_dying) {
				if (auto it = g_lastHit.find(a_dying->GetFormID()); it != g_lastHit.end()) src = RE::TESForm::LookupByID(it->second);
			}
			if (a_bySummon) how = 0;
			else if (auto* mi = src ? src->As<RE::MagicItem>() : nullptr) how = (mi->GetAssociatedSkill() == RE::ActorValue::kDestruction || (Arcana::IndexOf(src) >= 0 && Arcana::ClassOf(Arcana::IndexOf(src)) == coffer::kDestruction)) ? 1 : -1;
			else if (auto* w = src ? src->As<RE::TESObjectWEAP>() : nullptr) {
				how = (w->IsBow() || w->IsCrossbow()) ? 2 : 3;
				if (w->IsCrossbow() && c == coffer::kRanger && g_boltKills < 60000) {
					const int before = BoltsUnlocked();
					++g_boltKills;
					if (const int now = BoltsUnlocked(); now > before) {
						Forms::Play(gen::Sound::quest_step);
						Hud::Banner(fmt::format("CROSSBOW MASTERY - {} bolts unlocked: refilled with your quivers (Q at an altar)", kBoltElem[now - 1]), 5.0f);
						TopUpBolts(20);
					}
				}
			}
			else if (Bows::Held() >= 0) how = 2;  // (the altar's bows strike with our own shots)
			else if (p) {
				for (int h : Arcana::Hands()) if (h >= 0 && Arcana::ClassOf(h) == coffer::kDestruction) how = 1;
			}
			if (how == c) WonderKill(c);  // (the classes' kinds of kill line up with the class order)
			int element = -1;
			if (const int ci = src ? Arcana::IndexOf(src) : -1; ci >= 0) element = coffer::kSpells[ci].element;
			const float dist = a_dying && p ? a_dying->GetPosition().GetDistance(p->GetPosition()) : 0.0f;
			if (how >= 0) Curse::OnClassKill(how, element, dist);
		}
		if (FreedCount() >= 3) {
			for (int c2 = 0; c2 < 4; ++c2) {
				if (g_wStep[c2] == 0 && c2 == g_class.load()) {
					g_wStep[c2] = 1;
					Forms::Play(gen::Sound::quest_start);
					Hud::Lore(std::string("THE TRIAL OF THE ") + (c2 == 0 ? "WYRM" : c2 == 1 ? "SIX FIRES" : c2 == 2 ? "SUN" : "SLAYER"),
						fmt::format("Three cities are free, and your class's wonder stirs: {}. Win it in three steps, each in a different city: {} {} per step.", kWonderName[c2], kStepKills, kWonderKill[c2]), 12.0f);
				}
			}
		}
		// (Noah's plan, the Spellsword's resonance) its touch spells climb on melee kills only - a power attack counts twice
		const bool power = a_dying && g_powerHit.contains(a_dying->GetFormID());
		for (int h : Arcana::Hands()) {
			if (h < 0) continue;
			const int cls = Arcana::ClassOf(h);
			if (a_bySummon && cls != coffer::kSummoner) continue;
			if (cls == coffer::kSpellsword && killHow != 3) continue;
			auto& m = g_mastery[coffer::kSpells[h].ladder];
			const int add = cls == coffer::kSpellsword && power ? 2 : 1;
			m = static_cast<std::uint16_t>(std::min(60000, m + add));
		}
		if (a_dying) g_powerHit.erase(a_dying->GetFormID());
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
	namespace { std::mutex g_lineLock; std::string g_line; }
	std::string WonderLineNow();
	void Tick()
	{
		static auto last = std::chrono::steady_clock::now();
		if (std::chrono::steady_clock::now() - last < std::chrono::seconds(1)) return;
		last = std::chrono::steady_clock::now();
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!p || !dh) return;
		if (static int purge = 0; ++purge % 5 == 0 && Save::Flag(Save::kFlagStarted)) Purge();
		{
			auto line = WonderLineNow();
			std::scoped_lock l(g_lineLock);
			g_line = std::move(line);
		}
		// (Noah's plan, the Ranger's brew) Fletcher's Sap Draught: the arrows or bolts in use grow back, one every 20 s, faster each draught
		if (const int fl = Save::PerkStacks(gen::kPerk_fletcher); fl > 0) {
			static int tick = 0;
			if (++tick >= std::max(4, 20 / fl)) {
				tick = 0;
				if (auto* ammo = p->GetCurrentAmmo(); ammo && p->GetItemCount(ammo) < 60) p->AddObjectToContainer(ammo, nullptr, 1, nullptr);
			}
		}
		// (Noah's plan, the Spellsword) Imbue unlocks The Wizard Warrior's Enchantment Powers 1 to 4, one per rung climbed
		if (g_class.load() == coffer::kSpellsword) {
			int imbued = 0;
			for (int i : Arcana::Carried()) {
				if (Arcana::ClassOf(i) == coffer::kSpellsword) imbued += Arcana::RungOf(i) - 1;
			}
			// ... and its three Magical Shouts are the class's shout slot: the first from the start, the others with Powers 2 and 4
			static constexpr std::uint32_t kShouts[3] = { 0x0008B6, 0x0008B7, 0x0008B8 };
			for (int n = 0; n < 3; ++n) {
				if (n > 0 && imbued < n * 2) break;
				if (auto* sh = dh->LookupForm<RE::SpellItem>(kShouts[n], "The Wizard Warrior.esp"); sh && !p->HasSpell(sh)) {
					p->AddSpell(sh);
					if (n > 0) Hud::Banner(fmt::format("The Wizard Warrior: Magical Shout {} learned", n + 1), 4.0f);
				}
			}
			static constexpr std::uint32_t kPowers[4] = { 0x0008A0, 0x000893, 0x0008A1, 0x002F6A };
			for (int n = 0; n < std::min(imbued, 4); ++n) {
				auto* sp = dh->LookupForm<RE::SpellItem>(kPowers[n], "The Wizard Warrior.esp");
				if (sp && !p->HasSpell(sp)) {
					p->AddSpell(sp);
					Forms::Play(gen::Sound::shout_learned);
					Hud::Banner(fmt::format("The Wizard Warrior: Enchantment Power {} unlocked (Powers menu)", n + 1), 4.0f);
					logger::info("classes: Wizard Warrior enchantment power {}", n + 1);
				}
			}
		}
		// (Noah's plan, the Summoner's mounts) an Indrik mount for each freed city
		if (g_class.load() == coffer::kSummoner) {
			static constexpr std::uint32_t kIndrik[] = { 0x801, 0x847, 0x848, 0x849, 0x84A, 0x84B, 0x84C, 0x84D, 0x84E, 0x84F };
			const int freed = FreedCount();
			for (int n = 0; n < std::min<int>(freed, static_cast<int>(std::size(kIndrik))); ++n) {
				auto* sp = dh->LookupForm<RE::SpellItem>(kIndrik[n], "mihailindrikmounts.esp");
				if (sp && !p->HasSpell(sp)) {
					p->AddSpell(sp);
					Hud::Banner(std::string("A city's gift: ") + sp->GetName(), 4.0f);
				}
			}
		}
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

	// (the Spellsword's and the Ranger's altar gear) a weapon of the class's mods, its material by how many cities are
	// freed: Steel and Orcish at first, Dwarven and Elven with one, Glass, Ebony and the Dwemer's best with two, Daedric,
	// Dragonbone and the named blades with three
	int MaterialTier(std::string_view a_name)
	{
		static constexpr std::pair<std::string_view, int> kTiers[] = { { "Daedric", 3 }, { "Dragon", 3 }, { "Daedra", 3 }, { "Stahlrim", 3 }, { "Cataclysm", 3 }, { "Moonlight", 3 },
			{ "Giant Crusher", 3 }, { "Spirit Sword", 3 }, { "Glass", 2 }, { "Ebony", 2 }, { "Advanced", 2 }, { "Nordic", 2 }, { "Slayer", 2 }, { "Ballista", 2 },
			{ "Dwarven", 1 }, { "Dwemer", 1 }, { "Elven", 1 }, { "Silver", 1 }, { "Imperial", 1 }, { "Falmer", 1 }, { "Repeater", 1 }, { "Tribow", 1 }, { "Twin", 1 } };
		for (auto [k, t] : kTiers) if (a_name.find(k) != std::string_view::npos) return t;
		return 0;
	}
	bool RollGear(int a_cls, Prize* a_out)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!p || !dh) return false;
		int freed = 0;
		for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) freed += Curse::Freed(t) ? 1 : 0;
		std::vector<RE::TESBoundObject*> pool;
		const auto from = [&](std::string_view a_plugin, auto* a_type) {
			using T = std::remove_pointer_t<decltype(a_type)>;
			for (auto* f : dh->GetFormArray<T>()) {
				if (!f || !f->GetFile(0) || f->GetFile(0)->GetFilename() != a_plugin) continue;
				const char* nm = f->GetName();
				if (!nm || !*nm || MaterialTier(nm) > freed) continue;
				if (p->GetItemCount(f) > 0) continue;
				if constexpr (std::is_same_v<T, RE::TESObjectARMO>) {
					if (!f->IsShield()) continue;
				}
				pool.push_back(f);
			}
		};
		if (a_cls == coffer::kSpellsword) {
			from("Spellsword.esp", static_cast<RE::TESObjectWEAP*>(nullptr));
			from("Spellsword.esp", static_cast<RE::TESObjectARMO*>(nullptr));
			from("Light Greatswords.esp", static_cast<RE::TESObjectWEAP*>(nullptr));
			from("Ultra Greatswords.esp", static_cast<RE::TESObjectWEAP*>(nullptr));
		} else if (a_cls == coffer::kRanger) {
			from("Faction Crossbows.esp", static_cast<RE::TESObjectWEAP*>(nullptr));
			from("OffhandBows.esp", static_cast<RE::TESObjectWEAP*>(nullptr));
		}
		if (a_cls == coffer::kSpellsword) {  // (Noah's plan) its shells and cloaks come from this row too, a quarter of the time
			static constexpr std::uint32_t kShells[] = { 0x29265E, 0x29266B, 0x292664, 0x292672, 0x29265D, 0x29266C, 0x292665, 0x292673, 0x209A73 };
			std::vector<RE::SpellItem*> spells;
			for (auto id : kShells) if (auto* sp = Myst(id); sp && !p->HasSpell(sp)) spells.push_back(sp);
			static std::mt19937 rng2{ std::random_device{}() };
			if (!spells.empty() && (pool.empty() || std::uniform_int_distribution<int>(0, 3)(rng2) == 0)) {
				auto* sp = spells[std::uniform_int_distribution<std::size_t>(0, spells.size() - 1)(rng2)];
				if (a_out) {
					a_out->name = sp->GetName();
					a_out->give = [sp] { if (auto* pl = RE::PlayerCharacter::GetSingleton()) pl->AddSpell(sp); Hud::Banner(std::string("The altar teaches you ") + sp->GetName(), 4.0f); };
					return true;
				}
				p->AddSpell(sp);
				Hud::Banner(std::string("The altar teaches you ") + sp->GetName(), 4.0f);
				return true;
			}
		}
		if (pool.empty()) {
			Hud::Banner("The altar has nothing new for you until another city is freed", 3.0f);
			return false;
		}
		static std::mt19937 rng{ std::random_device{}() };
		auto* pick = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(rng)];
		if (a_out) {
			a_out->name = pick->GetName();
			a_out->give = [pick, a_cls] {
				auto* pl = RE::PlayerCharacter::GetSingleton();
				if (!pl) return;
				pl->AddObjectToContainer(pick, nullptr, 1, nullptr);
				if (auto* w = pick->As<RE::TESObjectWEAP>(); w && w->IsCrossbow()) Bows::Restock();
				Hud::Banner(std::string("The altar gives you ") + pick->GetName(), 4.0f);
				logger::info("classes: altar gear {} ({})", pick->GetName(), Name(a_cls));
			};
			return true;
		}
		p->AddObjectToContainer(pick, nullptr, 1, nullptr);
		if (auto* w = pick->As<RE::TESObjectWEAP>(); w && w->IsCrossbow()) Bows::Restock();  // (a crossbow comes with its bolts)
		Hud::Banner(std::string("The altar gives you ") + pick->GetName(), 4.0f);
		logger::info("classes: altar gear {} ({})", pick->GetName(), Name(a_cls));
		return true;
	}


	// ---- (Noah: the class framework) the outfit picker: any outfit for any class, as a cosmetic. Each altar visit can
	// step through them (O / left stick); a piece you don't have yet is handed to you, and the outfit is put on.
	namespace
	{
		struct Outfit { const char* name; const char* plugin; std::array<std::uint32_t, 5> ids; };
		constexpr Outfit kOutfits[] = {
			{ "Wandering Summoner", "Wandering Summoner Outfit.esp", { 0xE58, 0xE5B, 0xE5D, 0xE5F, 0xE63 } },
			{ "Necromancer's Robes", "Skyrim.esm", { 0x10900E, 0x0C36E8, 0, 0, 0 } },
			{ "Wild Witch", "ksws03.esp", { 0x808, 0x809, 0x80B, 0x811, 0x80D } },
			{ "Drifter", "DrifterArmorAndOutfitByXtudo.esp", { 0x857, 0x858, 0x859, 0, 0 } },
			{ "Practical Pirate", "practical_pirate.esp", { 0x800, 0x802, 0x805, 0, 0 } },
			{ "Wizard Hat", "sirwho_Wizard_Hats-Full.esp", { 0, 0, 0, 0, 0 } },  // (each press: the next of its hats)
		};
		std::atomic<int> g_outfit{ -1 };
		int              g_hat = -1;
		std::mutex       g_outfitLock;
		std::string      g_outfitName;
	}
	void NextOutfit()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!p || !dh) return;
		constexpr int n = static_cast<int>(std::size(kOutfits));
		for (int tries = 0; tries < n; ++tries) {
			int next = (g_outfit.load() + 1) % n;
			const auto& o = kOutfits[next];
			std::vector<RE::TESBoundObject*> pieces;
			if (o.ids[0] == 0) {  // the hats: the next one in the plugin
				std::vector<RE::TESObjectARMO*> hats;
				for (auto* a : dh->GetFormArray<RE::TESObjectARMO>()) {
					if (a && a->GetFile(0) && a->GetFile(0)->GetFilename() == o.plugin && a->GetName() && *a->GetName()) hats.push_back(a);
				}
				if (!hats.empty()) {
					g_hat = (g_hat + 1) % static_cast<int>(hats.size());
					pieces.push_back(hats[g_hat]);
					if (g_hat + 1 < static_cast<int>(hats.size())) next = next - 1 + n, next %= n;  // (stay on the hats until the last one)
				}
			} else {
				for (auto id : o.ids) {
					if (!id) continue;
					auto* f = std::string_view(o.plugin) == "Skyrim.esm" ? RE::TESForm::LookupByID(id) : dh->LookupForm(id, o.plugin);
					if (auto* b = f ? f->As<RE::TESBoundObject>() : nullptr) pieces.push_back(b);
				}
			}
			g_outfit = next;
			if (pieces.empty()) continue;  // (that outfit's mod isn't installed)
			for (auto* b : pieces) GiveItem(p, b, p->GetItemCount(b) > 0 ? 0 : 1, true);
			std::string nm = o.ids[0] == 0 ? std::string(pieces[0]->GetName()) : std::string(o.name);
			{
				std::scoped_lock l(g_outfitLock);
				g_outfitName = nm;
			}
			Hud::Banner("Outfit: " + nm + "   (O / left stick: the next one)", 2.5f);
			return;
		}
		Hud::Banner("No outfits found", 2.0f);
	}
	std::string OutfitName()
	{
		std::scoped_lock l(g_outfitLock);
		return g_outfitName;
	}

	// (Noah's plan, Destruction) its support spells come as the altar's side-pick, for this class only
	bool RollSupport(Prize* a_out)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return false;
		static constexpr std::uint32_t kSupport[] = { 0x1901E5, 0x4B065B, 0x25FBFF, 0xFF19E0 };  // Ash Cloud, Cinder Storm, Charged Nova, Symmetry
		std::vector<RE::SpellItem*> pool;
		for (auto id : kSupport) {
			if (auto* sp = Myst(id); sp && !p->HasSpell(sp)) pool.push_back(sp);
		}
		if (pool.empty()) {
			Hud::Banner("You know every support spell the altar holds", 2.5f);
			return false;
		}
		static std::mt19937 rng{ std::random_device{}() };
		auto* sp = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(rng)];
		if (a_out) {
			a_out->name = sp->GetName();
			a_out->give = [sp] { if (auto* pl = RE::PlayerCharacter::GetSingleton()) pl->AddSpell(sp); Hud::Banner(std::string("Support spell learned: ") + sp->GetName(), 4.0f); logger::info("classes: support spell {}", sp->GetName()); };
			return true;
		}
		p->AddSpell(sp);
		Hud::Banner(std::string("Support spell learned: ") + sp->GetName(), 4.0f);
		logger::info("classes: support spell {}", sp->GetName());
		return true;
	}

	std::string BoltLine()
	{
		if (g_class.load() != coffer::kRanger) return {};
		const int un = BoltsUnlocked();
		if (un >= 3) return "Crossbow mastery: fire, ice and shock bolts";
		return fmt::format("Crossbow mastery: {} of {} crossbow kills to {} bolts", g_boltKills, kBoltNeed[un], kBoltElem[un]);
	}

	// (Noah's plan, the Spellsword) Imbue moves the held spell-weapon up a material - Steel, Orcish, Dwarven, Elven, Glass,
	// Ebony, Daedric - keeping its kind and, where that material has it, its element
	void ImbueWeapon()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* held = p ? p->GetEquippedObject(false) : nullptr;
		auto* w = held ? held->As<RE::TESObjectWEAP>() : nullptr;
		if (!w || !w->GetFile(0) || w->GetFile(0)->GetFilename() != "Spellsword.esp" || !dh) return;
		static constexpr std::string_view kMat[] = { "Steel", "Orcish", "Dwarven", "Elven", "Glass", "Ebony", "Daedric" };
		const std::string name = w->GetName();
		int m = -1;
		for (int i = 0; i < 7; ++i) if (name.starts_with(kMat[i])) m = i;
		if (name.starts_with("Dwemer")) m = 2;
		if (m < 0 || m >= 6) return;
		const std::string rest = name.substr(name.find(' '));                 // " Spellsword of Fire"
		const std::string kind = rest.substr(0, rest.find(" of "));           // " Spellsword"
		const std::string elem = rest.find(" of ") != std::string::npos ? rest.substr(rest.find(" of ")) : std::string();
		RE::TESObjectWEAP* best = nullptr;
		int bestScore = -1;
		for (int nm = m + 1; nm < 7 && !best; ++nm) {
			for (auto* c : dh->GetFormArray<RE::TESObjectWEAP>()) {
				if (!c || !c->GetFile(0) || c->GetFile(0)->GetFilename() != "Spellsword.esp" || !c->GetName()) continue;
				const std::string cn = c->GetName();
				if (!cn.starts_with(kMat[nm])) continue;
				const int score = (cn.find(kind) != std::string::npos ? 2 : 0) + (!elem.empty() && cn.ends_with(elem) ? 1 : 0) + (c->GetWeaponType() == w->GetWeaponType() ? 2 : 0);
				if (score > bestScore) bestScore = score, best = c;
			}
		}
		if (!best || bestScore < 2) return;
		p->RemoveItem(w, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		GiveItem(p, best, 1, true);
		Hud::Banner(std::string("Imbued: ") + name + "  ->  " + best->GetName(), 4.0f);
		logger::info("classes: imbued {} -> {}", name, best->GetName());
	}

	// (Noah's plan, the Spellsword's defence) a spellshield blocks and turns spells back; a blocked blow staggers the dead
	void OnBlock(RE::Actor* a_attacker, bool a_spell)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !a_attacker || a_attacker->IsDead() || g_class.load() != coffer::kSpellsword) return;
		auto* left = p->GetEquippedObject(true);
		auto* sh = left ? left->As<RE::TESObjectARMO>() : nullptr;
		if (!sh || !sh->GetFile(0) || sh->GetFile(0)->GetFilename() != "Spellsword.esp") return;
		static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(5);
		if (std::chrono::steady_clock::now() - last < std::chrono::milliseconds(600)) return;
		last = std::chrono::steady_clock::now();
		if (a_spell) {  // the spell turned back: a share of it returns to its caster
			auto* av = a_attacker->AsActorValueOwner();
			av->DamageActorValue(RE::ActorValue::kHealth, 0.35f * av->GetPermanentActorValue(RE::ActorValue::kHealth));
			Gun::FxBlast("light", a_attacker->GetPosition() + RE::NiPoint3{ 0, 0, 80.0f });
		} else if (Director::IsRoundUndead(a_attacker)) {
			a_attacker->NotifyAnimationGraph("staggerStart");
		}
	}

	// (Noah's plan, the Summoner: Calling of the Conjurer's heroes and armies as late Legendaries) once one of your families
	// is Legendary and two cities are free, the altar's second row binds a hero - then, with four cities free, an army
	bool RollHero(Prize* a_out)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return false;
		bool legend = false;
		for (int i : Arcana::Carried()) legend |= Arcana::ClassOf(i) == coffer::kSummoner && Arcana::RungOf(i) >= coffer::kSpells[i].rungs;
		const int freed = FreedCount();
		if (!legend || freed < 2) {
			Hud::Banner("Heroes answer only a Legendary summoner, with two cities free", 3.0f);
			return false;
		}
		static constexpr std::uint32_t kHeroes[] = { 0x019D7C, 0x0290A4, 0x0290A3, 0x0290A6, 0x0CB318, 0x103618, 0x019D66 };  // Ysgramor, Gormlaith, Felldir, Hakon, Miraak, Karstaag, the Ebony Warrior
		static constexpr std::uint32_t kArmies[] = { 0x09370D, 0x0A2A1D, 0x0CB311, 0x0C61F9 };  // the Dragon Priest, Dwarven, Undead and Hero armies
		std::vector<RE::SpellItem*> pool;
		auto add = [&](std::uint32_t id) { if (auto* f = Lookup(id, "Calling of the Conjurer.esp"); f) if (auto* sp = f->As<RE::SpellItem>(); sp && !p->HasSpell(sp)) pool.push_back(sp); };
		for (auto id : kHeroes) add(id);
		if (freed >= 4) for (auto id : kArmies) add(id);
		if (pool.empty()) {
			Hud::Banner(freed >= 4 ? "Every hero and army already answers you" : "Every hero answers you - the armies wait for four free cities", 3.0f);
			return false;
		}
		static std::mt19937 rng{ std::random_device{}() };
		auto* sp = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(rng)];
		if (a_out) {
			a_out->name = sp->GetName();
			a_out->give = [sp] { if (auto* pl = RE::PlayerCharacter::GetSingleton()) pl->AddSpell(sp); Forms::Play(gen::Sound::shout_learned); Hud::Banner(std::string("A LEGENDARY PACT - ") + sp->GetName(), 5.0f); logger::info("classes: hero pact {}", sp->GetName()); };
			return true;
		}
		p->AddSpell(sp);
		Forms::Play(gen::Sound::shout_learned);
		Hud::Banner(std::string("A LEGENDARY PACT - ") + sp->GetName(), 5.0f);
		logger::info("classes: hero pact {}", sp->GetName());
		return true;
	}

	bool WonderEarned()
	{
		for (auto s : g_wStep) if (s > kWonderSteps) return true;
		return false;
	}
	std::string WonderLine()
	{
		std::scoped_lock l(g_lineLock);
		return g_line;
	}
	std::string WonderLineNow()
	{
		const int c = g_class.load();
		if (c < 0) return {};
		const int st = g_wStep[c];
		if (st > kWonderSteps) return std::string(kWonderName[c]) + " is yours";
		if (st == 0) return fmt::format("{}: free {} more {} to open its trial", kWonderName[c], std::max(0, 3 - FreedCount()), 3 - FreedCount() == 1 ? "city" : "cities");
		return fmt::format("{} - step {} of {}: {} of {} {} in a new city", kWonderName[c], st, kWonderSteps, g_wKills[c], kStepKills, kWonderKill[c]);
	}

	void NoteHit(RE::Actor* a_target, RE::FormID a_source, bool a_power)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !a_target || a_target == p) return;
		if (g_lastHit.size() > 400) g_lastHit.clear(), g_powerHit.clear();
		g_lastHit[a_target->GetFormID()] = a_source;
		if (a_power) g_powerHit.insert(a_target->GetFormID()); else g_powerHit.erase(a_target->GetFormID());
		if (!Director::IsFoe(a_target) || a_target->IsDead()) return;
		auto* src = RE::TESForm::LookupByID(a_source);
		if (!src) return;
		if (src == Auriel()) {  // sunfire on every hit; after Sovngarde every third calls Daybreak
			CastFrom(p, Myst(0x2466C0), a_target);
			if (AfterSovngarde() && ++g_aurielHits % 3 == 0) CastFrom(p, Myst(0x7BC311), a_target);
		} else if (src == Slayer() && g_bladeCool <= 0) {  // the spell in your other hand rides the blade
			const int left = Arcana::Hands()[0];
			auto* sp = left >= 0 ? Arcana::Spell(left) : nullptr;
			if (!sp) return;
			g_bladeCool = 0.35f;
			CastFrom(p, sp, a_target);
			if (a_power && AfterSovngarde()) {
				for (auto* f : NearFoes(a_target->GetPosition(), 900.0f, 6)) if (f != a_target) CastFrom(p, sp, f);
				Gun::FxBlast("light", a_target->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f });
			}
		}
	}

	// (Noah: "I get the casting animation and the sound, but nothing is spawning") a summon spell cast: if no new
	// summon of yours stands within a moment and you are under your cap, the creature is called up here instead and
	// held for the spell's duration
	namespace
	{
		struct PendingSummon { RE::TESBoundObject* what = nullptr; float wait = 0, seconds = 60; int before = 0; std::string name; };
		std::vector<PendingSummon>                        g_pendingSummon;
		std::vector<std::pair<RE::ActorHandle, float>>    g_ownSummons;  // the ones called up here: seconds left
		int CommandedNow(RE::PlayerCharacter* a_p)
		{
			int n = 0;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				if (auto a = h.get(); a && a.get() != a_p && !a->IsDead() && !a->IsDisabled() && a->IsCommandedActor() && a->GetCommandingActor().get() == a_p) ++n;
			}
			return n;
		}
		int SummonCap(RE::PlayerCharacter* a_p)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			for (int n = 4; n >= 2; --n) {
				if (auto* perk = dh ? dh->LookupForm<RE::BGSPerk>(gen::kSummonCapPerks[n - 2], gen::kPlugin) : nullptr; perk && a_p->HasPerk(perk)) return n;
			}
			return 1;
		}
		void TickSummons(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			for (auto& [h, t] : g_ownSummons) {
				auto a = h.get();
				if (!a || a->IsDisabled()) { t = 0; continue; }
				if ((t -= a_delta) <= 0 || a->IsDead()) {
					Gun::FxBlast("dark", a->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f });
					a->Disable();
					a->SetDelete(true);
					t = 0;
				}
			}
			std::erase_if(g_ownSummons, [](const auto& e) { return e.second <= 0; });
			for (auto& ps : g_pendingSummon) {
				if ((ps.wait -= a_delta) > 0) continue;
				const int now = CommandedNow(p) + static_cast<int>(g_ownSummons.size());
				if (now > ps.before) {
					logger::info("classes: {} came as it should ({} summons up)", ps.name, now);
					continue;
				}
				const int cap = SummonCap(p);
				while (!g_ownSummons.empty() && static_cast<int>(g_ownSummons.size()) >= cap) {  // (the oldest goes, as Skyrim's own do)
					if (auto a = g_ownSummons.front().first.get()) a->Disable(), a->SetDelete(true);
					g_ownSummons.erase(g_ownSummons.begin());
				}
				const float yaw = p->GetAngleZ();
				const RE::NiPoint3 at = p->GetPosition() + RE::NiPoint3{ std::sin(yaw) * 220.0f, std::cos(yaw) * 220.0f, 0 };
				auto ref = p->PlaceObjectAtMe(ps.what, false);
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor) {
					logger::warn("classes: {} could not be called up", ps.name);
					continue;
				}
				actor->SetPosition(at, true);
				static auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1);
				if (playerFaction) actor->AddToFaction(playerFaction, 0);
				Gun::FxBlast("dark", at + RE::NiPoint3{ 0, 0, 40.0f });
				g_ownSummons.push_back({ actor->GetHandle(), ps.seconds });
				Console(actor, "SetPlayerTeammate 1");
				logger::info("classes: {} did not come - called up here for {:.0f} s", ps.name, ps.seconds);
			}
			std::erase_if(g_pendingSummon, [](const PendingSummon& ps) { return ps.wait <= 0; });
		}
	}
	bool IsOwnSummon(RE::Actor* a_actor)
	{
		return a_actor && std::any_of(g_ownSummons.begin(), g_ownSummons.end(), [&](const auto& e) { return e.first.get().get() == a_actor; });
	}
	void NoteCast(RE::FormID a_spell)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(a_spell);
		if (!p || !sp) return;
		for (auto* e : sp->effects) {
			auto* mg = e ? e->baseEffect : nullptr;
			if (!mg || mg->GetArchetype() != RE::EffectArchetypes::ArchetypeID::kSummonCreature) continue;
			auto* what = mg->data.associatedForm ? mg->data.associatedForm->As<RE::TESBoundObject>() : nullptr;
			if (!what) continue;
			const int before = CommandedNow(p) + static_cast<int>(g_ownSummons.size());
			if (before >= SummonCap(p)) {
				logger::info("classes: {} cast at the cap ({}) - Skyrim swaps the oldest", sp->GetName(), before);
				return;
			}
			g_pendingSummon.push_back({ what, 1.5f, std::max(30.0f, static_cast<float>(e->effectItem.duration)), before, sp->GetName() });
			return;
		}
	}

	bool OnCast(RE::FormID a_spell)
	{
		auto* tome = TomeSpell();
		if (!tome || a_spell != tome->GetFormID()) return false;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return true;
		if (g_tomeCool > 0) {
			Hud::Banner(fmt::format("The Six Fires gather again ({:.0f} s)", g_tomeCool), 2.0f);
			return true;
		}
		const auto foes = NearFoes(p->GetPosition(), 2600.0f, 6);
		if (foes.empty()) {
			Hud::Banner("The Six Fires find nothing to burn", 2.0f);
			return true;
		}
		g_tomeCool = 12.0f;
		int k = 0;
		const bool legend = AfterSovngarde();
		for (const auto& s : coffer::kSpells) {
			if (s.cls != coffer::kDestruction) continue;
			const int idx = static_cast<int>(&s - coffer::kSpells.data());
			RE::Actor* t = foes[k % foes.size()];
			if (s.rung == s.rungs) CastFrom(p, Arcana::Spell(idx), t), ++k;
			else if (legend && s.rung == 3) CastFrom(p, Arcana::Spell(idx), foes[(k + 1) % foes.size()]);
		}
		Gun::FxBlast("fireball", p->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f });
		logger::info("classes: the Six Fires on {} foes{}", foes.size(), legend ? " (Legendary)" : "");
		return true;
	}

	int RestockWonder()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* bow = Auriel();
		auto* ar = Sunhallowed();
		int n = TopUpBolts(20);  // (crossbow mastery's elemental bolts)
		if (!p || !bow || !ar || p->GetItemCount(bow) <= 0) return n;
		const int have = p->GetItemCount(ar);
		if (have >= 60) return n;
		p->AddObjectToContainer(ar, nullptr, 60 - have, nullptr);
		return n + 1;
	}

	// every frame: cooldowns, and Auriel's arrows seek the dead
	void TickFast(float a_delta)
	{
		TickSummons(a_delta);
		g_tomeCool = std::max(0.0f, g_tomeCool - a_delta);
		g_bladeCool = std::max(0.0f, g_bladeCool - a_delta);
		auto* bow = Auriel();
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* mgr = RE::Projectile::Manager::GetSingleton();
		if (!bow || !p || !mgr || p->GetItemCount(bow) <= 0) return;
		std::vector<RE::Projectile*> mine;
		{
			RE::BSSpinLockGuard lock(mgr->projectileLock);
			for (auto* list : { &mgr->limited, &mgr->unlimited }) {
				for (auto& h : *list) {
					auto pr = h.get();
					if (!pr || pr->IsDeleted()) continue;
					auto& rd = pr->GetProjectileRuntimeData();
					if (rd.weaponSource == bow && rd.shooter.get().get() == p) mine.push_back(pr.get());
				}
			}
		}
		for (auto* pr : mine) {
			auto&              rd = pr->GetProjectileRuntimeData();
			const RE::NiPoint3 at = pr->GetPosition();
			const auto         closest = NearFoes(at, 2200.0f, 1);
			if (closest.empty()) continue;
			RE::NiPoint3 want = closest[0]->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f } - at;
			if (want.Length() < 1.0f) continue;
			want.Unitize();
			const float  sp = std::max(rd.linearVelocity.Length(), 600.0f);
			RE::NiPoint3 v = rd.linearVelocity.Length() > 1.0f ? rd.linearVelocity / rd.linearVelocity.Length() : want;
			const float  k = std::min(1.0f, 7.0f * a_delta);
			v = v * (1.0f - k) + want * k;
			if (v.Length() > 0.001f) v.Unitize();
			rd.linearVelocity = v * sp;
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
		v.push_back(0xA1);  // the wonder trials: per class [step][kills u16][cities u32]
		for (int c = 0; c < 4; ++c) {
			v.push_back(g_wStep[c]);
			v.push_back(static_cast<std::uint8_t>(g_wKills[c] & 0xFF)), v.push_back(static_cast<std::uint8_t>(g_wKills[c] >> 8));
			for (int b = 0; b < 4; ++b) v.push_back(static_cast<std::uint8_t>(g_wTowns[c] >> (8 * b)));
		}
		v.push_back(0xA2);  // crossbow mastery: [kills u16]
		v.push_back(static_cast<std::uint8_t>(g_boltKills & 0xFF)), v.push_back(static_cast<std::uint8_t>(g_boltKills >> 8));
		return v;
	}
	void SetState(const std::vector<std::uint8_t>& a_v)
	{
		g_class = -1, g_kit.fill(false);
		g_wStep.fill(0), g_wKills.fill(0), g_wTowns.fill(0), g_lastHit.clear(), g_boltKills = 0;
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
		if (k < a_v.size() && a_v[k] == 0xA1 && k + 1 + 4 * 7 <= a_v.size()) {
			++k;
			for (int c = 0; c < 4; ++c, k += 7) {
				g_wStep[c] = a_v[k];
				g_wKills[c] = static_cast<std::uint16_t>(a_v[k + 1] | (a_v[k + 2] << 8));
				g_wTowns[c] = a_v[k + 3] | (a_v[k + 4] << 8) | (a_v[k + 5] << 16) | (static_cast<std::uint32_t>(a_v[k + 6]) << 24);
			}
		}
		if (k < a_v.size() && a_v[k] == 0xA2 && k + 3 <= a_v.size()) {
			g_boltKills = static_cast<std::uint16_t>(a_v[k + 1] | (a_v[k + 2] << 8));
			k += 3;
		}
		if (g_wStep[coffer::kSummoner] > kWonderSteps) TuneWyrm();
		logger::info("classes: loaded ({})", Name(g_class));
	}
	void Revert() { SetState({}); g_open = false; g_mastery.fill(0); }
}
