// armourfit <job.txt>: one of Skyrim's armour pieces refitted to another body (3BA, HIMBO) - each shape's vertices moved to
// new positions given in the skeleton's (global) space, its own bones, weights and partitions kept; shapes dropped (the old
// body's skin pieces); the new body's own shape cloned in where the armour shows skin; textures swapped; a BODYTRI string.
// (tools/armourfit.py works out the positions)
// Job file:
//   armour <nif>
//   out <nif>
//   pos <shape> <file.f32>            (float32 x y z per vertex, global space; the count must match)
//   drop <shape>
//   body <nif> <shape>                (cloned in, its own name)
//   tex <shape> <slot> <path>         (texture slot 0 diffuse, 1 normal, 2 glow ...)
//   glow <shape> r g b mult           (the glow-map shader on, its emissive colour)
//   bodytri <path>
#include "NifFile.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
using namespace nifly;

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: armourfit job.txt\n"); return 1; }
	std::ifstream in(argv[1]);
	std::string line, out, bodytri;
	NifFile nif;
	std::vector<std::pair<std::string, std::string>> pos, bodies, drops;
	struct Tex { std::string shape; int slot; std::string path; };
	std::vector<Tex> texes;
	struct Glow { std::string shape; float r, g, b, m; };
	std::vector<Glow> glows;
	while (std::getline(in, line)) {
		std::istringstream ss(line);
		std::string k; ss >> k;
		if (k == "armour") { std::string p; std::getline(ss >> std::ws, p); if (nif.Load(p)) { fprintf(stderr, "cannot load %s\n", p.c_str()); return 1; } }
		else if (k == "out") std::getline(ss >> std::ws, out);
		else if (k == "pos") { std::string s, f; ss >> s; std::getline(ss >> std::ws, f); pos.push_back({ s, f }); }
		else if (k == "drop") { std::string s; std::getline(ss >> std::ws, s); drops.push_back({ s, "" }); }
		else if (k == "body") { std::string n, s; ss >> std::quoted(n); std::getline(ss >> std::ws, s); bodies.push_back({ n, s }); }
		else if (k == "tex") { Tex t; ss >> t.shape >> t.slot; std::getline(ss >> std::ws, t.path); texes.push_back(t); }
		else if (k == "glow") { Glow g; ss >> g.shape >> g.r >> g.g >> g.b >> g.m; glows.push_back(g); }
		else if (k == "bodytri") std::getline(ss >> std::ws, bodytri);
	}
	auto find = [&](const std::string& n) -> NiShape* { for (auto* s : nif.GetShapes()) if (s->name.get() == n) return s; return nullptr; };
	for (auto& [sn, file] : pos) {
		auto* sh = find(sn);
		if (!sh) { fprintf(stderr, "no shape %s\n", sn.c_str()); return 1; }
		auto* vs = nif.GetVertsForShape(sh);
		std::ifstream f(file, std::ios::binary);
		std::vector<float> d((std::istreambuf_iterator<char>(f)), {});
		f.close();
		std::ifstream f2(file, std::ios::binary);
		std::vector<char> raw((std::istreambuf_iterator<char>(f2)), {});
		const size_t n = raw.size() / 12;
		if (!vs || n != vs->size()) { fprintf(stderr, "%s: %zu positions for %zu vertices\n", sn.c_str(), n, vs ? vs->size() : 0); return 1; }
		MatTransform g2s;
		nif.CalcShapeTransformGlobalToSkin(sh, g2s);
		std::vector<Vector3> nv(n);
		const float* fp = reinterpret_cast<const float*>(raw.data());
		for (size_t i = 0; i < n; ++i) nv[i] = g2s.ApplyTransform(Vector3(fp[3 * i], fp[3 * i + 1], fp[3 * i + 2]));
		nif.SetVertsForShape(sh, nv);
		nif.CalcNormalsForShape(sh);
		nif.CalcTangentsForShape(sh);
		nif.UpdateSkinPartitions(sh);
	}
	for (auto& [sn, _] : drops) if (auto* sh = find(sn)) nif.DeleteShape(sh);
	for (auto& [bn, sn] : bodies) {
		NifFile src;
		if (src.Load(bn)) { fprintf(stderr, "cannot load %s\n", bn.c_str()); return 1; }
		NiShape* s = nullptr;
		for (auto* x : src.GetShapes()) if (x->name.get() == sn) s = x;
		if (!s) { fprintf(stderr, "%s: no shape %s\n", bn.c_str(), sn.c_str()); return 1; }
		if (!nif.CloneShape(s, sn, &src)) { fprintf(stderr, "clone of %s failed\n", sn.c_str()); return 1; }
	}
	for (auto& t : texes) if (auto* sh = find(t.shape)) nif.SetTextureSlot(sh, t.path, t.slot);
	for (auto& gl : glows) if (auto* sh = find(gl.shape)) {
		if (auto* ls = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(sh))) {
			ls->SetShaderType(BSLSP_GLOWMAP);
			ls->shaderFlags2 |= SLSF2_GLOW_MAP;
			ls->emissiveColor = Vector3(gl.r, gl.g, gl.b), ls->emissiveMultiple = gl.m;
		}
	}
	if (!bodytri.empty()) {
		auto& h = nif.GetHeader(); bool had = false;
		for (uint32_t i = 0; i < h.GetNumBlocks(); ++i)
			if (auto* x = h.GetBlock<NiStringExtraData>(i); x && x->name.get() == "BODYTRI") x->stringData.get() = bodytri, had = true;
		if (!had) {
			auto x = std::make_unique<NiStringExtraData>();
			x->name.get() = "BODYTRI"; x->stringData.get() = bodytri;
			nif.GetRootNode()->extraDataRefs.AddBlockRef(h.AddBlock(std::move(x)));
		}
	}
	if (nif.Save(out)) { fprintf(stderr, "save failed\n"); return 1; }
	for (auto* s : nif.GetShapes()) {
		MatTransform g; nif.CalcShapeTransformGlobalToSkin(s, g);   // (for the morphs: a delta in the skeleton's space -> the shape's own)
		printf("shape %s\nxf %s|%g %g %g %g %g %g %g %g %g %g\n", s->name.get().c_str(), s->name.get().c_str(),
			g.rotation[0].x, g.rotation[0].y, g.rotation[0].z, g.rotation[1].x, g.rotation[1].y, g.rotation[1].z,
			g.rotation[2].x, g.rotation[2].y, g.rotation[2].z, g.scale);
	}
	printf("wrote %s\n", out.c_str());
}
