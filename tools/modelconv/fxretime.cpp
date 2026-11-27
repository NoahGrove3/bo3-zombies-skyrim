// fxretime <nif> <old period> <new period>: every effect-shader controller that loops over <old period> seconds stretched to
// <new period> (its keys with it) - (Noah, Nov 27: the flasks' swirling potion "not looped ... a pause every 3 seconds": its swirl
// raced round once every 0.3 s, too fast to read as moving - now a slow climb)
#include "NifFile.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "usage\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) return 1;
	const float a = std::atof(argv[2]), b = std::atof(argv[3]);
	auto& h = nif.GetHeader();
	int n = 0;
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
		auto* c = h.GetBlock<BSEffectShaderPropertyFloatController>(i);
		if (!c || std::fabs(c->stopTime - a) > 0.005f) continue;
		auto* ip = h.GetBlock<NiFloatInterpolator>(c->interpolatorRef.index);
		auto* d = ip ? h.GetBlock<NiFloatData>(ip->dataRef.index) : nullptr;
		if (!d) continue;
		for (uint32_t k = 0; k < d->data.GetNumKeys(); ++k) { auto key = d->data.GetKey(k); key.time *= b / a; d->data.SetKey(k, key); }
		c->stopTime = b;
		++n;
	}
	nif.Save(argv[1]);
	printf("%s: %d controller(s) %.2f s -> %.2f s\n", argv[1], n, a, b);
}
