// dragonvariant: a bond's look for the glider's dragon (src/Movement.cpp, the Echo Altar's dragon bonds).
//   dragonvariant in.nif out.nif <diffuse.dds> <glow.dds> r g b mult
// The dragon's shader becomes Skyrim's glow shader: <glow.dds> (its eyes, and for the higher bonds its markings) lit in
// the emissive colour; <diffuse.dds> replaces its skin. (The env map is given up: the glow shader has no slot for it.)
#include "NifFile.hpp"
#include <cstdio>
#include <cstdlib>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 9) { fprintf(stderr, "usage: dragonvariant in.nif out.nif diffuse glow r g b mult\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	int n = 0;
	for (auto* s : nif.GetShapes()) {
		auto* sh = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(s));
		if (!sh) continue;
		printf("before: type %u flags1 %08X flags2 %08X emissive %.2f %.2f %.2f x%.2f\n", sh->GetShaderType(), sh->shaderFlags1, sh->shaderFlags2,
			sh->emissiveColor.x, sh->emissiveColor.y, sh->emissiveColor.z, sh->emissiveMultiple);
		std::string d = argv[3], g = argv[4];
		nif.SetTextureSlot(s, d, 0);
		nif.SetTextureSlot(s, g, 2);
		std::string none;
		nif.SetTextureSlot(s, none, 4);
		nif.SetTextureSlot(s, none, 5);
		sh->SetShaderType(BSLSP_GLOWMAP);
		sh->shaderFlags1 &= ~(1u << 7);  // SLSF1_Environment_Mapping off
		sh->shaderFlags1 |= (1u << 22);  // SLSF1_Own_Emit
		sh->shaderFlags2 |= (1u << 6);   // SLSF2_Glow_Map
		if (getenv("DV_NOBACKLIGHT")) sh->shaderFlags2 &= ~(1u << 27);  // (SLSF2_Back_Lighting: the glow map is ignored beside it)
		sh->emissiveColor = Vector3(atof(argv[5]), atof(argv[6]), atof(argv[7]));
		sh->emissiveMultiple = atof(argv[8]);
		++n;
	}
	nif.Save(argv[2]);
	printf("wrote %s (%d shapes)\n", argv[2], n);
}
