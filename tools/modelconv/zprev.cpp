// zprev: preview Mixamo animations
//   zprev mix <char.fbx> <anim.fbx> <outdir> [W H]           a Mixamo character with a Mixamo animation (same skeleton)
//   zprev nif <model.nif> <skeleton.nif> <anim.fbx> <texdir> <outdir> [W H]
//                                                            one of our Skyrim creatures (skinned on the draugr / human
//                                                            skeleton) with the Mixamo animation retargeted onto it
// Frames go to <outdir>/f0000.ppm ... ; the camera is fixed from the first frame (3/4 front).
// Retarget (nif mode): every mapped Skyrim bone takes the Mixamo bone's world-space turn since its rest pose, after
// its own rest direction has been aimed along the Mixamo bone's rest direction; bones keep their Skyrim lengths;
// the pelvis follows the hips' travel scaled by the two hips' heights. Unmapped bones (twists, pauldrons) follow
// their parent.
#include "../../src/ClipBlend.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "NifFile.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

// ------------------------------------------------------------------ math
struct V3 { float x = 0, y = 0, z = 0; };
static V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static float len(V3 a) { return std::sqrt(dot(a, a)); }
static V3 norm(V3 a) { float l = len(a); return l > 1e-9f ? a * (1.0f / l) : V3{ 0, 0, 1 }; }
struct M3 { float m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }; };
static M3 mul(const M3& a, const M3& b) { M3 r; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { r.m[i][j] = 0; for (int k = 0; k < 3; k++) r.m[i][j] += a.m[i][k] * b.m[k][j]; } return r; }
static M3 tr(const M3& a) { M3 r; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r.m[i][j] = a.m[j][i]; return r; }
static V3 mul(const M3& a, V3 v) { return { a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z, a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z, a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z }; }
static M3 rotBetween(V3 a, V3 b)
{
	a = norm(a), b = norm(b);
	V3 v = cross(a, b); float c = dot(a, b);
	M3 r;
	if (len(v) < 1e-7f) { if (c > 0) return r; // opposite: 180 about any perpendicular
		V3 p = std::fabs(a.x) < 0.9f ? norm(cross(a, { 1, 0, 0 })) : norm(cross(a, { 0, 1, 0 }));
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r.m[i][j] = 2 * (&p.x)[i] * (&p.x)[j] - (i == j ? 1 : 0);
		return r; }
	float k = 1.0f / (1.0f + c);
	float vx[3][3] = { { 0, -v.z, v.y }, { v.z, 0, -v.x }, { -v.y, v.x, 0 } };
	for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
		float vv = 0; for (int q = 0; q < 3; q++) vv += vx[i][q] * vx[q][j];
		r.m[i][j] = (i == j ? 1.0f : 0.0f) + vx[i][j] + vv * k;
	}
	return r;
}
// affine transform: p' = R*s*p + t
struct Xf { M3 R; V3 t; float s = 1; };
static Xf compose(const Xf& a, const Xf& b) { Xf r; r.R = mul(a.R, b.R); r.s = a.s * b.s; r.t = a.t + mul(a.R, b.t) * a.s; return r; }
static V3 apply(const Xf& a, V3 p) { return a.t + mul(a.R, p) * a.s; }
static Xf inverse(const Xf& a) { Xf r; r.R = tr(a.R); r.s = 1.0f / a.s; r.t = mul(r.R, a.t) * (-r.s); return r; }
static Xf fromNif(const nifly::MatTransform& m)
{
	Xf r; r.s = m.scale; r.t = { m.translation.x, m.translation.y, m.translation.z };
	for (int i = 0; i < 3; i++) { r.R.m[i][0] = m.rotation[i].x; r.R.m[i][1] = m.rotation[i].y; r.R.m[i][2] = m.rotation[i].z; }
	return r;
}
static Xf fromAi(const aiMatrix4x4& m)
{
	aiVector3D s, p; aiQuaternion q; m.Decompose(s, q, p);
	aiMatrix3x3 rm = q.GetMatrix();
	Xf r; r.s = (s.x + s.y + s.z) / 3.0f; r.t = { p.x, p.y, p.z };
	for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r.R.m[i][j] = rm[i][j];
	return r;
}

// ------------------------------------------------------------------ Mixamo animation sampling
static std::string Strip(const std::string& n) { auto p = n.rfind(':'); return p == std::string::npos ? n : n.substr(p + 1); }
struct Anim
{
	const aiScene* sc = nullptr;
	const aiAnimation* an = nullptr;
	std::map<std::string, const aiNodeAnim*> ch;  // by stripped name
	double dur = 1, tps = 30;
	bool keepLengths = true;
	void Init(const aiScene* s)
	{
		sc = s;
		if (!s->mNumAnimations) return;
		an = s->mAnimations[0];
		dur = an->mDuration, tps = an->mTicksPerSecond > 0 ? an->mTicksPerSecond : 30;
		for (unsigned i = 0; i < an->mNumChannels; i++) ch[Strip(an->mChannels[i]->mNodeName.C_Str())] = an->mChannels[i];
	}
	template <class K, class V>
	static V Lerp(const K* keys, unsigned n, double t, std::function<V(const V&, const V&, float)> f)
	{
		if (n == 1 || t <= keys[0].mTime) return keys[0].mValue;
		for (unsigned i = 0; i + 1 < n; i++)
			if (t < keys[i + 1].mTime) { float u = float((t - keys[i].mTime) / (keys[i + 1].mTime - keys[i].mTime)); return f(keys[i].mValue, keys[i + 1].mValue, u); }
		return keys[n - 1].mValue;
	}
	// local transform of the node at tick t (or its default when it has no channel)
	aiMatrix4x4 Local(const aiNode* n, double t, bool rest) const
	{
		if (rest || !an) return n->mTransformation;
		auto it = ch.find(Strip(n->mName.C_Str()));
		if (it == ch.end()) return n->mTransformation;
		const aiNodeAnim* c = it->second;
		aiVector3D p = c->mNumPositionKeys ? Lerp<aiVectorKey, aiVector3D>(c->mPositionKeys, c->mNumPositionKeys, t, [](const aiVector3D& a, const aiVector3D& b, float u) { return a + (b - a) * u; }) : aiVector3D();
		// (a clip made on another body: only the hips travel - every other bone keeps this body's own length; taking the
		// clip's offsets stretched the necks and arms of the Mixamo characters it was not made on)
		if (keepLengths && Strip(n->mName.C_Str()).rfind("Hips", 0) != 0) {
			aiVector3D s0, p0; aiQuaternion q0; n->mTransformation.Decompose(s0, q0, p0); p = p0;
		}
		aiQuaternion q = c->mNumRotationKeys ? Lerp<aiQuatKey, aiQuaternion>(c->mRotationKeys, c->mNumRotationKeys, t, [](const aiQuaternion& a, const aiQuaternion& b, float u) { aiQuaternion r; aiQuaternion::Interpolate(r, a, b, u); return r.Normalize(); }) : aiQuaternion();
		aiVector3D s = c->mNumScalingKeys ? Lerp<aiVectorKey, aiVector3D>(c->mScalingKeys, c->mNumScalingKeys, t, [](const aiVector3D& a, const aiVector3D& b, float u) { return a + (b - a) * u; }) : aiVector3D(1, 1, 1);
		return aiMatrix4x4(s, q, p);
	}
};
// world transforms of every node of a scene's hierarchy, with the animation's channels (by stripped name) applied
static void Worlds(const aiScene* sc, const Anim& a, double t, bool rest, std::map<std::string, aiMatrix4x4>& out)
{
	std::function<void(const aiNode*, const aiMatrix4x4&)> f = [&](const aiNode* n, const aiMatrix4x4& parent) {
		aiMatrix4x4 w = parent * a.Local(n, t, rest);
		out[Strip(n->mName.C_Str())] = w;
		for (unsigned i = 0; i < n->mNumChildren; i++) f(n->mChildren[i], w);
	};
	f(sc->mRootNode, aiMatrix4x4());
}

// ------------------------------------------------------------------ rendering
struct Tex { int w = 0, h = 0; std::vector<unsigned char> px; };  // RGBA
struct Mesh
{
	std::vector<V3> p;
	std::vector<float> uv;   // 2 per vertex
	std::vector<int> tri;    // 3 per triangle
	std::vector<int> triTex; // texture per triangle (-1 grey)
};
struct Cam { V3 eye, look; float fov = 32; bool set = false; float refH = 0, refLo = 0; };
static void Render(const Mesh& m, const std::vector<Tex>& tex, Cam& cam, int W, int H, const std::string& out)
{
	{
		V3 lo{ 1e9f, 1e9f, 1e9f }, hi{ -1e9f, -1e9f, -1e9f };
		for (auto& v : m.p) { lo = { std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z) }; hi = { std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z) }; }
		float h = hi.z - lo.z;
		if (cam.refH > 0) h = cam.refH, lo.z = cam.refLo;  // (the body's standing height: a clip that starts lying down keeps its frame)
		if (!cam.set) {
			cam.look = { (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, lo.z + h * 0.5f };
			cam.eye = cam.look + V3{ h * 0.95f, h * 2.3f, h * 0.22f };  // in front (+Y) and to its left side
			cam.set = true;
		} else {  // (follows the body across the ground: runs that travel stay in frame)
			V3 c{ (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, cam.look.z };
			V3 d = c - cam.look; d.z = 0;
			cam.look = cam.look + d, cam.eye = cam.eye + d;
		}
	}
	V3 f = norm(cam.look - cam.eye), r = norm(cross(f, { 0, 0, 1 })), u = cross(r, f);
	const float fl = 0.5f * H / std::tan(cam.fov * 0.5f * 3.14159265f / 180.0f);
	std::vector<V3> sp(m.p.size());
	for (size_t i = 0; i < m.p.size(); i++) {
		V3 d = m.p[i] - cam.eye;
		float z = dot(d, f);
		sp[i] = { W * 0.5f + dot(d, r) * fl / z, H * 0.5f - dot(d, u) * fl / z, z };
	}
	// smooth normals
	std::vector<V3> nrm(m.p.size());
	for (size_t t = 0; t + 2 < m.tri.size(); t += 3) {
		V3 a = m.p[m.tri[t]], b = m.p[m.tri[t + 1]], c = m.p[m.tri[t + 2]];
		V3 n = cross(b - a, c - a);
		for (int k = 0; k < 3; k++) nrm[m.tri[t + k]] = nrm[m.tri[t + k]] + n;
	}
	for (auto& n : nrm) n = norm(n);
	std::vector<float> zb(W * H, 1e30f);
	std::vector<unsigned char> img(W * H * 3);
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {  // a dusky backdrop, lighter at the foot
		float g = 0.12f + 0.10f * (float)y / H;
		img[(y * W + x) * 3] = (unsigned char)(255 * g * 0.9f), img[(y * W + x) * 3 + 1] = (unsigned char)(255 * g * 0.92f), img[(y * W + x) * 3 + 2] = (unsigned char)(255 * g);
	}
	const V3 sun = norm({ 0.5f, 0.8f, 0.9f });
	for (size_t t = 0; t + 2 < m.tri.size(); t += 3) {
		int i0 = m.tri[t], i1 = m.tri[t + 1], i2 = m.tri[t + 2];
		V3 a = sp[i0], b = sp[i1], c = sp[i2];
		if (a.z < 1 || b.z < 1 || c.z < 1) continue;
		float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
		if (std::fabs(area) < 1e-6f) continue;
		int x0 = std::max(0, (int)std::floor(std::min({ a.x, b.x, c.x }))), x1 = std::min(W - 1, (int)std::ceil(std::max({ a.x, b.x, c.x })));
		int y0 = std::max(0, (int)std::floor(std::min({ a.y, b.y, c.y }))), y1 = std::min(H - 1, (int)std::ceil(std::max({ a.y, b.y, c.y })));
		const int ti = m.triTex.empty() ? -1 : m.triTex[t / 3];
		const Tex* tx = ti >= 0 && ti < (int)tex.size() && tex[ti].w ? &tex[ti] : nullptr;
		for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
			float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
			float w2 = 1 - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			// perspective-correct
			float iz = w0 / a.z + w1 / b.z + w2 / c.z;
			float z = 1.0f / iz;
			if (z >= zb[y * W + x]) continue;
			zb[y * W + x] = z;
			float q0 = w0 / a.z * z, q1 = w1 / b.z * z, q2 = w2 / c.z * z;
			V3 n = norm(nrm[i0] * q0 + nrm[i1] * q1 + nrm[i2] * q2);
			if (dot(n, f) > 0) n = n * -1.0f;  // (the back of a face: lit as its front)
			float l = 0.32f + 0.75f * std::max(0.0f, dot(n, sun)) + 0.18f * std::max(0.0f, -dot(n, f));
			float col[3] = { 0.62f, 0.6f, 0.58f };
			if (tx) {
				float uu = m.uv[i0 * 2] * q0 + m.uv[i1 * 2] * q1 + m.uv[i2 * 2] * q2, vv = m.uv[i0 * 2 + 1] * q0 + m.uv[i1 * 2 + 1] * q1 + m.uv[i2 * 2 + 1] * q2;
				uu -= std::floor(uu), vv -= std::floor(vv);
				int tx_ = std::min(tx->w - 1, (int)(uu * tx->w)), ty_ = std::min(tx->h - 1, (int)(vv * tx->h));
				const unsigned char* c4 = &tx->px[(ty_ * tx->w + tx_) * 4];
				if (c4[3] < 60) { zb[y * W + x] = 1e30f; continue; }  // (cut-out: hair, rags)
				col[0] = c4[0] / 255.f, col[1] = c4[1] / 255.f, col[2] = c4[2] / 255.f;
			}
			for (int k = 0; k < 3; k++) img[(y * W + x) * 3 + k] = (unsigned char)std::min(255.0f, 255.0f * col[k] * l);
		}
	}
	FILE* o = fopen(out.c_str(), "wb");
	fprintf(o, "P6\n%d %d\n255\n", W, H);
	fwrite(img.data(), 1, img.size(), o);
	fclose(o);
}
static Tex LoadTex(const unsigned char* data, int bytes)
{
	Tex t; int n;
	unsigned char* p = stbi_load_from_memory(data, bytes, &t.w, &t.h, &n, 4);
	if (p) { t.px.assign(p, p + t.w * t.h * 4); stbi_image_free(p); } else t.w = t.h = 0;
	return t;
}
static Tex LoadTexFile(const std::string& path)
{
	Tex t; int n;
	unsigned char* p = stbi_load(path.c_str(), &t.w, &t.h, &n, 4);
	if (p) { t.px.assign(p, p + t.w * t.h * 4); stbi_image_free(p); } else { t.w = t.h = 0; fprintf(stderr, "no texture %s\n", path.c_str()); }
	return t;
}

// Mixamo (Y up, facing +Z, cm) into Skyrim's frame (Z up, facing +Y): x -> -x, y -> z, z -> y
static const M3 kM = [] { M3 r; r.m[0][0] = -1; r.m[0][1] = 0; r.m[0][2] = 0; r.m[1][0] = 0; r.m[1][1] = 0; r.m[1][2] = 1; r.m[2][0] = 0; r.m[2][1] = 1; r.m[2][2] = 0; return r; }();

// ------------------------------------------------------------------ mode: Mixamo character
static int RunMix(const char* charPath, const char* animPath, const std::string& outDir, int W, int H)
{
	Assimp::Importer ic, ia;
	const aiScene* cs = ic.ReadFile(charPath, aiProcess_Triangulate | aiProcess_LimitBoneWeights);
	const aiScene* as = ia.ReadFile(animPath, 0);
	if (!cs || !as) { fprintf(stderr, "load failed\n"); return 1; }
	Anim anim; anim.Init(as);
	std::vector<Tex> tex(cs->mNumMaterials);
	for (unsigned i = 0; i < cs->mNumMaterials; i++) {
		aiString p;
		if (cs->mMaterials[i]->GetTexture(aiTextureType_DIFFUSE, 0, &p) == AI_SUCCESS) {
			if (const aiTexture* et = cs->GetEmbeddedTexture(p.C_Str())) tex[i] = et->mHeight == 0 ? LoadTex((const unsigned char*)et->pcData, et->mWidth) : Tex{};
		}
	}
	std::filesystem::create_directories(outDir);
	const int frames = std::max(1, (int)std::round(anim.dur));
	Cam cam;
	{
		std::map<std::string, aiMatrix4x4> wo; Worlds(cs, anim, 0, true, wo);
		float lo = 1e9f, hi = -1e9f;
		for (unsigned mi = 0; mi < cs->mNumMeshes; mi++) {
			const aiMesh* me = cs->mMeshes[mi];
			for (unsigned b = 0; b < me->mNumBones; b++) {
				auto it = wo.find(Strip(me->mBones[b]->mName.C_Str())); if (it == wo.end()) continue;
				aiMatrix4x4 sk = it->second * me->mBones[b]->mOffsetMatrix;
				for (unsigned k = 0; k < me->mBones[b]->mNumWeights; k += 7) { aiVector3D v = sk * me->mVertices[me->mBones[b]->mWeights[k].mVertexId]; V3 p = mul(kM, V3{ v.x, v.y, v.z }); lo = std::min(lo, p.z), hi = std::max(hi, p.z); }
			}
		}
		cam.refH = hi - lo, cam.refLo = lo;
	}
	for (int fi = 0; fi <= frames; fi++) {
		std::map<std::string, aiMatrix4x4> wo;
		Worlds(cs, anim, fi, getenv("ZREST") != nullptr, wo);
		Mesh m;
		for (unsigned mi = 0; mi < cs->mNumMeshes; mi++) {
			const aiMesh* me = cs->mMeshes[mi];
			const int base = (int)m.p.size();
			std::vector<V3> acc(me->mNumVertices);
			std::vector<float> wsum(me->mNumVertices, 0);
			for (unsigned b = 0; b < me->mNumBones; b++) {
				const aiBone* bo = me->mBones[b];
				auto it = wo.find(Strip(bo->mName.C_Str()));
				if (it == wo.end()) continue;
				aiMatrix4x4 sk = it->second * bo->mOffsetMatrix;
				for (unsigned k = 0; k < bo->mNumWeights; k++) {
					const auto& vw = bo->mWeights[k];
					aiVector3D v = sk * me->mVertices[vw.mVertexId];
					acc[vw.mVertexId] = acc[vw.mVertexId] + V3{ v.x, v.y, v.z } * vw.mWeight;
					wsum[vw.mVertexId] += vw.mWeight;
				}
			}
			for (unsigned v = 0; v < me->mNumVertices; v++) {
				V3 p = wsum[v] > 0 ? acc[v] * (1.0f / wsum[v]) : V3{ me->mVertices[v].x, me->mVertices[v].y, me->mVertices[v].z };
				m.p.push_back(mul(kM, p));
				m.uv.push_back(me->HasTextureCoords(0) ? me->mTextureCoords[0][v].x : 0);
				m.uv.push_back(me->HasTextureCoords(0) ? 1.0f - me->mTextureCoords[0][v].y : 0);
			}
			for (unsigned t = 0; t < me->mNumFaces; t++) {
				if (me->mFaces[t].mNumIndices != 3) continue;
				for (int k = 0; k < 3; k++) m.tri.push_back(base + me->mFaces[t].mIndices[k]);
				m.triTex.push_back(me->mMaterialIndex);
			}
		}
		char name[64]; snprintf(name, sizeof name, "/f%04d.ppm", fi);
		Render(m, tex, cam, W, H, outDir + name);
	}
	printf("%d frames\n", frames + 1);
	return 0;
}

// ------------------------------------------------------------------ mode: our Skyrim creature, retargeted
struct SkBone { std::string name; int parent = -1; Xf local, restWorld; };
static int FindBone(const std::vector<SkBone>& sk, const std::string& prefix)
{
	for (size_t i = 0; i < sk.size(); i++) if (sk[i].name == prefix) return (int)i;
	for (size_t i = 0; i < sk.size(); i++) if (sk[i].name.rfind(prefix, 0) == 0) return (int)i;
	return -1;
}
static void QuatOf(const M3& m, float q[4])
{
	const float tr_ = m.m[0][0] + m.m[1][1] + m.m[2][2];
	if (tr_ > 0) { float s = std::sqrt(tr_ + 1.0f) * 2; q[0] = 0.25f * s; q[1] = (m.m[2][1] - m.m[1][2]) / s; q[2] = (m.m[0][2] - m.m[2][0]) / s; q[3] = (m.m[1][0] - m.m[0][1]) / s; }
	else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) { float s = std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2; q[0] = (m.m[2][1] - m.m[1][2]) / s; q[1] = 0.25f * s; q[2] = (m.m[0][1] + m.m[1][0]) / s; q[3] = (m.m[0][2] + m.m[2][0]) / s; }
	else if (m.m[1][1] > m.m[2][2]) { float s = std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2; q[0] = (m.m[0][2] - m.m[2][0]) / s; q[1] = (m.m[0][1] + m.m[1][0]) / s; q[2] = 0.25f * s; q[3] = (m.m[1][2] + m.m[2][1]) / s; }
	else { float s = std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2; q[0] = (m.m[1][0] - m.m[0][1]) / s; q[1] = (m.m[0][2] + m.m[2][0]) / s; q[2] = (m.m[1][2] + m.m[2][1]) / s; q[3] = 0.25f * s; }
	const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]); for (int i = 0; i < 4; i++) q[i] /= l;
}
static int RunNif(const char* modelPath, const char* skelPath, const char* animPath, const std::string& texDir, const std::string& outDir, int W, int H, const char* bakeOut = nullptr, int travel = 0)
{
	nifly::NifFile skel, model;
	if (skel.Load(skelPath) || (!bakeOut && model.Load(modelPath))) { fprintf(stderr, "nif load failed\n"); return 1; }
	// the skeleton's hierarchy, parents before children
	std::vector<SkBone> sk;
	std::function<void(nifly::NiNode*, int, const Xf&)> walk = [&](nifly::NiNode* n, int parent, const Xf& pw) {
		SkBone b; b.name = n->name.get(); b.parent = parent; b.local = fromNif(n->GetTransformToParent()); b.restWorld = compose(pw, b.local);
		sk.push_back(b);
		const int me = (int)sk.size() - 1;
		for (auto* c : skel.GetChildren<nifly::NiNode>(n)) walk(c, me, sk[me].restWorld);
	};
	walk(skel.GetRootNode(), -1, Xf{});
	// ZSTRETCH="bone prefix=factor,...": that bone stands further from its parent (a longer neck, longer arms)
	if (const char* zs = getenv("ZSTRETCH")) {
		std::string all = zs; size_t a = 0;
		while (a < all.size()) {
			size_t e = all.find(',', a); if (e == std::string::npos) e = all.size();
			std::string kv = all.substr(a, e - a); a = e + 1;
			size_t q = kv.find('='); if (q == std::string::npos) continue;
			int b = FindBone(sk, kv.substr(0, q)); if (b < 0) continue;
			sk[b].local.t = sk[b].local.t * std::stof(kv.substr(q + 1));
		}
		for (auto& b : sk) b.restWorld = b.parent >= 0 ? compose(sk[b.parent].restWorld, b.local) : b.local;  // (parents come first)
	}
	// Mixamo bone -> Skyrim bone (prefix), and the child each aims at (Mixamo, Skyrim)
	struct Map { const char* mix; const char* sky; const char* mixChild; const char* skyChild; };
	std::vector<Map> maps = {
		{ "Hips", "NPC Pelvis [Pelv]", "Spine", "NPC Spine [Spn0]" }, { "Spine", "NPC Spine [Spn0]", "Spine1", "NPC Spine1" }, { "Spine1", "NPC Spine1", "Spine2", "NPC Spine2" },
		{ "Spine2", "NPC Spine2", "Neck", "NPC Neck" }, { "Neck", "NPC Neck", "Head", "NPC Head [Head]" }, { "Head", "NPC Head [Head]", nullptr, nullptr },
	};
	static std::vector<std::string> keep;  // (string storage for the side tables)
	auto S = [&](const std::string& s) { keep.push_back(s); return keep.back().c_str(); };
	keep.reserve(400);
	for (int side = 0; side < 2; side++) {
		const std::string M = side ? "Right" : "Left", K = side ? "R" : "L";
		maps.push_back({ S(M + "Shoulder"), S("NPC " + K + " Clavicle"), S(M + "Arm"), S("NPC " + K + " UpperArm") });
		maps.push_back({ S(M + "Arm"), S("NPC " + K + " UpperArm"), S(M + "ForeArm"), S("NPC " + K + " Forearm") });
		maps.push_back({ S(M + "ForeArm"), S("NPC " + K + " Forearm"), S(M + "Hand"), S("NPC " + K + " Hand") });
		maps.push_back({ S(M + "Hand"), S("NPC " + K + " Hand"), S(M + "HandMiddle1"), S("NPC " + K + " Finger20") });
		maps.push_back({ S(M + "UpLeg"), S("NPC " + K + " Thigh"), S(M + "Leg"), S("NPC " + K + " Calf") });
		maps.push_back({ S(M + "Leg"), S("NPC " + K + " Calf"), S(M + "Foot"), S("NPC " + K + " Foot") });
		maps.push_back({ S(M + "Foot"), S("NPC " + K + " Foot"), S(M + "ToeBase"), S("NPC " + K + " Toe0") });
		const char* fing[5] = { "Thumb", "Index", "Middle", "Ring", "Pinky" };
		for (int f = 0; f < 5; f++)
			for (int j = 0; j < 3; j++)
				maps.push_back({ S(M + "Hand" + fing[f] + std::to_string(j + 1)), S("NPC " + K + " Finger" + std::to_string(f) + std::to_string(j)),
					j < 2 ? S(M + "Hand" + fing[f] + std::to_string(j + 2)) : nullptr, j < 2 ? S("NPC " + K + " Finger" + std::to_string(f) + std::to_string(j + 1)) : nullptr });
	}
	Assimp::Importer ia;
	const aiScene* as = ia.ReadFile(animPath, 0);
	if (!as) { fprintf(stderr, "anim load failed\n"); return 1; }
	Anim anim; anim.Init(as);
	std::map<std::string, aiMatrix4x4> rest;
	Worlds(as, anim, 0, true, rest);
	// ZMIRROR=1: the clip played by the other side (Left reads Right, everything reflected across the body's middle):
	// a right-handed swing becomes a left-handed one
	const bool mirror = getenv("ZMIRROR") != nullptr;
	auto mixXf = [&](const std::map<std::string, aiMatrix4x4>& w, const char* n, Xf& out) {
		std::string name = n;
		if (mirror) {
			if (name.rfind("Left", 0) == 0) name = "Right" + name.substr(4);
			else if (name.rfind("Right", 0) == 0) name = "Left" + name.substr(5);
		}
		auto it = w.find(name);
		if (it == w.end()) return false;
		Xf x = fromAi(it->second);
		out.R = mul(mul(kM, x.R), tr(kM));
		out.t = mul(kM, x.t);
		out.s = x.s;
		if (mirror) {
			M3 F; F.m[0][0] = -1;  // (Skyrim's X runs across the body)
			out.R = mul(mul(F, out.R), F);
			out.t.x = -out.t.x;
		}
		return true;
	};
	// per mapped bone: its Skyrim index, the aim correction C (Skyrim rest direction onto Mixamo rest direction)
	struct Live { int sky; const char* mix; M3 C; M3 Rm0; bool ok; };
	std::vector<Live> live;
	std::vector<int> mapOf(sk.size(), -1);
	for (auto& m : maps) {
		Live L{ FindBone(sk, m.sky), m.mix, M3{}, M3{}, false };
		Xf mr, mc;
		if (L.sky < 0 || !mixXf(rest, m.mix, mr)) continue;
		L.Rm0 = mr.R;
		if (m.mixChild && m.skyChild) {
			const int skc = FindBone(sk, m.skyChild);
			if (skc >= 0 && mixXf(rest, m.mixChild, mc)) {
				V3 dS = sk[skc].restWorld.t - sk[L.sky].restWorld.t, dM = mc.t - mr.t;
				if (len(dS) > 1e-4f && len(dM) > 1e-4f) L.C = rotBetween(dS, dM);
			}
		}
		L.ok = true;
		mapOf[L.sky] = (int)live.size();
		live.push_back(L);
	}
	// the hips' travel: scaled by the two hip heights
	Xf hip0; mixXf(rest, "Hips", hip0);
	const int pelv = FindBone(sk, "NPC Pelvis [Pelv]"), com = FindBone(sk, "NPC COM");
	float lo = 1e9f; for (auto& w : rest) { Xf x; if (mixXf(rest, w.first.c_str(), x)) lo = std::min(lo, x.t.z); }
	const float hipScale = (sk[pelv].restWorld.t.z) / std::max(1.0f, hip0.t.z - lo);
	// the model's shapes
	struct Shape { std::vector<V3> v; std::vector<float> uv; std::vector<int> tri; int tex; std::vector<int> bones; std::vector<Xf> s2b; std::vector<std::vector<std::pair<int, float>>> w; };
	std::vector<Shape> shapes; std::vector<Tex> texs; std::map<std::string, int> texIdx;
	for (auto* sh : model.GetShapes()) {
		Shape s;
		std::vector<nifly::Vector3> v; std::vector<nifly::Vector2> uv; std::vector<nifly::Triangle> t;
		if (!model.GetVertsForShape(sh, v) || !sh->GetTriangles(t)) continue;
		model.GetUvsForShape(sh, uv);
		for (auto& p : v) s.v.push_back({ p.x, p.y, p.z });
		for (size_t i = 0; i < v.size(); i++) { s.uv.push_back(i < uv.size() ? uv[i].u : 0); s.uv.push_back(i < uv.size() ? uv[i].v : 0); }
		for (auto& tt : t) { s.tri.push_back(tt.p1); s.tri.push_back(tt.p2); s.tri.push_back(tt.p3); }
		std::string tp; model.GetTextureSlot(sh, tp, 0);
		std::string base = std::filesystem::path(std::string(tp.begin(), tp.end())).filename().string();
		std::replace(tp.begin(), tp.end(), '\\', '/');
		base = std::filesystem::path(tp).stem().string();
		std::transform(base.begin(), base.end(), base.begin(), ::tolower);
		if (!texIdx.count(base)) { texIdx[base] = (int)texs.size(); texs.push_back(LoadTexFile(texDir + "/" + base + ".png")); }
		s.tex = texIdx[base];
		std::vector<std::string> bl; model.GetShapeBoneList(sh, bl);
		s.w.resize(v.size());
		for (uint32_t b = 0; b < bl.size(); b++) {
			s.bones.push_back(FindBone(sk, bl[b]));
			nifly::MatTransform x; model.GetShapeTransformSkinToBone(sh, b, x);
			s.s2b.push_back(fromNif(x));
			std::unordered_map<uint16_t, float> ws; model.GetShapeBoneWeights(sh, b, ws);
			for (auto& [vi, wt] : ws) if (vi < s.w.size()) s.w[vi].push_back({ (int)b, wt });
		}
		shapes.push_back(std::move(s));
	}
	if (!bakeOut) std::filesystem::create_directories(outDir);
	const int frames = std::max(1, (int)std::round(anim.dur));
	std::vector<std::vector<float>> bakeFrames; std::vector<V3> bakeCom;
	Cam cam;
	{
		float lo = 1e9f, hi = -1e9f;
		for (auto& s : shapes) for (auto& v : s.v) lo = std::min(lo, v.z), hi = std::max(hi, v.z);
		cam.refH = hi - lo, cam.refLo = lo;
	}
	// ZGAME="<anims dir>|<script>": play the baked clips through the game's own blending (src/ClipBlend.h) instead of
	// the FBX - script: segments "seconds@runRate@clip@rate" joined by ',' (clip: run attack punch hit hit2 scream standup)
	if (const char* zg = getenv("ZGAME"); zg && !bakeOut) {
		const std::string all = zg; const auto bar = all.find('|');
		const std::string dir = all.substr(0, bar), script = all.substr(bar + 1);
		zclip::ClipSet S;
		bool ok = true;
		for (int c = 0; c < zclip::kClips; c++) {
			FILE* f = fopen((dir + "/" + zclip::kClipFile[c] + ".zan").c_str(), "rb");
			if (!f) { ok = false; break; }
			char mg[4]; uint32_t fps, nf, nb; float sp;
			fread(mg, 1, 4, f); fread(&fps, 4, 1, f); fread(&nf, 4, 1, f); fread(&nb, 4, 1, f); fread(&sp, 4, 1, f);
			auto& C = S.c[c]; C.fps = fps, C.frames = nf, C.speed = sp; C.bones.resize(nb);
			for (auto& b : C.bones) { uint8_t l; fread(&l, 1, 1, f); b.resize(l); fread(b.data(), 1, l, f); }
			C.q.resize((size_t)nf * nb * 4); C.com.resize((size_t)nf * 3);
			fread(C.q.data(), 4, C.q.size(), f); fread(C.com.data(), 4, C.com.size(), f); fclose(f);
		}
		if (!ok) { fprintf(stderr, "clips missing in %s\n", dir.c_str()); return 1; }
		if (FILE* f = fopen((dir + "/rest.zan").c_str(), "rb"); f && !getenv("ZNOREST")) {
			char mg[4]; uint32_t fps, nf, nb; float sp;
			fread(mg, 1, 4, f); fread(&fps, 4, 1, f); fread(&nf, 4, 1, f); fread(&nb, 4, 1, f); fread(&sp, 4, 1, f);
			auto& C = S.rest; C.fps = fps, C.frames = nf; C.bones.resize(nb);
			for (auto& b : C.bones) { uint8_t l; fread(&l, 1, 1, f); b.resize(l); fread(b.data(), 1, l, f); }
			C.q.resize((size_t)nf * nb * 4); C.com.resize((size_t)nf * 3);
			fread(C.q.data(), 4, C.q.size(), f); fread(C.com.data(), 4, C.com.size(), f); fclose(f);
		}
		S.Prepare();
		std::vector<int> skOf(S.c[0].bones.size());
		for (size_t i = 0; i < skOf.size(); i++) skOf[i] = FindBone(sk, S.c[0].bones[i]);
		struct Seg { float secs, run, rate; int clip; };
		std::vector<Seg> segs;
		{
			size_t a = 0;
			while (a < script.size()) {
				size_t e = script.find(',', a); if (e == std::string::npos) e = script.size();
				std::string t = script.substr(a, e - a); a = e + 1;
				Seg g{ 1, 1, 1, 0 }; char nm[32] = "run";
				sscanf(t.c_str(), "%f@%f@%31[^@]@%f", &g.secs, &g.run, nm, &g.rate);
				for (int c = 0; c < zclip::kClips; c++) if (!strcmp(nm, zclip::kClipFile[c])) g.clip = c;
				segs.push_back(g);
			}
		}
		auto M3of = [](const zclip::Q4& q) { const float w = q[0], x = q[1], y = q[2], z = q[3]; M3 m;
			m.m[0][0] = 1 - 2 * (y * y + z * z), m.m[0][1] = 2 * (x * y - w * z), m.m[0][2] = 2 * (x * z + w * y);
			m.m[1][0] = 2 * (x * y + w * z), m.m[1][1] = 1 - 2 * (x * x + z * z), m.m[1][2] = 2 * (y * z - w * x);
			m.m[2][0] = 2 * (x * z - w * y), m.m[2][1] = 2 * (y * z + w * x), m.m[2][2] = 1 - 2 * (x * x + y * y); return m; };
		zclip::Player P; std::vector<zclip::Q4> pose; zclip::V3f cp;
		const float dt = 1.0f / 30.0f; int fi = 0;
		for (auto& g : segs) {
			const int n = std::max(1, (int)std::lround(g.secs * 30));
			for (int k = 0; k < n; k++, fi++) {
				if (k == 0 && g.clip != zclip::kRun) P.Play(g.clip, zclip::ClipRate(g.clip) * g.rate, S);
				if (P.clip < 0 || P.clip == zclip::kRun || !P.OneShotOn(S)) P.Play(zclip::kRun, g.run, S);
				P.Tick(S, dt, g.run, pose, cp);
				std::vector<Xf> loc(sk.size());
				for (size_t b = 0; b < sk.size(); b++) loc[b] = sk[b].local;
				for (size_t i = 0; i < skOf.size(); i++) if (skOf[i] >= 0) loc[skOf[i]].R = M3of(pose[i]);
				if (com >= 0) loc[com].t = { cp.x, cp.y, cp.z };
				std::vector<Xf> W_(sk.size());
				for (size_t b = 0; b < sk.size(); b++) W_[b] = sk[b].parent >= 0 ? compose(W_[sk[b].parent], loc[b]) : loc[b];
				Mesh m;
				for (auto& s : shapes) {
					const int base = (int)m.p.size();
					for (size_t v = 0; v < s.v.size(); v++) {
						V3 acc; float ws = 0;
						for (auto& [bi, wt] : s.w[v]) { const int sb = s.bones[bi]; if (sb < 0) continue; acc = acc + apply(compose(W_[sb], s.s2b[bi]), s.v[v]) * wt; ws += wt; }
						m.p.push_back(ws > 0 ? acc * (1.0f / ws) : s.v[v]);
						m.uv.push_back(s.uv[v * 2]), m.uv.push_back(s.uv[v * 2 + 1]);
					}
					for (size_t t = 0; t < s.tri.size(); t += 3) { for (int q = 0; q < 3; q++) m.tri.push_back(base + s.tri[t + q]); m.triTex.push_back(s.tex); }
				}
				char name[64]; snprintf(name, sizeof name, "/f%04d.ppm", fi);
				Render(m, texs, cam, W, H, outDir + name);
			}
		}
		printf("%d frames (game blend)\n", fi);
		return 0;
	}
	// the bones a bake writes: the mapped ones, or ZLIKE=<zan>'s exact list (so a mirrored clip lines up with the rest)
	std::vector<int> outBones;
	if (const char* like = getenv("ZLIKE")) {
		FILE* f = fopen(like, "rb");
		char mg[4]; uint32_t fps_, nf_, nb_; float sp_;
		if (f && fread(mg, 1, 4, f) == 4 && fread(&fps_, 4, 1, f) && fread(&nf_, 4, 1, f) && fread(&nb_, 4, 1, f) && fread(&sp_, 4, 1, f)) {
			for (uint32_t i = 0; i < nb_; i++) { uint8_t l = 0; if (!fread(&l, 1, 1, f)) break; std::string nm(l, ' '); if (fread(nm.data(), 1, l, f) != l) break;
				int b = -1; for (size_t k = 0; k < sk.size(); k++) if (sk[k].name == nm) b = (int)k;
				if (b < 0) { fprintf(stderr, "ZLIKE bone %s not in skeleton\n", nm.c_str()); return 1; }
				outBones.push_back(b); }
		}
		if (f) fclose(f);
	} else for (size_t b = 0; b < sk.size(); b++) if (mapOf[b] >= 0) outBones.push_back((int)b);
	for (int fi = 0; fi <= frames; fi++) {
		std::map<std::string, aiMatrix4x4> now;
		Worlds(as, anim, fi, false, now);
		std::vector<Xf> W_(sk.size());
		for (size_t b = 0; b < sk.size(); b++) {
			const Xf pw = sk[b].parent >= 0 ? W_[sk[b].parent] : Xf{};
			Xf w = compose(pw, sk[b].local);  // (unmapped: follows its parent)
			if ((int)b == com) {  // the body's travel: the hips' own, scaled by the two hip heights
				Xf hn;
				if (mixXf(now, "Hips", hn)) w.t = sk[b].restWorld.t + (hn.t - hip0.t) * hipScale;
			}
			if (mapOf[b] >= 0) {
				const Live& L = live[mapOf[b]];
				Xf mn;
				if (mixXf(now, L.mix, mn)) {
					M3 D = mul(mn.R, tr(L.Rm0));
					w.R = mul(mul(D, L.C), sk[b].restWorld.R);
					w.s = sk[b].restWorld.s;
					if ((int)b == pelv) {
						// (its travel is given to the COM above it, which carries the spine too - below)
						// (the hips' own rest turn is not the animation's: the pelvis is set from the body's frame - up the
						// spine, across the hips - in the animation now, against the same frame of the Skyrim rest pose)
						Xf s1, lu, ru;
						const int ss = FindBone(sk, "NPC Spine1"), lt = FindBone(sk, "NPC L Thigh"), rt = FindBone(sk, "NPC R Thigh");
						if (mixXf(now, "Spine1", s1) && mixXf(now, "LeftUpLeg", lu) && mixXf(now, "RightUpLeg", ru) && ss >= 0 && lt >= 0 && rt >= 0) {
							auto frame = [](V3 up, V3 side) { up = norm(up); V3 fw = norm(cross(up, side)); V3 sd = cross(fw, up); M3 F; F.m[0][0] = sd.x, F.m[1][0] = sd.y, F.m[2][0] = sd.z; F.m[0][1] = fw.x, F.m[1][1] = fw.y, F.m[2][1] = fw.z; F.m[0][2] = up.x, F.m[1][2] = up.y, F.m[2][2] = up.z; return F; };
							M3 Fa = frame(s1.t - mn.t, ru.t - lu.t);
							M3 Fs = frame(sk[ss].restWorld.t - sk[b].restWorld.t, sk[rt].restWorld.t - sk[lt].restWorld.t);
							w.R = mul(mul(Fa, tr(Fs)), sk[b].restWorld.R);
							if (getenv("ZDBG") && fi == 12) { V3 u = norm(s1.t - mn.t); printf("pelvis frame: up %.2f %.2f %.2f\n", u.x, u.y, u.z);
								V3 ls = sk[ss].restWorld.t - sk[b].restWorld.t; V3 lsl = mul(tr(sk[b].restWorld.R), ls); V3 nd = norm(mul(w.R, lsl)); printf("  pelvis->spine1 now %.2f %.2f %.2f parent %d %s\n", nd.x, nd.y, nd.z, sk[ss].parent, sk[sk[ss].parent].name.c_str());
								int s0 = FindBone(sk, "NPC Spine [Spn0]"); printf("  spine0 parent %s\n", sk[sk[s0].parent].name.c_str()); }
						}
					}
				}
			}
			W_[b] = w;
		}
		if (getenv("ZDBG") && (fi == 0 || fi == 12)) {
			for (const char* nm : { "LeftUpLeg", "LeftLeg", "Hips" }) {
				Xf a, c; mixXf(now, nm, a);
				const char* ch = !strcmp(nm, "LeftUpLeg") ? "LeftLeg" : !strcmp(nm, "LeftLeg") ? "LeftFoot" : "Spine";
				mixXf(now, ch, c);
				V3 d = norm(c.t - a.t);
				printf("f%d mix %s dir %.2f %.2f %.2f pos %.1f %.1f %.1f\n", fi, nm, d.x, d.y, d.z, a.t.x, a.t.y, a.t.z);
			}
			for (const char* nm : { "NPC L Thigh", "NPC L Calf", "NPC Pelvis [Pelv]" }) {
				int b = FindBone(sk, nm); int c = FindBone(sk, !strcmp(nm, "NPC L Thigh") ? "NPC L Calf" : !strcmp(nm, "NPC L Calf") ? "NPC L Foot" : "NPC Spine [Spn0]");
				V3 d = norm(W_[c].t - W_[b].t);
				printf("f%d sky %s dir %.2f %.2f %.2f pos %.1f %.1f %.1f\n", fi, nm, d.x, d.y, d.z, W_[b].t.x, W_[b].t.y, W_[b].t.z);
			}
		}
		if (getenv("ZPOS")) {
			for (const char* nm : { "NPC L Hand", "NPC R Hand", "NPC L Forearm", "NPC R Forearm" }) { int b = FindBone(sk, nm); printf("%d %s %.1f %.1f %.1f\n", fi, nm, W_[b].t.x, W_[b].t.y, W_[b].t.z); }
		}
		if (bakeOut) {
			bakeFrames.push_back({});
			auto& fr = bakeFrames.back();
			for (int b : outBones) {
				const M3 pr = sk[b].parent >= 0 ? W_[sk[b].parent].R : M3{};
				float q[4]; QuatOf(mul(tr(pr), W_[b].R), q);
				fr.insert(fr.end(), q, q + 4);
			}
			const Xf cl = com >= 0 ? (sk[com].parent >= 0 ? compose(inverse(W_[sk[com].parent]), W_[com]) : W_[com]) : Xf{};
			bakeCom.push_back(cl.t);
			continue;
		}
		Mesh m;
		for (auto& s : shapes) {
			const int base = (int)m.p.size();
			for (size_t v = 0; v < s.v.size(); v++) {
				V3 acc; float ws = 0;
				for (auto& [bi, wt] : s.w[v]) {
					const int sb = s.bones[bi];
					if (sb < 0) continue;
					acc = acc + apply(compose(W_[sb], s.s2b[bi]), s.v[v]) * wt;
					ws += wt;
				}
				m.p.push_back(ws > 0 ? acc * (1.0f / ws) : s.v[v]);
				m.uv.push_back(s.uv[v * 2]), m.uv.push_back(s.uv[v * 2 + 1]);
			}
			for (size_t t = 0; t < s.tri.size(); t += 3) {
				for (int k = 0; k < 3; k++) m.tri.push_back(base + s.tri[t + k]);
				m.triTex.push_back(s.tex);
			}
		}
		char name[64]; snprintf(name, sizeof name, "/f%04d.ppm", fi);
		Render(m, texs, cam, W, H, outDir + name);
	}
	if (bakeOut && !bakeFrames.empty()) {
		// the body's travel: a run loops in place (its drift across the ground taken out, the speed kept for matching the
		// pace in game); other clips keep a little of it (a lunge), never more than a stride
		const int n = (int)bakeCom.size();
		const V3 c0 = bakeCom.front(), c1 = bakeCom.back();
		const float secs = std::max(1.0f / 30.0f, (n - 1) / (float)anim.tps);
		const float speed = std::hypot(c1.x - c0.x, c1.y - c0.y) / secs;
		const V3 rest = sk[com].local.t;
		for (int i = 0; i < n; i++) {
			V3& c = bakeCom[i];
			const float u = n > 1 ? (float)i / (n - 1) : 0;
			if (travel == 0) { c.x -= (c1.x - c0.x) * u; c.y -= (c1.y - c0.y) * u; c.x += rest.x - c0.x; c.y += rest.y - c0.y; }
			else { float dx = c.x - rest.x, dy = c.y - rest.y, l = std::hypot(dx, dy); if (l > 30.0f) { c.x = rest.x + dx * 30.0f / l; c.y = rest.y + dy * 30.0f / l; } }
		}
		FILE* o = fopen(bakeOut, "wb");
		const uint32_t fps = (uint32_t)std::lround(anim.tps), nf = (uint32_t)n;
		std::vector<std::string> names; for (int b : outBones) names.push_back(sk[b].name);
		const uint32_t nb = (uint32_t)names.size();
		fwrite("ZAN1", 1, 4, o); fwrite(&fps, 4, 1, o); fwrite(&nf, 4, 1, o); fwrite(&nb, 4, 1, o); fwrite(&speed, 4, 1, o);
		for (auto& nm : names) { uint8_t l = (uint8_t)nm.size(); fwrite(&l, 1, 1, o); fwrite(nm.data(), 1, l, o); }
		for (auto& fr : bakeFrames) fwrite(fr.data(), 4, fr.size(), o);
		for (auto& c : bakeCom) fwrite(&c.x, 4, 3, o);
		fclose(o);
		printf("baked %u frames, %u bones, travel %.0f units/s -> %s\n", nf, nb, speed, bakeOut);
		return 0;
	}
	printf("%d frames\n", frames + 1);
	return 0;
}

// rest <skeleton.nif> <like.zan> <out.zan>: one frame, each of like.zan's bones at its bind rotation (the game's "straight")
static int RunRest(const char* skelPath, const char* likePath, const char* outPath)
{
	nifly::NifFile skel; if (skel.Load(skelPath)) return 1;
	std::vector<SkBone> sk;
	std::function<void(nifly::NiNode*, int, const Xf&)> walk = [&](nifly::NiNode* n, int parent, const Xf& pw) {
		SkBone b; b.name = n->name.get(); b.parent = parent; b.local = fromNif(n->GetTransformToParent()); b.restWorld = compose(pw, b.local);
		sk.push_back(b); const int me = (int)sk.size() - 1;
		for (auto* c : skel.GetChildren<nifly::NiNode>(n)) walk(c, me, sk[me].restWorld);
	};
	walk(skel.GetRootNode(), -1, Xf{});
	FILE* f = fopen(likePath, "rb"); if (!f) return 1;
	char mg[4]; uint32_t fps, nf, nb; float sp;
	fread(mg, 1, 4, f); fread(&fps, 4, 1, f); fread(&nf, 4, 1, f); fread(&nb, 4, 1, f); fread(&sp, 4, 1, f);
	std::vector<std::string> names(nb); for (auto& b : names) { uint8_t l; fread(&l, 1, 1, f); b.resize(l); fread(b.data(), 1, l, f); }
	fclose(f);
	FILE* o = fopen(outPath, "wb"); const uint32_t one = 1; const float z = 0;
	fwrite("ZAN1", 1, 4, o); fwrite(&fps, 4, 1, o); fwrite(&one, 4, 1, o); fwrite(&nb, 4, 1, o); fwrite(&z, 4, 1, o);
	for (auto& nm : names) { uint8_t l = (uint8_t)nm.size(); fwrite(&l, 1, 1, o); fwrite(nm.data(), 1, l, o); }
	for (auto& nm : names) { const int b = FindBone(sk, nm); float q[4] = { 1, 0, 0, 0 }; if (b >= 0) QuatOf(sk[b].local.R, q); fwrite(q, 4, 4, o); }
	const int com = FindBone(sk, "NPC COM"); V3 c = com >= 0 ? sk[com].local.t : V3{}; fwrite(&c.x, 4, 3, o);
	fclose(o); printf("rest %u bones -> %s\n", nb, outPath); return 0;
}
int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: zprev mix|nif ...\n"); return 1; }
	const std::string mode = argv[1];
	if (mode == "mix" && argc >= 5) return RunMix(argv[2], argv[3], argv[4], argc > 6 ? atoi(argv[5]) : 480, argc > 6 ? atoi(argv[6]) : 640);
	if (mode == "rest" && argc >= 5) return RunRest(argv[2], argv[3], argv[4]);
	if (mode == "bake" && argc >= 5) return RunNif("", argv[2], argv[3], "", "", 0, 0, argv[4], argc > 5 ? atoi(argv[5]) : 0);  // bake <skeleton.nif> <anim.fbx> <out.zan> [travel]
	if (mode == "nif" && argc >= 7) return RunNif(argv[2], argv[3], argv[4], argv[5], argv[6], argc > 8 ? atoi(argv[7]) : 480, argc > 8 ? atoi(argv[8]) : 640);
	fprintf(stderr, "bad args\n");
	return 1;
}
