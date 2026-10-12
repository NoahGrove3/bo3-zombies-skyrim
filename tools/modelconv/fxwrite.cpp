// fxwrite <job.txt>: a static NIF of unlit, additive magic glows (BSEffectShaderProperty) for spell visuals:
// projectiles, the orb between the hands, beams, hand glows, the spell-menu display models.
// Job file:
//   out <nif>
//   shape <name> tex=<texture> color=r,g,b,a scale=<base colour scale> [blend=add|alpha] [falloff=1]
//   v x y z u v [r g b a]        (vertex colours optional: per-vertex tint and fade)
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
	bool                 falloff = false, vcol = false;
	std::vector<Vector3> v;
	std::vector<Vector2> uv;
	std::vector<Color4>  c;
	std::vector<Triangle> t;
};

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: fxwrite job.txt\n"); return 1; }
	std::ifstream in(argv[1]);
	std::string   line, out;
	std::vector<Shape> shapes;
	while (std::getline(in, line)) {
		std::istringstream ss(line);
		std::string k;
		ss >> k;
		if (k == "out") { std::getline(ss >> std::ws, out); }
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
	nif.Create(NiVersion::getSSE());
	nif.GetRootNode()->name.get() = "FX";
	for (auto& s : shapes) {
		auto* sh = nif.CreateShapeFromData(s.name, &s.v, &s.t, &s.uv);
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
		ts->SetNormals(false);
		ts->SetTangents(false);
		// swap the default lighting shader for an effect shader: unlit, its own colour, additive
		auto fx = std::make_unique<BSEffectShaderProperty>();
		fx->sourceTexture.get() = s.tex;
		fx->baseColor = Color4(s.col[0], s.col[1], s.col[2], s.col[3]);
		fx->baseColorScale = s.scale;
		fx->shaderFlags1 = 0x80000000u | (s.falloff ? 0x40u : 0u);              // ZBuffer test (+ falloff)
		fx->shaderFlags2 = 0x10u | 0x20u * (s.vcol ? 1u : 0u);                  // double-sided (+ vertex colours)
		fx->falloffStartAngle = 1.0f, fx->falloffStopAngle = 0.0f, fx->falloffStartOpacity = 1.0f, fx->falloffStopOpacity = 0.0f;
		fx->textureClampMode = 3;
		auto ref = nif.GetHeader().AddBlock(std::move(fx));
		ts->ShaderPropertyRef()->index = ref;
		auto alpha = std::make_unique<NiAlphaProperty>();
		alpha->flags = s.blend == "add" ? 4109 : 4333;  // additive (src alpha, one) or alpha blend
		alpha->threshold = 0;
		nif.AssignAlphaProperty(sh, std::move(alpha));
	}
	nif.Save(out);
	printf("wrote %s (%zu shapes)\n", out.c_str(), shapes.size());
}
