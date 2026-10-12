// nifexport <nif> <out.npz-dir>: a skinned SE NIF as plain arrays for the offline renderer (tools/scene3d.py)
//   nodes.txt        index name parent  world: tx ty tz  r00 r01 r02 r10 r11 r12 r20 r21 r22
//   shape_<i>.txt    header lines (name, diffuse, normal, glow, alpha, bones...), then v/n/t/w/f records
#include "NifFile.hpp"
#include <cstdio>
#include <filesystem>
#include <map>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: nifexport <nif> <outdir>\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	std::filesystem::create_directories(argv[2]);
	std::string out = argv[2];
	auto& h = nif.GetHeader();
	std::map<NiNode*, int> idx;
	std::vector<NiNode*> nodes;
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) if (auto* n = h.GetBlock<NiNode>(i)) { idx[n] = (int)nodes.size(); nodes.push_back(n); }
	FILE* f = fopen((out + "/nodes.txt").c_str(), "w");
	for (size_t i = 0; i < nodes.size(); ++i) {
		auto* n = nodes[i];
		auto* p = nif.GetParentNode(n);
		MatTransform w; nif.GetNodeTransformToGlobal(n->name.get(), w);
		auto& r = w.rotation;
		fprintf(f, "%zu\t%s\t%d\t%f %f %f\t%f %f %f %f %f %f %f %f %f\n", i, n->name.get().c_str(), p && idx.count(p) ? idx[p] : -1,
			w.translation.x, w.translation.y, w.translation.z, r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
	}
	fclose(f);
	int si = 0;
	for (auto* s : nif.GetShapes()) {
		auto* t = dynamic_cast<BSTriShape*>(s);
		if (!t) continue;
		FILE* o = fopen((out + "/shape_" + std::to_string(si++) + ".txt").c_str(), "w");
		fprintf(o, "name\t%s\n", s->name.get().c_str());
		for (int k = 0; k < 3; ++k) { std::string tex; nif.GetTextureSlot(s, tex, k == 2 ? 2 : k); fprintf(o, "tex%d\t%s\n", k, tex.c_str()); }
		fprintf(o, "alpha\t%d\n", nif.GetAlphaProperty(s) ? 1 : 0);
		std::vector<std::string> bones; nif.GetShapeBoneList(s, bones);
		for (auto& b : bones) {
			MatTransform x; nif.GetShapeTransformSkinToBone(s, b, x);
			auto& r = x.rotation;
			fprintf(o, "bone\t%s\t%f %f %f\t%f %f %f %f %f %f %f %f %f\t%f\n", b.c_str(), x.translation.x, x.translation.y, x.translation.z,
				r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2], x.scale);
		}
		MatTransform st = s->GetTransformToParent();
		fprintf(o, "shapexf\t%f %f %f\t%f\n", st.translation.x, st.translation.y, st.translation.z, st.scale);
		for (auto& v : t->vertData) {
			fprintf(o, "v %f %f %f %f %f", v.vert.x, v.vert.y, v.vert.z, (float)v.uv.u, (float)v.uv.v);
			fprintf(o, " %d %d %d %d %f %f %f %f\n", v.weightBones[0], v.weightBones[1], v.weightBones[2], v.weightBones[3], (float)v.weights[0], (float)v.weights[1], (float)v.weights[2], (float)v.weights[3]);
		}
		std::vector<Triangle> tris; t->GetTriangles(tris);
		for (auto& tr : tris) fprintf(o, "f %d %d %d\n", tr.p1, tr.p2, tr.p3);
		fclose(o);
	}
	printf("%zu nodes, %d shapes\n", nodes.size(), si);
}
