#include "NifFile.hpp"
#include <cstdio>
#include <cstring>
using namespace nifly;
int main(int argc, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	for (auto* sh : nif.GetShapes()) {
		std::vector<std::string> names; nif.GetShapeBoneList(sh, names);
		auto* bsd = dynamic_cast<BSDismemberSkinInstance*>(nif.GetHeader().GetBlock(dynamic_cast<BSTriShape*>(sh)->SkinInstanceRef()));
		auto* sd = nif.GetHeader().GetBlock<NiSkinData>(bsd->dataRef);
		printf("%s overall t(%.1f %.1f %.1f) s %.2f\n", sh->name.get().c_str(), sd->skinTransform.translation.x, sd->skinTransform.translation.y, sd->skinTransform.translation.z, sd->skinTransform.scale);
		for (size_t i = 0; i < names.size(); ++i)
			for (int k = 2; k < argc; ++k)
				if (names[i] == argv[k]) {
					auto& t = sd->bones[i].boneTransform;
					printf("  %s t(%.1f %.1f %.1f) s %.2f r0(%.2f %.2f %.2f) sph c(%.1f %.1f %.1f) r %.1f\n", argv[k], t.translation.x, t.translation.y, t.translation.z, t.scale, t.rotation[0][0], t.rotation[0][1], t.rotation[0][2], sd->bones[i].bounds.center.x, sd->bones[i].bounds.center.y, sd->bones[i].bounds.center.z, sd->bones[i].bounds.radius);
					auto* node = nif.FindBlockByName<NiNode>(argv[k]);
					if (node) { auto x = node->transform; printf("    node t(%.1f %.1f %.1f) s %.2f\n", x.translation.x, x.translation.y, x.translation.z, x.scale); }
				}
		break;
	}
}
