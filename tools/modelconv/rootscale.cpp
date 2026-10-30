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
	const float was = r->transform.scale;
	r->transform.scale *= std::stof(argv[1]);
	if (n.Save(argv[3])) { fprintf(stderr, "save failed\n"); return 1; }
	NifFile chk;
	chk.Load(argv[3]);
	printf("%s: root %.3f -> %.3f, blocks %u -> %u\n", argv[2], was, r->transform.scale, before, chk.GetHeader().GetNumBlocks());
	return chk.GetHeader().GetNumBlocks() == before ? 0 : 2;
}
