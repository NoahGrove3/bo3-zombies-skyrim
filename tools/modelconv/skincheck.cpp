#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	for (auto* sh : nif.GetShapes()) {
		auto* ts = dynamic_cast<BSTriShape*>(sh);
		auto* si = nif.GetHeader().GetBlock(ts->SkinInstanceRef());
		auto* bsd = dynamic_cast<BSDismemberSkinInstance*>(si);
		auto* sp = nif.GetHeader().GetBlock<NiSkinPartition>(bsd->skinPartitionRef);
		auto* sd = nif.GetHeader().GetBlock<NiSkinData>(bsd->dataRef);
		printf("%s verts %u dataSize %u vdesc %016llx bones %u skel root %d\n", sh->name.get().c_str(), ts->GetNumVertices(), ts->dataSize, (unsigned long long)ts->vertexDesc.GetFlags(), bsd->boneRefs.GetSize(), bsd->targetRef.index);
		if (sp) {
			printf(" partition: numVerts %u dataSize %u vertexSize %u parts %u\n", sp->numVertices, sp->dataSize, sp->vertexSize, sp->numPartitions);
			for (auto& p : sp->partitions) printf("  part verts %u tris %u bones %u wpv %u\n", p.numVertices, p.numTriangles, p.numBones, p.numWeightsPerVertex);
		}
		if (sd) printf(" skindata bones %zu hasVW %d\n", sd->bones.size(), sd->hasVertWeights);
		std::vector<std::string> names; nif.GetShapeBoneList(sh, names);
		for (size_t i = 0; i < names.size() && i < 6; ++i) printf("  bone %s\n", names[i].c_str());
	}
}
