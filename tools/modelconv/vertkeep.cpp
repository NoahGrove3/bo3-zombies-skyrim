// vertkeep <in.nif> <out.nif> <shape> <keep.txt> [drop shape ...]: one shape cut down to the vertices listed (one index a line, the shape's own
// order); the rest go, and every triangle that used them, its skin and partitions kept in step (nifly DeleteVertsForShape). Used to
// lift one part out of another author's model on the player's own PC - the Black Mage robe's trousers and greaves for the
// Spellsword's legs (tools/sslegs.py); never committed.
#include "NifFile.hpp"
#include <cstdio>
#include <fstream>
#include <set>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 5) { fprintf(stderr, "usage: vertkeep in.nif out.nif shape keep.txt\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	NiShape* sh = nif.FindBlockByName<NiShape>(argv[3]);
	if (!sh) { fprintf(stderr, "no shape %s\n", argv[3]); return 1; }
	std::set<int> keep;
	std::ifstream f(argv[4]);
	for (int i; f >> i;) keep.insert(i);
	std::vector<uint16_t> del;
	const int n = static_cast<int>(sh->GetNumVertices());
	for (int i = 0; i < n; ++i) if (!keep.count(i)) del.push_back(static_cast<uint16_t>(i));
	if (!del.empty() && nif.DeleteVertsForShape(sh, del)) { fprintf(stderr, "nothing left of it\n"); return 1; }   // (true: every vertex gone)
	nif.UpdateSkinPartitions(sh);
	for (int a = 5; a < argc; ++a) if (auto* d = nif.FindBlockByName<NiShape>(argv[a])) nif.DeleteShape(d), printf("dropped %s\n", argv[a]);   // (another shape of it not wanted: a robe's own neck skin)
	if (nif.Save(argv[2])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("%s: kept %zu of %d\n", argv[3], keep.size(), n);
	return 0;
}
