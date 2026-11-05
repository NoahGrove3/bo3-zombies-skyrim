// swordprep <in.nif> <out.nif> <scale> <prn|-> [static] : (Noah: "change them to use the swords from Light Greatswords ... the
// sword for each class 1hand but using the same swords from the mod") one of Light Greatswords' models made ready for our
// sword: every root child scaled with its place (the class swords are the greatswords at 85%, a long sword in one hand);
// its Prn - where it hangs sheathed - set (WeaponSword: at the hip, one-handed; WeaponBack for the greatswords); with
// "static", its havok body taken off (the Reliquary's floating display copies: a dynamic body would let them fall).
#include "NifFile.hpp"
#include <cstdio>
#include <string>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 5) { fprintf(stderr, "usage: swordprep in out scale prn|- [static]\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed %s\n", argv[1]); return 1; }
	auto& h = nif.GetHeader();
	auto* root = nif.GetRootNode();
	if (!root) { fprintf(stderr, "no root\n"); return 1; }
	const float f = std::stof(argv[3]);
	int k = 0;
	if (f != 1.0f)
		for (auto* c : nif.GetChildren<NiAVObject>(root, false)) c->transform.scale *= f, c->transform.translation *= f, ++k;
	const std::string prn = argv[4];
	if (prn != "-") {
		bool had = false;
		for (uint32_t i = 0; i < h.GetNumBlocks(); ++i)
			if (auto* s = h.GetBlock<NiStringExtraData>(i); s && s->name.get() == "Prn") s->stringData.get() = prn, had = true;
		if (!had) {
			auto s = std::make_unique<NiStringExtraData>();
			s->name.get() = "Prn";
			s->stringData.get() = prn;
			root->extraDataRefs.AddBlockRef(h.AddBlock(std::move(s)));
		}
	}
	const bool still = argc > 5 && std::string(argv[5]) == "static";
	if (still) {
		root->collisionRef.Clear();
		nif.DeleteUnreferencedBlocks();
	}
	if (nif.Save(argv[2])) { fprintf(stderr, "save failed %s\n", argv[2]); return 1; }
	NifFile chk;
	if (chk.Load(argv[2])) { fprintf(stderr, "reload failed %s\n", argv[2]); return 2; }
	printf("%s: %d parts x%.2f, Prn %s%s, %u blocks\n", argv[2], k, f, prn.c_str(), still ? ", no havok" : "", chk.GetHeader().GetNumBlocks());
	return 0;
}
