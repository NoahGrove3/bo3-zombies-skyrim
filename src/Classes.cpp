#include "BO3Z.h"
#include "CofferSpells.h"

// The four classes (Noah, the classes overhaul): Summoner, Destruction, Ranger, Spellsword. A new character picks one
// on a full-screen picker the moment it is out of the character creator; the pick hands over the class's starter kit.
// Any Echo Altar can switch class for 50,000 Cursed Essence. (Noah: "one inventory for each player so a player can mix
// and match") everything you carry - weapons, armour, spells - stays with you; a class's own powers (the horn, the
// Mutagen Staff, the flasks) answer only to that class. The class you take up gets its kit the first time.
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
		// (Noah: "switching classes - my items should stay in the correct class, almost like an inventory swap, saveable") each
		// class's gear waits in its own hidden chest (a persistent, disabled container) while another class is played
		std::array<RE::FormID, 4>                              g_chest{};      // per class: its chest's reference (0 none yet)
		struct OldSlot { int gun = -1, clip = 0, reserve = 0, level = 0; };  // (an older save's weapon slots: read and dropped)
		std::array<std::array<OldSlot, 3>, 4>                  g_slotStash{};  // per class: its three weapon slots (none now)
		std::array<std::vector<RE::FormID>, 4>                 g_wornStash;    // per class: what it had on / in hand
		std::array<std::vector<std::uint8_t>, 4>               g_perkStash;    // per class: its altar blessings' stacks
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
		// a bow or a crossbow (Noah: "remove the crossbows with the bows" - OffhandBows is no longer part of the game)
		bool IsRangerWeapon(const RE::TESObjectWEAP* a_w)
		{
			return a_w && (a_w->IsBow() || a_w->IsCrossbow());
		}
		int Rung1(int a_cls, std::string_view a_ladderLabel)
		{
			for (int i = 0; i < static_cast<int>(coffer::kSpells.size()); ++i) {
				const auto& s = coffer::kSpells[i];
				if (s.cls == a_cls && s.rung == 1 && a_ladderLabel == s.ladderLabel) return i;
			}
			return -1;
		}
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
		void GiveItem(RE::PlayerCharacter* a_p, RE::TESForm* a_form, int a_count, bool a_equip)
		{
			auto* obj = a_form ? a_form->As<RE::TESBoundObject>() : nullptr;
			if (!obj) return;
			if (a_count > 0) Forms::Give(a_p, obj, a_count);
			if (a_equip) {
				if (auto* em = RE::ActorEquipManager::GetSingleton()) em->EquipObject(a_p, obj, nullptr, 1, nullptr, false, false, false, true);
			}
		}
		// (Noah: "with a male character the class armour sets make my body invisible - they're for women") a piece fits
		// you when one of its addons has a model for your sex (the Drifter Outfit and the Wandering Summoner have none for men)
		bool Fits(const RE::TESObjectARMO* a_armo, bool a_female)
		{
			if (!a_armo) return true;
			// (a piece with no body model for either sex at all - the Light Orb, drawn by the plugin over your head - is
			// meant to be unseen on the body, and fits everyone)
			bool any = false;
			for (auto* aa : a_armo->armorAddons) {
				if (!aa) continue;
				const char* m = aa->bipedModels[a_female ? RE::SEXES::kFemale : RE::SEXES::kMale].GetModel();
				if (m && *m) return true;
				const char* o = aa->bipedModels[a_female ? RE::SEXES::kMale : RE::SEXES::kFemale].GetModel();
				any |= o && *o;
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
			logger::info("classes: the Light Orb given, worn");
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
				// (Noah: "the destruction class should only be allowed to hold one wand at a time") the wand in the right hand
				GiveItem(p, CarianBlade(), 1, false);
				if (auto* em = RE::ActorEquipManager::GetSingleton(); em && CarianBlade()) {
					em->EquipObject(p, CarianBlade(), nullptr, 1, RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F42), false, false, false, true);
				}
				if (auto* tome = Lookup(0x817, "Equipable Tomes - Redone.esp")) GiveItem(p, tome, 1, true);  // a Destruction tome at the hip
				break;
			case coffer::kAlchemist:
				// (Noah: "replace it with the Alchemist class") the knife in the right hand, the Fire Flask in your pack (a hotkey throws it)
				GiveItem(p, AlchemistKnife(), 1, true);
				GiveFlask(0);
				// (the scythe is two-handed: the Fire Flask waits in your pack - sheathe the scythe and it comes to hand, TickScytheSwap)
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
		// (Noah: "make each class use one inventory for each player so a player can mix and match their setup" - "Keep
		// everything"; old saves: "Move into inventory") a class change no longer puts anything away: weapons, armour and
		// spells all stay with you. What older saves put away for another class (its chest, what the save held for it, its
		// altar spells and their levels, the altar's other spells, other spells, its blessings) comes back into your pack
		bool g_mergeDone = false;
		void MergeStashes()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || !p->Is3DLoaded() || !p->GetParentCell()) return;
			g_mergeDone = true;
			int items = 0, spells = 0;
			for (int c = 0; c < 4; ++c) {
				if (auto* chest = ChestOf(c, false)) {
					struct Move { RE::TESBoundObject* obj; std::int32_t count; RE::ExtraDataList* x; };
					std::vector<Move> moves;
					for (auto& [obj, e] : chest->GetInventory()) {
						if (!obj || e.first <= 0) continue;
						std::int32_t left = e.first;
						if (e.second && e.second->extraLists) {
							for (auto* x : *e.second->extraLists) {
								if (!x || left <= 0) continue;
								const std::int32_t n = std::max<std::int32_t>(1, x->GetCount());
								moves.push_back({ obj, std::min(n, left), x });
								left -= std::min(n, left);
							}
						}
						if (left > 0) moves.push_back({ obj, left, nullptr });
					}
					for (auto& m : moves) chest->RemoveItem(m.obj, m.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, m.x, p), items += m.count;
				}
				for (auto [id, n] : g_held[c]) {
					if (auto* o = RE::TESForm::LookupByID<RE::TESBoundObject>(id); o && n > 0) p->AddObjectToContainer(o, nullptr, n, nullptr), items += n;
				}
				g_held[c].clear();
				for (auto [i, lv] : g_stash[c]) {
					if (!Arcana::Known(i)) Arcana::Give(i, false), ++spells;
					Arcana::SetLevel(i, std::max(lv, Arcana::Level(i)));
				}
				g_stash[c].clear();
				for (auto id : g_extra[c]) if (auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(id); sp && !p->HasSpell(sp)) p->AddSpell(sp), ++spells;
				for (auto id : g_spellStash[c]) if (auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(id); sp && !p->HasSpell(sp)) p->AddSpell(sp), ++spells;
				g_spellStash[c].clear();
				if (!g_perkStash[c].empty()) Perks::GiveBack(g_perkStash[c]), g_perkStash[c].clear();
				g_wornStash[c].clear();
			}
			if (items || spells) logger::info("classes: one inventory - {} items and {} spells put away for other classes are back in your pack", items, spells);
		}
		void Swap(int a_from, int a_to)
		{
			if (a_from == coffer::kSummoner && a_to != coffer::kSummoner) DismissSummons();
			if (a_to >= 0 && a_to < 4 && !g_kit[a_to]) GiveKit(a_to);   // (a class's starter kit the first time - added to what you carry)
			MergeStashes();
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
		constexpr const char* kWonderName[4] = { "Soulcaller's Staff", "Staff of Magnus", "Mutagen Staff", "Dragon Slayer" };  // (Noah: the Summoner's, Destruction's and the Alchemist's remade)
		std::array<std::uint8_t, 4>  g_wStep{};    // per class: 0 not open, 1..3 the step under way, 4 earned
		std::array<std::uint16_t, 4> g_wKills{};
		std::array<std::uint32_t, 4> g_wTowns{};   // cities where a step was done (bits)
		std::unordered_map<RE::FormID, RE::FormID> g_lastHit;  // a foe -> what you last hit it with
		std::unordered_set<RE::FormID> g_powerHit;  // foes your last blow on was a power attack (the Spellsword's resonance)
		std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_lastBlade;  // a foe -> when your blade last struck it
		std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_flaskMark;  // (the Alchemist) a foe -> when a flask's burst last caught it
		bool FlaskMarked(RE::Actor* a) { auto it = g_flaskMark.find(a->GetFormID()); return it != g_flaskMark.end() && std::chrono::steady_clock::now() - it->second < std::chrono::milliseconds(1500); }
		float g_tomeCool = 0, g_bladeCool = 0;
		float g_tomeHold = 0;  // (the Six Fires: their spells held at the top tier until it runs out)
		std::vector<std::pair<int, int>> g_tomeHeld;  // a spell and the level it had

		int FreedCount()
		{
			int n = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		bool AfterSovngarde() { return Echo::DragonsSlain(); }  // (the story: the wonders' Legendary form - once the three dragons fall)
		RE::TESObjectWEAP* Soulcaller() { auto* f = Lookup(gen::kSoulcaller, gen::kPlugin); return f ? f->As<RE::TESObjectWEAP>() : nullptr; }  // (the Summoner's wonder now)
		RE::SpellItem* WyrmSpell() { auto* dh = RE::TESDataHandler::GetSingleton(); return dh ? dh->LookupForm<RE::SpellItem>(gen::kBoneHorn, gen::kPlugin) : nullptr; }  // (the Bone-Throne Horn)
		RE::TESObjectWEAP* Magnus() { auto* f = Lookup(0x035369, "Skyrim.esm"); return f ? f->As<RE::TESObjectWEAP>() : nullptr; }  // (Destruction's wonder: Skyrim's own Staff of Magnus)
		RE::SpellItem* TomeSpell() { auto* dh = RE::TESDataHandler::GetSingleton(); return dh ? dh->LookupForm<RE::SpellItem>(gen::kWonderTomeSpell, gen::kPlugin) : nullptr; }
		RE::TESObjectWEAP* Slayer() { auto* f = Lookup(gen::kSlayerWeap, gen::kPlugin); return f ? f->As<RE::TESObjectWEAP>() : nullptr; }  // (our own one-handed sword: the Ayleid Crystal Saber)
		RE::SpellItem* Myst(std::uint32_t a_id) { auto* f = Lookup(a_id, "MysticismMagic.esp"); return f ? f->As<RE::SpellItem>() : nullptr; }

		// the horn's dragon: a minute mid-game, after Sovngarde until it falls
		void TuneWyrm() {}  // (the Bone-Throne Horn is our own spell: its cost and cast set in the plugin)
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
			case coffer::kSummoner:   // (Noah: the Summoner's wonder "a staff from the weapons mod" - the Soulcaller's Staff)
				GiveItem(p, Soulcaller(), 1, true);
				break;
			case coffer::kDestruction:
				GiveItem(p, Magnus(), 1, true);
				break;
			case coffer::kAlchemist:
				GiveFlask(6);
				break;
			case coffer::kSpellsword:
				GiveItem(p, Slayer(), 1, true);
				break;
			}
		g_wStep[a_cls] = kWonderTaken;
			Forms::Play(gen::Sound::shout_learned);
			static constexpr const char* kText[4] = {
				"The Soulcaller's Staff. Hold its cast: the souls of the dead along your aim tear out of them into the staff. Let go and they return as spectral wolves that hunt for 20 seconds - one for each soul, up to eight.",
				"The Staff of Magnus. Hold its beam on the dead: it drains them and feeds your magicka, and erupts where it ends. Hold it two seconds or more and the eruption leaves an orb of searing light that pulses on everything round it. Once the three dragons fall it burns hotter still.",
				"The Mutagen Staff. A staff whose glass orb is fed by six vials of the elements. Hold attack to loose a beam from the orb: it pierces every one of the dead in its path, pouring each vial in turn for five seconds - fire, frost, lightning, acid, holy light and the void. Let go and the emptied vials refill; empty all six and they must refill completely. Once the three dragons fall, the beam burns stronger.",
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
	int  SwitchCost() { return 50000; }   // (Noah: "I want to make the change class 50,000 cursed essence so people cant just flip through them")

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
		void TakeGuides()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			auto* dh = RE::TESDataHandler::GetSingleton();
			std::vector<RE::TESObjectBOOK*> books;
			for (int c = 0; c < 4; ++c) if (auto* b = Guide(c)) books.push_back(b);
			if (auto* b = dh->LookupForm<RE::TESObjectBOOK>(gen::kGuideBook, gen::kPlugin)) books.push_back(b);   // (the old Survivor's Guide)
			for (auto* b : books) {
				if (const int n = p->GetItemCount(b); n > 0) {
					p->RemoveItem(b, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					logger::info("classes: {} taken back - The Curse of Alduin holds every class's pages", b->GetName());
				}
			}
		}
		[[maybe_unused]] bool GiveGuide(int a_cls)
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
		if (from >= 0 && from != a_cls) Perks::RefundOtherClasses(a_cls);
		Purge();
		Swap(from, a_cls);
		g_open = false;
		g_closedAt = std::chrono::steady_clock::now();
		Forms::Play(gen::Sound::quest_step);
		{ std::string u(coffer::kClassName[a_cls]); for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); Hud::Banner(u, 4.0f); }
		// (Noah: "since the curse of Alduin book contains everything for the classes and the lore remove the class path books so
		// everyone just gets the curse of Alduin book") your class's pages are a tab in it
		TakeGuides();
		Hud::Lore("THE CURSE OF ALDUIN", "Your class's path is a tab in The Curse of Alduin, in your pack under Books (F1). It shows how your class grows and keeps a live page of how far you have come.", 9.0f);
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
		TakeGuides();  // (a save from before: the class path books taken back - The Curse of Alduin holds them all)
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
	void SlayerKill();
	void OnKill(bool a_bySummon, RE::Actor* a_dying, RE::Actor* a_killer)
	{
		// (the class guide: the Summoner alone lent the party nothing) a Summoner in your party: each of your kills heals you 2%
		if (g_class.load() != coffer::kSummoner && Kit::PartyHas(coffer::kSummoner)) {
			if (auto* p = RE::PlayerCharacter::GetSingleton(); p && !p->IsDead()) {
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.02f);
			}
		}
		int killHow = -1;
		// (Dragonbreaker) a kill made by the Dragon Slayer feeds it an ember
		if (a_dying && !a_bySummon) if (auto it = g_lastHit.find(a_dying->GetFormID()); it != g_lastHit.end() && Slayer() && it->second == Slayer()->GetFormID()) SlayerKill();
		// the wonder trial: what made the kill
		if (const int c = g_class.load(); c >= 0) {
			int& how = killHow;  // 0 summon, 1 destruction spell, 2 a flask (the Alchemist), 3 melee
			auto* p = RE::PlayerCharacter::GetSingleton();
			RE::TESForm* src = nullptr;
			if (a_dying) {
				if (auto it = g_lastHit.find(a_dying->GetFormID()); it != g_lastHit.end()) src = RE::TESForm::LookupByID(it->second);
			}
			if (a_bySummon) how = 0;
			else if (auto* mi = src ? src->As<RE::MagicItem>() : nullptr) how = (mi->GetAssociatedSkill() == RE::ActorValue::kDestruction || (Arcana::IndexOf(src) >= 0 && Arcana::ClassOf(Arcana::IndexOf(src)) == coffer::kDestruction)) ? 1 : -1;
			else if (auto* w = src ? src->As<RE::TESObjectWEAP>() : nullptr) how = IsRangerWeapon(w) ? -1 : 3;  // (a bow or crossbow of Skyrim's: no class's kill)
			else if (p) {
				for (int h : Arcana::Hands()) if (h >= 0 && Arcana::ClassOf(h) == coffer::kDestruction) how = 1;
			}
			// (the Alchemist) caught in one of your flasks' bursts a moment ago: a flask kill
			if (c == coffer::kAlchemist && a_dying && FlaskMarked(a_dying)) how = 2;
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
			}
			if (how >= 0) Perks::OnClassKill(how, a_dying);  // (the class perks: Soul-Tether, Ember Heart, Mana-Siphon, Spellbinder)
		}
		if (const int c2 = g_class.load(); c2 >= 0 && c2 < 4) {
			if (g_wStep[c2] >= 1 && g_wStep[c2] <= kWonderSteps && FreedCount() < kWonderOpensAt && !Echo::HallOpen()) g_wStep[c2] = 0;  // (the audit: never in a new cycle begun at the Seat)  // (an old save's three-city trial: shut again until six)
			if (g_wStep[c2] == 0 && FreedCount() >= kWonderOpensAt) {
				g_wStep[c2] = 1;
				Forms::Play(gen::Sound::quest_start);
				Hud::Lore("YOUR PATH'S SECRET",
					fmt::format("Six cities are free, and the {} stirs somewhere beneath the Echo Altars. Nobody will tell you how to win it. Stand at a freed altar and listen to its whispers.", kWonderName[c2]), 12.0f);
			}
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

	namespace { std::mutex g_lineLock; std::string g_line; std::vector<std::string> g_progress; int g_progressCls = -1; CodexSnap g_codexSnap; }
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
		if (!any && c != coffer::kAlchemist) out.push_back("You carry none of your class's altar spells right now - call one at an Echo Altar.");
		switch (c) {
		case coffer::kSummoner:
			{
				int cap = 1;
				for (int k = 8; k >= 2; --k) {
					if (auto* perk = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSPerk>(gen::kSummonCapPerks[k - 2], gen::kPlugin); perk && p->HasPerk(perk)) { cap = k; break; }
				}
				out.push_back(fmt::format("<b>Summon slots: {} of 4.</b> You start with 2; the Conjurer's Command Mead adds one, and your robes' Tier II one more.", cap));
			}
			break;
		case coffer::kDestruction:
			out.push_back("Hold a different element in each hand: the Trial of the Elements wants kills with two elements or more.");
			break;
		case coffer::kAlchemist:
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
		if (!g_mergeDone && p->Is3DLoaded() && !ItemMenusOpen() && Forms::IntroDone()) MergeStashes();   // (an older save's class stashes back into your pack)
		// (Noah: "the mutagen staff should cast like the staff of magnus" - "Same cast and beam") its enchantment's effect
		// takes the Staff of Magnus's beam: aimed, its projectile and its casting and hit art - the vials, their elements
		// and the damage stay ours (GauntletBeam.inl)
		// (Noah: "lets just make it a summon spell") the Call of the Violet Wyrm casts as a Conjuration spell: Skyrim's
		// Conjure Familiar's hand glow and sounds, in the Conjuration school
		if (static bool conj = false; !conj) {
			conj = true;
			auto* sp = WyrmSpell();
			auto* me = sp && !sp->effects.empty() && sp->effects[0] ? sp->effects[0]->baseEffect : nullptr;
			auto* fam = RE::TESForm::LookupByID<RE::SpellItem>(0x000640B6);   // Conjure Familiar
			auto* src = fam && !fam->effects.empty() && fam->effects[0] ? fam->effects[0]->baseEffect : nullptr;
			if (me && src) {
				me->data.associatedSkill = RE::ActorValue::kConjuration;
				me->data.castingArt = src->data.castingArt;
				me->data.castingSoundLevel = src->data.castingSoundLevel;
				me->effectSounds = src->effectSounds;
				logger::info("summoner: the Call of the Violet Wyrm casts as {}", src->GetFullName());
			}
		}
		// (Noah: the Soulcaller's Staff - Fulcimentum's Glass Staff, "the same casting animation as the staff of magnus") its look
		// the Glass Staff's, its held cast the Staff of Magnus's beam; an old Call of the Violet Wyrm taken back
		if (static bool soul = false; !soul) {
			soul = true;
			auto* sc = Soulcaller();
			auto* gf = Lookup(0x000808, "Fulcimentum - More Staves and Wands of Skyrim.esp");
			if (auto* gs = gf ? gf->As<RE::TESObjectWEAP>() : nullptr; sc && gs) {
				auto* keep = sc->formEnchanting;
				AdoptLook(sc, gs);
				sc->formEnchanting = keep, sc->amountofEnchantment = 0;
			}
			auto* mg = Magnus();
			auto* me = sc && sc->formEnchanting && !sc->formEnchanting->effects.empty() && sc->formEnchanting->effects[0] ? sc->formEnchanting->effects[0]->baseEffect : nullptr;
			auto* src = mg && mg->formEnchanting && !mg->formEnchanting->effects.empty() && mg->formEnchanting->effects[0] ? mg->formEnchanting->effects[0]->baseEffect : nullptr;
			if (me && src) {
				me->data.projectileBase = src->data.projectileBase, me->data.castingArt = src->data.castingArt;
				me->data.hitEffectArt = src->data.hitEffectArt, me->data.effectShader = src->data.effectShader;
				me->data.castingType = RE::MagicSystem::CastingType::kConcentration, me->data.delivery = RE::MagicSystem::Delivery::kAimed;
			}
			logger::info("summoner: the Soulcaller's Staff wears the Glass Staff and casts the Staff of Magnus's beam");
		}
		if (auto* sp = WyrmSpell(); sp && p->HasSpell(sp)) { p->RemoveSpell(sp); logger::info("summoner: the Call of the Violet Wyrm taken back - the Soulcaller's Staff is the wonder now"); }
		if (static bool beamed = false; !beamed) {
			auto* mut = dh->LookupForm<RE::TESObjectWEAP>(gen::kFlaskItem[6], gen::kPlugin);
			auto* mg = Magnus();
			auto* me = mut && mut->formEnchanting && !mut->formEnchanting->effects.empty() && mut->formEnchanting->effects[0] ? mut->formEnchanting->effects[0]->baseEffect : nullptr;
			auto* src = mg && mg->formEnchanting && !mg->formEnchanting->effects.empty() && mg->formEnchanting->effects[0] ? mg->formEnchanting->effects[0]->baseEffect : nullptr;
			if (me && src) {
				me->data.delivery = RE::MagicSystem::Delivery::kAimed;
				me->data.castingType = RE::MagicSystem::CastingType::kConcentration;
				me->data.projectileBase = src->data.projectileBase;
				me->data.castingArt = src->data.castingArt;
				me->data.hitEffectArt = src->data.hitEffectArt;
				me->data.effectShader = src->data.effectShader;
				logger::info("alchemist: the Mutagen Staff casts the Staff of Magnus's beam ({})", src->GetFullName());
			}
			beamed = true;
		}
		TuneCorrosion();
		TuneSpellswordGear();
		TickArmour();
		// (Noah: the flasks are items now, thrown for nothing) a character given the old flask spells: taken off
		for (const auto id : gen::kFlaskSpell) {
			if (auto* sp = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(id, gen::kPlugin); sp && p->HasSpell(sp)) p->RemoveSpell(sp);
		}
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
			Hud::Banner("The Light Orb floats over your head - take it off and it goes dark", 5.0f);
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
		auto* uiN = RE::UI::GetSingleton();
		const bool menus = !uiN || uiN->GameIsPaused() || uiN->IsMenuOpen(RE::InventoryMenu::MENU_NAME) || uiN->IsMenuOpen(RE::FavoritesMenu::MENU_NAME) || uiN->IsMenuOpen(RE::ContainerMenu::MENU_NAME) || uiN->IsMenuOpen(RE::BarterMenu::MENU_NAME);
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
					card(2, "THE NIGHT", "At dusk the dead rise in waves. The counter at the top shows how many are left in this wave. Clear every wave and dawn breaks. Fall, and the wave begins again - and your perks are lost.");
				} else if (fighting && hs.wave >= 2 && !(g_guide & 8)) {
					card(3, "BETWEEN WAVES", "A few seconds' breath before the next wave rises: heal, reload, find better ground, or run for an altar. The last few of every wave come straight for you.");
				} else if (!fighting && hs.round >= 1 && (g_guide & 4) && !(g_guide & 16)) {
					card(4, "DAWN", "The dead crumble at first light. Spend your Cursed Essence, then rest until dusk - every night is harder than the last.");
				}
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
			const float want = ranged ? Settings::DrawSpeed() : Settings::MeleeSpeed();
			if (std::abs(av->GetBaseActorValue(RE::ActorValue::kWeaponSpeedMult) - want) > 0.01f) av->SetBaseActorValue(RE::ActorValue::kWeaponSpeedMult, want);
			if (std::abs(av->GetBaseActorValue(RE::ActorValue::kLeftWeaponSpeedMultiply) - want) > 0.01f) av->SetBaseActorValue(RE::ActorValue::kLeftWeaponSpeedMultiply, want);
		}  // (Noah: still there before the class was chosen - from the first moment now)
		DressUnder();  // (Noah: a bra and underwear, never bare - in the inventory too; the equip event dresses you the moment the robe comes off)
		{
			auto line = WonderLineNow();
			auto prog = ProgressNow();
			CodexSnap snap;
			snap.cls = g_class.load();
			snap.freed = FreedCount();
			snap.points = Points::Get();
			snap.nights = static_cast<int>(Save::Get(gen::kAV_round));
			snap.wonder = line;
			if (snap.cls >= 0) {
				Hud::State tmp;
				Kit::Fill(tmp);
				for (const auto& k : tmp.kitLines) snap.kit.push_back({ k.name, k.info, k.tier, k.asc, k.cost, k.can, k.carried, k.color });
			}
			std::scoped_lock l(g_lineLock);
			g_line = std::move(line);
			g_progress = std::move(prog);
			g_progressCls = g_class.load();
			g_codexSnap = std::move(snap);
		}
		// (Fletcher's Sap Draught: the quiver's regrowth comes sooner and faster - Bows.inl TickRegen)
		// (Noah's plan, the Spellsword) Imbue unlocks The Wizard Warrior's Enchantment Powers 1 to 4, one per rung climbed
		// (the audit: the Spellsword's Enchantment Powers followed you into every class)
		// another class's are taken off; coming back gives them again, without the banner a second time
		static std::unordered_set<RE::FormID> announced;
		static constexpr std::uint32_t kPowers[4] = { 0x0008A0, 0x000893, 0x0008A1, 0x002F6A };
		// (Noah: "In the inventory remove the enchantment power 1-4") The Wizard Warrior's Enchantment Powers are no longer given -
		// and taken from any character that has them
		for (auto id : kPowers) if (auto* sp = dh->LookupForm<RE::SpellItem>(id, "The Wizard Warrior.esp"); sp && p->HasSpell(sp)) p->RemoveSpell(sp);
		(void)announced;
		// (Noah: "remove the indrik mounts mod, since we have our dragon glider for movement") no mounts for the Summoner
		// (Noah: "summoner should start with 2 summon slots and go up for every 2 echo altars freed - max 7")
		int cap = 1;
		if (g_class.load() == coffer::kSummoner) {
			// (Noah: "make the max minions 4, which include the starting 2 slots, the perk that increases it by 1 slot, and the
			// summoner armor upgrade") 2, the Mead one more, the robes' Tier II one more - no more from freed cities
			cap = 2;
			if (Perks::Live(gen::kPerk_packleader)) ++cap;  // (Pack Leader Mead)
			if (Kit::ArmourTier(coffer::kSummoner) >= 2) ++cap;   // (the Arcane Conjurer's robes, Tier II)
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
		if (auto* sl = Slayer(); a_w && sl && a_w == sl) return AfterSovngarde() ? kWonderUps + 1 : kWonderUps;  // (Noah: the wonders the strongest of their class - above the blade's highest tier, 13)
		if (const int kt = Kit::TierOfWeapon(a_w); kt >= 0) return kt;  // (the altar rework: a kit or relic blade's own tiers)
		auto* b = a_w ? a_w->As<RE::TESBoundObject>() : nullptr;
		if (!b || !b->GetFile(0)) return -1;
		bool altar = b->GetFile(0)->GetFilename() == "Spellsword.esp";
		for (const auto& u : kTwoHanders) altar |= b->GetLocalFormID() == u.id && b->GetFile(0)->GetFilename() == u.plugin;
		return altar ? std::clamp(GearTier(b), 0, 3) : -1;
	}
	// one light blow of a blade: its share of one of the night's dead (before the night's power) - the one upgrade curve on its
	// tiers. (Noah: "the sword slashes ... should do the same damage as the swords light attack that grows through the tiers")
	float BladeShare(const RE::TESObjectWEAP* a_w)
	{
		if (Tablet::IsTablet(a_w)) return Tablet::Share();   // (the Tablet's donated weapons: one or two tiers over your sword, nothing else)
		const int gear = AltarGearUpgrades(a_w);
		float share = gen::kShareMelee * (gear >= 0 ? UpgradeMult(gear) : 1.0f);
		// (the class audit: the Spellsword's blade, single-target, trailed every other class) its blade a half again; a
		// two-handed wonder's slower swing made up for
		if (Get() == 3 && a_w && a_w == SpellswordBlade()) share *= 1.5f * (Kit::ArmourTier(3) >= 3 ? 1.15f : 1.0f);  // (its set's Tier III: 15% more)
		else if (Get() == 3 && gear >= 0 && a_w && a_w->IsTwoHandedSword()) share *= 1.6f;
		return share;
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
				fmt::format("Your path's secret is won. The Greybeards' Voice rolls across Skyrim: the {} waits for you, floating in the great hall of High Hrothgar.", kWonderName[a_cls]),
				14.0f);
		});
		logger::info("classes: wonder {} won - it waits in the Reliquary", kWonderName[a_cls]);
	}
	// (Noah: "my kit didn't include philosophers flask or the staff of magnus") the test kit's two item wonders, quietly, and
	// the Reliquary shows them taken
	void KitWonders()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		if (auto* m = Magnus(); m && p->GetItemCount(m) <= 0) GiveItem(p, m, 1, false);
		GiveFlask(6);
		if (auto* sc = Soulcaller(); sc && p->GetItemCount(sc) <= 0) GiveItem(p, sc, 1, false);   // (the Summoner's: the Soulcaller's Staff)
		for (int c = 0; c < 4; ++c) g_wStep[c] = kWonderTaken;   // (every class's wonder: the Dragon Slayer is given beside the kit)
		logger::info("testkit: the four wonders - the Soulcaller's Staff, the Staff of Magnus, the Mutagen Staff, the Dragon Slayer");
	}
	void DevGiveWonder(int a_cls)
	{
		if (a_cls < 0 || a_cls >= 4) return;
		GrantWonder(a_cls);
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* em = RE::ActorEquipManager::GetSingleton();
		auto* dh = RE::TESDataHandler::GetSingleton();
		RE::TESBoundObject* w = a_cls == coffer::kDestruction ? static_cast<RE::TESBoundObject*>(Magnus()) : a_cls == coffer::kSpellsword ? static_cast<RE::TESBoundObject*>(Slayer())
		                      : nullptr;
		(void)dh;
		if (p && em && w) em->EquipObject(p, w);
		logger::info("dev: wonder {} given", kWonderName[a_cls]);
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
			const bool have = a_cls == coffer::kSummoner ? (Soulcaller() && p->GetItemCount(Soulcaller()) > 0) : a_cls == coffer::kDestruction ? (Magnus() && p->GetItemCount(Magnus()) > 0)
			                : a_cls == coffer::kAlchemist ? (FlaskItem(6) && p->GetItemCount(FlaskItem(6)) > 0) : (Slayer() && p->GetItemCount(Slayer()) > 0);
			if (!have) {
				// (the audit: a dropped bow or blade was given again for free - copies without end) a lost weapon is made
				// again for 5,000 Cursed Essence; the spells cost nothing (they can't be copied)
				const bool weapon = a_cls == coffer::kDestruction || a_cls == coffer::kSpellsword;
				if (weapon && !Points::Spend(5000)) {
					Hud::Banner(std::string(kWonderName[a_cls]) + " is out in the world. Its pedestal makes it again for 5,000 Cursed Essence.", 5.0f);
					return true;
				}
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
	CodexSnap Codex()
	{
		std::scoped_lock l(g_lineLock);
		return g_codexSnap;
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
		if (st == kWonderWaiting) return std::string(kWonderName[c]) + " waits in High Hrothgar's great hall";
		if (st > kWonderSteps) return std::string(kWonderName[c]) + " is yours";
		if (st == 0) return fmt::format("{}: free {} more {} to wake your path's secret", kWonderName[c], std::max(0, kWonderOpensAt - FreedCount()), kWonderOpensAt - FreedCount() == 1 ? "city" : "cities");
		return std::string(kWonderName[c]) + ": " + Eggs::Line();
	}

	// ---- (Noah: Destruction's new wonder, "Staff of Magnus: a held beam that drains the dead and erupts at the end, plus a
	// charged shot that leaves a sun-like orb pulsing damage") Skyrim's own Staff of Magnus and its beam: each touch of the
	// beam drains the dead it holds and feeds your magicka; when the beam lets go it erupts where it last held - held two
	// seconds or more, the eruption leaves an orb of searing light pulsing on everything round it for five seconds ----
	namespace
	{
		struct MagnusState { RE::NiPoint3 at; float held = 0, since = 99.0f; bool on = false; std::chrono::steady_clock::time_point last{}; };
		MagnusState g_magnus;
		struct MagnusOrb { RE::NiPoint3 at; float left = 5.0f, tick = 0; };
		std::vector<MagnusOrb> g_orbs;
		float MagnusTop() { return UpgradeMult(AfterSovngarde() ? kWonderUps + 1 : kWonderUps) * (0.7f + 0.3f * Arcana::Power()); }
		void MagnusHit(RE::PlayerCharacter* p, RE::Actor* a_target)
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - g_magnus.last < std::chrono::milliseconds(200)) { g_magnus.at = a_target->GetPosition(), g_magnus.since = 0; return; }
			g_magnus.last = now;
			g_magnus.at = a_target->GetPosition(), g_magnus.since = 0, g_magnus.on = true;
			Gun::AreaHurt(a_target->GetPosition() + RE::NiPoint3{ 0, 0, 40.0f }, 110.0f, 0.16f * MagnusTop(), "");
			auto* av = p->AsActorValueOwner();
			av->RestoreActorValue(RE::ActorValue::kMagicka, av->GetPermanentActorValue(RE::ActorValue::kMagicka) * 0.02f);
		}
		void TickMagnus(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* mg = Magnus();
			if (!p || !mg) return;
			// its charge never runs down (the class's wonder, not a soul-gem staff)
			for (const bool left : { false, true }) if (p->GetEquippedObject(left) == mg) p->AsActorValueOwner()->RestoreActorValue(left ? RE::ActorValue::kLeftItemCharge : RE::ActorValue::kRightItemCharge, 1.0e6f);
			if (g_magnus.on) {
				g_magnus.since += a_delta;
				if (g_magnus.since < 0.45f) {
					g_magnus.held += a_delta;
				} else {  // the beam let go: it erupts where it last held
					const bool charged = g_magnus.held >= 2.0f;
					const float top = MagnusTop();
					Gun::ElemBurst(4, g_magnus.at, charged ? 2.6f : 1.4f, 0.0f, 1.0f);
					Gun::Motes(4, g_magnus.at, charged ? 2.2f : 1.3f, charged ? 3.0f : 2.0f);  // (Noah: fine motes off the ground where it struck)
					Gun::ElemBurst(2, g_magnus.at + RE::NiPoint3{ 0, 0, 30.0f }, charged ? 1.8f : 1.0f, 0.08f, 0.8f);
					Gun::FxBlast(charged ? "shockmass" : "runeshock", g_magnus.at);
					Gun::AreaHurt(g_magnus.at, charged ? 380.0f : 230.0f, (charged ? 1.2f : 0.45f) * top, "");
					if (charged && g_orbs.size() < 4) g_orbs.push_back({ g_magnus.at + RE::NiPoint3{ 0, 0, 90.0f }, AfterSovngarde() ? 7.0f : 5.0f, 0.0f });
					g_magnus.on = false, g_magnus.held = 0;
				}
			}
			for (auto& o : g_orbs) {
				o.left -= a_delta;
				if ((o.tick -= a_delta) > 0) continue;
				o.tick = 0.5f;
				Gun::ElemBurst(4, o.at, 1.6f, 0.0f, 0.6f);
				Gun::AreaHurt(o.at - RE::NiPoint3{ 0, 0, 90.0f }, 320.0f, 0.25f * MagnusTop(), "");
			}
			std::erase_if(g_orbs, [](const MagnusOrb& o) { return o.left <= 0; });
		}
	}

	namespace { void MaceHit(RE::PlayerCharacter* a_p, RE::Actor* a_target, bool a_power); }
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
		if (auto* mg = Magnus(); mg && (src == mg || (mg->formEnchanting && src == mg->formEnchanting))) {
			MagnusHit(p, a_target);
		}
		// (Noah: "the old weapon abilities ... would be fully replaced") the class swords' and the Dragon Slayer's work is done by
		// their swings (MaceSwing, SlayerSwing)
		(void)a_power;
	}

	// (Noah: "I get the casting animation and the sound, but nothing is spawning") a summon spell cast: if no new
	// summon of yours stands within a moment and you are under your cap, the creature is called up here instead and
	// held for the spell's duration
	namespace
	{
		struct PendingSummon { RE::TESBoundObject* what = nullptr; float wait = 0, seconds = 60; int before = 0; std::string name; };
		std::vector<PendingSummon>                        g_pendingSummon;
		std::vector<std::pair<RE::ActorHandle, float>>    g_ownSummons;  // the ones called up here: seconds left
		RE::ActorHandle                                   g_boneDragon;  // (the Bone-Throne Horn's dragon)
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
#include "WyrmHorn.inl"
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
			// (Noah: "a mini dragon that can fly around within 15 meters of the player and kill any enemies, with flying breath
			// attacks or fighting or shouts") the Bone-Throne's little dragon flies and fights as a dragon does; strayed past
			// 15 m it is drawn back toward you; and every 1.2 s it breathes green grave-fire down onto the nearest of the dead,
			// its stream seen from its jaws to where it lands - the strongest of any summon's
			// (Noah: the Horn of the Violet Wyrm - "burns enemies within 30 meters of the player") the horn's blow and rest, and
			// its wyrm (and at Legendary its wyrmling) flying and burning as below, held within 30 m of you
			TickHorn(a_delta);
			if (static float boneT = 0, breathTs[2] = { 0, 0.6f }; (boneT += a_delta) > 0.1f) {
				const float dt = boneT;
				boneT = 0;
				for (int wi = 0; wi < 2; ++wi) {
				float& breathT = breathTs[wi];
				breathT -= dt;
				if (auto a = g_wyrms[wi].get(); a && !a->IsDead() && !a->IsDisabled() && a->Is3DLoaded()) {
					const auto me = p->GetPosition();
					auto       at = a->GetPosition();
					RE::NiPoint3 flat = at - me;
					flat.z = 0;
					if (flat.Length() > 4500.0f && breathT < -4.0f) {  // (left far behind: at your side at once - not while it flies in)
						const float yaw = p->GetAngleZ() + 1.2f;
						a->SetPosition(me + RE::NiPoint3{ std::sin(yaw) * 400.0f, std::cos(yaw) * 400.0f, 250.0f }, true);
					} else if (flat.Length() > 3500.0f) {  // (Noah: "the leash on the dragon should be 50m") past it, drawn back toward you
						const RE::NiPoint3 want = me + flat * (3300.0f / flat.Length()) + RE::NiPoint3{ 0, 0, std::clamp(at.z - me.z, 80.0f, 600.0f) };
						RE::NiPoint3 step = want - at;
						if (const float d = step.Length(); d > 10.0f) a->SetPosition(at + step * std::min(1.0f, 220.0f * dt / d), true);  // (gently: a hard pull fought its flight)
					}
					if (auto t = a->GetActorRuntimeData().currentCombatTarget.get(); t && Director::PlayerSide(t.get(), p)) a->StopCombat();  // (never on you or a friend)
					// (Noah: "the dragon gets stuck in the air") hanging in one spot for 4 s (or more): it is carried round to a new
					// place on its circle about you and set on the nearest of the dead again, so its flight starts afresh
					{
						static RE::NiPoint3 lastAt[2]{};
						static float        stillT[2]{};
						if (at.GetDistance(lastAt[wi]) < 120.0f * dt) stillT[wi] += dt; else stillT[wi] = 0.0f;
						lastAt[wi] = at;
						if (stillT[wi] > 4.0f && at.z - me.z > 250.0f) {   // (up in the air: one standing on the ground to fight is left be)
							stillT[wi] = 0.0f;
							const float yaw = std::atan2(flat.x, flat.y) + 1.4f;
							a->SetPosition(me + RE::NiPoint3{ std::sin(yaw) * 1500.0f, std::cos(yaw) * 1500.0f, 700.0f }, true);
							a->StopCombat();
							if (const auto nf = NearFoes(me, 3500.0f, 1); !nf.empty()) SafeCombat(a.get(), nf[0]);
							logger::info("horn: the wyrm hung in the air 4 s - moved on round its circle");
						}
					}
					// (Noah: "cast green fire breath not ice breath") its own shouts held back (the frost, the drain, the
					// Soul Cairn's summons): its breath is the green grave-fire below
					if (auto* pr = a->GetActorRuntimeData().currentProcess; pr && pr->high) pr->high->voiceRecoveryTime = 1.0e6f;
					const auto foes = NearFoes(me, 3500.0f, 1);
					if (!foes.empty() && breathT <= 0) {
						breathT = 1.2f;
						SafeCombat(a.get(), foes[0]);
						at = a->GetPosition();
						const auto tgt = foes[0]->GetPosition();
						RE::NiPoint3 mouth = at + RE::NiPoint3{ 0, 0, 40.0f };
						if (auto* root = a->Get3D()) if (auto* head = root->GetObjectByName("NPC Head")) mouth = head->world.translate;
						const float pf = 0.7f + 0.3f * Arcana::Power();
						const float top = UpgradeMult(AfterSovngarde() ? kWonderUps + 1 : kWonderUps);
						// (Noah: "the dragon's breath needs to look like dragon flames from the actual dragon, not the particle
						// effects") Skyrim's own dragon fire breath (VoiceDragonFire01), breathed from its jaws at the foe
						static auto* breath = RE::TESForm::LookupByID<RE::SpellItem>(0x000252C2);
						// (Noah: "the dragon sound fx are also too loud") Skyrim's roaring breath only every third time (each 3.6 s) -
						// its burn on the dead still every 1.2 s
						static int breaths[2]{};
						if (auto* c = breath && breaths[wi]++ % 3 == 0 ? a->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr) {
							a->SetHeading(std::atan2(tgt.x - at.x, tgt.y - at.y));
							c->CastSpellImmediate(breath, false, foes[0], 1.0f, false, 0.0f, a.get());
						}
						for (int k = 2; k <= 6; k += 2) Gun::AreaHurt(mouth + (tgt - mouth) * (k / 8.0f), 150.0f, 0.15f * top * pf, "");
						Gun::AreaHurt(tgt, 300.0f, 0.6f * top * pf, "");
					}
				}
			}
			}
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
	// (Noah: "an enchanted dagger with a special ability" - Rift Stab) every fifth strike of the Riftblade on one of the night's
	// foes tears a small rift there: a Flame Atronach steps out and fights for you for 10 s (two at most)
	void RiftHit(RE::Actor* a_target)
	{
		(void)a_target;
		return;  // (Noah: the Riftblade is the Radiant Quarterstaff now - its rifts and atronachs replaced by the sword tier ladder)
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

	// ---- (Noah, 2026-11-05: Blood made "Arcane / Spectral", blue-white) the Destruction ladder's three spells of ours and the
	// Spellsword's arcane blade - built from Apocalypse's own arcane effects (fxmap.ini arc_arcane, arcane_orb, arcane_burst,
	// arcane_ring); their damage on the one curve every Echo Altar spell shares (src/Arcana.cpp)
	namespace
	{
		int ArcaneKind(RE::FormID a_spell)   // 0 bolt, 1 orb, 2 tide, 3-5 the blade I-III, -1 not one
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			for (int k = 0; k < 6; ++k) if (auto* f = dh->LookupForm(gen::kArcaneSpell[k], gen::kPlugin); f && f->GetFormID() == a_spell) return k;
			return -1;
		}
		float ArcaneShare(RE::FormID a_spell)   // one hit's share of one of the night's dead
		{
			const int i = Arcana::IndexOf(RE::TESForm::LookupByID(a_spell));
			const float skyrimDmg = 20.0f * UpgradeMult(i >= 0 ? Arcana::Ups(i) : 0) * 0.45f * Arcana::Power();
			const float whole = std::max(1.0f, Director::RoundHealth());
			return skyrimDmg / whole;
		}
		// (once) the arcane spells' casting art in the hands: Apocalypse's Multivortex's; the top rung, Apocalypse's own Apocalypse,
		// its blinding flash taken off (Noah: "Apocalypse, smaller flash")
		void PatchArcane()
		{
			static bool done = false;
			if (done) return;
			done = true;
			auto* dh = RE::TESDataHandler::GetSingleton();
			const char* apoc = "Apocalypse - Magic of Skyrim.esp";
			auto* vortex = dh->LookupForm<RE::EffectSetting>(0x1A4A49, apoc);
			for (int k = 0; k < 6; ++k) {
				auto* sp = dh->LookupForm<RE::SpellItem>(gen::kArcaneSpell[k], gen::kPlugin);
				auto* m = sp && !sp->effects.empty() && sp->effects[0] ? sp->effects[0]->baseEffect : nullptr;
				if (m && vortex) m->data.castingArt = vortex->data.castingArt, m->data.light = vortex->data.light, m->data.castingSoundLevel = vortex->data.castingSoundLevel;
			}
			if (auto* sp = dh->LookupForm<RE::SpellItem>(0x023316, apoc)) {
				for (auto* e : sp->effects) {
					auto* m = e ? e->baseEffect : nullptr;
					if (!m) continue;
					m->data.imageSpaceMod = nullptr;
					if (auto* pr = m->data.projectileBase; pr && pr->data.explosionType) {
						auto* ex = pr->data.explosionType;
						ex->imageSpaceModifying = nullptr;
						ex->data.imageSpaceRadius = 0.0f;
						ex->data.radius *= 0.6f;
						logger::info("arcane: Apocalypse's flash taken off ({})", ex->GetFormEditorID());
					}
				}
			}
			logger::info("arcane: casting art {}", vortex ? "Apocalypse's Multivortex" : "not found");
		}
		void ArcaneCast(int a_kind, RE::FormID a_spell)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			const float share = ArcaneShare(a_spell);
			const RE::NiPoint3 from = Gun::SwingFrom(), dir = Gun::SwingDir();
			if (a_kind == 0) {  // "Arcane bolt" (Noah: "BOLT A") - a bolt of blue-white light through the first it strikes
				Gun::SlashSpec sl;
				sl.model = "CurseOfAlduin\\fx\\arc_arcane.nif";
				sl.from = from, sl.dir = dir, sl.speed = 1500.0f, sl.range = 2600.0f, sl.radius = 70.0f, sl.scale = 1.0f;
				sl.bolt = true, sl.stopOnHit = true;
				sl.damage = share * Director::RoundHealth() * gen::kRounds.hpScale;
				sl.onHit = [](RE::Actor* a, bool) { if (a) Gun::ElemBurst(5, a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.45f, 0.0f, 0.5f); };
				Gun::Slash(sl);
			} else if (a_kind == 1) {  // "Mana Void orb" - the orb (Noah: "Bolt C, bigger") flies nearly flat, bursts ("ORB BURST B") and drags the dead in
				Gun::Lob("CurseOfAlduin\\fx\\arcane_orb.nif", from, dir * 1700.0f + RE::NiPoint3{ 0, 0, 60.0f }, 1.0f, [share](const RE::NiPoint3& at) {
					Gun::QueueFx("CurseOfAlduin\\fx\\arcane_burst.nif", at, 1.0f, 0.0f, 2.0f, false);
					Gun::BlastSound("lightmassive", at, 0.7f);
					Gun::AreaHurt(at, 300.0f, share, "");
					Gun::CycloneSpec cs;
					cs.model = "CurseOfAlduin\\fx\\arcane_burst.nif";
					cs.from = at, cs.fwd = { 0, 1, 0 }, cs.dist = 0.0f, cs.life = 1.4f, cs.size = 0.9f, cs.fec = -1, cs.share = share * 0.15f;
					cs.burst = [share](const RE::NiPoint3& c) { Gun::AreaHurt(c, 260.0f, share * 0.6f, ""); };
					Gun::ElemCyclone(std::move(cs));
				}, 0.0f, 200.0f);
			} else if (a_kind == 2) {  // "Lunar Tide" (Noah: "RING B") - a ring of arcane light bursts out round you and throws the dead back
				const RE::NiPoint3 at = p->GetPosition();
				Gun::QueueFx("CurseOfAlduin\\fx\\arcane_ring.nif", at, 1.0f, 0.0f, 2.0f, true);
				Gun::BlastSound("lightmassive", at, 0.8f);
				Gun::AreaHurt(at, 480.0f, share * 1.4f, "");
				for (auto* f : NearFoes(at, 480.0f, 20)) Gun::Knock(f, 1.0f);
			}
		}
	}
	// (Noah: "Arcane blade, our own" - "Chain at III") the Spellsword's arcane rung: each blade hit bursts in arcane light, wider at
	// II, and at III the burst leaps to two more of the dead (src/Events.cpp, the blade's hit)
	bool ArcaneBladeHit(RE::Actor* a_target, RE::SpellItem* a_spell)
	{
		const int k = a_spell ? ArcaneKind(a_spell->GetFormID()) : -1;
		if (k < 3 || !a_target) return false;
		const int   tier = k - 2;
		const float share = ArcaneShare(a_spell->GetFormID());
		const RE::NiPoint3 at = a_target->GetPosition();
		Gun::ElemBurst(5, at + RE::NiPoint3{ 0, 0, 70.0f }, tier >= 2 ? 0.8f : 0.5f, 0.0f, 0.5f);
		Gun::AreaHurt(at, tier >= 2 ? 220.0f : 110.0f, share, "");
		if (tier >= 3) {
			int n = 0;
			for (auto* f : NearFoes(at, 650.0f, 6)) {
				if (f == a_target || f->IsDead() || n >= 2) continue;
				++n;
				Gun::LightningArc(at + RE::NiPoint3{ 0, 0, 70.0f }, f->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.05f * n);
				Gun::ElemBurst(5, f->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.5f, 0.05f * n, 0.5f);
				Gun::AreaHurt(f->GetPosition(), 90.0f, share * 0.7f, "");
			}
		}
		return true;
	}
	bool ThrowFlask(RE::FormID a_spell);  // (below: the Alchemist's flasks)
	void TickAlchemy(float a_delta);
	namespace { const char* ArcModel(int a_key); }
	// (Noah: "Destruction - Carian Wands ... Each cast fires a Carian bolt. Tier II: every 3rd cast is a great bolt that pierces")
	// (Noah: "one wand at a time"; "point like a staff, and holding the wand's attack should cast automatically every 2s
	// alternating fire, lightning and ice") held, Skyrim's staff cast keeps the wand pointed; a bolt leaves its tip at once and
	// every second after - fire, lightning, ice in turn. Tier II every 3rd is a great bolt; Tier III every 3rd also looses a
	// crossing volley (two more either side). Each bolt sounds as Skyrim's own Firebolt, Lightning Bolt or Ice Spike leaving the hand.
	namespace
	{
		float g_wandNext = 0.0f;   // seconds to the next bolt while the cast is held
		bool  g_wandHeld = false;
		bool  g_wandButton = false;   // the attack held with the wand drawn (Gun's buttons)
		RE::FormID g_wandEcho = 0;    // the spell the wand just loosed (its cast event, if Skyrim sends one, not taken twice)
		std::chrono::steady_clock::time_point g_wandEchoAt{};
		bool  g_wandEchoArmed = false;
		RE::BGSSoundDescriptorForm* ReleaseSound(RE::FormID a_spell)
		{
			auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(a_spell);
			auto* mg = sp && !sp->effects.empty() && sp->effects[0] ? sp->effects[0]->baseEffect : nullptr;
			if (!mg) return nullptr;
			for (auto& sp2 : mg->effectSounds) if (sp2.id == RE::MagicSystem::SoundID::kRelease && sp2.sound) return sp2.sound;
			return nullptr;
		}
		// each of the five wands' tip along its model's +Y (Fulcimentum's SSS\\*Wand.nif): steel, stalhrim, glass, ebony, daedric
		float WandTipY(RE::TESObjectWEAP* a_w)
		{
			static constexpr float kTipY[5] = { 20.0f, 37.5f, 33.5f, 39.0f, 36.0f };
			return kTipY[std::clamp(a_w ? Kit::TierOfWeapon(a_w) : 0, 0, 4)];
		}
		RE::NiPoint3 WandTip(RE::PlayerCharacter* a_p)
		{
			// (the bolts loosed from the wand's own tip, where its orb hangs, in the body you're seen in)
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (auto* root = a_p->Get3D(cam && cam->IsInFirstPerson())) {
				if (auto* wn = root->GetObjectByName("WEAPON")) {
					const auto at = wn->world.translate + (wn->world.rotate * RE::NiPoint3{ 0.0f, WandTipY(CarianBlade()), 0.0f }) * wn->world.scale;
					if (at.GetDistance(a_p->GetPosition()) < 400.0f) return at;
				}
			}
			if (auto* c = a_p->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand)) {
				if (auto* n = c->GetMagicNode()) {
					const auto at = n->world.translate;
					if (at.GetDistance(a_p->GetPosition()) < 400.0f) return at;
				}
			}
			return Gun::SwingFrom();
		}
	}
	namespace
	{
		float g_wandPulse = 0.0f;   // a bolt just loosed: the tip flares
		// (Noah: "only a small compact light blue orb looking like compact magic that brightens when casting at the very tip of
		// the wand's model on each of the models") our little orb (tools/wandtip.py) hung on the wand itself, at its tip - each
		// of the five wands' tip measured from its model (Fulcimentum's SSS\*Wand.nif: along +Y) - in both bodies
		void TickWandTip(RE::PlayerCharacter* a_p, RE::TESObjectWEAP* a_w, bool a_inHand, bool a_held, float a_delta)
		{
			static RE::NiPointer<RE::NiAVObject> tip[2];
			g_wandPulse = std::max(0.0f, g_wandPulse - a_delta * 3.0f);
			static float t = 0;
			t += a_delta;
			const float glow = a_held ? 1.25f + 0.08f * std::sin(t * 9.0f) : 0.75f + 0.04f * std::sin(t * 2.5f);
			for (int v = 0; v < 2; ++v) {
				auto* root = a_p->Get3D(v == 1);
				auto* wn = a_inHand && root ? root->GetObjectByName("WEAPON") : nullptr;
				auto* node = wn ? wn->AsNode() : nullptr;
				if (!node) {
					if (tip[v] && tip[v]->parent) tip[v]->parent->DetachChild2(tip[v].get());
					continue;
				}
				if (!tip[v]) tip[v] = Gun::LoadModel("CurseOfAlduin\\fx\\wandtip.nif");
				if (!tip[v]) continue;
				if (tip[v]->parent != node) {
					if (tip[v]->parent) tip[v]->parent->DetachChild2(tip[v].get());
					node->AttachChild(tip[v].get(), true);
				}
				tip[v]->local.translate = RE::NiPoint3{ 0.0f, WandTipY(a_w), 0.0f };
				tip[v]->local.rotate = RE::NiMatrix3{};
				tip[v]->local.scale = glow + 0.6f * g_wandPulse;
				RE::NiUpdateData ud;
				tip[v]->Update(ud);
			}
		}
	}
	void WandCast()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* w = CarianBlade();
		if (!p || !w) return;
		const int t = std::max(0, Kit::TierOfWeapon(w));
		static int turn = 0, casts = 0;
		++casts;
		const bool third = casts % 3 == 0;
		const bool great = t >= 2 && third;
		const RE::NiPoint3 dir = Gun::SwingDir();
		const RE::NiPoint3 from = WandTip(p);
		static constexpr int kOrder[3] = { 4, 6, 5 };                                     // fire, lightning, ice (ArcModel's bolts)
		static constexpr RE::FormID kSnd[3] = { 0x0012FCD5, 0x0002DD2A, 0x0002B96C };    // Firebolt, Lightning Bolt, Ice Spike
		const int k = turn++ % 3;
		const int bolt = kOrder[k];
		if (auto* snd = ReleaseSound(kSnd[k])) Audio::Game(snd->GetFormID(), p, great ? 1.0f : 0.85f);
		auto fire = [&](const RE::NiPoint3& d, float sizeMul, float shareMul) {
			Gun::SlashSpec sl;
			sl.model = ArcModel(bolt);
			sl.from = from;
			sl.dir = d;
			sl.roll = Gun::SwingRoll(d);
			sl.range = 1400.0f + 200.0f * std::min(t, 3);
			sl.bolt = true, sl.speed = 1150.0f;
			sl.scale = (1.1f + 0.15f * std::min(t, 3)) * sizeMul;
			sl.share = BladeShare(w) * (0.7f + 0.3f * Arcana::Power()) * (t >= 1 ? UpgradeMult(t) / std::max(0.01f, UpgradeMult(1)) : 0.6f) * shareMul;
			sl.flingKill = 16.0f;
			sl.onHit = [bolt](RE::Actor* a, bool killed) {
				if (!a) return;
				static constexpr const char* kHit[3] = { "fireball", "frost", "runeshock" };
				Gun::BlastSound(kHit[std::clamp(bolt - 4, 0, 2)], a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.6f);
				if (!killed) Gun::ElemBurst(bolt - 4, a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.35f, 0.0f, 0.4f);
			};
			Gun::Slash(sl);
		};
		fire(dir, great ? 1.8f : 1.0f, great ? 2.0f : 1.0f);
		if (t >= 3 && third) {   // the crossing volley: two more, either side
			for (const float a : { -0.18f, 0.18f }) {
				RE::NiPoint3 d{ dir.x * std::cos(a) - dir.y * std::sin(a), dir.x * std::sin(a) + dir.y * std::cos(a), dir.z };
				fire(d, 1.0f, 0.8f);
			}
		}
	}
	// every frame: the held cast's bolts, and the wand kept to one, in the right hand
	// (Noah: "only the ascent level wand is doing the casting animation. Can you make sure it's all of them?") the wand carries
	// its cast from the moment the game loads, at every tier - not only once the kit has been synced
	void WandReady()
	{
		auto* w = CarianBlade();
		auto* en = RE::TESDataHandler::GetSingleton()->LookupForm<RE::EnchantmentItem>(gen::kCarianWandEnch, gen::kPlugin);
		if (!w || !en) return;
		w->formEnchanting = en, w->amountofEnchantment = 100;
	}
	void WandButton(bool a_down) { g_wandButton = a_down; }
	bool WandInHand()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		return p && CarianBlade() && p->GetEquippedObject(false) == CarianBlade() && p->AsActorState()->IsWeaponDrawn();
	}
	void TickWand(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* w = CarianBlade();
		if (!p || !w || !p->Is3DLoaded()) return;
		// (Noah: "the wands shouldn't have an element animation on the wand in hand") its held cast hangs no casting art or light
		// on the hand - the little blue orb at its tip is its only look (TickWandTip)
		if (static bool bare = false; !bare && w->formEnchanting && !w->formEnchanting->effects.empty() && w->formEnchanting->effects[0]) {
			bare = true;
			if (auto* me = w->formEnchanting->effects[0]->baseEffect) {
				me->data.castingArt = nullptr, me->data.light = nullptr, me->data.enchantShader = nullptr, me->data.enchantEffectArt = nullptr;
				logger::info("classes: the Carian Wand's cast shows nothing on the hand - its tip's orb only");
			}
		}
		bool held = false;
		const bool inHand = p->GetEquippedObject(false) == w && p->AsActorState()->IsWeaponDrawn();
		if (!inHand) g_wandButton = false;
		if (inHand) {
			auto* c = p->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand);
			const bool casting = c && c->currentSpell && w->formEnchanting && c->currentSpell == w->formEnchanting &&
			                     (c->state.get() == RE::MagicCaster::State::kCasting || c->state.get() == RE::MagicCaster::State::kCharging);
			// (Noah: "Letting go of casting should stop the wand from casting and holding attack should start the casting") the
			// attack button alone says: held, it casts; let go, it stops - Skyrim's own staff cast cut short with it
			held = g_wandButton;
			if (!held && casting && c) c->InterruptCast(false);
			if (static float said = 0; g_wandButton && (said -= a_delta) <= 0) {
				said = 1.0f;
				logger::info("classes: the Carian Wand held - its cast {} (state {}, spell {:08X})", casting ? "plays" : "doesn't play",
					c ? static_cast<int>(c->state.get()) : -1, c && c->currentSpell ? c->currentSpell->GetFormID() : 0);
			}
		}
		// (Noah: "I can spam the wand ... so it only fires after holding the cast") the cast held 0.3 s before the first, and
		// never two within half a second - tapping looses nothing
		static float heldFor = 0.0f;
		heldFor = held ? heldFor + a_delta : 0.0f;
		g_wandNext = std::max(0.0f, g_wandNext - a_delta);
		if (held && heldFor >= 0.3f) {
			if (g_wandNext <= 0.0f) {   // (Noah: "attack every half a second without being able to spam them. You have to hold them down")
				g_wandNext = 0.5f;
				g_wandPulse = 1.0f;
				// (Noah: "It's supposed to cast whatever spell I have on my left hand ... it shoots 1 per second without using
				// mana") the spell in your left hand, loosed from the wand's tip, free; no spell there: the Carian bolts
				auto* left = p->GetEquippedObject(true);
				auto* sp = left ? left->As<RE::SpellItem>() : nullptr;
				auto* c = p->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand);
				if (sp && c && sp->GetSpellType() == RE::MagicSystem::SpellType::kSpell) {
					// (Noah: "the wand animation stops when I cast a spell on my other hand" - the left spell was cast through the
					// right hand's own caster, cutting short the wand's held cast each time) an aimed spell is launched from the
					// wand's tip itself, along your aim; any other through the instant caster - the right hand left to its cast
					auto* mg = !sp->effects.empty() && sp->effects[0] ? sp->effects[0]->baseEffect : nullptr;
					auto* proj = mg && sp->GetDelivery() == RE::MagicSystem::Delivery::kAimed ? mg->data.projectileBase : nullptr;
					if (proj) {
						const RE::NiPoint3 dir = Gun::SwingDir();
						RE::Projectile::ProjectileRot rot{ -std::asin(std::clamp(dir.z, -1.0f, 1.0f)), std::atan2(dir.x, dir.y) };
						RE::Projectile::LaunchData ld(proj, p, WandTip(p), rot);
						ld.useOrigin = true, ld.autoAim = false, ld.spell = sp;
						ld.castingSource = RE::MagicSystem::CastingSource::kRightHand;
						RE::ProjectileHandle h;
						RE::Projectile::Launch(&h, ld);
					} else if (auto* ic = p->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
						ic->CastSpellImmediate(sp, false, nullptr, 1.0f, false, 0.0f, p);
					}
					g_wandEcho = sp->GetFormID(), g_wandEchoAt = std::chrono::steady_clock::now();
					OnCast(sp->GetFormID());   // (a spell of ours: its own cast, as if from the hand - Skyrim's own cast event for it, if any, ignored)
					g_wandEchoArmed = true;
					if (static int n = 0; n++ % 20 == 0) logger::info("classes: the Carian Wand looses {} (no magicka)", sp->GetName());
				} else {
					WandCast();
				}
			}
		}
		g_wandHeld = held;
		TickWandTip(p, w, inHand, held, a_delta);
		// (Noah: "only allowed to hold one wand at a time") never in the left hand, never two in the pack
		if (ItemMenusOpen()) return;
		static float check = 0.0f;
		if ((check -= a_delta) > 0) return;
		check = 0.5f;
		auto* em = RE::ActorEquipManager::GetSingleton();
		if (!w->formEnchanting || w->amountofEnchantment == 0) WandReady();
		if (em && p->GetEquippedObject(true) == w) {
			em->UnequipObject(p, w, nullptr, 1, RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F43), false, true, false, true);
			Hud::QuestBanner("The Carian Wand is held in the right hand only", 2.5f);
		}
		if (const int n = p->GetItemCount(w); n > 1) {
			p->RemoveItem(w, n - 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			logger::info("classes: {} spare Carian Wand(s) taken away - one wand at a time", n - 1);
		}
	}
	bool OnCast(RE::FormID a_spell)
	{
		if (g_wandEchoArmed && a_spell == g_wandEcho && std::chrono::steady_clock::now() - g_wandEchoAt < std::chrono::milliseconds(150)) {
			g_wandEchoArmed = false;
			return true;
		}
		PatchArcane();
		if (const int k = ArcaneKind(a_spell); k >= 0 && k < 3) { ArcaneCast(k, a_spell); return true; }
		if (ThrowFlask(a_spell)) return true;
		if (CarianBlade() && CarianBlade()->formEnchanting && a_spell == CarianBlade()->formEnchanting->GetFormID()) return true;   // (its bolts: TickWand, while held)
		if (auto* horn = WyrmSpell(); horn && a_spell == horn->GetFormID()) {
			WyrmCast();   // (the Call of the Violet Wyrm: WyrmHorn.inl)
			return true;
		}
		auto* tome = TomeSpell();
		if (!tome || a_spell != tome->GetFormID()) return false;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return true;
		// (Noah: "no cast cooldown") as often as your magicka allows
		const auto foes = NearFoes(p->GetPosition(), 2600.0f, 6);
		if (foes.empty()) {
			Hud::Banner("The Six Fires find nothing to burn", 2.0f);
			return true;
		}
		// (the class audit: nine casts on the same bodies within a quarter second took one share between them - the wonder hit
		// softer than a plain bolt) its six fires each burst on their foe for a real share, every 8 s; only the six element
		// ladders (Fire, Frost, Shock, Sun, Blood, Runes), as its name says
		// (Noah: "the tome of six fires needs to be updated with the current max tier for all the element spells") every fire at
		// the top of the curve - the ladder's last rung, fully Ascended (a kit item's highest tier) - whatever your own are
		int k = 0;
		const bool legend = AfterSovngarde();
		const float pf = 0.7f + 0.3f * Arcana::Power();
		const int kTopTier = legend ? kWonderUps + 1 : kWonderUps;  // (Noah: "the wonder weapons should be the strongest weapons for their class") above any kit spell's highest tier (13)
		for (auto* f : foes) {
			if (f && !f->IsDead()) Gun::AreaHurt(f->GetPosition(), 150.0f, 0.6f * UpgradeMult(kTopTier) * pf, "");
		}
		for (const auto& s : coffer::kSpells) {
			if (s.cls != coffer::kDestruction || s.ladder > 5) continue;
			const int idx = static_cast<int>(&s - coffer::kSpells.data());
			RE::Actor* t = foes[k % foes.size()];
			const bool top = s.rung == s.rungs, third = legend && s.rung == 3;
			if (!top && !third) continue;
			// the spell held at its highest tier until its fire has landed (a few seconds), then given back as it was
			if (auto it = std::find_if(g_tomeHeld.begin(), g_tomeHeld.end(), [&](auto& h) { return h.first == idx; }); it == g_tomeHeld.end()) g_tomeHeld.push_back({ idx, Arcana::Level(idx) });
			Arcana::SetLevel(idx, kTopTier - Arcana::TierOfRung(idx));
			if (top) CastFrom(p, Arcana::Spell(idx), t), ++k;
			else CastFrom(p, Arcana::Spell(idx), foes[(k + 1) % foes.size()]);
		}
		g_tomeHold = 3.0f;
		Gun::FxBlast("fireball", p->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f });
		logger::info("classes: the Six Fires on {} foes{}", foes.size(), legend ? " (Legendary)" : "");
		return true;
	}


	// every frame: the spells' costs, the summons, the flasks, the Staff of Magnus, cooldowns
	void TickFast(float a_delta)
	{
		// every other spell you know (heroes and armies, tomes, other mods' spells): its cost on the same curve,
		// worked from its own first cost (kept, so it is never drawn in twice)
		if (static float costT = 0; (costT -= a_delta) <= 0) {
			costT = 2.0f;
			static std::unordered_map<RE::SpellItem*, float> base;
			if (auto* p = RE::PlayerCharacter::GetSingleton()) {
				for (auto* sp : p->GetActorRuntimeData().addedSpells) {
					if (!sp || sp->GetSpellType() != RE::MagicSystem::SpellType::kSpell || Arcana::IndexOf(sp) >= 0) continue;
					if (sp->GetCastingType() == RE::MagicSystem::CastingType::kConcentration) continue;  // (a stream's cost is a second's: already small)
					if (sp == WyrmSpell() || sp == TomeSpell()) continue;  // (the wonders: 100, as Noah set them)
					auto [it, fresh] = base.try_emplace(sp, 0.0f);
					if (fresh) it->second = sp->CalculateMagickaCost(nullptr);
					if (it->second <= 50.0f) continue;
					const int want = CapSpellCost(it->second);
					if (!sp->data.flags.all(RE::SpellItem::SpellFlag::kCostOverride) || sp->data.costOverride != want) {
						sp->data.costOverride = want;
						sp->data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);
					}
				}
			}
		}

		TickSummons(a_delta);
		TickAlchemy(a_delta);
		TickWand(a_delta);
		TickMagnus(a_delta);
		g_tomeCool = std::max(0.0f, g_tomeCool - a_delta);
		if (g_tomeHold > 0 && (g_tomeHold -= a_delta) <= 0) {
			for (auto& [idx, lv] : g_tomeHeld) Arcana::SetLevel(idx, lv);
			g_tomeHeld.clear();
		}
		g_bladeCool = std::max(0.0f, g_bladeCool - a_delta);
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
		// (0xA2-0xA4, 0xA9: the old crossbows', bows' and Conjurer's Staff's records - no longer written; an older save's are read past)
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
		g_class = -1, g_kit.fill(false);
		g_wStep.fill(0), g_wKills.fill(0), g_wTowns.fill(0), g_lastHit.clear();
		for (auto& s : g_stash) s.clear();
		// (the audit: a new game kept the last game's chests, stashes and guide - its first class switch moved your gear
		// into an unrelated reference) all of it cleared before anything is read
		g_chest.fill(0), g_slotStash = {}, g_guide = 0;
		for (auto& w : g_wornStash) w.clear();
		for (auto& ps : g_perkStash) ps.clear();
		for (auto& e : g_extra) e.clear();
		for (auto& e : g_spellStash) e.clear();
		for (auto& e : g_held) e.clear();
		g_mergeDone = false;
		g_lanternGiven = false;
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
		// (the old crossbows', bows' and Conjurer's Staff's records in an older save: read past)
		if (k < a_v.size() && a_v[k] == 0xA2 && k + 3 <= a_v.size()) k += 3;
		if (k + 1 < a_v.size() && a_v[k] == 0xA3) k += 2 + 2 * static_cast<std::size_t>(a_v[k + 1]);
		if (k + 1 < a_v.size() && a_v[k] == 0xA4) k += 2;
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
				// (an older save's crossbow reforges: read past)
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
		g_open = false, g_mastery.fill(0);
		g_summonMarks.clear(), g_pendingSummon.clear(), g_ownSummons.clear(), g_thralls.clear(), g_pendingThrall.clear(), g_lastBlade.clear(), g_powerHit.clear();
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
	// ---- (Noah: "remove everything from the Ranger class ... and replace it with the Alchemist class") the Alchemist ----
	// Its flasks are hand spells: each cast lobs a tumbling flask (our projectile, carrying nothing) along where you aim, a
	// little up so it arcs; where it comes down - a body, the ground, a wall - it bursts (FlaskBurst). The damage is the class
	// spells' one curve; Tier I leaves a pool of it a few seconds, Tier II splits it into three, Legendary a far greater
	// blast. The Philosopher's Flask (the class's wonder) hangs a cloud of molten gold where it shatters (TickGoldClouds)
	RE::TESBoundObject* FlaskItem(int a_el)
	{
		if (a_el < 0 || a_el >= static_cast<int>(gen::kFlaskItem.size())) return nullptr;
		auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(gen::kFlaskItem[a_el], gen::kPlugin);
		return f ? f->As<RE::TESBoundObject>() : nullptr;
	}
	void GiveFlask(int a_el)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* it = FlaskItem(a_el);
		if (!p || !it || p->GetItemCount(it) > 0) return;
		Forms::Give(p, it, 1);
		logger::info("classes: {} in your pack", it->GetName());
	}
	RE::TESObjectWEAP* AlchemistKnife()
	{
		auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(gen::kAlchKnife, gen::kPlugin);
		return f ? f->As<RE::TESObjectWEAP>() : nullptr;
	}
	// (Noah: "the new weapons in the classes" - the Summoner a quarterstaff, the Spellsword a katana, each a better material with
	// each tier) a class weapon takes on an Animated Armoury weapon's whole look and feel: its models, how it's held and swung
	// (the animation family its keywords call up), its reach and speed, its sounds - our record, our powers
	bool AdoptLook(RE::TESObjectWEAP* a_w, const RE::TESObjectWEAP* a_s)
	{
		if (!a_w || !a_s) return false;
		const bool changed = std::string_view(a_w->GetModel()) != std::string_view(a_s->GetModel());
		a_w->SetModel(a_s->GetModel());
		a_w->firstPersonModelObject = a_s->firstPersonModelObject;
		a_w->weaponData.animationType = a_s->weaponData.animationType;
		a_w->weaponData.reach = a_s->weaponData.reach;
		a_w->weaponData.speed = a_s->weaponData.speed;
		a_w->SetEquipSlot(a_s->GetEquipSlot());
		a_w->attackSound = a_s->attackSound, a_w->attackSound2D = a_s->attackSound2D, a_w->attackLoopSound = a_s->attackLoopSound;
		a_w->attackFailSound = a_s->attackFailSound, a_w->idleSound = a_s->idleSound, a_w->equipSound = a_s->equipSound;
		a_w->unequipSound = a_s->unequipSound, a_w->impactDataSet = a_s->impactDataSet;
		for (std::uint32_t i = 0; i < a_s->numKeywords; ++i) {
			auto* k = a_s->keywords[i];
			if (!k || a_w->HasKeyword(k)) continue;
			const std::string_view ed(k->GetFormEditorID() ? k->GetFormEditorID() : "");
			auto* file = k->GetFile(0);
			if ((file && std::string_view(file->GetFilename()) == "NewArmoury.esp") || ed.starts_with("WeapType")) a_w->AddKeyword(k);
		}
		return changed;
	}
	// (Noah: "the dragon slayer swords gold color animations aren't visible ... change this weapon to ... the glass katana" -
	// "Yes, one-handed katana") the Dragon Slayer wears Animated Armoury's Glass Katana: its model, swings and reach; its gold
	// dragonfire looks and powers its own
	void SlayerLook()
	{
		auto* w = Slayer();
		auto* f = Lookup(0x344731, "NewArmoury.esp");
		auto* s = f ? f->As<RE::TESObjectWEAP>() : nullptr;
		if (!w || !s) { logger::warn("classes: the Dragon Slayer's katana look not found (Animated Armoury's Glass Katana)"); return; }
		if (AdoptLook(w, s)) logger::info("classes: the Dragon Slayer takes the look of {}", s->GetName());
	}
	// a weapon made a scythe: a model of Battle Scythe's, held and swung two-handed as a battleaxe, with our scythe keyword (the
	// MCO scythe moveset's); any whip's keywords Animated Armoury left on it taken off
	bool ScytheLook(RE::TESObjectWEAP* a_w, const char* a_model)
	{
		if (!a_w || !a_model) return false;
		const bool changed = std::string_view(a_w->GetModel()) != std::string_view(a_model);
		a_w->SetModel(a_model);
		a_w->firstPersonModelObject = nullptr;   // (the same model in first person)
		a_w->weaponData.animationType = RE::WEAPON_TYPE::kTwoHandAxe;
		a_w->weaponData.reach = 1.4f, a_w->weaponData.speed = 0.8f;
		if (auto* both = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F45)) a_w->SetEquipSlot(both);
		std::vector<RE::BGSKeyword*> drop;
		for (std::uint32_t i = 0; i < a_w->numKeywords; ++i) {
			auto* k = a_w->keywords[i];
			if (!k) continue;
			auto* file = k->GetFile(0);
			const std::string_view ed(k->GetFormEditorID() ? k->GetFormEditorID() : "");
			if ((file && std::string_view(file->GetFilename()) == "NewArmoury.esp") || (ed.starts_with("WeapType") && ed != "WeapTypeBattleaxe")) drop.push_back(k);
		}
		if (!drop.empty()) a_w->RemoveKeywords(drop);
		if (auto* k = RE::TESForm::LookupByID<RE::BGSKeyword>(0x06D932); k && !a_w->HasKeyword(k)) a_w->AddKeyword(k);   // WeapTypeBattleaxe
		if (auto* k = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSKeyword>(gen::kScytheKeyword, gen::kPlugin); k && !a_w->HasKeyword(k)) a_w->AddKeyword(k);
		if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectWEAP>(0x00013980); ref && ref->weaponData.animationType == RE::WEAPON_TYPE::kTwoHandAxe) {   // (the Iron Battleaxe's swings' sounds)
			a_w->attackSound = ref->attackSound, a_w->attackSound2D = ref->attackSound2D, a_w->attackFailSound = ref->attackFailSound;
			a_w->equipSound = ref->equipSound, a_w->unequipSound = ref->unequipSound, a_w->impactDataSet = ref->impactDataSet;
		}
		return changed;
	}
	// (Noah: "for the quarter staffs, wands, whips, and Rapier make it steel/iron, ebony, daedric, dragon bone for ascent
	// upgrades") Base and Tier I steel or iron, Tier II ebony, Tier III daedric, Ascended dragonbone
	bool AdoptTierLook(RE::TESObjectWEAP* a_w, int a_tier)
	{
		struct Src { const char* plugin; std::uint32_t id[4]; };
		static constexpr Src kQStaff{ "NewArmoury.esp", { 0x18B760, 0x18B75B, 0x18B757, 0x18B763 } };   // the Summoner's quarterstaff
		static constexpr Src kWhip{ "NewArmoury.esp", { 0x31BDDE, 0x31BDE4, 0x31BDE5, 0x31BDE6 } };     // the Alchemist's whip
		static constexpr Src kRapier{ "NewArmoury.esp", { 0x019D0D, 0x019D12, 0x019D13, 0x01EE23 } };   // the Spellsword's rapier
		const int c = SwordClass(a_w);
		// (Noah: "for the wands make it Steel starting, Stalhrim for tier 1, Glass tier 2, ebony tier 3, and daedric ascend")
		if (c == 1) {
			static constexpr std::uint32_t kWand[5] = { 0x000805, 0x000812, 0x000807, 0x00B041, 0x000816 };
			auto* f = Lookup(kWand[std::clamp(a_tier, 0, 4)], "Fulcimentum - More Staves and Wands of Skyrim.esp");
			auto* s = f ? f->As<RE::TESObjectWEAP>() : nullptr;
			if (!s) return false;
			const bool changed = AdoptLook(a_w, s);
			if (changed) logger::info("classes: the {} takes the look of {}", a_w->GetName(), s->GetName());
			return changed;
		}
		// (Noah: the whip out, a scythe in - "Forsworn -> Falmer -> Dwarven -> Glass -> Dragon Priest") Battle Scythe's models, read
		// from the install, swung with the MCO scythe moveset (its DAR folders ask for our scythe keyword)
		if (c == 2) {
			// (Noah: "all scythes should be smaller" - 60%: tools/scythes.sh makes them on your machine from your own Battle Scythe)
			static constexpr const char* kScythe[5] = { "CurseOfAlduin\\scythes\\forsworn.nif", "CurseOfAlduin\\scythes\\falmer.nif", "CurseOfAlduin\\scythes\\dwarven.nif",
				"CurseOfAlduin\\scythes\\glass.nif", "CurseOfAlduin\\scythes\\dragonpriest.nif" };
			const bool changed = ScytheLook(a_w, kScythe[std::clamp(a_tier, 0, 4)]);
			if (changed) logger::info("classes: the {} takes the look of {}", a_w->GetName(), kScythe[std::clamp(a_tier, 0, 4)]);
			return changed;
		}
		(void)kWhip;
		const Src* src = c == 0 ? &kQStaff : c == 3 ? &kRapier : nullptr;
		if (!src) return false;
		const int i = a_tier <= 1 ? 0 : a_tier == 2 ? 1 : a_tier == 3 ? 2 : 3;
		auto* f = Lookup(src->id[i], src->plugin);
		auto* s = f ? f->As<RE::TESObjectWEAP>() : nullptr;
		if (!s) return false;
		const bool changed = AdoptLook(a_w, s);
		if (changed) logger::info("classes: the {} takes the look of {}", a_w->GetName(), s->GetName());
		return changed;
	}
	// (Animated Armoury's whips swing their own chain by a variable its script sets on equipping - ours carry no script, so
	// it's set here, through the same Papyrus call)
	void WhipEquipped(RE::Actor* a_a, bool a_left)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm || !a_a) return;
		auto* policy = vm->GetObjectHandlePolicy();
		const auto h = policy ? policy->GetHandleForObject(RE::Actor::FORMTYPE, a_a) : 0;
		if (!h) return;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
		auto* args = RE::MakeFunctionArguments(std::string(a_left ? "EqpInLeft" : "EqpInRight"), 1.0f);
		vm->DispatchMethodCall(h, "Actor", "SetSubGraphFloatVariable", args, cb);
	}

	namespace
	{
		int g_lastFlaskEl = 0;  // the last flask thrown
		constexpr int kFlaskBurst[7] = { 0, 1, 2, 7, 10, 5, 8 };   // (Holy Water: its own golden light - Gun::ElemBurst 10)  // a flask's burst look (Gun::ElemBurst): fire frost shock poison sun blood gold
		struct FlaskShot { RE::ProjectileHandle h; int el = 0, tier = 0; RE::NiPoint3 last; bool seen = false, child = false; float life = 0; };
		std::vector<FlaskShot> g_flasks;
		struct GoldCloud { RE::NiPoint3 at; float left = 0, tick = 0, radius = 450, puff = 0; bool legend = false; std::unordered_set<RE::FormID> gilded; };
		std::vector<GoldCloud> g_gold;
		std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_gildedUntil;   // (the Philosopher's Flask's statues: until when)
		// (Noah, the flasks redone: Tier I "Bigger burst radius" - half again as far)
		float FlaskRadius(int a_tier) { return 230.0f * (a_tier >= 1 ? 1.5f : 1.0f) * (Kit::ArmourTier(coffer::kAlchemist) >= 2 ? 1.2f : 1.0f) * Perks::FlaskRadiusMult(); }
		float FlaskShare(int a_tier) { return 0.5f * UpgradeMult(a_tier) * (0.7f + 0.3f * Arcana::Power()) * (a_tier >= 3 ? 1.3f : 1.0f); }
		void FlaskBurst(const FlaskShot& f, const RE::NiPoint3& a_at);
		void LaunchFlask(int a_el, int a_tier, const RE::NiPoint3& a_from, const RE::NiPoint3& a_vel, bool a_child)
		{
			static constexpr const char* kModel[7] = { "fire", "frost", "shock", "poison", "sun", "blood", "philosopher" };
			FlaskShot shot{ {}, a_el, a_tier, a_from, false, a_child, 0.0f };
			// (Noah: "flasks are thrown faster and straighter so it's easier to land them on enemies") a flatter, quicker flight: a
			// thrown flask falls at half the pull a stone would (a chain-reaction's child keeps the old arc)
			Gun::Lob(std::string("CurseOfAlduin\\alchemist\\flask_") + kModel[a_el] + ".nif", a_from, a_vel, a_child ? 0.7f : 1.0f,
				[shot](const RE::NiPoint3& at) { FlaskBurst(shot, at); }, 12.0f, a_child ? 900.0f : 450.0f);
		}
		void FlaskBlast(int a_el, int a_tier, const RE::NiPoint3& a_at, float a_size, float a_share, float a_radius)
		{
			// (the element's own spell blast: Skyrim's explosion for it - FxBlast below - or, for Holy Water, the effects map's sun)
			// (Noah: flasks "way too big ... take up my whole screen"; "Burst only, no pool") the element's own spell impact (fxmap.ini
			// burst_*): small by make - a whole Skyrim explosion keeps its flames' size however it's scaled
			Gun::ElemBurst(kFlaskBurst[std::clamp(a_el, 0, 6)], a_at + RE::NiPoint3{ 0, 0, 10.0f }, std::clamp(a_size, 0.8f, 1.6f), 0.0f, 0.9f);
			// (Noah: "each flask to have its own impact sound effect, depending on the element") the bottle breaking and its element
			// let loose (tools/alchsfx.py hits)
			// (Noah: "they sound really toony - use the spell sound effect that the potion is casting ... with a cork sound effect
			// on impact ... as hyper realistic as possible") Skyrim's own: the element's spell striking (its explosion's sound),
			// the glass of the bottle, and a cork's pop
			static constexpr const char* kSpellSnd[7] = { "fireball", "frost", "runeshock", "poisongas", "light", "bloodplague", "lightmassive" };
			Gun::BlastSound(kSpellSnd[std::clamp(a_el, 0, 6)], a_at, 1.0f);
			// (Noah: "the impact sound fx sounds like drinking a potion ... the impact a glass breaking sound with the element of the
			// flask ... fire impact should sound like flames the duration of the effect and the glass break on impact") no potion's
			// cork or gulp: the glass shattering (Skyrim's oil-lamp trap breaking), the element's own strike, and a fire flask's
			// flames crackling on where it burns for as long as its fire lasts
			Audio::Mine(gen::Sound::alch_glass, a_at, 1.0f);
			if (a_el == 0) {
				static std::uint64_t n = 0;
				const std::uint64_t key = 0xF1A5C000ull + (n++ % 64);
				Audio::LoopAt(key, gen::Sound::alch_fireloop, a_at, 0.85f);
				Tasks::Later(3.0f, [key]() { Audio::LoopStop(key, 700); });
			}
			logger::info("alchemist: a flask breaks - glass, Skyrim's {} sound{}", kSpellSnd[std::clamp(a_el, 0, 6)], a_el == 0 ? ", its flames for 3 s" : "");
			static constexpr const char* kVanilla[6] = { "fireball", "frost", "runeshock", "poisongas", "", "bloodplague" };
			(void)kVanilla;
			auto* p = RE::PlayerCharacter::GetSingleton();
			const auto caught = NearFoes(a_at, a_radius, 16);
			if (g_flaskMark.size() > 400) g_flaskMark.clear();
			for (auto* f : caught) g_flaskMark[f->GetFormID()] = std::chrono::steady_clock::now();
			const float share = a_share * (a_el == 4 ? 1.25f : 1.0f);  // (Holy Water: the dead burn in it)
			Gun::AreaHurt(a_at, a_radius, share, "");
			if (!p || caught.empty()) return;
			auto* av = p->AsActorValueOwner();
			if (false && a_el == 5) av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * std::min(0.06f, 0.012f * static_cast<float>(caught.size())));  // (Bloodbloom: what it drains comes back)
			if (Kit::ArmourTier(coffer::kAlchemist) >= 3) {  // (the Drifter's set, Tier III: flask kills heal you)
				std::vector<RE::ActorHandle> hs;
				for (auto* f : caught) hs.push_back(f->GetHandle());
				Tasks::Later(0.25f, [hs]() {
					auto* pl = RE::PlayerCharacter::GetSingleton();
					if (!pl) return;
					int dead = 0;
					for (auto& h : hs) if (auto a = h.get(); a && a->IsDead()) ++dead;
					if (dead) pl->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, pl->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth) * std::min(0.1f, 0.02f * static_cast<float>(dead)));
				});
			}
		}
		// (Noah: "the alchemist's mace ... a shadow flame mace") its blows themselves do nothing more - its swings carry it
		// (MaceSwing, below): every swing from Tier I, contact or not
		void MaceHit(RE::PlayerCharacter*, RE::Actor*, bool) {}
		// Shadowflame: the fire's burn, at twice its damage - violet fire clinging to the one it's on. Caught again, it's
		// renewed, never stacked
		// (Noah: "swinging the mace, the fire effects should look like real fire that is purple") Skyrim's own purple fire
		// (Nocturnal's, from the game's files), set burning a moment where it's wanted
		struct Shadowflame { RE::ActorHandle who; float left = 0, tick = 0; };
		std::vector<Shadowflame> g_shadow;
		constexpr float kShadowSeconds = 5.0f;   // (Noah: "2x flame damage for 5 seconds")
		bool g_shadowChain = false;               // the Shadowflame Whip's Tier III: the burning dead explode (set by TickSwords)
		int  g_shadowTier = 1;
		// (Noah: "it should set enemies on fire with purple flames") the burning one wrapped in violet fire for as long as it burns
		void ShadowCloak(RE::Actor* a_f)
		{
			static auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kShadowburnShader, gen::kPlugin);
			if (fx && a_f && a_f->Is3DLoaded()) a_f->ApplyEffectShader(fx, kShadowSeconds, nullptr, false, false, nullptr, false);
		}
		// (Noah: "their death effect animations should use the mod we already do from dying by fire - the enemy should slowly
		// burn to a char just before despawning") one of the Shadowflame's dead: it died a fire death (its burn's killing blow
		// tells Frozen Electrocuted Combustion so - the body chars), and the violet fire goes on burning it down until it's
		// nearly time for the body to go
		void CorpseBurn(RE::Actor* a_f)
		{
			static auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kShadowburnShader, gen::kPlugin);
			if (fx && a_f && a_f->Is3DLoaded()) a_f->ApplyEffectShader(fx, std::max(2.0f, Settings::CorpseSeconds() * 0.8f), nullptr, false, false, nullptr, false);
		}
		void Shadowburn(RE::Actor* a_f, int a_t)
		{
			if (!a_f || a_f->IsDead()) return;
			const float perSec = Director::RoundHealth() * gen::kRounds.hpScale * 0.06f * 2.0f * UpgradeMult(a_t) * (0.7f + 0.3f * Arcana::Power());
			for (auto& s : g_shadow) if (s.who.get().get() == a_f) {   // (caught again: renewed to its full 5 s, never doubled)
				if (s.left < kShadowSeconds - 0.25f) Gun::Burn(a_f, perSec, kShadowSeconds - s.left);
				s.left = kShadowSeconds;
				ShadowCloak(a_f);
				return;
			}
			// the fire's own burn (a fire flask's or a meteor arrow's: 6% of one of the night's dead a second), doubled,
			// grown with the sword's tier and your Arcana
			Gun::Burn(a_f, perSec, kShadowSeconds);
			g_shadow.push_back({ a_f->GetHandle(), kShadowSeconds, 0.0f });
			ShadowCloak(a_f);
		}
		void TickShadow(float a_delta)
		{
			std::vector<RE::NiPoint3> blasts;
			for (auto& s : g_shadow) {
				auto a = s.who.get();
				if (!a) { s.left = 0; continue; }
				if (a->IsDead()) {   // (Noah, the Alchemist's Tier III: "chain reaction") died burning: it bursts and sets those round it alight
					if (g_shadowChain && s.left > 0) blasts.push_back(a->GetPosition());
					CorpseBurn(a.get());
					s.left = 0;
					continue;
				}
				s.left -= a_delta;
				// (Noah: "remove the purple ash pile" - Nocturnal's fire flaring at their feet brought its bed of embers, a pile of
				// ash with violet flames on it) the violet fire on the body alone
			}
			std::erase_if(g_shadow, [](const Shadowflame& s) { return s.left <= 0; });
			for (const auto& at : blasts) {
				Gun::ElemBurst(9, at + RE::NiPoint3{ 0, 0, 40.0f }, 1.2f, 0.0f, 0.6f);
				Gun::Ring("purple", at, 30.0f, 300.0f, 0.5f);
				Gun::FxBlast("fireball", at + RE::NiPoint3{ 0, 0, 40.0f });
				Gun::AreaHurt(at, 300.0f, 0.3f * UpgradeMult(g_shadowTier), "");
				for (auto* f : NearFoes(at, 300.0f, 12)) Shadowburn(f, g_shadowTier);
			}
		}
		void FlaskBurst(const FlaskShot& f, const RE::NiPoint3& a_at)
		{
			if (f.el == 6) {  // (Noah: the Philosopher's Flask made the Mutagen Gauntlet - "Bouncing cluster", "6 bounces, fixed order") the cluster
				// bounces from one of the dead to the next, a flask of each element bursting on each in turn: fire, frost, shock, acid,
				// holy, void; once the three dragons fall each burst is its Legendary form ("Bigger bursts")
				const bool legend = AfterSovngarde();
				Gun::BlastSound("lightmassive", a_at, 0.8f);
				static constexpr const char* kModel[6] = { "fire", "frost", "shock", "poison", "sun", "blood" };
				std::vector<RE::ActorHandle> hit;
				RE::NiPoint3 from = a_at;
				float delay = 0.0f;
				for (int el = 0; el < 6; ++el) {
					RE::Actor* next = nullptr;
					float best = 1e9f;
					for (auto* fo : NearFoes(from, 1100.0f, 24)) {
						if (fo->IsDead() || std::any_of(hit.begin(), hit.end(), [fo](const RE::ActorHandle& h) { return h.get().get() == fo; })) continue;
						if (const float d = fo->GetPosition().GetDistance(from); d < best) best = d, next = fo;
					}
					RE::NiPoint3 to = next ? next->GetPosition() : from + RE::NiPoint3{ std::cos(el * 1.05f) * 260.0f, std::sin(el * 1.05f) * 260.0f, 0.0f };
					if (next) hit.push_back(next->GetHandle());
					const RE::NiPoint3 a0 = from + RE::NiPoint3{ 0, 0, 40.0f };
					constexpr float kFly = 0.35f;
					RE::NiPoint3 vel = (to - a0) * (1.0f / kFly);
					vel.z += 0.5f * 900.0f * kFly;
					const int tier = legend ? 3 : 1;
					Tasks::Later(delay, [el, tier, a0, vel]() {
						FlaskShot shot{ {}, el, tier, a0, false, false, 0.0f };
						Gun::Lob(std::string("CurseOfAlduin\\\\alchemist\\\\flask_") + kModel[el] + ".nif", a0, vel, 0.7f, [shot](const RE::NiPoint3& at) { FlaskBurst(shot, at); });
					});
					delay += kFly + 0.1f;
					from = to;
				}
				logger::info("alchemist: the Mutagen Gauntlet's cluster - {} of the dead to bounce through{}", hit.size(), legend ? " (Legendary)" : "");
				return;
			}
			// (Noah, the flasks redone: "Glass shatter + spell blast + lingering pool"; Tier I a bigger burst, Tier II a chain reaction,
			// Legendary an element cyclone) the glass breaks (Skyrim's oil lamp shattering), the element's own spell blast goes off,
			// and a pool of it lingers on the ground
			const int   t = f.tier;
			const float r = FlaskRadius(t) * (f.child ? 0.6f : 1.0f);
			const float share = FlaskShare(t) * (f.child ? 0.5f : 1.0f);
			Gun::QueueFx("CurseOfAlduin\\fx\\flask_glass.nif", a_at + RE::NiPoint3{ 0, 0, 10.0f }, f.child ? 0.5f : 0.8f, 0.0f, 1.0f, false);
			for (auto* fo : NearFoes(a_at, r, 24)) Perks::Catalyze(fo);  // (Catalyst Philter: those it catches take more for a while)
			FlaskBlast(f.el, t, a_at, (f.child ? 0.6f : 1.0f) * 1.0f * (r / 230.0f), share, r);
			if (f.child) return;
			if (f.el == 5) {  // (Noah: the Bloodbloom made the Voidglass Flask - "Mana Void pull") Apocalypse's Mana Void: the dead round it
				// dragged in to where it broke for a second and a half (fxmap.ini flask_manavoid), then struck
				Gun::CycloneSpec cs;
				cs.model = "CurseOfAlduin\\fx\\flask_manavoid.nif";
				cs.from = a_at, cs.fwd = { 0, 1, 0 }, cs.dist = 0.0f, cs.life = 1.5f, cs.size = std::max(0.6f, r / 420.0f), cs.fec = -1;
				cs.share = 0.03f * FlaskShare(t);
				cs.burst = [r, share](const RE::NiPoint3& at) { Gun::AreaHurt(at, r * 0.8f, 0.6f * share, ""); };
				Gun::ElemCyclone(std::move(cs));
			}
			if (t >= 2) {  // Tier II: a chain reaction - the dead it kills burst in its element themselves (once: no chain of chains)
				const auto caught = NearFoes(a_at, r, 16);
				std::vector<RE::ActorHandle> hs;
				for (auto* c : caught) hs.push_back(c->GetHandle());
				const int el = f.el;
				Tasks::Later(0.3f, [hs, el, t]() {
					int n = 0;
					for (auto& h : hs) {
						auto a = h.get();
						if (!a || !a->IsDead() || n >= 6) continue;
						FlaskShot child{ {}, el, t, a->GetPosition(), false, true, 0.0f };
						Tasks::Later(0.08f * static_cast<float>(n++), [child, at = a->GetPosition()]() { FlaskBurst(child, at); });
					}
				});
			}
			// (Noah: "remove the tornados to be only used on the class swords and the flasks should focus more on splash damage and crowd
			// control") each flask's Legendary its own burst - no cyclones
			if (t >= 3) {
				const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
				const int   el = f.el;
				switch (el) {
				case 0: {  // Fire: "Firestorm burst" - "Ring of fire outward": Skyrim's Fire Storm ring spreading out once (fxmap.ini flask_firestorm)
					Gun::QueueFx("CurseOfAlduin\\fx\\flask_firestorm.nif", a_at, 1.0f, 0.0f, 2.5f, false);
					Gun::BlastSound("fireball", a_at, 0.9f);
					const float rr = r * 1.8f;
					Tasks::Later(0.25f, [a_at, rr, share, whole]() {
						Gun::AreaHurt(a_at, rr, 1.2f * share, "");
						for (auto* fo : NearFoes(a_at, rr, 20)) Gun::Burn(fo, whole * 0.04f, 3.0f);
					});
					break;
				}
				case 1: {  // Frost: "Frost Nova instead" - "Slowed, not frozen": Apocalypse's Frost Nova, the dead at half speed for 5 s
					Gun::QueueFx("CurseOfAlduin\\fx\\flask_frostnova.nif", a_at, 1.0f, 0.0f, 2.0f, false);
					Gun::BlastSound("icevolley", a_at, 0.9f);
					const float rr = r * 1.6f;
					Gun::AreaHurt(a_at, rr, 0.8f * share, "");
					for (auto* fo : NearFoes(a_at, rr, 20)) {
						if (fo->IsDead()) continue;
						fo->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -50.0f);
						Tasks::Later(5.0f, [h = fo->GetHandle()]() { if (auto a = h.get()) a->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 50.0f); });
					}
					break;
				}
				case 2: {  // Shock: "Lightning strikes" - three bolts from the sky round where it broke (Apocalypse's Lightning Strike look)
					auto foes = NearFoes(a_at, r * 1.8f, 3);
					for (int k = 0; k < 3; ++k) {
						RE::NiPoint3 at = k < static_cast<int>(foes.size()) ? foes[k]->GetPosition() : a_at + RE::NiPoint3{ std::cos(k * 2.1f) * r * 0.7f, std::sin(k * 2.1f) * r * 0.7f, 0.0f };
						Tasks::Later(0.25f * static_cast<float>(k), [at, share]() {
							Gun::QueueFx("CurseOfAlduin\\fx\\flask_strike.nif", at, 1.0f, 0.0f, 1.5f, false);
							Gun::BlastSound("runeshock", at, 0.8f);
							Gun::AreaHurt(at, 170.0f, 0.7f * share, "");
						});
					}
					break;
				}
				case 3: {  // Acid: "Splash split" - three smaller acid splashes round the first
					for (int k = 0; k < 3; ++k) {
						const RE::NiPoint3 at = a_at + RE::NiPoint3{ std::cos(k * 2.094f + 0.5f) * r * 0.8f, std::sin(k * 2.094f + 0.5f) * r * 0.8f, 0.0f };
						FlaskShot child{ {}, el, t, at, false, true, 0.0f };
						Tasks::Later(0.15f + 0.12f * static_cast<float>(k), [child, at]() { FlaskBurst(child, at); });
					}
					break;
				}
				case 4: {  // Holy Water: "a golden light beam ... from the sky" - the sunlit pillar and Mysticism's sunburst (Gun::SkyBeam)
					Gun::SkyBeam(a_at, 1.6f);
					Gun::BlastSound("lightmassive", a_at, 0.8f);
					const float rr = r * 1.2f;
					Tasks::Later(0.5f, [a_at, rr, share, whole]() {
						Gun::AreaHurt(a_at, rr, 1.5f * share, "");
						for (auto* fo : NearFoes(a_at, rr, 20)) { Gun::Burn(fo, whole * 0.03f, 3.0f); Gun::Knock(fo, 0.4f); }
					});
					break;
				}
				case 5: {  // Voidglass: "Pull + Lunar Tide" - once the pull ends, a Lunar Tide ring blasts them back out
					Tasks::Later(1.6f, [a_at, r, share]() {
						Gun::QueueFx("CurseOfAlduin\\fx\\flask_lunartide.nif", a_at, 1.0f, 0.0f, 2.0f, false);
						Gun::BlastSound("lightmassive", a_at, 0.8f);
						Gun::AreaHurt(a_at, r * 1.5f, 1.0f * share, "");
						for (auto* fo : NearFoes(a_at, r * 1.5f, 20)) Gun::Knock(fo, 0.9f);
					});
					break;
				}
				default: break;
				}
			}
		}
		void TickGoldClouds(float a_delta)
		{
			for (auto& g : g_gold) {
				g.left -= a_delta;
				// the gilded that fell: they shatter in gold, and each drops extra Essence; the rest set free when the gold wears off
				for (auto it = g.gilded.begin(); it != g.gilded.end();) {
					auto* a = RE::TESForm::LookupByID<RE::Actor>(*it);
					if (!a) { g_gildedUntil.erase(*it); it = g.gilded.erase(it); continue; }
					if (a->IsDead()) {
						Gun::ElemBurst(8, a->GetPosition() + RE::NiPoint3{ 0, 0, 30.0f }, 0.7f, 0.0f, 0.8f);
						Points::Add(g.legend ? 150 : 100);
						g_gildedUntil.erase(*it);
						it = g.gilded.erase(it);
						continue;
					}
					if (g.left <= 0) { a->EnableAI(true); g_gildedUntil.erase(*it); }
					++it;
				}
			}
			std::erase_if(g_gold, [](const GoldCloud& g) { return g.left <= 0; });
		}
	}
	bool Gilded(RE::Actor* a_a)
	{
		if (!a_a) return false;
		auto it = g_gildedUntil.find(a_a->GetFormID());
		return it != g_gildedUntil.end() && std::chrono::steady_clock::now() < it->second;
	}
	void DevWyrm(std::string_view a_what, std::string_view a_plugin, RE::FormID a_id)
	{
		if (a_what == "base") g_wyrmPlugin = a_plugin, g_wyrmBase = a_id;
		else if (a_what == "play") { if (auto* f = Lookup(a_id, a_plugin)) Audio::Game(f->GetFormID(), RE::PlayerCharacter::GetSingleton(), 1.0f); }
		else if (a_what == "call") { g_wh.cool = 0; g_wh.aloft = false; CallWyrm(0); if (a_id) CallWyrm(1); g_wh.aloft = true; g_wh.called = true; }
		logger::info("dev: wyrm {} {} {:06X}", a_what, a_plugin, a_id);
	}
	bool ThrowFlask(RE::FormID) { return false; }  // (the flasks were spells: items now - TickAlchemy)
	namespace
	{
		float g_flaskCool = 0, g_philCool = 0;
		std::chrono::steady_clock::time_point g_throwAt{};
		void ThrowFlaskEl(int el, bool a_left)
		{
			if (el >= 0 && el < 6) g_lastFlaskEl = el;
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!p || !cam || !cam->cameraRoot) return;
			const int t = el == 6 ? 0 : std::max(0, Kit::FlaskTier(el));
			const auto& w = cam->cameraRoot->world;
			RE::NiPoint3 dir{ w.rotate.entry[0][1], w.rotate.entry[1][1], w.rotate.entry[2][1] };
			dir.Unitize();
			RE::NiPoint3 right{ dir.y, -dir.x, 0.0f };
			if (const float l = right.Length(); l > 1e-3f) right = right * (1.0f / l);
			RE::NiPoint3 from = w.translate + dir * 45.0f + right * (a_left ? -18.0f : 18.0f) + RE::NiPoint3{ 0.0f, 0.0f, -14.0f };
			RE::NiPoint3 aim = dir;
			// (Noah: "when throwing the potions in 3rd person the flasks itself aren't being thrown from the players arm it looks like
			// its coming from the camera angle") out of the hand that threw it, in either view - and in third person flown from the
			// hand at the crosshair's point ahead, not along the camera's line
			if (auto* root = p->Get3D(cam->IsInFirstPerson())) {
				if (auto* hand = root->GetObjectByName(a_left ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]")) {
					const RE::NiPoint3 hp = hand->world.translate;
					if (hp.GetDistance(p->GetPosition()) < 250.0f) {
						from = hp + dir * 10.0f;
						if (!cam->IsInFirstPerson()) {
							aim = (w.translate + dir * 1800.0f) - from;
							if (aim.Length() > 1e-3f) aim.Unitize(); else aim = dir;
						}
					}
				}
			}
			// (Noah: "faster and straighter") about twice as fast, barely lifted: it flies near where the crosshair points
			const float speed = el == 6 ? 2500.0f : 2700.0f;
			LaunchFlask(el, t, from, aim * speed + RE::NiPoint3{ 0.0f, 0.0f, 90.0f }, false);
		}
		int FlaskHeld(bool a_left)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* o = p ? p->GetEquippedObject(a_left) : nullptr;
			if (!o) return -1;
			for (int el = 0; el < 7; ++el) if (o == FlaskItem(el)) return el;
			return -1;
		}
	}
	// ---- the class swords (Noah: "base should be the sword with no special effects or abilities yet. tier 1 sends out arc
	// slashes from the blade ... that pierce enemies, swing in the color of the sword, on light attack swings. Tier 2 should give
	// the heavy attack an AOE attack, and tier 3 should have a unique ability per class that matches the classes build")
	RE::TESObjectWEAP* CarianBlade()
	{
		auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(gen::kCarianWeap, gen::kPlugin);
		return f ? f->As<RE::TESObjectWEAP>() : nullptr;
	}
	// which class's sword: 0 the Summoner's Radiant Quarterstaff, 1 Destruction's Carian Wand, 2 the Alchemist's Shadowflame Whip,
	// 3 the Spellsword's Spellbound Rapier (-1: none of them)
	RE::TESObjectWEAP* ClassWeapon(int a_cls)
	{
		return a_cls == 0 ? Riftblade() : a_cls == 1 ? CarianBlade() : a_cls == 2 ? AlchemistKnife() : a_cls == 3 ? SpellswordBlade() : nullptr;
	}
	int SwordClass(const RE::TESForm* a_w)
	{
		if (!a_w) return -1;
		if (a_w == Riftblade()) return 0;
		if (a_w == CarianBlade()) return 1;
		if (a_w == AlchemistKnife()) return 2;
		if (a_w == SpellswordBlade()) return 3;
		return -1;
	}
	namespace
	{
		// the element on the Spellsword's blade: the spell in the other hand (or the Spellsword spell it last carried)
		int SpellswordElement()
		{
			int i = Arcana::Hands()[0];
			if (i < 0) i = BladeSpell();
			return i >= 0 ? Arcana::ElementOf(i) : -1;
		}
		int LookKey(int a_cls)   // its row in gen::kBladeViz (tools/esp_ids.py BLADE_VIZ)
		{
			switch (a_cls) {
			case 0: return 2;
			case 1: return 1;
			case 2: return 0;
			case 3:
				switch (SpellswordElement()) {
				case coffer::kFire: return 4;
				case coffer::kFrost: return 5;
				case coffer::kShock: return 6;
				case coffer::kSun: return 7;
				case coffer::kBlood: case coffer::kArcane: return 8;   // (Blood made Arcane: its look the blue-white one)
				default: return 3;
				}
			}
			return -1;
		}
		const char* ArcModel(int a_key)
		{
			static constexpr const char* kArc[10] = { "purple", "carian", "white", "red", "fire", "frost", "shock", "sun", "blood", "gold" };
			static std::string s[10];
			if (a_key < 0 || a_key > 9) a_key = 3;
			if (s[a_key].empty()) s[a_key] = std::string("CurseOfAlduin\\fx\\arc_") + kArc[a_key] + ".nif";
			return s[a_key].c_str();
		}
		// (Visual Animated Enchants) (Noah: "I still dont see any weapon animations on the sword itself ... the sword effects should
		// use the mods we installed for them. blue will have the lightning sword effect, red will have fire, white will have white
		// colored sunlight, and purple should be purple flames") each look takes the whole of a Visual Animated Enchants effect's
		// show - its enchantment shader on the blade and its animated art - from the effect of the mod's that looks that way (its
		// own, or the vanilla enchantment it remade): the plugin needs no master of theirs; without the mod the looks keep their glow
		// (Noah: "go through the mod that gives weapons effect animations and properly assign each one for every weapon in our
		// mod") every melee weapon of ours wears a look-only enchantment in its colour, its shader and art Visual Animated
		// Enchants' own (the plugin needs no master of theirs; without the mod the weapons are plain)
		void VaeWeapons()
		{
			static bool done = false;
			if (done) return;
			done = true;
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh->LookupModByName("VisualAnimatedEnchants.esp")) { logger::info("weapons: Visual Animated Enchants is not installed - no weapon looks"); return; }
			for (const auto& l : gen::kVaeLooks) {
				auto* m = dh->LookupForm<RE::EffectSetting>(l.fx, gen::kPlugin);
				auto* from = dh->LookupForm<RE::EffectSetting>(l.src, "VisualAnimatedEnchants.esp");
				if (!m || !from) { logger::warn("weapons: Visual Animated Enchants' effect {:06X} not found", l.src); continue; }
				m->data.enchantShader = from->data.enchantShader;
				m->data.enchantEffectArt = from->data.enchantEffectArt;
			}
			int n = 0;
			for (const auto& [id, en] : gen::kVaeWeapons) {
				auto* w = dh->LookupForm<RE::TESObjectWEAP>(id, gen::kPlugin);
				auto* e = dh->LookupForm<RE::EnchantmentItem>(en, gen::kPlugin);
				if (!w || !e) continue;
				if (w->formEnchanting && w->formEnchanting != e) { logger::info("weapons: {} keeps its own enchantment", w->GetName()); continue; }
				w->formEnchanting = e, w->amountofEnchantment = 0;
				++n;
			}
			logger::info("weapons: {} weapons wear Visual Animated Enchants' looks", n);
		}
		void PatchLookArt()
		{
			VaeWeapons();
			static bool done = false;
			if (done) return;
			done = true;
			struct Src { std::uint32_t id; bool vae; const char* what; };
			// (the vanilla enchantments it remade are remade again by Vibrant Weapons when that loads after it - their own art
			// lost: so each look takes the mod's own copy of the effect, which no other mod touches)
			static constexpr Src kSrc[10] = {
				{ 0x000D75, true, "purple flames" },            // alch: the Shadowflame Whip (zzOverflowingPurple1)
				{ 0x00184D, true, "lightning" },                // carian: the Carian Wand (zzOverflowingPurpleBlue1: its shock)
				{ 0x00438D, true, "white light" },              // radiant: the Radiant Quarterstaff (zzOverflowingmoon1)
				{ 0x000D73, true, "fire" },                     // ss_none: the Spellbound Rapier's red fire (zzOverflowingOrange1)
				{ 0x000D73, true, "fire" },                     // ss_fire
				{ 0x000D74, true, "frost" },                    // ss_frost (zzOverflowingIceBlue1)
				{ 0x00184D, true, "lightning" },                // ss_shock
				{ 0x010B53, true, "sunfire" },                  // ss_sun (its Dawnbreaker)
				{ 0x00438D, true, "arcane white light" },       // ss_blood: now the Arcane blade (zzOverflowingmoon1)
				{ 0x010B53, true, "sunfire" } };                // slayer (its Dawnbreaker)
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh->LookupModByName("VisualAnimatedEnchants.esp")) { logger::info("swords: Visual Animated Enchants is not installed - the looks keep their glow"); return; }
			int n = 0;
			for (int k = 0; k < 10; ++k) {
				auto* from = kSrc[k].vae ? dh->LookupForm<RE::EffectSetting>(kSrc[k].id, "VisualAnimatedEnchants.esp") : RE::TESForm::LookupByID<RE::EffectSetting>(kSrc[k].id);
				if (!from) { logger::warn("swords: Visual Animated Enchants' {} effect not found", kSrc[k].what); continue; }
				for (int t = 0; t < 3; ++t) {
					if (auto* m = dh->LookupForm<RE::EffectSetting>(gen::kBladeVizFx[k][t], gen::kPlugin)) {
						// (Noah: "remove the red effect ... it looks better with just the elemental effects from the mod" - "Both") the
						// Spellbound Rapier's looks (3-8) wear the mod's element art alone, no glow over the blade
						// (Noah: "I want the blade to show the element of the spell in my other hand") the element looks (4-8) wear the
						// mod's element shader and art again; the bare look (3) none - the static red was our own dark-red Madfire
						// (Props::TickMad), gone now
						// (Noah: "the wands shouldn't have an element animation on the wand in hand") the Carian Wand's looks (1) bare
						if (k == 3 || k == 1) m->data.enchantShader = nullptr;
						else if (from->data.enchantShader) m->data.enchantShader = from->data.enchantShader;
						// (Noah: "the red glow on the blade removed") the mod's animated art stayed on the Spellbound Rapier, glowing
						// red whatever its element, after its spell was gone: its looks keep Vibrant Weapons' element art alone
						if (k == 3 || k == 1) m->data.enchantEffectArt = nullptr;
						else if (from->data.enchantEffectArt) m->data.enchantEffectArt = from->data.enchantEffectArt;
						++n;
					}
				}
				logger::info("swords: look {} - {} ({} shader {}, art {})", k, kSrc[k].what, from->GetFormEditorID(), from->data.enchantShader ? "yes" : "no", from->data.enchantEffectArt ? "yes" : "no");
			}
			logger::info("swords: {} tier looks wear Visual Animated Enchants' shaders and art", n);
		}
	}
	RE::EnchantmentItem* BladeLook(const RE::TESObjectWEAP* a_w, int a_tier)
	{
		PatchLookArt();
		// (Noah: Destruction's "Carian Wands") the wands carry their cast, at every tier - the bolt is ours (WandCast)
		if (SwordClass(a_w) == 1) return RE::TESDataHandler::GetSingleton()->LookupForm<RE::EnchantmentItem>(gen::kCarianWandEnch, gen::kPlugin);
		int key = SwordClass(a_w) >= 0 ? LookKey(SwordClass(a_w)) : -1;
		if (a_w && a_w == Slayer()) key = 9;
		// (Noah: "the static red effect is still on the spellbound blade I want that removed") the Spellbound Rapier wears no
		// look at all - other mods dress any fire enchantment in their own red art, whatever ours carries
		// (Noah: "I want the blade to show the element of the spell in my other hand") its element looks back; bare-handed, none
		if (key < 0 || key == 3 || a_tier < 1) return nullptr;
		return RE::TESDataHandler::GetSingleton()->LookupForm<RE::EnchantmentItem>(gen::kBladeViz[key][std::clamp(a_tier, 1, 3) - 1], gen::kPlugin);
	}
	// a swing of a class sword (any swing, hit or miss - WonderProps hears it)
	namespace
	{
		// the swing's heading along the ground
		RE::NiPoint3 Flat(const RE::NiPoint3& a_dir, RE::PlayerCharacter* a_p)
		{
			RE::NiPoint3 f{ a_dir.x, a_dir.y, 0 };
			if (f.Length() < 1e-3f) f = { std::sin(a_p->GetAngleZ()), std::cos(a_p->GetAngleZ()), 0 };
			f.Unitize();
			return f;
		}
		// a point set down on the ground under it (or left at your feet's height where there's no land under it, indoors)
		RE::NiPoint3 OnGround(RE::NiPoint3 a_at, float a_feet)
		{
			a_at.z = a_feet;
			if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(a_at, land) && std::abs(land - a_feet) < 150.0f) a_at.z = land;
			return a_at;
		}
		float SegDist(const RE::NiPoint3& a_c, const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
		{
			const RE::NiPoint3 ab = a_b - a_a;
			const float L = ab.SqrLength();
			const float k = L > 1e-4f ? std::clamp((a_c - a_a).Dot(ab) / L, 0.0f, 1.0f) : 0.0f;
			return a_c.GetDistance(a_a + ab * k);
		}
	}
	// (Noah: "tier 2 should be a smaller cyclone tornado like the shadowflame one ... tier 3 ... the cyclone tornado"; "each class's
	// own element"; the Destruction's "all the elements in one tornado, fire, ice, lightning"; the Spellsword's "the element in the
	// opposite hand") every class sword's heavy attack: a cyclone rising where the blow lands and rolling on along the ground,
	// dragging the dead in and catching all it touches in the class's element, bursting at its end - small at Tier II, large
	// at Tier III. Its look is the effects map's (fxmap.ini cyclone_<element>: Skyrim's Cyclone shout and the College's fire
	// pillar, coloured)
	// (Noah: "the weapon's description, it's way too long ... the base weapon should have its own description. The tier 1 should
	// have the description for what the tier 1 does ... You can also still see the charge bar on the weapons") the inventory's
	// item card for our swords: what the sword is, and what its tier does - and no charge bar (their tier looks are an
	// enchantment of looks alone, with charge that never runs down). The card's own text is laid over with ours each time the
	// game fills it, and redrawn
	void TickItemCard()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) return;
		auto menu = ui->GetMenu<RE::InventoryMenu>();
		if (!menu || !menu->uiMovie) return;
		auto* card = menu->GetRuntimeData().itemCard;
		if (!card || !card->obj.IsObject() || !card->view) return;
		RE::GFxValue name;
		if (!card->obj.GetMember("name", &name) || !name.IsString() || !name.GetString()) return;
		const std::string nm = name.GetString();
		struct Card { const char* name; const char* base; const char* tier[3]; };
		static const Card kCards[] = {
			{ "Radiant Quarterstaff", "The Summoner's blade: pale silver, burning with white sunlight.",
				{ "Tier I: light swings loose white sun balls that burn through the dead.", "Tier II: heavy attacks call a pillar of sunlight down on the one you strike.", "Tier III: heavy attacks call a great pillar of sunlight down." } },
			{ "Carian Wand", "Destruction's wand: hold its cast and it looses a bolt every half second - fire, lightning and ice in turn.",
				{ "Tier I: hold the cast for a bolt every half second - fire, lightning, ice in turn.", "Tier II: every 3rd bolt is a great bolt that pierces.", "Tier III: every 3rd bolt is a great bolt with a crossing volley either side." } },
			{ "Shadowflame Scythe", "The Alchemist's scythe, wreathed in purple shadowflame. Sheathe it and your flasks come to hand.",
				{ "Tier I: light swings loose bolts of shadowflame that set the dead burning.", "Tier II: heavy attacks loose a cyclone of purple fire.", "Tier III: heavy attacks loose a great cyclone of purple fire." } },
			{ "Spellbound Rapier", "The Spellsword's blade: it takes the element of the spell in your other hand.",
				{ "Tier I: light swings loose bolts of your spell's element.", "Tier II: heavy attacks loose a cyclone of it, casting your spell on all it catches.", "Tier III: heavy attacks loose a great cyclone of it." } },
		};
		// (Noah: "Now do the same thing for the flasks and the armor for each class") the flasks: what each is, and its tier's
		// gift; the class sets: what each is, and the power its tier has woken
		static const Card kFlasks[] = {
			{ "Fire Flask", "A flask of liquid fire: it shatters and bursts in flame.", {} },
			{ "Frost Flask", "A flask of frozen brine: it shatters into killing cold.", {} },
			{ "Shock Flask", "A flask of bottled storm: it shatters into arcing lightning.", {} },
			{ "Acid Flask", "A flask of green acid: it shatters into a corroding splash.", {} },
			{ "Holy Water", "Blessed water of Stendarr: it shatters into searing light; the dead burn in it.", {} },
			{ "Voidglass Flask", "A flask of bottled void: it shatters and drags the dead in to where it broke.", {} },
		};
		static constexpr const char* kFlaskTier[3] = { "Tier I: its burst reaches half again as wide.", "Tier II: a chain reaction - the dead it kills burst in its element too.",
			"Legendary: a greater burst of its element." };
		// (each flask's own Legendary, Noah 2026-11-05)
		static constexpr const char* kFlaskLegend[6] = { "Legendary: a ring of fire spreads out from where it breaks.", "Legendary: a freezing nova slows the dead to half speed for 5 seconds.",
			"Legendary: three bolts of lightning strike from the sky around it.", "Legendary: it splits into three more acid splashes.",
			"Legendary: a pillar of sunlight strikes down where it breaks.", "Legendary: after the pull, a lunar tide blasts the dead back out." };
		static const Card kSets[] = {
			{ "Summoner Set", "The Summoner's armor: a conjurer's robe, hood and boots, stitched with binding sigils that steady the bond between you and what you call.", { "Tier I: your summons take a fifth less harm.", "Tier II: one more summon slot.", "Tier III: your summons' kills heal you." } },
			{ "Destruction Set", "The Destruction mage's armor: dark College robes, hood and boots, woven to carry raw magicka from the heart to the hands.", { "Tier I: your spells cost 10% less.", "Tier II: your magicka comes back faster.", "Tier III: your spells cost a quarter less." } },
			{ "Alchemist Set", "The Alchemist's armor: a ranger's cuirass, hood and boots, light and hard-wearing, with loops at the belt for flasks.", { "Tier I: you throw 15% faster.", "Tier II: your flasks' bursts reach 20% wider.", "Tier III: each flask kill heals you 2%." } },
			{ "Spellsword Set", "The Spellsword's armor: black plate etched with runes - cuirass, helm, gauntlets and sabatons - for the one who fights with blade and spell together.", { "Tier I: you mend in a fight.", "Tier II: your blade's hits put magicka back.", "Tier III: your blade hits 15% harder." } },
		};
		std::string want;
		const Card* c = nullptr;
		for (const auto& k : kCards) if (nm.rfind(k.name, 0) == 0) { c = &k; break; }
		if (c) {
			const std::string rest = nm.substr(std::strlen(c->name));
			const int t = rest == " III" ? 3 : rest == " II" ? 2 : rest == " I" ? 1 : 0;
			want = t == 0 ? std::string(c->base) : std::string(c->base) + " " + c->tier[t - 1];
		}
		for (int e = 0; !c && e < 6; ++e) if (nm == kFlasks[e].name) {
			c = &kFlasks[e];
			const int t = std::clamp(Kit::FlaskTier(e), 0, 3);
			want = std::string(c->base) + (t >= 3 ? std::string(" ") + kFlaskLegend[e] : t >= 1 ? std::string(" ") + kFlaskTier[t - 1] : std::string{}) + " Attack to throw it; it never runs out.";
		}
		for (int k = 0; !c && k < 4; ++k) if (nm.rfind(kSets[k].name, 0) == 0) {
			c = &kSets[k];
			const int t = std::clamp(Kit::ArmourTier(k), 0, 3);
			want = std::string(c->base) + " " + std::to_string(5 + 5 * t) + "% less harm taken." + (t >= 1 ? std::string(" ") + c->tier[t - 1] : std::string{});
		}
		if (!c) return;
		// the field the card shows its text from: whichever holds the long one (or the effects line)
		const char* field = nullptr;
		for (const char* f : { "description", "effects" }) {   // (the one with tiers in it, else the one with any text, else effects)
			RE::GFxValue v;
			if (card->obj.GetMember(f, &v) && v.IsString() && v.GetString() && std::string(v.GetString()).find("Tier") != std::string::npos) { field = f; break; }
		}
		for (const char* f : { "description", "effects" }) {
			RE::GFxValue v;
			if (!field && card->obj.GetMember(f, &v) && v.IsString() && v.GetString() && v.GetString()[0]) field = f;
		}
		if (!field) field = "effects";
		RE::GFxValue cur;
		const bool same = card->obj.GetMember(field, &cur) && cur.IsString() && cur.GetString() && want == cur.GetString();
		if (same && !card->obj.HasMember("charge")) return;
		RE::GFxValue text;
		card->view->CreateString(&text, want.c_str());
		card->obj.SetMember(field, text);
		card->obj.DeleteMember("charge");
		menu->uiMovie->Invoke("_root.Menu_mc.UpdateItemCardInfo", nullptr, &card->obj, 1);
		if (static int said = 0; said++ < 12) logger::info("cards: {} - its card laid over ({}, no charge): {}", nm, field, want);
	}
	void ClassCyclone(int a_cls, int a_key, int a_t, float a_pw, RE::SpellItem* a_spell, const RE::NiPoint3& a_me, const RE::NiPoint3& a_dir)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		// its element: Summoner white light, Destruction the storm of all three, Shadowflame violet fire, the Spellsword the spell
		// in its other hand (its blade's look key: 4 fire 5 frost 6 shock 7 sun 8 blood; a bare blade, fire)
		enum El { kSun, kStorm, kShadow, kFire, kFrost, kShock, kBlood };
		int el = a_cls == 0 ? kSun : a_cls == 1 ? kStorm : a_cls == 2 ? kShadow : kFire;
		if (a_cls == 3) el = a_key == 5 ? kFrost : a_key == 6 ? kShock : a_key == 7 ? kSun : a_key == 8 ? kBlood : kFire;
		static constexpr const char* kModel[7] = { "white", "storm", "violet", "fire", "frost", "shock", "blood" };
		static constexpr const char* kRing[7] = { "white", "blue", "purple", "fire", "frost", "shock", "blood" };
		static constexpr int kBurst[7] = { 4, 2, 9, 0, 1, 2, 5 };   // (Gun::ElemBurst: 0 fire 1 frost 2 shock 4 sun 5 blood 9 shadowfire)
		static constexpr int kFec[7] = { -1, 2, -1, 0, 1, 2, -1 };   // (the Shadowflame's: no orange fire death - its violet one, touch below)     // (how its dead die: burning, frozen, shocked)
		static constexpr const char* kSound[7] = { "lightmassive", "shockmass", "fireball", "fireball", "icevolley", "runeshock", "bloodplague" };
		const bool big = a_t >= 3;
		const RE::NiPoint3 fwd = Flat(a_dir, p);
		const RE::NiPoint3 from = OnGround(a_me + fwd * 380.0f, a_me.z);   // (seen in game: risen 120 units ahead, its funnel swallowed the camera - the whole screen white and orange)
		if (a_cls == 0) {
			// (Noah, the Radiant Quarterstaff reworked: "Sun pillar strike") Auriel's sunbeam slams down where the blow lands, searing all
			// round it - Tier II smaller, Tier III great (fxmap.ini sun_strike: Dawnguard's own)
			// (Noah, clip 10: "the heavy attack should cast the light beam from the sky closer to the player where the enemy was
			// hit") on the one your blow reaches - the nearest of the dead just ahead - else a step in front of you
			RE::NiPoint3 at = OnGround(a_me + fwd * 170.0f, a_me.z);
			{
				float best = 1e9f;
				for (auto* f : NearFoes(a_me + fwd * 150.0f, 260.0f, 12)) {
					const float d = f->GetPosition().GetDistance(a_me);
					if (d < best) best = d, at = OnGround(f->GetPosition(), a_me.z);
				}
			}
			const float sz = big ? 1.0f : 0.65f, r = big ? 420.0f : 280.0f;
			// (Noah: "Cast the real Sunbeam") Mysticism's own Sunbeam down out of the sky (Gun::SkyBeam; without Mysticism the sunlit pillar)
			Gun::SkyBeam(at, big ? 2.0f : 1.4f);
			(void)sz;
			Gun::BlastSound("lightmassive", at, 0.9f);
			const float pw = a_pw, whole = Director::RoundHealth() * gen::kRounds.hpScale;
			Tasks::Later(0.5f, [at, r, pw, whole, big]() {   // (the sun's bolt lands about half a second on)
				Gun::Ring("white", at, 30.0f, r, 0.6f);
				Gun::AreaHurt(at, r, (big ? 0.55f : 0.4f) * pw, "");
				for (auto* f : NearFoes(at, r, 20)) { Gun::Burn(f, whole * 0.04f * pw, 3.0f); Gun::Knock(f, 0.6f); }
			});
			logger::info("swords: the Radiant Quarterstaff's sun pillar ({})", big ? "great" : "small");
			return;
		}
		// (seen in game: a burst and ring set off 120 units before your eyes filled the whole screen orange as the cyclone rose)
		// the cyclone rises on its own, heard, not flashed
		Gun::BlastSound(kSound[el], from, 0.8f);
		const int t = a_t;
		const float pw = a_pw;
		auto* spell = a_spell;
		Gun::CycloneSpec cs;
		cs.model = std::string("CurseOfAlduin\\fx\\cyclone_") + kModel[el] + ".nif";
		cs.from = from, cs.fwd = fwd;
		// (Noah: "make the tornadoes / cyclones last. For 10 seconds", then "Make the tornadoes last 5 seconds") five seconds rolling on: each beat of it a little lighter, the
		// whole of it about twice what the old two-second one did
		cs.dist = big ? 1100.0f : 750.0f, cs.life = 5.0f, cs.size = big ? 1.0f : 0.6f;
		cs.share = (big ? 0.09f : 0.06f) * pw * 0.8f;
		cs.fec = kFec[el];
		const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
		cs.touch = [el, t, pw, whole, spell](RE::Actor* a) {
			if (!a || a->IsDead()) return;
			Gun::Knock(a, 0.3f);
			switch (el) {
			case kShadow: Shadowburn(a, t); if (a->IsDead()) CorpseBurn(a); break;
			case kStorm: {   // (fire, ice and lightning in one: each it catches struck by one of the three in turn)
				static int k = 0;
				const int e = (k++) % 3;
				Gun::ElemBurst(e, a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.4f, 0.0f, 0.4f);
				if (e == 0) Gun::Burn(a, whole * 0.04f * pw, 3.0f);
				break;
			}
			case kSun: Gun::Burn(a, whole * 0.04f * pw, 3.0f); break;
			default:
				// (the Spellsword's: the spell in the other hand, cast on each it catches)
				if (spell) CastFrom(RE::PlayerCharacter::GetSingleton(), spell, a);
				else if (el == kFire) Gun::Burn(a, whole * 0.04f * pw, 3.0f);
				break;
			}
		};
		const int burstEl = kBurst[el], fec = kFec[el];
		const char* ring = kRing[el];
		const char* snd = kSound[el];
		cs.burst = [el, burstEl, fec, ring, snd, big, t, pw](const RE::NiPoint3& at) {
			const float sz = big ? 1.0f : 0.65f;
			Gun::ElemBurst(burstEl, at + RE::NiPoint3{ 0, 0, 80.0f }, 2.0f * sz, 0.0f, 0.8f);
			if (el == kStorm) {   // (its three elements bursting together)
				Gun::ElemBurst(0, at + RE::NiPoint3{ 60, 0, 60.0f }, 1.2f * sz, 0.05f, 0.6f);
				Gun::ElemBurst(1, at + RE::NiPoint3{ -40, 50, 60.0f }, 1.2f * sz, 0.1f, 0.6f);
			}
			Gun::Ring(ring, at, 40.0f, 420.0f * sz, 0.7f);
			Gun::BlastSound(snd, at, 1.0f);
			const int was = Gun::FecElem(fec);
			Gun::AreaHurt(at, 330.0f * sz, 0.4f * pw, "");
			Gun::FecElem(was);
			for (auto* f : NearFoes(at, 330.0f * sz, 20)) {
				if (el == kShadow) Shadowburn(f, t);
				Gun::Knock(f, 0.6f);
			}
		};
		Gun::ElemCyclone(std::move(cs));
		logger::info("swords: a {} cyclone of {} (tier {})", big ? "large" : "small", kModel[el], a_t);
	}
	void MaceSwing(bool a_power)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* w = p ? p->GetEquippedObject(false) : nullptr;
		const int cls = SwordClass(w);
		if (!p || cls < 0 || !p->AsActorState()->IsWeaponDrawn()) return;
		static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(5);  // (one swing heard from both bodies)
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(200)) return;
		last = now;
		const int t = std::clamp(Kit::TierOfWeapon(w), 0, 3);
		if (t < 1) return;
		const float pw = UpgradeMult(t) * (0.7f + 0.3f * Arcana::Power());
		const RE::NiPoint3 dir = Gun::SwingDir();
		const RE::NiPoint3 me = p->GetPosition();
		const int key = LookKey(cls);
		RE::SpellItem* spell = nullptr;
		if (cls == 3) if (const int i = Arcana::Hands()[0] >= 0 ? Arcana::Hands()[0] : BladeSpell(); i >= 0) spell = Arcana::Spell(i);
		if (!a_power) {
			// Tier I on: the arc slash, in the sword's colour, through every one of the dead in its path
			Gun::SlashSpec sl;
			// (Noah: "Compact magic projectiles" - each class's bolt in its element; the Destruction sword's fire, frost and lightning
			// in turn, swing after swing)
			static int turn = 0;
			const int bolt = cls == 1 ? 4 + (turn++ % 3) : key;
			sl.model = ArcModel(bolt);
			sl.from = Gun::SwingFrom();
			sl.dir = dir;
			sl.roll = Gun::SwingRoll(dir);
			sl.range = 1400.0f + 200.0f * t;
			// (seen in game: the bolts left the blade at a third of their size at 2,300 a second - never seen) a bolt at its full
			// size from the blade, at a spell's own pace - the Radiant Quarterstaff's spectral wave still a crescent that grows
			const bool isBolt = true;   // (Noah, clip 10: the Radiant Quarterstaff's a white sun ball now, a bolt like the rest)
			sl.bolt = isBolt;
			if (isBolt) sl.speed = 1150.0f;
			sl.scale = (isBolt ? 1.3f : 0.9f) + 0.15f * t;
			sl.share = BladeShare(w->As<RE::TESObjectWEAP>()) * (0.7f + 0.3f * Arcana::Power());   // (the blade's own light blow, at its tier)
			sl.flingKill = 16.0f;   // (Noah: "send enemies flying if the slash kills them")
			// (Noah: "when enemies are killed by my sword swinging on them, they explode in golden fire instead of purple fire") Skyrim's
			// fire death burns orange whatever it's asked: the Shadowflame's dead die in its own violet fire instead (below)
			sl.onHit = [cls, t, bolt](RE::Actor* a, bool killed) {
				if (killed && a && cls == 2) { CorpseBurn(a); Gun::ElemBurst(9, a->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, 0.6f, 0.0f, 0.6f); }
				if (killed || !a) return;
				if (cls == 2) Shadowburn(a, t);                                   // shadowflame
				else if (cls == 1) Gun::ElemBurst(bolt - 4, a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.35f, 0.0f, 0.4f);   // (its element bursting on each it passes through)
			};
			Gun::Slash(sl);
			return;
		}
		if (t >= 2) ClassCyclone(cls, key, t, pw, spell, me, dir);
	}
	// (Noah: "the dragon slayer sword's light attacks should send golden fire slashes that burn 2x damage to dragon enemies, and
	// heavy attack should rain golden fire meteors on any enemies within 30 meters for 5s after each heavy attack swing")
	namespace
	{
		float g_goldRain = 0, g_goldNext = 0;
		bool IsDragon(RE::Actor* a_a)
		{
			static auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(0x035D59);   // ActorTypeDragon
			auto* race = a_a ? a_a->GetRace() : nullptr;
			return kw && race && race->HasKeyword(kw);
		}
		float SlayerTop() { return UpgradeMult(AfterSovngarde() ? kWonderUps + 1 : kWonderUps); }
		// the gold fire's burn on one struck: twice over on a dragon
		void GoldBurn(RE::Actor* a_a, float a_share)
		{
			if (!a_a || a_a->IsDead()) return;
			const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
			Gun::Burn(a_a, whole * 0.06f * SlayerTop() * (IsDragon(a_a) ? 2.0f : 1.0f), 4.0f);
			if (IsDragon(a_a) && a_share > 0) Gun::AreaHurt(a_a->GetPosition(), 10.0f, a_share, "");   // (the blow itself doubled on a dragon)
			Gun::ElemBurst(8, a_a->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, 0.45f, 0.0f, 0.4f);
		}
		void TickGoldRain(RE::PlayerCharacter* p, float a_delta)
		{
			if (g_goldRain <= 0 || !p) return;
			g_goldRain -= a_delta;
			if ((g_goldNext -= a_delta) > 0) return;
			g_goldNext = 0.6f;
			const auto foes = NearFoes(p->GetPosition(), 1400.0f, 40);   // (20 m)
			if (foes.empty()) return;
			static std::size_t turn = 0;
			auto* f = foes[(turn++) % foes.size()];   // (a different one each time, round the ones near you)
			const float share = BladeShare(Slayer()) * (0.7f + 0.3f * Arcana::Power());
			// (the look Noah kept: Skyrim's fireball in gold, small, homing; a small burst where it lands)
			Gun::MeteorAt(f, true, 0.8f, [share](const RE::NiPoint3& hit) {
				Gun::FxBlastSized("fireball", hit, 0.45f);
				Gun::ElemBurst(8, hit + RE::NiPoint3{ 0, 0, 30.0f }, 0.5f, 0.0f, 0.5f);
				for (auto* g : NearFoes(hit, 240.0f, 12)) GoldBurn(g, share * 1.6f);
			});
		}
	}
	void SlayerKill() {}       // (its dragon embers are gone: the heavy attack's rain of gold has their place)
	int  SlayerEmbers() { return 0; }
	void SlayerSwing(bool a_power)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(5);
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(200)) return;   // (one swing heard from both bodies)
		last = now;
		if (a_power) {
			// (Noah: "the heavy attack ... should spawn meteors from the sky that look like the smaller fireballs for 5 seconds on enemies
			// within 20 meters of the player" - "About 8 per swing") five seconds of small golden fireballs out of the sky, one every 0.6 s,
			// each chasing one of the dead near you (TickGoldRain); a swing during it starts the five seconds over
			g_goldRain = 5.0f, g_goldNext = 0.0f;
			Gun::BlastSound("fireball", p->GetPosition(), 0.7f);
			logger::info("swords: the Dragon Slayer calls down five seconds of golden fireballs");
			return;
		}
		// (Noah: "Light attack for the Dragon Slayer sword should shoot a fireball that is golden flames and deals 2x damage to dragon
		// type enemies") a light swing: a fireball of golden flame (Skyrim's own fireball, in gold), bursting on the first it strikes
		const RE::NiPoint3 dir = Gun::SwingDir();
		Gun::SlashSpec sl;
		sl.model = ArcModel(9);
		sl.from = Gun::SwingFrom();
		sl.dir = dir;
		sl.roll = 0.0f;
		sl.speed = 1150.0f, sl.range = 2200.0f, sl.radius = 90.0f, sl.scale = 1.4f, sl.stopOnHit = true, sl.bolt = true;   // (seen in game: at a third of its size and 2,700 a second it was never seen)
		const float share = BladeShare(Slayer()) * (0.7f + 0.3f * Arcana::Power());   // (its own light blow)
		sl.share = share;
		sl.flingKill = 16.0f;
		sl.onHit = [share](RE::Actor* a, bool killed) { if (a && !killed) GoldBurn(a, share); };
		Gun::Slash(sl);
	}
	// each frame's sword upkeep: the Summoner's circling blades of light (Tier III, drawn), the Alchemist's chain reaction
	void TickSwords(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* w = p ? p->GetEquippedObject(false) : nullptr;
		const int cls = SwordClass(w);
		const int t = cls >= 0 ? std::clamp(Kit::TierOfWeapon(w), 0, 3) : 0;
		const bool drawn = p && p->AsActorState()->IsWeaponDrawn();
		TickGoldRain(p, a_delta);
		// (Noah: "85% is good"; "make them required") the class swords are Light Greatswords' own models, drawn at 85% in the hand
		// and at the side - a long sword in one hand - in both bodies, every frame (the game sets a model's size again as it
		// moves between hand and sheath)
		// (Noah, clips 315-321: "the alchemist sword was stuck on my character's back"; the class swords unseen in the hand) Light
		// Greatswords' models are greatswords: each names the back as its sheath (its Prn), so the game hangs our one-handed
		// sword there - and a one-handed draw only ever takes a sword from the hip, so it stayed on the back, in both bodies, and
		// the first-person body has no back to see. Ours is moved by hand: in the hand from the moment it's drawn to the moment
		// it's put away, on the back the rest of the time
		if (p) {
			for (const bool fp : { false, true }) {
				auto& bip = p->GetBiped(fp);
				if (!bip) continue;
				auto& o = bip->objects[RE::BIPED_OBJECT::kOneHandSword];
				if (!o.item || SwordClass(o.item) < 0 || !o.partClone) continue;
				// (only Light Greatswords' long blades: the Animated Armoury weapons the classes carry now are their own size)
				auto* ow = o.item->As<RE::TESObjectWEAP>();
				if (!ow || !std::string_view(ow->GetModel()).contains("hipblades")) continue;
				if (std::abs(o.partClone->local.scale - 0.85f) > 0.001f) o.partClone->local.scale = 0.85f;
				// (Noah, clips 8-9: the blade unseen in the hand after a night began, and gone as it was put away in first person)
				// it was moved between the hand and the back by hand - fighting the game's own draw and sheathe; its model now
				// names the hip as its sheath (ArmorForge::BuildHipBlades), so the game moves it itself
			}
		}
		// (Noah: "Both layered") the look's Vibrant Weapons art along the blade in your hand - an ability of ours, laid on while
		// a class sword at Tier I or more (or the Dragon Slayer) is in your right hand, taken off as soon as it isn't
		if (static float art = 0; p && (art -= a_delta) <= 0) {
			art = 0.25f;
			int key = cls >= 0 && t >= 1 ? LookKey(cls) : -1;
			if (w && w == Slayer()) key = 9;
			if (key == 3) key = -1;   // (the Spellbound Rapier bare-handed: no look)
			if (key == 1) key = -1;   // (Noah: the wands - no element look in the hand, only the tip's orb)
			// (Noah: "the ice effect ... was floating" - the Radiant Quarterstaff put away on the back, its art still hung where the
			// hand was) the art along the blade only while it's drawn
			if (!p->AsActorState()->IsWeaponDrawn()) key = -1;
			// (Noah: "when I change it to ice, my sword is still on fire") its enchantment's look was only set when the kit was synced:
			// the moment the other hand's element changes, the blade is given its new look (taken off and on again)
			static int lastSs = -2;
			if (cls == 3 && key != lastSs) {
				if (lastSs != -2) Kit::SyncWeapons(true);
				lastSs = key;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* want = key >= 0 ? dh->LookupForm<RE::SpellItem>(gen::kBladeVizArt[key], gen::kPlugin) : nullptr;
			for (const auto id : gen::kBladeVizArt) {   // (every other look's taken off - a saved game may still wear one)
				if (auto* sp = dh->LookupForm<RE::SpellItem>(id, gen::kPlugin); sp && sp != want && p->HasSpell(sp)) p->RemoveSpell(sp);
			}
			if (want && !p->HasSpell(want)) {
				p->AddSpell(want);
				logger::info("swords: blade art {}", key);
			}
			// (Noah: "The green animation circling the sword, I want that to be changed to a golden color instead of green, but keep the
			// same animation") the Dragon Slayer's art round the blade coloured gold, in both bodies
			if (key == 9) for (const bool fp : { false, true }) if (auto* root = p->Get3D(fp)) Gun::TintGold(root->GetObjectByName("WEAPON"));
			// (Noah, clips 8-9: the Radiant Quarterstaff's art showed frost-blue and green - "should be white") the Summoner's in white
			if (key == 2) for (const bool fp : { false, true }) if (auto* root = p->Get3D(fp)) Gun::TintPal(root->GetObjectByName("WEAPON"), "textures\\CurseOfAlduin\\fx\\pal_white.dds", false);
			// (Noah, clips 301-302: the Shadowflame Whip and the Spellbound Rapier unseen in the hand, in both views) a flask's
			// throw hides the hand's weapon node from the release to the end of the throw: never left hidden once no throw is on
			if (!Gun::Throwing()) {
				for (const bool fp : { false, true }) {
					auto* root = p->Get3D(fp);
					for (const char* nm : { "WEAPON", "SHIELD" }) {
						if (auto* n = root ? root->GetObjectByName(nm) : nullptr; n && n->GetAppCulled()) {
							n->SetAppCulled(false);
							logger::warn("swords: {} {} node was left hidden - shown again", fp ? "1st" : "3rd", nm);
						}
					}
				}
			}
			// and what the hand holds, written down each time the blade changes (its model, where it is, what of it is hidden)
			static const RE::TESForm* seen = nullptr;
			static float since = -1.0f;
			if (w != seen) seen = w, since = 0.0f;
			else if (since >= 0.0f && (since += 0.25f) >= 1.0f) {   // (a moment after: its model is in the hand by then)
				since = -1.0f;
				for (const bool fp : { false, true }) {
					auto& bip = p->GetBiped(fp);
					auto* clone = bip ? bip->objects[RE::BIPED_OBJECT::kOneHandSword].partClone.get() : nullptr;
					if (!clone) { logger::info("swords: {} hand holds no model", fp ? "1st" : "3rd"); continue; }
					int shapes = 0, hidden = 0;
					std::string up;
					for (auto* q = clone->parent; q; q = q->parent) if (q->GetAppCulled()) up += std::string(q->name.c_str()) + " ";
					RE::BSVisit::TraverseScenegraphGeometries(clone, [&](RE::BSGeometry* g) { ++shapes; if (g->GetAppCulled()) ++hidden; return RE::BSVisit::BSVisitControl::kContinue; });
					logger::info("swords: {} hand model '{}' culled {} scale {:.2f} world {:.2f} at ({:.0f},{:.0f},{:.0f}) parent '{}' shapes {} hidden {} hidden above: {}", fp ? "1st" : "3rd",
						clone->name.c_str(), clone->GetAppCulled(), clone->local.scale, clone->world.scale, clone->world.translate.x, clone->world.translate.y, clone->world.translate.z,
						clone->parent ? clone->parent->name.c_str() : "none", shapes, hidden, up.empty() ? "none" : up);
				}
			}
		}
		g_shadowChain = false;   // (Noah: the Tier III cyclone replaces the burning dead's explosions)
		g_shadowTier = std::max(1, t);
		// (Noah: the Tier III cyclone replaces the Summoner's circling blades of light)
		Gun::Halo(0, "", 1.0f, 1.0f, nullptr);
		// the Dragon Slayer, a wonder above every tier: its gold dragonfire look always
		if (static float sl = 0; (sl -= a_delta) <= 0) {
			sl = 2.0f;
			if (auto* d = Slayer(); d && p) {
				// (Noah: "remove the flames on the sword itself when holding it. So it's just the golden circular animation on the blade
				// itself") no enchantment's flames on it: only its art circling the blade (TickSwords), in gold
				if (RE::EnchantmentItem* want = nullptr; d->formEnchanting != want) {
					d->formEnchanting = want;
					d->amountofEnchantment = 0;
					auto* em = RE::ActorEquipManager::GetSingleton();
					if (em && p->GetEquippedObject(false) == d && !ItemMenusOpen()) {
						em->UnequipObject(p, d, nullptr, 1, nullptr, false, false, false, true);
						em->EquipObject(p, d, nullptr, 1, nullptr, false, false, false, true);
					}
				}
			}
		}
		// the Spellsword's blade takes the element of the spell in the other hand: its look follows (Kit swaps it when it can)
		static float look = 0;
		if (cls == 3 && t >= 1 && (look -= a_delta) <= 0) {
			look = 0.5f;
			if (auto* wp = w->As<RE::TESObjectWEAP>(); wp && wp->formEnchanting != BladeLook(wp, t)) Kit::SyncWeapons(true);
		}
	}
	void TickShadowflame(float a_delta) { TickShadow(a_delta); }

	bool FlaskSwingBlocked(bool a_left)
	{
		const int el = FlaskHeld(a_left);
		if (el < 0 || el == 6) return false;   // (the Mutagen Staff: Skyrim's staff cast plays)
		// (Noah: "my character is still making swing grunt sounds when throwing") Skyrim's own swing never plays with a flask in
		// hand - its grunt came with it, whatever voice you had; the throw is all ours (the arm posed by hand, Gun's PoseThrow)
		return true;
	}
	int FlaskElementInHand(bool a_left) { return FlaskHeld(a_left); }
	bool FlaskInHand(bool a_left)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		return p && p->AsActorState()->IsWeaponDrawn() && FlaskHeld(a_left) >= 0;
	}
	namespace
	{
#include "GauntletBeam.inl"
	}
	void FlaskAttackUp(bool a_left) { if (FlaskHeld(a_left) == 6) g_gb.held = false; }
	void DevGauntletHold(bool a_on, bool a_left) { if (a_on) GauntletPress(a_left); else g_gb.held = false; }
	// ---- (Noah: "make the mutagen staff just like a quarter staff ... give it the quarterstaff animations. And make it cast a
	// 2-second laser beam that pierces and is an element for each of the vials"; "holding block should fire the beam and heavy
	// attack should throw flasks cycling through each flask"; "next vial each beam"; the look: "Dwarven Quarterstaff")
	namespace
	{
		struct MutagenQs { bool block = false; float beam = 0, cool = 0, tick = 0, burst = 0; int el = 0, throwEl = 0, beamEl = -1; };
		MutagenQs g_mq;
		constexpr std::uint64_t kMqLoop = 0x6A07B0A2ull;
		RE::NiPoint3 MutagenTip(RE::PlayerCharacter* a_p)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			const bool first = cam && cam->IsInFirstPerson();
			RE::NiPoint3 fwd{ 0, 1, 0 };
			if (cam && cam->cameraRoot) { const auto& m = cam->cameraRoot->world.rotate; fwd = { m.entry[0][1], m.entry[1][1], m.entry[2][1] }; fwd.Unitize(); }
			if (auto* root = a_p->Get3D(first)) {
				if (auto* n = root->GetObjectByName("WEAPON")) {
					const auto at = n->world.translate;
					if (at.GetDistance(a_p->GetPosition()) < 300.0f) return at + fwd * 35.0f;
				}
			}
			return Gun::SwingFrom();
		}
		void MutagenBeamStop()
		{
			if (g_mq.beam > 0 || g_mq.beamEl >= 0) { Audio::LoopStop(kMqLoop, 250); Gun::GauntletBeamOff(); }
			g_mq.beam = 0, g_mq.beamEl = -1;
		}
	}
	bool MutagenInHand()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		return p && p->AsActorState()->IsWeaponDrawn() && FlaskHeld(false) == 6;
	}
	void MutagenLook()
	{
		auto* mut = FlaskItem(6) ? FlaskItem(6)->As<RE::TESObjectWEAP>() : nullptr;
		auto* f = Lookup(0x18B75A, "NewArmoury.esp");   // Animated Armoury's Dwarven Quarterstaff
		auto* s = f ? f->As<RE::TESObjectWEAP>() : nullptr;
		if (!mut || !s) { logger::warn("alchemist: the Mutagen Staff can't take the Dwarven Quarterstaff's look - is Animated Armoury installed?"); return; }
		AdoptLook(mut, s);
		mut->formEnchanting = nullptr, mut->amountofEnchantment = 0;   // (no staff cast of Skyrim's: its beam and flasks are ours)
		logger::info("alchemist: the Mutagen Staff takes the look of {} - swung as a quarterstaff", s->GetName());
	}
	void MutagenBlock(bool a_down) { g_mq.block = a_down; }
	void MutagenPower()
	{
		if (g_class.load() != coffer::kAlchemist) { Hud::QuestBanner("The Mutagen Staff answers only to an Alchemist", 2.0f); return; }
		static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(5);
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(400)) return;   // (one swing, heard once)
		last = now;
		const int el = g_mq.throwEl;
		g_mq.throwEl = (g_mq.throwEl + 1) % 6;
		ThrowFlaskEl(el, false);   // fire, frost, storm, acid, holy water, void in turn
	}
	void TickMutagen(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* ui = RE::UI::GetSingleton();
		const bool ok = p && MutagenInHand() && !p->IsDead() && !ItemMenusOpen() && !(ui && ui->GameIsPaused());
		if (!ok) { g_mq.block = false; MutagenBeamStop(); return; }
		g_mq.cool = std::max(0.0f, g_mq.cool - a_delta);
		MutagenBeamStop();   // (Noah: "remove the beam")
		// (Noah: "when holding block it should be throwing the flasks from the center of the staff alternating through all of them" -
		// "One every 0.5 s") block held: a flask every half second from the staff in your hands - fire, frost, storm, acid, holy
		// water, void in turn
		if (g_mq.block && g_mq.cool <= 0) {
			if (g_class.load() != coffer::kAlchemist) {
				Hud::QuestBanner("The Mutagen Staff answers only to an Alchemist", 2.0f);
				g_mq.block = false;
				return;
			}
			g_mq.cool = 0.5f;
			const int el = g_mq.el;
			g_mq.el = (g_mq.el + 1) % 6;
			ThrowFlaskEl(el, false);
		}
	}
	// (Noah: "hold it in my left or right hand with an underhand throwing animation ... it should feel like a weapon") the
	// hand's attack: the arm swings it underhand and lets it go at the top of the swing; a breath between throws
	void FlaskAttack(bool a_left)
	{
		const int el = FlaskHeld(a_left);
		if (el < 0 || Gun::Throwing()) return;
		if (el != 6 && g_class.load() != coffer::kAlchemist) { Hud::QuestBanner("Only an Alchemist can throw the flasks", 2.0f); return; }   // (Noah: one inventory - "carry but class-locked")
		if (el == 6 && g_class.load() != coffer::kAlchemist) { Hud::QuestBanner("The Mutagen Staff answers only to an Alchemist", 2.5f)   /* (Noah: "it won't let me throw it" - and nothing said why: always shown) */; return; }
		if (el == 6) { GauntletPress(a_left); return; }   // (Noah: the Mutagen Gauntlet's beam - held, not thrown)
		float& cool = el == 6 ? g_philCool : g_flaskCool;
		if (cool > 0) {
			if (el == 6) Hud::QuestBanner(fmt::format("The Mutagen Gauntlet is recharging ({:.0f} s)", std::ceil(cool)), 1.5f);
			return;
		}
		cool = el == 6 ? 30.0f :   /* (Noah: the Mutagen Gauntlet "once every 30s") */ 0.9f * (Kit::ArmourTier(coffer::kAlchemist) >= 1 ? 0.85f : 1.0f) * Perks::FlaskCostMult();
		g_throwAt = std::chrono::steady_clock::now();
		// (Noah: the flasks' own synthesized sounds "really toony") the throw makes no sound of its own: the impact carries it
		Gun::StartThrow(a_left, [el, a_left]() { ThrowFlaskEl(el, a_left); });
	}
	// (Noah: "an item that you actually throw") a flask that leaves your pack was used (its hotkey, or Use in the pack): it is
	// thrown and put back - an Alchemist's flasks never run out, a breath between throws (the Philosopher's Flask twelve seconds)
	// (Noah: "the claws are still facing the wrong way. The blade part of the claws should be facing away from the player") the
	// claws' own model turned round in first person - the right hand's, the left's: degrees about x, y, z (dev: clawrot)
	namespace
	{
		// (Noah, after "clawrot 180 0 180" live: "nice, but the only one that's not looking correct is the Daedric claws")
		RE::NiPoint3 g_clawRot{ 180.0f, 0.0f, 180.0f };     // first person
		RE::NiPoint3 g_clawRot3{ 180.0f, 0.0f, 180.0f };    // third person
		bool         g_clawRotOn = true;
		// one claw's own turn in place of the above, in the right hand (Animated Armoury's left Daedric claw - the Cinderclaws'
		// left - sat "upside down under my palm" ... "its only when i equip the left one on my right hand"): its NewArmoury.esp
		// id -> degrees (dev: clawform <hex id> x y z)
		std::unordered_map<RE::FormID, RE::NiPoint3> g_clawForm{ { 0x19AAE8, { 180.0f, 0.0f, 0.0f } } };
	}
	void DevClawForm(RE::FormID a_local, float a_x, float a_y, float a_z)
	{
		g_clawForm[a_local & 0xFFFFFF] = { a_x, a_y, a_z };
		logger::info("dev: clawform {:06X} {:.0f} {:.0f} {:.0f}", a_local & 0xFFFFFF, a_x, a_y, a_z);
	}
	void DevClawRot(float a_x, float a_y, float a_z, bool a_on, bool a_third)
	{
		(a_third ? g_clawRot3 : g_clawRot) = { a_x, a_y, a_z }, g_clawRotOn = a_on;
		logger::info("dev: clawrot {} {:.0f} {:.0f} {:.0f}", a_on ? "on" : "off", a_x, a_y, a_z);
	}
	void TickClaws()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* cam = RE::PlayerCamera::GetSingleton();
		if (!p || !cam || !p->Is3DLoaded()) return;
		static RE::BGSKeyword* claw = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSKeyword>(0x19AAB4, "NewArmoury.esp");
		if (!claw) return;
		static std::unordered_map<RE::NiAVObject*, RE::NiMatrix3> was;   // (each claw's model: how it sat before)
		if (was.size() > 64) was.clear();
		for (const bool third : { false, true }) {
		auto* root = p->Get3D(!third);
		if (!root) continue;
		const RE::NiPoint3 rot = third ? g_clawRot3 : g_clawRot;
		for (const bool left : { false, true }) {
			auto* o = p->GetEquippedObject(left);
			auto* w = o ? o->As<RE::TESObjectWEAP>() : nullptr;
			if (!w || !w->HasKeyword(claw)) continue;
			auto* node = root->GetObjectByName(left ? "SHIELD" : "WEAPON");
			auto* nn = node ? node->AsNode() : nullptr;
			if (!nn) continue;
			RE::NiPoint3 r = rot;
			if (!left && w->GetFile(0) && w->GetFile(0)->GetFilename() == "NewArmoury.esp") {
				if (auto f = g_clawForm.find(w->GetLocalFormID()); f != g_clawForm.end()) r = f->second;
			}
			for (auto& ch : nn->GetChildren()) {
				if (!ch) continue;
				auto [it, fresh] = was.try_emplace(ch.get(), ch->local.rotate);
				RE::NiMatrix3 turn;
				const float d = 0.0174533f;
				turn.SetEulerAnglesXYZ(r.x * d, r.y * d, r.z * d);
				ch->local.rotate = g_clawRotOn ? it->second * turn : it->second;
			}
		}
		}
	}
	// (Noah: the Alchemist's scythe is two-handed - "Swap by sheathing") sheathe the scythe and your flasks come to hand (the
	// ones you held last, or those you carry), ready to draw and throw; sheathe them and the scythe comes back
	namespace
	{
		RE::FormID g_lastFlask[2] = { 0, 0 };   // the flasks last in your hands: right, left
		void TickScytheSwap()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* sc = AlchemistKnife();
			static RE::WEAPON_STATE was = RE::WEAPON_STATE::kSheathed;
			if (!p || !sc || g_class.load() != coffer::kAlchemist || !p->Is3DLoaded() || ItemMenusOpen()) return;
			const auto now = p->AsActorState()->GetWeaponState();
			const bool sheathedNow = now == RE::WEAPON_STATE::kSheathed && was != RE::WEAPON_STATE::kSheathed && was != RE::WEAPON_STATE::kWantToDraw;
			was = now;
			for (bool left : { false, true }) if (const int el = FlaskHeld(left); el >= 0) g_lastFlask[left ? 1 : 0] = FlaskItem(el)->GetFormID();
			if (!sheathedNow) return;
			auto* em = RE::ActorEquipManager::GetSingleton();
			if (!em) return;
			auto* rightSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F42);
			auto* leftSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x013F43);
			if (p->GetEquippedObject(false) == sc) {
				// the scythe put away: the flasks to hand - those held last, else the ones you carry, one in each hand
				std::vector<RE::TESBoundObject*> pick;
				for (auto id : g_lastFlask) if (auto* f = RE::TESForm::LookupByID<RE::TESBoundObject>(id); f && p->GetItemCount(f) > 0 && std::find(pick.begin(), pick.end(), f) == pick.end()) pick.push_back(f);
				for (int el = 0; el < 7 && pick.size() < 2; ++el) if (auto* f = FlaskItem(el); f && p->GetItemCount(f) > 0 && std::find(pick.begin(), pick.end(), f) == pick.end()) pick.push_back(f);
				if (pick.empty()) return;
				em->UnequipObject(p, sc, nullptr, 1, nullptr, false, false, false, true);
				em->EquipObject(p, pick[0], nullptr, 1, rightSlot, false, false, false, true);
				if (pick.size() > 1) em->EquipObject(p, pick[1], nullptr, 1, leftSlot, false, false, false, true);
				logger::info("classes: the scythe sheathed - {} flask(s) to hand", pick.size());
			} else if (FlaskHeld(false) >= 0 || FlaskHeld(true) >= 0) {
				// the flasks put away: the scythe back in both hands
				if (p->GetItemCount(sc) <= 0) return;
				em->EquipObject(p, sc, nullptr, 1, nullptr, false, false, false, true);
				logger::info("classes: the flasks sheathed - the scythe to hand");
			}
		}
	}
	// (Noah: "Soulcaller's Staff": "hold the cast: souls rip out of the dead along your aim into the staff; release to throw them back
	// as a swarm of spectral wolves that hunt for 20 s")
	namespace
	{
		int g_souls = 0;
		std::unordered_set<RE::FormID> g_soulTaken;
		RE::TESNPC* SoulWolf()
		{
			static RE::TESNPC* wolf = nullptr;
			if (wolf) return wolf;
			for (int i = 0; i < 64 && !wolf; ++i) {
				auto* sp = Arcana::Spell(i);
				if (!sp || std::string_view(sp->GetName()) != "Conjure Wolf") continue;
				for (auto* e : sp->effects) if (e && e->baseEffect && e->baseEffect->data.associatedForm) if (auto* n = e->baseEffect->data.associatedForm->As<RE::TESNPC>()) { wolf = n; break; }
			}
			if (!wolf) if (auto* f = RE::TESForm::LookupByID<RE::TESNPC>(0x00023ABE)) wolf = f;   // (Skyrim's own wolf)
			return wolf;
		}
		void TickSoulcaller(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* sc = Soulcaller();
			if (!p || !sc || !p->Is3DLoaded()) return;
			auto* c = p->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand);
			const bool casting = p->GetEquippedObject(false) == sc && p->AsActorState()->IsWeaponDrawn() && c && c->currentSpell && c->currentSpell == sc->formEnchanting &&
			                     (c->state.get() == RE::MagicCaster::State::kCasting || c->state.get() == RE::MagicCaster::State::kCharging);
			static float tick = 0;
			if (casting) {
				if ((tick -= a_delta) > 0) return;
				tick = 0.25f;
				const RE::NiPoint3 from = Gun::SwingFrom(), dir = Gun::SwingDir();
				for (auto* f : Gun::LineHurt(from, from + dir * 1500.0f, 80.0f, 0.05f * UpgradeMult(kWonderUps))) {
					if (g_souls >= 8 || !g_soulTaken.insert(f->GetFormID()).second) continue;
					++g_souls;
					if (auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kHallGhostShader, gen::kPlugin)) f->ApplyEffectShader(fx, 1.2f);
					Gun::Motes(6, f->GetPosition(), 0.5f, 1.0f);
					Gun::LightningArc(f->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, from, 0.0f);
					Hud::QuestBanner(fmt::format("Souls: {}", g_souls), 0.8f);
				}
				return;
			}
			tick = 0;
			if (g_souls <= 0) { g_soulTaken.clear(); return; }
			// let go: a wolf for each soul, round you, hunting for 20 s
			const int n = g_souls;
			g_souls = 0, g_soulTaken.clear();
			auto* base = SoulWolf();
			const float hp = Director::RoundHealth() * gen::kRounds.hpScale * 1.5f;
			int made = 0;
			for (int k = 0; k < n; ++k) {
				const float a = 6.2832f * k / n;
				const RE::NiPoint3 at = p->GetPosition() + RE::NiPoint3{ std::cos(a) * 140.0f, std::sin(a) * 140.0f, 0.0f };
				if (auto* w = RaiseAlly(p, base, at, 20.0f, hp, 16)) {
					if (auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kHallGhostShader, gen::kPlugin)) w->ApplyEffectShader(fx, 20.0f);
					Gun::Motes(6, at, 0.6f, 1.2f);
					++made;
				}
			}
			Gun::BlastSound("lightmassive", p->GetPosition(), 0.6f);
			logger::info("summoner: the Soulcaller's Staff lets {} soul(s) go - {} spectral wolves hunt", n, made);
		}
	}
	void TickAlchemy(float a_delta)
	{
		TickGoldClouds(a_delta);
		TickShadow(a_delta);
		TickSwords(a_delta);
		g_flaskCool = std::max(0.0f, g_flaskCool - a_delta), g_philCool = std::max(0.0f, g_philCool - a_delta);
		TickGauntletBeam(a_delta);
		TickMutagen(a_delta);
		TickClaws();
		// (Noah: "when swinging the whips, I don't see the whip uncoil") its chain told again each time it's drawn - the
		// variable Animated Armoury's script sets on equipping doesn't outlive the weapon's 3D being put away and remade
		TickScytheSwap();
		TickSoulcaller(a_delta);
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !p->Is3DLoaded() || p->IsDead() || ItemMenusOpen()) return;
		// (Noah: "turn off player voice grunts while casting the flasks") with a flask in hand your voice is quiet: its voice
		// type set aside while one is drawn, given back the moment none is
		if (auto* base = p->GetActorBase()) {
			static RE::BGSVoiceType* saved = nullptr;
			static bool               quiet = false;
			const bool flask = FlaskInHand(false) || FlaskInHand(true);
			// (a voice type of none falls back to the race's own - the grunts went on - so a voice with nothing to say instead)
			static auto* mute = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSVoiceType>(gen::kSilentVoice, gen::kPlugin);
			if (flask && !quiet && mute) saved = base->voiceType, base->voiceType = mute, quiet = true;
			else if (!flask && quiet) base->voiceType = saved, quiet = false;
		}
		// (Noah: "when putting my flask away, the potion sitting on my waist is upside down") a flask hangs from its neck in the
		// hand (tools/flasks.py: the bottle down its -Y), and Skyrim's dagger sheath turns -Y up: on the hip it is turned over,
		// the cork up and the bottle down
		for (const bool left : { false, true }) {
			if (FlaskHeld(left) < 0 || Save::SinceWorldReady() < 6.0f) continue;  // (never while the save is still coming up)
			for (const bool first : { false, true }) {
				auto* root = p->Get3D(first);
				auto* sheath = root ? root->GetObjectByName(left ? "WeaponDaggerLeft" : "WeaponDagger") : nullptr;
				auto* node = sheath ? sheath->AsNode() : nullptr;
				if (!node) continue;
				for (auto& kid : node->GetChildren()) {
					if (!kid) continue;
					RE::NiMatrix3 flip;
					flip.entry[0][0] = -1.0f, flip.entry[1][1] = -1.0f, flip.entry[2][2] = 1.0f;  // (half a turn about its Z)
					flip.entry[0][1] = flip.entry[0][2] = flip.entry[1][0] = flip.entry[1][2] = flip.entry[2][0] = flip.entry[2][1] = 0.0f;
					if (kid->local.rotate.entry[0][0] > -0.99f || kid->local.rotate.entry[1][1] > -0.99f) kid->local.rotate = flip;  // (the body's own update carries it)
					kid->local.translate = {}, kid->local.scale = 1.0f;   // (the hand's placement - in the palm, Gun's PlaceHeldFlask - left behind)
				}
			}
		}
		static float look = 0;
		if ((look -= a_delta) > 0) return;
		look = 1.0f;
		// (the Alchemist's set changed from the Drifter's outfit to the Black Mage's) an Alchemist still carrying the Drifter's
		// pieces has them taken back and the robes given at the set's tier, worn
		if (static bool swapped = false; !swapped && g_class.load() == coffer::kAlchemist && g_kit[coffer::kAlchemist]) {
			swapped = true;
			int had = 0;
			for (std::uint32_t id : { 0x831u, 0x830u, 0x832u, 0x80Eu, 0x80Du, 0x84Du, 0x81Du, 0x81Cu, 0x84Cu, 0x841u }) {
				auto* f = Lookup(id, "DrifterArmorAndOutfitByXtudo.esp");
				auto* b = f ? f->As<RE::TESBoundObject>() : nullptr;
				if (b && p->GetItemCount(b) > 0) {
					if (auto* em = RE::ActorEquipManager::GetSingleton()) em->UnequipObject(p, b, nullptr, 1, nullptr, false, false, false, true);
					p->RemoveItem(b, p->GetItemCount(b), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr), ++had;
				}
			}
			if (had) {
				GiveArmourSet(coffer::kAlchemist, Kit::ArmourTier(coffer::kAlchemist), true);
				logger::info("classes: the Alchemist's Drifter pieces ({}) traded for the Black Mage Robes", had);
			}
		}
		for (int el = 0; el < 7; ++el) {  // an Alchemist's flasks never leave you (dropped or sold: back in the pack)
			auto* it = FlaskItem(el);
			const bool owned = el == 6 ? WonderState(coffer::kAlchemist) >= 4 : Kit::FlaskTier(el) >= 0;
			if (it && owned && p->GetItemCount(it) <= 0) p->AddObjectToContainer(it, nullptr, 1, nullptr);
			// (the flasks were potions for a build: those taken back)
			if (auto* old = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESBoundObject>(gen::kFlaskPotion[el], gen::kPlugin); old && p->GetItemCount(old) > 0) p->RemoveItem(old, p->GetItemCount(old), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
	}
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
		// (Noah: "for the Spellsword class the female should have the madness sword as well") one blade for every Spellsword
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
		// (Noah, from the armour lookbook: "for Summoner use #9 without the gauntlets. for Destruction use #7 mage robes without
		// the gauntlets. for Alchemist use #33 without the gauntlets. for Spellsword use #6" - the gauntlets clipped the cloth;
		// "exactly as previewed": each set in its own mod's colours, the class colour only in the upgrade aura)
		std::vector<Piece> SetPieces(int a_cls, int, bool)
		{
			switch (a_cls) {
			case coffer::kSummoner:  // Calling of the Conjurer's Conjurer's Robe (Rogues 'n Raiders), its boots, the Arch Conjurer's hood
				return { { "Rogues 'n Raiders.esp", 0x8C94A2 }, { "Rogues 'n Raiders.esp", 0x2998A6 }, { "Rogues 'n Raiders.esp", 0x91A4FB } };
			case coffer::kDestruction:  // Black Mage Armor SE: the College robes, the leather hood, the College boots
				return { { "Black Mage Armor SE.esp", 0x0146A5 }, { "Black Mage Armor SE.esp", 0x000D71 }, { "Black Mage Armor SE.esp", 0x0146A6 } };
			case coffer::kAlchemist:  // the Gondorian Ranger's cuirass and boots, the Ithilien hood
				return { { "GondorianRangerArmor.esp", 0x000D64 }, { "GondorianRangerArmor.esp", 0x000D66 }, { "GondorianRangerArmor.esp", 0x000D63 } };
			case coffer::kSpellsword:  // Black Mage Armor SE: the Black Archmage's plate - cuirass, helm, gauntlets, sabatons
				return { { "Black Mage Armor SE.esp", 0x00DA79 }, { "Black Mage Armor SE.esp", 0x00DFDD }, { "Black Mage Armor SE.esp", 0x00EAC2 }, { "Black Mage Armor SE.esp", 0x00EAC1 } };
			default: return {};
			}
		}
		// the sets the classes wore before (taken back from a character still carrying them)
		std::vector<Piece> OldSetPieces(int a_cls, int a_tier, bool a_female)
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
			case coffer::kAlchemist:  // (Noah: "Could we use the destruction class armor set for male and female alchemist class?") the
				// Black Mage's robes, as the Destruction class wears them - made for both bodies (the Drifter's outfit had no man's)
				return OldSetPieces(coffer::kDestruction, a_tier, a_female);
			case coffer::kSpellsword:
				// (Noah: "remove the cape from the [Spellsword] class, just so that we don't have to worry about it anymore")
				if (a_female) return { { "ValkyrieSwordShield.esp", 0x00285D }, { "ValkyrieSwordShield.esp", 0x00285A }, { "ValkyrieSwordShield.esp", 0x00285B }, { "ValkyrieSwordShield.esp", 0x002859 } };
				// (Noah: "change the spell sword helmet for the male character to the other option ... I don't like the way the Dragon Lord
				// plume helmet looks") the DragonLord Helmet, not the plume
				return { { "DragonLordSwords.esp", 0x0012D3 }, { "DragonLordSwords.esp", 0x0012CF }, { "DragonLordSwords.esp", 0x0012CE }, { "DragonLordSwords.esp", 0x0012CD } };
			default: return {};
			}
		}
		// (Noah: "the armor kits for all classes to stay the same visually through all the upgrades") every tier wears the
		// set's first look; an upgrade makes it glow as if enchanted (TickArmour)
		std::vector<RE::TESObjectARMO*> SetFormsAt(int a_cls, int a_look)
		{
			std::vector<RE::TESObjectARMO*> out;
			for (const auto& pc : SetPieces(a_cls, a_look, PlayerFemale())) {
				if (auto* f = Lookup(pc.id, pc.plugin)) if (auto* a = f->As<RE::TESObjectARMO>()) out.push_back(a);
			}
			return out;
		}
		std::vector<RE::TESObjectARMO*> SetForms(int a_cls, int)
		{
			std::vector<RE::TESObjectARMO*> out;
			for (const auto& pc : SetPieces(a_cls, 0, PlayerFemale())) {
				if (auto* f = Lookup(pc.id, pc.plugin)) if (auto* a = f->As<RE::TESObjectARMO>()) out.push_back(a);
			}
			return out;
		}
		// every piece of every tier the class's set has had (the Battle-mage's earlier looks)
		std::vector<RE::TESObjectARMO*> AllSetForms(int a_cls)
		{
			std::vector<RE::TESObjectARMO*> out;
			for (auto* a : SetFormsAt(a_cls, 0)) if (std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);  // (the earlier tiers' looks, taken back)
			for (int i = 0; i < 4; ++i) {   // (the class-coloured Black Mage robes every class wore before the lookbook's sets)
				if (auto* f = Lookup(gen::kClassArmour[a_cls][i], gen::kPlugin.data())) if (auto* a = f->As<RE::TESObjectARMO>(); a && std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
			}
			for (int t = 0; t <= 3; ++t) {   // (and the sets the classes wore before every class wore the Black Mage's)
				for (const bool fem : { false, true }) {
					for (const auto& pc : OldSetPieces(a_cls, t, fem)) {
						if (auto* f = Lookup(pc.id, pc.plugin)) if (auto* a = f->As<RE::TESObjectARMO>(); a && std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
					}
				}
			}
			return out;
		}
	}
	std::string ArmourSetName(int a_cls, int a_tier)
	{
		if (a_cls < 0 || a_cls > 3) return "Armor";
		static constexpr const char* kName[4] = { "Summoner Set", "Destruction Set", "Alchemist Set", "Spellsword Set" };   // (Noah: "renamed to their class like \"Alchemist Set\"")
		return a_tier >= 1 ? fmt::format("{} (Enchanted {})", kName[a_cls], a_tier >= 3 ? "III" : a_tier == 2 ? "II" : "I") : std::string(kName[a_cls]);
	}

	// (Noah: "the new armor sets also need to be renamed to their class like \"Alchemist Set\" or Destruction Set") each piece
	// named for its class's set and its place in it
	void NameSetPieces()
	{
		static constexpr const char* kSet[4] = { "Summoner Set", "Destruction Set", "Alchemist Set", "Spellsword Set" };
		using S = RE::BGSBipedObjectForm::BipedObjectSlot;
		for (int c = 0; c < 4; ++c) {
			for (auto* a : SetForms(c, 0)) {
				const char* part = a->HasPartOf(S::kBody) ? (c <= 1 ? "Robes" : "Cuirass") : (a->HasPartOf(S::kHead) || a->HasPartOf(S::kHair)) ? (c == 3 ? "Helm" : "Hood") : a->HasPartOf(S::kHands) ? "Gauntlets" : a->HasPartOf(S::kFeet) ? (c == 3 ? "Sabatons" : "Boots") : "Piece";
				a->fullName = fmt::format("{} {}", kSet[c], part);
			}
		}
	}
	bool ArmourCarried(int a_cls, int a_tier)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return false;
		for (auto* a : SetForms(a_cls, a_tier)) if (p->GetItemCount(a) > 0) return true;
		return false;
	}
	// (Noah: the Madness Sword for a woman Spellsword too) Odin's Wrath, plain or awoken, goes back; the Madness Sword in its place
	void ReplaceOdinsWrath(RE::PlayerCharacter* p)
	{
		if (!p) return;
		bool held = false, had = false;
		for (const std::uint32_t id : { 0x00435Au, 0x000D62u }) {
			auto* f = Lookup(id, "ValkyrieSwordShield.esp");
			auto* w = f ? f->As<RE::TESBoundObject>() : nullptr;
			if (w && p->GetItemCount(w) > 0) {
				held |= p->GetEquippedObject(false) == w;
				had = true;
				p->RemoveItem(w, p->GetItemCount(w), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
		}
		if (had) {
			if (auto* blade = SpellswordBlade(); blade && p->GetItemCount(blade) <= 0) GiveItem(p, blade, 1, held);
			logger::info("classes: Odin's Wrath taken back - the Madness Sword in its place");
		}
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
		// (Noah: "I don't see the destruction class's full armor set in my inventory, I only see the destruction hood" - the
		// Alchemist's old look was the Destruction set's robes and boots, taken back when the test kit gave the Alchemist's
		// set after it) no class's current set is ever taken back
		std::vector<RE::TESObjectARMO*> keep;
		for (int k = 0; k < 4; ++k) for (auto* a : SetForms(k, 0)) keep.push_back(a);
		for (auto* a : AllSetForms(a_cls)) {
			if (std::find(want.begin(), want.end(), a) != want.end() || std::find(keep.begin(), keep.end(), a) != keep.end()) continue;
			if (const int n = p->GetItemCount(a); n > 0) p->RemoveItem(a, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		for (auto* a : want) GiveItem(p, a, p->GetItemCount(a) > 0 ? 0 : 1, a_wear);
		if (a_cls == coffer::kSpellsword) ReplaceOdinsWrath(p);
		logger::info("classes: {} - {} (tier {}) given{}", Name(a_cls), ArmourSetName(a_cls, a_tier), a_tier, a_wear ? ", worn" : "");
	}
	// every second: the set's pieces as the rules want them (light, weight 1, the set's armour shared out), the Ranger's stride
	void TickArmour()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		const int c = g_class.load();
		if (!p || c < 0) return;
		if (c == coffer::kSpellsword && p->Is3DLoaded() && !ItemMenusOpen()) ReplaceOdinsWrath(p);
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
		// (Noah: "the upgraded version to make the item look enchanted") the set's glow, its class's colour, from Tier I
		// (Noah: "give the player an aura when the armor is upgraded", faint to blazing) Tier I a sheen, Tier II wisps rising off you,
		// Tier III a full aura, each Ascend (three steps) brighter
		if (const int at = std::min(6, Kit::ArmourTier(c)); at >= 1 && p->Is3DLoaded() && !p->IsDead()) {
			static float renew = 0;
			static int   was = 0;
			renew -= 1.0f;
			if (renew <= 0 || at != was) {
				renew = 2.0f, was = at;
				if (auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kArmGlow[c * 6 + at - 1], gen::kPlugin)) p->ApplyEffectShader(fx, 3.2f, nullptr, false, false, nullptr, false);
			}
		}
		// (the Gondorian set, Tier I) the Ranger's stride: 10% faster
		if (auto* swift = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(gen::kSetSwift, gen::kPlugin)) {
			const bool want = false;  // (the Ranger's stride: gone with the Ranger)
			if (want != p->HasSpell(swift)) {
				if (want) p->AddSpell(swift); else p->RemoveSpell(swift);
				auto* av = p->AsActorValueOwner();  // (a speed change shows once the carry weight is touched)
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.1f);
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.1f);
			}
		}
	}
}
