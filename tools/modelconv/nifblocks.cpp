#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	auto& h = nif.GetHeader();
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
		auto* b = h.GetBlock<NiObject>(i);
		printf("%u %s", i, b->GetBlockName());
		if (auto* ts = dynamic_cast<BSShaderTextureSet*>(b)) printf(" textures=%zu", ts->textures.size());
		if (auto* bs = dynamic_cast<BSDismemberSkinInstance*>(b)) { printf(" parts:"); for (auto& p : bs->partitions) printf(" %d/%d", p.flags, p.partID); }
		printf("\n");
	}
}
