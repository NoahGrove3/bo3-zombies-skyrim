#include "BO3Z.h"
#include "CofferSpells.h"

// The Coffer's spells (sheets/coffer_spells.json, src/CofferSpells.h, tools/spellpool.py): 200 attack spells (Destruction first) from the
// spell mods the party plays with (Apocalypse, Mysticism, Forgotten Magic Redone, Stormcaller, Strange Runes), each
// exactly as its mod made it (its effects, art, sounds, casting). The shrine hands them out at random; a spell won is
// learned (it stays in the Magic menu to view, favourite and equip) and goes straight into the right hand.
// Two changes to every one of them, made when the game's data is loaded:
//   - its magicka cost is the pool's (a new character can cast it; the strongest still take most of a bar)
//   - a fire-and-forget spell has no charge time (no wind-up before a regular cast)
namespace bo3z::Arcana
{
	namespace
	{
		std::array<RE::SpellItem*, coffer::kSpells.size()> g_spell{};
		std::unordered_map<RE::FormID, int>                 g_index;
		std::unordered_map<RE::FormID, int>                 g_byEffect;  // a magic effect -> the (strongest) Coffer spell that has it
		std::array<bool, coffer::kSpells.size()>            g_damages{};  // has an effect that takes health
		int                                                 g_found = 0;
		std::mt19937                                        g_rng{ std::random_device{}() };
		std::array<std::uint8_t, coffer::kSpells.size()>    g_level{};
		std::array<std::vector<float>, coffer::kSpells.size()> g_baseMag;  // each effect's magnitude as the mod made it
		std::array<std::uint8_t, coffer::kSpells.size()>    g_school{};  // 0 destruction, 1 conjuration, 2 restoration, 3 alteration, 4 illusion
		int                                                 g_night = -1;  // the night the spells' power was set for
		// (Noah: balanced the whole way through - no early spell that one-shots) the spells', bows' and shouts' power
		// follows the nights survived: half on the first night, full by the tenth, then a little more each night
		float NightPower(int a_night)
		{
			const int n = std::max(1, a_night);
			return n <= 10 ? 0.5f + 0.5f * (n - 1) / 9.0f : std::min(1.5f, 1.0f + 0.025f * (n - 10));
		}
		float AltarPower() { return NightPower(g_night); }

		// Balance (Noah: most of them one-shot Skyrim's own enemies): every Coffer spell does 45% of the damage its mod
		// gave it and costs 75% of the pool's price.
		constexpr float kPower = 0.45f, kCost = 0.375f;  // (Noah: half the magicka they took before)
		// a reforged spell: each level adds gen::kForgeStep of its power and takes a tenth off its cost
		void Apply(int a_index)
		{
			auto* sp = g_spell[a_index];
			if (!sp) {
				return;
			}
			const int   lv = g_level[a_index];
			const float mult = kPower * AltarPower() * (1.0f + gen::kForgeStep * lv);
			auto&       base = g_baseMag[a_index];
			for (std::size_t e = 0; e < sp->effects.size() && e < base.size(); ++e) {
				if (sp->effects[e]) {
					sp->effects[e]->effectItem.magnitude = base[e] * mult;
				}
			}
			sp->data.costOverride = std::max(1, static_cast<int>(std::lround(coffer::kSpells[a_index].cost * kCost * (1.0f - 0.1f * lv) * Difficulty::Mana())));
		}
	}

	void Load()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			return;
		}
		g_found = 0;
		std::map<std::string, int> missing;
		for (std::size_t i = 0; i < coffer::kSpells.size(); ++i) {
			const auto& s = coffer::kSpells[i];
			auto*       sp = dh->LookupForm<RE::SpellItem>(s.id, s.plugin);
			if (sp && sp->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
				logger::info("arcana: {} is not a spell you cast (an ability or power) - left out", s.name);
				sp = nullptr;  // (an ability can't be cast: it would sit in the Magic menu doing nothing)
			}
			g_spell[i] = sp;
			if (!sp) {
				++missing[s.plugin];
				continue;
			}
			++g_found;
			g_index[sp->GetFormID()] = static_cast<int>(i);
			g_baseMag[i].clear();
			g_damages[i] = false;
			for (auto* e : sp->effects) {
				g_baseMag[i].push_back(e ? e->effectItem.magnitude : 0.0f);
				auto* mg = e ? e->baseEffect : nullptr;
				if (!mg) {
					continue;
				}
				using A = RE::EffectArchetypes::ArchetypeID;
				const auto arch = mg->GetArchetype();
				if (mg->data.primaryAV == RE::ActorValue::kHealth && mg->IsDetrimental() && e->effectItem.magnitude > 0 &&
					(arch == A::kValueModifier || arch == A::kDualValueModifier || arch == A::kAbsorb || arch == A::kPeakValueModifier)) {
					g_damages[i] = true;
				}
				auto [it, fresh] = g_byEffect.try_emplace(mg->GetFormID(), static_cast<int>(i));
				if (!fresh && s.tier > coffer::kSpells[it->second].tier) {
					it->second = static_cast<int>(i);
				}
			}
			// (Noah: every spell the altar gives is an attack - no debuffs): one that takes no health, or that also
			// paralyses, frightens, frenzies, calms, turns, disarms or weakens, is left out of the pool
			bool debuff = false;
			for (auto* e : sp->effects) {
				auto* mg = e ? e->baseEffect : nullptr;
				if (!mg || !(mg->IsDetrimental() || mg->IsHostile())) {
					continue;
				}
				using A = RE::EffectArchetypes::ArchetypeID;
				const auto arch = mg->GetArchetype();
				const auto av = mg->data.primaryAV;
				if (arch == A::kParalysis || arch == A::kDemoralize || arch == A::kFrenzy || arch == A::kCalm || arch == A::kTurnUndead ||
					arch == A::kDisarm || arch == A::kEtherealize || arch == A::kSoulTrap || arch == A::kBanish) {
					debuff = true;
				}
				const bool riderOk = av == RE::ActorValue::kHealth || av == RE::ActorValue::kStamina || av == RE::ActorValue::kMagicka ||
				                     av == RE::ActorValue::kSpeedMult || av == RE::ActorValue::kNone;
				if ((arch == A::kValueModifier || arch == A::kPeakValueModifier || arch == A::kDualValueModifier) && !riderOk) {
					debuff = true;  // (a weakness, a resistance or skill lowered)
				}
			}
			// (Noah: "Spell Twine does nothing") the utility spells the mods count as hostile: a tether, a mark, a lure
			static constexpr std::string_view kNotAttacks[] = { "Spell Twine", "Mark of", "Lure", "Tether", "Detect", "Telekinesis" };
			for (auto n : kNotAttacks) {
				if (std::string_view(s.name).find(n) != std::string_view::npos) debuff = true;
			}
			if (false && (!g_damages[i] || debuff)) {  // (Noah chose these 17 himself: every one of them is in the altar)
				logger::info("arcana: {} left out (not a straight attack)", s.name);
				g_spell[i] = nullptr;
				g_index.erase(sp->GetFormID());
				--g_found;
				continue;
			}
			switch (sp->GetAssociatedSkill()) {
			case RE::ActorValue::kConjuration: g_school[i] = 1; break;
			case RE::ActorValue::kRestoration: g_school[i] = 2; break;
			case RE::ActorValue::kAlteration: g_school[i] = 3; break;
			case RE::ActorValue::kIllusion: g_school[i] = 4; break;
			default: g_school[i] = 0; break;
			}
			Apply(static_cast<int>(i));  // the balanced power and cost (and any reforge)
			sp->data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);
			if (!s.concentration && sp->data.castingType == RE::MagicSystem::CastingType::kFireAndForget) {
				sp->data.chargeTime = 0.0f;  // released the moment you cast
			}
		}
		logger::info("arcana: {} of {} Coffer spells found", g_found, coffer::kSpells.size());
		for (auto& [plugin, n] : missing) {
			logger::warn("arcana: {} spells missing ({} not installed?)", n, plugin);
		}
	}

	int Count() { return g_found; }

	int   SchoolOf(int a_index) { return a_index >= 0 && a_index < static_cast<int>(g_school.size()) ? g_school[a_index] : 0; }
	float Power() { return AltarPower(); }
	void  RefreshPower()
	{
		const int night = Save::Get(gen::kAV_round) + 1;  // (the night being fought, or the next)
		if (night == g_night) {
			return;
		}
		g_night = night;
		for (int i = 0; i < static_cast<int>(g_spell.size()); ++i) {
			Apply(i);
		}
		logger::info("arcana: night {} - spells, bows and shouts at {:.0f}% power", night, AltarPower() * 100.0f);
	}

	// (Noah: two of the altar's spells at a time) the altar's spells you carry, the ones in your hands first
	// (the audit: a Firebolt read from a looted tome counted as one of a Summoner's altar spells, and pushed its own out)
	// only your class's spells count
	std::vector<int> Carried() { return Carried(Classes::Get()); }
	std::vector<int> Carried(int a_cls)
	{
		std::vector<int> out;
		const int cls = a_cls;
		auto mine = [cls](int i) { return cls < 0 || ClassOf(i) == cls; };
		for (int h : Hands()) {
			if (h >= 0 && Known(h) && mine(h) && std::ranges::find(out, h) == out.end()) {
				out.push_back(h);
			}
		}
		for (int i = 0; i < static_cast<int>(g_spell.size()); ++i) {
			if (g_spell[i] && Known(i) && mine(i) && std::ranges::find(out, i) == out.end()) {
				out.push_back(i);
			}
		}
		return out;
	}
	// how many you may carry: two - three for the Summoner (its Corrosion took the staff's place beside its two summons)
	std::size_t CarryCap() { return Classes::Get() == coffer::kSummoner ? 3 : 2; }
	// one of them gone for good: out of your hands, out of the Magic menu, its reforging lost
	void Forget(int a_index)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* sp = Spell(a_index);
		if (!p || !sp) {
			return;
		}
		auto* em = RE::ActorEquipManager::GetSingleton();
		for (bool left : { false, true }) {
			if (p->GetEquippedObject(left) == sp && em) {
				em->UnequipObject(p, sp, nullptr, 1, RE::TESForm::LookupByID<RE::BGSEquipSlot>(left ? 0x13F43 : 0x13F42), false, true, false, true);
			}
		}
		p->RemoveSpell(sp);
		SetLevel(a_index, 0);
		logger::info("arcana: {} given back to the altar", coffer::kSpells[a_index].name);
	}
	// the new spell takes the old one's place - and its hands
	void Replace(int a_old, int a_new)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* oldSp = Spell(a_old);
		auto* newSp = Spell(a_new);
		if (!p || !newSp) {
			return;
		}
		const bool inL = oldSp && p->GetEquippedObject(true) == oldSp, inR = oldSp && p->GetEquippedObject(false) == oldSp;
		Forget(a_old);
		Give(a_new, false);
		if (auto* em = RE::ActorEquipManager::GetSingleton()) {
			if (inR || !inL) {
				em->EquipSpell(p, newSp, RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x13F42));
			}
			if (inL) {
				em->EquipSpell(p, newSp, RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x13F43));
			}
		}
	}

	int IndexOfEffect(RE::FormID a_effect)
	{
		auto it = g_byEffect.find(a_effect);
		return it == g_byEffect.end() ? -1 : it->second;
	}

	bool Damages(int a_index) { return a_index >= 0 && a_index < static_cast<int>(g_damages.size()) && g_damages[a_index]; }

	RE::SpellItem* Spell(int a_index) { return a_index >= 0 && a_index < static_cast<int>(g_spell.size()) ? g_spell[a_index] : nullptr; }

	int IndexOf(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return -1;
		}
		auto it = g_index.find(a_form->GetFormID());
		return it == g_index.end() ? -1 : it->second;
	}

	bool Known(int a_index)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* sp = Spell(a_index);
		return p && sp && p->HasSpell(sp);
	}

	// a spell you don't know yet (every one has the same chance); -1 if you know them all
	int Roll()
	{
		// (the classes overhaul) the altar rolls your class's base spells (each ladder's first rung) of ladders you
		// don't carry yet; the Ranger rolls bows instead (Shops), so it gets none
		const int cls = Classes::Get();
		if (cls < 0 || cls == coffer::kRanger) {
			return -1;
		}
		std::array<bool, coffer::kLadders> carried{};
		for (int i = 0; i < static_cast<int>(g_spell.size()); ++i) {
			if (g_spell[i] && Known(i)) carried[coffer::kSpells[i].ladder] = true;
		}
		std::vector<int> pool;
		for (int i = 0; i < static_cast<int>(g_spell.size()); ++i) {
			const auto& s = coffer::kSpells[i];
			if (g_spell[i] && s.cls == cls && s.rung == 1 && !carried[s.ladder]) {
				pool.push_back(i);
			}
		}
		if (pool.empty()) {
			return -1;
		}
		return pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(g_rng)];
	}
	int ClassOf(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? coffer::kSpells[a_index].cls : -1; }
	int RungOf(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? coffer::kSpells[a_index].rung : 0; }
	int Rungs(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? coffer::kSpells[a_index].rungs : 0; }
	std::string_view LadderName(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? coffer::kSpells[a_index].ladderLabel : ""; }
	int NextRung(int a_index)
	{
		if (a_index < 0 || a_index >= static_cast<int>(coffer::kSpells.size())) return -1;
		const auto& s = coffer::kSpells[a_index];
		for (int i = 0; i < static_cast<int>(coffer::kSpells.size()); ++i) {
			if (coffer::kSpells[i].ladder == s.ladder && coffer::kSpells[i].rung == s.rung + 1) return g_spell[i] ? i : -1;
		}
		return -1;
	}

	// every spell in the pool, in order (the shrine's roll flickers through them)
	int Next(int a_after)
	{
		const int cls = Classes::Get();
		for (int k = 1; k <= static_cast<int>(g_spell.size()); ++k) {
			const int i = (std::max(a_after, -1) + k) % static_cast<int>(g_spell.size());
			if (g_spell[i] && coffer::kSpells[i].rung == 1 && (cls < 0 || coffer::kSpells[i].cls == cls)) {
				return i;
			}
		}
		return -1;
	}

	void Give(int a_index, bool a_equip)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* sp = Spell(a_index);
		if (!p || !sp) {
			return;
		}
		if (!p->HasSpell(sp)) {
			p->AddSpell(sp);
		}
		if (a_equip) {
			if (auto* em = RE::ActorEquipManager::GetSingleton()) {
				auto* right = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x13F42);
				em->EquipSpell(p, sp, right);
			}
		}
		logger::info("arcana: {} learned{}", coffer::kSpells[a_index].name, a_equip ? " and in hand" : "");
	}

	// (the classes overhaul) a new character's start is its class's kit, handed over by the class picker (Classes.cpp)
	void GiveStarter() { Classes::OnNewCharacter(); }

	std::string_view Name(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? coffer::kSpells[a_index].name : ""; }
	int              ElementOf(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? coffer::kSpells[a_index].element : coffer::kArcane; }
	int              TierOf(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? coffer::kSpells[a_index].tier : 0; }
	int              CostOf(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) ? coffer::kSpells[a_index].cost : 0; }
	bool             Concentration(int a_index) { return a_index >= 0 && a_index < static_cast<int>(coffer::kSpells.size()) && coffer::kSpells[a_index].concentration; }

	std::uint32_t ElementColor(int a_element)
	{
		static constexpr std::uint32_t kCol[coffer::kElements] = { 0xFF6A1E, 0x6FC8FF, 0x8C7CFF, 0x6FD24A, 0xC8642E, 0xFFC83C, 0xD0202E, 0xB45CFF };
		return a_element >= 0 && a_element < coffer::kElements ? kCol[a_element] : 0xB45CFF;
	}
	std::string_view ElementName(int a_element) { return a_element >= 0 && a_element < coffer::kElements ? coffer::kElementName[a_element] : "arcane"; }
	int              Elements() { return coffer::kElements; }

	// what the hands hold: the Coffer's spell in each (index, -1 none)
	std::array<int, 2> Hands()
	{
		std::array<int, 2> out{ -1, -1 };
		auto*              p = RE::PlayerCharacter::GetSingleton();
		if (!p) {
			return out;
		}
		out[0] = IndexOf(p->GetEquippedObject(true));
		out[1] = IndexOf(p->GetEquippedObject(false));
		return out;
	}

	int Level(int a_index) { return a_index >= 0 && a_index < static_cast<int>(g_level.size()) ? g_level[a_index] : 0; }
	void SetLevel(int a_index, int a_level)
	{
		if (a_index < 0 || a_index >= static_cast<int>(g_level.size())) {
			return;
		}
		g_level[a_index] = static_cast<std::uint8_t>(std::clamp(a_level, 0, gen::kForgeMax));
		Apply(a_index);
	}
	std::vector<std::uint8_t> Levels() { return { g_level.begin(), g_level.end() }; }
	void SetLevels(const std::vector<std::uint8_t>& a_levels)
	{
		for (std::size_t i = 0; i < g_level.size(); ++i) {
			g_level[i] = i < a_levels.size() ? static_cast<std::uint8_t>(std::min<int>(a_levels[i], gen::kForgeMax)) : 0;
			Apply(static_cast<int>(i));
		}
	}
}
