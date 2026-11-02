// spinwrap <in.nif> <out.nif> <nodeName> <ax> <ay> <az> <turnsPerSecond> [keysPerTurn]
// A static model (e.g. modelconv's output) made to spin forever: every shape under the root is moved under a new NiNode
// <nodeName> (a child of the root, at the origin), and that node turns about the axis (ax, ay, az) through the origin
// <turnsPerSecond> whole turns a second - a NiTransformController (active, looping: flags 0x48, running from load) on a
// NiTransformInterpolator of linear quaternion keys, one turn per cycle. The NIF is flagged animated (BSXFlags bit 0),
// the same free-running controller setup as tools/modelconv/nifanim.cpp and fxwrite.cpp. Nothing else is touched
// (no collision, no Prn) - for projectile (PROJ) models such as the Ranger's throwing dagger (tools/throwdagger.py).
#include "NifFile.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace nifly;

int main(int argc, char** argv)
{
	if (argc < 8) { fprintf(stderr, "usage: spinwrap <in.nif> <out.nif> <node> <ax> <ay> <az> <turnsPerSecond> [keysPerTurn]\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	auto& hdr = nif.GetHeader();
	auto* root = nif.GetRootNode();
	float ax = std::atof(argv[4]), ay = std::atof(argv[5]), az = std::atof(argv[6]);
	const float tps = std::atof(argv[7]);
	const int keys = argc > 8 ? std::max(4, std::atoi(argv[8])) : 24;
	const float l = std::sqrt(ax * ax + ay * ay + az * az);
	if (l <= 0 || tps <= 0) { fprintf(stderr, "bad axis or rate\n"); return 1; }
	ax /= l, ay /= l, az /= l;
	// the spinning node, and every shape moved under it (their own transforms kept: the node sits at the origin)
	MatTransform none;
	auto* spin = nif.AddNode(argv[3], none, root);
	int moved = 0;
	for (auto* s : nif.GetShapes()) {
		if (nif.GetParentNode(s) != root) continue;
		nif.SetParentNode(s, spin);
		++moved;
	}
	// one whole turn per cycle: keys every 360/keys degrees (slerp between neighbours always takes the short way, so
	// with >= 4 keys a turn the rotation runs steadily one way; the last key, q(2pi) = -q(0), is the first again)
	const float period = 1.0f / tps;
	auto data = std::make_unique<NiTransformData>();
	data->rotationType = LINEAR_KEY;
	for (int i = 0; i <= keys; ++i) {
		const float a = 6.2831853f * i / keys, s = std::sin(a * 0.5f);
		NiAnimationKey<Quaternion> q;
		q.type = LINEAR_KEY;
		q.time = period * i / keys;
		q.value = Quaternion(std::cos(a * 0.5f), ax * s, ay * s, az * s);
		data->quaternionKeys.push_back(q);
	}
	const auto dataRef = hdr.AddBlock(std::move(data));
	auto ip = std::make_unique<NiTransformInterpolator>();
	ip->translation = Vector3();
	ip->rotation = Quaternion();
	ip->scale = 1.0f;
	ip->dataRef.index = dataRef;
	const auto ipRef = hdr.AddBlock(std::move(ip));
	auto ctrl = std::make_unique<NiTransformController>();
	ctrl->flags = 0x48;  // active, looping (cycle)
	ctrl->frequency = 1.0f;
	ctrl->phase = 0.0f;
	ctrl->startTime = 0.0f;
	ctrl->stopTime = period;
	ctrl->targetRef.index = nif.GetBlockID(spin);
	ctrl->interpolatorRef.index = ipRef;
	ctrl->nextControllerRef.index = spin->controllerRef.index;
	spin->controllerRef.index = hdr.AddBlock(std::move(ctrl));
	// controllers only run in a NIF flagged as animated
	BSXFlags* have = nullptr;
	for (auto& r : root->extraDataRefs)
		if (auto* b = hdr.GetBlock<BSXFlags>(r)) have = b;
	if (have) have->integerData |= 1u;
	else {
		auto bsx = std::make_unique<BSXFlags>();
		bsx->name.get() = "BSX";
		bsx->integerData = 1u;
		nif.AssignExtraData(root, std::move(bsx));
	}
	if (nif.Save(argv[2])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("wrote %s: %d shapes under %s, %.2f turns/s about (%.2f %.2f %.2f), %d keys\n", argv[2], moved, argv[3], tps, ax, ay, az, keys);
	return 0;
}
