// weaponwrap <template.nif> <in.nif> <out.nif> <rootName> <hull.txt|-> [invRx invRy invRz zoom] [mass]
// weaponwrap dump <nif>
// A melee weapon's model put in a vanilla weapon's frame (e.g. Skyrim's steelgreatsword.nif): its BSFadeNode root, BSX
// flags, Prn (where it hangs sheathed) and its havok body (weapon layer, a dynamic body, so it falls and rests when
// dropped) are kept; the template's own shapes are thrown away and ours cloned in; the body's convex hull is replaced by
// the one in hull.txt (game units: "v x y z" corners and "p nx ny nz px py pz" outward face planes through a point), its
// centre of mass moved to the hull's middle; a BSInvMarker (inventory / item-preview pose) set or added.
#include "NifFile.hpp"
#include "bhk.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cmath>
using namespace nifly;
static constexpr float kHavok = 69.99124f;

static int dump(const char* path)
{
	NifFile nif;
	if (nif.Load(path)) { fprintf(stderr, "cannot load %s\n", path); return 1; }
	auto& h = nif.GetHeader();
	auto* root = nif.GetRootNode();
	printf("root %s '%s' collisionRef %d\n", root->GetBlockName(), root->name.get().c_str(), (int)root->collisionRef.index);
	for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
		if (auto* c = h.GetBlock<bhkCollisionObject>(i)) printf("[%u] bhkCollisionObject flags %u target %d body %d\n", i, c->flags, (int)c->targetRef.index, (int)c->bodyRef.index);
		if (auto* b = h.GetBlock<bhkRigidBody>(i)) {
			printf("[%u] %s layer %u flagsAndPart %u group %u broadphase %u response %u\n", i, b->GetBlockName(), b->collisionFilter.layer, b->collisionFilter.flagsAndParts, b->collisionFilter.group, b->broadPhaseType, (unsigned)b->collisionResponse);
			printf("   translation %.4f %.4f %.4f  rot %.3f %.3f %.3f %.3f\n", b->translation.x, b->translation.y, b->translation.z, b->rotation.x, b->rotation.y, b->rotation.z, b->rotation.w);
			printf("   center %.4f %.4f %.4f mass %.2f lin %.3f ang %.3f friction %.3f restitution %.3f maxLin %.2f maxAng %.2f pen %.3f\n", b->center.x, b->center.y, b->center.z, b->mass, b->linearDamping, b->angularDamping, b->friction, b->restitution, b->maxLinearVelocity, b->maxAngularVelocity, b->penetrationDepth);
			printf("   motion %u deact %u solverDeact %u quality %u bodyFlags %u\n   inertia", b->motionSystem, b->deactivatorType, b->solverDeactivation, b->qualityType, b->bodyFlags);
			for (float f : b->inertiaMatrix) printf(" %.4f", f);
			printf("\n");
		}
		if (auto* s = h.GetBlock<bhkConvexVerticesShape>(i)) {
			printf("[%u] bhkConvexVerticesShape radius %.4f material %u verts %zu normals %zu\n", i, s->radius, s->GetMaterial(), s->verts.size(), s->normals.size());
			for (auto& v : s->verts) printf("   v %.3f %.3f %.3f  (game %.1f %.1f %.1f)\n", v.x, v.y, v.z, v.x * kHavok, v.y * kHavok, v.z * kHavok);
			for (auto& n : s->normals) printf("   n %.3f %.3f %.3f %.4f\n", n.x, n.y, n.z, n.w);
		}
		if (auto* x = h.GetBlock<BSXFlags>(i)) printf("[%u] BSX %u\n", i, x->integerData);
		if (auto* inv = h.GetBlock<BSInvMarker>(i)) printf("[%u] InvMarker %u %u %u zoom %.2f\n", i, inv->rotationX, inv->rotationY, inv->rotationZ, inv->zoom);
		if (auto* s = h.GetBlock<NiStringExtraData>(i)) printf("[%u] string '%s' = '%s'\n", i, s->name.get().c_str(), s->stringData.get().c_str());
	}
	for (auto& r : root->extraDataRefs) if (auto* e = h.GetBlock<NiExtraData>(r)) printf("root extra: %s '%s'\n", e->GetBlockName(), e->name.get().c_str());
	return 0;
}

int main(int argc, char** argv)
{
	if (argc >= 3 && !strcmp(argv[1], "dump")) return dump(argv[2]);
	if (argc < 6) { fprintf(stderr, "usage: weaponwrap <template.nif> <in.nif> <out.nif> <rootName> <hull.txt|-> [rx ry rz zoom] [mass]\n"); return 1; }
	NifFile nif, src;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load template %s\n", argv[1]); return 1; }
	if (src.Load(argv[2])) { fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }
	auto& h = nif.GetHeader();
	auto* root = nif.GetRootNode();
	root->name.get() = argv[4];
	// the template's own shapes out, and anything only they used
	for (auto* s : nif.GetShapes()) nif.DeleteShape(s);
	nif.DeleteUnreferencedBlocks();
	// ours in, under the root, as they were
	int n = 0;
	for (auto* s : src.GetShapes()) {
		auto* c = nif.CloneShape(s, s->name.get(), &src);
		if (c) ++n;
	}
	// the hull
	if (strcmp(argv[5], "-") != 0) {
		std::ifstream in(argv[5]);
		std::vector<Vector4> verts, planes;
		std::string line;
		double cx = 0, cy = 0, cz = 0;
		while (std::getline(in, line)) {
			std::istringstream ss(line);
			std::string k;
			ss >> k;
			if (k == "v") { float x, y, z; ss >> x >> y >> z; verts.emplace_back(x / kHavok, y / kHavok, z / kHavok, 0.0f); cx += x, cy += y, cz += z; }
			else if (k == "p") {
				float nx, ny, nz, px, py, pz; ss >> nx >> ny >> nz >> px >> py >> pz;
				const float L = std::sqrt(nx * nx + ny * ny + nz * nz);
				nx /= L, ny /= L, nz /= L;
				planes.emplace_back(nx, ny, nz, -(nx * px + ny * py + nz * pz) / kHavok);
			}
		}
		if (verts.empty() || planes.empty()) { fprintf(stderr, "empty hull\n"); return 1; }
		cx /= verts.size(), cy /= verts.size(), cz /= verts.size();
		bhkConvexVerticesShape* cv = nullptr;
		bhkRigidBody* body = nullptr;
		for (uint32_t i = 0; i < h.GetNumBlocks(); ++i) {
			if (auto* s = h.GetBlock<bhkConvexVerticesShape>(i)) cv = s;
			if (auto* b = h.GetBlock<bhkRigidBody>(i)) body = b;
		}
		if (!cv || !body) { fprintf(stderr, "template has no convex body\n"); return 1; }
		cv->verts.clear();
		for (auto& v : verts) cv->verts.push_back(v);
		cv->normals.clear();
		for (auto& p : planes) cv->normals.push_back(p);
		body->center = Vector4(cx / kHavok, cy / kHavok, cz / kHavok, 0.0f);
		if (argc >= 11) {
			const float m = std::atof(argv[10]);
			const float k = m / std::max(body->mass, 0.01f);
			for (float& f : body->inertiaMatrix) f *= k;
			body->mass = m;
		}
		// inertia of a box of the hull's extent (havok units), mass already set
		Vector3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
		for (auto& v : verts) { lo.x = std::min(lo.x, v.x), lo.y = std::min(lo.y, v.y), lo.z = std::min(lo.z, v.z), hi.x = std::max(hi.x, v.x), hi.y = std::max(hi.y, v.y), hi.z = std::max(hi.z, v.z); }
		const float a = hi.x - lo.x, b = hi.y - lo.y, c = hi.z - lo.z, m = body->mass;
		std::fill(std::begin(body->inertiaMatrix), std::end(body->inertiaMatrix), 0.0f);
		body->inertiaMatrix[0] = m / 12.0f * (b * b + c * c);
		body->inertiaMatrix[5] = m / 12.0f * (a * a + c * c);
		body->inertiaMatrix[10] = m / 12.0f * (a * a + b * b);
		printf("hull: %zu corners %zu planes, centre of mass %.1f %.1f %.1f, mass %.1f\n", verts.size(), planes.size(), cx, cy, cz, body->mass);
	}
	if (argc >= 10) {
		BSInvMarker* inv = nullptr;
		for (auto& r : root->extraDataRefs) if (auto* e = h.GetBlock<BSInvMarker>(r)) inv = e;
		if (!inv) {
			auto m = std::make_unique<BSInvMarker>();
			m->name.get() = "INV";
			nif.AssignExtraData(root, std::move(m));
			for (auto& r : root->extraDataRefs) if (auto* e = h.GetBlock<BSInvMarker>(r)) inv = e;
		}
		inv->rotationX = (uint16_t)std::atoi(argv[6]);
		inv->rotationY = (uint16_t)std::atoi(argv[7]);
		inv->rotationZ = (uint16_t)std::atoi(argv[8]);
		inv->zoom = std::atof(argv[9]);
	}
	if (nif.Save(argv[3])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("wrote %s (%d shapes)\n", argv[3], n);
	return 0;
}
