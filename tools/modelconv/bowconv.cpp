// bowconv <job.txt>: put a new bow mesh on a vanilla bow rig (template NIF keeps its Bow_* bones, behavior graph,
// collision and markers; its shapes are replaced). Python (tools/bows.py) does the placement and the weights.
// Job file:
//   template <vanilla bow nif>
//   out <nif>
//   shape <name> diffuse=<tex> normal=<tex> [glow=<tex> emit=r,g,b emitmult=<f>] [alpha=add] [spec=<f>] [gloss=<f>]
//   v x y z u v nx ny nz b0 w0 [b1 w1 [b2 w2 [b3 w3]]]      (b = index into the bone list below)
//   t a b c
// Bones (fixed order): Bow_MidBone Bow_LoBone1 Bow_LoBone2 Bow_StringBone1 Bow_UpBone1 Bow_UpBone2 Bow_StringBone2
#include "NifFile.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
using namespace nifly;

static const char* kBones[7] = { "Bow_MidBone", "Bow_LoBone1", "Bow_LoBone2", "Bow_StringBone1", "Bow_UpBone1", "Bow_UpBone2", "Bow_StringBone2" };

struct Shape
{
	std::string                            name, diffuse, normal, glow, alpha;
	Vector3                                emit{ 1, 1, 1 };
	float                                  emitMult = 1, spec = 0.4f, gloss = 60;
	std::vector<Vector3>                   v, n;
	std::vector<Vector2>                   uv;
	std::vector<Triangle>                  t;
	std::vector<std::vector<std::pair<int, float>>> w;
};

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: bowconv job.txt\n"); return 1; }
	std::ifstream in(argv[1]);
	std::string   line, tmpl, out;
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
				else if (key == "alpha") s.alpha = val;
				else if (key == "emit") sscanf(val.c_str(), "%f,%f,%f", &s.emit.x, &s.emit.y, &s.emit.z);
				else if (key == "emitmult") s.emitMult = std::stof(val);
				else if (key == "spec") s.spec = std::stof(val);
				else if (key == "gloss") s.gloss = std::stof(val);
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
	for (auto* s : nif.GetShapes()) nif.DeleteShape(s);
	int boneId[7];
	MatTransform boneGlobal[7];
	for (int i = 0; i < 7; ++i) {
		auto* node = nif.FindBlockByName<NiNode>(kBones[i]);
		if (!node) { fprintf(stderr, "template has no %s\n", kBones[i]); return 1; }
		boneId[i] = nif.GetBlockID(node);
		nif.GetNodeTransformToGlobal(kBones[i], boneGlobal[i]);
	}
	for (auto& s : shapes) {
		NiShape* sh = nif.CreateShapeFromData(s.name, &s.v, &s.t, &s.uv, &s.n);
		nif.CreateSkinning(sh);
		std::vector<int> ids(boneId, boneId + 7);
		nif.SetShapeBoneIDList(sh, ids);
		for (int bi = 0; bi < 7; ++bi) {
			const MatTransform toBone = boneGlobal[bi].InverseTransform();
			nif.SetShapeTransformSkinToBone(sh, bi, toBone);
			std::unordered_map<uint16_t, float> wm;
			for (uint32_t v = 0; v < s.w.size(); ++v)
				for (auto& [b, x] : s.w[v]) if (b == bi && x > 0) wm[uint16_t(v)] = x;
			nif.SetShapeBoneWeights(s.name, bi, wm);
			std::vector<Vector3> pts;
			for (auto& [v, x] : wm) pts.push_back(toBone.ApplyTransform(s.v[v]));
			if (pts.empty()) pts.push_back(Vector3());
			Vector3 c; for (auto& p : pts) c += p; c /= float(pts.size());
			float r = 0; for (auto& p : pts) r = std::max(r, (p - c).length());
			BoundingSphere bs(c, r + 1.0f);
			nif.SetShapeBoneBounds(s.name, bi, bs);
		}
		for (uint32_t v = 0; v < s.w.size(); ++v) {
			std::vector<uint8_t> bids; std::vector<float> wts;
			for (auto& [b, x] : s.w[v]) if (x > 0 && bids.size() < 4) bids.push_back(uint8_t(b)), wts.push_back(x);
			nif.SetShapeVertWeights(s.name, v, bids, wts);
		}
		nif.UpdateSkinPartitions(sh);
		nif.CalcTangentsForShape(sh);
		if (!s.diffuse.empty()) nif.SetTextureSlot(sh, s.diffuse, 0);
		if (!s.normal.empty()) nif.SetTextureSlot(sh, s.normal, 1);
		if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(sh))) {
			ls->shaderFlags1 = SLSF1_SPECULAR | SLSF1_SKINNED | SLSF1_RECEIVE_SHADOWS | SLSF1_CAST_SHADOWS | SLSF1_REMAPPABLE_TEXTURES | SLSF1_ZBUFFER_TEST;
			ls->shaderFlags2 = SLSF2_ZBUFFER_WRITE | SLSF2_DOUBLE_SIDED;
			ls->specularStrength = s.spec, ls->glossiness = s.gloss;
			if (!s.glow.empty()) {
				nif.SetTextureSlot(sh, s.glow, 2);
				ls->SetShaderType(BSLSP_GLOWMAP);
				ls->shaderFlags2 |= SLSF2_GLOW_MAP;
				ls->emissiveColor = s.emit, ls->emissiveMultiple = s.emitMult;
			}
		}
		if (s.alpha == "add") {
			auto a = std::make_unique<NiAlphaProperty>();
			a->flags = 0x000D;
			a->threshold = 96;
			if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(sh))) ls->shaderFlags2 &= ~SLSF2_ZBUFFER_WRITE;
			nif.AssignAlphaProperty(sh, std::move(a));
		}
		printf("shape %s: %zu verts %zu tris\n", s.name.c_str(), s.v.size(), s.t.size());
	}
	if (nif.Save(out)) { fprintf(stderr, "save failed\n"); return 1; }
	printf("wrote %s\n", out.c_str());
}
