// fxwrite <job.txt>: a static NIF of unlit, additive magic glows (BSEffectShaderProperty) for spell visuals:
// projectiles, the orb between the hands, beams, hand glows, the spell-menu display models.
// Job file:
//   out <nif>
//   base <nif>                   (optional: add the shapes to this NIF - e.g. a lit model - instead of a new one)
//   shape <name> tex=<texture> color=r,g,b,a scale=<base colour scale> [blend=add|alpha] [falloff=1] [rim=1] [single=1] [vscroll=<s>] [uscroll=<s>]
//     rim: the falloff the other way round (clear where it faces you, bright at its edge: a glow round a shape)
//     vscroll / uscroll: the texture slides one whole turn in that many seconds (a looping shader controller)
//   v x y z u v [r g b a]        (vertex colours optional: per-vertex tint and fade)
//   n x y z                      (optional, after its v: that vertex's normal - needed by falloff / rim)
//   t a b c
#include "NifFile.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace nifly;

struct Shape
{
	std::string          name, tex, blend = "add";
	float                col[4]{ 1, 1, 1, 1 }, scale = 1.0f;
	bool                 falloff = false, vcol = false, rim = false, single = false;
	float                vscroll = 0.0f, uscroll = 0.0f;
	std::vector<Vector3> v;
	std::vector<Vector2> uv;
	std::vector<Color4>  c;
	std::vector<Vector3> n;
	std::vector<Triangle> t;
};

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: fxwrite job.txt\n"); return 1; }
	std::ifstream in(argv[1]);
	std::string   line, out, base;
	std::vector<Shape> shapes;
	while (std::getline(in, line)) {
		std::istringstream ss(line);
		std::string k;
		ss >> k;
		if (k == "out") { std::getline(ss >> std::ws, out); }
		else if (k == "base") { std::getline(ss >> std::ws, base); }
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
				else if (key == "vscroll") s.vscroll = std::stof(val);
				else if (key == "uscroll") s.uscroll = std::stof(val);
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
		// swap the default lighting shader for an effect shader: unlit, its own colour, additive
		auto fx = std::make_unique<BSEffectShaderProperty>();
		fx->sourceTexture.get() = s.tex;
		fx->baseColor = Color4(s.col[0], s.col[1], s.col[2], s.col[3]);
		fx->baseColorScale = s.scale;
		fx->shaderFlags1 = 0x80000000u | (s.falloff || s.rim ? 0x40u : 0u);    // ZBuffer test (+ falloff)
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
		if (prevCtrl != 0xFFFFFFFF) fxp->controllerRef.index = prevCtrl;
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
