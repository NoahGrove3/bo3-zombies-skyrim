#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <algorithm>
#define private public
#define protected public
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	for (auto* sh : nif.GetShapes()) {
		auto* ts = dynamic_cast<BSTriShape*>(sh);
		auto* bsd = dynamic_cast<BSDismemberSkinInstance*>(nif.GetHeader().GetBlock(ts->SkinInstanceRef()));
		auto* sp = nif.GetHeader().GetBlock<NiSkinPartition>(bsd->skinPartitionRef);
		printf("%s desc %016llx flags %x size %u | part desc %016llx | avflags %u | bound c(%.1f %.1f %.1f) r %.1f\n", sh->name.get().c_str(), (unsigned long long)ts->vertexDesc.desc, (unsigned)ts->vertexDesc.GetFlags(), 0u, (unsigned long long)sp->vertexDesc.desc, ts->flags, ts->bounds.center.x, ts->bounds.center.y, ts->bounds.center.z, ts->bounds.radius);
		auto& p = sp->partitions[0];
		printf("   part0 vdesc %016llx hasVertMap %d hasWeights %d hasBoneIdx %d strips %u tris %zu trueTris %zu\n", (unsigned long long)p.vertexDesc.desc, p.hasVertexMap, p.hasVertexWeights, p.hasBoneIndices, p.numStrips, p.triangles.size(), p.trueTriangles.size());
		printf("   root flags %u name %s\n", nif.GetRootNode()->flags, nif.GetRootNode()->name.get().c_str());
	}
}
