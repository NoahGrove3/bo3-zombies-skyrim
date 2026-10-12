// skinconv <job.txt>: re-rig a skinned FBX character onto a Skyrim skeleton and write a skinned SE NIF.
// The mesh is re-posed into the target skeleton's bind pose bone by bone (each bone: move its joint onto the
// target joint, turn it to the target bone's direction and stretch it to the target length), weights are
// moved to the mapped target bones, and the shape is skinned with the target bind transforms. Skyrim's own
// animations for that skeleton then drive it.
// Job directives:
//   in <fbx>   out <nif>   skeleton <skeleton.nif>   scale <extra uniform scale, default 1>
//   map <srcBone> <tgtBone>        (repeat; first src mapped to a tgt bone is its "primary" joint)
//   aim <tgtBone> <tgtChildBone>   (bone direction for re-posing; bones without aim keep their parent's turn)
//   mat <name|*> diffuse=.. normal=.. glow=.. emit=r,g,b emitmult=f spec=f gloss=f doublesided=0|1
//   skip <meshName>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include "NifFile.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
using namespace nifly;

struct V3
{
	double x = 0, y = 0, z = 0;
	V3 operator+(const V3& o) const { return { x + o.x, y + o.y, z + o.z }; }
	V3 operator-(const V3& o) const { return { x - o.x, y - o.y, z - o.z }; }
	V3 operator*(double s) const { return { x * s, y * s, z * s }; }
	double dot(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
	V3 cross(const V3& o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
	double len() const { return std::sqrt(dot(*this)); }
	V3 unit() const { double l = len(); return l > 1e-9 ? *this * (1 / l) : V3{ 0, 0, 1 }; }
};
struct M3
{
	double m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	V3 operator*(const V3& v) const { return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z, m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z }; }
	M3 operator*(const M3& o) const { M3 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { r.m[i][j] = 0; for (int k = 0; k < 3; ++k) r.m[i][j] += m[i][k] * o.m[k][j]; } return r; }
};
// rotation taking unit a onto unit b (minimal)
M3 RotBetween(V3 a, V3 b)
{
	a = a.unit(), b = b.unit();
	V3     v = a.cross(b);
	double c = a.dot(b), s = v.len();
	M3     r;
	if (s < 1e-9) {
		if (c > 0) return r;
		V3 axis = std::abs(a.x) < 0.9 ? V3{ 1, 0, 0 }.cross(a).unit() : V3{ 0, 1, 0 }.cross(a).unit();
		for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = 2 * (&axis.x)[i] * (&axis.x)[j] - (i == j ? 1 : 0);
		return r;
	}
	double k[3][3] = { { 0, -v.z, v.y }, { v.z, 0, -v.x }, { -v.y, v.x, 0 } };
	double f = (1 - c) / (s * s);
	for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
		double kk = 0;
		for (int t = 0; t < 3; ++t) kk += k[i][t] * k[t][j];
		r.m[i][j] = (i == j ? 1 : 0) + k[i][j] + f * kk;
	}
	return r;
}
// stretch by factor l along unit axis d
M3 StretchAlong(V3 d, double l)
{
	d = d.unit();
	M3 r;
	for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = (i == j ? 1 : 0) + (l - 1) * (&d.x)[i] * (&d.x)[j];
	return r;
}

struct Mat
{
	std::string diffuse, normal, glow;
	bool        doubleSided = false;
	Vector3     emit{ 1, 1, 1 };
	float       emitMult = 1, spec = 0.2f, gloss = 30;
};

int main(int argc, char** argv)
{
	std::ifstream jf(argv[1]);
	std::string   in, out, skel, line;
	double        extraScale = 1;
	std::vector<std::pair<std::string, std::string>> maps;
	std::map<std::string, std::string> aims;
	std::map<std::string, Mat>         mats;
	std::set<std::string>              skip;
	while (std::getline(jf, line)) {
		std::istringstream ss(line);
		std::string        k;
		ss >> k;
		if (k == "in") std::getline(ss >> std::ws, in);
		else if (k == "out") std::getline(ss >> std::ws, out);
		else if (k == "skeleton") std::getline(ss >> std::ws, skel);
		else if (k == "scale") ss >> extraScale;
		else if (k == "skip") { std::string n; std::getline(ss >> std::ws, n); skip.insert(n); }
		else if (k == "map" || k == "aim") {
			// names may contain spaces: "a|b"
			std::string rest; std::getline(ss >> std::ws, rest);
			auto bar = rest.find('|');
			auto a = rest.substr(0, bar), b = rest.substr(bar + 1);
			if (k == "map") maps.emplace_back(a, b); else aims[a] = b;
		} else if (k == "mat") {
			std::string name, kv; ss >> name; Mat m;
			while (ss >> kv) {
				auto eq = kv.find('='); auto key = kv.substr(0, eq), val = kv.substr(eq + 1);
				if (key == "diffuse") m.diffuse = val; else if (key == "normal") m.normal = val; else if (key == "glow") m.glow = val;
				else if (key == "doublesided") m.doubleSided = val == "1";
				else if (key == "emit") sscanf(val.c_str(), "%f,%f,%f", &m.emit.x, &m.emit.y, &m.emit.z);
				else if (key == "emitmult") m.emitMult = std::stof(val); else if (key == "spec") m.spec = std::stof(val); else if (key == "gloss") m.gloss = std::stof(val);
			}
			mats[name] = m;
		}
	}

	// ---- target skeleton
	NifFile sk;
	if (sk.Load(skel)) { fprintf(stderr, "skeleton load failed\n"); return 1; }
	std::map<std::string, MatTransform> tgtBind;
	std::map<std::string, std::string>  tgtParent;
	for (auto* n : sk.GetNodes()) {
		MatTransform g; sk.GetNodeTransformToGlobal(n->name.get(), g);
		tgtBind[n->name.get()] = g;
		auto* p = sk.GetParentNode(n);
		tgtParent[n->name.get()] = p ? p->name.get() : "";
	}
	auto T = [&](const std::string& b) { auto t = tgtBind.at(b).translation; return V3{ t.x, t.y, t.z }; };

	// ---- source
	Assimp::Importer imp;
	imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	const aiScene* sc = imp.ReadFile(in, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals | aiProcess_LimitBoneWeights | aiProcess_FlipUVs);
	if (!sc) { fprintf(stderr, "load failed: %s\n", imp.GetErrorString()); return 1; }
	std::function<aiMatrix4x4(const aiNode*)> globalOf = [&](const aiNode* n) { return n->mParent ? globalOf(n->mParent) * n->mTransformation : n->mTransformation; };
	std::map<const aiMesh*, aiMatrix4x4> meshGlobal;
	std::function<void(const aiNode*)> findMeshes = [&](const aiNode* n) {
		for (unsigned i = 0; i < n->mNumMeshes; ++i) meshGlobal[sc->mMeshes[n->mMeshes[i]]] = globalOf(n);
		for (unsigned c = 0; c < n->mNumChildren; ++c) findMeshes(n->mChildren[c]);
	};
	findMeshes(sc->mRootNode);
	// source joints (scene space) from the bind matrices
	std::map<std::string, V3> srcJoint;
	for (unsigned m = 0; m < sc->mNumMeshes; ++m) {
		const aiMesh* me = sc->mMeshes[m];
		for (unsigned b = 0; b < me->mNumBones; ++b) {
			aiMatrix4x4 bind = meshGlobal[me] * aiMatrix4x4(me->mBones[b]->mOffsetMatrix).Inverse();
			srcJoint[me->mBones[b]->mName.C_Str()] = { bind.a4, bind.b4, bind.c4 };
		}
	}
	// also joints of bones that influence nothing (needed for aiming): node globals
	std::function<void(const aiNode*)> nodeJoints = [&](const aiNode* n) {
		if (!srcJoint.count(n->mName.C_Str())) { auto g = globalOf(n); srcJoint[n->mName.C_Str()] = { g.a4, g.b4, g.c4 }; }
		for (unsigned c = 0; c < n->mNumChildren; ++c) nodeJoints(n->mChildren[c]);
	};
	nodeJoints(sc->mRootNode);
	std::map<std::string, std::string> s2t;
	std::map<std::string, std::string> primary;  // tgt -> src
	int missing = 0;
	for (auto& [s, t] : maps) {
		if (!srcJoint.count(s)) { ++missing; continue; }  // e.g. a two-joint thumb
		if (!tgtBind.count(t)) { fprintf(stderr, "unknown target bone %s\n", t.c_str()); return 1; }
		s2t[s] = t;
		if (!primary.count(t)) primary[t] = s;
	}
	if (missing) printf("note: %d mapped source bones not in this model (skipped)\n", missing);
	if (!tgtBind.count("NPC Pelvis [Pelv]") || !primary.count("NPC Pelvis [Pelv]")) { fprintf(stderr, "need a pelvis mapping\n"); return 1; }

	// ---- normalise source: Z up, facing +Y, pelvis over origin, pelvis height = target pelvis height
	// Up axis = the longest extent of the standing character (FBX scenes usually come in Y-up).
	{
		double lo[3] = { 1e18, 1e18, 1e18 }, hi[3] = { -1e18, -1e18, -1e18 };
		for (unsigned m = 0; m < sc->mNumMeshes; ++m) {
			const aiMesh* me = sc->mMeshes[m];
			for (unsigned v = 0; v < me->mNumVertices; ++v) {
				auto p = meshGlobal[me] * me->mVertices[v];
				double c[3] = { p.x, p.y, p.z };
				for (int i = 0; i < 3; ++i) lo[i] = std::min(lo[i], c[i]), hi[i] = std::max(hi[i], c[i]);
			}
		}
		if (hi[1] - lo[1] > hi[2] - lo[2]) {  // Y-up: rotate +90 deg about X so Y becomes Z
			aiMatrix4x4 r(1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1);
			for (auto& [me, g] : meshGlobal) g = r * g;
			for (auto& [n, j] : srcJoint) j = { j.x, -j.z, j.y };
			printf("source is Y-up: turned to Z-up\n");
		}
	}
	double floorZ = 1e18;
	for (unsigned m = 0; m < sc->mNumMeshes; ++m) {
		const aiMesh* me = sc->mMeshes[m];
		for (unsigned v = 0; v < me->mNumVertices; ++v) { auto p = meshGlobal[me] * me->mVertices[v]; floorZ = std::min<double>(floorZ, p.z); }
	}
	const V3 pel = srcJoint[primary["NPC Pelvis [Pelv]"]];
	// Facing: turn about Z so the feet point along +Y (Skyrim's forward).
	double yaw = 0;
	if (primary.count("NPC L Foot [LLft ]") && primary.count("NPC L Toe0 [LToe]")) {
		V3 f = srcJoint[primary["NPC L Toe0 [LToe]"]] - srcJoint[primary["NPC L Foot [LLft ]"]];
		if (primary.count("NPC R Foot [Rft ]") && primary.count("NPC R Toe0 [RToe]")) f = f + (srcJoint[primary["NPC R Toe0 [RToe]"]] - srcJoint[primary["NPC R Foot [Rft ]"]]);
		yaw = std::atan2(f.x, f.y);  // angle of the toes from +Y toward +X
	}
	const double cy = std::cos(yaw), sy = std::sin(yaw);
	auto turn = [&](V3 d) { return V3{ d.x * cy - d.y * sy, d.x * sy + d.y * cy, d.z }; };  // rotates the toe direction onto +Y
	const double s = T("NPC Pelvis [Pelv]").z / (pel.z - floorZ);
	auto norm = [&](V3 p) { V3 q = turn(V3{ p.x - pel.x, p.y - pel.y, 0 }); return V3{ q.x * s, q.y * s, (p.z - floorZ) * s }; };
	auto normDir = [&](V3 d) { return turn(d); };
	const double facing = yaw;
	printf("source: scale %.4f, turned %.0f deg to face +Y, floor %.2f\n", s, facing * 57.2958, floorZ);

	// ---- per target bone: affine A(v) = Tj + M * (v - Sj)
	struct Aff { V3 S, Tj; M3 M; M3 R; V3 axis; double len = 0, stretch = 1; };
	std::map<std::string, Aff> aff;
	// Per target bone: rotation R (source bone direction -> target direction) and a stretch along the bone that
	// only applies within the bone's length (parts sticking out past it just move along), clamped to sane values.
	std::function<std::pair<M3, std::tuple<V3, double, double>>(const std::string&)> rotFor = [&](const std::string& t) -> std::pair<M3, std::tuple<V3, double, double>> {
		if (aims.count(t) && primary.count(t) && primary.count(aims[t])) {
			V3 sd = norm(srcJoint[primary[aims[t]]]) - norm(srcJoint[primary[t]]);
			V3 td = T(aims[t]) - T(t);
			if (sd.len() > 0.05) {
				const double l = std::clamp(td.len() / sd.len(), 0.6, 1.8);
				return { RotBetween(sd, td), { td.unit(), sd.len(), l } };
			}
		}
		const std::string p = tgtParent.count(t) ? tgtParent[t] : "";
		if (p.empty() || !primary.count(p)) return { M3{}, { V3{ 0, 0, 1 }, 0.0, 1.0 } };
		auto r = rotFor(p);
		std::get<2>(r.second) = 1.0;  // inherit the turn, not the stretch
		return r;
	};
	for (auto& [t, src] : primary) {
		auto [R, st] = rotFor(t);
		Aff a;
		a.S = norm(srcJoint[src]), a.Tj = T(t), a.R = R, a.M = R;
		a.axis = std::get<0>(st), a.len = std::get<1>(st), a.stretch = std::get<2>(st);
		aff[t] = a;
	}
	const bool noRetarget = getenv("SKINCONV_RAW") != nullptr;
	// The trunk (pelvis..head) moves as one: a continuous remap of height along the spine joints (source heights ->
	// target heights, with the spine's sideways offset), so neighbouring trunk bones never pull the torso apart.
	static const std::vector<std::string> trunkNames = { "NPC Pelvis [Pelv]", "NPC Spine [Spn0]", "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]", "NPC Neck [Neck]", "NPC Head [Head]" };
	struct Knot { double sz, tz; V3 off; };
	std::vector<Knot> trunk;
	for (auto& n : trunkNames) {
		if (!primary.count(n)) continue;
		const V3 sj = norm(srcJoint[primary[n]]), tj = T(n);
		trunk.push_back({ sj.z, tj.z, V3{ tj.x - sj.x, tj.y - sj.y, 0 } });
	}
	std::sort(trunk.begin(), trunk.end(), [](const Knot& a, const Knot& b) { return a.sz < b.sz; });
	// two trunk joints at the same source height (a Biped ribcage joint sits up at the neck) would tear the torso
	// open between their two target heights: keep the upper one, so the chest stretches smoothly up to the neck
	for (size_t i = 0; i + 1 < trunk.size();) {
		if (trunk[i + 1].sz - trunk[i].sz < 1.0 || trunk[i + 1].tz <= trunk[i].tz) {
			trunk.erase(trunk.begin() + i);
		} else {
			++i;
		}
	}
	for (auto& k : trunk) printf("trunk knot: source z %.1f -> target z %.1f, offset (%.1f %.1f)\n", k.sz, k.tz, k.off.x, k.off.y);
	auto trunkMap = [&](V3 v) {
		if (trunk.empty()) return v;
		if (v.z <= trunk.front().sz) return V3{ v.x + trunk.front().off.x, v.y + trunk.front().off.y, v.z + (trunk.front().tz - trunk.front().sz) };
		if (v.z >= trunk.back().sz) return V3{ v.x + trunk.back().off.x, v.y + trunk.back().off.y, v.z + (trunk.back().tz - trunk.back().sz) };
		for (size_t i = 0; i + 1 < trunk.size(); ++i) {
			const auto& a = trunk[i];
			const auto& b = trunk[i + 1];
			if (v.z >= a.sz && v.z <= b.sz) {
				const double t = b.sz > a.sz ? (v.z - a.sz) / (b.sz - a.sz) : 0;
				const V3 off = a.off * (1 - t) + b.off * t;
				return V3{ v.x + off.x, v.y + off.y, a.tz + (b.tz - a.tz) * t };
			}
		}
		return v;
	};
	std::set<std::string> trunkSet(trunkNames.begin(), trunkNames.end());
	trunkSet.insert("NPC Head [Jaw]");
	auto applyBone = [&](const std::string& t, V3 v) {
		if (noRetarget) return v;
		if (trunkSet.count(t)) return trunkMap(v);
		const auto& a = aff[t];
		V3     r = a.R * (v - a.S);                   // turned, relative to the target joint
		double u = r.dot(a.axis);                     // distance along the target bone
		double u2 = u <= 0 ? u : (u <= a.len ? u * a.stretch : a.len * a.stretch + (u - a.len));
		return a.Tj + r + a.axis * (u2 - u);
	};

	// ---- build shapes per material
	struct Group { std::vector<Vector3> v, n; std::vector<Vector2> uv; std::vector<Triangle> tri; std::vector<std::map<std::string, float>> w; };
	std::map<std::string, Group> groups;
	auto mapBone = [&](std::string b) -> std::string {
		// unmapped source bones fall back to their nearest mapped ancestor
		const aiNode* n = sc->mRootNode->FindNode(b.c_str());
		while (n && !s2t.count(n->mName.C_Str())) n = n->mParent;
		return n ? s2t[n->mName.C_Str()] : "NPC Pelvis [Pelv]";
	};
	// Seams: characters made of separate pieces (torso, hips, sleeves) have vertices at the same spot in two pieces,
	// often weighted differently, so the engine pulls them apart and the inside shows through. Every vertex takes the
	// average weights of all vertices that share its position (welded skinning; geometry and UVs stay as they are).
	auto posKey = [](const aiVector3D& v) { return std::make_tuple(std::lround(v.x * 200.0), std::lround(v.y * 200.0), std::lround(v.z * 200.0)); };
	std::map<std::tuple<long, long, long>, std::pair<std::map<std::string, float>, int>> weld;
	const bool noWeld = getenv("SKINCONV_NOWELD") != nullptr;
	for (unsigned m = 0; m < sc->mNumMeshes && !noWeld; ++m) {
		const aiMesh* me = sc->mMeshes[m];
		if (skip.count(me->mName.C_Str())) continue;
		std::vector<std::map<std::string, float>> w(me->mNumVertices);
		for (unsigned b = 0; b < me->mNumBones; ++b) {
			const std::string tb = mapBone(me->mBones[b]->mName.C_Str());
			for (unsigned i = 0; i < me->mBones[b]->mNumWeights; ++i) w[me->mBones[b]->mWeights[i].mVertexId][tb] += me->mBones[b]->mWeights[i].mWeight;
		}
		for (unsigned v = 0; v < me->mNumVertices; ++v) {
			auto& e = weld[posKey(meshGlobal[me] * me->mVertices[v])];
			float tot = 0;
			for (auto& [bn, x] : w[v]) tot += x;
			if (tot <= 0) continue;
			for (auto& [bn, x] : w[v]) e.first[bn] += x / tot;
			++e.second;
		}
	}
	int welded = 0;
	for (unsigned m = 0; m < sc->mNumMeshes; ++m) {
		const aiMesh* me = sc->mMeshes[m];
		if (skip.count(me->mName.C_Str())) continue;
		aiString mn; sc->mMaterials[me->mMaterialIndex]->Get(AI_MATKEY_NAME, mn);
		auto& g = groups[mn.C_Str()];
		const unsigned base = g.v.size();
		std::vector<std::map<std::string, float>> w(me->mNumVertices);
		for (unsigned b = 0; b < me->mNumBones; ++b) {
			const std::string tb = mapBone(me->mBones[b]->mName.C_Str());
			for (unsigned i = 0; i < me->mBones[b]->mNumWeights; ++i) w[me->mBones[b]->mWeights[i].mVertexId][tb] += me->mBones[b]->mWeights[i].mWeight;
		}
		aiMatrix3x3 nrm(meshGlobal[me]);
		for (unsigned v = 0; v < me->mNumVertices; ++v) {
			auto pw = meshGlobal[me] * me->mVertices[v];
			V3   p = norm({ pw.x, pw.y, pw.z });
			if (!noWeld) {
				if (auto it = weld.find(posKey(pw)); it != weld.end() && it->second.second > 1) {
					w[v] = it->second.first;  // shared with another piece: the average of them all
					++welded;
				}
			}
			auto nw = nrm * (me->HasNormals() ? me->mNormals[v] : aiVector3D(0, 0, 1));
			V3   nn = normDir({ nw.x, nw.y, nw.z });
			if (w[v].empty()) w[v]["NPC Pelvis [Pelv]"] = 1;
			// keep the 4 strongest, renormalise
			std::vector<std::pair<float, std::string>> sorted;
			for (auto& [b, x] : w[v]) sorted.emplace_back(x, b);
			std::sort(sorted.rbegin(), sorted.rend());
			sorted.resize(std::min<size_t>(4, sorted.size()));
			float tot = 0; for (auto& e : sorted) tot += e.first;
			std::map<std::string, float> ww;
			V3 out, on;
			for (auto& [x, b] : sorted) {
				ww[b] = x / tot;
				out = out + applyBone(b, p) * (x / tot);
				on = on + (aff[b].M * nn) * (x / tot);
			}
			on = on.unit();
			g.v.emplace_back(float(out.x), float(out.y), float(out.z));
			g.n.emplace_back(float(on.x), float(on.y), float(on.z));
			g.uv.emplace_back(me->HasTextureCoords(0) ? me->mTextureCoords[0][v].x : 0.f, me->HasTextureCoords(0) ? me->mTextureCoords[0][v].y : 0.f);
			g.w.push_back(ww);
		}
		for (unsigned f = 0; f < me->mNumFaces; ++f)
			if (me->mFaces[f].mNumIndices == 3) g.tri.emplace_back(base + me->mFaces[f].mIndices[0], base + me->mFaces[f].mIndices[1], base + me->mFaces[f].mIndices[2]);
	}
	for (auto& [k, g] : groups) for (auto& v : g.v) v = v * float(extraScale);
	printf("welded skin weights on %d seam vertices\n", welded);

	// ---- write NIF
	NifFile nif;
	nif.Create(NiVersion::getSSE());
	nif.GetRootNode()->name.get() = "Scene Root";
	std::set<std::string> used;
	for (auto& [k, g] : groups) for (auto& w : g.w) for (auto& [b, x] : w) used.insert(b);
	std::map<std::string, int> nodeId;
	for (auto& b : used) {
		MatTransform x = tgtBind[b];
		x.translation = x.translation * float(extraScale);
		nodeId[b] = nif.GetBlockID(nif.AddNode(b, x, nif.GetRootNode()));
	}
	int si = 0;
	for (auto& [matName, g] : groups) {
		if (g.v.size() > 65535) { fprintf(stderr, "group %s too big\n", matName.c_str()); return 1; }
		const std::string name = matName + ":" + std::to_string(si++);
		NiShape*          sh = nif.CreateShapeFromData(name, &g.v, &g.tri, &g.uv, &g.n);
		nif.CreateSkinning(sh);
		std::vector<std::string> bones;
		for (auto& w : g.w) for (auto& [b, x] : w) if (std::find(bones.begin(), bones.end(), b) == bones.end()) bones.push_back(b);
		std::vector<int> ids;
		for (auto& b : bones) ids.push_back(nodeId[b]);
		nif.SetShapeBoneIDList(sh, ids);
		for (uint32_t bi = 0; bi < bones.size(); ++bi) {
			MatTransform bind = tgtBind[bones[bi]];
			bind.translation = bind.translation * float(extraScale);
			nif.SetShapeTransformSkinToBone(sh, bi, bind.InverseTransform());
			std::unordered_map<uint16_t, float> wm;
			for (uint32_t v = 0; v < g.w.size(); ++v) if (auto it = g.w[v].find(bones[bi]); it != g.w[v].end()) wm[uint16_t(v)] = it->second;
			nif.SetShapeBoneWeights(name, bi, wm);
			// Per-bone bounding sphere in bone space: the engine culls skinned meshes with these (radius 0 = never drawn).
			const MatTransform toBone = bind.InverseTransform();
			std::vector<Vector3> pts;
			for (auto& [v, wt] : wm) pts.push_back(toBone.ApplyTransform(g.v[v]));
			if (pts.empty()) pts.push_back(toBone.ApplyTransform(Vector3()));
			Vector3 c;
			for (auto& p : pts) c += p;
			c /= float(pts.size());
			float r = 0;
			for (auto& p : pts) r = std::max(r, (p - c).length());
			BoundingSphere bs(c, r + 1.0f);
			nif.SetShapeBoneBounds(name, bi, bs);
		}
		// SSE draws from the per-vertex weights in BSTriShape/NiSkinPartition (SetShapeBoneWeights only fills NiSkinData).
		for (uint32_t v = 0; v < g.w.size(); ++v) {
			std::vector<std::pair<float, uint8_t>> ws;
			for (auto& [b, x] : g.w[v]) {
				const auto it = std::find(bones.begin(), bones.end(), b);
				if (it != bones.end() && x > 0) ws.push_back({ x, static_cast<uint8_t>(it - bones.begin()) });
			}
			std::sort(ws.begin(), ws.end(), [](auto& a, auto& b) { return a.first > b.first; });
			if (ws.size() > 4) ws.resize(4);
			std::vector<uint8_t> ids;
			std::vector<float>   wts;
			for (auto& [x, i] : ws) ids.push_back(i), wts.push_back(x);
			nif.SetShapeVertWeights(name, v, ids, wts);
		}
		nif.UpdateSkinPartitions(sh);
		nif.CalcTangentsForShape(sh);
		Mat mat = mats.count(matName) ? mats[matName] : mats["*"];
		std::string t0 = mat.diffuse, t1 = mat.normal, t2 = mat.glow;
		if (!t0.empty()) nif.SetTextureSlot(sh, t0, 0);
		if (!t1.empty()) nif.SetTextureSlot(sh, t1, 1);
		if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(sh))) {
			ls->shaderFlags1 = SLSF1_SPECULAR | SLSF1_SKINNED | SLSF1_RECEIVE_SHADOWS | SLSF1_CAST_SHADOWS | SLSF1_REMAPPABLE_TEXTURES | SLSF1_ZBUFFER_TEST;
			ls->shaderFlags2 = SLSF2_ZBUFFER_WRITE | (mat.doubleSided ? SLSF2_DOUBLE_SIDED : 0);
			ls->specularStrength = mat.spec, ls->glossiness = mat.gloss;
			if (!t2.empty()) {
				nif.SetTextureSlot(sh, t2, 2);
				ls->SetShaderType(BSLSP_GLOWMAP);
				ls->shaderFlags2 |= SLSF2_GLOW_MAP;
				ls->emissiveColor = mat.emit, ls->emissiveMultiple = mat.emitMult;
			}
		}
		printf("shape %s: %zu verts, %zu tris, %zu bones\n", name.c_str(), g.v.size(), g.tri.size(), bones.size());
	}
	if (nif.Save(out)) { fprintf(stderr, "save failed\n"); return 1; }
	printf("wrote %s\n", out.c_str());
}
