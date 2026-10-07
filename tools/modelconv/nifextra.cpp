#include "NifFile.hpp"
#include "bhk.hpp"
#include <cstdio>
using namespace nifly;
int main(int, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	auto& h = nif.GetHeader();
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
		if (auto* s = h.GetBlock<NiStringExtraData>(i)) printf("string '%s' = '%s'\n", s->name.get().c_str(), s->stringData.get().c_str());
		if (auto* x = h.GetBlock<BSXFlags>(i)) printf("BSX %u\n", x->integerData);
		if (auto* inv = h.GetBlock<BSInvMarker>(i)) printf("InvMarker rot %u %u %u zoom %.2f\n", inv->rotationX, inv->rotationY, inv->rotationZ, inv->zoom);
		if (auto* r = h.GetBlock<bhkRigidBody>(i)) printf("body layer %u motion %u quality %u mass %.2f\n", r->collisionFilter.layer, r->motionSystem, r->qualityType, r->mass);
	}
	for (auto* sh : nif.GetShapes()) { auto b = sh->GetBounds(); printf("shape %s c(%.1f %.1f %.1f) r %.1f\n", sh->name.get().c_str(), b.center.x, b.center.y, b.center.z, b.radius); }
}
