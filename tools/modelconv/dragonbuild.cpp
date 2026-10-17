// dragonbuild <glider.nif> <job.txt> <out.nif> [--hidden|--world]: our own glider dragon (tools/dragonbuild.py writes the
// job: Skyrim's dragon mesh on a skeleton square to the dragon's space, with Skyrim's skin weights and a 6 s loop of keys)
// put in place of the paraglider's static dragon. The glider's own rig (PG_*) is kept as it was; the dragon hangs under
//   DG_Space (the old shape's place) > DG_Bank > DG_Body > ... (the job's bones)
// --hidden: DG_Space at no size (the game shows it once it has put it where the swoop begins)
// --world: the dragon on its own for a reference placed in the world (DG_Space at the origin, keeping its scale)
#include "NifFile.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
using namespace nifly;

struct Bone { std::string name, parent; Vector3 pos; };
struct Key { float t; Vector3 p; Quaternion q; };
struct Anim { std::string bone; float len; std::vector<Key> keys; };

int main(int argc, char** argv)
{
	if (argc < 4) { fprintf(stderr, "usage: dragonbuild glider.nif job.txt out.nif [--hidden|--world]\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	auto& hdr = nif.GetHeader();
	// ---- the job
	std::vector<Bone> bones;
	std::vector<std::string> skin;
	struct Mesh {
		std::string name, diffuse, normal;
		std::vector<Vector3> V, N;
		std::vector<Vector2> U;
		std::vector<Color4> C;
		std::vector<Triangle> T;
		std::vector<std::vector<std::pair<int, float>>> VW;
	};
	std::vector<Mesh> meshes;
	std::vector<Anim> anims;
	std::map<int, std::string> tex;
	{
		std::ifstream in(argv[2]);
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream s(line);
			std::string k;
			s >> k;
			if (k == "bone") { Bone b; s >> b.name >> b.parent >> b.pos.x >> b.pos.y >> b.pos.z; if (b.parent == "-") b.parent.clear(); bones.push_back(b); }
			else if (k == "skin") { std::string n; while (s >> n) skin.push_back(n); }
			else if (k == "tex") { int i; std::string p; s >> i >> p; tex[i] = p; }
			else if (k == "mesh") { Mesh m; s >> m.name >> m.diffuse >> m.normal; meshes.push_back(m); }
			else if (k == "v") { auto& m = meshes.back(); Vector3 p, n; Vector2 u; Color4 c; s >> p.x >> p.y >> p.z >> n.x >> n.y >> n.z >> u.u >> u.v >> c.r >> c.g >> c.b >> c.a; m.V.push_back(p); m.N.push_back(n); m.U.push_back(u); m.C.push_back(c); }
			else if (k == "t") { int a, b, c; s >> a >> b >> c; meshes.back().T.emplace_back(a, b, c); }
			else if (k == "vw") { std::vector<std::pair<int, float>> w; int i; float x; while (s >> i >> x) w.push_back({ i, x }); meshes.back().VW.push_back(w); }
			else if (k == "anim") { Anim a; int n; s >> a.bone >> a.len >> n; anims.push_back(a); }
			else if (k == "k") { Key kk; s >> kk.t >> kk.p.x >> kk.p.y >> kk.p.z >> kk.q.w >> kk.q.x >> kk.q.y >> kk.q.z; anims.back().keys.push_back(kk); }
		}
	}
	printf("job: %zu bones, %zu skinned, %zu meshes, %zu loops\n", bones.size(), skin.size(), meshes.size(), anims.size());

	// ---- the glider's dragon shape: its place becomes DG_Space; the shape is rebuilt from the job
	BSTriShape* ts = nullptr;
	for (auto* s : nif.GetShapes()) if (auto* t = dynamic_cast<BSTriShape*>(s)) ts = t;
	if (!ts) { fprintf(stderr, "no BSTriShape\n"); return 1; }
	NiNode* parent = nif.GetParentNode(ts);
	MatTransform shapeXf = ts->GetTransformToParent();
	NiNode* space = nif.AddNode("DG_Space", shapeXf, parent);
	std::map<std::string, NiNode*> node;
	std::map<std::string, Vector3> bpos;
	for (auto& b : bones) bpos[b.name] = b.pos;
	for (auto& b : bones) {
		NiNode* p = b.parent.empty() ? space : node[b.parent];
		MatTransform x;
		x.translation = b.pos - (b.parent.empty() ? Vector3() : bpos[b.parent]);
		node[b.name] = nif.AddNode(b.name, x, p);
	}
	nif.SetParentNode(ts, space);
	ts->SetTransformToParent(MatTransform());
	BSTriShape* first = ts;
	for (size_t mi = 0; mi < meshes.size(); ++mi) {
		auto& m = meshes[mi];
		if (m.V.size() > 65535) { fprintf(stderr, "too many vertices for one shape\n"); return 1; }
		BSTriShape* sh = first;
		if (mi > 0) {  // another material (the harness): a copy of the dragon's shape, retextured
			sh = dynamic_cast<BSTriShape*>(nif.CloneShape(first, m.name));
			nif.SetParentNode(sh, space);
		}
		sh->Create(hdr.GetVersion(), &m.V, &m.T, &m.U, &m.N);
		sh->name.get() = m.name;
		if (mi > 0) {
			std::string none;
			nif.SetTextureSlot(sh, m.diffuse, 0);
			nif.SetTextureSlot(sh, m.normal, 1);
			for (int t = 2; t < 9; ++t) nif.SetTextureSlot(sh, none, t);
			if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(sh))) {
				ls->SetShaderType(BSLSP_DEFAULT);
				ls->shaderFlags1 &= ~((1u << 7) | (1u << 11));          // no env map, no parallax
				ls->shaderFlags2 &= ~((1u << 27) | (1u << 25) | (1u << 4));  // no back lighting / soft lighting / double-sided
				ls->shaderFlags1 |= 1u;                               // specular
				ls->glossiness = 30.0f;
				ls->specularStrength = 0.6f;
			}
		}
		nif.CalcTangentsForShape(sh);
		bool white = true;
		for (auto& c : m.C) white = white && c.r > 0.99f && c.g > 0.99f && c.b > 0.99f && c.a > 0.99f;
		if (!white) nif.SetColorsForShape(sh, m.C);
		// ---- the skin: binds are pure offsets (every bone square to the dragon's space at rest)
		nif.CreateSkinning(sh);
		std::vector<int> ids;
		for (auto& n : skin) ids.push_back(nif.GetBlockID(node[n]));
		nif.SetShapeBoneIDList(sh, ids);
		if (auto* si = hdr.GetBlock<NiSkinInstance>(sh->SkinInstanceRef())) si->targetRef.index = nif.GetBlockID(space);
		const std::string name = sh->name.get();
		std::vector<std::vector<Vector3>> pts(skin.size());
		for (size_t v = 0; v < m.V.size(); ++v)
			for (auto& [i, x] : m.VW[v]) if (x > 0) pts[i].push_back(m.V[v] - bpos[skin[i]]);
		for (uint32_t b = 0; b < skin.size(); ++b) {
			MatTransform bind;
			bind.translation = bpos[skin[b]];
			nif.SetShapeTransformSkinToBone(sh, b, bind.InverseTransform());
			Vector3 c;
			for (auto& p : pts[b]) c += p;
			if (!pts[b].empty()) c /= float(pts[b].size());
			float r = 1;
			for (auto& p : pts[b]) r = std::max(r, (p - c).length());
			BoundingSphere bs(c, r + 2.0f);
			nif.SetShapeBoneBounds(name, b, bs);
		}
		{  // (the skin data's own per-bone weights: the partitions are built from these)
			std::vector<std::unordered_map<uint16_t, float>> wm(skin.size());
			for (size_t v = 0; v < m.V.size(); ++v)
				for (auto& [i, x] : m.VW[v]) wm[i][uint16_t(v)] = x;
			for (uint32_t b = 0; b < skin.size(); ++b) nif.SetShapeBoneWeights(name, b, wm[b]);
		}
		for (size_t v = 0; v < m.V.size(); ++v) {
			std::vector<uint8_t> bid;
			std::vector<float> wt;
			for (auto& [i, x] : m.VW[v]) bid.push_back(uint8_t(i)), wt.push_back(x);
			nif.SetShapeVertWeights(name, uint16_t(v), bid, wt);
		}
		nif.UpdateSkinPartitions(sh);
		if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(sh))) ls->shaderFlags1 |= SLSF1_SKINNED;
		printf("%s: %zu verts, %zu tris\n", name.c_str(), m.V.size(), m.T.size());
	}

	// ---- the loop
	for (auto& a : anims) {
		NiNode* n = node[a.bone];
		if (!n || a.keys.empty()) continue;
		auto data = std::make_unique<NiTransformData>();
		data->rotationType = LINEAR_KEY;
		data->translations.SetInterpolationType(LINEAR_KEY);
		for (auto& k : a.keys) {
			NiAnimationKey<Quaternion> qk;
			qk.type = LINEAR_KEY, qk.time = k.t, qk.value = k.q;
			data->quaternionKeys.push_back(qk);
			NiAnimationKey<Vector3> pk;
			pk.type = LINEAR_KEY, pk.time = k.t, pk.value = k.p;
			data->translations.AddKey(pk);
		}
		const auto dataRef = hdr.AddBlock(std::move(data));
		auto ip = std::make_unique<NiTransformInterpolator>();
		ip->translation = n->GetTransformToParent().translation;
		ip->rotation = Quaternion();
		ip->scale = 1.0f;
		ip->dataRef.index = dataRef;
		const auto ipRef = hdr.AddBlock(std::move(ip));
		auto ctrl = std::make_unique<NiTransformController>();
		ctrl->flags = 0x48;  // active, looping
		ctrl->frequency = 1.0f;
		ctrl->startTime = 0.0f;
		ctrl->stopTime = a.len;
		ctrl->targetRef.index = nif.GetBlockID(n);
		ctrl->interpolatorRef.index = ipRef;
		ctrl->nextControllerRef.index = n->controllerRef.index;
		n->controllerRef.index = hdr.AddBlock(std::move(ctrl));
	}
	if (argc > 4 && std::string(argv[4]) == "--hidden") {
		MatTransform x = space->GetTransformToParent();
		x.scale = 0.0f;
		space->SetTransformToParent(x);
	}
	if (argc > 4 && std::string(argv[4]) == "--world") {
		MatTransform x = space->GetTransformToParent();
		x.translation = Vector3();
		space->SetTransformToParent(x);
	}
	auto* root = nif.GetRootNode();
	for (auto& r : root->extraDataRefs)
		if (auto* b = hdr.GetBlock<BSXFlags>(r)) b->integerData |= 1u;
	nif.Save(argv[3]);
	printf("wrote %s\n", argv[3]);
}
