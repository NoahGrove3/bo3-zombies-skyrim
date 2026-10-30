// rootscale <factor> <in.nif> <out.nif>: (Noah: "the orange runic ring when charging up a spell - make it 50% of its size, it
// takes up too much of the screen in first person") a model's root scaled by factor (Strange Runes' hand runes)
#include "NifFile.hpp"
#include <cstdio>
#include <string>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 4) { fprintf(stderr, "usage: rootscale <factor> <in.nif> <out.nif>\n"); return 1; }
	NifFile n;
	if (n.Load(argv[2])) { fprintf(stderr, "load failed %s\n", argv[2]); return 1; }
	const auto before = n.GetHeader().GetNumBlocks();
	auto* r = n.GetRootNode();
	if (!r) { fprintf(stderr, "no root\n"); return 1; }
	// (Noah: "it starts off small and then grows - make the size it grows to half") the game sets an art's root itself:
	// the size is in the root's children (Strange Runes: the "Rune" node, its animation scaling the RotNode under it from
	// 0 to 1) - those are what is scaled
	const float f = std::stof(argv[1]);
	const float was = r->transform.scale;
	int k = 0;
	for (auto* c : n.GetChildren<NiAVObject>(r, false)) c->transform.scale *= f, ++k;
	if (n.Save(argv[3])) { fprintf(stderr, "save failed\n"); return 1; }
	NifFile chk;
	chk.Load(argv[3]);
	printf("%s: %d root children scaled by %.2f (root %.2f kept), blocks %u -> %u\n", argv[2], k, f, was, before, chk.GetHeader().GetNumBlocks());
	return chk.GetHeader().GetNumBlocks() == before ? 0 : 2;
}
