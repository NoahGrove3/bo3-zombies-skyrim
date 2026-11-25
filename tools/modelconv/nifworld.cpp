// nifworld <nif>: every shape's triangles in the model's own space (each node's turn, scale and place applied up the tree),
// with its name, its diffuse texture and whether it is a lit shape or an effect - for fitting effects to a weapon's steel
// (tools/weaponfx.py). Prints: "shape <name>\t<tex0>\t<lit|effect>\t<alpha 0|1>" then "v x y z" lines then "t a b c" lines.
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
static Vector3 apply(const MatTransform& m, const Vector3& v) { return m.rotation * (v * m.scale) + m.translation; }
static MatTransform compose(const MatTransform& parent, const MatTransform& child)
{
	MatTransform r;
	r.translation = parent.rotation * (child.translation * parent.scale) + parent.translation;
	r.rotation = parent.rotation * child.rotation;
	r.scale = parent.scale * child.scale;
	return r;
}
int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "nifworld <nif>\n"); return 2; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	for (auto* s : nif.GetShapes()) {
		MatTransform g = s->GetTransformToParent();
		NiAVObject* cur = s;
		for (int guard = 0; guard < 64; ++guard) {
			auto* p = nif.GetParentNode(cur);
			if (!p) break;
			g = compose(p->GetTransformToParent(), g);
			cur = p;
		}
		std::string tex; nif.GetTextureSlot(s, tex, 0);
		auto* sh = nif.GetShader(s);
		const bool effect = sh && sh->HasType<BSEffectShaderProperty>();
		printf("shape %s\t%s\t%s\t%d\n", s->name.get().c_str(), tex.c_str(), effect ? "effect" : "lit", nif.GetAlphaProperty(s) ? 1 : 0);
		const auto* vs = nif.GetVertsForShape(s);
		if (vs) for (auto& v : *vs) { auto w = apply(g, v); printf("v %.4f %.4f %.4f\n", w.x, w.y, w.z); }
		std::vector<Triangle> tris; s->GetTriangles(tris);
		for (auto& t : tris) printf("t %d %d %d\n", t.p1, t.p2, t.p3);
	}
	return 0;
}
