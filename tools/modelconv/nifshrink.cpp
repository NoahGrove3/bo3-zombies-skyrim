// nifshrink <factor> <in.nif> <out.nif>: a whole model made smaller about its root - each child of the root scaled and moved in
// by the factor (Noah: the scythes "are massive in game" - Battle Scythe's models, made 60% on the player's own machine from his
// own copy of the mod: tools/scythes.py; never shipped)
#include "NifFile.hpp"
#include <cstdio>
#include <string>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 4) { fprintf(stderr, "usage: nifshrink <factor> <in.nif> <out.nif>\n"); return 1; }
	NifFile n;
	if (n.Load(argv[2])) { fprintf(stderr, "load failed %s\n", argv[2]); return 1; }
	auto* r = n.GetRootNode();
	if (!r) { fprintf(stderr, "no root\n"); return 1; }
	const float f = std::stof(argv[1]);
	int k = 0;
	for (auto* c : n.GetChildren<NiAVObject>(r, false)) {
		c->transform.scale *= f;
		c->transform.translation = c->transform.translation * f;
		++k;
	}
	if (n.Save(argv[3])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("%s: %d children shrunk by %.2f\n", argv[2], k, f);
	return 0;
}
