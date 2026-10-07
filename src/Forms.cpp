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
				g_enemies[i] = f ? f->As<RE::TESBoundObject>() : nullptr;
				ok = ok && g_enemies[i];
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
			// Hand spells borrow the look of the vanilla spell they unleash: fire hands for flames, frost for frost...
			for (std::size_t i = 0; i < gen::kGuns.size(); ++i) {
				auto* hs = g_gunSpells[i];
				auto* src = gen::kGuns[i].spell >= 0 ? Spell(gen::kGuns[i].spell) : nullptr;
				if (!hs || !src || hs->effects.empty() || src->effects.empty()) {
					continue;
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

		void Play(gen::Sound a_sound)
		{
			const auto i = static_cast<std::size_t>(a_sound);
			if (i < g_sounds.size() && g_sounds[i]) {
				RE::BSAudioManager::GetSingleton()->Play(static_cast<RE::BSISoundDescriptor*>(g_sounds[i]));
			}
		}

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

		void PlayAt(gen::Sound a_sound, RE::TESObjectREFR* a_at)
		{
			const auto i = static_cast<std::size_t>(a_sound);
			if (i >= g_sounds.size() || !g_sounds[i]) {
				return;
			}
			if (!a_at) {
				return Play(a_sound);
			}
			RE::BSSoundHandle h;
			if (RE::BSAudioManager::GetSingleton()->GetSoundHandle(h, static_cast<RE::BSISoundDescriptor*>(g_sounds[i]))) {
				h.SetPosition(a_at->GetPosition());
				if (auto* node = a_at->Get3D()) {
					h.SetObjectToFollow(node);
				}
				h.Play();
			}
		}
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
		int Get(int a_av)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p ? static_cast<int>(std::lround(p->AsActorValueOwner()->GetActorValue(static_cast<RE::ActorValue>(a_av)))) : 0;
		}
		void Set(int a_av, int a_value)
		{
			if (auto* p = RE::PlayerCharacter::GetSingleton()) {
				p->AsActorValueOwner()->SetActorValue(static_cast<RE::ActorValue>(a_av), static_cast<float>(a_value));
			}
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
