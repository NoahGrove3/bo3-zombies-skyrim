// (Noah, 2026-11-24: "my forearms are still invisible" with the class sets, in first person) Skyrim's own skinned armour keeps
// each shape's bounding sphere empty (centre 0, radius 0): the game bounds a skinned piece by its bones. Ours were saved with a
// sphere worked out round the bind pose (nifly's bounds update): in first person that sphere sits away from where the arms are
// drawn and the sleeves were culled. zerobounds <in.nif> [out.nif]: every skinned shape's sphere set back to empty.
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "zerobounds <in.nif> [out.nif]\n"); return 2; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	int n = 0;
	for (auto* s : nif.GetShapes()) if (s->IsSkinned()) { s->SetBounds(BoundingSphere()); ++n; }
	NifSaveOptions o; o.optimize = false;
	if (nif.Save(argc > 2 ? argv[2] : argv[1], o)) { fprintf(stderr, "save failed\n"); return 1; }
	printf("%d skinned shapes\n", n);
	return 0;
}
