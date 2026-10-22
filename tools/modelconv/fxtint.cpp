// fxtint <in.nif> <out.nif> <r g b> [scale]: every effect shader's base colour set (keeps its alpha), its emissive scaled,
// and the whole model scaled (the night's portals in the Curse's red; the elites' bigger)
#include "NifFile.hpp"
#include <cstdio>
#include <cstdlib>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 6) { fprintf(stderr, "usage: fxtint in out r g b [scale]\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	auto& h = nif.GetHeader();
	int n = 0;
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
		if (auto* p = h.GetBlock<BSEffectShaderProperty>(i)) {
			p->baseColor = Color4(atof(argv[3]), atof(argv[4]), atof(argv[5]), p->baseColor.a);
			++n;
		}
	}
	if (argc >= 7) {
		if (auto* root = nif.GetRootNode()) root->transform.scale *= static_cast<float>(atof(argv[6]));
	}
	nif.Save(argv[2]);
	printf("%d effect shaders tinted\n", n);
}
