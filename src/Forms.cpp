#include "BO3Z.h"

namespace bo3z
{
	namespace
	{
		std::array<RE::TESBoundObject*, gen::kEnemies.size()>        g_enemies{};
		std::array<RE::BGSSoundDescriptorForm*, gen::kSounds.size()> g_sounds{};
		std::array<RE::TESObjectWEAP*, gen::kGuns.size()>            g_guns{};
		std::array<RE::SpellItem*, gen::kGuns.size()>                g_gunSpells{};
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

			int plugin = 0;
			for (std::size_t i = 0; i < gen::kGuns.size(); ++i) {
				auto* f = Lookup(gen::kPlugin, gen::kGuns[i].weapId, gen::kGuns[i].id);
				g_guns[i] = f ? f->As<RE::TESObjectWEAP>() : nullptr;
				plugin += g_guns[i] != nullptr;
				if (gen::kGuns[i].handSpell) {
					auto* sf = Lookup(gen::kPlugin, gen::kGuns[i].handSpell, gen::kGuns[i].id);
					g_gunSpells[i] = sf ? sf->As<RE::SpellItem>() : nullptr;
				}
			}
			// Wand bolts fly on Skyrim's own projectile records (the ones its spells use with the same model), so
			// they carry Bethesda's settings for those effects (lights, sizing, particle setup).
			{
				std::unordered_map<std::string, RE::BGSProjectile*> byModel;
				for (auto* pr : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSProjectile>()) {
					if (!pr || (pr->GetFormID() >> 24) >= 0xFE || !pr->IsMissile()) {
						continue;
					}
					std::string m = pr->GetModel() ? pr->GetModel() : "";
					std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (!m.empty() && !byModel.contains(m)) {
						byModel[m] = pr;
					}
				}
				std::unordered_set<RE::EffectSetting*> done;
				for (const auto& g : gen::kGuns) {
					if (!g.bulletSpell) {
						continue;
					}
					auto* raw = RE::TESDataHandler::GetSingleton()->LookupForm(g.bulletSpell, gen::kPlugin);
					auto* sp = raw ? raw->As<RE::SpellItem>() : nullptr;
					auto* e = sp && !sp->effects.empty() && sp->effects[0] ? sp->effects[0]->baseEffect : nullptr;
					if (!e || !e->data.projectileBase) {
						logger::warn("forms: {} bolt: spell {:03X} form {} spell {} effects {} effect {} projectile {}", g.id, g.bulletSpell, raw != nullptr, sp != nullptr,
							sp ? sp->effects.size() : 0, e != nullptr, e && e->data.projectileBase);
						continue;
					}
					// the bolt in flight hums with its own loop (Spec 12.3), whatever projectile carries it; Skyrim's own
					// flight and fade sounds are taken off (only the mod's library plays)
					auto fly = [&](RE::BGSProjectile* a_p) {
						const int gi = static_cast<int>(&g - gen::kGuns.data());
						if (a_p && gi >= 0 && gi < static_cast<int>(gen::kGunSounds.size())) {
							const int tr = gen::kGunSounds[gi].travel;
							a_p->data.activeSoundLoop = tr >= 0 ? Audio::Descriptor(static_cast<gen::Sound>(tr)) : nullptr;
							a_p->data.countdownSound = nullptr;
							a_p->data.deactivateSound = nullptr;
						}
					};
					if (done.contains(e)) {
						continue;
					}
					done.insert(e);
					std::string m = e->data.projectileBase->GetModel() ? e->data.projectileBase->GetModel() : "";
					std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (auto it = byModel.find(m); it != byModel.end()) {
						logger::info("forms: {} bolt flies on Skyrim's projectile {:08X} ({})", g.id, it->second->GetFormID(), m);
						e->data.projectileBase = it->second;
					} else {
						logger::info("forms: {} bolt keeps our projectile ({}: no vanilla match)", g.id, m);
					}
					fly(e->data.projectileBase);
				}
			}
			// Hand spells borrow the look of the vanilla spell they unleash: fire hands for flames, frost for frost...
			for (std::size_t i = 0; i < gen::kGuns.size(); ++i) {
				auto* hs = g_gunSpells[i];
				const int look = gen::kGuns[i].handLook >= 0 ? gen::kGuns[i].handLook : gen::kGuns[i].spell;  // the hands' glow
				auto* src = look >= 0 ? Spell(look) : nullptr;
				if (!hs || !src || hs->effects.empty() || src->effects.empty()) {
					continue;
				}
				if (std::any_of(gen::kSpellcraft.begin(), gen::kSpellcraft.end(), [&](const auto& sc) { return sc.id == gen::kGuns[i].id; })) {
					continue;  // the ten spells wear their own hand glow and light (plugin: fx_<id>_hand / fx_<id>_light)
				}
				auto* ours = hs->effects[0]->baseEffect;
				for (auto* e : src->effects) {
					if (auto* b = e ? e->baseEffect : nullptr; b && ours && (b->data.castingArt || b->data.light)) {
						ours->data.castingArt = b->data.castingArt;
						ours->data.light = b->data.light;
						ours->data.castingSoundLevel = b->data.castingSoundLevel;
						break;
					}
				}
			}
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
			logger::info("forms: {} of {} guns from {}, intro quest {}, quickstart global {}, Riverwood marker {}", plugin, gen::kGuns.size(), gen::kPlugin,
				g_intro != nullptr, g_quick != nullptr, g_riverwood != nullptr);
			if (plugin == 0) {
				logger::error("{} is not loaded: check Plugins.txt", gen::kPlugin);
				ok = false;
			}
			return ok && g_intro;
		}

		RE::TESBoundObject* Enemy(std::size_t a_index) { return a_index < g_enemies.size() ? g_enemies[a_index] : nullptr; }
		RE::TESQuest*       IntroQuest() { return g_intro; }
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
				if (q && (q->IsRunning() || q->IsCompleted())) {
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
		RE::TESObjectWEAP*  Gun(int a_gun) { return a_gun >= 0 && a_gun < static_cast<int>(g_guns.size()) ? g_guns[a_gun] : nullptr; }
		RE::SpellItem* GunSpell(int a_gun) { return a_gun >= 0 && a_gun < static_cast<int>(g_gunSpells.size()) ? g_gunSpells[a_gun] : nullptr; }
		int GunIndexOf(const RE::TESForm* a_form)
		{
			for (int i = 0; a_form && i < static_cast<int>(g_guns.size()); ++i) {
				if (g_guns[i] == a_form || (g_gunSpells[i] && g_gunSpells[i] == a_form)) {
					return i;
				}
			}
			return -1;
		}
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

	namespace PluginList
	{
		// %LOCALAPPDATA%\Skyrim Special Edition\Plugins.txt: append "*BO3Zombies.esp" if it isn't listed.
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
				// An unticked "BO3Zombies.esp" line (no *) is the player's choice: leave it.
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
			std::array<int, 16>                   g_stacks{};  // blessings bought, each
			bool                                  g_stacksLoaded = false;
			int                                   g_stamBonus = 0;  // the extra stamina given to this character  // SKSE's serialization is ours (else: the old actor values)
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
				if (a_s->OpenRecord('PRKS', 1)) {
					a_s->WriteRecordData(g_stacks.data(), static_cast<std::uint32_t>(sizeof(int) * g_stacks.size()));
				}
				if (a_s->OpenRecord('STAM', 1)) {
					a_s->WriteRecordData(&g_stamBonus, sizeof(int));
				}
				const auto lv = Arcana::Levels();
				if (a_s->OpenRecord('SPLV', 2)) {  // (2: the 200-spell Coffer; the 150-spell levels are dropped)
					a_s->WriteRecordData(lv.data(), static_cast<std::uint32_t>(lv.size()));
				}
				const auto sh = Director::Sheltered();
				if (a_s->OpenRecord('SHLT', 1)) {
					a_s->WriteRecordData(sh.data(), static_cast<std::uint32_t>(sizeof(RE::FormID) * sh.size()));
				}
			}
			void OnLoad(SKSE::SerializationInterface* a_s)
			{
				bool          found = false;
				std::uint32_t type = 0, version = 0, length = 0;
				while (a_s->GetNextRecordInfo(type, version, length)) {
					if (type == kRecord && length == sizeof(int) * g_val.size()) {
						found = a_s->ReadRecordData(g_val.data(), length) == length;
					} else if (type == 'PRKS' && length == sizeof(int) * g_stacks.size()) {
						g_stacksLoaded = a_s->ReadRecordData(g_stacks.data(), length) == length;
					} else if (type == 'STAM' && length == sizeof(int)) {
						a_s->ReadRecordData(&g_stamBonus, length);
					} else if (type == 'SHLT') {
						std::vector<RE::FormID> ids(length / sizeof(RE::FormID));
						a_s->ReadRecordData(ids.data(), static_cast<std::uint32_t>(ids.size() * sizeof(RE::FormID)));
						for (auto& id : ids) {
							RE::FormID n = 0;
							id = a_s->ResolveFormID(id, n) ? n : 0;
						}
						Director::Unshelter(ids);
					} else if (type == 'SPLV' && version == 2) {
						std::vector<std::uint8_t> lv(length);
						a_s->ReadRecordData(lv.data(), length);
						Arcana::SetLevels(lv);
					}
				}
				if (!g_stacksLoaded) {  // an older save: one of each blessing it carries
					for (int i = 0; i < static_cast<int>(gen::kPerks.size()) && i < 16; ++i) {
						g_stacks[i] = 0;
					}
				}
				if (found) {
					logger::info("save: values loaded from the co-save (flags {:06X}, {} points)", g_val[3], g_val[6]);
				} else {
					Migrate();
				}
			}
			void OnRevert(SKSE::SerializationInterface*) { g_val.fill(0), g_stacks.fill(0), g_stacksLoaded = false, g_stamBonus = 0, Arcana::SetLevels({}); }
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

		int PerkStacks(int a_perk)
		{
			if (a_perk < 0 || a_perk >= 16) {
				return 0;
			}
			if (!g_stacksLoaded) {  // an older save (or the old actor values): what its perk bits say
				for (int i = 0; i < static_cast<int>(gen::kPerks.size()) && i < 16; ++i) {
					g_stacks[i] = HasPerk(i) ? 1 : 0;
				}
				g_stacksLoaded = true;
			}
			return g_stacks[a_perk];
		}
		void SetPerkStacks(int a_perk, int a_n)
		{
			if (a_perk < 0 || a_perk >= 16) {
				return;
			}
			PerkStacks(a_perk);
			g_stacks[a_perk] = std::max(0, a_n);
			SetPerk(a_perk, a_n > 0);
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
		static int SlotAV(int a_slot) { return a_slot == 0 ? gen::kAV_slot0 : a_slot == 1 ? gen::kAV_slot1 : gen::kAV_slot2; }
		int ReserveAV(int a_slot) { return a_slot == 0 ? gen::kAV_reserve0 : a_slot == 1 ? gen::kAV_reserve1 : gen::kAV_reserve2; }

		Slot GetSlot(int a_slot)
		{
			const int v = Get(SlotAV(a_slot));
			Slot      s;
			s.gun = ((v >> 19) & 0x1F) - 1;
			s.clip = (v >> 11) & 0xFF;
			s.reserve = Flag(kFlagReserveV2) ? Get(ReserveAV(a_slot)) : (v & 0x7FF);
			s.level = Flag(kFlagReserveV2) ? (v & 0x3) : 0;  // v0.4+: the old reserve bits hold the forge level
			return s;
		}
		void SetSlot(int a_slot, const Slot& a_value)
		{
			const int v = a_value.gun < 0 ? 0 : (((a_value.gun + 1) & 0x1F) << 19) | ((std::clamp(a_value.clip, 0, 255)) << 11) | (Flag(kFlagReserveV2) ? std::clamp(a_value.level, 0, 3) : std::clamp(a_value.reserve, 0, 2047));
			Set(SlotAV(a_slot), v);
			if (Flag(kFlagReserveV2)) {
				Set(ReserveAV(a_slot), std::clamp(a_value.reserve, 0, 9999999));
			}
		}
		int  Flags() { return Get(gen::kAV_flags); }
		void SetFlags(int a_flags) { Set(gen::kAV_flags, a_flags & 0xFFFFFF); }
		bool Flag(int a_bit) { return (Flags() >> a_bit) & 1; }
		void SetFlag(int a_bit, bool a_on) { SetFlags(a_on ? (Flags() | (1 << a_bit)) : (Flags() & ~(1 << a_bit))); }
		int  Crew() { return ((Flags() >> 1) & 0x7) - 1; }
		void SetCrew(int a_crew) { SetFlags((Flags() & ~(0x7 << 1)) | (((a_crew + 1) & 0x7) << 1)); }
		// v0.3 layout: perks moved to bits 14-22 so nine fit (v0.2 kept seven at bits 4-10).
		bool HasPerk(int a_perk) { return (Flags() >> (kPerkShift + a_perk)) & 1; }
		void SetPerk(int a_perk, bool a_on) { SetFlag(kPerkShift + a_perk, a_on); }
		int  PerkBits() { return (Flags() >> kPerkShift) & 0x1FF; }
		int  PerkCount()
		{
			int n = 0;
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) {
				n += HasPerk(i);
			}
			return n;
		}
		int  ActiveSlot() { return (Flags() >> 12) & 0x3; }
		void SetActiveSlot(int a_slot) { SetFlags((Flags() & ~(0x3 << 12)) | ((a_slot & 0x3) << 12)); }

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

		void OnWorldReady()
		{
			if (!Flag(kFlagInit)) {
				SetFlags(1 << kFlagInit);
				Set(gen::kAV_round, 0);
				Set(gen::kAV_points, gen::kPoints_start);
				Set(gen::kAV_box, 0);
				for (int s = 0; s < 3; ++s) {
					SetSlot(s, {});
				}
				SetFlag(kFlagPerksV2, true);
				logger::info("new BO3 save state: {} points", gen::kPoints_start);
			}
			if (!Flag(kFlagReserveV2)) {
				for (int s = 0; s < 3; ++s) {
					Set(ReserveAV(s), Get(SlotAV(s)) & 0x7FF);
				}
				SetFlag(kFlagReserveV2, true);
				logger::info("save moved reserve ammo to its own slots");
			}
			if (!Flag(kFlagPerksV2)) {
				const int old = (Flags() >> 4) & 0x7F;
				SetFlags((Flags() & ~(0x7F << 4)) | (old << kPerkShift) | (1 << kFlagPerksV2));
				logger::info("save moved to v0.3 perk layout ({:07b})", old);
			}
			logger::info("world ready: round {} cleared, {} points, crew {}, perks {:09b}", Get(gen::kAV_round), Get(gen::kAV_points), Crew(), PerkBits());
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
			a_points *= PowerUps::PointsMult();
			Save::Set(gen::kAV_points, Get() + a_points);
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.lastGain = a_points;
			h.gainTime = 1.0f;
		}
		bool Spend(int a_points)
		{
			if (Get() < a_points) {
				Hud::Banner("Not enough points (" + std::to_string(a_points) + ")", 2.0f);
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
