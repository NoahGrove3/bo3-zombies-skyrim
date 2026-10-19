// fxshell <bow.nif> <effect src.nif> <effect shape> <out.nif> <palette|-> <r g b colourScale> <inflate> <uPerUnit> <vPerUnit>
// The element coursing through a bow: every lit, skinned part of the bow gets a twin a hair outside it (pushed out
// along its normals, on the same bones, so it bends with the draw) wearing another model's animated effect shader
// (the Spectral Bow's flowing flame: its swirl texture, its scrolling controllers), its palette swapped for the
// element's, its UVs laid along the bow's length so the scroll runs from grip to tips. The string and anything named
// *_flame are left alone.
#include "NifFile.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 12) { fprintf(stderr, "usage\n"); return 1; }
	NifFile nif, src;
	if (nif.Load(argv[1]) || src.Load(argv[2])) { fprintf(stderr, "load failed\n"); return 1; }
	NiShape* fxShape = nullptr;
	for (auto* sh : src.GetShapes()) if (sh->name.get() == argv[3]) fxShape = sh;
	if (!fxShape) { fprintf(stderr, "no effect shape %s\n", argv[3]); return 1; }
	const std::string pal = argv[5];
	const float r = atof(argv[6]), g = atof(argv[7]), b = atof(argv[8]), cs = atof(argv[9]), inflate = atof(argv[10]), uk = atof(argv[11]), vk = argc > 12 ? atof(argv[12]) : uk;
	auto& h = nif.GetHeader();
	std::vector<NiShape*> todo;
	for (auto* sh : nif.GetShapes()) {
		const std::string n = sh->name.get();
		if (!sh->IsSkinned() || n.find("_string") != std::string::npos || n.find("_flame") != std::string::npos || n.find("_course") != std::string::npos) continue;
		if (!dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(sh))) continue;
		todo.push_back(sh);
	}
	int made = 0;
	for (auto* sh : todo) {
		auto* c = nif.CloneShape(sh, sh->name.get() + "_course");
		if (!c) continue;
		// the effect shader (and its controllers) copied in, pointed at its new home
		auto* fxs = src.GetShader(fxShape);
		auto clone = fxs->Clone();
		const uint32_t sid = h.AddBlock(std::move(clone));
		nif.CloneChildren(h.GetBlock<NiObject>(sid), &src);
		auto* sp = h.GetBlock<BSEffectShaderProperty>(sid);
		if (!sp) continue;
		sp->shaderFlags1 |= 0x2;              // skinned
		sp->shaderFlags2 &= ~0x20u;           // no vertex colours (the bow has none)
		if (pal != "-") sp->greyscaleTexture.get() = pal;
		sp->baseColor = Color4(r, g, b, 1.0f);
		sp->baseColorScale = cs;
		for (auto ci = sp->controllerRef.index; ci != NIF_NPOS;) {
			auto* ctl = h.GetBlock<NiTimeController>(ci);
			if (!ctl) break;
			ctl->targetRef.index = sid;
			ci = ctl->nextControllerRef.index;
		}
		c->ShaderPropertyRef()->index = sid;
		if (auto* fa = src.GetAlphaProperty(fxShape)) {
			auto ac = fa->Clone();
			c->AlphaPropertyRef()->index = h.AddBlock(std::move(ac));
		}
		// a hair outside the surface, its UVs along the bow (y) and round it
		std::vector<Vector3> v, nrm;
		nif.GetVertsForShape(c, v);
		const auto* np = nif.GetNormalsForShape(c);
		std::vector<Vector2> uv(v.size());
		for (size_t i = 0; i < v.size(); ++i) {
			Vector3 n = np && i < np->size() ? (*np)[i] : Vector3(0, 0, 0);
			v[i] = v[i] + n * inflate;
			uv[i] = Vector2(v[i].y * uk, (v[i].x * 0.6f + v[i].z) * vk);
		}
		nif.SetVertsForShape(c, v);
		nif.SetUvsForShape(c, uv);
		nif.UpdateSkinPartitions(c);
		++made;
	}
	nif.Save(argv[4]);
	printf("coursing shells: %d\n", made);
}
