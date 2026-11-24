// (Noah, 2026-11-24: "the weapon effects we made are not on the blade - it's on the floor and moves with my character") an art
// object laid on an actor (a magic effect's hit art) hangs at the actor's root unless its model names a node to hang on: Skyrim
// reads the root's NiStringsExtraData "AttachT" - "NamedNode&WEAPON" puts it on the weapon in the hand (as Vibrant Weapons' own
// arts do). addattach <in.nif> <node> [out.nif]
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "addattach <in.nif> <node> [out.nif]\n"); return 2; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	auto& h = nif.GetHeader();
	const std::string want = std::string("NamedNode&") + argv[2];
	bool had = false;
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i)
		if (auto* s = h.GetBlock<NiStringsExtraData>(i); s && s->name.get() == "AttachT") {
			s->stringsData.clear();
			NiString v; v.get() = want; s->stringsData.push_back(v);
			had = true;
		}
	if (!had) {
		auto s = std::make_unique<NiStringsExtraData>();
		s->name.get() = "AttachT";
		NiString v; v.get() = want; s->stringsData.push_back(v);
		nif.GetRootNode()->extraDataRefs.AddBlockRef(h.AddBlock(std::move(s)));
	}
	NifSaveOptions o; o.optimize = false;
	if (nif.Save(argc > 3 ? argv[3] : argv[1], o)) { fprintf(stderr, "save failed\n"); return 1; }
	printf("%s: AttachT %s\n", argv[1], want.c_str());
	return 0;
}
