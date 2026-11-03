// addprn <in.nif> <out.nif> <prn node> : a held model given what a weapon needs to be hung on the body - the Prn extra data
// (the sheath node, e.g. WeaponDagger; without it Skyrim never draws the weapon in the hand) and a BSInvMarker - on its root
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 4) { fprintf(stderr, "addprn <in.nif> <out.nif> <prn>\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	auto& h = nif.GetHeader();
	auto* root = nif.GetRootNode();
	bool had = false;
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i)
		if (auto* s = h.GetBlock<NiStringExtraData>(i); s && s->name.get() == "Prn") s->stringData.get() = argv[3], had = true;
	if (!had) {
		auto s = std::make_unique<NiStringExtraData>();
		s->name.get() = "Prn";
		s->stringData.get() = argv[3];
		root->extraDataRefs.AddBlockRef(h.AddBlock(std::move(s)));
		auto inv = std::make_unique<BSInvMarker>();
		inv->name.get() = "INV";
		inv->rotationX = 4712, inv->rotationY = 6283, inv->rotationZ = 0, inv->zoom = 1.0f;
		root->extraDataRefs.AddBlockRef(h.AddBlock(std::move(inv)));
	}
	// (Noah: the flasks stood at odd angles in the inventory and the hand) a held model keeps no spin: its turning node's
	// transform controller (made for the flying copy, run by the game's clock) is taken off, the node left upright
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
		auto* n = h.GetBlock<NiNode>(i);
		if (!n || n->controllerRef.IsEmpty()) continue;
		if (h.GetBlock<NiTransformController>(n->controllerRef)) n->controllerRef.Clear();
	}
	nif.DeleteUnreferencedBlocks();
	if (nif.Save(argv[2])) { fprintf(stderr, "cannot save %s\n", argv[2]); return 1; }
	printf("%s: Prn %s\n", argv[2], argv[3]);
	return 0;
}
