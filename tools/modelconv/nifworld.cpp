// nifworld <nif>: every shape's triangles in the model's root space (each node's turn, scale and place applied up the tree, but
// not the root node's own - the game hangs our effect art under that root, which carries its own turn), skinned shapes posed by
// their bones (the claws are skinned to a turned node of their own) - with its name, its diffuse texture and whether it is a lit
// shape or an effect - for fitting effects to a weapon's steel (tools/weaponfx.py).
// Prints: "shape <name>\t<tex0>\t<lit|effect>\t<alpha 0|1>" then "v x y z u v" lines then "t a b c" lines.
#include "NifFile.hpp"
#include <cstdio>
#include <unordered_map>
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
// an object's transform up to (not including) the root node
static MatTransform toRoot(NifFile& nif, NiAVObject* o)
{
	MatTransform g = o->GetTransformToParent();
	NiAVObject* cur = o;
	for (int guard = 0; guard < 64; ++guard) {
		auto* p = nif.GetParentNode(cur);
		if (!p || !nif.GetParentNode(p)) break;   // (p is the root: its own transform left out)
		g = compose(p->GetTransformToParent(), g);
		cur = p;
	}
	if (!nif.GetParentNode(o)) return MatTransform{};   // (the root itself)
	return g;
}
int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "nifworld <nif>\n"); return 2; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	for (auto* s : nif.GetShapes()) {
		std::string tex; nif.GetTextureSlot(s, tex, 0);
		auto* sh = nif.GetShader(s);
		const bool effect = sh && sh->HasType<BSEffectShaderProperty>();
		printf("shape %s\t%s\t%s\t%d\n", s->name.get().c_str(), tex.c_str(), effect ? "effect" : "lit", nif.GetAlphaProperty(s) ? 1 : 0);
		const auto* vs = nif.GetVertsForShape(s);
		const auto* uvs = nif.GetUvsForShape(s);
		auto UV = [&](size_t i) { return uvs && i < uvs->size() ? (*uvs)[i] : Vector2{}; };
		std::vector<int> bones;
		if (s->IsSkinned()) nif.GetShapeBoneIDList(s, bones);
		if (vs && !bones.empty()) {
			// each vertex: its bones' poses (bone to root after skin to bone), by weight
			std::vector<Vector3> out(vs->size());
			std::vector<float>   wsum(vs->size(), 0.0f);
			for (uint32_t b = 0; b < bones.size(); ++b) {
				auto* bn = nif.GetHeader().GetBlock<NiNode>(bones[b]);
				if (!bn) continue;
				MatTransform s2b;
				nif.GetShapeTransformSkinToBone(s, b, s2b);
				const MatTransform m = compose(toRoot(nif, bn), s2b);
				std::unordered_map<uint16_t, float> w;
				nif.GetShapeBoneWeights(s, b, w);
				for (auto& [i, x] : w) if (i < out.size() && x > 0) out[i] += apply(m, (*vs)[i]) * x, wsum[i] += x;
			}
			const MatTransform g = toRoot(nif, s);
			for (size_t i = 0; i < out.size(); ++i) {
				const auto w = wsum[i] > 0 ? out[i] / wsum[i] : apply(g, (*vs)[i]);
				printf("v %.4f %.4f %.4f %.4f %.4f\n", w.x, w.y, w.z, UV(i).u, UV(i).v);
			}
		} else if (vs) {
			const MatTransform g = toRoot(nif, s);
			for (size_t i = 0; i < vs->size(); ++i) { auto w = apply(g, (*vs)[i]); printf("v %.4f %.4f %.4f %.4f %.4f\n", w.x, w.y, w.z, UV(i).u, UV(i).v); }
		}
		std::vector<Triangle> tris; s->GetTriangles(tris);
		for (auto& t : tris) printf("t %d %d %d\n", t.p1, t.p2, t.p3);
	}
	return 0;
}
