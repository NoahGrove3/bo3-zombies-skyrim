// nifnodes <nif>: every NiNode with its local and world transform, and per-shape skin bone bind transforms
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	auto& h = nif.GetHeader();
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) if (auto* n = h.GetBlock<NiNode>(i)) {
		MatTransform w; nif.GetNodeTransformToGlobal(n->name.get(), w);
		auto& t = n->transform;
		printf("%2u %-22s local t(%.2f %.2f %.2f) world t(%.2f %.2f %.2f) children %zu\n", i, n->name.get().c_str(), t.translation.x, t.translation.y, t.translation.z, w.translation.x, w.translation.y, w.translation.z, n->childRefs.GetSize());
		auto r = w.rotation; printf("      rot [%.2f %.2f %.2f | %.2f %.2f %.2f | %.2f %.2f %.2f]\n", r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
	}
	for (auto* s : nif.GetShapes()) {
		std::vector<std::string> bones; nif.GetShapeBoneList(s, bones);
		for (auto& b : bones) { MatTransform x; nif.GetShapeTransformSkinToBone(s, b, x); printf("skin->%s t(%.2f %.2f %.2f)\n", b.c_str(), x.translation.x, x.translation.y, x.translation.z); }
	}
}
