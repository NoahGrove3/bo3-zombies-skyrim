// beltlantern <skeleton.nif> <lantern.nif> <out.nif>: (Noah: "a Skyrim-looking smaller lantern attached by a chain from
// your body at your hip") the Belt Lantern's worn model. Skyrim's candle lantern (whatever version the game has - SMIM's on
// Noah's) made small, hung on a short chain of iron links from the belt at the left hip, skinned to the pelvis (the chain's
// top) and partly to the left thigh (the lantern sways a little as you walk). The candle keeps its glow map.
// The output is built on the player's machine's own copy of the lantern, so it is deployed, never committed.
#include "NifFile.hpp"
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace nifly;

struct Part
{
	std::string name, tex[3];
	std::vector<Vector3> v, n;
	std::vector<Vector2> uv;
	std::vector<Triangle> t;
	std::vector<float> wThigh;  // per vertex: how much the thigh carries it (the rest the pelvis)
	bool glow = false, chain = false;
};

int main(int argc, char** argv)
{
	if (argc < 4) { fprintf(stderr, "usage: beltlantern <skeleton.nif> <lantern.nif> <out.nif> [scale] [side x] [fwd y] [drop z]\n"); return 1; }
	const float scale = argc > 4 ? std::stof(argv[4]) : 0.36f;
	NifFile sk;
	if (sk.Load(argv[1])) { fprintf(stderr, "skeleton load failed\n"); return 1; }
	const std::string kPelvis = "NPC Pelvis [Pelv]", kThigh = "NPC L Thigh [LThg]";
	MatTransform pelvis, thigh;
	if (!sk.GetNodeTransformToGlobal(kPelvis, pelvis) || !sk.GetNodeTransformToGlobal(kThigh, thigh)) { fprintf(stderr, "bones missing\n"); return 1; }
	printf("pelvis at %.2f %.2f %.2f, left thigh at %.2f %.2f %.2f\n", pelvis.translation.x, pelvis.translation.y, pelvis.translation.z, thigh.translation.x, thigh.translation.y, thigh.translation.z);
	const float side = thigh.translation.x < 0 ? -1.0f : 1.0f;  // (which way is left on this skeleton)
	// the belt's anchor: outside the left hip at the belt line, a little forward of the thigh
	// (the belt rides 5-6 units above the pelvis bone, its outside 16-17 units from the middle; the hip bulges out to about 16
	// below it, so the lantern hangs a little further out, its chain slanting down from the belt to it)
	const float ax = argc > 5 ? std::stof(argv[5]) : 16.5f, lx = argc > 6 ? std::stof(argv[6]) : 20.0f, az = argc > 7 ? std::stof(argv[7]) : 5.5f;
	const Vector3 anchor(side * ax, 1.5f, pelvis.translation.z + az);

	NifFile src;
	if (src.Load(argv[2])) { fprintf(stderr, "lantern load failed\n"); return 1; }
	std::vector<Part> parts;
	float topZ = -1e9f, botZ = 1e9f;
	for (auto* sh : src.GetShapes()) {
		Part p;
		p.name = sh->name.get();
		const auto xf = sh->GetTransformToParent();
		const auto* v = src.GetVertsForShape(sh);
		const auto* n = src.GetNormalsForShape(sh);
		const auto* uv = src.GetUvsForShape(sh);
		if (!v || !uv) continue;
		for (std::size_t i = 0; i < v->size(); ++i) {
			const Vector3 w = xf.ApplyTransform((*v)[i]);
			p.v.push_back(w), topZ = std::max(topZ, w.z), botZ = std::min(botZ, w.z);
			p.n.push_back(n && i < n->size() ? xf.ApplyTransformToDir((*n)[i]) : Vector3(0, 0, 1));
		}
		p.uv = *uv;
		sh->GetTriangles(p.t);
		for (int k = 0; k < 3; ++k) src.GetTextureSlot(sh, p.tex[k], k);
		p.glow = !p.tex[2].empty();
		parts.push_back(p);
	}
	if (parts.empty()) { fprintf(stderr, "no shapes\n"); return 1; }
	printf("lantern %.1f tall\n", topZ - botZ);
	// the chain: from the anchor down to the lantern's top ring, links of iron (each a little torus, turned 90 degrees from the last)
	const float chainLen = 7.0f;
	const Vector3 lanternTop(side * lx, anchor.y, anchor.z - chainLen);
	for (auto& p : parts) {
		for (auto& v : p.v) v = Vector3(v.x * scale, v.y * scale, (v.z - topZ) * scale) + lanternTop;
		p.wThigh.assign(p.v.size(), 0.3f);
	}
	// the chain's look: the lantern's own iron, from the uv of its highest point (the ring it hangs by)
	Part* body = &parts[0];
	for (auto& p : parts) if (p.v.size() > body->v.size()) body = &p;
	std::size_t top = 0;
	for (std::size_t i = 0; i < body->v.size(); ++i) if (body->v[i].z > body->v[top].z) top = i;
	const Vector2 ironUv = body->uv[top];
	Part c;
	c.name = "BeltLanternChain";
	for (int k = 0; k < 3; ++k) c.tex[k] = body->tex[k];
	c.tex[2].clear();
	c.chain = true;
	const int links = 5, segU = 10, segV = 6;
	const float R = chainLen / links * 0.62f, r = 0.24f;
	for (int l = 0; l < links; ++l) {
		const float t = (l + 0.5f) / links;
		const Vector3 ctr = anchor + (lanternTop - anchor) * t;
		const bool turned = l % 2;
		const auto base = static_cast<uint16_t>(c.v.size());
		for (int i = 0; i < segU; ++i) {
			const float a = 2.0f * 3.14159265f * i / segU;
			for (int j = 0; j < segV; ++j) {
				const float b = 2.0f * 3.14159265f * j / segV;
				// a link stretched along z (an oval), in the xz plane or, turned, the yz plane
				const float ox = std::cos(a) * R * 0.6f, oz = std::sin(a) * R;
				const Vector3 ring = turned ? Vector3(0, ox, oz) : Vector3(ox, 0, oz);
				Vector3 out = ring;
				const float len = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
				out = Vector3(out.x / len, out.y / len, out.z / len);
				const Vector3 axis = turned ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
				const Vector3 nrm = out * std::cos(b) + axis * std::sin(b);
				c.v.push_back(ctr + ring + nrm * r);
				c.n.push_back(nrm);
				c.uv.push_back(Vector2(ironUv.u + 0.004f * std::cos(a), ironUv.v + 0.004f * std::sin(b)));
				c.wThigh.push_back(0.3f * t);  // (the top link the belt's alone, the lower ones swing with the lantern)
			}
		}
		for (int i = 0; i < segU; ++i)
			for (int j = 0; j < segV; ++j) {
				const uint16_t a0 = base + i * segV + j, a1 = base + ((i + 1) % segU) * segV + j, b0 = base + i * segV + (j + 1) % segV, b1 = base + ((i + 1) % segU) * segV + (j + 1) % segV;
				c.t.emplace_back(a0, a1, b1), c.t.emplace_back(a0, b1, b0);
			}
	}
	parts.push_back(c);

	// ---- write it, skinned
	NifFile nif;
	nif.Create(NiVersion::getSSE());
	nif.GetRootNode()->name.get() = "Scene Root";
	const std::string bones[2] = { kPelvis, kThigh };
	const MatTransform bind[2] = { pelvis, thigh };
	int nodeId[2];
	for (int b = 0; b < 2; ++b) nodeId[b] = nif.GetBlockID(nif.AddNode(bones[b], bind[b], nif.GetRootNode()));
	int si = 0;
	for (auto& p : parts) {
		const std::string name = p.name + ":" + std::to_string(si++);
		NiShape* sh = nif.CreateShapeFromData(name, &p.v, &p.t, &p.uv, &p.n);
		nif.CreateSkinning(sh);
		std::vector<int> ids{ nodeId[0], nodeId[1] };
		nif.SetShapeBoneIDList(sh, ids);
		for (uint32_t b = 0; b < 2; ++b) {
			nif.SetShapeTransformSkinToBone(sh, b, bind[b].InverseTransform());
			std::unordered_map<uint16_t, float> wm;
			for (uint32_t v = 0; v < p.v.size(); ++v) wm[uint16_t(v)] = b == 0 ? 1.0f - p.wThigh[v] : p.wThigh[v];
			nif.SetShapeBoneWeights(name, b, wm);
			const MatTransform toBone = bind[b].InverseTransform();
			Vector3 cc;
			for (auto& v : p.v) cc += toBone.ApplyTransform(v);
			cc /= float(p.v.size());
			float rad = 0;
			for (auto& v : p.v) rad = std::max(rad, (toBone.ApplyTransform(v) - cc).length());
			BoundingSphere bs(cc, rad + 1.0f);
			nif.SetShapeBoneBounds(name, b, bs);
		}
		for (uint32_t v = 0; v < p.v.size(); ++v) {
			const float wt = p.wThigh[v];
			std::vector<uint8_t> bi{ 0 };
			std::vector<float>   bw{ 1.0f };
			if (wt > 0) bi = { 0, 1 }, bw = { 1.0f - wt, wt };
			nif.SetShapeVertWeights(name, v, bi, bw);
		}
		nif.UpdateSkinPartitions(sh);
		// (an armour addon's pieces sit in the partition of their slot: 60)
		if (auto* skin = nif.GetHeader().GetBlock<BSDismemberSkinInstance>(sh->SkinInstanceRef())) {
			for (auto& pi : skin->partitions) pi.partID = 60;
		}
		nif.CalcTangentsForShape(sh);
		for (int k = 0; k < 3; ++k) if (!p.tex[k].empty()) nif.SetTextureSlot(sh, p.tex[k], k);
		if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(sh))) {
			ls->shaderFlags1 = SLSF1_SPECULAR | SLSF1_SKINNED | SLSF1_RECEIVE_SHADOWS | SLSF1_CAST_SHADOWS | SLSF1_REMAPPABLE_TEXTURES | SLSF1_ZBUFFER_TEST;
			ls->shaderFlags2 = SLSF2_ZBUFFER_WRITE;
			ls->specularStrength = p.chain ? 0.6f : 0.4f, ls->glossiness = p.chain ? 40.0f : 25.0f;
			if (p.glow) {  // (the candle: its flame and wax lit from within)
				ls->SetShaderType(BSLSP_GLOWMAP);
				ls->shaderFlags2 |= SLSF2_GLOW_MAP;
				ls->emissiveColor = Vector3(1.0f, 0.78f, 0.5f), ls->emissiveMultiple = 1.6f;
			}
		}
		printf("shape %s: %zu verts, %zu tris\n", name.c_str(), p.v.size(), p.t.size());
	}
	if (nif.Save(argv[3])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("wrote %s\n", argv[3]);
}
