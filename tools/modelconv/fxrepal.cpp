// fxrepal <in.nif> <out.nif> <greyscale palette> [r g b]: every effect shader in the model takes that palette (its
// greyscale-to-colour ramp) - and, given, that base colour - e.g. a fire tornado turned to ice or lightning.
#include "NifFile.hpp"
#include <cstdio>
#include <cstdlib>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "usage\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	auto& h = nif.GetHeader();
	int n = 0;
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
		if (auto* p = h.GetBlock<BSEffectShaderProperty>(i)) {
			p->greyscaleTexture.get() = argv[3];
			p->shaderFlags1 |= 0x10;  // greyscale to palette colour
			if (argc >= 7) p->baseColor = Color4(atof(argv[4]), atof(argv[5]), atof(argv[6]), p->baseColor.a);
			++n;
		}
	}
	nif.Save(argv[2]);
	printf("%d effect shaders -> %s\n", n, argv[3]);
}
