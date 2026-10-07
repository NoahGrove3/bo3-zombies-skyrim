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
		if (!bsd) continue;
		auto* sp = nif.GetHeader().GetBlock<NiSkinPartition>(bsd->skinPartitionRef);
		printf("%s partVerts %zu shapeVerts %zu\n", sh->name.get().c_str(), sp->vertData.size(), ts->vertData.size());
		double wsum = 0; int zero = 0;
		for (size_t i = 0; i < sp->vertData.size(); ++i) {
			auto& v = sp->vertData[i];
			float s = 0; for (int k = 0; k < 4; ++k) s += v.weights[k];
			wsum += s; if (s < 0.01f) ++zero;
			if (i < 3) printf("  v%zu pos (%.1f %.1f %.1f) w %.2f %.2f %.2f %.2f idx %d %d %d %d\n", i, v.vert.x, v.vert.y, v.vert.z, v.weights[0], v.weights[1], v.weights[2], v.weights[3], v.weightBones[0], v.weightBones[1], v.weightBones[2], v.weightBones[3]);
		}
		printf("  avg weight sum %.3f, zero-weight verts %d\n", sp->vertData.empty() ? 0 : wsum / sp->vertData.size(), zero);
		auto& p = sp->partitions[0];
		printf("  part bones %u vertexMap %zu boneIndices %zu weights %zu\n", p.numBones, p.vertexMap.size(), p.boneIndices.size(), p.vertexWeights.size());
		break;
	}
}
