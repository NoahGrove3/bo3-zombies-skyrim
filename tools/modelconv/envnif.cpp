// envnif <in.nif> <out.nif> <diffuse substring> <cubemap.dds> <envmask.dds> <env scale>: (Noah: "in game the mace textures
// look really low quality - in the model that I sent you, that's exactly how I want it to look") the lit shapes whose colour map
// path holds <diffuse substring> turned into polished metal as Blender shows it: Skyrim's environment-map shader, a cubemap
// reflected in them through a mask (bare smooth metal reflects, rough or painted parts don't); their glow given up (that shader
// has no slot for it)
#include "NifFile.hpp"
#include <cstdio>
#include <string>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 7) { fprintf(stderr, "usage: envnif in out diffuse-substr cube mask scale\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	int n = 0;
	for (auto* shape : nif.GetShapes()) {
		auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(shape));
		if (!ls) continue;
		std::string d;
		nif.GetTextureSlot(shape, d, 0);
		if (d.find(argv[3]) == std::string::npos) continue;
		std::string none, cube(argv[4]), mask(argv[5]);
		nif.SetTextureSlot(shape, none, 2);
		ls->SetShaderType(BSLSP_ENVMAP);
		ls->shaderFlags2 &= ~static_cast<uint32_t>(SLSF2_GLOW_MAP);
		ls->shaderFlags1 |= SLSF1_ENVIRONMENT_MAPPING;
		ls->emissiveMultiple = 0.0f;
		ls->environmentMapScale = std::stof(argv[6]);
		nif.SetTextureSlot(shape, cube, 4);
		nif.SetTextureSlot(shape, mask, 5);
		++n;
	}
	nif.Save(argv[2]);
	printf("%d shapes made reflective metal\n", n);
	return n ? 0 : 2;
}
