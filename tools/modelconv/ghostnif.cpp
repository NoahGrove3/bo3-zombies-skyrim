// ghostnif <in.nif> <out.nif> <grey.dds path in Data>: the model greyed out - every lit shape's colour map swapped for a flat
// grey (its normal map kept, so its carving still catches the light), its glow and emissive put out; every effect shader
// (a flask's fizz, a halo) dimmed to a faint grey. (Noah: "a grayed out version so that I can see when I have it unlocked")
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "usage: ghostnif in out grey.dds\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	auto& h = nif.GetHeader();
	int lit = 0, fx = 0;
	for (auto* shape : nif.GetShapes()) {
		auto* sh = nif.GetShader(shape);
		if (!sh) continue;
		if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(sh)) {
			std::string grey(argv[3]);
			nif.SetTextureSlot(shape, grey, 0);
			std::string none;
			nif.SetTextureSlot(shape, none, 2);  // (no glow map)
			ls->emissiveColor = Vector3(0, 0, 0);
			ls->emissiveMultiple = 0.0f;
			ls->glossiness = 12.0f;
			ls->specularStrength = 0.35f;
			ls->shaderFlags2 &= ~static_cast<uint32_t>(SLSF2_GLOW_MAP);
			++lit;
		} else if (auto* es = dynamic_cast<BSEffectShaderProperty*>(sh)) {
			es->baseColor = Color4(0.35f, 0.35f, 0.36f, es->baseColor.a * 0.35f);
			es->baseColorScale *= 0.4f;
			++fx;
		}
	}
	(void)h;
	nif.Save(argv[2]);
	printf("%d lit shapes greyed, %d effects dimmed\n", lit, fx);
}
