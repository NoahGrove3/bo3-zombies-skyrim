// dragonexport <dragon.nif> <out.txt>: Skyrim's own dragon mesh as text, for tools/dragonbuild.py - every shape's
// vertices (position, normal, uv, colour), triangles, its skin's bones (name, skin-to-bone transform) and each vertex's
// weights, and its textures. The skeleton's hierarchy comes from skeleton.nif (dragonexport --skeleton <nif> <out>).
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;

static void Xf(FILE* o, const MatTransform& x)
{
	auto& r = x.rotation;
	fprintf(o, " %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.4f %.4f %.4f %.6f", r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2],
		x.translation.x, x.translation.y, x.translation.z, x.scale);
}

int main(int argc, char** argv)
{
	if (argc < 3) return 1;
	NifFile nif;
	if (std::string(argv[1]) == "--skeleton") {
		if (nif.Load(argv[2])) return 2;
		FILE* o = fopen(argv[3], "w");
		auto& h = nif.GetHeader();
		for (uint32_t i = 0; i < h.GetNumBlocks(); ++i)
			if (auto* n = h.GetBlock<NiNode>(i)) {
				auto* p = nif.GetParentNode(n);
				std::string nm = n->name.get(), pn = p ? p->name.get() : "";
				for (auto& c : nm) if (c == ' ') c = '_';
				for (auto& c : pn) if (c == ' ') c = '_';
				MatTransform w;
				nif.GetNodeTransformToGlobal(n->name.get(), w);
				fprintf(o, "node %s %s", nm.c_str(), pn.empty() ? "-" : pn.c_str());
				Xf(o, n->GetTransformToParent());
				Xf(o, w);
				fprintf(o, "\n");
			}
		fclose(o);
		return 0;
	}
	if (nif.Load(argv[1])) return 2;
	FILE* o = fopen(argv[2], "w");
	for (auto* s : nif.GetShapes()) {
		std::vector<Vector3> v;
		nif.GetVertsForShape(s, v);
		const auto* n = nif.GetNormalsForShape(s);
		const auto* uv = nif.GetUvsForShape(s);
		std::vector<Color4> col;
		nif.GetColorsForShape(s, col);
		std::vector<Triangle> tris;
		s->GetTriangles(tris);
		std::string nm = s->name.get();
		for (auto& c : nm) if (c == ' ') c = '_';
		fprintf(o, "shape %s %zu %zu\n", nm.c_str(), v.size(), tris.size());
		fprintf(o, "xform");
		Xf(o, s->GetTransformToParent());
		fprintf(o, "\n");
		MatTransform g;
		if (nif.GetShapeTransformGlobalToSkin(s, g)) { fprintf(o, "globaltoskin"); Xf(o, g); fprintf(o, "\n"); }
		for (uint32_t t = 0; t < 9; ++t) {
			std::string tex;
			nif.GetTextureSlot(s, tex, t);
			fprintf(o, "tex %u %s\n", t, tex.empty() ? "-" : tex.c_str());
		}
		std::vector<std::string> bones;
		nif.GetShapeBoneList(s, bones);
		for (uint32_t b = 0; b < bones.size(); ++b) {
			std::string bn = bones[b];
			for (auto& c : bn) if (c == ' ') c = '_';
			MatTransform x;
			nif.GetShapeTransformSkinToBone(s, b, x);
			fprintf(o, "bone %s", bn.c_str());
			Xf(o, x);
			fprintf(o, "\n");
		}
		for (size_t i = 0; i < v.size(); ++i) {
			Vector3 nn = n && i < n->size() ? (*n)[i] : Vector3(0, 0, 1);
			Vector2 tt = uv && i < uv->size() ? (*uv)[i] : Vector2();
			Color4 c = i < col.size() ? col[i] : Color4(1, 1, 1, 1);
			fprintf(o, "v %.4f %.4f %.4f %.5f %.5f %.5f %.6f %.6f %.3f %.3f %.3f %.3f\n", v[i].x, v[i].y, v[i].z, nn.x, nn.y, nn.z, tt.u, tt.v, c.r, c.g, c.b, c.a);
		}
		for (auto& t : tris) fprintf(o, "t %d %d %d\n", t.p1, t.p2, t.p3);
		for (uint32_t b = 0; b < bones.size(); ++b) {
			std::unordered_map<uint16_t, float> w;
			nif.GetShapeBoneWeights(s, b, w);
			for (auto& [vi, x] : w)
				if (x > 0) fprintf(o, "w %u %u %.5f\n", vi, b, x);
		}
	}
	fclose(o);
	return 0;
}
