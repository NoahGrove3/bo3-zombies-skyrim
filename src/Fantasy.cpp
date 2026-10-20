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

		void Inject()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh || !dh->LookupModByName(kPack)) {
				logger::info("fantasy: the High Fantasy Pack isn't installed - Skyrim's encounters left as they are");
				return;
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
