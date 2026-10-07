#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	for (auto* s : nif.GetShapes()) {
		auto* t = dynamic_cast<BSTriShape*>(s); if (!t) continue;
		std::vector<std::string> bones; nif.GetShapeBoneList(s, bones);
		
		for (size_t i = 0; i < t->vertData.size(); i += 7) {
			auto& v = t->vertData[i];
			printf("%7.2f %7.2f %6.2f :", v.vert.x, v.vert.y, v.vert.z);
			for (int k = 0; k < 4; ++k) if (v.weights[k] > 0.01f) printf(" %s=%.2f", bones[v.weightBones[k]].c_str(), v.weights[k]);
			printf("\n");
		}
	}
}
