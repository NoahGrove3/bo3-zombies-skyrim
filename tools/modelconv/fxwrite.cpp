// fxwrite <job.txt>: a static NIF of unlit, additive magic glows (BSEffectShaderProperty) for spell visuals:
// projectiles, the orb between the hands, beams, hand glows, the spell-menu display models.
// Job file:
//   out <nif>
//   base <nif>                   (optional: add the shapes to this NIF - e.g. a lit model - instead of a new one)
//   shape <name> tex=<texture> color=r,g,b,a scale=<base colour scale> [blend=add|alpha] [falloff=1] [rim=1] [single=1] [vscroll=<s>] [uscroll=<s>]
//         [billboard=1] [flip=<cols>x<rows> fps=<n> phase=<s>]
//     billboard: the shape always faces the camera (under a NiBillboardNode)
//     flip: the texture is a flipbook atlas played in a loop (the shape's UVs span one cell)
//     rim: the falloff the other way round (clear where it faces you, bright at its edge: a glow round a shape)
//     parent=<node>: under that node of the base NIF (e.g. a bone: the shape moves with it)
//     vscroll / uscroll: the texture slides one whole turn in that many seconds (a looping shader controller)
//   v x y z u v [r g b a]        (vertex colours optional: per-vertex tint and fade)
//   n x y z                      (optional, after its v: that vertex's normal - needed by falloff / rim)
//   t a b c
//   node <name> <parent> x y z   (a plain node added under <parent> of the base NIF, at x y z in its space - e.g. bones to skin to)
//   shape ... skin=<bone>,<bone>,...   the shape is skinned to those nodes (each must hang under the shape's own parent, unturned);
//   w <i> <weight> [<i> <weight> ...]   after its v: that vertex's bones (indices into skin=) and weights
#include "NifFile.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
using namespace nifly;

struct Shape
{
	std::string          name, tex, blend = "add";
	float                col[4]{ 1, 1, 1, 1 }, scale = 1.0f;
	bool                 falloff = false, vcol = false, rim = false, single = false;
	float                vscroll = 0.0f, uscroll = 0.0f;
	bool                 billboard = false;
	int                  bbMode = 0;
	float                at[3]{ 0, 0, 0 };  // at=x,y,z: where the billboard's node stands (it turns about it)  // billboard=1: always face the camera; billboard=<n>: that NiBillboardNode mode (2 rigid: stays upright on screen)
	int                  cols = 0, rows = 0;
	std::string          parent;  // parent=<node>: the shape hangs under that node (its coordinates in the node's space)
	float                fps = 18.0f, phase = 0.0f;
	std::vector<Vector3> v;
	std::vector<Vector2> uv;
	std::vector<Color4>  c;
	std::vector<Vector3> n;
	std::vector<Triangle> t;
	std::vector<std::string> skin;
	std::vector<std::vector<std::pair<int, float>>> w;
};
struct NodeAdd
{
	std::string name, parent;
	float       x = 0, y = 0, z = 0;
};

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: fxwrite job.txt\n"); return 1; }
	std::ifstream in(argv[1]);
	std::string   line, out, base;
	std::vector<Shape> shapes;
	std::vector<NodeAdd> nodes;
	while (std::getline(in, line)) {
		std::istringstream ss(line);
		std::string k;
		ss >> k;
		if (k == "out") { std::getline(ss >> std::ws, out); }
		else if (k == "base") { std::getline(ss >> std::ws, base); }
		else if (k == "node") { NodeAdd a; ss >> a.name >> a.parent >> a.x >> a.y >> a.z; nodes.push_back(a); }
		else if (k == "w" && !shapes.empty()) {
			auto& s = shapes.back(); s.w.resize(s.v.size());
			int i; float x;
			while (ss >> i >> x) s.w.back().push_back({ i, x });
		}
		else if (k == "n" && !shapes.empty()) {
			auto& s = shapes.back(); float x, y, z; ss >> x >> y >> z;
			s.n.resize(s.v.size()); s.n.back() = Vector3(x, y, z);
		}
		else if (k == "shape") {
			Shape s; ss >> s.name; std::string kv;
			while (ss >> kv) {
				auto e = kv.find('='); if (e == std::string::npos) continue;
				auto key = kv.substr(0, e), val = kv.substr(e + 1);
				if (key == "tex") s.tex = val;
				else if (key == "color") sscanf(val.c_str(), "%f,%f,%f,%f", &s.col[0], &s.col[1], &s.col[2], &s.col[3]);
				else if (key == "scale") s.scale = std::stof(val);
				else if (key == "blend") s.blend = val;
				else if (key == "falloff") s.falloff = val == "1";
				else if (key == "rim") s.rim = val == "1";
				else if (key == "single") s.single = val == "1";
				else if (key == "billboard") { s.billboard = val != "0"; s.bbMode = std::stoi(val) == 1 ? 0 : std::stoi(val); }
				else if (key == "flip") sscanf(val.c_str(), "%dx%d", &s.cols, &s.rows);
				else if (key == "fps") s.fps = std::stof(val);
				else if (key == "at") sscanf(val.c_str(), "%f,%f,%f", &s.at[0], &s.at[1], &s.at[2]);
				else if (key == "phase") s.phase = std::stof(val);
				else if (key == "vscroll") s.vscroll = std::stof(val);
				else if (key == "uscroll") s.uscroll = std::stof(val);
				else if (key == "parent") s.parent = val;
				else if (key == "skin") {
					std::stringstream bs(val); std::string b;
					while (std::getline(bs, b, ',')) if (!b.empty()) s.skin.push_back(b);
				}
			}
			shapes.push_back(s);
		} else if (k == "v" && !shapes.empty()) {
			auto& s = shapes.back(); float x, y, z, u, w, r = 1, g = 1, b = 1, a = 1;
			ss >> x >> y >> z >> u >> w;
			if (ss >> r >> g >> b >> a) s.vcol = true;
			s.v.push_back({ x, y, z }); s.uv.push_back({ u, w }); s.c.push_back({ r, g, b, a });
		} else if (k == "t" && !shapes.empty()) {
			uint16_t a, b, c; ss >> a >> b >> c; shapes.back().t.push_back({ a, b, c });
		}
	}
	NifFile nif;
	if (!base.empty()) {
		if (nif.Load(base)) { fprintf(stderr, "cannot load %s\n", base.c_str()); return 1; }
	} else {
		nif.Create(NiVersion::getSSE());
		nif.GetRootNode()->name.get() = "FX";
	}
	for (auto& a : nodes) {
		auto* pn = nif.FindBlockByName<NiNode>(a.parent);
		if (!pn) { fprintf(stderr, "no node %s (for %s)\n", a.parent.c_str(), a.name.c_str()); return 1; }
		MatTransform x;
		x.translation = Vector3(a.x, a.y, a.z);
		nif.AddNode(a.name, x, pn);
	}
	for (auto& s : shapes) {
		const bool withNormals = !s.n.empty();
		if (withNormals) s.n.resize(s.v.size(), Vector3(0, 0, 1));
		auto* sh = nif.CreateShapeFromData(s.name, &s.v, &s.t, &s.uv, withNormals ? &s.n : nullptr);
		auto* ts = dynamic_cast<BSTriShape*>(sh);
		if (!ts) continue;
		if (s.vcol) {
			ts->SetVertexColors(true);
			for (size_t i = 0; i < ts->vertData.size() && i < s.c.size(); ++i) {
				ts->vertData[i].colorData[0] = (uint8_t)(std::clamp(s.c[i].r, 0.f, 1.f) * 255);
				ts->vertData[i].colorData[1] = (uint8_t)(std::clamp(s.c[i].g, 0.f, 1.f) * 255);
				ts->vertData[i].colorData[2] = (uint8_t)(std::clamp(s.c[i].b, 0.f, 1.f) * 255);
				ts->vertData[i].colorData[3] = (uint8_t)(std::clamp(s.c[i].a, 0.f, 1.f) * 255);
			}
		}
		ts->SetNormals(withNormals);  // (the falloff / rim needs them)
		ts->SetTangents(false);
		if (!s.skin.empty()) {  // skinned to nodes of the base: each bone's bind is its own place in the shape's parent's space
			auto* sp = s.parent.empty() ? nif.GetRootNode() : nif.FindBlockByName<NiNode>(s.parent);
			if (!sp) { fprintf(stderr, "no parent %s for the skin\n", s.parent.c_str()); return 1; }
			nif.CreateSkinning(sh);
			std::vector<int> ids;
			std::vector<Vector3> at;
			for (auto& b : s.skin) {
				auto* bn = nif.FindBlockByName<NiNode>(b);
				if (!bn) { fprintf(stderr, "no bone %s\n", b.c_str()); return 1; }
				ids.push_back(nif.GetBlockID(bn));
				at.push_back(bn->GetTransformToParent().translation);
			}
			nif.SetShapeBoneIDList(sh, ids);
			if (auto* si = nif.GetHeader().GetBlock<NiSkinInstance>(sh->SkinInstanceRef())) si->targetRef.index = nif.GetBlockID(sp);
			s.w.resize(s.v.size());
			std::vector<std::unordered_map<uint16_t, float>> wm(s.skin.size());
			std::vector<std::vector<Vector3>> pts(s.skin.size());
			for (size_t v = 0; v < s.v.size(); ++v)
				for (auto& [i, x] : s.w[v]) if (i >= 0 && i < (int)s.skin.size() && x > 0) wm[i][uint16_t(v)] = x, pts[i].push_back(s.v[v] - at[i]);
			for (uint32_t b = 0; b < s.skin.size(); ++b) {
				MatTransform bind;
				bind.translation = at[b];
				nif.SetShapeTransformSkinToBone(sh, b, bind.InverseTransform());
				float r = 1;
				for (auto& q : pts[b]) r = std::max(r, q.length());
				BoundingSphere bs(Vector3(), r + 2.0f);
				nif.SetShapeBoneBounds(s.name, b, bs);
				nif.SetShapeBoneWeights(s.name, b, wm[b]);
			}
			for (size_t v = 0; v < s.v.size(); ++v) {
				std::vector<uint8_t> bid;
				std::vector<float>   wt;
				for (auto& [i, x] : s.w[v]) bid.push_back(uint8_t(i)), wt.push_back(x);
				nif.SetShapeVertWeights(s.name, uint16_t(v), bid, wt);
			}
			nif.UpdateSkinPartitions(sh);
		}
		// swap the default lighting shader for an effect shader: unlit, its own colour, additive
		auto fx = std::make_unique<BSEffectShaderProperty>();
		fx->sourceTexture.get() = s.tex;
		fx->baseColor = Color4(s.col[0], s.col[1], s.col[2], s.col[3]);
		fx->baseColorScale = s.scale;
		fx->shaderFlags1 = 0x80000000u | (s.falloff || s.rim ? 0x40u : 0u) | (s.skin.empty() ? 0u : 0x2u);    // ZBuffer test (+ falloff, + skinned)
		fx->shaderFlags2 = (s.single ? 0u : 0x10u) | 0x20u * (s.vcol ? 1u : 0u);  // double-sided (+ vertex colours)
		fx->falloffStartAngle = 1.0f, fx->falloffStopAngle = 0.0f, fx->falloffStartOpacity = 1.0f, fx->falloffStopOpacity = 0.0f;
		if (s.rim) {
			fx->falloffStartOpacity = 0.0f, fx->falloffStopOpacity = 1.0f;
		}
		fx->textureClampMode = 3;
		auto* fxp = fx.get();
		auto ref = nif.GetHeader().AddBlock(std::move(fx));
		// texture scrolling: a float controller on the shader's U or V offset, 0 -> 1 over the period, looping
		uint32_t prevCtrl = 0xFFFFFFFF;
		for (int axis = 0; axis < 2; ++axis) {
			const float period = axis == 0 ? s.vscroll : s.uscroll;
			if (period <= 0.0f) continue;
			auto data = std::make_unique<NiFloatData>();
			data->data.SetInterpolationType(LINEAR_KEY);
			NiAnimationKey<float> k0; k0.type = LINEAR_KEY; k0.time = 0.0f; k0.value = 0.0f;
			NiAnimationKey<float> k1; k1.type = LINEAR_KEY; k1.time = period; k1.value = axis == 0 ? -1.0f : 1.0f;  // (V runs up: the flames rise)
			data->data.AddKey(k0);
			data->data.AddKey(k1);
			const auto dataRef = nif.GetHeader().AddBlock(std::move(data));
			auto interp = std::make_unique<NiFloatInterpolator>();
			interp->dataRef.index = dataRef;
			const auto interpRef = nif.GetHeader().AddBlock(std::move(interp));
			auto ctrl = std::make_unique<BSEffectShaderPropertyFloatController>();
			ctrl->flags = 0x48;  // active, looping
			ctrl->frequency = 1.0f;
			ctrl->phase = 0.0f;
			ctrl->startTime = 0.0f;
			ctrl->stopTime = period;
			ctrl->targetRef.index = ref;
			ctrl->interpolatorRef.index = interpRef;
			ctrl->typeOfControlledVariable = axis == 0 ? 8u : 6u;  // V offset / U offset
			if (prevCtrl != 0xFFFFFFFF) ctrl->nextControllerRef.index = prevCtrl;
			prevCtrl = nif.GetHeader().AddBlock(std::move(ctrl));
		}
		// flipbook: U and V offsets stepping cell by cell (steps made of key pairs, held until the next cell)
		if (s.cols > 0 && s.rows > 0) {
			const int   n = s.cols * s.rows;
			const float dt = 1.0f / s.fps, loop = n * dt;
			for (int axis = 0; axis < 2; ++axis) {
				auto data = std::make_unique<NiFloatData>();
				data->data.SetInterpolationType(LINEAR_KEY);
				for (int f = 0; f <= n; ++f) {
					// offsets only ever step forward (the texture wraps, so 1 is 0 again): a step never sweeps back
					// across the whole sheet, which showed as a flash of other cells for a frame
					const float v = axis == 0 ? f / float(s.cols) : (f / s.cols) / float(s.rows);
					NiAnimationKey<float> a; a.type = LINEAR_KEY; a.time = f * dt; a.value = v;
					data->data.AddKey(a);
					if (f < n) {
						NiAnimationKey<float> b; b.type = LINEAR_KEY; b.time = (f + 1) * dt - 0.00002f; b.value = v;
						data->data.AddKey(b);
					}
				}
				const auto dataRef = nif.GetHeader().AddBlock(std::move(data));
				auto interp = std::make_unique<NiFloatInterpolator>();
				interp->dataRef.index = dataRef;
				const auto interpRef = nif.GetHeader().AddBlock(std::move(interp));
				auto ctrl = std::make_unique<BSEffectShaderPropertyFloatController>();
				ctrl->flags = 0x48;
				ctrl->frequency = 1.0f;
				ctrl->phase = s.phase;
				ctrl->startTime = 0.0f;
				ctrl->stopTime = loop;
				ctrl->targetRef.index = ref;
				ctrl->interpolatorRef.index = interpRef;
				ctrl->typeOfControlledVariable = axis == 0 ? 6u : 8u;  // U offset / V offset
				if (prevCtrl != 0xFFFFFFFF) ctrl->nextControllerRef.index = prevCtrl;
				prevCtrl = nif.GetHeader().AddBlock(std::move(ctrl));
			}
			s.vscroll = std::max(s.vscroll, 0.001f);  // (counts as animated below)
		}
		if (prevCtrl != 0xFFFFFFFF) fxp->controllerRef.index = prevCtrl;
		if (!s.parent.empty()) {
			if (auto* pn = nif.FindBlockByName<NiNode>(s.parent)) nif.SetParentNode(sh, pn);
			else fprintf(stderr, "no node %s\n", s.parent.c_str());
		}
		if (s.billboard) {  // always facing the camera
			auto bb = std::make_unique<NiBillboardNode>();
			bb->name.get() = s.name + "_bb";
			bb->billboardMode = static_cast<BillboardMode>(s.bbMode);
			{
				MatTransform xf;
				xf.translation = Vector3(s.at[0], s.at[1], s.at[2]);
				bb->SetTransformToParent(xf);
			}
			auto* bbp = bb.get();
			const auto bbRef = nif.GetHeader().AddBlock(std::move(bb));
			nif.GetRootNode()->childRefs.AddBlockRef(bbRef);
			nif.SetParentNode(sh, bbp);
		}
		ts->ShaderPropertyRef()->index = ref;
		auto alpha = std::make_unique<NiAlphaProperty>();
		alpha->flags = s.blend == "add" ? 4109 : 4333;  // additive (src alpha, one) or alpha blend
		alpha->threshold = 0;
		nif.AssignAlphaProperty(sh, std::move(alpha));
	}
	// controllers only run in a NIF flagged as animated (BSXFlags bit 0) - add it, or set the bit on the one there
	bool scrolls = false;
	for (auto& s : shapes) scrolls |= s.vscroll > 0.0f || s.uscroll > 0.0f;
	if (scrolls) {
		auto* root = nif.GetRootNode();
		BSXFlags* have = nullptr;
		for (auto& r : root->extraDataRefs) {
			if (auto* b = nif.GetHeader().GetBlock<BSXFlags>(r)) have = b;
		}
		if (have) {
			have->integerData |= 1u;
		} else {
			auto bsx = std::make_unique<BSXFlags>();
			bsx->name.get() = "BSX";
			bsx->integerData = 1u;
			nif.AssignExtraData(root, std::move(bsx));
		}
	}
	nif.Save(out);
	printf("wrote %s (%zu shapes)\n", out.c_str(), shapes.size());
}
