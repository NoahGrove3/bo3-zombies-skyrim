// setpart <in.nif> <out.nif> [slot] : every skinned shape's dismember partitions read out, and (with a slot) set to that body
// part - an armour addon's mesh must carry its addon's own slot, or Skyrim never draws it (Skyrim's body underwear says 32)
#include "NifFile.hpp"
#include <cstdio>
#include <cstdlib>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "setpart <in.nif> <out.nif> [slot]\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	const int slot = argc > 3 ? std::atoi(argv[3]) : -1;
	for (auto* sh : nif.GetShapes()) {
		auto* bsd = nif.GetHeader().GetBlock<BSDismemberSkinInstance>(sh->SkinInstanceRef());
		if (!bsd) { printf("%s: no dismember skin\n", sh->name.get().c_str()); continue; }
		for (auto& p : bsd->partitions) {
			printf("%s: partition %d (flags %d)%s\n", sh->name.get().c_str(), (int)p.partID, (int)p.flags, slot > 0 ? " -> set" : "");
			if (slot > 0) p.partID = static_cast<uint16_t>(slot);
		}
	}
	if (slot > 0 && nif.Save(argv[2])) { fprintf(stderr, "cannot save %s\n", argv[2]); return 1; }
	return 0;
}
