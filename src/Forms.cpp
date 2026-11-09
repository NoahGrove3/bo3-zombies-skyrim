#include <shlobj.h>
#include "BO3Z.h"

namespace bo3z
{
	namespace
	{
		std::array<RE::TESBoundObject*, gen::kEnemies.size()>        g_enemies{};
		std::array<RE::BGSSoundDescriptorForm*, gen::kSounds.size()> g_sounds{};
		std::array<RE::AlchemyItem*, gen::kPerks.size()>             g_potions{};
		std::array<RE::FormID, gen::kPerks.size()>                   g_effects{};
		std::array<RE::TESObjectACTI*, gen::kObjects.size()>         g_objects{};
		RE::TESQuest*      g_intro = nullptr;
		RE::TESGlobal*     g_quick = nullptr;
		RE::TESObjectREFR* g_riverwood = nullptr;
		std::array<RE::TESQuest*, 3> g_mq102{};

		RE::TESForm* Lookup(std::string_view a_plugin, std::uint32_t a_id, std::string_view a_what)
		{
			auto* form = RE::TESDataHandler::GetSingleton()->LookupForm(a_id, a_plugin);
			if (!form) {
				logger::error("missing form {} {:06X} ({})", a_plugin, a_id, a_what);
			}
			return form;
		}
		RE::TESForm* Lookup(const gen::FormRef& a_ref) { return Lookup(a_ref.plugin, a_ref.localId, a_ref.editorId); }
	}

	namespace Forms
	{
		void CastOnce(RE::TESObjectREFR* a_from, RE::SpellItem* a_spell, RE::TESObjectREFR* a_target, float a_strength, RE::Actor* a_blame)
		{
			if (!a_from || !a_spell) return;
			auto* c = a_from->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			if (!c) return;
			c->CastSpellImmediate(a_spell, false, a_target, a_strength, false, 0.0f, a_blame);
			if (a_spell->GetCastingType() != RE::MagicSystem::CastingType::kConcentration) return;
			// held spells: one short burst, then the caster lets go (and again a little later, in case it took hold late)
			const auto h = a_from->CreateRefHandle();
			for (float sec : { 0.25f, 0.65f }) {  // (one timer, no thread per cast)
				Tasks::Later(sec, [h] {
					if (auto r = h.get()) {
						if (auto* mc = r->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) mc->InterruptCast(false);
					}
				});
			}
		}

		bool Resolve()
		{
			bool ok = true;
			for (std::size_t i = 0; i < gen::kEnemies.size(); ++i) {
				auto* f = Lookup(gen::kEnemies[i].form);
				g_enemies[i] = f ? f->As<RE::TESBoundObject>() : nullptr;  // (a creature mod missing: its rows sit out, the rest still rise)
			}
			for (std::size_t i = 0; i < gen::kSounds.size(); ++i) {
				auto* f = Lookup(gen::kSounds[i]);
				g_sounds[i] = f ? f->As<RE::BGSSoundDescriptorForm>() : nullptr;
			}
			auto* q = Lookup(gen::kIntroQuest);
			g_intro = q ? q->As<RE::TESQuest>() : nullptr;
			auto* g = Lookup(gen::kQ_MQQuickstart);
			g_quick = g ? g->As<RE::TESGlobal>() : nullptr;
			int qi = 0;
			for (const auto* r : { &gen::kQ_MQ102A, &gen::kQ_MQ102B, &gen::kQ_MQ102 }) {
				auto* f = Lookup(*r);
				g_mq102[qi++] = f ? f->As<RE::TESQuest>() : nullptr;
			}
			auto* m = Lookup(gen::kQ_MQ102Marker);
			g_riverwood = m ? m->As<RE::TESObjectREFR>() : nullptr;

			for (std::size_t i = 0; i < gen::kPerks.size(); ++i) {
				auto* a = Lookup(gen::kPlugin, gen::kPerks[i].alchId, gen::kPerks[i].id);
				auto* e = Lookup(gen::kPlugin, gen::kPerks[i].mgefId, gen::kPerks[i].id);
				g_potions[i] = a ? a->As<RE::AlchemyItem>() : nullptr;
				g_effects[i] = e ? e->GetFormID() : 0;
			}
			for (std::size_t i = 0; i < gen::kObjects.size(); ++i) {
				auto* f = Lookup(gen::kPlugin, gen::kObjects[i].actiId, gen::kObjects[i].id);
				g_objects[i] = f ? f->As<RE::TESObjectACTI>() : nullptr;
			}
			const int plugin = static_cast<int>(std::count_if(g_objects.begin(), g_objects.end(), [](auto* o) { return o != nullptr; }));
			logger::info("forms: {} of {} objects from {}, intro quest {}, quickstart global {}, Riverwood marker {}", plugin, g_objects.size(), gen::kPlugin,
				g_intro != nullptr, g_quick != nullptr, g_riverwood != nullptr);
			if (plugin == 0) {
				logger::error("{} is not loaded: check Plugins.txt", gen::kPlugin);
				ok = false;
			}
			return ok && g_intro;
		}

		RE::TESBoundObject* Enemy(std::size_t a_index) { return a_index < g_enemies.size() ? g_enemies[a_index] : nullptr; }
		RE::TESQuest*       IntroQuest() { return g_intro; }
		std::array<RE::TESQuest*, 3> MQ102() { return g_mq102; }
		// The intro is over when MQ101 is completed (a normal playthrough) or when Bethesda's quick-start
		// fragment has run: it sets stages 25/26/1000 and then Stop()s the quest without completing it.
		bool IntroDone()
		{
			if (!g_intro) {
				return false;
			}
			if (g_intro->IsCompleted() || (!g_intro->IsRunning() && !g_intro->IsStarting() && g_intro->GetCurrentStageID() >= 25)) {
				return true;
			}
			for (auto* q : g_mq102) {  // Before the Storm has started: Helgen is behind us
				// (Noah: the Helgen skip never showed, the class came at once) with Skyrim Together, Before the Storm's three
				// quests are running from the very start at stage 0: only a stage past 0 means you are out of Helgen
				if (q && (q->IsCompleted() || (q->IsRunning() && q->GetCurrentStageID() > 0))) {
					return true;
				}
			}
			return false;
		}

		std::string IntroDebug()
		{
			auto one = [](const char* n, RE::TESQuest* q) {
				return q ? fmt::format("{}[stage {} run {} start {} done {}]", n, q->GetCurrentStageID(), q->IsRunning(), q->IsStarting(), q->IsCompleted()) : fmt::format("{}[missing]", n);
			};
			return one("MQ101", g_intro) + " " + one("MQ102A", g_mq102[0]) + " " + one("MQ102B", g_mq102[1]) + " " + one("MQ102", g_mq102[2]) +
			       fmt::format(" quickstart={}", g_quick ? g_quick->value : -1.0f);
		}
		RE::TESGlobal*      Quickstart() { return g_quick; }
		RE::TESObjectREFR*  RiverwoodMarker() { return g_riverwood; }
		RE::AlchemyItem* PerkPotion(int a_perk) { return a_perk >= 0 && a_perk < static_cast<int>(g_potions.size()) ? g_potions[a_perk] : nullptr; }
		int PerkIndexOfEffect(RE::FormID a_mgef)
		{
			for (int i = 0; a_mgef && i < static_cast<int>(g_effects.size()); ++i) {
				if (g_effects[i] == a_mgef) {
					return i;
				}
			}
			return -1;
		}
		RE::TESObjectACTI* Object(int a_object) { return a_object >= 0 && a_object < static_cast<int>(g_objects.size()) ? g_objects[a_object] : nullptr; }
		int ObjectIndexOf(const RE::TESForm* a_base)
		{
			for (int i = 0; a_base && i < static_cast<int>(g_objects.size()); ++i) {
				if (g_objects[i] == a_base) {
					return i;
				}
			}
			return -1;
		}

		void Play(gen::Sound a_sound) { Audio::Ui(a_sound); }  // (all sound goes through Audio: rate limits, loops, voices)

		RE::MagicItem* Spell(int a_spell)
		{
			static std::array<RE::MagicItem*, gen::kSpells.size()> cache{};
			static std::array<bool, gen::kSpells.size()>           tried{};
			if (a_spell < 0 || a_spell >= static_cast<int>(gen::kSpells.size())) {
				return nullptr;
			}
			if (!tried[a_spell]) {
				tried[a_spell] = true;
				const auto& r = gen::kSpells[a_spell];
				auto*       f = RE::TESDataHandler::GetSingleton()->LookupForm(r.localId, r.plugin);
				if (auto* m = f ? f->As<RE::MagicItem>() : nullptr) {
					cache[a_spell] = m;
				} else if (auto* w = f ? f->As<RE::TESObjectWEAP>() : nullptr) {
					cache[a_spell] = w->formEnchanting;
				}
				if (!cache[a_spell]) {
					logger::warn("spells: {} not found", r.editorId);
				}
			}
			return cache[a_spell];
		}

		// (Noah: "new loot ... should be viewed as if I were picking up other weapons or items on the bottom right of my screen")
		// a thing given is picked up the way one off the ground is: set down at your feet and taken at once, so every item
		// feed sees it come in; anything that can't be set down goes straight into the pack
		void Give(RE::Actor* a_to, RE::TESBoundObject* a_obj, int a_count)
		{
			if (!a_to || !a_obj || a_count <= 0) return;
			const auto t = a_obj->GetFormType();
			const bool settable = t == RE::FormType::Weapon || t == RE::FormType::Armor || t == RE::FormType::Misc || t == RE::FormType::Book ||
			                      t == RE::FormType::AlchemyItem || t == RE::FormType::Ammo || t == RE::FormType::Ingredient || t == RE::FormType::SoulGem;
			// (Noah's game crashed loading a save: nothing is set down in the world while the save is still coming up)
			auto* ui = RE::UI::GetSingleton();
			const bool settled = Save::SinceWorldReady() > 6.0f && a_to->Is3DLoaded() && !(ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
			if (auto* pc = a_to->As<RE::PlayerCharacter>(); pc && settable && settled) {
				for (int n = 0; n < a_count; ++n) {
					auto ref = pc->PlaceObjectAtMe(a_obj, false);
					if (!ref) {
						pc->AddObjectToContainer(a_obj, nullptr, a_count - n, nullptr);
						return;
					}
					pc->PickUpObject(ref.get(), 1, false, true);
				}
				return;
			}
			a_to->AddObjectToContainer(a_obj, nullptr, a_count, nullptr);
		}
		RE::TESBoundObject* Ours(std::uint32_t a_localId)
		{
			static std::unordered_map<std::uint32_t, RE::TESBoundObject*> cache;
			if (auto it = cache.find(a_localId); it != cache.end()) {
				return it->second;
			}
			auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(a_localId, gen::kPlugin);
			auto* b = f ? f->As<RE::TESBoundObject>() : nullptr;
			if (!b) {
				logger::warn("forms: record {:03X} missing from {}", a_localId, gen::kPlugin);
			}
			return cache[a_localId] = b;
		}

		void PlayAt(gen::Sound a_sound, RE::TESObjectREFR* a_at) { Audio::At(a_sound, a_at); }
	}

	namespace Display
	{
		// (Noah: "automatically adapt the game resolution and UI for in-game to whatever monitor or screen the game is being
		// displayed on") before the game makes its window: SkyrimPrefs.ini's size set to the screen the game opens on (the main
		// one) at its own resolution, windowed full screen kept - a laptop, a monitor, a TV each get theirs. (The menus and our
		// HUD scale with the screen's height on their own.)
		void FitScreen(bool a_memory)
		{
			DEVMODEW dm{};
			dm.dmSize = sizeof(dm);
			if (!::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) || dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) return;
			wchar_t docs[MAX_PATH]{};
			if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs))) return;
			for (const wchar_t* folder : { L"\\My Games\\Skyrim Special Edition", L"\\My Games\\Skyrim Special Edition GOG", L"\\My Games\\Skyrim Special Edition EPIC" }) {
				const std::wstring ini = std::wstring(docs) + folder + L"\\SkyrimPrefs.ini";
				if (::GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
				const UINT w = ::GetPrivateProfileIntW(L"Display", L"iSize W", 0, ini.c_str());
				const UINT h = ::GetPrivateProfileIntW(L"Display", L"iSize H", 0, ini.c_str());
				const UINT full = ::GetPrivateProfileIntW(L"Display", L"bFull Screen", 0, ini.c_str());
				const UINT bord = ::GetPrivateProfileIntW(L"Display", L"bBorderless", 1, ini.c_str());
				if (!full && !bord) continue;   // (a window by choice: its size left alone)
				if (w == dm.dmPelsWidth && h == dm.dmPelsHeight) continue;
				::WritePrivateProfileStringW(L"Display", L"iSize W", std::to_wstring(dm.dmPelsWidth).c_str(), ini.c_str());
				::WritePrivateProfileStringW(L"Display", L"iSize H", std::to_wstring(dm.dmPelsHeight).c_str(), ini.c_str());
				if (auto* prefs = a_memory ? RE::INIPrefSettingCollection::GetSingleton() : nullptr) {   // (should the game have read its settings already)
					if (auto* sw = prefs->GetSetting("iSize W:Display")) sw->data.i = static_cast<std::int32_t>(dm.dmPelsWidth);
					if (auto* sh = prefs->GetSetting("iSize H:Display")) sh->data.i = static_cast<std::int32_t>(dm.dmPelsHeight);
				}
				if (a_memory) logger::info("display: the screen is {}x{} - the game's size set to it (was {}x{})", dm.dmPelsWidth, dm.dmPelsHeight, w, h);
			}
		}
	}
	namespace PluginList
	{
		// %LOCALAPPDATA%\Skyrim Special Edition\Plugins.txt: append "*CurseOfAlduin.esp" if it isn't listed.
		void Ensure()
		{
			wchar_t local[MAX_PATH]{};
			if (!::GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
				return;
			}
			for (const wchar_t* folder : { L"\\Skyrim Special Edition", L"\\Skyrim Special Edition GOG", L"\\Skyrim Special Edition EPIC" }) {
				std::wstring dir = std::wstring(local) + folder;
				if (::GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
					continue;
				}
				const std::wstring path = dir + L"\\Plugins.txt";
				std::string        text;
				if (HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); h != INVALID_HANDLE_VALUE) {
					char  buf[8192];
					DWORD n = 0;
					while (::ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) {
						text.append(buf, n);
					}
					::CloseHandle(h);
				}
				std::string lower = text;
				std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::string name(gen::kPlugin);
				std::string nameLower = name;
				std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (lower.find("*" + nameLower) != std::string::npos) {
					return;
				}
				std::string line = (text.empty() || text.back() == '\n' ? "" : "\r\n") + std::string("*") + name + "\r\n";
				// An unticked "CurseOfAlduin.esp" line (no *) is the player's choice: leave it.
				if (lower.find(nameLower) != std::string::npos) {
					return;
				}
				if (HANDLE h = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr); h != INVALID_HANDLE_VALUE) {
					DWORD n = 0;
					::WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &n, nullptr);
					::CloseHandle(h);
				}
				return;
			}
		}
	}

	namespace Save
	{
		// The mod's save lives in SKSE's co-save beside each save (record 'VALS': ten numbers). It used to live in ten of
		// the player's spare actor values, but Survival Mode's own scripts use four of them (Variable02-04, 09): every
		// load it zeroed them and took the old "Variable04" (our flags) off the player's health, killing a new character
		// a few seconds into the game. Old saves are read from those actor values once, which are then cleared.
		namespace
		{
			constexpr int kSavedAVs[] = { gen::kAV_slot0, gen::kAV_slot1, gen::kAV_slot2, gen::kAV_flags, gen::kAV_box, gen::kAV_round, gen::kAV_points,
				gen::kAV_reserve0, gen::kAV_reserve1, gen::kAV_reserve2 };
			std::array<int, std::size(kSavedAVs)> g_val{};
			bool                                  g_coSave = false;
			std::array<int, 32>                   g_stacks{};  // blessings bought, each (32: the class perks)
			bool                                  g_stacksLoaded = false;
			int                                   g_stamBonus = 0;
			int                                   g_bond = 0;  // the dragon's bond  // the extra stamina given to this character  // SKSE's serialization is ours (else: the old actor values)
			constexpr std::uint32_t               kRecord = 'VALS', kRecordVersion = 1;

			int Index(int a_av)
			{
				for (std::size_t i = 0; i < std::size(kSavedAVs); ++i) {
					if (kSavedAVs[i] == a_av) {
						return static_cast<int>(i);
					}
				}
				return -1;
			}
			float ReadAV(int a_av)
			{
				auto* p = RE::PlayerCharacter::GetSingleton();
				return p ? p->AsActorValueOwner()->GetActorValue(static_cast<RE::ActorValue>(a_av)) : 0.0f;
			}
			void WriteAV(int a_av, float a_value)
			{
				if (auto* p = RE::PlayerCharacter::GetSingleton()) {
					p->AsActorValueOwner()->SetActorValue(static_cast<RE::ActorValue>(a_av), a_value);
				}
			}
			void Migrate()  // an older save: its values are in the actor values; take them, then clear them
			{
				// (the audit: a save from before the mod had its Variable01-10 - other mods' and Survival's - read as ours and
				// zeroed) only a save whose flags value carries our own mark is ours to move
				if (const float f = ReadAV(kSavedAVs[3]); !(f >= 1.0f && f < 16777216.0f && ((static_cast<int>(f) >> Save::kFlagInit) & 1))) {
					logger::info("save: no values of ours to move (a save from before the mod)");
					return;
				}
				for (std::size_t i = 0; i < std::size(kSavedAVs); ++i) {
					g_val[i] = static_cast<int>(std::lround(ReadAV(kSavedAVs[i])));
					WriteAV(kSavedAVs[i], 0.0f);
				}
				logger::info("save: moved the mod's values from actor values into the co-save (flags {:06X}, {} points)", g_val[3], g_val[6]);
			}
			void OnSave(SKSE::SerializationInterface* a_s)
			{
				if (a_s->OpenRecord(kRecord, kRecordVersion)) {
					a_s->WriteRecordData(g_val.data(), static_cast<std::uint32_t>(sizeof(int) * g_val.size()));
				}
				if (a_s->OpenRecord('PRKS', 2)) {  // (2: room for 32)
					a_s->WriteRecordData(g_stacks.data(), static_cast<std::uint32_t>(sizeof(int) * g_stacks.size()));
				}
				if (a_s->OpenRecord('STAM', 1)) {
					a_s->WriteRecordData(&g_stamBonus, sizeof(int));
				}
				if (a_s->OpenRecord('DBND', 1)) {
					a_s->WriteRecordData(&g_bond, sizeof(int));
				}
				const auto lv = Arcana::Levels();
				if (a_s->OpenRecord('SPLV', 3)) {  // (3: the class pools; older levels are dropped)
					a_s->WriteRecordData(lv.data(), static_cast<std::uint32_t>(lv.size()));
				}
				if (a_s->OpenRecord('CRS2', 1)) {  // (ten rites a city: marks a save made with them)
					const std::uint32_t one = 1;
					a_s->WriteRecordData(&one, sizeof(one));
				}
				if (const auto ks = Kit::State(); a_s->OpenRecord('KIT1', 1)) {  // (the altar rework) your kit, relics, Infusions, armour
					a_s->WriteRecordData(ks.data(), static_cast<std::uint32_t>(ks.size()));
				}
				if (const auto cs = Classes::State(); a_s->OpenRecord('CLAS', 1)) {  // your class, and the spells of the ones you left
					a_s->WriteRecordData(cs.data(), static_cast<std::uint32_t>(cs.size()));
				}
				if (const auto mn = Curse::MineState(); a_s->OpenRecord('MINE', 1)) {  // (a party's altars) the trials you did yourself
					a_s->WriteRecordData(mn.data(), static_cast<std::uint32_t>(mn.size()));
				}
				if (const auto rs = Shops::RollState(); a_s->OpenRecord('ROLL', 1)) {  // each altar's price tonight
					a_s->WriteRecordData(rs.data(), static_cast<std::uint32_t>(rs.size()));
				}
				const auto sh = Director::Sheltered();
				if (a_s->OpenRecord('SHLT', 1)) {
					a_s->WriteRecordData(sh.data(), static_cast<std::uint32_t>(sizeof(RE::FormID) * sh.size()));
				}
			}
			void OnLoad(SKSE::SerializationInterface* a_s)
			{
				bool          found = false, crs2 = false, kit = false;
				std::uint32_t type = 0, version = 0, length = 0;
				while (a_s->GetNextRecordInfo(type, version, length)) {
					if (type == kRecord && length == sizeof(int) * g_val.size()) {
						found = a_s->ReadRecordData(g_val.data(), length) == length;
					} else if (type == 'PRKS' && (length == sizeof(int) * g_stacks.size() || length == sizeof(int) * 16)) {  // (16: before the class perks)
						g_stacks.fill(0);
						g_stacksLoaded = a_s->ReadRecordData(g_stacks.data(), length) == length;
					} else if (type == 'STAM' && length == sizeof(int)) {
						a_s->ReadRecordData(&g_stamBonus, length);
					} else if (type == 'DBND' && length == sizeof(int)) {
						a_s->ReadRecordData(&g_bond, length);
						g_bond = std::clamp(g_bond, 0, 3);
					} else if (type == 'CRS2') {
						std::uint32_t one = 0;
						a_s->ReadRecordData(&one, sizeof(one));
						crs2 = true;
					} else if (type == 'SHLT') {
						std::vector<RE::FormID> ids(length / sizeof(RE::FormID));
						a_s->ReadRecordData(ids.data(), static_cast<std::uint32_t>(ids.size() * sizeof(RE::FormID)));
						for (auto& id : ids) {
							RE::FormID n = 0;
							id = a_s->ResolveFormID(id, n) ? n : 0;
						}
						Director::Unshelter(ids);
					} else if (type == 'KIT1' && version == 1) {
						std::vector<std::uint8_t> ks(length);
						a_s->ReadRecordData(ks.data(), length);
						Kit::SetState(ks);
						kit = true;
					} else if (type == 'CLAS' && version == 1) {
						std::vector<std::uint8_t> cs(length);
						a_s->ReadRecordData(cs.data(), length);
						Classes::SetState(cs);
					} else if (type == 'MINE' && version == 1) {
						std::vector<std::uint8_t> mn(length);
						a_s->ReadRecordData(mn.data(), length);
						Curse::SetMineState(mn);
					} else if (type == 'ROLL' && version == 1) {
						std::vector<std::uint8_t> rs(length);
						a_s->ReadRecordData(rs.data(), length);
						Shops::SetRollState(rs);
					} else if (type == 'SPLV' && version == 3) {
						std::vector<std::uint8_t> lv(length);
						a_s->ReadRecordData(lv.data(), length);
						Arcana::SetLevels(lv);
					}
				}
				if (!crs2) {
					Tasks::Add([] { Curse::MigrateRites(); });
				}
				if (!kit && found) {  // (the altar rework: a character from before it - its classes' starting kits counted as owned)
					for (int c = 0; c < 4; ++c) if (Classes::KitGiven(c)) Kit::OnKitGiven(c);
					logger::info("save: a character from before the altar rework - its starting kits taken as its own");
				}
				if (!g_stacksLoaded) {  // an older save: one of each blessing it carries
					for (int i = 0; i < static_cast<int>(gen::kPerks.size()) && i < 32; ++i) {
						g_stacks[i] = 0;
					}
				}
				if (found) {
					logger::info("save: values loaded from the co-save (flags {:06X}, {} points)", g_val[3], g_val[6]);
				} else {
					Migrate();
				}
			}
			void OnRevert(SKSE::SerializationInterface*) { g_val.fill(0), g_stacks.fill(0), g_stacksLoaded = false, g_stamBonus = 0, g_bond = 0, Arcana::SetLevels({}), Shops::SetRollState({}), Curse::SetMineState({}), Classes::Revert(), Kit::Revert(), Party::OnLoad(); }
		}

		void InstallCoSave()
		{
			if (auto* s = SKSE::GetSerializationInterface()) {
				s->SetUniqueID('UNDN');
				s->SetSaveCallback(OnSave);
				s->SetLoadCallback(OnLoad);
				s->SetRevertCallback(OnRevert);
				g_coSave = true;
				logger::info("save: the mod's values live in the SKSE co-save");
			}
		}

		int  StaminaBonus() { return g_stamBonus; }
		void SetStaminaBonus(int a_n) { g_stamBonus = a_n; }
		int  DragonBond() { return g_bond; }
		void SetDragonBond(int a_bond) { g_bond = std::clamp(a_bond, 0, 3); }

		int PerkStacks(int a_perk)
		{
			if (a_perk < 0 || a_perk >= 32) {
				return 0;
			}
			if (!g_stacksLoaded) {  // an older save (or the old actor values): what its perk bits say
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()) && i < 32; ++i) {
					g_stacks[i] = PerkBit(i) ? 1 : 0;
				}
				g_stacksLoaded = true;
			}
			return g_stacks[a_perk];
		}
		void SetPerkStacks(int a_perk, int a_n)
		{
			if (a_perk < 0 || a_perk >= 32) {
				return;
			}
			PerkStacks(a_perk);
			g_stacks[a_perk] = std::max(0, a_n);
			if (a_perk < kOldPerkBits) SetPerk(a_perk, a_n > 0);  // (the flags hold the first nine: bit 23 is the layout's own)
		}

		int Get(int a_av)
		{
			if (const int i = Index(a_av); g_coSave && i >= 0) {
				return g_val[i];
			}
			return static_cast<int>(std::lround(ReadAV(a_av)));
		}
		void Set(int a_av, int a_value)
		{
			if (const int i = Index(a_av); g_coSave && i >= 0) {
				g_val[i] = a_value;
				return;
			}
			WriteAV(a_av, static_cast<float>(a_value));
		}
		int  Flags() { return Get(gen::kAV_flags); }
		void SetFlags(int a_flags) { Set(gen::kAV_flags, a_flags & 0xFFFFFF); }
		bool Flag(int a_bit) { return (Flags() >> a_bit) & 1; }
		void SetFlag(int a_bit, bool a_on) { SetFlags(a_on ? (Flags() | (1 << a_bit)) : (Flags() & ~(1 << a_bit))); }
		// v0.3 layout: perks moved to bits 14-22 so nine fit (v0.2 kept seven at bits 4-10).
		// (the class perks: past the nine the flags have room for, a perk lives in the co-save's counts alone)
		bool PerkBit(int a_perk) { return a_perk >= 0 && a_perk < kOldPerkBits && ((Flags() >> (kPerkShift + a_perk)) & 1); }
		bool HasPerk(int a_perk) { return PerkStacks(a_perk) > 0; }
		void SetPerk(int a_perk, bool a_on) { if (a_perk >= 0 && a_perk < kOldPerkBits) SetFlag(kPerkShift + a_perk, a_on); }
		std::uint32_t PerkBits()
		{
			std::uint32_t b = 0;
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()) && i < 32; ++i) b |= (PerkStacks(i) > 0 ? 1u : 0u) << i;
			return b;
		}
		int  PerkCount()
		{
			int n = 0;
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
				n += HasPerk(i);
			}
			return n;
		}

		// (only without SKSE's co-save) the old actor values: a snapshot puts them back if they vanish at once
		namespace
		{
			int  g_snap[std::size(kSavedAVs)]{};
			bool g_snapValid = false;
		}
		void ForgetSnapshot() { g_snapValid = false; }
		void Guard()
		{
			if (g_coSave) {
				return;
			}
			const int f = Get(gen::kAV_flags);
			if (g_snapValid && f == 0 && (g_snap[3] & (1 << kFlagInit))) {
				for (std::size_t i = 0; i < std::size(kSavedAVs); ++i) {
					Set(kSavedAVs[i], g_snap[i]);
				}
				logger::warn("save: the mod's values were wiped by something else; put back (flags {:06X}, {} points)", g_snap[3], g_snap[6]);
				return;
			}
			if (f & (1 << kFlagInit)) {
				for (std::size_t i = 0; i < std::size(kSavedAVs); ++i) {
					g_snap[i] = Get(kSavedAVs[i]);
				}
				g_snapValid = true;
			}
		}

		std::atomic<std::int64_t> g_readyAtMs{ 0 };
		float SinceWorldReady()
		{
			const auto t = g_readyAtMs.load();
			if (!t) return 1.0e9f;
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			return (now - t) / 1000.0f;
		}
		void OnWorldReady()
		{
			g_readyAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			if (!Flag(kFlagInit)) {
				SetFlags(1 << kFlagInit);
				Set(gen::kAV_round, 0);
				Set(gen::kAV_points, gen::kPoints_start);
				Set(gen::kAV_box, 0);
				SetFlag(kFlagPerksV2, true);
				logger::info("new BO3 save state: {} points", gen::kPoints_start);
			}
			if (!Flag(kFlagPerksV2)) {
				const int old = (Flags() >> 4) & 0x7F;
				SetFlags((Flags() & ~(0x7F << 4)) | (old << kPerkShift) | (1 << kFlagPerksV2));
				logger::info("save moved to v0.3 perk layout ({:07b})", old);
			}
			logger::info("world ready: round {} cleared, {} points, perks {:09b}", Get(gen::kAV_round), Get(gen::kAV_points), PerkBits());
			Perks::RearmAfterLoad();
			Classes::OnWorldReady();
			Curse::ForgetPendingLift();
		}
	}

	namespace Points
	{
		int Get() { return Save::Get(gen::kAV_points); }
		void Add(int a_points)
		{
			if (a_points <= 0) {
				return;
			}
			// (Noah: too many septims) every earning - kills, trials, rounds, the altars - pays 80%; the fractions carry over
			static float carry = 0;
			const float  paid = a_points * 0.8f + carry;
			a_points = static_cast<int>(paid);
			carry = paid - static_cast<float>(a_points);
			if (a_points <= 0) {
				return;
			}
			Save::Set(gen::kAV_points, Get() + a_points);
			Stats::OnEarned(a_points);
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.lastGain = a_points;
			h.gainTime = 1.0f;
		}
		void Refund(int a_points)  // money back, exactly (not an earning: no 80%, no Double Points)
		{
			if (a_points <= 0) {
				return;
			}
			Save::Set(gen::kAV_points, Get() + a_points);
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().lastGain = a_points;
			Hud::Edit().gainTime = 1.0f;
		}
		bool Spend(int a_points)
		{
			if (Get() < a_points) {
				Hud::Banner("Not enough Cursed Essence (" + Num(a_points) + " needed)", 2.0f);
				return false;
			}
			Save::Set(gen::kAV_points, Get() - a_points);
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.lastGain = -a_points;
			h.gainTime = 1.0f;
			return true;
		}
	}
}
