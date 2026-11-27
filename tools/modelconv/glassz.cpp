// glassz <in.nif> [out.nif]: every lit, alpha-blended shape (glass) stops writing depth - so what glows inside it (a flask's
// swirling potion) is never hidden behind it from one side and seen from the other (Noah, Nov 27: "only visible from certain
// angles"). Blended shapes are drawn back to front anyway; writing depth only hid the light behind them.
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 2) { fprintf(stderr, "usage: glassz in.nif [out.nif]\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	int n = 0;
	for (auto* s : nif.GetShapes()) {
		auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(s));
		auto* ap = nif.GetAlphaProperty(s);
		if (!ls || !ap || !(ap->flags & 1)) continue;   // (bit 0: blending on)
		if (ls->shaderFlags2 & SLSF2_ZBUFFER_WRITE) { ls->shaderFlags2 &= ~SLSF2_ZBUFFER_WRITE; ++n; printf("  %s: no depth write\n", s->name.get().c_str()); }
	}
	nif.Save(argc > 2 ? argv[2] : argv[1]);
	printf("%s: %d shape(s)\n", argv[1], n);
}
