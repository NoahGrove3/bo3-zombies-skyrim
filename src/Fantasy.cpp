#include "BO3Z.h"

// (Noah: the High Fantasy Pack's creatures dispersed through the game, full and fun without being overwhelming)
// Skyrim's own wilderness and bandit encounters pick one creature at random from a leveled list of everything that
// fits the place and your level. A few of the pack's creatures are added to those lists - one or two per place,
// each from a level where it is a fair fight - so out in the world now and then a goblin band, an ogre or a harpy
// turns up among the wolves, bears and bandits (the vanilla creatures still make up most of every list). The nights'
// share of them is in sheets/enemies.json (hf_* rows). Nothing changes if the pack is not installed.
namespace bo3z
{
	namespace Fantasy
	{
		namespace
		{
			constexpr std::string_view kPack = "mihailhighfantasypack.esp";
			struct Add
			{
				RE::FormID     list;   // Skyrim.esm LVLN
				std::uint32_t  npc;    // the pack's NPC_ (local id)
				std::uint16_t  level;  // from this player level on
				const char*    what;
			};
			// Skyrim's lists: LCharAnimal<place>Predator pick from everything at or under your level (17-26 entries
			// each); LCharBanditMelee* fill each bandit camp's fighters (6 entries)
			constexpr Add kAdds[] = {
				// forests (Falkreath, the Rift)
				{ 0x042297, 0x00087D, 4, "goblin" }, { 0x042297, 0x0009A9, 8, "giant centipede" }, { 0x042297, 0x0008D8, 12, "harpy" },
				{ 0x042297, 0x000BAB, 16, "cyrodilic troll" }, { 0x042297, 0x000D56, 22, "savage ogre" },
				// snowy forests and mountains
				{ 0x042299, 0x000871, 4, "riekr" }, { 0x042299, 0x00096F, 14, "wampa" }, { 0x042299, 0x000D60, 22, "snowhills ogre" },
				{ 0x04229D, 0x000871, 4, "riekr" }, { 0x04229D, 0x0008D8, 10, "harpy" }, { 0x04229D, 0x00096F, 16, "wampa" }, { 0x04229D, 0x0008F0, 24, "griffin" },
				// the snowy coast
				{ 0x0422A3, 0x000871, 4, "riekr" }, { 0x0422A3, 0x000B21, 12, "snowray" },
				// the Reach's canyons
				{ 0x04229B, 0x00087D, 4, "goblin" }, { 0x04229B, 0x000913, 10, "cockatrice" }, { 0x04229B, 0x0008D8, 12, "harpy" }, { 0x04229B, 0x07F285, 24, "minotaur" },
				// marshes (Hjaalmarch)
				{ 0x042295, 0x0009A9, 6, "giant centipede" }, { 0x042295, 0x03839B, 8, "arachling" }, { 0x042295, 0x000B8F, 14, "gelatinous cube" },
				// plains and hills (Whiterun's tundra)
				{ 0x042293, 0x00087D, 4, "goblin" }, { 0x042293, 0x000913, 10, "cockatrice" }, { 0x042293, 0x000D3B, 26, "ettin" },
				{ 0x01E78F, 0x00087D, 4, "goblin" }, { 0x01E78F, 0x000C15, 12, "bolgan" },
				// frostbite spider nests
				{ 0x01E77C, 0x03839B, 6, "arachling" },
				// bandit camps: the pack's own bandit-sworn kin fight beside them
				{ 0x039CFC, 0x05C0D7, 1, "goblin bandit" }, { 0x03DEC8, 0x05C0DE, 6, "goblin berserker" },
				{ 0x03DEC9, 0x000C2B, 12, "bolgan bandit" }, { 0x03DECA, 0x000D82, 20, "ogre mercenary" },
			};
		}

		// (Noah: "the magic sound effects are still too loud, especially when drawing a magical sword or weapon") every
		// magic effect's sounds brought down, hardest where they drone: the draw / sheathe and the readied loop (a spell or
		// a spell-blade held ready) 10 dB, the charge and the cast loop 6 dB, the release and the hit 4 dB. Each sound once.
		void TameSpellSounds()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return;
			std::unordered_map<RE::BGSSoundDescriptorForm*, int> cut;  // the most each sound's roles ask (centi-dB)
			for (auto* mg : dh->GetFormArray<RE::EffectSetting>()) {
				if (!mg) continue;
				for (auto& sp : mg->effectSounds) {
					if (!sp.sound) continue;
					int db = 400;
					switch (sp.id) {
					case RE::MagicSystem::SoundID::kDrawSheatheLPM:
					case RE::MagicSystem::SoundID::kReadyLoop: db = 1000; break;
					case RE::MagicSystem::SoundID::kCharge:
					case RE::MagicSystem::SoundID::kCastLoop: db = 600; break;
					default: break;
					}
					auto& c = cut[sp.sound];
					c = std::max(c, db);
				}
			}
			int n = 0;
			for (auto& [sd, db] : cut) {
				auto* sdef = sd->soundDescriptor ? skyrim_cast<RE::BGSStandardSoundDef*>(sd->soundDescriptor) : nullptr;
				if (!sdef) continue;
				sdef->soundCharacteristics.staticAttenuation = static_cast<std::uint16_t>(std::min(10000, sdef->soundCharacteristics.staticAttenuation + db));
				++n;
			}
			logger::info("audio: {} magic sounds brought down (draw and readied loops 10 dB, charge 6, release and hit 4)", n);
		}
		void HushAltarEchoes()
		{
			// (Noah: "the Vampiric Touch is still really loud") the altar's own spells: Mysticism's dummy and perk effects
			// echo the touch's sounds two times more, and another mod leads with them, so the rule above let them be - for
			// the altar's spells they fall silent all the same (unless one leads an altar spell of its own)
			{
				std::unordered_set<RE::EffectSetting*> altarLeads;
				for (int i = 0; i < 256; ++i) {
					if (auto* sp = Arcana::Spell(i); sp && !sp->effects.empty() && sp->effects[0] && sp->effects[0]->baseEffect) altarLeads.insert(sp->effects[0]->baseEffect);
				}
				int n = 0;
				for (int i = 0; i < 256; ++i) {
					auto* m = Arcana::Spell(i);
					if (!m || m->effects.size() < 2 || !m->effects[0] || !m->effects[0]->baseEffect) continue;
					auto* first = m->effects[0]->baseEffect;
					std::unordered_set<RE::BGSSoundDescriptorForm*> have;
					for (auto& sp : first->effectSounds) if (sp.sound) have.insert(sp.sound);
					if (have.empty()) continue;
					for (std::size_t e = 1; e < m->effects.size(); ++e) {
						auto* mg = m->effects[e] ? m->effects[e]->baseEffect : nullptr;
						if (!mg || mg == first || altarLeads.contains(mg) || mg->effectSounds.empty()) continue;
						bool same = true;
						for (auto& sp : mg->effectSounds) same &= !sp.sound || have.contains(sp.sound);
						if (same) mg->effectSounds.clear(), ++n;
					}
				}
				logger::info("audio: {} more echoes of the altar's spells silenced", n);
			}
		}
		void Inject()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh || !dh->LookupModByName(kPack)) {
				logger::info("fantasy: the High Fantasy Pack isn't installed - Skyrim's encounters left as they are");
				return;
			}
			// (Noah: "the Minotaur's enemy audio is way too loud") its two sounds (its voice, its entrance) brought down
			// 14 dB - about a third as loud
			for (const std::uint32_t id : { 0x000D0Eu, 0x000D25u }) {
				if (auto* sd = dh->LookupForm<RE::BGSSoundDescriptorForm>(id, kPack); sd && sd->soundDescriptor) {
					if (auto* sdef = skyrim_cast<RE::BGSStandardSoundDef*>(sd->soundDescriptor)) {
						sdef->soundCharacteristics.staticAttenuation = static_cast<std::uint16_t>(std::min(10000, sdef->soundCharacteristics.staticAttenuation + 1400));
						logger::info("fantasy: minotaur sound {:06X} made quieter (attenuation {:.1f} dB)", id, sdef->soundCharacteristics.staticAttenuation / 100.0f);
					}
				}
			}
			// (Noah: "the audio mixing pass") every creature sound of the packs measured louder than Skyrim's own is turned
			// down to meet them (tools/audiomix.py: its files' level less its own attenuation, to -21 dB)
			int mixed = 0;
			for (const auto& m : gen::kAudioMix) {
				if (auto* sd = dh->LookupForm<RE::BGSSoundDescriptorForm>(m.id, m.plugin); sd && sd->soundDescriptor) {
					if (auto* sdef = skyrim_cast<RE::BGSStandardSoundDef*>(sd->soundDescriptor)) {
						sdef->soundCharacteristics.staticAttenuation = static_cast<std::uint16_t>(std::min(10000, sdef->soundCharacteristics.staticAttenuation + static_cast<int>(m.extraDb * 100.0f)));
						++mixed;
					}
				}
			}
			logger::info("fantasy: {} of {} loud creature sounds brought down to the mix", mixed, gen::kAudioMix.size());
			// (Noah: "the vampire spell's sound is way too loud - balance every spell sound") a spell of several effects played
			// each effect's sounds at once - Mysticism gives its perk and dummy effects the main effect's own sounds, so a
			// Vampiric Bolt let go of the same release three times over. A later effect whose sounds the first one already
			// plays falls silent (never one that leads any spell, scroll, staff or potion of its own)
			{
				std::unordered_set<RE::EffectSetting*> leads;
				auto lead = [&](RE::MagicItem* m) { if (m && !m->effects.empty() && m->effects[0] && m->effects[0]->baseEffect) leads.insert(m->effects[0]->baseEffect); };
				for (auto* x : dh->GetFormArray<RE::SpellItem>()) lead(x);
				for (auto* x : dh->GetFormArray<RE::ScrollItem>()) lead(x);
				for (auto* x : dh->GetFormArray<RE::EnchantmentItem>()) lead(x);
				for (auto* x : dh->GetFormArray<RE::AlchemyItem>()) lead(x);
				std::unordered_set<RE::EffectSetting*> hushed;
				auto hush = [&](RE::MagicItem* m) {
					if (!m || m->effects.size() < 2 || !m->effects[0] || !m->effects[0]->baseEffect) return;
					auto* first = m->effects[0]->baseEffect;
					std::unordered_set<RE::BGSSoundDescriptorForm*> have;
					for (auto& sp : first->effectSounds) if (sp.sound) have.insert(sp.sound);
					if (have.empty()) return;
					for (std::size_t i = 1; i < m->effects.size(); ++i) {
						auto* mg = m->effects[i] ? m->effects[i]->baseEffect : nullptr;
						if (!mg || mg == first || leads.contains(mg) || mg->effectSounds.empty() || hushed.contains(mg)) continue;
						bool same = true;
						for (auto& sp : mg->effectSounds) same &= !sp.sound || have.contains(sp.sound);
						if (same) mg->effectSounds.clear(), hushed.insert(mg);
					}
				};
				for (auto* x : dh->GetFormArray<RE::SpellItem>()) hush(x);
				for (auto* x : dh->GetFormArray<RE::ScrollItem>()) hush(x);
				for (auto* x : dh->GetFormArray<RE::EnchantmentItem>()) hush(x);

				logger::info("audio: {} doubled spell sounds silenced (an effect echoing its spell's first)", hushed.size());
			}
			int added = 0;
			for (const auto& a : kAdds) {
				auto* list = RE::TESForm::LookupByID<RE::TESLevCharacter>(a.list);
				auto* npc = dh->LookupForm(a.npc, kPack);
				if (!list || !npc || !npc->Is(RE::FormType::NPC)) {
					logger::warn("fantasy: {} ({:06X}) or list {:06X} missing", a.what, a.npc, a.list);
					continue;
				}
				if ((static_cast<std::uint32_t>(list->llFlags) & static_cast<std::uint32_t>(RE::TESLeveledList::Flag::kCalculateFromAllLevelsLTOrEqPCLevel)) == 0) {
					logger::warn("fantasy: list {:06X} picks only its top level - {} left out", a.list, a.what);
					continue;
				}
				auto&      e = list->entries;
				const auto n = e.size();
				if (n >= 250) {
					continue;
				}
				bool dup = false;
				for (auto& o : e) dup |= o.form == npc;
				if (dup) {
					continue;
				}
				std::vector<RE::LEVELED_OBJECT> v(e.begin(), e.end());
				RE::LEVELED_OBJECT o{};
				o.form = npc, o.count = 1, o.level = a.level, o.pad0C = 0, o.itemExtra = nullptr;
				v.push_back(o);
				std::stable_sort(v.begin(), v.end(), [](const auto& x, const auto& y) { return x.level < y.level; });
				e.resize(v.size());
				std::copy(v.begin(), v.end(), e.begin());
				list->numEntries = static_cast<std::uint8_t>(v.size());
				++added;
			}
			logger::info("fantasy: {} High Fantasy Pack creatures added to Skyrim's wilderness and bandit encounters", added);
		}
	}
}
