// nifanim <job.txt>: looping animations added to a static NIF's shapes (the NIF is flagged animated, BSXFlags bit 0).
// Job file:
//   in <nif>
//   out <nif>
//   sway <shape-name prefix> <degrees> <seconds> [seed]
//     each matching shape is re-centred on itself (its vertices moved so its own middle is its origin) and rocks back
//     and forth about a tilted axis through that middle, by up to <degrees>, once every <seconds> (each its own phase,
//     axis and pace a little apart)
//   pulse <shape-name prefix> <min> <max> <seconds>
//     the lit shader's emissive multiple of each matching shape breathes between min and max (its glow map pulses)
#include "NifFile.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
using namespace nifly;

static bool Prefix(const std::string& a_name, const std::string& a_pre) { return a_name.rfind(a_pre, 0) == 0; }

static Quaternion AxisAngle(float ax, float ay, float az, float ang)
{
	const float l = std::sqrt(ax * ax + ay * ay + az * az);
	const float s = std::sin(ang * 0.5f) / (l > 0 ? l : 1);
	return Quaternion(std::cos(ang * 0.5f), ax * s, ay * s, az * s);
}

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: nifanim job.txt\n"); return 1; }
	std::ifstream in(argv[1]);
	std::string   line, inPath, outPath;
	struct Sway { std::string pre; float deg, secs; unsigned seed; };
	struct Pulse { std::string pre; float lo, hi, secs; };
	std::vector<Sway>  sways;
	std::vector<Pulse> pulses;
	while (std::getline(in, line)) {
		std::istringstream ss(line);
		std::string k;
		ss >> k;
		if (k == "in") std::getline(ss >> std::ws, inPath);
		else if (k == "out") std::getline(ss >> std::ws, outPath);
		else if (k == "sway") { Sway s{ "", 3, 4, 1 }; ss >> s.pre >> s.deg >> s.secs >> s.seed; sways.push_back(s); }
		else if (k == "pulse") { Pulse p{ "", 1, 3, 3 }; ss >> p.pre >> p.lo >> p.hi >> p.secs; pulses.push_back(p); }
	}
	NifFile nif;
	if (nif.Load(inPath)) { fprintf(stderr, "cannot load %s\n", inPath.c_str()); return 1; }
	auto& hdr = nif.GetHeader();
	int   swayed = 0, pulsed = 0;
	auto  chain = [&](NiObjectNET* a_target, std::unique_ptr<NiTimeController> a_ctrl) {
        a_ctrl->nextControllerRef.index = a_target->controllerRef.index;
        a_target->controllerRef.index = hdr.AddBlock(std::move(a_ctrl));
	};
	for (auto& sw : sways) {
		std::mt19937 rng(sw.seed);
		std::uniform_real_distribution<float> U(0.0f, 1.0f);
		for (auto* shape : nif.GetShapes()) {
			auto* ts = dynamic_cast<BSTriShape*>(shape);
			if (!ts || !Prefix(ts->name.get(), sw.pre)) continue;
			// re-centre: the shape's own middle becomes its origin (its place moves into its transform)
			Vector3 c;
			for (auto& v : ts->vertData) c += v.vert;
			c /= static_cast<float>(ts->vertData.size());
			for (auto& v : ts->vertData) v.vert -= c;
			ts->UpdateBounds();
			MatTransform xf = ts->GetTransformToParent();
			xf.translation += c;
			ts->SetTransformToParent(xf);
			// rocking about a tilted, mostly level axis
			const float a = U(rng) * 6.2832f, amp = sw.deg * (0.75f + 0.5f * U(rng)) * 3.14159f / 180.0f;
			const float period = sw.secs * (0.8f + 0.45f * U(rng)), phase = U(rng) * 6.2832f;
			const float ax = std::cos(a), ay = std::sin(a), az = 0.25f * (U(rng) - 0.5f);
			auto data = std::make_unique<NiTransformData>();
			data->rotationType = LINEAR_KEY;
			const int n = 24;
			for (int i = 0; i <= n; ++i) {
				NiAnimationKey<Quaternion> q;
				q.type = LINEAR_KEY;
				q.time = period * i / n;
				q.value = AxisAngle(ax, ay, az, amp * std::sin(6.2832f * i / n + phase));
				data->quaternionKeys.push_back(q);
			}
			const auto dataRef = hdr.AddBlock(std::move(data));
			auto ip = std::make_unique<NiTransformInterpolator>();
			ip->translation = xf.translation;
			ip->rotation = Quaternion();
			ip->scale = 1.0f;
			ip->dataRef.index = dataRef;
			const auto ipRef = hdr.AddBlock(std::move(ip));
			auto ctrl = std::make_unique<NiTransformController>();
			ctrl->flags = 0x48;  // active, looping
			ctrl->frequency = 1.0f;
			ctrl->startTime = 0.0f;
			ctrl->stopTime = period;
			ctrl->targetRef.index = hdr.GetBlockID(ts);
			ctrl->interpolatorRef.index = ipRef;
			chain(ts, std::move(ctrl));
			++swayed;
		}
	}
	for (auto& p : pulses) {
		for (auto* shape : nif.GetShapes()) {
			if (!Prefix(shape->name.get(), p.pre)) continue;
			auto* sh = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(shape));
			if (!sh) continue;
			auto data = std::make_unique<NiFloatData>();
			data->data.SetInterpolationType(LINEAR_KEY);
			const int n = 24;
			for (int i = 0; i <= n; ++i) {
				NiAnimationKey<float> k;
				k.type = LINEAR_KEY;
				k.time = p.secs * i / n;
				k.value = p.lo + (p.hi - p.lo) * (0.5f - 0.5f * std::cos(6.2832f * i / n));
				data->data.AddKey(k);
			}
			const auto dataRef = hdr.AddBlock(std::move(data));
			auto ip = std::make_unique<NiFloatInterpolator>();
			ip->dataRef.index = dataRef;
			const auto ipRef = hdr.AddBlock(std::move(ip));
			auto ctrl = std::make_unique<BSLightingShaderPropertyFloatController>();
			ctrl->flags = 0x48;
			ctrl->frequency = 1.0f;
			ctrl->startTime = 0.0f;
			ctrl->stopTime = p.secs;
			ctrl->targetRef.index = hdr.GetBlockID(sh);
			ctrl->interpolatorRef.index = ipRef;
			ctrl->typeOfControlledVariable = 11;  // emissive multiple (LightingShaderControlledFloat)
			chain(sh, std::move(ctrl));
			++pulsed;
		}
	}
	// controllers only run in a NIF flagged as animated
	auto* root = nif.GetRootNode();
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
	nif.Save(outPath);
	printf("wrote %s (%d swaying, %d pulsing)\n", outPath.c_str(), swayed, pulsed);
}
