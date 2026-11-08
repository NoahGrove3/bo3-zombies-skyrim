// skinglove <job.txt>: a skinned armour piece written into a copy of a Skyrim body-part NIF (the template: e.g. the
// hands), on the template's own bones, bind transforms and dismember partition - its own shape(s) replaced by ours.
// (the Tablet of Mysteries' fist weapons: gloves that bend with the fingers - tools/tabletweapons.py)
// Job file:
//   template <nif>          (one skinned shape: its bones, skin-to-bone transforms and partition are kept)
//   out <nif>
//   shape <name> diffuse=<tex> normal=<tex> [glow=<tex> emit=r,g,b emitmult=<f>] [spec=<f>] [gloss=<f>] [double=1]
//   v x y z u v nx ny nz b0 w0 [b1 w1 [b2 w2 [b3 w3]]]      (positions in the template's skin space; b = the
//                                                            template shape's bone list index)
//   t a b c
#include "NifFile.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
using namespace nifly;

struct Shape
{
	std::string name, diffuse, normal, glow;
	Vector3     emit{ 1, 1, 1 };
	float       emitMult = 1, spec = 0.4f, gloss = 40;
	bool        dbl = false;
	std::vector<Vector3> v, n;
	std::vector<Vector2> uv;
	std::vector<Triangle> t;
	std::vector<std::vector<std::pair<int, float>>> w;
};

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: skinglove job.txt\n"); return 1; }
	std::ifstream in(argv[1]);
	std::string line, tmpl, out;
	std::vector<Shape> shapes;
	while (std::getline(in, line)) {
		std::istringstream ss(line);
		std::string k;
		ss >> k;
		if (k == "template") std::getline(ss >> std::ws, tmpl);
		else if (k == "out") std::getline(ss >> std::ws, out);
		else if (k == "shape") {
			shapes.emplace_back();
			auto& s = shapes.back();
			ss >> s.name;
			std::string kv;
			while (ss >> kv) {
				auto eq = kv.find('=');
				if (eq == std::string::npos) continue;
				auto key = kv.substr(0, eq), val = kv.substr(eq + 1);
				if (key == "diffuse") s.diffuse = val;
				else if (key == "normal") s.normal = val;
				else if (key == "glow") s.glow = val;
				else if (key == "emit") sscanf(val.c_str(), "%f,%f,%f", &s.emit.x, &s.emit.y, &s.emit.z);
				else if (key == "emitmult") s.emitMult = std::stof(val);
				else if (key == "spec") s.spec = std::stof(val);
				else if (key == "gloss") s.gloss = std::stof(val);
				else if (key == "double") s.dbl = val == "1";
			}
		} else if (k == "v" && !shapes.empty()) {
			auto& s = shapes.back();
			float x, y, z, u, vv, nx, ny, nz;
			ss >> x >> y >> z >> u >> vv >> nx >> ny >> nz;
			s.v.emplace_back(x, y, z), s.uv.emplace_back(u, vv), s.n.emplace_back(nx, ny, nz);
			std::vector<std::pair<int, float>> ws;
			int b; float wt;
			while (ss >> b >> wt) ws.push_back({ b, wt });
			s.w.push_back(ws);
		} else if (k == "t" && !shapes.empty()) {
			int a, b, c; ss >> a >> b >> c;
			shapes.back().t.emplace_back(a, b, c);
		}
	}
	NifFile nif;
	if (nif.Load(tmpl)) { fprintf(stderr, "cannot load %s\n", tmpl.c_str()); return 1; }
	auto tshapes = nif.GetShapes();
	if (tshapes.empty()) { fprintf(stderr, "template has no shape\n"); return 1; }
	NiShape* src = tshapes[0];
	std::vector<std::string> boneNames;
	nif.GetShapeBoneList(src, boneNames);
	std::vector<int> boneIds;
	for (auto& b : boneNames) {
		auto* node = nif.FindBlockByName<NiNode>(b);
		boneIds.push_back(node ? int(nif.GetBlockID(node)) : -1);
	}
	std::vector<MatTransform> s2b(boneNames.size());
	for (uint32_t i = 0; i < boneNames.size(); ++i) nif.GetShapeTransformSkinToBone(src, i, s2b[i]);
	NiVector<BSDismemberSkinInstance::PartitionInfo> parts;
	std::vector<int> triParts;
	nif.GetShapePartitions(src, parts, triParts);
	if (parts.size() > 1) parts.resize(1);
	for (auto& s : shapes) {
		NiShape* sh = nif.CreateShapeFromData(s.name, &s.v, &s.t, &s.uv, &s.n);
		nif.CreateSkinning(sh);
		nif.SetShapeBoneIDList(sh, boneIds);
		for (uint32_t bi = 0; bi < boneNames.size(); ++bi) {
			nif.SetShapeTransformSkinToBone(sh, bi, s2b[bi]);
			std::vector<Vector3> pts;
			for (uint32_t v = 0; v < s.w.size(); ++v)
				for (auto& [b, x] : s.w[v]) if (b == int(bi) && x > 0) pts.push_back(s2b[bi].ApplyTransform(s.v[v]));
			if (pts.empty()) pts.push_back(Vector3());
			Vector3 c; for (auto& p : pts) c += p; c /= float(pts.size());
			float r = 0; for (auto& p : pts) r = std::max(r, (p - c).length());
			BoundingSphere bs(c, r + 1.0f);
			nif.SetShapeBoneBounds(s.name, bi, bs);
		}
		for (uint32_t v = 0; v < s.w.size(); ++v) {
			std::vector<uint8_t> bids; std::vector<float> wts;
			float tot = 0;
			for (auto& [b, x] : s.w[v]) if (x > 0 && bids.size() < 4) bids.push_back(uint8_t(b)), wts.push_back(x), tot += x;
			for (auto& x : wts) x /= (tot > 0 ? tot : 1);
			nif.SetShapeVertWeights(s.name, v, bids, wts);
		}
		if (!parts.empty()) {
			std::vector<int> tp(s.t.size(), 0);
			nif.SetShapePartitions(sh, parts, tp, true);
		} else {
			nif.SetDefaultPartition(sh);
		}
		nif.UpdateSkinPartitions(sh);
		nif.CalcTangentsForShape(sh);
		if (!s.diffuse.empty()) nif.SetTextureSlot(sh, s.diffuse, 0);
		if (!s.normal.empty()) nif.SetTextureSlot(sh, s.normal, 1);
		if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(sh))) {
			ls->shaderFlags1 = SLSF1_SPECULAR | SLSF1_SKINNED | SLSF1_RECEIVE_SHADOWS | SLSF1_CAST_SHADOWS | SLSF1_REMAPPABLE_TEXTURES | SLSF1_ZBUFFER_TEST;
			ls->shaderFlags2 = SLSF2_ZBUFFER_WRITE | (s.dbl ? SLSF2_DOUBLE_SIDED : 0);
			ls->specularStrength = s.spec, ls->glossiness = s.gloss;
			if (!s.glow.empty()) {
				nif.SetTextureSlot(sh, s.glow, 2);
				ls->SetShaderType(BSLSP_GLOWMAP);
				ls->shaderFlags2 |= SLSF2_GLOW_MAP;
				ls->emissiveColor = s.emit, ls->emissiveMultiple = s.emitMult;
			}
		}
		printf("shape %s: %zu verts %zu tris\n", s.name.c_str(), s.v.size(), s.t.size());
	}
	nif.DeleteShape(src);
	if (nif.Save(out)) { fprintf(stderr, "save failed\n"); return 1; }
	printf("wrote %s\n", out.c_str());
}
