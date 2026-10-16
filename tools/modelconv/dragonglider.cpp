// dragonglider <in.nif> <out.nif>: the Dragon Snow paraglider (a static dragon over the glider's rig) brought to life.
// The dragon becomes a skinned mesh on a skeleton of its own, built in its own space (head +Y, up +Z, wings +-X):
//   DG_Space (the old shape's place) > DG_Bank (turned by the game: the dragon leans into turns)
//     > DG_Body (bobs and pitches with the wingbeats)
//         > DG_Chest > DG_Neck1 > DG_Neck2 > DG_Look (turned by the game: the head looks where you glide)
//                                                > DG_Head (nods) > DG_Jaw (opened by the game when it breathes fire)
//                                                                  > DG_Mouth (where the fire comes from)
//                    > DG_WingL1 > DG_WingL2 > DG_WingL3   (shoulder, elbow, wrist; same on the right)
//         > DG_LegL1 > DG_LegL2 > DG_LegL3 (hip, knee, foot; same on the right) - moved by the game: they dangle and
//           kick in flight, reach out and clutch when the dragon swoops in to take you, tuck up when it breathes fire
//         > DG_Tail1 > DG_Tail2 > DG_Tail3 > DG_Tail4
// Weights are smooth (each vertex shared between neighbouring bones), so the body bends without seams. The keyframed
// bones loop over 6 seconds: two strong wingbeats (a quick downstroke, a slower upstroke with the wing folding, the
// elbow and wrist lagging behind like a whip) then a long glide with the membranes rippling; the body lifts on each
// downstroke, the neck answers it, the tail swims behind in a travelling wave, the legs swing. The glider's own rig
// (PG_*) is kept as it was.
#include "NifFile.hpp"
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
using namespace nifly;

static float S(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
static Quaternion Euler(float rx, float ry, float rz)  // degrees, applied X then Y then Z (intrinsic)
{
	const float d = 3.14159265f / 180.0f;
	auto q = [](float ax, float ay, float az, float a) { return Quaternion(std::cos(a / 2), ax * std::sin(a / 2), ay * std::sin(a / 2), az * std::sin(a / 2)); };
	auto mul = [](const Quaternion& a, const Quaternion& b) {
		return Quaternion(a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w);
	};
	return mul(mul(q(0, 0, 1, rz * d), q(0, 1, 0, ry * d)), q(1, 0, 0, rx * d));
}

struct Bone { std::string name, parent; Vector3 pos; };

int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "usage: dragonglider in.nif out.nif\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	auto& hdr = nif.GetHeader();
	NiShape* shape = nullptr;
	for (auto* s : nif.GetShapes()) shape = s;
	auto* ts = dynamic_cast<BSTriShape*>(shape);
	if (!ts) { fprintf(stderr, "no BSTriShape\n"); return 1; }
	NiNode* parent = nif.GetParentNode(ts);
	printf("shape '%s' under '%s', %zu verts\n", ts->name.get().c_str(), parent ? parent->name.get().c_str() : "?", ts->vertData.size());

	// ---- the skeleton, in the dragon's own space
	const float SX = 200, EX = 370, WX = 530;  // shoulder, elbow, wrist (|x|)
	std::vector<Bone> bones = {
		{ "DG_Bank", "", { 0, -1450, 180 } },
		{ "DG_Body", "DG_Bank", { 0, -1450, 180 } },
		{ "DG_Chest", "DG_Body", { 0, -1380, 200 } },
		{ "DG_Neck1", "DG_Chest", { 0, -1290, 205 } },
		{ "DG_Neck2", "DG_Neck1", { 0, -1205, 212 } },
		{ "DG_Look", "DG_Neck2", { 0, -1150, 205 } },
		{ "DG_Head", "DG_Look", { 0, -1150, 205 } },
		{ "DG_Jaw", "DG_Head", { 0, -1128, 152 } },
		{ "DG_Mouth", "DG_Head", { 0, -1012, 150 } },
		{ "DG_WingL0", "DG_Chest", { -SX, -1380, 170 } },  // (turned by the game: the wing unfolding as the glider opens)
		{ "DG_WingL1", "DG_WingL0", { -SX, -1380, 170 } },
		{ "DG_WingL2", "DG_WingL1", { -EX, -1300, 180 } },
		{ "DG_WingL3", "DG_WingL2", { -WX, -1245, 180 } },
		{ "DG_WingR0", "DG_Chest", { SX, -1380, 170 } },
		{ "DG_WingR1", "DG_WingR0", { SX, -1380, 170 } },
		{ "DG_WingR2", "DG_WingR1", { EX, -1300, 180 } },
		{ "DG_WingR3", "DG_WingR2", { WX, -1245, 180 } },
		{ "DG_LegL1", "DG_Body", { -88, -1565, 70 } },
		{ "DG_LegL2", "DG_LegL1", { -96, -1585, 12 } },
		{ "DG_LegL3", "DG_LegL2", { -100, -1590, -42 } },
		{ "DG_LegR1", "DG_Body", { 88, -1565, 70 } },
		{ "DG_LegR2", "DG_LegR1", { 96, -1585, 12 } },
		{ "DG_LegR3", "DG_LegR2", { 100, -1590, -42 } },
		{ "DG_Tail1", "DG_Body", { 0, -1680, 140 } },
		{ "DG_Tail2", "DG_Tail1", { 0, -1860, 180 } },
		{ "DG_Tail3", "DG_Tail2", { 0, -2040, 235 } },
		{ "DG_Tail4", "DG_Tail3", { 0, -2210, 215 } },
	};
	std::map<std::string, int> bi;
	for (int i = 0; i < (int)bones.size(); ++i) bi[bones[i].name] = i;

	// DG_Space takes the shape's place (its transform), the shape moves under it with none of its own
	MatTransform shapeXf = ts->GetTransformToParent();
	NiNode* space = nif.AddNode("DG_Space", shapeXf, parent);
	std::map<std::string, NiNode*> node;
	for (auto& b : bones) {
		NiNode* p = b.parent.empty() ? space : node[b.parent];
		Vector3 pp = b.parent.empty() ? Vector3() : bones[bi[b.parent]].pos;
		MatTransform x;
		x.translation = b.pos - pp;
		node[b.name] = nif.AddNode(b.name, x, p);
	}
	nif.SetParentNode(ts, space);
	ts->SetTransformToParent(MatTransform());

	// ---- weights
	const int nv = (int)ts->vertData.size();
	std::vector<std::map<std::string, float>> W(nv);
	struct Seg { float y; const char* bone; };
	const std::vector<Seg> spine = { { -1150, "DG_Head" }, { -1205, "DG_Neck2" }, { -1290, "DG_Neck1" }, { -1380, "DG_Chest" }, { -1480, "DG_Body" },
		{ -1620, "DG_Body" }, { -1700, "DG_Tail1" }, { -1870, "DG_Tail2" }, { -2045, "DG_Tail3" }, { -2215, "DG_Tail4" } };
	auto spineW = [&](float y, std::map<std::string, float>& w, float k) {
		if (y >= spine.front().y) { w[spine.front().bone] += k; return; }
		if (y <= spine.back().y) { w[spine.back().bone] += k; return; }
		for (size_t i = 0; i + 1 < spine.size(); ++i) {
			const float y0 = spine[i].y, y1 = spine[i + 1].y;
			if (y <= y0 && y >= y1) {
				float t = S(0, 1, (y0 - y) / (y0 - y1));
				w[spine[i].bone] += k * (1 - t);
				w[spine[i + 1].bone] += k * t;
				return;
			}
		}
	};
	for (int v = 0; v < nv; ++v) {
		const Vector3 p = ts->vertData[v].vert;
		const float ax = std::abs(p.x);
		const bool left = p.x < 0;
		auto& w = W[v];
		// the wing (from the shoulder out), blended into the chest over the shoulder
		const float wing = S(SX - 30, SX + 40, ax) * (p.y > -1650 ? 1.0f : 0.0f) * (1.0f - S(30, -10, p.z) * (ax < SX + 60 ? 1.0f : 0.0f));
		if (wing > 0) {
			const char* b1 = left ? "DG_WingL1" : "DG_WingR1";
			const char* b2 = left ? "DG_WingL2" : "DG_WingR2";
			const char* b3 = left ? "DG_WingL3" : "DG_WingR3";
			const float t12 = S(EX - 50, EX + 40, ax), t23 = S(WX - 50, WX + 50, ax);
			w[b1] += wing * (1 - t12);
			w[b2] += wing * t12 * (1 - t23);
			w[b3] += wing * t12 * t23;
		}
		const float rest = 1.0f - wing;
		if (rest <= 0) continue;
		// the hind legs: low under the hips
		const float legs = S(85, 45, p.z) * S(-1450, -1490, p.y) * S(-1720, -1690, p.y) * S(25, 50, ax) * S(200, 175, ax);
		if (legs > 0) {  // hip -> knee -> foot down the leg
			const std::string sd = left ? "L" : "R";
			const float k12 = S(30, 0, p.z), k23 = S(-22, -40, p.z);
			w["DG_Leg" + sd + "1"] += rest * legs * (1 - k12);
			w["DG_Leg" + sd + "2"] += rest * legs * k12 * (1 - k23);
			w["DG_Leg" + sd + "3"] += rest * legs * k12 * k23;
		}
		const float body = rest * (1 - legs);
		// the jaw: under the mouth line, forward of the hinge
		const float mouthZ = 152.0f - (p.y + 1130.0f) * 0.218f;
		const float jaw = p.y > -1150 ? S(-1150, -1128, p.y) * S(mouthZ + 3, mouthZ - 4, p.z) : 0.0f;
		if (jaw > 0) w["DG_Jaw"] += body * jaw;
		spineW(p.y, w, body * (1 - jaw));
	}
	// to the shape: bone list, binds, weights (4 strongest), bounds
	nif.CreateSkinning(ts);
	std::vector<int> ids;
	std::vector<std::string> used;
	for (auto& b : bones) if (b.name != "DG_Bank" && b.name != "DG_Look" && b.name != "DG_Mouth" && b.name != "DG_WingL0" && b.name != "DG_WingR0") used.push_back(b.name);
	for (auto& n : used) ids.push_back(nif.GetBlockID(node[n]));
	nif.SetShapeBoneIDList(ts, ids);
	if (auto* si = hdr.GetBlock<NiSkinInstance>(ts->SkinInstanceRef())) si->targetRef.index = nif.GetBlockID(space);
	const std::string name = ts->name.get();
	for (uint32_t b = 0; b < used.size(); ++b) {
		MatTransform bind;
		bind.translation = bones[bi[used[b]]].pos;  // (no rotations in the rest pose: the bind is a pure offset)
		nif.SetShapeTransformSkinToBone(ts, b, bind.InverseTransform());
		std::unordered_map<uint16_t, float> wm;
		std::vector<Vector3> pts;
		for (int v = 0; v < nv; ++v) {
			// normalise and keep the 4 strongest
			std::vector<std::pair<float, std::string>> s;
			float tot = 0;
			for (auto& [k, x] : W[v]) if (x > 1e-4f) s.push_back({ x, k }), tot += x;
			std::sort(s.rbegin(), s.rend());
			if (s.size() > 4) s.resize(4);
			float t4 = 0;
			for (auto& e : s) t4 += e.first;
			for (auto& [x, k] : s) if (k == used[b]) { wm[uint16_t(v)] = x / t4; pts.push_back(ts->vertData[v].vert - bind.translation); }
		}
		nif.SetShapeBoneWeights(name, b, wm);
		Vector3 c;
		for (auto& p : pts) c += p;
		if (!pts.empty()) c /= float(pts.size());
		float r = 1;
		for (auto& p : pts) r = std::max(r, (p - c).length());
		BoundingSphere bs(c, r + 2.0f);
		nif.SetShapeBoneBounds(name, b, bs);
	}
	for (int v = 0; v < nv; ++v) {
		std::vector<std::pair<float, uint8_t>> s;
		for (auto& [k, x] : W[v]) {
			auto it = std::find(used.begin(), used.end(), k);
			if (it != used.end() && x > 1e-4f) s.push_back({ x, uint8_t(it - used.begin()) });
		}
		std::sort(s.begin(), s.end(), [](auto& a, auto& b) { return a.first > b.first; });
		if (s.size() > 4) s.resize(4);
		float t = 0;
		for (auto& e : s) t += e.first;
		std::vector<uint8_t> bid;
		std::vector<float>   wt;
		for (auto& [x, i] : s) bid.push_back(i), wt.push_back(x / t);
		nif.SetShapeVertWeights(name, v, bid, wt);
	}
	nif.UpdateSkinPartitions(ts);
	if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(ts))) ls->shaderFlags1 |= SLSF1_SKINNED;

	// ---- the loop: 6 seconds, sampled 60 times a second
	constexpr float T = 6.0f;
	constexpr int   N = 360;  // (60 keys a second)
	auto flapEnv = [](float t) { return S(0.15f, 0.45f, t) * (1 - S(2.75f, 3.15f, t)); };  // two beats, then the glide
	// one wingbeat (period 1.3 s): -1 bottom .. +1 top; the downstroke takes 40% of it
	auto beat = [](float t) {
		float u = std::fmod(t + 10.0f * 1.3f, 1.3f) / 1.3f;
		if (u < 0.4f) { float k = u / 0.4f; return std::cos(3.14159f * k); }          // top -> bottom, fast
		float k = (u - 0.4f) / 0.6f; return -std::cos(3.14159f * k);                    // bottom -> top, slower
	};
	auto glide = [](float t, float ph) { return std::sin(6.28318f * t / 3.0f + ph); };  // period 3 s: two in the loop
	struct Track { std::string bone; std::function<void(float, Vector3&, Quaternion&)> f; };
	std::vector<Track> tracks;
	for (int side = 0; side < 2; ++side) {
		const float sg = side == 0 ? 1.0f : -1.0f;  // left wing: +roll raises it; right: -roll
		const std::string L = side == 0 ? "L" : "R";
		const float lags[3] = { 0.0f, 0.10f, 0.20f }, amps[3] = { 34.0f, 22.0f, 16.0f }, rest[3] = { 9.0f, -3.0f, -4.0f };
		for (int j = 0; j < 3; ++j) {
			tracks.push_back({ "DG_Wing" + L + std::to_string(j + 1), [=](float t, Vector3&, Quaternion& q) {
				const float e = flapEnv(t - lags[j]);
				const float b = beat(t - 0.3f - lags[j]);
				const float up = std::max(0.0f, std::sin(3.14159f * std::fmod(t + 13.0f - 0.3f - lags[j], 1.3f) / 1.3f * 2.0f - 0.8f));
				const float roll = rest[j] + e * amps[j] * b + (1 - e) * (j == 0 ? 2.5f : 1.5f) * glide(t - lags[j], j * 0.6f);
				const float sweep = e * (j == 0 ? 6.0f : 10.0f) * up;               // folding back on the upstroke
				const float twist = e * (j == 2 ? 7.0f : 3.0f) * (b < 0 ? 1.0f : 0.4f) + (1 - e) * 1.5f * glide(t, 1.0f + j);
				q = Euler(twist, sg * roll, -sg * sweep);
			} });
		}
	}
	tracks.push_back({ "DG_Body", [=](float t, Vector3& p, Quaternion& q) {
		const float e = flapEnv(t), b = beat(t - 0.42f);
		p.z += -e * 14.0f * b + (1 - e) * 4.0f * glide(t, 0.3f);    // lifts as the wings come down
		q = Euler(e * 2.5f * b + (1 - e) * 1.2f * glide(t, 1.4f), 0, 0);
	} });
	tracks.push_back({ "DG_Neck1", [=](float t, Vector3&, Quaternion& q) {
		const float e = flapEnv(t), b = beat(t - 0.6f);
		q = Euler(-e * 4.0f * b + 1.5f * glide(t, 2.0f), 0, 2.0f * std::sin(6.28318f * t / 6.0f));
	} });
	tracks.push_back({ "DG_Neck2", [=](float t, Vector3&, Quaternion& q) {
		const float e = flapEnv(t), b = beat(t - 0.75f);
		q = Euler(e * 3.0f * b + 1.2f * glide(t, 2.6f), 0, 3.0f * std::sin(6.28318f * t / 6.0f - 0.7f));
	} });
	tracks.push_back({ "DG_Head", [=](float t, Vector3&, Quaternion& q) {
		q = Euler(-2.0f * glide(t, 3.1f), 1.0f * std::sin(6.28318f * t / 3.0f), 0);
	} });
	for (int j = 0; j < 4; ++j) {
		tracks.push_back({ "DG_Tail" + std::to_string(j + 1), [=](float t, Vector3&, Quaternion& q) {
			const float ph = j * 0.55f;
			const float yaw = (4.0f + 2.5f * j) * std::sin(6.28318f * t / 3.0f - ph);
			const float e = flapEnv(t - 0.2f * j), b = beat(t - 0.5f - 0.12f * j);
			const float pitch = e * (2.0f + 1.5f * j) * b + (1.5f + j) * std::sin(6.28318f * t / 6.0f - ph);
			q = Euler(pitch, 0, yaw);
		} });
	}
	if (getenv("DG_TESTLEGS")) {  // (preview only: the legs in a reaching, clutching swing, to see their weights bend well)
		for (const char* sd : { "L", "R" }) {
			tracks.push_back({ std::string("DG_Leg") + sd + "1", [](float t, Vector3&, Quaternion& q) { q = Euler(40.0f * std::sin(6.28318f * t / 3.0f), 0, 0); } });
			tracks.push_back({ std::string("DG_Leg") + sd + "2", [](float t, Vector3&, Quaternion& q) { q = Euler(-35.0f * std::sin(6.28318f * t / 3.0f + 0.8f), 0, 0); } });
			tracks.push_back({ std::string("DG_Leg") + sd + "3", [](float t, Vector3&, Quaternion& q) { q = Euler(45.0f * std::sin(6.28318f * t / 3.0f + 1.6f), 0, 0); } });
		}
	}
	// --preview <dir>: the posed mesh at 24 moments of the loop (verts + tris as text, for tools/dragonpreview.py)
	if (argc > 4 && std::string(argv[3]) == "--preview") {
		const int nt = (int)ts->triangles.size();
		for (int f = 0; f < 24; ++f) {
			const float t = T * f / 24;
			// each bone's pose in the dragon's space: parent * (offset + track)
			std::map<std::string, std::pair<Vector3, Matrix3>> world;  // rotation as a matrix
			auto qm = [](const Quaternion& q) { Matrix3 m; m[0][0] = 1 - 2 * (q.y * q.y + q.z * q.z); m[0][1] = 2 * (q.x * q.y - q.z * q.w); m[0][2] = 2 * (q.x * q.z + q.y * q.w);
				m[1][0] = 2 * (q.x * q.y + q.z * q.w); m[1][1] = 1 - 2 * (q.x * q.x + q.z * q.z); m[1][2] = 2 * (q.y * q.z - q.x * q.w);
				m[2][0] = 2 * (q.x * q.z - q.y * q.w); m[2][1] = 2 * (q.y * q.z + q.x * q.w); m[2][2] = 1 - 2 * (q.x * q.x + q.y * q.y); return m; };
			for (auto& b : bones) {
				Vector3 off = b.pos - (b.parent.empty() ? Vector3() : bones[bi[b.parent]].pos);
				Quaternion q;
				for (auto& tr : tracks) if (tr.bone == b.name) tr.f(t, off, q);
				Matrix3 R = qm(q);
				if (b.parent.empty()) world[b.name] = { off, R };
				else { auto& [pp, pr] = world[b.parent]; world[b.name] = { pp + pr * off, pr * R }; }
			}
			char fn[512]; snprintf(fn, sizeof fn, "%s/f%02d.txt", argv[4], f);
			FILE* o = fopen(fn, "w");
			for (int v = 0; v < nv; ++v) {
				std::vector<std::pair<float, std::string>> s; float tot = 0;
				for (auto& [k, x] : W[v]) if (x > 1e-4f) s.push_back({ x, k }), tot += x;
				Vector3 out;
				for (auto& [x, k] : s) { auto& [wp, wr] = world[k]; out += (wp + wr * (ts->vertData[v].vert - bones[bi[k]].pos)) * (x / tot); }
				fprintf(o, "v %.2f %.2f %.2f\n", out.x, out.y, out.z);
			}
			if (f == 0) for (int i = 0; i < nt; ++i) fprintf(o, "f %d %d %d\n", ts->triangles[i].p1, ts->triangles[i].p2, ts->triangles[i].p3);
			fclose(o);
		}
		printf("preview written\n");
		return 0;
	}
	for (auto& tr : tracks) {
		NiNode* n = node[tr.bone];
		const Vector3 base = n->GetTransformToParent().translation;
		auto data = std::make_unique<NiTransformData>();
		data->rotationType = LINEAR_KEY;
		data->translations.SetInterpolationType(LINEAR_KEY);
		for (int i = 0; i <= N; ++i) {
			const float t = T * i / N;
			Vector3 p = base;
			Quaternion q;
			tr.f(t, p, q);
			NiAnimationKey<Quaternion> qk;
			qk.type = LINEAR_KEY, qk.time = t, qk.value = q;
			data->quaternionKeys.push_back(qk);
			NiAnimationKey<Vector3> pk;
			pk.type = LINEAR_KEY, pk.time = t, pk.value = p;
			data->translations.AddKey(pk);
		}
		const auto dataRef = hdr.AddBlock(std::move(data));
		auto ip = std::make_unique<NiTransformInterpolator>();
		ip->translation = base;
		ip->rotation = Quaternion();
		ip->scale = 1.0f;
		ip->dataRef.index = dataRef;
		const auto ipRef = hdr.AddBlock(std::move(ip));
		auto ctrl = std::make_unique<NiTransformController>();
		ctrl->flags = 0x48;  // active, looping
		ctrl->frequency = 1.0f;
		ctrl->startTime = 0.0f;
		ctrl->stopTime = T;
		ctrl->targetRef.index = nif.GetBlockID(n);
		ctrl->interpolatorRef.index = ipRef;
		ctrl->nextControllerRef.index = n->controllerRef.index;
		n->controllerRef.index = hdr.AddBlock(std::move(ctrl));
	}
	// --world: the dragon on its own, for a reference placed in the world (it flies off when the glider is put away):
	// DG_Space at the origin, keeping its scale (the game sets the root to where DG_Space was on you)
	if (argc > 3 && std::string(argv[3]) == "--world") {
		MatTransform x = space->GetTransformToParent();
		x.translation = Vector3();
		space->SetTransformToParent(x);
	}
	auto* root = nif.GetRootNode();
	for (auto& r : root->extraDataRefs)
		if (auto* b = hdr.GetBlock<BSXFlags>(r)) b->integerData |= 1u;
	nif.Save(argv[2]);
	printf("wrote %s: %zu bones, %zu animated\n", argv[2], bones.size(), tracks.size());
}
