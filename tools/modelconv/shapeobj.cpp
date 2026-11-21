// shapeobj <nif> <out.obj> : every shape's geometry in the model's own space (its shape transforms applied) as one OBJ - positions,
// UVs, normals - each shape its own "usemtl <shape name>" (to bring a Skyrim model into tools/*.py builds)
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "shapeobj <nif> <out.obj>\n"); return 1; }
	NifFile n;
	if (n.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	FILE* o = fopen(argv[2], "w");
	int base = 1;
	for (auto* s : n.GetShapes()) {
		MatTransform t;
		n.GetNodeTransformToGlobal(s->name.get(), t);
		auto* vs = n.GetVertsForShape(s);
		auto* uv = n.GetUvsForShape(s);
		auto* nm = n.GetNormalsForShape(s);
		std::vector<Triangle> tris;
		s->GetTriangles(tris);
		if (!vs) continue;
		fprintf(o, "usemtl %s\n", s->name.get().c_str());
		for (std::size_t i = 0; i < vs->size(); ++i) {
			auto p = t.ApplyTransform((*vs)[i]);
			fprintf(o, "v %f %f %f\n", p.x, p.y, p.z);
			fprintf(o, "vt %f %f\n", uv && i < uv->size() ? (*uv)[i].u : 0.0f, uv && i < uv->size() ? 1.0f - (*uv)[i].v : 0.0f);
			Vector3 q = nm && i < nm->size() ? t.rotation * (*nm)[i] : Vector3(0, 0, 1);
			fprintf(o, "vn %f %f %f\n", q.x, q.y, q.z);
		}
		for (auto& tr : tris) fprintf(o, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", tr.p1 + base, tr.p1 + base, tr.p1 + base, tr.p2 + base, tr.p2 + base, tr.p2 + base, tr.p3 + base, tr.p3 + base, tr.p3 + base);
		base += static_cast<int>(vs->size());
	}
	fclose(o);
	printf("%s\n", argv[2]);
	return 0;
}
