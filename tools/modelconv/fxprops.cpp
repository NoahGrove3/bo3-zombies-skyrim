// fxprops <nif>: every BSEffectShaderProperty (textures, flags, colours, uv) and its float controllers' keys
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	auto& h = nif.GetHeader();
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
		if (auto* p = h.GetBlock<BSEffectShaderProperty>(i)) {
			printf("[%u] effect src '%s' grey '%s' flags1 %08x flags2 %08x col(%.2f %.2f %.2f %.2f)x%.2f uvoff(%.2f %.2f) uvscale(%.2f %.2f) falloff %.2f %.2f %.2f %.2f soft %.1f clamp %u\n", i,
				p->sourceTexture.get().c_str(), p->greyscaleTexture.get().c_str(), p->shaderFlags1, p->shaderFlags2,
				p->baseColor.r, p->baseColor.g, p->baseColor.b, p->baseColor.a, p->baseColorScale, p->uvOffset.u, p->uvOffset.v, p->uvScale.u, p->uvScale.v,
				p->falloffStartAngle, p->falloffStopAngle, p->falloffStartOpacity, p->falloffStopOpacity, p->softFalloffDepth, p->textureClampMode);
		}
		if (auto* c = h.GetBlock<BSEffectShaderPropertyFloatController>(i)) {
			printf("[%u] ctrl var %u freq %.2f phase %.2f start %.2f stop %.2f flags %u interp %d\n", i, c->typeOfControlledVariable, c->frequency, c->phase, c->startTime, c->stopTime, c->flags, c->interpolatorRef.index);
		}
		if (auto* d = h.GetBlock<NiFloatData>(i)) {
			printf("[%u] floatdata keys %u:", i, d->data.GetNumKeys());
			for (uint32_t k = 0; k < d->data.GetNumKeys(); ++k) { auto key = d->data.GetKey(k); printf(" (%.2f,%.3f)", key.time, key.value); }
			printf("\n");
		}
		if (auto* a = h.GetBlock<NiAlphaProperty>(i)) printf("[%u] alpha flags %04x thr %u\n", i, a->flags, a->threshold);
	}
}
