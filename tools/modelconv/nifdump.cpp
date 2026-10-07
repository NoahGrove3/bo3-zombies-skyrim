// nifdump <nif> [nodes]: shapes (verts, bounds, textures, skin) and optionally every node's global transform
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv) {
	NifFile nif;
	if (nif.Load(argv[1])) { printf("load failed\n"); return 1; }
	auto v = nif.GetHeader().GetVersion();
	printf("version file %u user %u stream %u\n", v.File(), v.User(), v.Stream());
	for (auto* s : nif.GetShapes()) {
		std::vector<Vector3> verts; nif.GetVertsForShape(s, verts);
		Vector3 lo(1e9,1e9,1e9), hi(-1e9,-1e9,-1e9);
		for (auto& p : verts) { lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
		printf("shape '%s' type %s verts %zu tris %u bounds (%.1f %.1f %.1f)..(%.1f %.1f %.1f)\n", s->name.get().c_str(), s->GetBlockName(), verts.size(), s->GetNumTriangles(), lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
		auto tr = s->GetTransformToParent();
		printf("  xform t(%.2f %.2f %.2f) scale %.3f\n", tr.translation.x, tr.translation.y, tr.translation.z, tr.scale);
		for (uint32_t i = 0; i < 9; ++i) { std::string t; nif.GetTextureSlot(s, t, i); if (!t.empty()) printf("  tex%u %s\n", i, t.c_str()); }
		if (auto* sh = nif.GetShader(s)) printf("  shader %s type %u flags1 %08X flags2 %08X\n", sh->GetBlockName(), sh->GetShaderType(), 0u, 0u);
		std::vector<std::string> bones; nif.GetShapeBoneList(s, bones);
		if (!bones.empty()) { printf("  skinned to %zu bones:", bones.size()); for (auto& b : bones) printf(" [%s]", b.c_str()); printf("\n"); }
	}
	if (argc > 2) for (auto* n : nif.GetNodes()) {
		MatTransform g; nif.GetNodeTransformToGlobal(n->name.get(), g);
		auto* p = nif.GetParentNode(n);
		printf("node '%s' parent '%s' pos (%.2f %.2f %.2f) scale %.3f\n", n->name.get().c_str(), p ? p->name.get().c_str() : "-", g.translation.x, g.translation.y, g.translation.z, g.scale);
	}
}
