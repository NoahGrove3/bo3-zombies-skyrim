// vibtint <in.nif> [<out.nif> <r g b> [scale] [palette.dds|-]] : (Noah: "for the weapon effects around the swords in hand use the effects mod
// we downloaded for that" - both, layered) a Vibrant Weapons enchantment art turned to one of our swords' colours: every
// particle colour key and every effect shader's base colour moved to that hue (each keeps its own brightness and alpha),
// greyscale palettes swapped for the given one (a flame's own fire ramp for ours; "-" keeps theirs), and particle
// colour keys that are grey (the palette's brightness, not a colour) left as they are; with scale, every root child scaled with its place (our one-handed
// swords are 85% greatswords). Without an out file it only prints what it finds.
#include "NifFile.hpp"
#include "Particles.hpp"
#include <cmath>
#include <cstdio>
#include <string>
using namespace nifly;
static void hue(Color4& c, float r, float g, float b)
{
	const float lum = std::max({ c.r, c.g, c.b });
	c.r = r * lum, c.g = g * lum, c.b = b * lum;
}
int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: vibtint in [out r g b [scale]]\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed %s\n", argv[1]); return 1; }
	auto& h = nif.GetHeader();
	const bool write = argc >= 6;  // (r < 0: their colours kept - only sized)
	const float r = write ? std::stof(argv[3]) : 0, g = write ? std::stof(argv[4]) : 0, b = write ? std::stof(argv[5]) : 0;
	const std::string pal = argc >= 8 ? argv[7] : "-";
	int sh = 0, pc = 0;
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
		if (auto* s = h.GetBlock<BSEffectShaderProperty>(i)) {
			if (!write) printf("shader %u src '%s' grey '%s' base %.2f %.2f %.2f %.2f x%.2f\n", i, s->sourceTexture.get().c_str(), s->greyscaleTexture.get().c_str(), s->baseColor.r, s->baseColor.g, s->baseColor.b, s->baseColor.a, s->baseColorScale);
			else if (r >= 0) {
				if (s->greyscaleTexture.get().empty()) hue(s->baseColor, r, g, b);   // (a plain glow: its colour ours)
				else if (pal != "-") s->greyscaleTexture.get() = pal;                // (a palette's flame: our ramp)
				++sh;
			}
		}
		if (auto* m = h.GetBlock<BSPSysSimpleColorModifier>(i)) {
			if (!write) printf("colours %u: %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f\n", i, m->color1.r, m->color1.g, m->color1.b, m->color1.a, m->color2.r, m->color2.g, m->color2.b, m->color2.a, m->color3.r, m->color3.g, m->color3.b, m->color3.a);
			else if (r >= 0) {
				const auto grey = [](const Color4& c) { return std::abs(c.r - c.g) < 0.02f && std::abs(c.g - c.b) < 0.02f; };
				if (grey(m->color1) && grey(m->color2) && grey(m->color3)) continue;   // (brightness for a palette)
				// the first key hotter (towards white), the last deeper, the middle the colour itself
				hue(m->color1, std::min(1.0f, r * 0.5f + 0.5f), std::min(1.0f, g * 0.5f + 0.5f), std::min(1.0f, b * 0.5f + 0.5f));
				hue(m->color2, r, g, b);
				hue(m->color3, r * 0.7f, g * 0.7f, b * 0.7f);
				++pc;
			}
		}
		if (!write) {
			if (auto* e = h.GetBlock<NiStringsExtraData>(i)) {
				printf("strings %u '%s':", i, e->name.get().c_str());
				for (auto& s : e->stringsData) printf(" '%s'", s.get().c_str());
				printf("\n");
			}
		}
	}
	if (!write) return 0;
	if (argc >= 7) {
		const float f = std::stof(argv[6]);
		for (auto* c : nif.GetChildren<NiAVObject>(nif.GetRootNode(), false)) c->transform.scale *= f, c->transform.translation *= f;
	}
	if (nif.Save(argv[2])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("%s: %d shaders, %d colour ramps -> %.2f %.2f %.2f\n", argv[2], sh, pc, r, g, b);
	return 0;
}
