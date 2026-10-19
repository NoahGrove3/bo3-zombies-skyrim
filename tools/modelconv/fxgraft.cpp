// fxgraft <in.nif> <src.nif> <srcShape> <out.nif> <newName> <greyscale palette|-> <scale> <tx> <ty> <tz> [r g b colourScale]
// Copies an animated effect shape (its shader, alpha and shader controllers) from another NIF onto this one's root,
// scaled and moved, its palette (greyscale-to-colour gradient) swapped - e.g. the Spectral Bow's flames recoloured.
#include "NifFile.hpp"
#include <cstdio>
#include <cstdlib>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 11) { fprintf(stderr, "usage\n"); return 1; }
	NifFile nif, src;
	if (nif.Load(argv[1]) || src.Load(argv[2])) { fprintf(stderr, "load failed\n"); return 1; }
	NiShape* s = nullptr;
	for (auto* sh : src.GetShapes()) if (sh->name.get() == argv[3]) s = sh;
	if (!s) { fprintf(stderr, "no shape %s\n", argv[3]); return 1; }
	auto* d = nif.CloneShape(s, argv[5], &src);
	if (!d) { fprintf(stderr, "clone failed\n"); return 1; }
	MatTransform t = d->GetTransformToParent();
	t.scale = std::atof(argv[7]);
	t.translation = Vector3(std::atof(argv[8]), std::atof(argv[9]), std::atof(argv[10]));
	d->SetTransformToParent(t);
	auto& h = nif.GetHeader();
	auto* sp = dynamic_cast<BSEffectShaderProperty*>(nif.GetShader(d));
	if (sp) {
		if (std::string(argv[6]) != "-") sp->greyscaleTexture.get() = argv[6];
		if (argc >= 15) { sp->baseColor = Color4(std::atof(argv[11]), std::atof(argv[12]), std::atof(argv[13]), 1.0f); sp->baseColorScale = std::atof(argv[14]); }
		const uint32_t spId = h.GetBlockID(sp);
		// the shader's controllers point back at it
		for (auto ci = sp->controllerRef.index; ci != NIF_NPOS;) {
			auto* c = h.GetBlock<NiTimeController>(ci);
			if (!c) break;
			c->targetRef.index = spId;
			ci = c->nextControllerRef.index;
		}
	}
	nif.Save(argv[4]);
	printf("grafted %s -> %s (shader %s)\n", argv[3], argv[5], sp ? "effect" : "?");
}
