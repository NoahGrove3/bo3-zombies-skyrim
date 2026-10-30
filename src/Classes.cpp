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
		int                                                    g_staffLevel = 0;  // (Noah) the Conjurer's Staff's reforges at the altar, 0..3
		std::array<std::vector<std::pair<int, int>>, 4>        g_stash;  // the spells a class you left carried (index, level)
		// (Noah: "switching classes - my items should stay in the correct class, almost like an inventory swap, saveable") each
		// class's gear waits in its own hidden chest (a persistent, disabled container) while another class is played
		std::array<RE::FormID, 4>                              g_chest{};      // per class: its chest's reference (0 none yet)
		std::array<std::array<Save::Slot, 3>, 4>               g_slotStash{};  // per class: its three weapon slots
		std::array<std::vector<RE::FormID>, 4>                 g_wornStash;    // per class: what it had on / in hand
		std::array<std::vector<std::uint8_t>, 4>               g_perkStash;    // per class: its altar blessings' stacks
		std::unordered_map<RE::FormID, std::pair<std::uint8_t, std::uint16_t>> g_xbow;  // (the classes' growth) a crossbow's reforges and its kills toward the next
		std::array<std::vector<RE::FormID>, 4>                 g_extra;        // (the audit) per class: the altar's other spells it was taught (shells, support, pacts, wonders)
		std::uint8_t                                           g_guide = 0;    // (the guided first night) which of its cards have been shown (bits)
		std::chrono::steady_clock::time_point                  g_closedAt{};
		std::array<std::uint16_t, coffer::kLadders>            g_mastery{};  // kills made with each ladder's spell in hand (its summons' kills for the Summoner)

		RE::TESForm* Lookup(std::uint32_t a_id, std::string_view a_plugin)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm(a_id, a_plugin) : nullptr;
		}
		// a spell the altar teaches the class you play (not one of its ladders): learned, and kept with that class
		void Learn(RE::SpellItem* a_sp)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !a_sp) return;
			p->AddSpell(a_sp);
			if (const int c = g_class.load(); c >= 0 && c < 4 && std::find(g_extra[c].begin(), g_extra[c].end(), a_sp->GetFormID()) == g_extra[c].end()) g_extra[c].push_back(a_sp->GetFormID());
		}
		// (the consistency pass) a class prize's reel: up to a dozen of its pool's names, the won one among them
		template <class T>
		void FillReel(Classes::Prize* a_out, const std::vector<T*>& a_pool, T* a_pick, const char* a_kind, bool a_legend = false)
		{
			if (!a_out) return;
			a_out->kind = a_kind, a_out->legendary = a_legend, a_out->reel.clear();
			std::vector<T*> pool = a_pool;
			static std::mt19937 rr{ std::random_device{}() };
			std::shuffle(pool.begin(), pool.end(), rr);
			if (pool.size() > 12) pool.resize(12);
			if (std::find(pool.begin(), pool.end(), a_pick) == pool.end()) pool.back() = a_pick;
			for (auto* x : pool) {
				if (x == a_pick) a_out->at = static_cast<int>(a_out->reel.size());
				a_out->reel.emplace_back(x && x->GetName() ? x->GetName() : "");
			}
		}
		// a bow, a crossbow, or one of OffhandBows' one-hand crossbows (staves to Skyrim)
		bool IsRangerWeapon(const RE::TESObjectWEAP* a_w)
		{
			if (!a_w) return false;
			if (a_w->IsBow() || a_w->IsCrossbow()) return true;
			auto* f = a_w->GetFile(0);
			return f && std::string_view(f->GetFilename()) == "OffhandBows.esp";
		}
		int Rung1(int a_cls, std::string_view a_ladderLabel)
		{
			for (int i = 0; i < static_cast<int>(coffer::kSpells.size()); ++i) {
				const auto& s = coffer::kSpells[i];
				if (s.cls == a_cls && s.rung == 1 && a_ladderLabel == s.ladderLabel) return i;
			}
			return -1;
		}
		RE::TESObjectWEAP* ElemCrossbow(int a_e);  // (the Ranger's elemental crossbows: below)
	// (Noah: "the Dremora's voice lines kept repeating over and over just before the crash") every StartCombat is a new
	// combat bark: a summon is sent at a foe at most once in 2 s, and at the same foe again only after 8 s
	static bool SummonMayEngage(RE::Actor* a_s, RE::Actor* a_t)
	{
		struct Last { RE::FormID t = 0; std::chrono::steady_clock::time_point at{}; };
		static std::unordered_map<RE::FormID, Last> last;
		if (last.size() > 300) last.clear();
		const auto now = std::chrono::steady_clock::now();
		auto& l = last[a_s->GetFormID()];
		if (now - l.at < std::chrono::seconds(2)) return false;
		if (l.t == a_t->GetFormID() && now - l.at < std::chrono::seconds(8)) return false;
		l.t = a_t->GetFormID(), l.at = now;
		return true;
	}
		int ElemCrossbowIndex(const RE::TESForm* a_obj) { for (int e = 0; e < 3; ++e) if (a_obj && a_obj == ElemCrossbow(e)) return e; return -1; }
		RE::TESAmmo*       ElemBolts(int a_e);
		int                BoltCap(int a_level);
		void GiveItem(RE::PlayerCharacter* a_p, RE::TESForm* a_form, int a_count, bool a_equip)
		{
			auto* obj = a_form ? a_form->As<RE::TESBoundObject>() : nullptr;
			if (!obj) return;
			if (a_count > 0) a_p->AddObjectToContainer(obj, nullptr, a_count, nullptr);
			if (a_equip) {
				if (auto* em = RE::ActorEquipManager::GetSingleton()) em->EquipObject(a_p, obj, nullptr, 1, nullptr, false, false, false, true);
			}
		}
		// (Noah: "with a male character the class armour sets make my body invisible - they're for women") a piece fits
		// you when one of its addons has a model for your sex (the Drifter Outfit and the Wandering Summoner have none for men)
		bool Fits(const RE::TESObjectARMO* a_armo, bool a_female)
		{
			if (!a_armo) return true;
			bool any = false;
			for (auto* aa : a_armo->armorAddons) {
				if (!aa) continue;
				any = true;
				const char* m = aa->bipedModels[a_female ? RE::SEXES::kFemale : RE::SEXES::kMale].GetModel();
				if (m && *m) return true;
			}
			return !any;
		}
		bool PlayerFemale()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p && p->GetActorBase() && p->GetActorBase()->IsFemale();
		}
		// a man's Ranger wears the Dawnguard's own light armour in the Drifter's place
		constexpr std::uint32_t kDawnguardLight[] = { 0x00F3FB, 0x00F400, 0x00F3FE };  // cuirass, boots, gauntlets (Dawnguard.esm)
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

		int ConjurerStaff()
		{
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) if (gen::kGuns[i].id == "gravecaller") return i;
			return -1;
		}
		bool g_staffChecked = false;
		// (the Conjurer's Staff is gone: Corrosion took its work) Mysticism's Armoreater burst, drawn round a Corrosion bolt's hit
		// (Noah: "the Corrosion impact is way too big and I don't want it to shake my screen - it should be the size of the
		// area damage") its own bolts' bursts: Mysticism's (an area of 75 feet, a jolt on the camera) made quiet - no damage,
		// no shove, no screen effect - and each rung's area set to the burst our hit deals (3 m, a metre more each rung)
		// (Noah: "the one-handed weapons in the Echo Altar shouldn't have traditional enchantments - they contain one spell,
		// so it uses whatever spell is in your opposite hand") every one-handed Spellsword blade carries the mod's plain
		// "Contains 1 spell" enchantment (no fire, frost or fear of its own), its name without the " of ..." it no longer has
		void TuneSpellswordGear()
		{
			static bool done = false;
			if (done) return;
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* plain = dh ? dh->LookupForm<RE::EnchantmentItem>(0x003E16, "Spellsword.esp") : nullptr;
			if (!dh) return;
			done = true;
			if (!plain) {
				logger::info("classes: Spellsword.esp's plain spell-enchantment not found - its blades left as they are");
				return;
			}
			int n = 0;
			for (auto* w : dh->GetFormArray<RE::TESObjectWEAP>()) {
				if (!w || !w->GetFile(0) || w->GetFile(0)->GetFilename() != "Spellsword.esp") continue;
				if (!w->IsOneHandedSword() && !w->IsOneHandedDagger() && !w->IsOneHandedAxe() && !w->IsOneHandedMace()) continue;
				if (w->formEnchanting == plain) continue;
				w->formEnchanting = plain;
				std::string nm = w->GetName() ? w->GetName() : "";
				if (const auto at = nm.find(" of "); at != std::string::npos) nm.resize(at);
				for (std::string_view cut : { ", Advanced", " Empowered", " Modified" }) {
					if (const auto at = nm.find(cut); at != std::string::npos) nm.erase(at, cut.size());
				}
				for (std::string_view pre : { "Reverent ", "Dovah Sos " }) {
					if (nm.rfind(pre, 0) == 0) nm.erase(0, pre.size());
				}
				if (!nm.empty() && nm != w->GetName()) w->fullName = nm;
				++n;
			}
			logger::info("classes: {} one-handed Spellsword blades now hold one spell, no enchantment of their own", n);
		}
		void TuneCorrosion()
		{
			static bool done = false;
			if (done) return;
			done = true;
			for (int k = 0; k < static_cast<int>(coffer::kSpells.size()); ++k) {
				if (std::string_view(coffer::kSpells[k].ladderLabel) != "Reanimate" || coffer::kSpells[k].cls != coffer::kSummoner) continue;
				auto* sp = Arcana::Spell(k);
				if (!sp) continue;
				const float radius = 210.0f + 70.0f * static_cast<float>(coffer::kSpells[k].rung - 1);
				for (auto* e : sp->effects) {
					if (!e) continue;
					if (auto* m = e->baseEffect; m && m->data.explosion) {  // (Reanimate's bolts have no burst: left single-target)
						e->effectItem.area = static_cast<std::uint32_t>(radius / 21.33f + 0.5f);
						if (auto* q = Gun::Quiet(m->data.explosion)) {
							q->data.radius = std::min(q->data.radius, radius);
							m->data.explosion = q;
						}
					}
				}
				logger::info("classes: Corrosion rung {} ({}) - its burst {:.0f} units, quiet", coffer::kSpells[k].rung, sp->GetName(), radius);
			}
		}
		RE::BGSExplosion* StaffBurstLook()
		{
			static auto* m = RE::TESDataHandler::GetSingleton()->LookupForm<RE::EffectSetting>(0xAC7E88, "MysticismMagic.esp");
			return m ? m->data.explosion : nullptr;
		}
		// (Noah: "the belt lantern should be an equipable item like the book for battle mage classes") one Belt Lantern for
		// the character, worn: every class shares it (it never goes in a class's chest), so it is only handed over when you
		// have none and have never been given one
		bool g_lanternGiven = false;
		void GiveLantern(RE::PlayerCharacter* a_p)
		{
			auto* l = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectARMO>(gen::kLanternItem, gen::kPlugin);
			if (!a_p || !l || g_lanternGiven) return;
			g_lanternGiven = true;
			if (a_p->GetItemCount(l) > 0) return;
			GiveItem(a_p, l, 1, true);
			logger::info("classes: the Belt Lantern given, worn");
		}
		void GiveKit(int a_cls)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			const bool female = p->GetActorBase() && p->GetActorBase()->IsFemale();
			(void)female;
			switch (a_cls) {
			case coffer::kSummoner:
				// (Noah: "remove the staff the Summoner comes with - an upgradable spell of an element no other class uses")
				// Corrosion in the right hand (its bolts tell your summons what to kill), the Beasts in the left, the Dead in
				// the book
				// (Noah, the altar rework: "only the summons and the Riftblade") the Beasts in the left hand, the Dead in the
				// book, the Riftblade in the right
				Arcana::Give(Rung1(a_cls, "The Dead"), false);
				GiveSpell(Rung1(a_cls, "Beasts"), true);
				GiveItem(p, Riftblade(), 1, true);
				break;
			case coffer::kDestruction:
				GiveSpell(Rung1(a_cls, "Fire"), false);
				GiveSpell(Rung1(a_cls, "Frost"), true);
				if (auto* tome = Lookup(0x817, "Equipable Tomes - Redone.esp")) GiveItem(p, tome, 1, true);  // a Destruction tome at the hip
				break;
			case coffer::kRanger:
				// (Noah: "they should start with only the crossbow, the armour and the bolts - the bows stay at the altar") the
				// Ember Crossbow and its fire bolts, the bolts nocked
				// (Noah, the altar rework) the Emberstring bow beside the Ember Crossbow
				for (int b = 0; b < Bows::Count(); ++b) if (gen::kBows[b].id == std::string_view("emberstring")) Bows::Give(b);
				if (auto* xb = ElemCrossbow(0)) {
					GiveItem(p, xb, 1, true);
					GiveItem(p, ElemBolts(0), BoltCap(0), true);
				} else {  // (our plugin's crossbow missing: Faction Crossbows' own)
					GiveItem(p, Lookup(0x002F9A, "Faction Crossbows.esp"), 1, false);
					GiveItem(p, Lookup(0x020B04, "Faction Crossbows.esp"), 30, false);
				}
				break;
			case coffer::kSpellsword:
				// (Noah, the altar rework: "remove shields entirely - a one-hand sword from one of the weapon mods, and a fire
				// touch") the blade in the right hand, Burning Touch in the left
				GiveItem(p, SpellswordBlade(), 1, true);
				GiveSpell(Rung1(a_cls, "Fire"), true);
				break;
			}
			// (Noah: "each class's armour set part of its starting kit") the set, worn (Kit owns it as an upgradable item)
			GiveArmourSet(a_cls, 0, true);
			GiveLantern(p);
			g_kit[a_cls] = true;
			Kit::OnKitGiven(a_cls);
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
				// (Noah: "a dragon's MCM item - I used it and it broke the game: spells stopped casting, some audio stopped")
				// Diverse Dragons' "ACCM: Diverse Dragons Col. 3 Spell Menu", whatever its full name says
				if (n.find("spell menu") != std::string::npos || n.rfind("accm", 0) == 0) return true;
				// (Noah: "remove the magical shout") the Wizard Warrior's shout slots go with its shout
				if (n.find("the wizard warrior") != std::string::npos && n.find("shout") != std::string::npos) return true;
				if (auto* f = a_sp->GetFile(0); f && std::string_view(f->GetFilename()) == "Skyrim.esm" && Arcana::IndexOf(a_sp) >= 0) return false;  // (an altar spell you were given)
				for (std::string_view bad : { "diverse dragons spell menu", "azurite meditation" }) {
					if (n.find(bad) != std::string::npos) return true;
				}
				// (Noah: "remove the import mode and magical shout for spellsword") the Wizard Warrior's Import Mode and Magical
				// Shouts go for every class (its Enchantment Powers stay the Spellsword's)
				(void)cls;
				return n.find("import mode") != std::string::npos || n.find("magical shout") != std::string::npos;
			};
			std::vector<RE::SpellItem*> drop;
			for (auto* sp : p->GetActorRuntimeData().addedSpells) {
				if (unwanted(sp)) drop.push_back(sp);
			}
			for (auto* sp : drop) {
				p->RemoveSpell(sp);
				logger::info("classes: {} taken off (not of your race or class)", sp->GetName());
			}
			// (Noah: "please remove the magical shout from inventories") the Wizard Warrior's Magical Shout, a shout of its own
			if (auto* sh = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESShout>(0x0008B2, "The Wizard Warrior.esp"); sh && p->GetActorRuntimeData().selectedPower == sh) {
				p->GetActorRuntimeData().selectedPower = nullptr;
			}
			if (auto* sh = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESShout>(0x0008B2, "The Wizard Warrior.esp")) {
				static std::chrono::steady_clock::time_point told{};
				if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton(); vm && std::chrono::steady_clock::now() - told > std::chrono::seconds(20)) {
					told = std::chrono::steady_clock::now();
					const auto hd = vm->GetObjectHandlePolicy()->GetHandleForObject(p->GetFormType(), p);
					RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
					vm->DispatchMethodCall(hd, "Actor", "RemoveShout", RE::MakeFunctionArguments(static_cast<RE::TESShout*>(sh)), none);
				}
			}
			// (Noah: "Obtain the Spellsword Manual - what is that for?") the Spellsword mod's own start quest: its manual
			// is never needed here (the Spellsword's kit comes from the class)
			if (auto* q = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x009A24, "Spellsword.esp"); q && q->IsRunning()) {
				// (a console StopQuest didn't hold, nor Quest.Stop() through Papyrus, nor keeping it from starting with a new
				// game: the Spellsword mod's own scripts start it again) its objectives are put back to dormant every pass -
				// a quest showing none is not in the journal - and Stop tried natively a few times
				int hidden = 0;
				for (auto* o : q->objectives) {
					if (o && o->state.get() != RE::QUEST_OBJECTIVE_STATE::kDormant) {
						o->state = RE::QUEST_OBJECTIVE_STATE::kDormant;
						++hidden;
					}
				}
				q->data.flags.reset(RE::QuestFlag::kDisplayedInHUD, RE::QuestFlag::kActive);
				static int tries = 0;
				if (tries < 6) {
					++tries;
					q->Stop();
					Console(p, "StopQuest _Spellsword_Start_Quest");
				}
				if (hidden) logger::info("classes: the Spellsword mod's manual quest kept off the journal ({} objective(s) hidden, stop try {})", hidden, tries);
			}
			// (Noah: "Flames, Healing, Sparks - am I supposed to start with these?") the race's own starting spells go too
			// (its powers and abilities - Ancestor's Wrath - stay)
			if (auto* race = p->GetRace(); race && race->actorEffects) {
				std::vector<RE::SpellItem*> raceDrop;
				for (std::uint32_t i = 0; i < race->actorEffects->numSpells; ++i) {
					if (auto* sp = race->actorEffects->spells[i]; sp && sp->GetSpellType() == RE::MagicSystem::SpellType::kSpell && unwanted(sp)) raceDrop.push_back(sp);
				}
				for (auto* sp : raceDrop) {
					race->actorEffects->RemoveSpell(sp);
					p->RemoveSpell(sp);
					logger::info("classes: {} taken off the race's starting spells", sp->GetName());
				}
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

		void DismissSummons();
		RE::FormID ResolveId(RE::FormID a_id)
		{
			RE::FormID n = 0;
			if (auto* si = SKSE::GetSerializationInterface(); si && si->ResolveFormID(a_id, n)) return n;
			return 0;
		}
		// (Noah: "each class its own loadout and inventory - weapons, magic, armour sets, all of it"; "potions, food and crafting
		// materials should be shared") a class's own: its weapons, ammunition, armour and clothing, scrolls and spell tomes.
		// Shared by every class: gold, potions, food, ingredients and crafting materials, keys, quest items, books and notes,
		// the night's relics (our plugin's own pieces, the Belt Lantern among them), and your underwear
		bool ClassGear(RE::TESBoundObject* a_obj, const RE::InventoryEntryData* a_entry)
		{
			if (!a_obj || (a_entry && a_entry->IsQuestObject())) return false;
			const auto t = a_obj->GetFormType();
			if (t == RE::FormType::Book) {
				auto* bk = a_obj->As<RE::TESObjectBOOK>();
				if (!bk || !bk->TeachesSpell()) return false;
			} else if (t != RE::FormType::Weapon && t != RE::FormType::Armor && t != RE::FormType::Ammo && t != RE::FormType::Scroll) {
				return false;
			}
			if (a_obj == Underwear()) return false;  // (worn by every class)
			if (auto* f = a_obj->GetFile(0); f && std::string_view(f->GetFilename()) == "Paragliding.esp") return false;  // (Noah: "the dragon glider shared on all classes")
			// our own plugin's pieces are the night's rewards (Alduin's Bane, the hall's relics), kept by every class - but the
			// class weapons it makes (wands, tomes, bows, the Conjurer's Staff) belong to their class
			// (the audit: the Ranger's altar bows and the Subsuming Vortex followed you into every class) they are class gear too
			auto* dh = RE::TESDataHandler::GetSingleton();
			const bool vortex = dh && (a_obj == dh->LookupForm(gen::kVortexWeap, gen::kPlugin) || a_obj == dh->LookupForm(gen::kVortexAmmo, gen::kPlugin));
			const bool blade = dh && (a_obj == dh->LookupForm(gen::kRiftblade, gen::kPlugin) || a_obj == dh->LookupForm(gen::kMadnessSword, gen::kPlugin));
			if (auto* f = a_obj->GetFile(0); f && std::string_view(f->GetFilename()) == gen::kPlugin && Forms::GunIndexOf(a_obj) < 0 && Bows::IndexOf(a_obj) < 0 && !vortex && ElemCrossbowIndex(a_obj) < 0 && !blade) return false;  // (Noah: the Ranger's Ember / Rime / Storm Crossbows went into every class)
			return true;
		}
		// (Noah: "I switched from Summoner to Spellsword and still kept all of my Summoner stuff") a class's things wait in its
		// own hidden chest; should the chest not be had, they are held in the co-save instead (the item and its count), so a
		// switch always takes them off you
		std::array<std::vector<std::pair<RE::FormID, std::int32_t>>, 4> g_held;
		std::array<std::vector<RE::FormID>, 4>                          g_spellStash;  // per class: its other spells (learned from tomes, other mods)
		RE::TESObjectREFR* ChestOf(int a_cls, bool a_make)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || a_cls < 0 || a_cls >= 4) return nullptr;
			auto* base = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESBoundObject>(gen::kClassChest, gen::kPlugin);
			if (g_chest[a_cls]) {
				if (auto* r = RE::TESForm::LookupByID<RE::TESObjectREFR>(g_chest[a_cls]); r && !r->IsDeleted() && base && r->GetBaseObject() == base) return r;  // (only our own chest - never a reused ID)
				logger::info("classes: {}'s chest {:08X} is gone", Name(a_cls), g_chest[a_cls]);
				g_chest[a_cls] = 0;
			}
			if (!a_make) return nullptr;
			if (!base) {
				logger::warn("classes: the class chest (UN_ClassChest) not found - {}'s things held in the save instead", Name(a_cls));
				return nullptr;
			}
			auto ref = p->PlaceObjectAtMe(base, true);
			if (!ref) {
				logger::warn("classes: {}'s chest could not be set down - its things held in the save instead", Name(a_cls));
				return nullptr;
			}
			ref->Disable();
			g_chest[a_cls] = ref->GetFormID();
			logger::info("classes: {}'s chest made ({:08X})", Name(a_cls), ref->GetFormID());
			return ref.get();
		}
		// everything the class you leave was carrying of its own (above) into its chest; what it had on, and its weapon slots,
		// noted
		void StashGear(int a_cls)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || a_cls < 0 || a_cls >= 4) return;
			auto* chest = ChestOf(a_cls, true);
			g_wornStash[a_cls].clear();
			for (int s = 0; s < 3; ++s) g_slotStash[a_cls][s] = Save::GetSlot(s), Save::SetSlot(s, {});
			struct Move { RE::TESBoundObject* obj; std::int32_t count; RE::ExtraDataList* x; };
			std::vector<Move> moves;
			for (auto& [obj, e] : p->GetInventory()) {
				if (!obj || e.first <= 0 || !ClassGear(obj, e.second.get())) continue;
				if (e.second && e.second->IsWorn()) g_wornStash[a_cls].push_back(obj->GetFormID());
				std::int32_t left = e.first;
				if (e.second && e.second->extraLists) {
					for (auto* x : *e.second->extraLists) {
						if (!x || left <= 0) continue;
						const std::int32_t c = std::max<std::int32_t>(1, x->GetCount());
						moves.push_back({ obj, std::min(c, left), x });
						left -= std::min(c, left);
					}
				}
				if (left > 0) moves.push_back({ obj, left, nullptr });
			}
			int items = 0;
			for (auto& m : moves) {
				items += m.count;
				if (chest) {
					p->RemoveItem(m.obj, m.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, m.x, chest);
				} else {
					p->RemoveItem(m.obj, m.count, RE::ITEM_REMOVE_REASON::kRemove, m.x, nullptr);
					g_held[a_cls].push_back({ m.obj->GetFormID(), m.count });
				}
			}
			logger::info("classes: {}'s gear put away ({} stacks, {} items, {})", Name(a_cls), moves.size(), items, chest ? "in its chest" : "held in the save");
		}
		// a class you come back to: its chest (and anything held for it in the save) emptied back into your pack, its pieces
		// back on, its slots back
		bool RestoreGear(int a_cls)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || a_cls < 0 || a_cls >= 4) return false;
			auto* chest = ChestOf(a_cls, false);
			struct Move { RE::TESBoundObject* obj; std::int32_t count; RE::ExtraDataList* x; };
			std::vector<Move> moves;
			if (chest) {
				for (auto& [obj, e] : chest->GetInventory()) {
					if (!obj || e.first <= 0) continue;
					std::int32_t left = e.first;
					if (e.second && e.second->extraLists) {
						for (auto* x : *e.second->extraLists) {
							if (!x || left <= 0) continue;
							const std::int32_t c = std::max<std::int32_t>(1, x->GetCount());
							moves.push_back({ obj, std::min(c, left), x });
							left -= std::min(c, left);
						}
					}
					if (left > 0) moves.push_back({ obj, left, nullptr });
				}
				for (auto& m : moves) chest->RemoveItem(m.obj, m.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, m.x, p);
			}
			int held = 0;
			for (auto [id, n] : g_held[a_cls]) {
				if (auto* o = RE::TESForm::LookupByID<RE::TESBoundObject>(id); o && n > 0) p->AddObjectToContainer(o, nullptr, n, nullptr), held += n;
			}
			g_held[a_cls].clear();
			for (int s = 0; s < 3; ++s) Save::SetSlot(s, g_slotStash[a_cls][s]);
			if (auto* em = RE::ActorEquipManager::GetSingleton()) {
				for (auto id : g_wornStash[a_cls]) {
					auto* o = RE::TESForm::LookupByID<RE::TESBoundObject>(id);
					if (o && (o->IsArmor() || o->GetFormType() == RE::FormType::Ammo || (o->IsWeapon() && Forms::GunIndexOf(o) < 0)) && p->GetItemCount(o) > 0) em->EquipObject(p, o, nullptr, 1, nullptr, false, false, false, true);
				}
			}
			logger::info("classes: {}'s gear taken back out ({} stacks from its chest, {} items from the save)", Name(a_cls), moves.size(), held);
			return true;
		}

		// the class's spells off you (kept for when you come back to it), the new class's back on
		void Swap(int a_from, int a_to)
		{
			// (Noah: "when switching from spellsword to another class, the orange rune from the hand casting animation gets
			// stuck on my hand") a spell in a hand is put down properly before the class's spells are taken: its cast
			// stopped and the spell unequipped, so its hand art (the Spellsword's rune) goes with it - taking the spell
			// away while it was held left its art behind
			if (a_from != a_to) {
				if (auto* p = RE::PlayerCharacter::GetSingleton()) {
					auto* em = RE::ActorEquipManager::GetSingleton();
					for (bool left : { true, false }) {
						auto* sp = p->GetEquippedObject(left) ? p->GetEquippedObject(left)->As<RE::SpellItem>() : nullptr;
						if (!sp) continue;
						if (auto* mc = p->GetMagicCaster(left ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand)) mc->InterruptCast(false);
						if (em) em->UnequipObject(p, sp, nullptr, 1, nullptr, false, true, false, true);
						logger::info("classes: {} put down from the {} hand", sp->GetName(), left ? "left" : "right");
					}
				}
			}
			if (a_from == coffer::kSummoner && a_to != coffer::kSummoner) DismissSummons();
			if (a_from >= 0 && a_from < 4 && a_from != a_to) StashGear(a_from), g_perkStash[a_from] = Perks::TakeAll();
			if (a_from >= 0 && a_from < 4) {
				g_stash[a_from].clear();
				for (int i : Arcana::Carried(-1)) {  // (every altar spell you hold goes with the class you leave - g_class is already the new one)
					g_stash[a_from].push_back({ i, Arcana::Level(i) });
				}
				for (auto [i, lv] : g_stash[a_from]) Arcana::Forget(i);
				// (the audit) the altar's other spells it taught this class go with it
				if (a_from != a_to) {
					if (auto* p = RE::PlayerCharacter::GetSingleton()) {
						for (auto id : g_extra[a_from]) if (auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(id)) p->RemoveSpell(sp);
						// (Noah: "magic" goes with its class too) every other spell you had learned as this class (from a tome,
						// another mod's gift) - spells only: your race's powers, abilities and shouts stay with you
						g_spellStash[a_from].clear();
						std::vector<RE::SpellItem*> other;
						for (auto* sp : p->GetActorRuntimeData().addedSpells) {
							if (!sp || sp->GetSpellType() != RE::MagicSystem::SpellType::kSpell || Arcana::IndexOf(sp) >= 0) continue;
							if (std::find(g_extra[a_from].begin(), g_extra[a_from].end(), sp->GetFormID()) != g_extra[a_from].end()) continue;
							other.push_back(sp);
						}
						for (auto* sp : other) g_spellStash[a_from].push_back(sp->GetFormID()), p->RemoveSpell(sp);
						if (!other.empty()) logger::info("classes: {} of {}'s other spells put away", other.size(), Name(a_from));
					}
				}
			}
			if (a_to >= 0 && a_to < 4 && a_from != a_to) {
				if (auto* p = RE::PlayerCharacter::GetSingleton()) {
					for (auto id : g_extra[a_to]) if (auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(id)) p->AddSpell(sp);
					for (auto id : g_spellStash[a_to]) if (auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(id)) p->AddSpell(sp);
					g_spellStash[a_to].clear();
				}
			}
			if (!g_kit[a_to]) {
				GiveKit(a_to);
			} else {
				RestoreGear(a_to);
				if (!g_perkStash[a_to].empty()) Perks::GiveBack(g_perkStash[a_to]), g_perkStash[a_to].clear();
				// (the class review: hands came back swapped - and the Spellsword's blade knocked out of its right) the stash is
				// kept left hand first: it goes back there, the next to the right, the rest to the book; the Spellsword's right
				// hand is its blade's, so its others wait in the book
				int n = 0;
				for (auto [i, lv] : g_stash[a_to]) {
					if (n == 0) GiveSpell(i, true);
					else if (n == 1 && a_to != coffer::kSpellsword) GiveSpell(i, false);
					else Arcana::Give(i, false);
					Arcana::SetLevel(i, lv);
					++n;
				}
			}
		}
	}

	// ---- (Noah: each class its own wonder weapon, mid and end game) never rolled: (Noah: "make the wonders harder to
	// obtain so players can't get over powered early on") once six cities are freed the class's secret quest wakes (src/
	// Eggs.cpp: a long hidden quest, step by step from city to city) and its end hands over the wonder. After Sovngarde
	// (MQ305 done) each one takes its full, Legendary form.
	//   Summoner     Horn of the Spectral Wyrm  Calling of the Conjurer's Spectral Dragon, 60 s (Legendary: it stays)
	//   Destruction  Tome of the Six Fires      one cast looses all six element ladders' top spells round you (Legendary: their runes and storms too)
	//   Ranger       Subsuming Vortex           (Noah) a purple bow: each arrow opens a black hole that drags the dead in, then bursts (Legendary: wider, stronger pull)
	//   Spellsword   Dragon Slayer              each hit carries the spell in your other hand (Legendary: a power attack sends it down the line)
	namespace
	{
		constexpr int kWonderSteps = 3, kWonderOpensAt = 6;  // (g_wStep: 0 shut, 1-3 its quest open, 4 won and taken up, 5 won - waiting on its pedestal)
		constexpr int kWonderTaken = kWonderSteps + 1, kWonderWaiting = kWonderSteps + 2;
		constexpr const char* kWonderName[4] = { "Horn of the Spectral Wyrm", "Tome of the Six Fires", "Subsuming Vortex", "Dragon Slayer" };
		std::array<std::uint8_t, 4>  g_wStep{};    // per class: 0 not open, 1..3 the step under way, 4 earned
		std::array<std::uint16_t, 4> g_wKills{};
		std::array<std::uint32_t, 4> g_wTowns{};   // cities where a step was done (bits)
		std::unordered_map<RE::FormID, RE::FormID> g_lastHit;  // a foe -> what you last hit it with
		std::unordered_set<RE::FormID> g_powerHit;  // foes your last blow on was a power attack (the Spellsword's resonance)
		std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_lastBlade;  // a foe -> when your blade last struck it
		std::uint16_t g_boltKills = 0;
		std::array<std::uint16_t, 16> g_bowMastery{};  // (Noah: "make the ranger class also need kills") each altar bow's kills toward its next tier  // (the Ranger's crossbow mastery) kills with a crossbow: fire bolts at 25, ice at 60, shock at 120
		// (Noah) the Ranger's elemental crossbows (Ember, Rime, Storm) and their bolts: Faction Crossbows' Dawnguard bolts
		// of that element. A crossbow's quiver holds 40 bolts, 20 more for each reforge
		RE::TESObjectWEAP* ElemCrossbow(int a_e)
		{
			return a_e >= 0 && a_e < 3 ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kElemCrossbows[a_e], gen::kPlugin) : nullptr;
		}
		RE::TESAmmo* ElemBolts(int a_e)
		{
			return a_e >= 0 && a_e < 3 ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESAmmo>(0x020B05 + a_e, "Faction Crossbows.esp") : nullptr;
		}
		int  BoltCap(int a_level) { return 40 + 20 * std::max(0, a_level); }
		int  CrossbowLevelImpl(const RE::TESForm* a_w);
		int TopUpBolts(int)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return 0;
			int n = 0;
			for (int e = 0; e < 3; ++e) {
				auto* w = ElemCrossbow(e);
				auto* b = ElemBolts(e);
				if (!w || !b || p->GetItemCount(w) <= 0) continue;
				const int cap = BoltCap(CrossbowLevelImpl(w));
				if (const int have = p->GetItemCount(b); have < cap) {
					p->AddObjectToContainer(b, nullptr, cap - have, nullptr);
					++n;
				}
			}
			return n;
		}
		float g_tomeCool = 0, g_bladeCool = 0, g_vortexCool = 0;
		struct VortexArrowState { RE::NiPoint3 last; bool done, hit; };
		std::unordered_map<RE::FormID, VortexArrowState> g_vortexArrows;  // the Subsuming Vortex's arrows in flight

		int FreedCount()
		{
			int n = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		bool AfterSovngarde() { return Echo::DragonsSlain(); }  // (the story: the wonders' Legendary form - once the three dragons fall)
		RE::SpellItem* WyrmSpell() { auto* f = Lookup(0x019D74, "Calling of the Conjurer.esp"); return f ? f->As<RE::SpellItem>() : nullptr; }
		RE::SpellItem* TomeSpell() { auto* dh = RE::TESDataHandler::GetSingleton(); return dh ? dh->LookupForm<RE::SpellItem>(gen::kWonderTomeSpell, gen::kPlugin) : nullptr; }
		RE::TESObjectWEAP* Vortex() { auto* dh = RE::TESDataHandler::GetSingleton(); return dh ? dh->LookupForm<RE::TESObjectWEAP>(gen::kVortexWeap, gen::kPlugin) : nullptr; }
		RE::TESAmmo* VortexArrow() { auto* dh = RE::TESDataHandler::GetSingleton(); return dh ? dh->LookupForm<RE::TESAmmo>(gen::kVortexAmmo, gen::kPlugin) : nullptr; }
		RE::TESObjectWEAP* Slayer() { auto* f = Lookup(0x00083D, "Ultra Greatswords.esp"); return f ? f->As<RE::TESObjectWEAP>() : nullptr; }
		RE::SpellItem* Myst(std::uint32_t a_id) { auto* f = Lookup(a_id, "MysticismMagic.esp"); return f ? f->As<RE::SpellItem>() : nullptr; }

		// (the Subsuming Vortex) a hole where an arrow came down - one a moment, however fast you loose
		void OpenVortex(const RE::NiPoint3& a_at, bool a_onHit)
		{
			if (a_onHit) {
				for (auto& [id, a] : g_vortexArrows) if (!a.done) { a.done = true, a.hit = true; break; }  // (the arrow that struck: no second hole where it falls)
			}
			if (g_vortexCool > 0) return;
			g_vortexCool = 0.6f;
			Gun::BlackHole(a_at, AfterSovngarde());
		}
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
			Forms::CastOnce(a_from, a_sp, a_at, 1.0f, a_from);
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
				if (auto* sp = WyrmSpell()) Learn(sp);
				break;
			case coffer::kDestruction:
				if (auto* sp = TomeSpell()) Learn(sp);
				break;
			case coffer::kRanger:
				GiveItem(p, Vortex(), 1, true);
				GiveItem(p, VortexArrow(), 60, true);
				break;
			case coffer::kSpellsword:
				GiveItem(p, Slayer(), 1, true);
				break;
			}
		g_wStep[a_cls] = kWonderTaken;
			Forms::Play(gen::Sound::shout_learned);
			static constexpr const char* kText[4] = {
				"A horn of dragon bone. Blow it and Calling of the Conjurer's Spectral Dragon fights at your side for a minute. Once the three dragons fall, it stays for an hour, or until it is slain.",
				"One cast looses the top spell of your six element ladders - Fire, Frost, Shock, Sun, Blood and Runes - on the dead round you, each burst on its foe. Once the three dragons fall, the fires burn hotter and their third rungs come too.",
				"A bow of black-violet steel. Every arrow tears open a black hole where it lands: the dead are dragged in, then it bursts. Once the three dragons fall, the holes grow wider and pull harder. More arrows: at any Echo Altar, or from a Max Ammo.",
				"The Akaviri dragon slayer, two tiers above your blade. Every hit carries the last Spellsword spell you held. Once the three dragons fall it grows stronger again, and a power attack looses that spell on the dead round you." };
			std::string up(kWonderName[a_cls]);
			for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
			Hud::Lore(up, kText[a_cls], 12.0f);
			logger::info("classes: wonder {} earned", kWonderName[a_cls]);
		}

		// a kill of the class's own kind: the secret quest's "classkills" steps count it (Eggs)
		void WonderKill(int a_cls)
		{
			if (a_cls < 0 || a_cls > 3) return;
			const auto st = g_wStep[a_cls];
			if (st == 0 || st > kWonderSteps) return;
			Eggs::OnClassKill();
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

	// (Noah: "the second will be the class guide after selecting a class") the class's guide book, in your pack once
	namespace
	{
		RE::TESObjectBOOK* Guide(int a_cls)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh && a_cls >= 0 && a_cls < 4 ? dh->LookupForm<RE::TESObjectBOOK>(gen::kClassGuides[a_cls], gen::kPlugin) : nullptr;
		}
		bool GiveGuide(int a_cls)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* b = Guide(a_cls);
			if (!p || !b || p->GetItemCount(b) > 0) return false;
			p->AddObjectToContainer(b, nullptr, 1, nullptr);
			logger::info("classes: {} added", b->GetName());
			return true;
		}
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
		if (GiveGuide(a_cls)) {
			auto* b = Guide(a_cls);
			std::string t(b ? b->GetName() : "Your class guide");
			for (auto& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
			Hud::Lore(t, "Your class guide is in your pack, under Books. It shows how your class grows and keeps a live page of how far you have come.", 9.0f);
		}
		logger::info("classes: {} -> {}{}", Name(from), Name(a_cls), g_switching ? " (switched at an altar)" : "");
	}

	// a new character, out of the creator: the picker
	void OnNewCharacter()
	{
		// (Noah: "the class chooser built into the Echo Altar") no pop-up: the altar is where a class is chosen
		if (g_class.load() < 0) Hud::Banner("Go to Riverwood's Echo Altar and choose your class", 6.0f);
	}
	// a save loaded with no class yet (older saves): the picker too
	void OnWorldReady()
	{
		if (const int c = g_class.load(); c >= 0) GiveGuide(c);  // (a save from before the guides: its class's guide)
		// (Noah: "it only lets me equip a helmet and boots") the kit and the underwear went on locked in older builds: each
		// worn piece is taken off and put back on, free to change
		if (auto* p = RE::PlayerCharacter::GetSingleton(); p) {
			if (auto* em = RE::ActorEquipManager::GetSingleton()) {
				std::vector<RE::TESBoundObject*> worn;
				for (auto& [obj, e] : p->GetInventory([](RE::TESBoundObject& o) { return o.IsArmor(); })) {
					if (obj && e.second && e.second->IsWorn()) worn.push_back(obj);
				}
				for (auto* o : worn) em->UnequipObject(p, o, nullptr, 1, nullptr, false, true, false, true);
				for (auto* o : worn) em->EquipObject(p, o, nullptr, 1, nullptr, false, false, false, true);
				if (!worn.empty()) logger::info("classes: {} worn pieces put back on unlocked", worn.size());
			}
		}
		if (g_class.load() < 0 && Save::Flag(Save::kFlagStarted)) Hud::Banner("Choose your class at an Echo Altar", 6.0f);
	}

	// (each class grows its own way) a kill trains the ladders of what you hold: the spell in each hand for Destruction and
	// the Spellsword, and your summons' kills (or your own, a summon spell in hand) for the Summoner
	void OnKill(bool a_bySummon, RE::Actor* a_dying, RE::Actor* a_killer)
	{
		Kit::OnKill(a_dying);  // (the altar rework: on-kill Infusions, the Legendary Riftblade's thralls)
		int killHow = -1;
		// the wonder trial: what made the kill
		if (const int c = g_class.load(); c >= 0) {
			int& how = killHow;  // 0 summon, 1 destruction spell, 2 bow or crossbow, 3 melee
			auto* p = RE::PlayerCharacter::GetSingleton();
			RE::TESForm* src = nullptr;
			if (a_dying) {
				if (auto it = g_lastHit.find(a_dying->GetFormID()); it != g_lastHit.end()) src = RE::TESForm::LookupByID(it->second);
			}
			// (the Ranger review: an altar bow's tier II/III strikes are Mysticism spells - its kills read as a Battle-mage's) the
			// bow in your hands made them
			const bool bowSpell = Bows::Held() >= 0 && src && src->As<RE::MagicItem>() && Arcana::IndexOf(src) < 0;
			if (a_bySummon) how = 0;
			else if (auto* mi = src && !bowSpell ? src->As<RE::MagicItem>() : nullptr) how = (mi->GetAssociatedSkill() == RE::ActorValue::kDestruction || (Arcana::IndexOf(src) >= 0 && Arcana::ClassOf(Arcana::IndexOf(src)) == coffer::kDestruction)) ? 1 : -1;
			else if (auto* w = src && !bowSpell ? src->As<RE::TESObjectWEAP>() : nullptr) {
				how = IsRangerWeapon(w) ? 2 : 3;  // (OffhandBows' one-hand crossbows are staves to Skyrim)
				if (how == 2 && !w->IsBow() && Bows::IndexOf(w) < 0 && (FreedCount() >= 3 || Gun::TestKitOn())) {
					auto& x = g_xbow[w->GetFormID()];
					x.second = static_cast<std::uint16_t>(std::min(60000, x.second + 1));
				}
				if (const int b = Bows::IndexOf(w); b >= 0 && b < static_cast<int>(g_bowMastery.size()) && (FreedCount() >= 3 || Gun::TestKitOn())) g_bowMastery[b] = static_cast<std::uint16_t>(std::min(60000, g_bowMastery[b] + 1));
				if (w->IsCrossbow() && c == coffer::kRanger && g_boltKills < 60000) ++g_boltKills;  // (the elemental crossbows bring their own bolts now)
			}
			else if (const int b = Bows::Held(); b >= 0) {  // (the altar's bows strike with our own shots)
				how = 2;
				if (b < static_cast<int>(g_bowMastery.size()) && (FreedCount() >= 3 || Gun::TestKitOn())) g_bowMastery[b] = static_cast<std::uint16_t>(std::min(60000, g_bowMastery[b] + 1));
			}
			else if (p) {
				for (int h : Arcana::Hands()) if (h >= 0 && Arcana::ClassOf(h) == coffer::kDestruction) how = 1;
			}
			// a blade struck it within the last second and a half: a melee kill, whatever spell rode the blade after it
			if (how != 0 && how != 2 && a_dying) {
				if (auto it = g_lastBlade.find(a_dying->GetFormID()); it != g_lastBlade.end() && std::chrono::steady_clock::now() - it->second < std::chrono::milliseconds(1500)) how = 3;
			}
			if (how == c) WonderKill(c);  // (the classes' kinds of kill line up with the class order)
			int element = -1;
			if (const int ci = src ? Arcana::IndexOf(src) : -1; ci >= 0) element = coffer::kSpells[ci].element;
			const float dist = a_dying && p ? a_dying->GetPosition().GetDistance(p->GetPosition()) : 0.0f;
			if (how >= 0) Curse::OnClassKill(how, element, dist);
			// (the class sets' powers) the Arcane Conjurer's Tier III: your summons' kills heal you a little; the Gondorian's
			// Tier III: a kill from 30 m or more puts stamina back
			if (p && !p->IsDead()) {
				auto* av = p->AsActorValueOwner();
				if (how == 0 && c == coffer::kSummoner && Kit::ArmourTier(c) >= 3) av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.02f);
				if (how == 2 && c == coffer::kRanger && dist >= 2100.0f && Kit::ArmourTier(c) >= 3) av->RestoreActorValue(RE::ActorValue::kStamina, av->GetPermanentActorValue(RE::ActorValue::kStamina) * 0.15f);
			}
			if (how >= 0) Perks::OnClassKill(how, a_dying);  // (the class perks: Soul-Tether, Ember Heart, Mana-Siphon, Spellbinder)
		}
		if (const int c2 = g_class.load(); c2 >= 0 && c2 < 4) {
			if (g_wStep[c2] >= 1 && g_wStep[c2] <= kWonderSteps && FreedCount() < kWonderOpensAt) g_wStep[c2] = 0;  // (an old save's three-city trial: shut again until six)
			if (g_wStep[c2] == 0 && FreedCount() >= kWonderOpensAt) {
				g_wStep[c2] = 1;
				Forms::Play(gen::Sound::quest_start);
				Hud::Lore("YOUR PATH'S SECRET",
					fmt::format("Six cities are free, and the {} stirs somewhere beneath the Echo Altars. Nobody will tell you how to win it. Stand at a freed altar and listen to its whispers.", kWonderName[c2]), 12.0f);
			}
		}
		// (the classes' growth, the Ranger's own reward) a long shot - 30 m or more - gives its arrow back and a breath of stamina
		if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && a_dying && g_class.load() == coffer::kRanger && killHow == 2 &&
			a_dying->GetPosition().GetDistance(pl->GetPosition()) >= 2100.0f) {
			if (auto* ammo = pl->GetCurrentAmmo()) pl->AddObjectToContainer(ammo, nullptr, 1, nullptr);
			auto* av = pl->AsActorValueOwner();
			av->RestoreActorValue(RE::ActorValue::kStamina, 0.15f * av->GetPermanentActorValue(RE::ActorValue::kStamina));
		}
		// (Noah's plan, the Spellsword's resonance) its touch spells climb on melee kills only - a power attack counts twice
		// (Noah: "make kills count after the forge opens") nothing counts toward a climb before three altars are freed
		const bool forgeOpen = FreedCount() >= 3 || Gun::TestKitOn();
		const bool power = a_dying && g_powerHit.contains(a_dying->GetFormID());
		auto handsNow = Arcana::Hands();
		if (g_class.load() == coffer::kSpellsword && handsNow[0] < 0 && handsNow[1] < 0) handsNow[0] = BladeSpell();  // (a two-handed blade: the spell it carries)
		// (the classes' growth: a summon's kill trained whatever was in your hands) it trains the family that called it up
		if (a_bySummon && forgeOpen) {
			if (const int fam = SummonFamily(a_killer); fam >= 0) {
				auto& m = g_mastery[coffer::kSpells[fam].ladder];
				m = static_cast<std::uint16_t>(std::min(60000, m + 1));
				handsNow = { -1, -1 };
			}
		}
		for (int h : handsNow) {
			if (!forgeOpen) break;
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
	int BowMastery(int a_bow) { return a_bow >= 0 && a_bow < static_cast<int>(g_bowMastery.size()) ? g_bowMastery[a_bow] : 0; }
	int BowMasteryNeed(int a_bow) { const int lv = Bows::Level(a_bow); return lv <= 0 ? 25 : lv == 1 ? 60 : 120; }
	void SpendBowMastery(int a_bow) { if (a_bow >= 0 && a_bow < static_cast<int>(g_bowMastery.size())) g_bowMastery[a_bow] = 0; }
	int Mastery(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? g_mastery[coffer::kSpells[a_index].ladder] : 0; }
	// kills a ladder needs before its next rung can be raised: 25 to rung 2, 60 to rung 3, 120 to the Legendary
	// (the consistency pass: a short ladder's Legendary came after 25 kills and 5,000) the Legendary rung always asks the most
	// (the Spellsword review: Dragon Slayer is two-handed - with it no spell could be held, so its own power, the resonance,
	// the trial and the climbs all stopped) the spell your blade carries: the Spellsword spell in a hand, or - with a
	// two-handed blade - the last one you held
	int BladeSpell()
	{
		static int remembered = -1;
		const auto h = Arcana::Hands();
		for (int i : { h[0], h[1] }) {
			if (i >= 0 && Arcana::ClassOf(i) == coffer::kSpellsword) return remembered = i;
		}
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* w = p ? p->GetEquippedObject(false) : nullptr;
		auto* wp = w ? w->As<RE::TESObjectWEAP>() : nullptr;
		const bool twoHands = wp && (wp->IsTwoHandedSword() || wp->IsTwoHandedAxe());
		// (the classes' growth: the bound spell) a spellshield raised in the left keeps the last spell on the blade
		auto* l = p ? p->GetEquippedObject(true) : nullptr;
		auto* sh = l ? l->As<RE::TESObjectARMO>() : nullptr;
		const bool shield = sh && sh->IsShield() && sh->GetFile(0) && std::string_view(sh->GetFile(0)->GetFilename()) == "Spellsword.esp";
		if ((twoHands || shield) && remembered >= 0 && Arcana::Known(remembered)) return remembered;
		return -1;
	}
	// (the classes' growth) the summon family an actor of yours came from: its creature's form, or its name, against what
	// each Summoner spell calls up (-1: none of the altar's)
	int SummonFamily(RE::Actor* a_actor)
	{
		static std::unordered_map<RE::FormID, int> byForm;
		static std::unordered_map<std::string, int> byName;
		static bool built = false;
		if (!built) {
			built = true;
			for (int i = 0; i < static_cast<int>(coffer::kSpells.size()); ++i) {
				if (coffer::kSpells[i].cls != coffer::kSummoner) continue;
				auto* sp = Arcana::Spell(i);
				if (!sp) continue;
				for (auto* e : sp->effects) {
					auto* m = e ? e->baseEffect : nullptr;
					if (!m || m->GetArchetype() != RE::EffectArchetypes::ArchetypeID::kSummonCreature || !m->data.associatedForm) continue;
					byForm[m->data.associatedForm->GetFormID()] = i;
					if (const char* n = m->data.associatedForm->GetName(); n && *n) byName[n] = i;
				}
			}
		}
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!base) return -1;
		if (auto it = byForm.find(base->GetFormID()); it != byForm.end()) return it->second;
		if (const char* n = a_actor->GetName(); n && *n) {
			if (auto it = byName.find(n); it != byName.end()) return it->second;
		}
		return -1;
	}
	// ---- (the classes' growth) a crossbow reforged at the altar: three times, a quarter more each, on its own kills
	int CrossbowLevel(const RE::TESForm* a_w) { if (!a_w) return 0; auto it = g_xbow.find(a_w->GetFormID()); return it == g_xbow.end() ? 0 : it->second.first; }
	namespace { int CrossbowLevelImpl(const RE::TESForm* a_w) { return CrossbowLevel(a_w); } }
	int CrossbowElement(const RE::TESForm* a_w)
	{
		if (!a_w) return -1;
		for (int e = 0; e < 3; ++e) if (a_w == ElemCrossbow(e)) return e;
		return -1;
	}
	RE::TESAmmo* CrossbowBolts(int a_e) { return ElemBolts(a_e); }
	int          CrossbowQuiver(int a_level) { return BoltCap(a_level); }
	// (Noah: "one crossbow and one bow at a time - the Storm Crossbow should have replaced my fire one and its bolts")
	// every elemental crossbow but this one goes back, with its bolts
	void KeepOnlyCrossbow(int a_keep)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* em = RE::ActorEquipManager::GetSingleton();
		if (!p) return;
		for (int e = 0; e < 3; ++e) {
			if (e == a_keep) continue;
			auto* w = ElemCrossbow(e);
			auto* b = ElemBolts(e);
			if (w && p->GetItemCount(w) > 0) {
				for (bool left : { false, true }) if (em && p->GetEquippedObject(left) == w) em->UnequipObject(p, w, nullptr, 1, nullptr, false, false, false, true);
				p->RemoveItem(w, p->GetItemCount(w), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				logger::info("classes: {} given back - one crossbow at a time", w->GetName());
			}
			if (b && p->GetItemCount(b) > 0) {
				if (em && p->GetCurrentAmmo() == b) em->UnequipObject(p, b, nullptr, 1, nullptr, false, false, false, true);
				p->RemoveItem(b, p->GetItemCount(b), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
		}
	}
	RE::TESObjectWEAP* HeldCrossbow()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		for (bool left : { false, true }) {
			auto* o = p ? p->GetEquippedObject(left) : nullptr;
			if (auto* w = o ? o->As<RE::TESObjectWEAP>() : nullptr; w && IsRangerWeapon(w) && !w->IsBow() && Bows::IndexOf(w) < 0) return w;
		}
		return nullptr;
	}
	int ReforgeCrossbow(int a_freed, bool& a_spent)
	{
		a_spent = false;
		auto* w = HeldCrossbow();
		if (!w) return -1;  // (none in hand)
		auto& x = g_xbow[w->GetFormID()];
		const std::string name = w->GetName() ? w->GetName() : "Crossbow";
		if (x.first >= gen::kForgeMax) { Hud::Banner(name + " cannot hold more power", 2.5f); return 0; }
		const int need = x.first == 0 ? 25 : x.first == 1 ? 60 : 120;
		if (x.first + 1 >= 3 && a_freed < 6 && !Gun::TestKitOn()) { Hud::Banner(name + "'s last reforge waits until six Echo Altars are freed (" + std::to_string(a_freed) + " of 6)", 3.0f); return 0; }
		if (x.second < need) { Hud::Banner(name + ": " + std::to_string(x.second) + " / " + std::to_string(need) + " kills with it before it can be reforged", 3.0f); return 0; }
		if (!Points::Spend(gen::kForgeCost[x.first])) return 0;
		x.second = 0, ++x.first;
		a_spent = true;
		TopUpBolts(0);
		Hud::Banner(name + " reforged  " + std::string(x.first, '|') + (CrossbowElement(w) >= 0 ? fmt::format("  - harder bolts, a bigger blast, a quiver of {}", BoltCap(x.first)) : std::string("  - its bolts strike harder")), 4.0f);
		logger::info("forge: crossbow {} -> level {}", name, x.first);
		return 1;
	}
	int MasteryNeed(int a_index)
	{
		const int next = Arcana::NextRung(a_index);
		if (next < 0 || Arcana::TierOf(next) >= 3) return 120;  // (a Legendary, and each Ascend after it)
		const int r = Arcana::RungOf(a_index);
		return r <= 1 ? 25 : 60;
	}
	void SpendMastery(int a_index)
	{
		if (a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size())) g_mastery[coffer::kSpells[a_index].ladder] = 0;
	}

	// (the Summoner's command) how many summons you may hold: 1, then 2 / 3 / 4 as its ladders climb (each rung raised
	// past the first counts): our hidden perks set Skyrim's commanded-actor limit
	namespace { std::mutex g_lineLock; std::string g_line; std::vector<std::string> g_progress; int g_progressCls = -1; }
	std::string BoltLine();
	std::string WonderLineNow();
	// (Noah: "a guide book for each class that tracks progression") the class guide's live page: built here once a second
	// (the game's own thread), read by the Codex when the book is open
	std::vector<std::string> ProgressNow()
	{
		std::vector<std::string> out;
		const int c = g_class.load();
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (c < 0 || !p) return out;
		const int freed = FreedCount();
		std::string towns;
		for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
			if (Curse::Freed(t)) towns += (towns.empty() ? "" : ", ") + std::string(gen::kTowns[t]);
		}
		out.push_back(fmt::format("<b>Echo Altars freed: {} of {}</b>{}", freed, gen::kTowns.size(), towns.empty() ? std::string(" - none yet. Start with Riverwood's altar.") : " (" + towns + ")."));
		// (the consistency pass: every class was told of a Legendary row only the Summoner has) the forge's gates, as they are
		out.push_back(fmt::format("Your kit's tiers (the Echo Altar): Tier I open. Tier II: {}. Tier III (Legendary): {}. Ascends: {}. Your armor set's damage reduction: +{:.0f}%.",
			freed >= 2 ? std::string("<b>open</b>") : fmt::format("at 2 freed cities ({} to go)", 2 - freed),
			freed >= 4 ? std::string("<b>open</b>") : fmt::format("at 4 freed cities ({} to go)", 4 - freed),
			freed >= 6 ? std::string("<b>open</b>") : fmt::format("at 6 freed cities ({} to go)", 6 - freed), Kit::ArmorResist() * 100.0f));
		// the spells you carry: where each stands on its ladder, and the kills it still needs
		bool any = false;
		for (int i : Arcana::Carried()) {
			if (Arcana::ClassOf(i) != c) continue;
			any = true;
			const int r = Arcana::RungOf(i), n = Arcana::Rungs(i), next = Arcana::NextRung(i);
			if (next < 0) {
				out.push_back(fmt::format("<b>{}</b> ({} ladder) - rung {} of {}: <b>Legendary</b>, the top of its ladder.", Arcana::Name(i), Arcana::LadderName(i), r, n));
			} else {
				const int have = std::min(Mastery(i), MasteryNeed(i)), need = MasteryNeed(i);
				out.push_back(fmt::format("<b>{}</b> ({} ladder) - rung {} of {}. Kills toward <b>{}</b>: {} / {}{}", Arcana::Name(i), Arcana::LadderName(i), r, n,
					Arcana::Name(next), have, need, have >= need ? " - ready: raise it at an altar's forge" : freed < 3 ? " (kills count once the forge opens)" : ""));
			}
		}
		if (!any && c != coffer::kRanger) out.push_back("You carry none of your class's altar spells right now - call one at an Echo Altar.");
		switch (c) {
		case coffer::kSummoner:
			{
				int cap = 1;
				for (int k = 8; k >= 2; --k) {
					if (auto* perk = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSPerk>(gen::kSummonCapPerks[k - 2], gen::kPlugin); perk && p->HasPerk(perk)) { cap = k; break; }
				}
				out.push_back(fmt::format("<b>Summon slots: {}.</b> You start with 2 and get one more for every 2 Echo Altars you free, up to 7 - and the Conjurer's Command Mead adds one more, up to 8.", cap));
				out.push_back(fmt::format("Indrik mounts: {} of 10 - one for each altar you free.", std::min(freed, 10)));
				out.push_back(freed >= 6 ? std::string("<b>Hero's Pact</b> is open: bind a hero or an army at any altar's second row.") : fmt::format("Hero's Pact: opens at 6 freed altars ({} to go).", 6 - freed));
			}
			break;
		case coffer::kDestruction:
			out.push_back("Hold a different element in each hand: the Trial of the Elements wants kills with two elements or more.");
			break;
		case coffer::kRanger:
			{
				const int b = Bows::Carried();
				if (b < 0) {
					out.push_back("You carry no elemental bow - call one at an Echo Altar.");
				} else if (Bows::Level(b) >= 3) {
					out.push_back(fmt::format("<b>Your bow: {}</b>, tier 3 of 3 - {}: <b>fully reforged</b>. Quiver: 200 arrows, growing back one every 2 s.", Bows::Name(b), Bows::TierName(b, 3)));
				} else {
					const int have = std::min(BowMastery(b), BowMasteryNeed(b)), need = BowMasteryNeed(b);
					out.push_back(fmt::format("<b>Your bow: {}</b>, tier {} of 3 - {}. Kills toward tier {} ({}): {} / {}{}", Bows::Name(b), Bows::Level(b), Bows::TierName(b, Bows::Level(b)),
						Bows::Level(b) + 1, Bows::TierName(b, Bows::Level(b) + 1), have, need, have >= need ? " - ready: reforge it at an altar" : freed < 3 ? " (kills count once the forge opens)" : ""));
				}
				out.push_back(BoltLine() + ".");
			}
			break;
		case coffer::kSpellsword:
			{
				// (the audit: the old two-hander's Enchantment Powers line) your blade's tier
				const int bt = Kit::TierOfWeapon(SpellswordBlade());
				static constexpr const char* kTierName[4] = { "Base", "Tier I", "Tier II", "Tier III - Legendary" };
				out.push_back(bt < 0 ? std::string("<b>Your blade:</b> not yet upgraded.")
				                     : bt <= 3 ? fmt::format("<b>Your blade:</b> {}.", kTierName[bt]) : fmt::format("<b>Your blade:</b> Legendary, Ascend {}.", bt - 3));
			}
			break;
		}
		// the class's own perks
		std::string perks;
		for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
			if (gen::kPerkClass[i] != c) continue;
			perks += (perks.empty() ? "" : ", ") + std::string(gen::kPerks[i].label) + (Save::PerkStacks(i) > 0 ? " (taken)" : "");
		}
		if (!perks.empty()) out.push_back("Your class's perks at the altar: " + perks + ".");
		out.push_back("<b>Wonder:</b> " + WonderLineNow() + ".");
		// (the story audit: nothing tracked Acts III and IV)
		if (Echo::WondersAll()) {
			const int m = Echo::DragonMask(), n = (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1);
			out.push_back(fmt::format("<b>Alduin's words silenced:</b> {} of 3{}.", n, n < 3 ? " - SLEN in the Reach, TIID at Labyrinthian, VO on Mount Anthor, each at night" : ""));
			if (Echo::CurseBroken()) out.push_back("<b>The Echo of Alduin:</b> slain - the curse is broken.");
			else if (Echo::EchoAwake()) out.push_back("<b>The Echo of Alduin:</b> waits on the summit of the Throat of the World, at night.");
			else if (n == 3) out.push_back("<b>The Echo of Alduin:</b> waits until every Echo Altar is awake.");
		}
		return out;
	}
	std::string WonderLineNow();
	void Tick()
	{
		static auto last = std::chrono::steady_clock::now();
		if (std::chrono::steady_clock::now() - last < std::chrono::seconds(1)) return;
		last = std::chrono::steady_clock::now();
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!p || !dh) return;
		if (static int purge = 0; ++purge % 3 == 0 && p->Is3DLoaded() && !ItemMenusOpen()) Purge();  // (the audit: never under the Magic menu)
		TuneCorrosion();
		TuneSpellswordGear();
		TickArmour();
		// (Noah: "for the destruction class there should only be a 1 second delay before the mana starts regenerating after
		// firing - 5 seconds is fine for the others"; "sprinting should cost a little more stamina for all players")
		if (auto* gs = RE::GameSettingCollection::GetSingleton()) {
			static float regenWas = -1.0f, sprintWas = -1.0f;
			if (auto* st = gs->GetSetting("fMagickaRegenDelay")) {
				if (regenWas < 0) regenWas = st->data.f;
				const float want = g_class.load() == coffer::kDestruction ? 1.0f : regenWas;
				if (st->data.f != want) st->data.f = want, logger::info("classes: magicka comes back {:.0f} s after a cast", want);
			}
			if (auto* st = gs->GetSetting("fSprintStaminaDrainMult")) {
				if (sprintWas < 0) sprintWas = st->data.f, st->data.f = sprintWas * 1.5f, logger::info("classes: sprinting drains stamina {:.1f} (was {:.1f})", st->data.f, sprintWas);
			}
		}
		// (Noah: the belt lantern is an item now) a character from before it is handed one, worn
		if (g_class.load() >= 0 && !g_lanternGiven && p->Is3DLoaded() && !ItemMenusOpen()) {
			GiveLantern(p);
			Hud::Banner("The Belt Lantern hangs at your hip - take it off and it goes dark", 5.0f);
		}
		// (Noah: the Madness Sword for the Spellsword) a Spellsword from before it trades the Bolgan Sword for it, once
		if (static bool madGiven = false; !madGiven && g_class.load() == coffer::kSpellsword && p->Is3DLoaded() && !ItemMenusOpen()) {
			madGiven = true;
			auto* mad = SpellswordBlade();
			auto* bolgan = Lookup(0x065CAF, "mihailhighfantasypack.esp");
			auto* old = bolgan ? bolgan->As<RE::TESBoundObject>() : nullptr;
			if (mad && old && mad != old && p->GetItemCount(mad) == 0 && p->GetItemCount(old) > 0) {
				const bool held = p->GetEquippedObject(false) == old;
				p->RemoveItem(old, p->GetItemCount(old), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				GiveItem(p, mad, 1, held);
				Hud::Banner("The Madness Sword is your blade now", 5.0f);
				logger::info("classes: the Bolgan Sword traded for the Madness Sword");
			}
		}
		// (Noah) a Ranger from before the elemental crossbows is handed the Ember Crossbow and its bolts, once
		if (static bool xbowGiven = false; !xbowGiven && g_class.load() == coffer::kRanger && p->Is3DLoaded()) {
			xbowGiven = true;
			bool any = false;
			for (int e = 0; e < 3; ++e) if (auto* w = ElemCrossbow(e); w && p->GetItemCount(w) > 0) any = true;
			if (!any && ElemCrossbow(0)) {
				GiveItem(p, ElemCrossbow(0), 1, false);
				GiveItem(p, ElemBolts(0), BoltCap(0), false);
				Hud::Banner("The Ember Crossbow and its fire bolts are in your pack - the Rime and Storm Crossbows wait at the Echo Altar", 6.0f);
			}
		}
		auto* uiN = RE::UI::GetSingleton();
		const bool menus = !uiN || uiN->GameIsPaused() || uiN->IsMenuOpen(RE::InventoryMenu::MENU_NAME) || uiN->IsMenuOpen(RE::FavoritesMenu::MENU_NAME) || uiN->IsMenuOpen(RE::ContainerMenu::MENU_NAME) || uiN->IsMenuOpen(RE::BarterMenu::MENU_NAME);
		// (Noah: "switching from Ranger to Spellsword I still had the Ranger's things") an elemental crossbow and its bolts
		// carried by another class go back into the Ranger's chest
		if (!menus && g_class.load() >= 0 && g_class.load() != coffer::kRanger) {
			for (int e = 0; e < 3; ++e) {
				for (RE::TESBoundObject* o : { static_cast<RE::TESBoundObject*>(ElemCrossbow(e)), static_cast<RE::TESBoundObject*>(ElemBolts(e)) }) {
					if (!o || p->GetItemCount(o) <= 0) continue;
					if (auto* chest = ChestOf(coffer::kRanger, true)) {
						if (auto* em = RE::ActorEquipManager::GetSingleton()) em->UnequipObject(p, o, nullptr, 1, nullptr, false, false, false, true);
						p->RemoveItem(o, p->GetItemCount(o), RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, chest);
						logger::info("classes: {} put back in the Ranger's chest", o->GetName());
					}
				}
			}
		}
		// (one crossbow at a time) more than one carried from before: the one in hand stays, else the last of them
		if (!menus) {
			int owned = 0, keep = -1;
			for (int e = 0; e < 3; ++e) if (auto* w = ElemCrossbow(e); w && p->GetItemCount(w) > 0) ++owned, keep = e;
			if (owned > 1) {
				if (const int h = CrossbowElement(HeldCrossbow()); h >= 0) keep = h;
				KeepOnlyCrossbow(keep);
			}
		}
		// (Noah) an elemental crossbow in hand nocks its own element's bolts
		// (Noah: the game crashed switching bolts in the inventory) never while a menu is open, and never over bolts you
		// chose yourself: only when what is nocked isn't a bolt at all
		if (auto* w = HeldCrossbow(); w && p->Is3DLoaded() && !menus) {
			if (const int e = CrossbowElement(w); e >= 0) {
				auto* want = ElemBolts(e);
				auto* cur = p->GetCurrentAmmo();
				if (want && p->GetItemCount(want) > 0 && (!cur || !cur->IsBolt())) {
					if (auto* em = RE::ActorEquipManager::GetSingleton()) em->EquipObject(p, want, nullptr, 1, nullptr, false, false, false, true);
				}
			}
		}
		// (Noah: "all the weapons with enchantments shouldn't need a soul gem to recharge") what you hold never runs dry
		{
			auto* av = p->AsActorValueOwner();
			for (auto v : { RE::ActorValue::kRightItemCharge, RE::ActorValue::kLeftItemCharge }) {
				if (av->GetActorValue(v) < av->GetPermanentActorValue(v)) av->RestoreActorValue(v, av->GetPermanentActorValue(v));
			}
		}
		// (Noah) worn armour made only for the other body is taken off; a man's Drifter Outfit becomes the Dawnguard's
		if (static int fitCheck = 0; ++fitCheck % 3 == 0 && p->Is3DLoaded() && !menus) {
			const bool fem = PlayerFemale();
			std::vector<RE::TESObjectARMO*> off;
			for (auto& [obj, data] : p->GetInventory()) {
				auto* a = obj ? obj->As<RE::TESObjectARMO>() : nullptr;
				if (a && data.second && data.second->IsWorn() && !Fits(a, fem)) off.push_back(a);
			}
			bool drifter = false;
			for (auto* a : off) {
				if (auto* em = RE::ActorEquipManager::GetSingleton()) em->UnequipObject(p, a, nullptr, 1, nullptr, false, false, false, true);
				drifter |= a->GetFile(0) && a->GetFile(0)->GetFilename() == "DrifterArmorAndOutfitByXtudo.esp";
				logger::info("classes: {} taken off - it has no model for a {} body", a->GetName(), fem ? "woman's" : "man's");
			}
			if (drifter) {
				for (auto id : kDawnguardLight) {
					auto* f = Lookup(id, "Dawnguard.esm");
					auto* b = f ? f->As<RE::TESBoundObject>() : nullptr;
					if (b) GiveItem(p, b, p->GetItemCount(b) > 0 ? 0 : 1, true);
				}
				Hud::Banner("The Drifter Outfit is made for a woman's body - the Dawnguard's light armor in its place", 4.0f);
			}
		}
		// (the shells the Spell-weapon row once gave - Flame Shield's 600 magicka and the like - are taken back)
		if (static bool shellsGone = false; !shellsGone && p->Is3DLoaded()) {
			shellsGone = true;
			for (std::uint32_t id : { 0x29265Eu, 0x29266Bu, 0x292664u, 0x292672u, 0x29265Du, 0x29266Cu, 0x292665u, 0x292673u, 0x209A73u }) {
				if (auto* sp = Myst(id); sp && p->HasSpell(sp)) {
					p->RemoveSpell(sp);
					logger::info("classes: {} taken back (the altar no longer gives the Mysticism shells)", sp->GetName());
				}
			}
		}
		// (Noah: "the Spellsword should only have touch spells") a breath spell carried from before goes back, and the first
		// rung of a touch you don't carry takes its place
		if (g_class.load() == coffer::kSpellsword && p->Is3DLoaded()) {
			for (int i : Arcana::Carried()) {
				if (Arcana::Rollable(i)) continue;
				std::array<bool, coffer::kLadders> have{};
				for (int c : Arcana::Carried()) have[coffer::kSpells[c].ladder] = true;
				int pick = -1;
				for (int j = 0; j < static_cast<int>(coffer::kSpells.size()) && pick < 0; ++j) {
					const auto& sj = coffer::kSpells[j];
					if (sj.cls == coffer::kSpellsword && sj.rung == 1 && Arcana::Rollable(j) && !have[sj.ladder] && Arcana::Spell(j)) pick = j;
				}
				const std::string was(Arcana::Name(i));
				if (pick >= 0) Arcana::Replace(i, pick);
				else Arcana::Forget(i);
				Hud::Banner("The Spellsword's spells are touches: " + was + " went back to the altar" + (pick >= 0 ? " - " + std::string(Arcana::Name(pick)) + " in its place" : std::string()), 5.0f);
				logger::info("classes: {} (a breath) given back{}", was, pick >= 0 ? fmt::format(" for {}", Arcana::Name(pick)) : std::string());
				break;
			}
		}
		// (the Summoner review) the three dragons' fall mid-session turns the Horn of the Spectral Wyrm Legendary at once, not at the next load
		// (the story audit: tuned while the co-save loaded, perhaps before the game's globals were back) checked every second
		if (static int was = -1; static_cast<int>(AfterSovngarde()) != was) {
			was = AfterSovngarde() ? 1 : 0;
			if (g_wStep[coffer::kSummoner] > kWonderSteps) TuneWyrm();
		}
		// (Noah: "random Dremora at the Echo Altar attack me and my summons") a conjured creature with no master - a summon
		// whose spell ended while it was out of the world, or a stand-in from before a load - crumbles instead of turning on you
		if (static int sweep = 0; ++sweep % 5 == 0 && Party::FriendCount() == 0) {  // (alone: a friend's summons show here masterless)
			static std::unordered_set<RE::FormID> g_masterless;
			std::unordered_set<RE::FormID> nowMasterless;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!a || a.get() == p || a->IsDead() || a->IsDisabled() || a->IsCommandedActor() || IsOwnSummon(a.get()) || Echo::IsAlly(a.get()) || Party::IsFriend(a.get())) continue;
				auto* base = a->GetActorBase();
				if (!base || !base->IsSummonable()) continue;
				if (Director::IsRoundUndead(a.get()) || (base->GetFile(0) && std::string_view(base->GetFile(0)->GetFilename()) == gen::kPlugin)) continue;
				// (masterless on two sweeps running, five seconds apart: never a summon in its first moments)
				if (!g_masterless.contains(a->GetFormID())) {
					nowMasterless.insert(a->GetFormID());
					continue;
				}
				logger::info("classes: a masterless conjured {} ({:08X}) let go", a->GetName(), a->GetFormID());
				a->Disable();
				a->SetDelete(true);
			}
			g_masterless = std::move(nowMasterless);
		}
		// (Noah: "the weapons that contain a spell shouldn't need a soul gem - only the Echo Altar's, not Skyrim's own") an
		// altar weapon in either hand never runs dry: its charge is topped up each second
		for (const bool left : { false, true }) {
			auto* obj = p->GetEquippedObject(left);
			auto* w = obj ? obj->As<RE::TESObjectWEAP>() : nullptr;
			auto* file = w ? w->GetFile(0) : nullptr;
			if (!file || !w->formEnchanting) continue;
			const std::string_view fn = file->GetFilename();
			if (fn != "Spellsword.esp" && fn != "Light Greatswords.esp" && fn != "Ultra Greatswords.esp") continue;
			p->AsActorValueOwner()->RestoreActorValue(left ? RE::ActorValue::kLeftItemCharge : RE::ActorValue::kRightItemCharge, 1.0e6f);
			// (and on the item itself: the charge it carries in your pack)
			const float full = static_cast<float>(w->amountofEnchantment);
			for (auto& [item, data] : p->GetInventory([w](RE::TESBoundObject& a_o) { return &a_o == w; })) {
				if (!data.second || !data.second->extraLists) continue;
				for (auto* xl : *data.second->extraLists) {
					if (auto* xc = xl ? xl->GetByType<RE::ExtraCharge>() : nullptr; xc && full > 0 && xc->charge < full) xc->charge = full;
				}
			}
		}
		// (Noah: "a guided first night") a new character's first steps, each card shown once: the altar, septims, the night's
		// waves, the breath between them, and the first dawn
		if (p->Is3DLoaded() && Save::Flag(Save::kFlagMoved) && !Open()) {
			static float wait = 0;
			auto card = [&](int a_bit, const char* a_title, const char* a_text) {
				if (g_guide & (1 << a_bit)) return false;
				g_guide |= static_cast<std::uint8_t>(1 << a_bit);
				Hud::Lore(a_title, a_text, 11.0f);
				wait = 13.0f;  // (one card at a time)
				logger::info("guide: '{}' shown", a_title);
				return true;
			};
			struct { int wave = 0, round = 0; } hs;
			{
				std::scoped_lock l(Hud::Lock());
				hs.wave = Hud::Edit().wave, hs.round = Hud::Edit().round;
			}
			if ((wait -= 1.0f) <= 0) {
				const bool fighting = Director::Fighting();
				if (g_class.load() < 0) {
					card(0, "THE ECHO ALTAR", "Walk to Riverwood's Echo Altar and press Activate to choose your class. Every city has an altar: it is where you spend Cursed Essence on your kit, perks and upgrades.");
				} else if (!fighting && !(g_guide & 2)) {
					card(1, "CURSED ESSENCE", "Every hit and every kill on the dead earns Cursed Essence - your total sits in the bottom-left corner. Spend it at an Echo Altar between waves and by day.");
				} else if (fighting && hs.wave <= 1 && !(g_guide & 4)) {
					card(2, "THE NIGHT", "At dusk the dead rise in waves. The counter at the top shows how many are left in this wave. Clear every wave and dawn breaks. Fall, and the wave begins again - and your blessings are lost.");
				} else if (fighting && hs.wave >= 2 && !(g_guide & 8)) {
					card(3, "BETWEEN WAVES", "A few seconds' breath before the next wave rises: heal, reload, find better ground, or run for an altar. The last few of every wave come straight for you.");
				} else if (!fighting && hs.round >= 1 && (g_guide & 4) && !(g_guide & 16)) {
					card(4, "DAWN", "The dead crumble at first light. Spend your Cursed Essence, then rest until dusk - every night is harder than the last.");
				}
			}
		}
		// (Noah: the staff is gone) a Summoner from before learns Corrosion in its place (the staff itself is the Echo's Void
		// Staff, the same weapon - it is never taken: the audit found that took the wonder from whoever held it)
		if (!g_staffChecked && p->Is3DLoaded()) {
			g_staffChecked = true;
			if (g_class.load() == coffer::kSummoner && g_kit[coffer::kSummoner]) {
				bool knows = false;
				for (int i = 0; i < static_cast<int>(coffer::kSpells.size()); ++i) {
					if (coffer::kSpells[i].cls == coffer::kSummoner && std::string_view(coffer::kSpells[i].ladderLabel) == "Reanimate" && Arcana::Spell(i) && p->HasSpell(Arcana::Spell(i))) knows = true;
				}
				if (!knows) GiveSpell(Rung1(coffer::kSummoner, "Reanimate"), false);
				// (Noah: Reanimate in Corrosion's place) Mysticism's old Corrosion bolts are taken back
				auto* dh0 = RE::TESDataHandler::GetSingleton();
				for (std::uint32_t id : { 0x29C8ABu, 0x2A19B9u, 0x9E9213u }) {
					if (auto* old = dh0 ? dh0->LookupForm<RE::SpellItem>(id, "MysticismMagic.esp") : nullptr; old && p->HasSpell(old)) p->RemoveSpell(old);
				}
				// (Noah: the Summoner's enchanted dagger) a Summoner from before is given the Riftblade
				if (auto* rift = dh0 ? dh0->LookupForm<RE::TESBoundObject>(gen::kRiftblade, gen::kPlugin) : nullptr; rift && p->GetItemCount(rift) <= 0) GiveItem(p, rift, 1, false);
			} else if (g_kit[coffer::kSummoner]) {
				// (the audit) a Summoner waiting in its stash gets Corrosion there, for when you come back to it
				auto& st = g_stash[coffer::kSummoner];
				const bool has = std::any_of(st.begin(), st.end(), [](const auto& e) { return e.first >= 0 && e.first < static_cast<int>(coffer::kSpells.size()) && std::string_view(coffer::kSpells[e.first].ladderLabel) == "Reanimate"; });
				if (!has && Rung1(coffer::kSummoner, "Reanimate") >= 0) st.insert(st.begin(), { Rung1(coffer::kSummoner, "Reanimate"), 0 });
			}
		}
		// (Noah: no movement-blocking hits) the night's dead never stagger you: the Steady perk (Mod Incoming Stagger x0)
		if (auto* steady = dh->LookupForm<RE::BGSPerk>(gen::kSteadyPerk, gen::kPlugin); steady && !p->HasPerk(steady)) {
			p->AddPerk(steady);
			logger::info("classes: Steady given - the night's dead never stagger you");
		}
		{  // (Noah: "increase the attack speed with melee weapons") both hands' swing speed, from the Mod Configuration menu
			// (Noah: "a bow and crossbow draw speed changer next to the melee speed") Skyrim's weapon speed scales a bow's
			// draw and a crossbow's reload as it does a swing: with a bow or crossbow in hand it takes the draw setting
			auto*       av = p->AsActorValueOwner();
			auto*       held = p->GetEquippedObject(false);
			auto*       wp = held ? held->As<RE::TESObjectWEAP>() : nullptr;
			const bool  ranged = IsRangerWeapon(wp);
			const float want = ranged ? Settings::DrawSpeed() * Perks::DrawSpeedMult() : Settings::MeleeSpeed() * Perks::SwingSpeedMult();
			if (std::abs(av->GetBaseActorValue(RE::ActorValue::kWeaponSpeedMult) - want) > 0.01f) av->SetBaseActorValue(RE::ActorValue::kWeaponSpeedMult, want);
			if (std::abs(av->GetBaseActorValue(RE::ActorValue::kLeftWeaponSpeedMultiply) - want) > 0.01f) av->SetBaseActorValue(RE::ActorValue::kLeftWeaponSpeedMultiply, want);
		}  // (Noah: still there before the class was chosen - from the first moment now)
		if (auto* ui = RE::UI::GetSingleton(); ui && !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) && !ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME)) DressUnder();  // (Noah: a bra and underwear, never bare)
		{
			auto line = WonderLineNow();
			auto prog = ProgressNow();
			std::scoped_lock l(g_lineLock);
			g_line = std::move(line);
			g_progress = std::move(prog);
			g_progressCls = g_class.load();
		}
		// (Fletcher's Sap Draught: the quiver's regrowth comes sooner and faster - Bows.inl TickRegen)
		// (Noah's plan, the Spellsword) Imbue unlocks The Wizard Warrior's Enchantment Powers 1 to 4, one per rung climbed
		// (the audit: the Spellsword's Enchantment Powers and the Summoner's Indrik mounts followed you into every class)
		// another class's are taken off; coming back gives them again, without the banner a second time
		static std::unordered_set<RE::FormID> announced;
		static constexpr std::uint32_t kPowers[4] = { 0x0008A0, 0x000893, 0x0008A1, 0x002F6A };
		static constexpr std::uint32_t kIndrik[] = { 0x801, 0x847, 0x848, 0x849, 0x84A, 0x84B, 0x84C, 0x84D, 0x84E, 0x84F };
		if (g_class.load() >= 0) {
			if (g_class.load() != coffer::kSpellsword) {
				for (auto id : kPowers) if (auto* sp = dh->LookupForm<RE::SpellItem>(id, "The Wizard Warrior.esp"); sp && p->HasSpell(sp)) p->RemoveSpell(sp);
			}
			if (g_class.load() != coffer::kSummoner) {
				for (auto id : kIndrik) if (auto* sp = dh->LookupForm<RE::SpellItem>(id, "mihailindrikmounts.esp"); sp && p->HasSpell(sp)) p->RemoveSpell(sp);
			}
		}
		if (g_class.load() == coffer::kSpellsword) {
			int imbued = 0;
			for (int i : Arcana::Carried()) {
				if (Arcana::ClassOf(i) == coffer::kSpellsword) imbued += Arcana::RungOf(i) - 1;
			}
			for (int n = 0; n < std::min(imbued, 4); ++n) {
				auto* sp = dh->LookupForm<RE::SpellItem>(kPowers[n], "The Wizard Warrior.esp");
				if (sp && !p->HasSpell(sp)) {
					p->AddSpell(sp);
					if (announced.insert(sp->GetFormID()).second) {
						Forms::Play(gen::Sound::shout_learned);
						Hud::Banner(fmt::format("The Wizard Warrior: Enchantment Power {} unlocked (Powers menu)", n + 1), 4.0f);
						logger::info("classes: Wizard Warrior enchantment power {}", n + 1);
					}
				}
			}
		}
		// (Noah's plan, the Summoner's mounts) an Indrik mount for each freed city
		if (g_class.load() == coffer::kSummoner) {
			const int freed = FreedCount();
			for (int n = 0; n < std::min<int>(freed, static_cast<int>(std::size(kIndrik))); ++n) {
				auto* sp = dh->LookupForm<RE::SpellItem>(kIndrik[n], "mihailindrikmounts.esp");
				if (sp && !p->HasSpell(sp)) {
					p->AddSpell(sp);
					if (announced.insert(sp->GetFormID()).second) Hud::Banner(std::string("A city's gift: ") + sp->GetName(), 4.0f);
				}
			}
		}
		// (Noah: "summoner should start with 2 summon slots and go up for every 2 echo altars freed - max 7")
		int cap = 1;
		if (g_class.load() == coffer::kSummoner) {
			cap = std::min(7, 2 + FreedCount() / 2);
			if (Save::PerkStacks(gen::kPerk_command) > 0) cap = std::min(8, cap + 1);  // (the Conjurer's Command Mead: one more - 8 at most)
			if (Kit::ArmourTier(coffer::kSummoner) >= 2) cap = std::min(8, cap + 1);  // (the Arcane Conjurer's robes, Tier II: one more)
		}
		for (int n = 2; n <= 8; ++n) {
			auto* perk = dh->LookupForm<RE::BGSPerk>(gen::kSummonCapPerks[n - 2], gen::kPlugin);
			if (!perk) continue;
			const bool want = n == cap, have = p->HasPerk(perk);
			if (want && !have) {
				p->AddPerk(perk);
				Hud::Banner("Summon slots: " + std::to_string(n), 3.0f);
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
			{ "Giant Crusher", 3 }, { "Spirit Sword", 3 }, { "Deathbane", 6 }, { "Mirror", 6 }, { "Shadow Blade", 6 }, { "Hex", 6 },  // (the classes' growth: the Spellsword's Legendary steel, at six freed altars)
			 { "Glass", 2 }, { "Ebony", 2 }, { "Advanced", 2 }, { "Nordic", 2 }, { "Slayer", 2 }, { "Ballista", 2 },
			{ "Dwarven", 1 }, { "Dwemer", 1 }, { "Elven", 1 }, { "Silver", 1 }, { "Imperial", 1 }, { "Falmer", 1 }, { "Repeater", 1 }, { "Tribow", 1 }, { "Twin", 1 } };
		for (auto [k, t] : kTiers) if (a_name.find(k) != std::string_view::npos) return t;
		return 0;
	}
	// (Noah: "unique two-handed weapons in the Echo Altar for the Spellsword, with enchantments") Skyrim's named
	// greatswords, battleaxes and warhammers, each at its own number of freed altars; the Ebony Blade is Legendary
	struct Unique { std::string_view plugin; RE::FormID id; int tier; };
	constexpr Unique kTwoHanders[] = {
		{ "Skyrim.esm", 0x06AF63, 0 },      // Trollsbane
		{ "Skyrim.esm", 0x01C4E6, 1 },      // the Rueful Axe
		{ "Dawnguard.esm", 0x011BAD, 2 },   // the Dawnguard Rune Hammer
		{ "Spellsword.esp", 0x00C0C5, 2 },  // Greatsword of the Righteous
		{ "Dragonborn.esm", 0x01AEA4, 3 },  // the Bloodskal Blade
		{ "Skyrim.esm", 0x02ACD2, 3 },      // Volendrung
		{ "Spellsword.esp", 0x01910A, 4 },  // the Aetherial Great Spellsword
		{ "Skyrim.esm", 0x04A38F, 6 },      // the Ebony Blade
	};
	int GearTier(const RE::TESBoundObject* a_obj)
	{
		if (!a_obj) return 0;
		for (const auto& u : kTwoHanders) {
			if (a_obj->GetLocalFormID() == u.id && a_obj->GetFile(0) && a_obj->GetFile(0)->GetFilename() == u.plugin) return u.tier;
		}
		return a_obj->GetName() ? MaterialTier(a_obj->GetName()) : 0;
	}
	int AltarGearUpgrades(const RE::TESForm* a_w)
	{
		// (the class audit: the Dragon Slayer, the Spellsword's wonder, never grew - four times weaker than its Madness Sword)
		// it stands two tiers above your blade (Tier III at the least), three once the three dragons fall
		if (auto* sl = Slayer(); a_w && sl && a_w == sl) return std::max(3, Kit::TierOfWeapon(SpellswordBlade())) + (AfterSovngarde() ? 3 : 2);
		if (const int kt = Kit::TierOfWeapon(a_w); kt >= 0) return kt;  // (the altar rework: a kit or relic blade's own tiers)
		auto* b = a_w ? a_w->As<RE::TESBoundObject>() : nullptr;
		if (!b || !b->GetFile(0)) return -1;
		bool altar = b->GetFile(0)->GetFilename() == "Spellsword.esp";
		for (const auto& u : kTwoHanders) altar |= b->GetLocalFormID() == u.id && b->GetFile(0)->GetFilename() == u.plugin;
		return altar ? std::clamp(GearTier(b), 0, 3) : -1;
	}
	bool RollGear(int a_cls, Prize* a_out)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!p || !dh) return false;
		int freed = 0;
		for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) freed += Curse::Freed(t) ? 1 : 0;
		std::vector<RE::TESBoundObject*> pool;
		std::unordered_set<std::string> held;
		for (auto& [obj, data] : p->GetInventory()) if (obj && data.first > 0 && obj->GetName() && *obj->GetName()) held.insert(obj->GetName());
		const auto from = [&](std::string_view a_plugin, auto* a_type) {
			using T = std::remove_pointer_t<decltype(a_type)>;
			for (auto* f : dh->GetFormArray<T>()) {
				if (!f || !f->GetFile(0) || f->GetFile(0)->GetFilename() != a_plugin) continue;
				const char* nm = f->GetName();
				if (!nm || !*nm || MaterialTier(nm) > freed) continue;
				if (p->GetItemCount(f) > 0 || held.contains(nm)) continue;  // (one by that name already yours: no second "Steel Spellsword")
				if constexpr (std::is_same_v<T, RE::TESObjectARMO>) {
					if (!f->IsShield()) continue;
				}
				if constexpr (std::is_same_v<T, RE::TESObjectWEAP>) {  // (Noah: the Spellsword's altar weapons are one-handed only)
					if (a_cls == coffer::kSpellsword && !f->IsOneHandedSword() && !f->IsOneHandedDagger() && !f->IsOneHandedAxe() && !f->IsOneHandedMace()) continue;
				}
				pool.push_back(f);
			}
		};
		if (a_cls == coffer::kSpellsword) {
			from("Spellsword.esp", static_cast<RE::TESObjectWEAP*>(nullptr));
			from("Spellsword.esp", static_cast<RE::TESObjectARMO*>(nullptr));
			for (const auto& u : kTwoHanders) {
				auto* w = dh->LookupForm<RE::TESObjectWEAP>(u.id, u.plugin);
				if (!w || u.tier > freed || p->GetItemCount(w) > 0 || (w->GetName() && held.contains(w->GetName()))) continue;
				pool.push_back(w);
			}
			// (one of each name: the blades that lost their " of ..." share theirs)
			std::unordered_set<std::string> names;
			std::erase_if(pool, [&](RE::TESBoundObject* f) { return !names.insert(f->GetName() ? f->GetName() : "").second; });
		} else if (a_cls == coffer::kRanger) {
			// (Noah) the Ranger's crossbows are the three elemental ones (Ember, Rime, Storm): one of each, the bows beside them
			for (int e = 0; e < 3; ++e) {
				if (auto* w = ElemCrossbow(e); w && p->GetItemCount(w) <= 0) pool.push_back(w);
			}
			// (Noah: "the wristbow broke my crossbow, doesn't work properly, and does 45 frost") OffhandBows' one-hand
			// crossbows are staves to Skyrim: one in the off hand can't be held with the Ranger's two-handed bows and
			// crossbows, and it fires on enchantment charge - the altar no longer gives them
		}
		// (Noah: "a spell I can't even use - it costs too much mana") the Mysticism shells (Flame Shield and the rest) no longer come from this row
		if (pool.empty()) {
			Hud::Banner("The altar has nothing new for you until another city is freed", 3.0f);
			return false;
		}
		static std::mt19937 rng{ std::random_device{}() };
		auto* pick = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(rng)];
		if (a_out) {
			a_out->name = pick->GetName();
			{
				auto* pw = pick->As<RE::TESObjectWEAP>();
				const bool legend = GearTier(pick) >= 6;
				const bool two = pw && (pw->IsTwoHandedSword() || pw->IsTwoHandedAxe());
				FillReel(a_out, pool, pick, a_cls == coffer::kRanger ? "Crossbow" : legend ? (two ? "Legendary Greatweapon" : "Legendary Spell-weapon") : two ? "Unique Greatweapon" : pw ? "Spell-weapon" : "Spellshield", legend);
			}
			a_out->give = [pick, a_cls] {
				auto* pl = RE::PlayerCharacter::GetSingleton();
				if (!pl) return;
				const auto had = pl->GetItemCount(pick);
				const int  xe = CrossbowElement(pick);
				if (xe >= 0) KeepOnlyCrossbow(xe);  // (the old crossbow and its bolts go back)
				pl->AddObjectToContainer(pick, nullptr, 1, nullptr);
				if (xe >= 0) {
					TopUpBolts(0);
					if (auto* em = RE::ActorEquipManager::GetSingleton()) {
						em->EquipObject(pl, pick, nullptr, 1, nullptr, false, false, false, true);
						if (auto* b = ElemBolts(xe); b && pl->GetItemCount(b) > 0) em->EquipObject(pl, b, nullptr, 1, nullptr, false, false, false, true);
					}
				}
				if (auto* w = pick->As<RE::TESObjectWEAP>(); IsRangerWeapon(w) && !w->IsBow()) Bows::Restock();
				// (Noah: "I collected two of the item") keep it to the one given - anything that doubled the add is taken back
				Tasks::Later(1.5f, [pick, had] {
					if (ItemMenusOpen()) return;  // (the audit: never with an item menu open - the extra stays)
					auto* q = RE::PlayerCharacter::GetSingleton();
					if (const auto extra = q ? q->GetItemCount(pick) - (had + 1) : 0; extra > 0) {
						q->RemoveItem(pick, extra, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						logger::info("classes: altar gear came twice ({} extra taken back)", extra);
					}
				});
				Hud::Banner(std::string(pick->GetName()) + "  -  in your pack", 4.0f);
				logger::info("classes: altar gear {} ({})", pick->GetName(), Name(a_cls));
			};
			return true;
		}
		p->AddObjectToContainer(pick, nullptr, 1, nullptr);
		if (auto* w = pick->As<RE::TESObjectWEAP>(); IsRangerWeapon(w) && !w->IsBow()) Bows::Restock();  // (a crossbow comes with its bolts)
		Hud::Banner(std::string(pick->GetName()) + "  -  in your pack", 4.0f);
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
			{ "Dawnguard", "Dawnguard.esm", { 0x00F3FB, 0x00F400, 0x00F3FE, 0, 0 } },
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
			{
				const bool fem = PlayerFemale();
				std::erase_if(pieces, [&](RE::TESBoundObject* b) { return !Fits(b->As<RE::TESObjectARMO>(), fem); });  // (none your body can't wear)
			}
			if (pieces.empty()) continue;  // (that outfit's mod isn't installed, or it isn't made for you)
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

	namespace
	{
		constexpr std::uint32_t kSupport[] = { 0x1901E5, 0x4B065B, 0x25FBFF, 0xFF19E0 };  // Ash Cloud, Cinder Storm, Charged Nova, Symmetry
	}
	// (Noah: "I'm not able to cast it because it costs too much mana" - Ash Cloud at 282) the support relics cost about a
	// third of Mysticism's price, never more than 100 magicka
	void TameSupport()
	{
		for (auto id : kSupport) {
			auto* sp = Myst(id);
			if (!sp || sp->data.flags.any(RE::SpellItem::SpellFlag::kCostOverride)) continue;
			float was = 0;
			for (auto* e : sp->effects) if (e) was += e->cost;
			sp->data.costOverride = std::clamp(static_cast<std::int32_t>(std::lround(was * 0.35f)), 40, 100);
			sp->data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);
			logger::info("classes: support relic {} costs {} (was {:.0f})", sp->GetName(), sp->data.costOverride, was);
		}
	}
	// (Noah's plan, Destruction) its support spells come as the altar's side-pick, for this class only
	bool RollSupport(Prize* a_out)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return false;
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
			FillReel(a_out, pool, sp, "Support Spell");
			a_out->give = [sp] { Learn(sp); Hud::Banner(std::string(sp->GetName()) + "  -  learned", 4.0f); logger::info("classes: support spell {}", sp->GetName()); };
			return true;
		}
		Learn(sp);
		Hud::Banner(std::string("Support spell learned: ") + sp->GetName(), 4.0f);
		logger::info("classes: support spell {}", sp->GetName());
		return true;
	}

	std::string BoltLine()
	{
		if (g_class.load() != coffer::kRanger) return {};
		std::string out;
		static constexpr const char* kName[3] = { "Ember", "Rime", "Storm" };
		auto* p = RE::PlayerCharacter::GetSingleton();
		for (int e = 0; e < 3; ++e) {
			auto* w = ElemCrossbow(e);
			if (!w || !p || p->GetItemCount(w) <= 0) continue;
			auto it = g_xbow.find(w->GetFormID());
			const int lv = it == g_xbow.end() ? 0 : it->second.first, kills = it == g_xbow.end() ? 0 : it->second.second;
			const int need = lv == 0 ? 25 : lv == 1 ? 60 : 120;
			out += (out.empty() ? "" : "; ") + fmt::format("{} Crossbow {} (quiver {}{})", kName[e], lv == 0 ? std::string("unforged") : "reforged " + std::string(lv, '|'), BoltCap(lv),
				lv >= 3 ? std::string(", fully reforged") : fmt::format(", {} of {} kills to the next", kills, need));
		}
		return out.empty() ? std::string("Crossbows: the Ember, Rime and Storm Crossbows wait at the Echo Altar") : out;
	}

	// (Noah's plan, the Spellsword) Imbue moves the held spell-weapon up a material - Steel, Orcish, Dwarven, Elven, Glass,
	// Ebony, Daedric - keeping its kind and, where that material has it, its element
	void ImbueWeapon()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* held = p ? p->GetEquippedObject(false) : nullptr;
		auto* w = held ? held->As<RE::TESObjectWEAP>() : nullptr;
		if (!w || !w->GetFile(0) || w->GetFile(0)->GetFilename() != "Spellsword.esp" || !dh) {
			Hud::Banner("Hold a spell-weapon as you Imbue and its steel rises too", 3.0f);  // (the consistency pass: it was silent)
			return;
		}
		static constexpr std::string_view kMat[] = { "Steel", "Orcish", "Dwarven", "Elven", "Glass", "Ebony", "Daedric" };
		const std::string name = w->GetName();
		int m = -1;
		for (int i = 0; i < 7; ++i) if (name.starts_with(kMat[i])) m = i;
		if (name.starts_with("Dwemer")) m = 2;
		if (m < 0 || m >= 6) {
			Hud::Banner(std::string(w->GetName()) + " has reached its finest steel", 3.0f);
			return;
		}
		if (name.find(' ') == std::string::npos) return;  // (the audit: a one-word name threw here)
		const std::string rest = name.substr(name.find(' '));                 // " Spellsword of Fire"
		const std::string kind = rest.substr(0, rest.find(" of "));           // " Spellsword"
		const std::string elem = rest.find(" of ") != std::string::npos ? rest.substr(rest.find(" of ")) : std::string();
		RE::TESObjectWEAP* best = nullptr;
		int bestScore = -1;
		for (int nm = m + 1; nm < 7 && !best; ++nm) {
			for (auto* c : dh->GetFormArray<RE::TESObjectWEAP>()) {
				if (!c || !c->GetFile(0) || c->GetFile(0)->GetFilename() != "Spellsword.esp" || !c->GetName()) continue;
				const std::string cn = c->GetName();
				if (!cn.starts_with(kMat[nm]) || c->GetWeaponType() != w->GetWeaponType()) continue;  // (the same kind of weapon only: one-handed stays one-handed)
				const int score = (cn.find(kind) != std::string::npos ? 2 : 0) + (!elem.empty() && cn.ends_with(elem) ? 1 : 0) + (c->GetWeaponType() == w->GetWeaponType() ? 2 : 0);
				if (score > bestScore) bestScore = score, best = c;
			}
		}
		if (!best) {
			Hud::Banner("There is no finer steel for " + name + " at the altar", 3.0f);
			return;
		}
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

	// (Noah's plan, the Summoner: Calling of the Conjurer's heroes and armies as late Legendaries) the altar's second row
	// binds a hero or an army - (Noah: "keep the 6 free cities gate instead of both") its only gate is the altar's six
	// freed cities (src/Shops.cpp), no Legendary family needed
	bool RollHero(Prize* a_out)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return false;
		constexpr int freed = 6;  // (the row opens at six freed altars: heroes and armies both answer)
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
			FillReel(a_out, pool, sp, "Legendary Pact", true);
			a_out->give = [sp] { Learn(sp); Forms::Play(gen::Sound::shout_learned); Hud::Banner(std::string(sp->GetName()) + "  -  a Legendary pact, learned", 5.0f); logger::info("classes: hero pact {}", sp->GetName()); };
			return true;
		}
		Learn(sp);
		Forms::Play(gen::Sound::shout_learned);
		Hud::Banner(std::string("A LEGENDARY PACT - ") + sp->GetName(), 5.0f);
		logger::info("classes: hero pact {}", sp->GetName());
		return true;
	}

	bool WonderOpen()
	{
		const int c = g_class.load();
		return c >= 0 && c < 4 && g_wStep[c] >= 1 && g_wStep[c] <= kWonderSteps;
	}
	// (Noah: "the sound effect that the grey beards use to call the dragon born should play when the Easter egg for the class
	// wonder weapon is completed ... They should be retrieved at the reliquary in high hrothgar") a path's secret won: the
	// Greybeards' "Dovahkiin!" rolls over Skyrim, and the wonder waits on its pedestal in the Reliquary
	void GreybeardsCall()
	{
		auto* am = RE::BSAudioManager::GetSingleton();
		for (const RE::FormID id : { 0x0C3CABu, 0x10F85Eu }) {  // Skyrim.esm: QSTGreybeardCall's and QSTGreybeardRumble's sounds (MQ104: the Greybeards summon you)
			auto* d = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(id);
			RE::BSSoundHandle h;
			if (d && am && am->GetSoundHandle(h, static_cast<RE::BSISoundDescriptor*>(d))) {
				h.SetVolume(1.0f);
				h.Play();
			}
		}
		if (auto* p = RE::PlayerCharacter::GetSingleton(); p && Settings::ScreenShake()) RE::ShakeCamera(0.5f, p->GetPosition(), 2.5f);
	}
	void EarnWonder(int a_cls)
	{
		if (a_cls < 0 || a_cls >= 4 || g_wStep[a_cls] >= kWonderTaken) return;
		g_wStep[a_cls] = kWonderWaiting;
		GreybeardsCall();
		Stats::Note(Stats::kWonder);
		Points::Add(3000);
		Tasks::Later(2.5f, [a_cls] {
			Hud::Lore("DOVAHKIIN! THE GREYBEARDS CALL",
				fmt::format("Your path's secret is won. The Greybeards' Voice rolls across Skyrim: the {} waits for you on its pedestal in the Reliquary of the Tongues, behind the sealed door in High Hrothgar's great hall.", kWonderName[a_cls]),
				14.0f);
		});
		logger::info("classes: wonder {} won - it waits in the Reliquary", kWonderName[a_cls]);
	}
	void DevWonder(int a_cls, int a_st)
	{
		if (a_cls < 0 || a_cls >= 4) return;
		if (a_st == kWonderWaiting) {
			g_wStep[a_cls] = 0;
			EarnWonder(a_cls);
		} else {
			g_wStep[a_cls] = static_cast<std::uint8_t>(std::clamp(a_st, 0, 5));
		}
		logger::info("dev: wonder {} state {}", kWonderName[a_cls], g_wStep[a_cls]);
	}
	int  WonderState(int a_cls) { return a_cls >= 0 && a_cls < 4 ? g_wStep[a_cls] : 0; }
	std::string_view WonderName(int a_cls) { return a_cls >= 0 && a_cls < 4 ? kWonderName[a_cls] : ""; }
	int  WonderMask()
	{
		int m = 0;
		for (int c = 0; c < 4; ++c) m |= g_wStep[c] >= kWonderTaken ? 1 << c : 0;
		return m;
	}
	bool TakeWonder(int a_cls)
	{
		if (a_cls < 0 || a_cls >= 4) return false;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (g_wStep[a_cls] == kWonderWaiting) {
			GrantWonder(a_cls);
			return true;
		}
		if (g_wStep[a_cls] == kWonderTaken && p) {  // (lost from the pack, or a new copy wanted: given again)
			const bool have = a_cls == coffer::kSummoner ? (WyrmSpell() && p->HasSpell(WyrmSpell())) : a_cls == coffer::kDestruction ? (TomeSpell() && p->HasSpell(TomeSpell()))
			                : a_cls == coffer::kRanger ? (Vortex() && p->GetItemCount(Vortex()) > 0) : (Slayer() && p->GetItemCount(Slayer()) > 0);
			if (!have) {
				GrantWonder(a_cls);
				return true;
			}
		}
		return false;
	}
	bool WonderEarned()
	{
		for (auto s : g_wStep) if (s >= kWonderTaken) return true;
		return false;
	}
	std::vector<std::string> Progress(int& a_cls)
	{
		std::scoped_lock l(g_lineLock);
		a_cls = g_progressCls;
		return g_progress;
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
		if (st == kWonderWaiting) return std::string(kWonderName[c]) + " waits on its pedestal in the Reliquary beneath High Hrothgar";
		if (st > kWonderSteps) return std::string(kWonderName[c]) + " is yours";
		if (st == 0) return fmt::format("{}: free {} more {} to wake your path's secret", kWonderName[c], std::max(0, kWonderOpensAt - FreedCount()), kWonderOpensAt - FreedCount() == 1 ? "city" : "cities");
		return std::string(kWonderName[c]) + ": " + Eggs::Line();
	}

	void NoteHit(RE::Actor* a_target, RE::FormID a_source, bool a_power)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !a_target || a_target == p) return;
		if (g_lastHit.size() > 400) g_lastHit.clear(), g_powerHit.clear();
		auto* src = RE::TESForm::LookupByID(a_source);
		// (Noah: "melee kills with a spell in hand don't count") the blade's own spell strikes a moment after the blade: it must
		// not take the blade's place as the last thing to hit (the blade's time is kept on its own too)
		const bool blade = src && src->As<RE::TESObjectWEAP>() && !src->As<RE::TESObjectWEAP>()->IsBow() && !src->As<RE::TESObjectWEAP>()->IsCrossbow() && !src->As<RE::TESObjectWEAP>()->IsStaff();
		if (blade) g_lastBlade[a_target->GetFormID()] = std::chrono::steady_clock::now();
		if (g_lastBlade.size() > 400) g_lastBlade.clear();
		g_lastHit[a_target->GetFormID()] = a_source;
		if (a_power) g_powerHit.insert(a_target->GetFormID()); else g_powerHit.erase(a_target->GetFormID());
		if (!Director::IsFoe(a_target) || a_target->IsDead()) return;
		if (!src) return;
		if (auto* w = src->As<RE::TESObjectWEAP>(); w && !w->IsBow() && !w->IsCrossbow() && !w->IsStaff()) Perks::OnMeleeHit();  // (Bladesong Mead)
		if (src == Vortex()) {
			// (its arrow found one of the dead: the hole opens on it - TickFast sees the arrow gone and lets it be)
			OpenVortex(a_target->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, true);
		} else if (src == Slayer() && g_bladeCool <= 0) {  // the spell in your other hand rides the blade
			const int left = BladeSpell();  // (two-handed: the last Spellsword spell you held)
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
		struct PendingThrall { RE::ActorHandle h; float wait; int level; };
		std::vector<PendingThrall>                        g_pendingThrall;  // struck by Reanimate: does it fall?
		std::vector<std::pair<RE::ActorHandle, float>>    g_thralls;        // the risen dead and the Riftblade's atronachs: seconds left
		std::mt19937                                      g_summonRng{ std::random_device{}() };
		std::unordered_set<RE::FormID>                    g_riftSummons;     // the Riftblade's atronachs (they hit by its tier)
		std::vector<RE::ActorHandle> g_summonMarks;  // (the Summoner's staff) the ones your summons are told to kill
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
			for (int n = 8; n >= 2; --n) {
				if (auto* perk = dh ? dh->LookupForm<RE::BGSPerk>(gen::kSummonCapPerks[n - 2], gen::kPlugin) : nullptr; perk && a_p->HasPerk(perk)) return n;
			}
			return 1;
		}
		// a body of the night's dead (or a Flame Atronach) stood up on your side for a while: a teammate, out of the Curse's
		// faction, in yours; when its time is up it crumbles back into the dark
		static auto* PlayerFaction() { static auto* f = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1); return f; }
		void EndThrall(RE::Actor* a)
		{
			if (!a) return;
			Gun::FxBlast("dark", a->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f });
			Console(a, "SetPlayerTeammate 0");
			a->Disable();
			a->SetDelete(true);
		}
		RE::Actor* RaiseAlly(RE::PlayerCharacter* p, RE::TESNPC* a_base, const RE::NiPoint3& a_at, float a_seconds, float a_health, int a_cap)
		{
			if (!p || !a_base || !p->Is3DLoaded() || !p->GetParentCell()) return nullptr;
			std::erase_if(g_thralls, [](const auto& e) { auto a = e.first.get(); return !a || a->IsDisabled(); });
			while (static_cast<int>(g_thralls.size()) >= a_cap && !g_thralls.empty()) {  // (the oldest goes)
				EndThrall(g_thralls.front().first.get().get());
				g_thralls.erase(g_thralls.begin());
			}
			auto ref = p->PlaceObjectAtMe(a_base, false);
			auto* a = ref ? ref->As<RE::Actor>() : nullptr;
			if (!a) return nullptr;
			a->SetPosition(a_at, true);
			if (auto* fac = Director::UndeadFactionForm()) a->RemoveFromFaction(fac);
			if (auto* pf = PlayerFaction()) a->AddToFaction(pf, 0);
			Console(a, "SetPlayerTeammate 1");
			if (a_health > 0) {
				auto* av = a->AsActorValueOwner();
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, a_health - av->GetPermanentActorValue(RE::ActorValue::kHealth));
			}
			g_thralls.push_back({ a->GetHandle(), a_seconds });
			return a;
		}
		void TickThralls(RE::PlayerCharacter* p, float a_delta)
		{
			for (auto& pt : g_pendingThrall) {
				pt.wait -= a_delta;
				auto a = pt.h.get();
				if (!a) { pt.wait = 0; continue; }
				if (!a->IsDead()) continue;
				pt.wait = 0;
				auto* base = a->GetActorBase();
				const float secs = 20.0f + 5.0f * static_cast<float>(pt.level);
				const float hp = Director::RoundHealth() * gen::kRounds.hpScale * (1.5f + 1.0f * static_cast<float>(pt.level));
				const RE::NiPoint3 at = a->GetPosition();
				if (auto* t = RaiseAlly(p, base, at, secs, hp, pt.level >= 2 ? 3 : 2)) {
					Gun::FxBlast("dark", at + RE::NiPoint3{ 0, 0, 40.0f });
					a->Disable();  // (the body it rose from is gone)
					static int told = 0;
					if (told++ < 10) logger::info("classes: Reanimate - {} rises as your thrall for {:.0f} s", t->GetName(), secs);
				}
			}
			std::erase_if(g_pendingThrall, [](const PendingThrall& pt) { return pt.wait <= 0; });
			for (auto& [h, t] : g_thralls) {
				auto a = h.get();
				if (!a || a->IsDisabled()) { t = 0; continue; }
				if ((t -= a_delta) <= 0 || a->IsDead()) { EndThrall(a.get()); t = 0; }
			}
			std::erase_if(g_thralls, [](const auto& e) { return e.second <= 0; });
		}
		void TickSummons(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			TickThralls(p, a_delta);
			// (the staff is gone: its bolt is no longer dressed each frame)
			for (auto& [h, t] : g_ownSummons) {
				auto a = h.get();
				if (!a || a->IsDisabled()) { t = 0; continue; }
				if ((t -= a_delta) <= 0 || a->IsDead()) {
					Gun::FxBlast("dark", a->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f });
					Console(a.get(), "SetPlayerTeammate 0");  // (no teammate the save remembers and can't find)
					a->Disable();
					a->SetDelete(true);
					t = 0;
				}
			}
			std::erase_if(g_ownSummons, [](const auto& e) { return e.second <= 0; });
			// (Noah: "they aren't attacking the Curse's enemies") every summon of yours not already fighting goes for the
			// nearest of the night's dead within reach
			if (static float look = 0; (look += a_delta) > 1.0f) {
				look = 0;
				std::vector<RE::Actor*> mine;
				for (auto& [h, t] : g_ownSummons) if (auto a = h.get()) mine.push_back(a.get());
				for (auto& [h, t] : g_thralls) if (auto a = h.get(); a && !a->IsDead()) mine.push_back(a.get());  // (the risen and the atronachs fight as summons)
				for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
					if (auto a = h.get(); a && a.get() != p && !a->IsDead() && a->IsCommandedActor() && a->GetCommandingActor().get() == p) mine.push_back(a.get());
				}
				static std::unordered_set<RE::FormID> mighty;  // (Bonebinder's Brew) each summon made stronger once
				if (mighty.size() > 200) mighty.clear();
				static int diag = 0;
				if (!mine.empty() && diag++ % 5 == 0 && diag < 200) {  // (Noah: "my summons still weren't attacking") what each is doing
					for (auto* s : mine) {
						auto t = s->GetActorRuntimeData().currentCombatTarget.get();
						logger::info("summons: {} {:08X} - in combat {}, target {} {}", s->GetName(), s->GetFormID(), s->IsInCombat(), t ? t->GetName() : "none",
							t ? fmt::format("({:.0f} away, foe {})", t->GetPosition().GetDistance(s->GetPosition()), Director::IsFoe(t.get())) : std::string());
					}
				}
				static auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1);
				std::vector<RE::NiPoint3> friends;  // (Noah, co-op: "your summons also guard your partner")
				if (Net::InLobby()) {
					for (auto& fh : RE::ProcessLists::GetSingleton()->highActorHandles) {
						if (auto f = fh.get(); f && !f->IsDead() && Party::IsFriend(f.get()) && f->GetPosition().GetDistance(p->GetPosition()) < 4000.0f) friends.push_back(f->GetPosition());
					}
				}
				for (auto* s : mine) {
					if (s->IsDead() || s->IsDisabled()) continue;
					// (Noah: "my summons start to kill me if I am damaging them") a stray blow of yours never turns one on you
					// or on a friend: it drops that fight at once
					if (auto t = s->GetActorRuntimeData().currentCombatTarget.get(); t && Director::PlayerSide(t.get(), p)) {
						s->StopCombat();
						static int told = 0;
						if (told++ < 10) logger::info("summons: {} turned on {} - called off", s->GetName(), t->GetName());
					}
					// (Noah: "my summons still weren't attacking the Curse's enemies, and the enemies weren't attacking them") on
					// the player's side by faction too: the night's dead (enemies of the player's faction) and it are foes
					if (playerFaction && !s->IsInFaction(playerFaction)) s->AddToFaction(playerFaction, 0);
					// (Noah: "the Spriggan summon made my screen super bright colors ... it kills all the enemies in a long area")
					// a summon keeps its bolts and blows: its auras, cloaks, swarms and mind spells (calm, frenzy, fear,
					// paralysis) and anything that tints the screen are taken from it once, when first seen
					static std::unordered_set<RE::FormID> tamed;
					if (tamed.size() > 400) tamed.clear();
					if (tamed.insert(s->GetFormID()).second) {
						auto wild = [](RE::SpellItem* sp) {
							if (!sp) return false;
							for (auto* e : sp->effects) {
								auto* m = e ? e->baseEffect : nullptr;
								if (!m) continue;
								using A = RE::EffectArchetypes::ArchetypeID;
								const auto k = m->GetArchetype();
								if (m->data.imageSpaceMod || k == A::kCloak || k == A::kCalm || k == A::kFrenzy || k == A::kDemoralize || k == A::kParalysis ||
									k == A::kInvisibility || e->GetArea() > 20 || (sp->GetSpellType() == RE::MagicSystem::SpellType::kAbility && m->IsHostile()))
									return true;
							}
							return false;
						};
						std::vector<RE::SpellItem*> drop;
						if (auto* base = s->GetActorBase(); base && base->actorEffects && base->actorEffects->spells) {
							for (std::uint32_t i = 0; i < base->actorEffects->numSpells; ++i) if (wild(base->actorEffects->spells[i])) drop.push_back(base->actorEffects->spells[i]);
						}
						for (auto* sp : s->GetActorRuntimeData().addedSpells) if (wild(sp)) drop.push_back(sp);
						for (auto* sp : drop) {
							s->RemoveSpell(sp);
							if (auto* mt = s->AsMagicTarget()) { RE::BSPointerHandle<RE::Actor> none; mt->DispelEffect(sp, none); }
						}
						if (!drop.empty()) logger::info("summons: {} - {} wild spell(s) taken (auras, swarms, mind spells)", s->GetName(), drop.size());
						// (Noah: "the Dremora and the Dwarven Spider are particularly strong") Skyrim's own weapon damage is
						// turned down as the dead's is: what a summon does to them is the share counted on its hit (src/Events.cpp)
						auto* sav0 = s->AsActorValueOwner();
						sav0->SetActorValue(RE::ActorValue::kAttackDamageMult, 1.0f);  // (Noah: "my summons don't do anything other than sponge damage" - their own blows at full against Skyrim's foes; the night's dead count the share)
						logger::info("summons: {} {:08X} ({:.0f} health) joined", s->GetName(), s->GetFormID(), sav0->GetPermanentActorValue(RE::ActorValue::kHealth));
					}
					// (Noah: "summons stay within 20 meters of the player at all times - they wander off and fight") past 20 m it
					// drops its fight and comes back to you; past 34 m it is brought to your side
					{
						const float pd = s->GetPosition().GetDistance(p->GetPosition());
						if (pd > 2400.0f || (pd > 1400.0f && !s->Is3DLoaded())) {
							if (!s->IsAIEnabled()) continue;  // (already on its way)
							const float yaw = p->GetAngleZ() + 2.4f;
							s->StopCombat();
							// (the stability audit: moved with its AI running - the pathing crash) asleep, set down, woken
							s->EnableAI(false);
							const auto to = p->GetPosition() + RE::NiPoint3{ std::sin(yaw) * 180.0f, std::cos(yaw) * 180.0f, 0.0f };
							Tasks::Later(0.05f, [h = s->GetHandle(), to] {
								auto a = h.get();
								if (!a || a->IsDead() || a->IsDeleted()) return;
								if (a->IsDisabled()) { a->EnableAI(true); return; }
								a->SetPosition(to, true);
								Tasks::Later(0.1f, [h] { if (auto b = h.get(); b && !b->IsDead() && !b->IsDisabled()) b->EnableAI(true); });
							});
							continue;
						}
						if (pd > 1400.0f) {
							s->StopCombat();
							s->EvaluatePackage(true, false);
							continue;
						}
						if (auto t = s->GetActorRuntimeData().currentCombatTarget.get(); t && t->GetPosition().GetDistance(p->GetPosition()) > 1500.0f &&
							std::none_of(friends.begin(), friends.end(), [&](const RE::NiPoint3& f) { return t->GetPosition().GetDistance(f) <= 1500.0f; })) {
							s->StopCombat();  // (its quarry ran past your 20 m: let it go)
						}
					}
					if (Perks::SummonMight() && mighty.insert(s->GetFormID()).second) {
						auto*       sav = s->AsActorValueOwner();
						const float more = gen::kPerks[gen::kPerk_bonebinder].value - 1.0f;
						sav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, sav->GetPermanentActorValue(RE::ActorValue::kHealth) * more);
						sav->RestoreActorValue(RE::ActorValue::kHealth, 1.0e6f);
						(void)more;  // (its might shows in the share it lands: src/Events.cpp)
					}
					auto cur = s->GetActorRuntimeData().currentCombatTarget.get();
					// (Noah: "a staff that when attacks hit a target the summons focus that enemy until they're dead or a
					// different enemy is targeted") the marked one first
					RE::NiPointer<RE::Actor> m;
					{
						float md = 6000.0f;
						for (auto& mh : g_summonMarks) {
							auto c = mh.get();
							if (!c || c->IsDead() || c->IsDisabled() || !c->Is3DLoaded() || Director::Rising(c.get()) || c->GetPosition().GetDistance(p->GetPosition()) > 1450.0f) continue;
							if (c.get() == cur.get()) { m = c; break; }  // (already on one of them: it stays on it)
							if (const float d = c->GetPosition().GetDistance(s->GetPosition()); d < md) md = d, m = c;
						}
					}
					if (m) {
						if (cur.get() != m.get() && SummonMayEngage(s, m.get())) {
							if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
								const auto hd = vm->GetObjectHandlePolicy()->GetHandleForObject(s->GetFormType(), s);
								RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
								vm->DispatchMethodCall(hd, "Actor", "StartCombat", RE::MakeFunctionArguments(static_cast<RE::Actor*>(m.get())), none);
							}
						}
						continue;
					}
					if (cur && !cur->IsDead() && Director::IsFoe(cur.get())) continue;
					RE::Actor* best = nullptr;
					float bestD = 3000.0f;
					for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
						auto z = h.get();
						if (!z || z->IsDead() || z->IsDisabled() || !z->Is3DLoaded() || !Director::IsFoe(z.get()) || Director::Rising(z.get())) continue;  // (not one still in the ground)
						if (z->GetPosition().GetDistance(p->GetPosition()) > 1400.0f &&
							std::none_of(friends.begin(), friends.end(), [&](const RE::NiPoint3& f) { return z->GetPosition().GetDistance(f) <= 1400.0f; })) continue;  // (only what comes within 20 m of you - or, co-op, of a friend: the Summoner's aura)
						if (z->IsCommandedActor() && z->GetCommandingActor().get() == p) continue;
						if (const float d = z->GetPosition().GetDistance(s->GetPosition()); d < bestD) bestD = d, best = z.get();
					}
					if (best && SummonMayEngage(s, best)) {
						if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
							const auto hd = vm->GetObjectHandlePolicy()->GetHandleForObject(s->GetFormType(), s);
							RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
							vm->DispatchMethodCall(hd, "Actor", "StartCombat", RE::MakeFunctionArguments(static_cast<RE::Actor*>(best)), none);
						}
					}
				}
			}
			for (auto& ps : g_pendingSummon) {
				if ((ps.wait -= a_delta) > 0) continue;
				const int now = CommandedNow(p) + static_cast<int>(g_ownSummons.size());
				const int cap = SummonCap(p);
				if (now > ps.before) {
					logger::info("classes: {} came as it should ({} summons up)", ps.name, now);
					continue;
				}
				// (Noah: "the Dremora is particularly strong" - the log: one cast, the Dremora came, and then a second was called
				// up beside it, again and again) at the cap Skyrim swaps its oldest for the new one - the count stays the same,
				// which is not a failed call
				if (now >= cap || ps.before >= cap) {
					logger::info("classes: {} cast at the cap ({}) - Skyrim's own swap, nothing added", ps.name, cap);
					continue;
				}
				while (!g_ownSummons.empty() && static_cast<int>(g_ownSummons.size()) >= cap) {  // (the oldest goes, as Skyrim's own do)
					if (auto a = g_ownSummons.front().first.get()) Console(a.get(), "SetPlayerTeammate 0"), a->Disable(), a->SetDelete(true);
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
				g_ownSummons.push_back({ actor->GetHandle(), std::min(ps.seconds, 90.0f) });  // (a called-up stand-in lasts as a summon spell does, never an hour)
				Console(actor, "SetPlayerTeammate 1");
				logger::info("classes: {} did not come - called up here for {:.0f} s", ps.name, std::min(ps.seconds, 90.0f));
			}
			std::erase_if(g_pendingSummon, [](const PendingSummon& ps) { return ps.wait <= 0; });
		}
		// your summons, all of them, gone (a class change away from the Summoner)
		void DismissSummons()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			int n = 0;
			for (auto& [h, t] : g_ownSummons) {
				if (auto a = h.get()) {
					Gun::FxBlast("dark", a->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f });
					Console(a.get(), "SetPlayerTeammate 0");
					a->Disable(), a->SetDelete(true), ++n;
				}
			}
			g_ownSummons.clear();
			g_pendingSummon.clear();
			for (auto& [h, t] : g_thralls) EndThrall(h.get().get());
			g_thralls.clear(), g_pendingThrall.clear();
			if (auto* mt = p->AsMagicTarget()) {  // (Skyrim's own summon spells on you: ended)
				std::vector<RE::ActiveEffect*> ends;
				if (auto* list = mt->GetActiveEffectList()) {
					for (auto* ae : *list) {
						if (ae && ae->effect && ae->effect->baseEffect && ae->effect->baseEffect->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kSummonCreature) ends.push_back(ae);
					}
				}
				for (auto* ae : ends) ae->Dispel(true);
			}
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {  // (any left over: banished)
				if (auto a = h.get(); a && !a->IsDead() && a->IsCommandedActor() && a->GetCommandingActor().get() == p) {
					Gun::FxBlast("dark", a->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f });
					a->KillImmediate(), ++n;
				}
			}
			g_summonMarks.clear();
			logger::info("classes: summons dismissed ({} of ours, and Skyrim's own)", n);
		}
	}
	RE::TESObjectARMO* Underwear()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		return dh ? dh->LookupForm<RE::TESObjectARMO>(0x801, "Underwear.esp") : nullptr;
	}
	void DressUnder(bool a_anySex)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* uw = Underwear();
		auto* em = RE::ActorEquipManager::GetSingleton();
		if (!p || !uw || !em || p->IsDead() || !p->Is3DLoaded()) return;
		if (!a_anySex && !(p->GetActorBase() && p->GetActorBase()->IsFemale())) return;
		if (p->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) return;
		if (p->GetItemCount(uw) <= 0) p->AddObjectToContainer(uw, nullptr, 1, nullptr);
		em->EquipObject(p, uw, nullptr, 1, nullptr, false, false, false, true);
	}
	void ConjurerStaffHit(RE::Actor* a_target, RE::FormID a_source)
	{
		// (Noah: the staff's work is Corrosion's now) one of Corrosion's bolts struck: its rung sets the burst
		int level = -1;
		for (int k = 0; k < static_cast<int>(coffer::kSpells.size()); ++k) {
			if (std::string_view(coffer::kSpells[k].ladderLabel) != "Reanimate" || coffer::kSpells[k].cls != coffer::kSummoner) continue;
			if (auto* sp = Arcana::Spell(k); sp && sp->GetFormID() == a_source) level = coffer::kSpells[k].rung - 1;
		}
		if (level < 0 || !a_target) return;
		static std::chrono::steady_clock::time_point last{};
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(200)) return;  // (one bolt, one burst)
		last = now;
		// (Noah: "Reanimate - a dark bolt; any undead it kills rises for 20 s as your thrall") the bolt strikes what it hit
		// (two fifths of one of the dead's health at first, on the one upgrade curve each rung), marks it for your summons,
		// and if that blow kills one of the night's dead it rises again on your side (TickThralls)
		const int   tier = level >= 2 ? 3 : level;
		const float share = 0.4f * UpgradeMult(tier);
		const auto  at = a_target->GetPosition();
		const bool  undead = Director::IsRoundUndead(a_target) && !Director::IsBoss(a_target);
		Gun::AreaHurt(at, 70.0f, share, "");
		if (Director::IsFoe(a_target) && !a_target->IsDead()) g_summonMarks = { a_target->GetHandle() };
		if (undead) g_pendingThrall.push_back({ a_target->GetHandle(), 0.6f, level });
	}
	// (Noah: "an enchanted dagger with a special ability" - Rift Stab) every fifth strike of the Riftblade on one of the night's
	// foes tears a small rift there: a Flame Atronach steps out and fights for you for 10 s (two at most)
	void RiftHit(RE::Actor* a_target)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !a_target || !Director::IsFoe(a_target)) return;
		static int hits = 0, bigHits = 0;
		static std::chrono::steady_clock::time_point last{};
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(120)) return;  // (one swing, one count)
		last = now;
		// (Noah: "the Riftblade hits mark that enemy, and your summons go after it")
		if (!a_target->IsDead()) g_summonMarks = { a_target->GetHandle() };
		// (Noah: "the abilities should change per level to a stronger form of the base") always the Flame Atronach, stronger
		// each tier: Base every 5th hit one for 10 s; I every 4th, 15 s, larger and tougher; II every 3rd, the potent
		// atronach, 20 s, two at once; III (Legendary) every 3rd, a great potent atronach, 25 s, three at once, and the
		// rift it steps out of bursts into flame
		const int t = std::clamp(Kit::RiftTier(), 0, 3);
		static constexpr int   kEvery[4] = { 5, 4, 3, 3 }, kCap[4] = { 1, 2, 2, 3 };
		static constexpr float kSecs[4] = { 10.0f, 15.0f, 20.0f, 25.0f }, kScale[4] = { 1.0f, 1.1f, 1.2f, 1.35f }, kHp[4] = { 1.0f, 1.4f, 1.8f, 2.4f };
		if (++hits < kEvery[t]) return;
		hits = 0;
		(void)bigHits;
		const float yaw = std::atan2(p->GetPosition().x - a_target->GetPosition().x, p->GetPosition().y - a_target->GetPosition().y);
		const RE::NiPoint3 at = a_target->GetPosition() + RE::NiPoint3{ std::sin(yaw) * 110.0f, std::cos(yaw) * 110.0f, 0.0f };
		auto* base = RE::TESForm::LookupByID<RE::TESNPC>(t >= 2 ? 0x0004E940 : 0x000204C0);  // (the Flame Atronach; from Tier II the potent one)
		if (!base) base = RE::TESForm::LookupByID<RE::TESNPC>(0x000204C0);
		const float secs = kSecs[t], scale = kScale[t], hpMul = kHp[t];
		const int   cap = kCap[t], tier = t;
		Tasks::Add([base, at, secs, cap, scale, hpMul, tier] {  // (out of the hit event: set down on the main thread's own turn)
			auto* pl = RE::PlayerCharacter::GetSingleton();
			Gun::FxBlast(tier >= 2 ? "firestorm" : "fireball", at + RE::NiPoint3{ 0, 0, 60.0f });
			if (tier >= 3) Gun::AreaHurt(at, 260.0f, 0.6f, "");  // (the Legendary rift bursts into flame)
			if (auto* a = RaiseAlly(pl, base, at, secs, 0.0f, cap)) {
				g_riftSummons.insert(a->GetFormID());
				if (g_riftSummons.size() > 200) g_riftSummons.clear();
				a->SetScale(scale);
				auto* av = a->AsActorValueOwner();
				const float top = av->GetPermanentActorValue(RE::ActorValue::kHealth);
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, top * (hpMul - 1.0f));
				av->RestoreActorValue(RE::ActorValue::kHealth, 1.0e6f);
				static int told = 0;
				if (told++ < 10) logger::info("classes: Riftblade (tier {}) - a rift opens, {} steps out for {:.0f} s", tier, a->GetName(), secs);
			}
		});
	}
	bool IsRiftSummon(RE::Actor* a_actor) { return a_actor && g_riftSummons.contains(a_actor->GetFormID()); }
	bool IsRiftblade(RE::FormID a_source)
	{
		static auto* w = RE::TESDataHandler::GetSingleton()->LookupForm(gen::kRiftblade, gen::kPlugin);
		return w && w->GetFormID() == a_source;
	}
	void MarkForSummons(RE::Actor* a_target)
	{
		if (!a_target || a_target->IsDead()) return;
		g_summonMarks = { a_target->GetHandle() };
	}
	bool IsOwnSummon(RE::Actor* a_actor)
	{
		return a_actor && (std::any_of(g_ownSummons.begin(), g_ownSummons.end(), [&](const auto& e) { return e.first.get().get() == a_actor; }) ||
		                   std::any_of(g_thralls.begin(), g_thralls.end(), [&](const auto& e) { return e.first.get().get() == a_actor; }) ||
		                   IsRiftSummon(a_actor));  // (the Riftblade's atronachs are your summons too: the Pact of the Host counts their kills)
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
			int before = CommandedNow(p) + static_cast<int>(g_ownSummons.size());
			// (Noah: "it only worked one time") at the cap, one called up here makes room itself - Skyrim never swaps ours
			while (before >= SummonCap(p) && !g_ownSummons.empty()) {
				if (auto a = g_ownSummons.front().first.get()) {
					Gun::FxBlast("dark", a->GetPosition() + RE::NiPoint3{ 0, 0, 50.0f });
					Console(a.get(), "SetPlayerTeammate 0");
					a->Disable(), a->SetDelete(true);
				}
				g_ownSummons.erase(g_ownSummons.begin());
				--before;
			}
			if (before >= SummonCap(p)) {
				logger::info("classes: {} cast at the cap ({}) - Skyrim swaps the oldest", sp->GetName(), before);
				return;
			}
			if (std::any_of(g_pendingSummon.begin(), g_pendingSummon.end(), [&](const PendingSummon& q) { return q.what == what; })) return;  // (one cast seen twice - both hands, or the cast and its release: one summon)
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
		// (the class audit: nine casts on the same bodies within a quarter second took one share between them - the wonder hit
		// softer than a plain bolt) its six fires each burst on their foe for a real share, every 8 s; only the six element
		// ladders (Fire, Frost, Shock, Sun, Blood, Runes), as its name says
		g_tomeCool = 8.0f;
		int k = 0;
		const bool legend = AfterSovngarde();
		const float pf = 0.7f + 0.3f * Arcana::Power();
		for (auto* f : foes) {
			if (f && !f->IsDead()) Gun::AreaHurt(f->GetPosition(), 150.0f, 0.6f * UpgradeMult(legend ? 6 : 4) * pf, "");
		}
		for (const auto& s : coffer::kSpells) {
			if (s.cls != coffer::kDestruction || s.ladder > 5) continue;
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
		auto* bow = Vortex();
		auto* ar = VortexArrow();
		int n = TopUpBolts(20);  // (crossbow mastery's elemental bolts)
		if (!p || !bow || !ar || p->GetItemCount(bow) <= 0) return n;
		const int have = p->GetItemCount(ar);
		if (have >= 60) return n;
		p->AddObjectToContainer(ar, nullptr, 60 - have, nullptr);
		return n + 1;
	}

	// every frame: cooldowns, and where the Subsuming Vortex's arrows come down
	void TickFast(float a_delta)
	{
		TickSummons(a_delta);
		g_tomeCool = std::max(0.0f, g_tomeCool - a_delta);
		g_bladeCool = std::max(0.0f, g_bladeCool - a_delta);
		g_vortexCool = std::max(0.0f, g_vortexCool - a_delta);
		auto* bow = Vortex();
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* mgr = RE::Projectile::Manager::GetSingleton();
		if (!bow || !p || !mgr || p->GetItemCount(bow) <= 0) {
			g_vortexArrows.clear();
			return;
		}
		// each arrow of ours in flight: where it is; one that stops, or is gone, came down there - a hole opens
		std::unordered_map<RE::FormID, std::pair<RE::NiPoint3, float>> now;
		{
			RE::BSSpinLockGuard lock(mgr->projectileLock);
			for (auto* list : { &mgr->limited, &mgr->unlimited }) {
				for (auto& h : *list) {
					auto pr = h.get();
					if (!pr || pr->IsDeleted()) continue;
					auto& rd = pr->GetProjectileRuntimeData();
					if (rd.weaponSource == bow && rd.shooter.get().get() == p) now[pr->GetFormID()] = { pr->GetPosition(), rd.linearVelocity.Length() };
				}
			}
		}
		for (auto& [id, a] : g_vortexArrows) {
			auto it = now.find(id);
			const bool gone = it == now.end();
			if (!a.done && (gone || it->second.second < 60.0f)) {
				a.done = true;
				if (!a.hit) OpenVortex(gone ? a.last : it->second.first, false);
			}
			if (!gone) a.last = it->second.first;
		}
		std::erase_if(g_vortexArrows, [&](const auto& e) { return !now.contains(e.first); });
		for (auto& [id, pv] : now) {
			if (!g_vortexArrows.contains(id)) g_vortexArrows[id] = { pv.first, false, false };
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
			else if (code == 0x0E || code == 0x01) g_open = false;  // Backspace / Esc: back out (the first time too - Noah)
			return true;
		}
		if (a_b->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
			if (code == 0x0004) g_sel = (sel + 3) % 4;       // D-pad left
			else if (code == 0x0008) g_sel = (sel + 1) % 4;  // D-pad right
			else if (code == 0x1000) Choose(sel);           // A
			else if (code == 0x2000) g_open = false;  // B
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
		v.push_back(0xA3);  // the altar bows' kills: [n] then n x u16
		v.push_back(static_cast<std::uint8_t>(g_bowMastery.size()));
		for (auto m : g_bowMastery) v.push_back(static_cast<std::uint8_t>(m & 0xFF)), v.push_back(static_cast<std::uint8_t>(m >> 8));
		v.push_back(0xA4);  // the Conjurer's Staff's reforges
		v.push_back(static_cast<std::uint8_t>(g_staffLevel));
		v.push_back(0xA5);  // each class's chest, slots and worn pieces: per class [chest u32][3 x (gun i16, clip i16, reserve i16, level u8)][n u8][n x u32]
		auto u32 = [&](std::uint32_t x) { for (int b = 0; b < 4; ++b) v.push_back(static_cast<std::uint8_t>(x >> (8 * b))); };
		auto i16 = [&](int x) { const auto u = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(x, -32768, 32767))); v.push_back(static_cast<std::uint8_t>(u & 0xFF)), v.push_back(static_cast<std::uint8_t>(u >> 8)); };
		for (int c = 0; c < 4; ++c) {
			u32(g_chest[c]);
			for (auto& sl : g_slotStash[c]) i16(sl.gun), i16(sl.clip), i16(sl.reserve), v.push_back(static_cast<std::uint8_t>(sl.level));
			const auto n = std::min<std::size_t>(g_wornStash[c].size(), 255);
			v.push_back(static_cast<std::uint8_t>(n));
			for (std::size_t i = 0; i < n; ++i) u32(g_wornStash[c][i]);
		}
		v.push_back(0xA7);  // the guided first night's cards shown
		v.push_back(g_guide);
		v.push_back(0xA6);  // each class's blessings: per class [n u8][n x stacks u8]
		for (int c = 0; c < 4; ++c) {
			const auto n = std::min<std::size_t>(g_perkStash[c].size(), 255);
			v.push_back(static_cast<std::uint8_t>(n));
			for (std::size_t i = 0; i < n; ++i) v.push_back(g_perkStash[c][i]);
		}
		v.push_back(0xA9);  // the crossbows' reforges: [n u8][n x (u32 form, u8 level, u16 kills)]
		{
			const auto n = std::min<std::size_t>(g_xbow.size(), 255);
			v.push_back(static_cast<std::uint8_t>(n));
			std::size_t k = 0;
			for (auto& [id, x] : g_xbow) {
				if (k++ >= n) break;
				u32(id), v.push_back(x.first), v.push_back(static_cast<std::uint8_t>(x.second & 0xFF)), v.push_back(static_cast<std::uint8_t>(x.second >> 8));
			}
		}
		v.push_back(0xA8);  // each class's other altar spells: per class [n u8][n x u32]
		for (int c = 0; c < 4; ++c) {
			const auto n = std::min<std::size_t>(g_extra[c].size(), 255);
			v.push_back(static_cast<std::uint8_t>(n));
			for (std::size_t i = 0; i < n; ++i) u32(g_extra[c][i]);
		}
		v.push_back(0xAA);  // each class's other spells: per class [n u8][n x u32]
		for (int c = 0; c < 4; ++c) {
			const auto n = std::min<std::size_t>(g_spellStash[c].size(), 255);
			v.push_back(static_cast<std::uint8_t>(n));
			for (std::size_t i = 0; i < n; ++i) u32(g_spellStash[c][i]);
		}
		v.push_back(0xAB);  // each class's things held in the save (no chest): per class [n u16][n x (u32 form, i32 count)]
		for (int c = 0; c < 4; ++c) {
			const auto n = std::min<std::size_t>(g_held[c].size(), 65535);
			v.push_back(static_cast<std::uint8_t>(n & 0xFF)), v.push_back(static_cast<std::uint8_t>(n >> 8));
			for (std::size_t i = 0; i < n; ++i) u32(g_held[c][i].first), u32(static_cast<std::uint32_t>(g_held[c][i].second));
		}
		v.push_back(0xAC);  // the Belt Lantern given
		v.push_back(g_lanternGiven ? 1 : 0);
		return v;
	}
	void SetState(const std::vector<std::uint8_t>& a_v)
	{
		g_class = -1, g_kit.fill(false), g_staffLevel = 0;
		g_wStep.fill(0), g_wKills.fill(0), g_wTowns.fill(0), g_lastHit.clear(), g_boltKills = 0;
		for (auto& s : g_stash) s.clear();
		// (the audit: a new game kept the last game's chests, stashes and guide - its first class switch moved your gear
		// into an unrelated reference) all of it cleared before anything is read
		g_chest.fill(0), g_slotStash = {}, g_guide = 0, g_bowMastery.fill(0);
		for (auto& w : g_wornStash) w.clear();
		for (auto& ps : g_perkStash) ps.clear();
		for (auto& e : g_extra) e.clear();
		for (auto& e : g_spellStash) e.clear();
		for (auto& e : g_held) e.clear();
		g_lanternGiven = false;
		g_xbow.clear();
		if (a_v.empty()) return;
		g_class = static_cast<int>(a_v[0]) - 1;
		if (g_class < -1 || g_class > 3) g_class = -1;  // (the audit: a corrupt co-save indexed the four-class arrays)
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
		g_bowMastery.fill(0);
		if (k + 1 < a_v.size() && a_v[k] == 0xA3) {
			const int n = a_v[k + 1];
			k += 2;
			for (int b = 0; b < n && k + 2 <= a_v.size(); ++b, k += 2) {
				if (b < static_cast<int>(g_bowMastery.size())) g_bowMastery[b] = static_cast<std::uint16_t>(a_v[k] | (a_v[k + 1] << 8));
			}
		}
		g_staffLevel = 0;
		if (k + 1 < a_v.size() && a_v[k] == 0xA4) g_staffLevel = std::min<int>(a_v[k + 1], gen::kForgeMax), k += 2;
		g_chest.fill(0);
		for (auto& c : g_slotStash) c.fill({});
		for (auto& w : g_wornStash) w.clear();
		if (k < a_v.size() && a_v[k] == 0xA5) {
			++k;
			auto u32 = [&](std::uint32_t& x) { if (k + 4 > a_v.size()) return false; x = a_v[k] | (a_v[k + 1] << 8) | (a_v[k + 2] << 16) | (static_cast<std::uint32_t>(a_v[k + 3]) << 24); k += 4; return true; };
			auto i16 = [&](int& x) { if (k + 2 > a_v.size()) return false; x = static_cast<std::int16_t>(a_v[k] | (a_v[k + 1] << 8)); k += 2; return true; };
			for (int c = 0; c < 4; ++c) {
				std::uint32_t ch = 0;
				if (!u32(ch)) break;
				g_chest[c] = ResolveId(ch);
				bool ok = true;
				for (auto& sl : g_slotStash[c]) {
					ok = i16(sl.gun) && i16(sl.clip) && i16(sl.reserve) && k < a_v.size();
					if (!ok) break;
					sl.level = a_v[k++];
				}
				if (!ok || k >= a_v.size()) break;
				const int n = a_v[k++];
				for (int i = 0; i < n; ++i) {
					std::uint32_t id = 0;
					if (!u32(id)) break;
					if (const auto r = ResolveId(id)) g_wornStash[c].push_back(r);
				}
			}
		}
		for (auto& ps : g_perkStash) ps.clear();
		g_guide = 0;
		if (k + 1 < a_v.size() && a_v[k] == 0xA7) g_guide = a_v[k + 1], k += 2;
		if (k < a_v.size() && a_v[k] == 0xA6) {
			++k;
			for (int c = 0; c < 4 && k < a_v.size(); ++c) {
				const int n = a_v[k++];
				for (int i = 0; i < n && k < a_v.size(); ++i) g_perkStash[c].push_back(a_v[k++]);
			}
		}
		if (k < a_v.size() && a_v[k] == 0xA9) {
			++k;
			const int n = k < a_v.size() ? a_v[k++] : 0;
			for (int i = 0; i < n && k + 7 <= a_v.size(); ++i, k += 7) {
				const RE::FormID id = a_v[k] | (a_v[k + 1] << 8) | (a_v[k + 2] << 16) | (static_cast<std::uint32_t>(a_v[k + 3]) << 24);
				g_xbow[id] = { a_v[k + 4], static_cast<std::uint16_t>(a_v[k + 5] | (a_v[k + 6] << 8)) };
			}
		}
		if (k < a_v.size() && a_v[k] == 0xA8) {
			++k;
			for (int c = 0; c < 4 && k < a_v.size(); ++c) {
				const int n = a_v[k++];
				for (int i = 0; i < n && k + 4 <= a_v.size(); ++i, k += 4) g_extra[c].push_back(static_cast<RE::FormID>(a_v[k] | (a_v[k + 1] << 8) | (a_v[k + 2] << 16) | (static_cast<std::uint32_t>(a_v[k + 3]) << 24)));
			}
		}
		auto rd32 = [&](std::size_t at) { return static_cast<std::uint32_t>(a_v[at] | (a_v[at + 1] << 8) | (a_v[at + 2] << 16) | (static_cast<std::uint32_t>(a_v[at + 3]) << 24)); };
		if (k < a_v.size() && a_v[k] == 0xAA) {
			++k;
			for (int c = 0; c < 4 && k < a_v.size(); ++c) {
				const int n = a_v[k++];
				for (int i = 0; i < n && k + 4 <= a_v.size(); ++i, k += 4) if (const auto r = ResolveId(rd32(k))) g_spellStash[c].push_back(r);
			}
		}
		if (k < a_v.size() && a_v[k] == 0xAB) {
			++k;
			for (int c = 0; c < 4 && k + 2 <= a_v.size(); ++c) {
				const int n = a_v[k] | (a_v[k + 1] << 8);
				k += 2;
				for (int i = 0; i < n && k + 8 <= a_v.size(); ++i, k += 8) if (const auto r = ResolveId(rd32(k))) g_held[c].push_back({ r, static_cast<std::int32_t>(rd32(k + 4)) });
			}
		}
		if (k + 1 < a_v.size() && a_v[k] == 0xAC) g_lanternGiven = a_v[k + 1] != 0, k += 2;
		if (g_wStep[coffer::kSummoner] > kWonderSteps) TuneWyrm();
		logger::info("classes: loaded ({})", Name(g_class));
	}
	void Revert()
	{
		SetState({});
		g_open = false, g_mastery.fill(0), g_staffChecked = false;
		g_summonMarks.clear(), g_pendingSummon.clear(), g_ownSummons.clear(), g_thralls.clear(), g_pendingThrall.clear(), g_lastBlade.clear(), g_powerHit.clear(), g_vortexArrows.clear();
	}

	// ---- (the altar rework) what Kit.cpp builds a class's kit from ----
	RE::TESObjectWEAP* CrossbowForm(int a_elem) { return a_elem >= 0 && a_elem < 3 ? ElemCrossbow(a_elem) : nullptr; }
	void TakeCrossbow(int a_elem)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* w = CrossbowForm(a_elem);
		if (!p || !w || ItemMenusOpen()) return;
		KeepOnlyCrossbow(a_elem);
		if (p->GetItemCount(w) <= 0) GiveItem(p, w, 1, false);
		if (auto* b = ElemBolts(a_elem)) {
			const int want = BoltCap(std::clamp(CrossbowLevel(w), 0, 3)), have = p->GetItemCount(b);
			if (have < want) GiveItem(p, b, want - have, false);
			GiveItem(p, b, 0, true);
		}
		GiveItem(p, w, 0, true);
	}
	void SetCrossbowLevel(const RE::TESForm* a_w, int a_level)
	{
		if (!a_w) return;
		g_xbow[a_w->GetFormID()].first = static_cast<std::uint8_t>(std::clamp(a_level, 0, 3));
		TopUpBolts(0);
	}
	void GiveGear(RE::TESForm* a_form, int a_count, bool a_equip)
	{
		if (auto* p = RE::PlayerCharacter::GetSingleton(); p && a_form) GiveItem(p, a_form, a_count, a_equip);
	}
	void LearnSpell(RE::SpellItem* a_sp) { Learn(a_sp); }
	void QueueThrall(RE::Actor* a_struck, int a_level)
	{
		if (a_struck && Director::IsRoundUndead(a_struck) && !Director::IsBoss(a_struck)) g_pendingThrall.push_back({ a_struck->GetHandle(), 0.6f, a_level });
	}
	bool KitGiven(int a_cls) { return a_cls >= 0 && a_cls < 4 && g_kit[a_cls]; }
	RE::TESObjectWEAP* Riftblade()
	{
		auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(gen::kRiftblade, gen::kPlugin);
		return f ? f->As<RE::TESObjectWEAP>() : nullptr;
	}
	// (Noah: "the Spellsword should be a one-hand sword selected from one of the weapon mods to look unique") its blade
	RE::TESObjectWEAP* SpellswordBlade()
	{
		// (Noah: "use the Madness Sword for the Spellsword class's weapon") our copy of Saints & Seducers' Madness Sword; the
		// Bolgan Sword, then the Steel Spellsword, should its model be missing
		// (Noah: "the Valkyrie armour for a female Spellsword - it comes with a weapon, use it") a woman's blade is Odin's Wrath,
		// Awoken once her set reaches Tier III; a man's the Madness Sword
		if (PlayerFemale()) {
			const bool awoken = Kit::ArmourTier(coffer::kSpellsword) >= 3;
			if (auto* f = Lookup(awoken ? 0x000D62 : 0x00435A, "ValkyrieSwordShield.esp")) {
				if (auto* w = f->As<RE::TESObjectWEAP>()) return w;
			}
		}
		static constexpr std::pair<std::string_view, std::uint32_t> kBlade[] = { { gen::kPlugin, gen::kMadnessSword }, { "mihailhighfantasypack.esp", 0x065CAF }, { "Spellsword.esp", 0x00B550 } };
		for (const auto& [plugin, id] : kBlade) {
			if (auto* f = Lookup(id, plugin)) {
				if (auto* w = f->As<RE::TESObjectWEAP>()) return w;
			}
		}
		return nullptr;
	}
	int TwoHanderCount() { return static_cast<int>(std::size(kTwoHanders)); }
	RE::TESObjectWEAP* TwoHander(int a_i)
	{
		if (a_i < 0 || a_i >= TwoHanderCount()) return nullptr;
		auto* f = Lookup(kTwoHanders[a_i].id, kTwoHanders[a_i].plugin);
		return f ? f->As<RE::TESObjectWEAP>() : nullptr;
	}

	// ---- (Noah: the class armour sets - "each armour set its own way of gaining power, just like their weapons") ----
	// Summoner: the Arcane Conjurer Robes; Destruction: the Black Mage's robes, its look growing with the set's tiers;
	// Ranger: the Gondorian Ranger's; Spellsword: the Valkyrie's (a woman) or the DragonLord's robe armour and helmet
	// (a man). Every piece light armour weighing 1 (no slowing, no noise); the set's armour 10 at the start and 10 more for
	// each of its upgrades, shared out over its pieces (Kit::ArmourRating)
	namespace
	{
		struct Piece { const char* plugin; std::uint32_t id; };
		std::vector<Piece> SetPieces(int a_cls, int a_tier, bool a_female)
		{
			switch (a_cls) {
			case coffer::kSummoner: return { { "ArcaneConjurerRobes.esp", 0x000802 } };
			case coffer::kDestruction:
				switch (std::clamp(a_tier, 0, 3)) {
				case 0: return { { "Black Mage Armor SE.esp", 0x0146A5 }, { "Black Mage Armor SE.esp", 0x0146A7 }, { "Black Mage Armor SE.esp", 0x014C0B }, { "Black Mage Armor SE.esp", 0x0146A6 } };  // College robes, hood and scarf, handwraps, boots
				case 1: return { { "Black Mage Armor SE.esp", 0x002869 }, { "Black Mage Armor SE.esp", 0x008411 }, { "Black Mage Armor SE.esp", 0x002867 }, { "Black Mage Armor SE.esp", 0x002866 } };  // the mantled robes
				case 2: return { { "Black Mage Armor SE.esp", 0x005E4D }, { "Black Mage Armor SE.esp", 0x00EABA }, { "Black Mage Armor SE.esp", 0x00EAB9 }, { "Black Mage Armor SE.esp", 0x00EAB8 } };  // the plate
				default: return { { "Black Mage Armor SE.esp", 0x00DA78 }, { "Black Mage Armor SE.esp", 0x00DFDC }, { "Black Mage Armor SE.esp", 0x00EAA3 }, { "Black Mage Armor SE.esp", 0x00EAA2 } };  // the Black Archmage's ebony
				}
			case coffer::kRanger: return { { "GondorianRangerArmor.esp", 0x000D64 }, { "GondorianRangerArmor.esp", 0x000D66 }, { "GondorianRangerArmor.esp", 0x000D65 }, { "GondorianRangerArmor.esp", 0x000D63 }, { "GondorianRangerArmor.esp", 0x000D79 } };
			case coffer::kSpellsword:
				// (Noah: "remove the cape from the [Spellsword] class, just so that we don't have to worry about it anymore")
				if (a_female) return { { "ValkyrieSwordShield.esp", 0x00285D }, { "ValkyrieSwordShield.esp", 0x00285A }, { "ValkyrieSwordShield.esp", 0x00285B }, { "ValkyrieSwordShield.esp", 0x002859 } };
				// (Noah: "change the spell sword helmet for the male character to the other option ... I don't like the way the Dragon Lord
				// plume helmet looks") the DragonLord Helmet, not the plume
				return { { "DragonLordSwords.esp", 0x0012D3 }, { "DragonLordSwords.esp", 0x0012CF }, { "DragonLordSwords.esp", 0x0012CE }, { "DragonLordSwords.esp", 0x0012CD } };
			default: return {};
			}
		}
		std::vector<RE::TESObjectARMO*> SetForms(int a_cls, int a_tier)
		{
			std::vector<RE::TESObjectARMO*> out;
			for (const auto& pc : SetPieces(a_cls, a_tier, PlayerFemale())) {
				if (auto* f = Lookup(pc.id, pc.plugin)) if (auto* a = f->As<RE::TESObjectARMO>()) out.push_back(a);
			}
			return out;
		}
		// every piece of every tier the class's set has had (the Battle-mage's earlier looks)
		std::vector<RE::TESObjectARMO*> AllSetForms(int a_cls)
		{
			std::vector<RE::TESObjectARMO*> out;
			for (int t = 0; t <= (a_cls == coffer::kDestruction ? 3 : 0); ++t) for (auto* a : SetForms(a_cls, t)) out.push_back(a);
			return out;
		}
	}
	std::string ArmourSetName(int a_cls, int a_tier)
	{
		switch (a_cls) {
		case coffer::kSummoner: return "Arcane Conjurer Robes";
		case coffer::kDestruction: { static constexpr const char* kN[4] = { "Black Mage Robes", "Black Mage Mantled Robes", "Black Mage Plate", "Black Archmage Ebony" }; return kN[std::clamp(a_tier, 0, 3)]; }
		case coffer::kRanger: return "Gondorian Ranger Armor";
		case coffer::kSpellsword: return PlayerFemale() ? "Valkyrie Armor" : "DragonLord Armor";
		default: return "Armor";
		}
	}
	bool ArmourCarried(int a_cls, int a_tier)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return false;
		for (auto* a : SetForms(a_cls, a_tier)) if (p->GetItemCount(a) > 0) return true;
		return false;
	}
	void GiveArmourSet(int a_cls, int a_tier, bool a_wear)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || ItemMenusOpen()) return;
		const auto want = SetForms(a_cls, a_tier);
		if (want.empty()) {
			logger::warn("classes: {}'s armour set not found (its mod missing?)", Name(a_cls));
			return;
		}
		// (the Battle-mage's set: the earlier look's pieces go as the new ones come)
		for (auto* a : AllSetForms(a_cls)) {
			if (std::find(want.begin(), want.end(), a) != want.end()) continue;
			if (const int n = p->GetItemCount(a); n > 0) p->RemoveItem(a, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		for (auto* a : want) GiveItem(p, a, p->GetItemCount(a) > 0 ? 0 : 1, a_wear);
		// (a woman Spellsword's Odin's Wrath awakens with her set's Tier III)
		if (a_cls == coffer::kSpellsword && PlayerFemale() && a_tier >= 3) {
			auto* plain = Lookup(0x00435A, "ValkyrieSwordShield.esp");
			auto* awoken = Lookup(0x000D62, "ValkyrieSwordShield.esp");
			auto* pw = plain ? plain->As<RE::TESBoundObject>() : nullptr;
			if (pw && awoken && p->GetItemCount(pw) > 0) {
				const bool held = p->GetEquippedObject(false) == pw;
				p->RemoveItem(pw, p->GetItemCount(pw), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				GiveItem(p, awoken, 1, held);
				Hud::Banner("Odin's Wrath awakens", 4.0f);
			}
		}
		logger::info("classes: {} - {} (tier {}) given{}", Name(a_cls), ArmourSetName(a_cls, a_tier), a_tier, a_wear ? ", worn" : "");
	}
	// every second: the set's pieces as the rules want them (light, weight 1, the set's armour shared out), the Ranger's stride
	void TickArmour()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		const int c = g_class.load();
		if (!p || c < 0) return;
		const int  rating = Kit::ArmourRating(c);
		const auto set = SetForms(c, std::max(0, Kit::ArmourTier(c)));
		// (the Valkyrie cape, no longer the set's: taken back from a character given it before)
		if (static auto* cape = Lookup(0x003890, "ValkyrieSwordShield.esp"); cape) {
			if (auto* ca = cape->As<RE::TESObjectARMO>(); ca && p->GetItemCount(ca) > 0 && !ItemMenusOpen()) {
				p->RemoveItem(ca, p->GetItemCount(ca), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				logger::info("classes: the Valkyrie cape taken back (no longer part of the set)");
			}
		}
		// (the DragonLord plume helmet, no longer the set's: the plain helmet in its place - worn if the plume was)
		if (static auto* plume = Lookup(0x0012D2, "DragonLordSwords.esp"); plume && !ItemMenusOpen()) {
			if (auto* pa = plume->As<RE::TESObjectARMO>(); pa && p->GetItemCount(pa) > 0) {
				const bool worn = p->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kHead) == pa || p->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kHair) == pa;
				p->RemoveItem(pa, p->GetItemCount(pa), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				auto* helm = Lookup(0x0012CF, "DragonLordSwords.esp");
				if (auto* hb = helm ? helm->As<RE::TESBoundObject>() : nullptr; hb && c == coffer::kSpellsword && !PlayerFemale()) GiveItem(p, hb, p->GetItemCount(hb) > 0 ? 0 : 1, worn);
				logger::info("classes: the DragonLord plume helmet swapped for the plain helmet{}", worn ? " (worn)" : "");
			}
		}
		static auto* lightKw = RE::TESForm::LookupByID<RE::BGSKeyword>(0x06BBD3);  // ArmorLight
		static auto* heavyKw = RE::TESForm::LookupByID<RE::BGSKeyword>(0x06BBD2);  // ArmorHeavy (no heavy armour's weight on you)
		for (int k = 0; k < 4; ++k) {
			for (auto* a : AllSetForms(k)) {
				if (!a) continue;
				a->bipedModelData.armorType = RE::BIPED_MODEL::ArmorType::kLightArmor;
				a->weight = 1.0f;
				if (lightKw && !a->HasKeyword(lightKw)) a->AddKeyword(lightKw);
				if (heavyKw && a->HasKeyword(heavyKw)) a->RemoveKeyword(heavyKw);
				if (k != c) continue;
				const bool inSet = std::find(set.begin(), set.end(), a) != set.end();
				const auto want = static_cast<std::uint32_t>(inSet && !set.empty() ? std::lround(100.0 * rating / static_cast<double>(set.size())) : 0);
				if (a->armorRating != want) a->armorRating = want;
			}
		}
		// (the Gondorian set, Tier I) the Ranger's stride: 10% faster
		if (auto* swift = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(gen::kSetSwift, gen::kPlugin)) {
			const bool want = c == coffer::kRanger && Kit::ArmourTier(c) >= 1;
			if (want != p->HasSpell(swift)) {
				if (want) p->AddSpell(swift); else p->RemoveSpell(swift);
				auto* av = p->AsActorValueOwner();  // (a speed change shows once the carry weight is touched)
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.1f);
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.1f);
			}
		}
	}
}
