// modelconv <job.txt>: convert an FBX/DAE/OBJ model to a Skyrim SE NIF (assimp in, nifly out).
// Job file, one directive per line:
//   in <model>            out <nif>
//   scale <s>             rotate <xdeg> <ydeg> <zdeg>      (applied in that order, after scale)
//   ground 1|0            (1: put the lowest point at z=0 and centre x/y; 0: centre the bounds)
//   offset <x> <y> <z>    (after centring)
//   skip <meshName>
//   mat <materialName|*> diffuse=<tex> normal=<tex> glow=<tex> alpha=<0|1|blend> doublesided=<0|1>
//                         emit=<r>,<g>,<b> emitmult=<f> spec=<f> gloss=<f>
// Textures are written as given (paths relative to Data\Textures, e.g. UndyingNights\book\fire_d.dds).
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include "NifFile.hpp"
#include "bhk.hpp"
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <functional>
using namespace nifly;

struct Mat
{
	std::string diffuse, normal, glow, alpha = "0";
	bool        doubleSided = false;
	Vector3     emit{ 1, 1, 1 };
	float       emitMult = 1, spec = 0.3f, gloss = 40;
};

int main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: modelconv job.txt\n");
		return 2;
	}
	std::ifstream jf(argv[1]);
	std::string   in, out, line;
	float         scale = 1, rx = 0, ry = 0, rz = 0, ox = 0, oy = 0, oz = 0;
	bool          ground = false;
	bool          collide = false;  // add a static box collision (placed furniture)
	std::string   prn;
	bool          keepOrigin = false;  // authored in place: don't recentre
	bool          boxSet = false;      // collidebox: explicit collision bounds (model units, after transforms)
	Vector3       boxLo, boxHi;              // weapons: attach point (Prn extra data), e.g. WeaponStaff
	float         fit = 0;  // if set: scale so the longest side is this many units
	std::set<std::string>      skip;
	std::map<std::string, aiVector3D> meshOffset;  // final-units nudge per source mesh (e.g. a lid resting open)
	std::map<std::string, Mat> mats;
	while (std::getline(jf, line)) {
		std::istringstream ss(line);
		std::string        k;
		ss >> k;
		if (k == "in") std::getline(ss >> std::ws, in);
		else if (k == "out") std::getline(ss >> std::ws, out);
		else if (k == "scale") ss >> scale;
		else if (k == "rotate") ss >> rx >> ry >> rz;
		else if (k == "offset") ss >> ox >> oy >> oz;
		else if (k == "ground") ss >> ground;
		else if (k == "collide") ss >> collide;
		else if (k == "prn") ss >> prn;
		else if (k == "origin") ss >> keepOrigin;
		else if (k == "collidebox") { ss >> boxLo.x >> boxLo.y >> boxLo.z >> boxHi.x >> boxHi.y >> boxHi.z; boxSet = collide = true; }
		else if (k == "fit") ss >> fit;
		else if (k == "meshoffset") { std::string n; float x, y, z; ss >> n >> x >> y >> z; meshOffset[n] = { x, y, z }; }
		else if (k == "skip") { std::string n; std::getline(ss >> std::ws, n); skip.insert(n); }
		else if (k == "mat") {
			std::string name, kv;
			ss >> name;
			Mat m;
			while (ss >> kv) {
				auto eq = kv.find('=');
				auto key = kv.substr(0, eq), val = kv.substr(eq + 1);
				if (key == "diffuse") m.diffuse = val;
				else if (key == "normal") m.normal = val;
				else if (key == "glow") m.glow = val;
				else if (key == "alpha") m.alpha = val;
				else if (key == "doublesided") m.doubleSided = val == "1";
				else if (key == "emit") sscanf(val.c_str(), "%f,%f,%f", &m.emit.x, &m.emit.y, &m.emit.z);
				else if (key == "emitmult") m.emitMult = std::stof(val);
				else if (key == "spec") m.spec = std::stof(val);
				else if (key == "gloss") m.gloss = std::stof(val);
			}
			mats[name] = m;
		}
	}

	Assimp::Importer imp;
	const aiScene*   sc = imp.ReadFile(in, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
	                                           aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs | aiProcess_ImproveCacheLocality);
	if (!sc) {
		fprintf(stderr, "load failed: %s\n", imp.GetErrorString());
		return 1;
	}
	std::function<aiMatrix4x4(const aiNode*)> globalOf = [&](const aiNode* n) { return n->mParent ? globalOf(n->mParent) * n->mTransformation : n->mTransformation; };
	std::map<const aiMesh*, aiMatrix4x4> meshGlobal;
	std::function<void(const aiNode*)> findMeshes = [&](const aiNode* n) {
		for (unsigned i = 0; i < n->mNumMeshes; ++i) meshGlobal[sc->mMeshes[n->mMeshes[i]]] = globalOf(n);
		for (unsigned c = 0; c < n->mNumChildren; ++c) findMeshes(n->mChildren[c]);
	};
	findMeshes(sc->mRootNode);
	// Transform: scale, rotate X then Y then Z (degrees).
	auto rot = [&](aiVector3D p) {
		const float d = 3.14159265f / 180.0f;
		float       c, s, y, z, x;
		c = cosf(rx * d), s = sinf(rx * d), y = p.y * c - p.z * s, z = p.y * s + p.z * c, p.y = y, p.z = z;
		c = cosf(ry * d), s = sinf(ry * d), x = p.x * c + p.z * s, z = -p.x * s + p.z * c, p.x = x, p.z = z;
		c = cosf(rz * d), s = sinf(rz * d), x = p.x * c - p.y * s, y = p.x * s + p.y * c, p.x = x, p.y = y;
		return p;
	};
	Vector3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
	for (unsigned m = 0; m < sc->mNumMeshes; ++m) {
		if (skip.count(sc->mMeshes[m]->mName.C_Str())) continue;
		for (unsigned v = 0; v < sc->mMeshes[m]->mNumVertices; ++v) {
			auto p = rot((meshGlobal[sc->mMeshes[m]] * sc->mMeshes[m]->mVertices[v]) * scale);
			lo.x = std::min(lo.x, p.x), lo.y = std::min(lo.y, p.y), lo.z = std::min(lo.z, p.z);
			hi.x = std::max(hi.x, p.x), hi.y = std::max(hi.y, p.y), hi.z = std::max(hi.z, p.z);
		}
	}
	if (fit > 0) {
		const float longest = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z });
		const float k = fit / std::max(1e-6f, longest);
		scale *= k, lo = lo * k, hi = hi * k;
	}
	const Vector3 shift = keepOrigin ? Vector3(ox, oy, oz) : Vector3(-(lo.x + hi.x) / 2 + ox, -(lo.y + hi.y) / 2 + oy, (ground ? -lo.z : -(lo.z + hi.z) / 2) + oz);

	NifFile nif;
	nif.Create(NiVersion::getSSE());
	{
		// Statics in Skyrim are rooted on a BSFadeNode (distance fade, LOD); keep the "Scene Root" name.
		auto fade = std::make_unique<BSFadeNode>();
		fade->name.get() = "Scene Root";
		nif.GetHeader().ReplaceBlock(nif.GetBlockID(nif.GetRootNode()), std::move(fade));
	}
	Vector3 bmin(FLT_MAX, FLT_MAX, FLT_MAX), bmax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	int n = 0;
	for (unsigned m = 0; m < sc->mNumMeshes; ++m) {
		const aiMesh* me = sc->mMeshes[m];
		if (skip.count(me->mName.C_Str())) continue;
		aiString matName;
		sc->mMaterials[me->mMaterialIndex]->Get(AI_MATKEY_NAME, matName);
		Mat mat = mats.count(matName.C_Str()) ? mats[matName.C_Str()] : mats["*"];
		// split meshes over 65535 vertices is unnecessary for these models; guard anyway
		if (me->mNumVertices > 65535) {
			fprintf(stderr, "mesh %s too big (%u verts)\n", me->mName.C_Str(), me->mNumVertices);
			return 1;
		}
		std::vector<Vector3>  verts, norms;
		std::vector<Vector2>  uvs;
		std::vector<Triangle> tris;
		for (unsigned v = 0; v < me->mNumVertices; ++v) {
			auto p = rot((meshGlobal[me] * me->mVertices[v]) * scale);
			aiVector3D mo = meshOffset.count(me->mName.C_Str()) ? meshOffset[me->mName.C_Str()] : aiVector3D(0, 0, 0);
			verts.emplace_back(p.x + shift.x + mo.x, p.y + shift.y + mo.y, p.z + shift.z + mo.z);
			const auto& vv = verts.back();
			bmin = Vector3(std::min(bmin.x, vv.x), std::min(bmin.y, vv.y), std::min(bmin.z, vv.z));
			bmax = Vector3(std::max(bmax.x, vv.x), std::max(bmax.y, vv.y), std::max(bmax.z, vv.z));
			auto nn = me->HasNormals() ? rot(aiMatrix3x3(meshGlobal[me]) * me->mNormals[v]) : aiVector3D(0, 0, 1);
			nn.Normalize();
			norms.emplace_back(nn.x, nn.y, nn.z);
			uvs.emplace_back(me->HasTextureCoords(0) ? me->mTextureCoords[0][v].x : 0.0f, me->HasTextureCoords(0) ? me->mTextureCoords[0][v].y : 0.0f);
		}
		for (unsigned f = 0; f < me->mNumFaces; ++f)
			if (me->mFaces[f].mNumIndices == 3)
				tris.emplace_back(me->mFaces[f].mIndices[0], me->mFaces[f].mIndices[1], me->mFaces[f].mIndices[2]);
		const std::string name = std::string(me->mName.C_Str()) + ":" + std::to_string(n++);
		NiShape*          shape = nif.CreateShapeFromData(name, &verts, &tris, &uvs, &norms);
		nif.CalcTangentsForShape(shape);
		std::string t0 = mat.diffuse, t1 = mat.normal, t2 = mat.glow;
		if (!t0.empty()) nif.SetTextureSlot(shape, t0, 0);
		if (!t1.empty()) nif.SetTextureSlot(shape, t1, 1);
		auto* sh = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(shape));
		if (sh) {
			sh->shaderFlags1 = SLSF1_SPECULAR | SLSF1_RECEIVE_SHADOWS | SLSF1_CAST_SHADOWS | SLSF1_REMAPPABLE_TEXTURES | SLSF1_ZBUFFER_TEST;
			sh->shaderFlags2 = SLSF2_ZBUFFER_WRITE;
			if (mat.doubleSided) sh->shaderFlags2 |= SLSF2_DOUBLE_SIDED;
			sh->specularStrength = mat.spec;
			sh->glossiness = mat.gloss;
			if (!t2.empty()) {
				nif.SetTextureSlot(shape, t2, 2);
				sh->SetShaderType(BSLSP_GLOWMAP);
				sh->shaderFlags2 |= SLSF2_GLOW_MAP;
				sh->emissiveColor = mat.emit;
				sh->emissiveMultiple = mat.emitMult;
			}
		}
		if (mat.alpha != "0") {
			auto a = std::make_unique<NiAlphaProperty>();
			a->flags = mat.alpha == "blend" ? 0x00ED : 0x12EC;  // blend: src alpha / inv src alpha; test: alpha test >= threshold
			a->threshold = 96;
			nif.AssignAlphaProperty(shape, std::move(a));
		}
	}
	auto* root = nif.GetRootNode();
	root->name.get() = "Scene Root";
	if (boxSet) {
		bmin = boxLo, bmax = boxHi;
	}
	if (collide && n > 0) {
		// One fixed box around the whole model on the static layer: blocks the player and takes the crosshair.
		constexpr float kHavok = 69.99124f;
		auto box = std::make_unique<bhkBoxShape>();
		box->SetMaterial(500811281);  // SKY_HAV_MAT_WOOD
		box->radius = 0.01f;
		box->dimensions = Vector3((bmax.x - bmin.x) / 2 / kHavok, (bmax.y - bmin.y) / 2 / kHavok, (bmax.z - bmin.z) / 2 / kHavok);
		const uint32_t boxId = nif.GetHeader().AddBlock(std::move(box));
		auto body = std::make_unique<bhkRigidBodyT>();
		body->shapeRef.index = boxId;
		body->collisionFilter.layer = 1;  // SKYL_STATIC
		body->collisionFilterCopy = body->collisionFilter;
		body->broadPhaseType = 1;
		body->collisionResponse = RESPONSE_SIMPLE_CONTACT;
		body->translation = Vector4((bmin.x + bmax.x) / 2 / kHavok, (bmin.y + bmax.y) / 2 / kHavok, (bmin.z + bmax.z) / 2 / kHavok, 0);
		body->rotation = QuaternionXYZW{ 0, 0, 0, 1 };
		body->mass = 0;
		body->motionSystem = 7;     // MO_SYS_FIXED
		body->deactivatorType = 1;  // never
		body->solverDeactivation = 1;
		body->qualityType = 1;      // MO_QUAL_FIXED
		const uint32_t bodyId = nif.GetHeader().AddBlock(std::move(body));
		auto col = std::make_unique<bhkCollisionObject>();
		col->flags = 1;
		col->targetRef.index = nif.GetBlockID(root);
		col->bodyRef.index = bodyId;
		root->collisionRef.index = nif.GetHeader().AddBlock(std::move(col));
		auto bsx = std::make_unique<BSXFlags>();
		bsx->name.get() = "BSX";
		bsx->integerData = 2;  // havok
		nif.AssignExtraData(root, std::move(bsx));
	}
	if (!prn.empty()) {
		// Without Prn the engine has nowhere to hang the weapon: it ends up at the actor's root (by the feet).
		auto x = std::make_unique<NiStringExtraData>();
		x->name.get() = "Prn";
		x->stringData.get() = prn;
		nif.AssignExtraData(root, std::move(x));
	}
	if (nif.Save(out)) {
		fprintf(stderr, "save failed\n");
		return 1;
	}
	printf("wrote %s: %d shapes, bounds %.1f x %.1f x %.1f\n", out.c_str(), n, hi.x - lo.x, hi.y - lo.y, hi.z - lo.z);
}
