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
		std::uint8_t                                           g_guide = 0;    // (the guided first night) which of its cards have been shown (bits)
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
				if (auto* em = RE::ActorEquipManager::GetSingleton()) em->EquipObject(a_p, obj, nullptr, 1, nullptr, false, false, false, true);
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

		int ConjurerStaff()
		{
			for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) if (gen::kGuns[i].id == "gravecaller") return i;
			return -1;
		}
		bool g_staffChecked = false;
		// (Noah: "instead of a spiderweb bolt, another spell from our mods that makes sense - not one the Echo Altar or the
		// other classes use") the Conjurer's Staff casts Mysticism's Armoreater: its rust-red charge in the staff's head as it
		// charges, its corroding mass bolt, and its rust burst wherever the bolt lands - an armor-eating mark that your
		// summons fall on. The bolt's own effect is ours (no armor taken: Classes::ConjurerStaffHit does the burst and marks);
		// its look - projectile, burst, charge art, hit art, impact set, light and sounds - is the Armoreater's, set once.
		void DressStaffBolt(float a_radius)
		{
			const int i = ConjurerStaff();
			auto*     w = i >= 0 ? Forms::Gun(i) : nullptr;
			auto*     ench = w ? w->formEnchanting : nullptr;
			if (!ench || ench->effects.empty() || !ench->effects[0] || !ench->effects[0]->baseEffect) return;
			auto* ours = ench->effects[0]->baseEffect;
			static RE::EffectSetting* theirs = RE::TESDataHandler::GetSingleton()->LookupForm<RE::EffectSetting>(0xAC7E88, "MysticismMagic.esp");  // MAG_DamageArmorFFAimedArea75 (Armoreater)
			if (!theirs) {
				static bool warned = false;
				if (!warned) warned = true, logger::warn("classes: Mysticism's Armoreater not found - the staff keeps its green bolt");
				return;
			}
			static bool dressed = false;
			if (!dressed) {
				dressed = true;
				auto& d = ours->data;
				const auto& t = theirs->data;
				d.projectileBase = t.projectileBase, d.explosion = Gun::Quiet(t.explosion), d.castingArt = t.castingArt, d.hitEffectArt = t.hitEffectArt;
				d.impactDataSet = t.impactDataSet, d.light = t.light, d.effectShader = t.effectShader, d.dualCastData = t.dualCastData;
				ours->effectSounds = theirs->effectSounds;
				logger::info("classes: the Conjurer's Staff casts Armoreater (projectile {:08X}, burst {:08X})", d.projectileBase ? d.projectileBase->GetFormID() : 0, d.explosion ? d.explosion->GetFormID() : 0);
			}
			// its burst reaches as far as the staff's splash (3 m, a metre more each reforge)
			if (auto* ex = ours->data.explosion) ex->data.radius = a_radius;
			ench->effects[0]->effectItem.area = static_cast<std::uint32_t>(a_radius / 21.33f + 0.5f);  // (feet: everyone in it is struck - and marked)
		}
		RE::BGSExplosion* StaffBurstLook()
		{
			static auto* m = RE::TESDataHandler::GetSingleton()->LookupForm<RE::EffectSetting>(0xAC7E88, "MysticismMagic.esp");
			return m ? m->data.explosion : nullptr;
		}
		void GiveConjurerStaff()
		{
			// (Noah: "remake the staff") a plain Skyrim staff now - in your hand, never in a gun slot (a slot that held it
			// from before is emptied)
			const int i = ConjurerStaff();
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* w = i >= 0 ? Forms::Gun(i) : nullptr;
			if (!p || !w) return;
			for (int s = 0; s < 3; ++s) {
				if (Save::GetSlot(s).gun == i) Save::SetSlot(s, {});
			}
			if (p->GetItemCount(w) <= 0) {
				GiveItem(p, w, 1, true);
				logger::info("classes: the Conjurer's Staff given");
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
				GiveConjurerStaff();  // (Noah) the staff whose bolts tell your summons what to kill
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
		bool ClassGear(RE::TESBoundObject* a_obj, const RE::InventoryEntryData* a_entry)
		{
			if (!a_obj || (a_entry && a_entry->IsQuestObject())) return false;
			const auto t = a_obj->GetFormType();
			if (t != RE::FormType::Weapon && t != RE::FormType::Armor && t != RE::FormType::Ammo && t != RE::FormType::Scroll) return false;
			if (a_obj == Underwear()) return false;  // (worn by every class)
			// our own plugin's pieces are the night's rewards (Alduin's Bane, the hall's relics), kept by every class - but the
			// class weapons it makes (wands, tomes, bows, the Conjurer's Staff) belong to their class
			if (auto* f = a_obj->GetFile(0); f && std::string_view(f->GetFilename()) == gen::kPlugin && Forms::GunIndexOf(a_obj) < 0) return false;
			return true;
		}
		RE::TESObjectREFR* ChestOf(int a_cls, bool a_make)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p || a_cls < 0 || a_cls >= 4) return nullptr;
			if (g_chest[a_cls]) {
				if (auto* r = RE::TESForm::LookupByID<RE::TESObjectREFR>(g_chest[a_cls]); r && !r->IsDeleted()) return r;
				g_chest[a_cls] = 0;
			}
			if (!a_make) return nullptr;
			auto* base = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESBoundObject>(gen::kClassChest, gen::kPlugin);
			auto  ref = base ? p->PlaceObjectAtMe(base, true) : nullptr;
			if (!ref) return nullptr;
			ref->Disable();
			g_chest[a_cls] = ref->GetFormID();
			logger::info("classes: {}'s chest made ({:08X})", Name(a_cls), ref->GetFormID());
			return ref.get();
		}
		// everything the class you leave was carrying (its weapons, armour, ammunition and scrolls; the night's relics, quest
		// items and your underwear stay) into its chest; what it had on, and its weapon slots, noted
		void StashGear(int a_cls)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* chest = ChestOf(a_cls, true);
			if (!p || !chest) return;
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
						if (!x) continue;
						const std::int32_t c = std::max<std::int32_t>(1, x->GetCount());
						moves.push_back({ obj, std::min(c, left), x });
						left -= std::min(c, left);
					}
				}
				if (left > 0) moves.push_back({ obj, left, nullptr });
			}
			for (auto& m : moves) p->RemoveItem(m.obj, m.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, m.x, chest);
			logger::info("classes: {}'s gear put away ({} stacks)", Name(a_cls), moves.size());
		}
		// a class you come back to: its chest emptied back into your pack, its pieces back on, its slots back
		bool RestoreGear(int a_cls)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* chest = ChestOf(a_cls, false);
			if (!p || !chest) return false;
			struct Move { RE::TESBoundObject* obj; std::int32_t count; RE::ExtraDataList* x; };
			std::vector<Move> moves;
			for (auto& [obj, e] : chest->GetInventory()) {
				if (!obj || e.first <= 0) continue;
				std::int32_t left = e.first;
				if (e.second && e.second->extraLists) {
					for (auto* x : *e.second->extraLists) {
						if (!x) continue;
						const std::int32_t c = std::max<std::int32_t>(1, x->GetCount());
						moves.push_back({ obj, std::min(c, left), x });
						left -= std::min(c, left);
					}
				}
				if (left > 0) moves.push_back({ obj, left, nullptr });
			}
			for (auto& m : moves) chest->RemoveItem(m.obj, m.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, m.x, p);
			for (int s = 0; s < 3; ++s) Save::SetSlot(s, g_slotStash[a_cls][s]);
			if (auto* em = RE::ActorEquipManager::GetSingleton()) {
				for (auto id : g_wornStash[a_cls]) {
					auto* o = RE::TESForm::LookupByID<RE::TESBoundObject>(id);
					if (o && (o->IsArmor() || o->GetFormType() == RE::FormType::Ammo || (o->IsWeapon() && Forms::GunIndexOf(o) < 0)) && p->GetItemCount(o) > 0) em->EquipObject(p, o, nullptr, 1, nullptr, false, false, false, true);
				}
			}
			logger::info("classes: {}'s gear taken back out ({} stacks)", Name(a_cls), moves.size());
			return true;
		}

		// the class's spells off you (kept for when you come back to it), the new class's back on
		void Swap(int a_from, int a_to)
		{
			if (a_from == coffer::kSummoner && a_to != coffer::kSummoner) DismissSummons();
			if (a_from >= 0 && a_from < 4 && a_from != a_to) StashGear(a_from), g_perkStash[a_from] = Perks::TakeAll();
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
				RestoreGear(a_to);
				if (!g_perkStash[a_to].empty()) Perks::GiveBack(g_perkStash[a_to]), g_perkStash[a_to].clear();
				bool left = false;
				for (auto [i, lv] : g_stash[a_to]) {
					GiveSpell(i, left);
					Arcana::SetLevel(i, lv);
					left = !left;
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
		constexpr int kWonderSteps = 3, kWonderOpensAt = 6;  // (g_wStep: 0 shut, 1-3 its quest open, 4 won)
		constexpr const char* kWonderName[4] = { "Horn of the Spectral Wyrm", "Tome of the Six Fires", "Subsuming Vortex", "Dragon Slayer" };
		std::array<std::uint8_t, 4>  g_wStep{};    // per class: 0 not open, 1..3 the step under way, 4 earned
		std::array<std::uint16_t, 4> g_wKills{};
		std::array<std::uint32_t, 4> g_wTowns{};   // cities where a step was done (bits)
		std::unordered_map<RE::FormID, RE::FormID> g_lastHit;  // a foe -> what you last hit it with
		std::unordered_set<RE::FormID> g_powerHit;  // foes your last blow on was a power attack (the Spellsword's resonance)
		std::uint16_t g_boltKills = 0;
		std::array<std::uint16_t, 16> g_bowMastery{};  // (Noah: "make the ranger class also need kills") each altar bow's kills toward its next tier  // (the Ranger's crossbow mastery) kills with a crossbow: fire bolts at 25, ice at 60, shock at 120
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
		float g_tomeCool = 0, g_bladeCool = 0, g_vortexCool = 0;
		struct VortexArrowState { RE::NiPoint3 last; bool done, hit; };
		std::unordered_map<RE::FormID, VortexArrowState> g_vortexArrows;  // the Subsuming Vortex's arrows in flight

		int FreedCount()
		{
			int n = 0;
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) n += Curse::Freed(t) ? 1 : 0;
			return n;
		}
		bool AfterSovngarde() { return Story::AlduinFallen(); }
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
				if (auto* sp = WyrmSpell()) p->AddSpell(sp);
				break;
			case coffer::kDestruction:
				if (auto* sp = TomeSpell()) p->AddSpell(sp);
				break;
			case coffer::kRanger:
				GiveItem(p, Vortex(), 1, true);
				GiveItem(p, VortexArrow(), 60, true);
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
				"A bow of black-violet steel. Every arrow tears open a black hole where it lands: the dead are dragged in, then it bursts. After Sovngarde the holes grow wider and pull harder. More arrows: Q at an Echo Altar, or a Max Ammo.",
				"The Akaviri dragon slayer. Every hit carries the spell in your other hand. After Sovngarde a power attack sends that spell down the line." };
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
				if (const int b = Bows::IndexOf(w); b >= 0 && b < static_cast<int>(g_bowMastery.size()) && (FreedCount() >= 3 || Gun::TestKitOn())) g_bowMastery[b] = static_cast<std::uint16_t>(std::min(60000, g_bowMastery[b] + 1));
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
			else if (const int b = Bows::Held(); b >= 0) {  // (the altar's bows strike with our own shots)
				how = 2;
				if (b < static_cast<int>(g_bowMastery.size()) && (FreedCount() >= 3 || Gun::TestKitOn())) g_bowMastery[b] = static_cast<std::uint16_t>(std::min(60000, g_bowMastery[b] + 1));
			}
			else if (p) {
				for (int h : Arcana::Hands()) if (h >= 0 && Arcana::ClassOf(h) == coffer::kDestruction) how = 1;
			}
			if (how == c) WonderKill(c);  // (the classes' kinds of kill line up with the class order)
			int element = -1;
			if (const int ci = src ? Arcana::IndexOf(src) : -1; ci >= 0) element = coffer::kSpells[ci].element;
			const float dist = a_dying && p ? a_dying->GetPosition().GetDistance(p->GetPosition()) : 0.0f;
			if (how >= 0) Curse::OnClassKill(how, element, dist);
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
		// (Noah's plan, the Spellsword's resonance) its touch spells climb on melee kills only - a power attack counts twice
		// (Noah: "make kills count after the forge opens") nothing counts toward a climb before three altars are freed
		const bool forgeOpen = FreedCount() >= 3 || Gun::TestKitOn();
		const bool power = a_dying && g_powerHit.contains(a_dying->GetFormID());
		for (int h : Arcana::Hands()) {
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
		out.push_back(fmt::format("The altar's forge (raising your spells and bows): {}. Your class's Legendary row: {}.",
			freed >= 3 ? std::string("<b>open</b>") : fmt::format("opens at 3 freed altars ({} to go)", 3 - freed),
			freed >= 6 ? std::string("<b>open</b>") : fmt::format("opens at 6 freed altars ({} to go)", 6 - freed)));
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
				int imbued = 0;
				for (int i : Arcana::Carried()) if (Arcana::ClassOf(i) == coffer::kSpellsword) imbued += Arcana::RungOf(i) - 1;
				out.push_back(fmt::format("<b>Imbues: {}.</b> Enchantment Powers unlocked: {} of 4 (one for each imbue, in your Powers).", imbued, std::min(imbued, 4)));
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
		if (static int purge = 0; ++purge % 3 == 0 && p->Is3DLoaded()) Purge();
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
					card(0, "THE ECHO ALTAR", "Walk to Riverwood's Echo Altar and press Activate to choose your class. Every city has an altar: it is where you spend septims on spells, blessings and upgrades.");
				} else if (!fighting && !(g_guide & 2)) {
					card(1, "SEPTIMS", "Every hit and every kill on the dead earns septims - your total sits at the top of the screen. Spend them at an Echo Altar between waves and by day.");
				} else if (fighting && hs.wave <= 1 && !(g_guide & 4)) {
					card(2, "THE NIGHT", "At dusk the dead rise in waves. The counter at the top shows how many are left in this wave. Clear every wave and dawn breaks. Fall, and the wave begins again - and your blessings are lost.");
				} else if (fighting && hs.wave >= 2 && !(g_guide & 8)) {
					card(3, "BETWEEN WAVES", "A few seconds' breath before the next wave rises: heal, reload, find better ground, or run for an altar. The last few of every wave come straight for you.");
				} else if (!fighting && hs.round >= 1 && (g_guide & 4) && !(g_guide & 16)) {
					card(4, "DAWN", "The dead crumble at first light. Spend your septims, then rest until dusk - every night is harder than the last.");
				}
			}
		}
		// (Noah) a Summoner from before the staff came gets it once, a moment into the game
		if (!g_staffChecked && p->Is3DLoaded() && g_class.load() == coffer::kSummoner && g_kit[coffer::kSummoner]) {
			g_staffChecked = true;
			GiveConjurerStaff();
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
			const bool  ranged = wp && (wp->IsBow() || wp->IsCrossbow());
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
		// (Noah: "summoner should start with 2 summon slots and go up for every 2 echo altars freed - max 7")
		int cap = 1;
		if (g_class.load() == coffer::kSummoner) {
			cap = std::min(7, 2 + FreedCount() / 2);
			if (Save::PerkStacks(gen::kPerk_command) > 0) cap = std::min(8, cap + 1);  // (the Conjurer's Command Mead: one more - 8 at most)
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

	// (Noah's plan, the Summoner: Calling of the Conjurer's heroes and armies as late Legendaries) the altar's second row
	// binds a hero or an army - (Noah: "keep the 6 free cities gate instead of both") its only gate is the altar's six
	// freed cities (src/Shops.cpp), no Legendary family needed
	bool RollHero(Prize* a_out)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return false;
		const int freed = std::max(FreedCount(), 4);  // (past the altar's gate of six: the armies answer too)
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

	bool WonderOpen()
	{
		const int c = g_class.load();
		return c >= 0 && c < 4 && g_wStep[c] >= 1 && g_wStep[c] <= kWonderSteps;
	}
	void EarnWonder(int a_cls)
	{
		if (a_cls >= 0 && a_cls < 4 && g_wStep[a_cls] <= kWonderSteps) GrantWonder(a_cls);
	}
	bool WonderEarned()
	{
		for (auto s : g_wStep) if (s > kWonderSteps) return true;
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
		if (st > kWonderSteps) return std::string(kWonderName[c]) + " is yours";
		if (st == 0) return fmt::format("{}: free {} more {} to wake your path's secret", kWonderName[c], std::max(0, kWonderOpensAt - FreedCount()), kWonderOpensAt - FreedCount() == 1 ? "city" : "cities");
		return std::string(kWonderName[c]) + ": " + Eggs::Line();
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
		if (auto* w = src->As<RE::TESObjectWEAP>(); w && !w->IsBow() && !w->IsCrossbow() && !w->IsStaff()) Perks::OnMeleeHit();  // (Bladesong Mead)
		if (src == Vortex()) {
			// (its arrow found one of the dead: the hole opens on it - TickFast sees the arrow gone and lets it be)
			OpenVortex(a_target->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, true);
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
		void TickSummons(float a_delta)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return;
			DressStaffBolt(210.0f + 70.0f * static_cast<float>(g_staffLevel));
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
				for (auto* s : mine) {
					if (s->IsDead() || s->IsDisabled()) continue;
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
						sav0->SetActorValue(RE::ActorValue::kAttackDamageMult, 0.1f);
						logger::info("summons: {} {:08X} ({:.0f} health) joined", s->GetName(), s->GetFormID(), sav0->GetPermanentActorValue(RE::ActorValue::kHealth));
					}
					// (Noah: "summons stay within 20 meters of the player at all times - they wander off and fight") past 20 m it
					// drops its fight and comes back to you; past 34 m it is brought to your side
					{
						const float pd = s->GetPosition().GetDistance(p->GetPosition());
						if (pd > 2400.0f || (pd > 1400.0f && !s->Is3DLoaded())) {
							const float yaw = p->GetAngleZ() + 2.4f;
							s->StopCombat();
							s->SetPosition(p->GetPosition() + RE::NiPoint3{ std::sin(yaw) * 180.0f, std::cos(yaw) * 180.0f, 0.0f }, true);
							continue;
						}
						if (pd > 1400.0f) {
							s->StopCombat();
							s->EvaluatePackage(true, false);
							continue;
						}
						if (auto t = s->GetActorRuntimeData().currentCombatTarget.get(); t && t->GetPosition().GetDistance(p->GetPosition()) > 1500.0f) {
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
						if (cur.get() != m.get()) {
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
						if (z->GetPosition().GetDistance(p->GetPosition()) > 1400.0f) continue;  // (only what comes within 20 m of you)
						if (z->IsCommandedActor() && z->GetCommandingActor().get() == p) continue;
						if (const float d = z->GetPosition().GetDistance(s->GetPosition()); d < bestD) bestD = d, best = z.get();
					}
					if (best) {
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
		const int i = ConjurerStaff();
		auto*     w = i >= 0 ? Forms::Gun(i) : nullptr;
		if (!w || !a_target || (a_source != w->GetFormID() && !(w->formEnchanting && a_source == w->formEnchanting->GetFormID()))) return;
		static std::chrono::steady_clock::time_point last{};
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(200)) return;  // (one bolt, one burst)
		last = now;
		// (Noah: "the Echo Altar should increase the area of damage on impact, and the damage itself - and any enemy within
		// the damage area gets marked for the summons") a burst round what it hit: 3 m and a fifth of one of the dead's
		// health at first, each reforge 1 m wider and harder; everyone in it is marked
		const float radius = 210.0f + 70.0f * static_cast<float>(g_staffLevel);
		const float share = 0.2f + 0.1f * static_cast<float>(g_staffLevel);
		const auto  at = a_target->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f };
		Gun::AreaHurt(at, radius, share, "");
		// the rust burst grown to the splash's own reach (a reforged staff's wider ring shows as wider)
		if (g_staffLevel > 0) Gun::FxFrom(StaffBurstLook(), a_target->GetPosition() + RE::NiPoint3{ 0, 0, 20.0f }, radius / 210.0f);
		std::vector<RE::ActorHandle> marks;
		for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
			auto z = h.get();
			if (z && !z->IsDead() && !z->IsDisabled() && Director::IsFoe(z.get()) && z->GetPosition().GetDistance(at) <= radius + 40.0f) marks.push_back(z->GetHandle());
		}
		if (Director::IsFoe(a_target) && !a_target->IsDead() && std::none_of(marks.begin(), marks.end(), [&](const RE::ActorHandle& m) { return m.get().get() == a_target; })) marks.push_back(a_target->GetHandle());
		if (!marks.empty()) {
			g_summonMarks = std::move(marks);
			static int told = 0;
			if (told++ < 10) logger::info("classes: your summons turn on {} marked (level {} staff)", g_summonMarks.size(), g_staffLevel);
		}
	}
	void MarkForSummons(RE::Actor* a_target)
	{
		if (!a_target || a_target->IsDead()) return;
		g_summonMarks = { a_target->GetHandle() };
	}
	bool HoldsConjurerStaff()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		const int i = ConjurerStaff();
		auto* w = i >= 0 ? Forms::Gun(i) : nullptr;
		return p && w && (p->GetEquippedObject(false) == w || p->GetEquippedObject(true) == w);
	}
	// (Noah: "upgrading the summon staff at the Echo Altar") the altar's Reforge row with the staff in hand: three steps,
	// the same prices as a spell's
	bool ReforgeStaff()
	{
		const std::string name = "Conjurer's Staff";
		if (g_staffLevel >= gen::kForgeMax) {
			Hud::Banner(name + " cannot hold more power", 2.5f);
			return false;
		}
		if (!Points::Spend(gen::kForgeCost[g_staffLevel])) return false;
		++g_staffLevel;
		Hud::Banner(name + " reforged  " + std::string(g_staffLevel, '|') + fmt::format("  - its burst {} m, harder", 3 + g_staffLevel), 4.0f);
		logger::info("forge: Conjurer's Staff -> level {}", g_staffLevel);
		return true;
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
		return v;
	}
	void SetState(const std::vector<std::uint8_t>& a_v)
	{
		g_class = -1, g_kit.fill(false), g_staffLevel = 0;
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
		if (g_wStep[coffer::kSummoner] > kWonderSteps) TuneWyrm();
		logger::info("classes: loaded ({})", Name(g_class));
	}
	void Revert() { SetState({}); g_open = false; g_mastery.fill(0); g_staffChecked = false; g_summonMarks.clear(); }
}
