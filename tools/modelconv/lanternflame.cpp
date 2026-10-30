// lanternflame <beltlantern.nif> <out.nif>: (Noah: "are you able to add an actual flame in the candles on the lantern so it
// looks more realistic?") a flame on each candle of the Belt Lantern: two crossed cards at the wick, lit by Skyrim's own
// candle flame (textures\effects\candleflame01.dds, a 2x2 sheet of flame frames), added light (it glows, never darkens),
// skinned like the candle under it so it rides the lantern's sway. It flickers: the sheet's frames step at two uneven
// rates (U and V offsets) and the flame's brightness wavers.
// Built on the deployed belt lantern (made from the player's own copy of the lantern), so it is deployed, never committed.
#include "NifFile.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>
using namespace nifly;

int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "usage: lanternflame <beltlantern.nif> <out.nif> [height] [width]\n"); return 1; }
	const float H = argc > 3 ? std::stof(argv[3]) : 1.7f, Wd = argc > 4 ? std::stof(argv[4]) : 0.75f;
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	// the candle: the shape with a glow map
	NiShape* candle = nullptr;
	for (auto* s : nif.GetShapes()) {
		std::string g;
		nif.GetTextureSlot(s, g, 2);
		if (!g.empty() && s->name.get().find("Flame") == std::string::npos) candle = s;
	}
	if (!candle) { fprintf(stderr, "no candle (glow-mapped) shape\n"); return 1; }
	const auto verts = *nif.GetVertsForShape(candle);
	std::vector<std::string> bones;
	nif.GetShapeBoneList(candle, bones);
	std::vector<std::unordered_map<uint16_t, float>> bw(bones.size());
	for (uint32_t b = 0; b < bones.size(); ++b) nif.GetShapeBoneWeights(candle, b, bw[b]);
	MatTransform g2s;
	nif.CalcShapeTransformGlobalToSkin(candle, g2s);
	// the wicks: the candle mesh's small separate pieces (the lantern's lit candles each have one), the flame at each's tip
	struct Wick { Vector3 p; int v; };
	std::vector<Wick> wicks;
	{
		std::vector<Triangle> tris;
		candle->GetTriangles(tris);
		std::vector<int> par(verts.size());
		for (size_t i = 0; i < verts.size(); ++i) par[i] = int(i);
		std::function<int(int)> root = [&](int x) { while (par[x] != x) x = par[x] = par[par[x]]; return x; };
		for (auto& tr : tris) par[root(tr.p1)] = root(tr.p2), par[root(tr.p2)] = root(tr.p3);
		for (size_t i = 0; i < verts.size(); ++i)
			for (size_t j = i + 1; j < verts.size(); ++j)
				if ((verts[i] - verts[j]).length() < 0.01f) par[root(int(i))] = root(int(j));
		std::map<int, std::vector<int>> pieces;
		for (size_t i = 0; i < verts.size(); ++i) pieces[root(int(i))].push_back(int(i));
		for (auto& [k, g] : pieces) {
			float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, cx = 0, cy = 0;
			int top = g[0];
			for (int i : g) x0 = std::min(x0, verts[i].x), x1 = std::max(x1, verts[i].x), y0 = std::min(y0, verts[i].y), y1 = std::max(y1, verts[i].y), cx += verts[i].x, cy += verts[i].y, top = verts[i].z > verts[top].z ? i : top;
			if (x1 - x0 > 0.5f || y1 - y0 > 0.5f) continue;  // (a candle's wax, not a wick)
			wicks.push_back({ Vector3(cx / g.size(), cy / g.size(), verts[top].z - 0.12f), top });
			printf("wick at %.2f %.2f %.2f (skin space)\n", wicks.back().p.x, wicks.back().p.y, wicks.back().p.z);
		}
	}
	if (wicks.empty()) { fprintf(stderr, "no wicks\n"); return 1; }
	// the flames: two crossed cards a wick (one cell of the sheet each)
	std::vector<Vector3> v;
	std::vector<Vector2> uv;
	std::vector<Triangle> t;
	std::vector<int> owner;
	for (size_t k = 0; k < wicks.size(); ++k) {
		const auto& w = wicks[k];
		for (int card = 0; card < 2; ++card) {
			const float a = card ? 3.14159265f * 0.5f : 0.0f;
			const Vector3 side(std::cos(a) * Wd * 0.5f, std::sin(a) * Wd * 0.5f, 0);
			const Vector3 base = w.p, top = base + Vector3(0, 0, H);
			const auto b0 = uint16_t(v.size());
			v.push_back(base - side), v.push_back(base + side), v.push_back(top + side), v.push_back(top - side);
			uv.push_back({ 0.0f, 0.5f }), uv.push_back({ 0.5f, 0.5f }), uv.push_back({ 0.5f, 0.0f }), uv.push_back({ 0.0f, 0.0f });
			t.emplace_back(b0, uint16_t(b0 + 1), uint16_t(b0 + 2)), t.emplace_back(b0, uint16_t(b0 + 2), uint16_t(b0 + 3));
			for (int q = 0; q < 4; ++q) owner.push_back(w.v);
		}
	}
	std::vector<Vector3> nrm(v.size(), Vector3(0, -1, 0));
	const std::string name = "BeltLanternFlame";
	NiShape* fl = nif.CreateShapeFromData(name, &v, &t, &uv, &nrm);
	nif.CreateSkinning(fl);
	std::vector<int> ids;
	nif.GetShapeBoneIDList(candle, ids);
	nif.SetShapeBoneIDList(fl, ids);
	for (uint32_t b = 0; b < bones.size(); ++b) {
		MatTransform s2b;
		nif.GetShapeTransformSkinToBone(candle, b, s2b);
		nif.SetShapeTransformSkinToBone(fl, b, s2b);
		std::unordered_map<uint16_t, float> m;
		for (size_t i = 0; i < v.size(); ++i)
			if (auto it = bw[b].find(uint16_t(owner[i])); it != bw[b].end() && it->second > 0) m[uint16_t(i)] = it->second;
		nif.SetShapeBoneWeights(name, b, m);
		Vector3 cc;
		for (auto& p : v) cc += s2b.ApplyTransform(p);
		cc /= float(v.size());
		float rad = 0;
		for (auto& p : v) rad = std::max(rad, (s2b.ApplyTransform(p) - cc).length());
		BoundingSphere bs(cc, rad + 1.0f);
		nif.SetShapeBoneBounds(name, b, bs);
	}
	for (size_t i = 0; i < v.size(); ++i) {
		std::vector<uint8_t> bi;
		std::vector<float> ww;
		for (uint32_t b = 0; b < bones.size(); ++b)
			if (auto it = bw[b].find(uint16_t(owner[i])); it != bw[b].end() && it->second > 0) bi.push_back(uint8_t(b)), ww.push_back(it->second);
		nif.SetShapeVertWeights(name, uint16_t(i), bi, ww);
	}
	nif.UpdateSkinPartitions(fl);
	if (auto* skin = nif.GetHeader().GetBlock<BSDismemberSkinInstance>(fl->SkinInstanceRef()))
		for (auto& pi : skin->partitions) pi.partID = 60;
	// the look: Skyrim's candle flame, added light, both faces, no shadows
	auto& hdr = nif.GetHeader();
	auto es = std::make_unique<BSEffectShaderProperty>();
	es->shaderFlags1 = SLSF1_SKINNED | SLSF1_ZBUFFER_TEST;
	es->shaderFlags2 = SLSF2_DOUBLE_SIDED;
	es->sourceTexture.get() = "textures\\effects\\candleflame01.dds";
	es->baseColor = Color4(1.0f, 0.86f, 0.62f, 1.0f);
	es->baseColorScale = 2.0f;
	es->uvOffset = Vector2(0, 0), es->uvScale = Vector2(1, 1);
	es->textureClampMode = 3;
	es->falloffStartAngle = 1, es->falloffStopAngle = 1, es->falloffStartOpacity = 1, es->falloffStopOpacity = 1;
	const int esId = int(hdr.AddBlock(std::move(es)));
	fl->ShaderPropertyRef()->index = esId;
	auto ap = std::make_unique<NiAlphaProperty>();
	ap->flags = 1 | (6 << 1) | (0 << 5);  // blend: source alpha onto one (added light)
	ap->threshold = 0;
	fl->AlphaPropertyRef()->index = int(hdr.AddBlock(std::move(ap)));
	// the flicker: the sheet's frames (U and V offsets stepping at uneven rates) and the brightness wavering
	auto* esp = hdr.GetBlock<BSEffectShaderProperty>(esId);
	NiBlockRef<NiTimeController> prev;
	auto ctl = [&](uint32_t var, std::vector<std::pair<float, float>> keys, uint32_t interp) {
		auto data = std::make_unique<NiFloatData>();
		data->data.SetInterpolationType(static_cast<NiKeyType>(interp));
		for (auto& [tm, val] : keys) {
			NiAnimationKey<float> key;
			key.time = tm, key.value = val;
			data->data.AddKey(key);
		}
		const int dId = int(hdr.AddBlock(std::move(data)));
		auto ip = std::make_unique<NiFloatInterpolator>();
		ip->floatValue = keys.front().second;
		ip->dataRef.index = dId;
		const int iId = int(hdr.AddBlock(std::move(ip)));
		auto c = std::make_unique<BSEffectShaderPropertyFloatController>();
		c->typeOfControlledVariable = var;
		c->flags = 0x0008;  // active, looping
		c->frequency = 1.0f, c->phase = 0.0f, c->startTime = 0.0f, c->stopTime = keys.back().first;
		c->interpolatorRef.index = iId;
		c->targetRef.index = esId;
		c->nextControllerRef = prev;
		const int cId = int(hdr.AddBlock(std::move(c)));
		prev.index = cId;
	};
	ctl(6, { { 0.0f, 0.0f }, { 0.13f, 0.5f }, { 0.24f, 0.0f }, { 0.41f, 0.5f }, { 0.5f, 0.0f }, { 0.66f, 0.5f }, { 0.77f, 0.0f } }, 5);   // U offset (constant keys)
	ctl(8, { { 0.0f, 0.0f }, { 0.19f, 0.5f }, { 0.35f, 0.0f }, { 0.58f, 0.5f }, { 0.71f, 0.0f }, { 0.93f, 0.5f }, { 1.09f, 0.0f } }, 5);   // V offset
	ctl(0, { { 0.0f, 2.0f }, { 0.21f, 1.7f }, { 0.38f, 2.25f }, { 0.6f, 1.85f }, { 0.83f, 2.15f }, { 1.0f, 2.0f } }, 1);               // brightness (linear)
	esp->controllerRef.index = prev.index;
	if (nif.Save(argv[2])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("wrote %s: %zu flame(s)\n", argv[2], wicks.size());
}
