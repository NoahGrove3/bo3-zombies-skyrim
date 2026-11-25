// nifpsys <nif>: a Skyrim effect's particle systems and effect meshes as JSON - their emitters, modifiers, controllers' keys,
// shaders and world placement - for the workshop's close pictures of the real parts (tools/realpreview.py)
#include "NifFile.hpp"
#include "Particles.hpp"
#include "Animation.hpp"
#include "Shaders.hpp"
#include <cstdio>
#include <sstream>
using namespace nifly;

static NifFile nif;
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }
static std::string f(float v) { if (!std::isfinite(v)) v = v > 0 ? 3.0e38f : -3.0e38f; char b[32]; snprintf(b, 32, "%.5g", v); return b; }
static std::string v3(const Vector3& v) { return "[" + f(v.x) + "," + f(v.y) + "," + f(v.z) + "]"; }
static std::string c4(const Color4& c) { return "[" + f(c.r) + "," + f(c.g) + "," + f(c.b) + "," + f(c.a) + "]"; }
static MatTransform world(NiAVObject* o) {
	MatTransform w = o->GetTransformToParent();
	for (NiNode* p = nif.GetParentNode(o); p; p = nif.GetParentNode(p)) {
		const MatTransform& P = p->GetTransformToParent();
		MatTransform r;
		r.translation = P.translation + P.rotation * (w.translation * P.scale);
		r.rotation = P.rotation * w.rotation;
		r.scale = P.scale * w.scale;
		w = r;
	}
	return w;
}
static std::string parents(NiAVObject* o) {
	std::string r = "[";
	bool fst = true;
	for (NiNode* p = nif.GetParentNode(o); p; p = nif.GetParentNode(p)) { r += (fst ? "\"" : ",\"") + esc(p->name.get()) + "\""; fst = false; }
	return r + "]";
}
static std::string xf(const MatTransform& w) {
	Vector3 cx = w.rotation * Vector3(1, 0, 0), cy = w.rotation * Vector3(0, 1, 0), cz = w.rotation * Vector3(0, 0, 1);
	return "{\"t\":" + v3(w.translation) + ",\"x\":" + v3(cx) + ",\"y\":" + v3(cy) + ",\"z\":" + v3(cz) + ",\"s\":" + f(w.scale) + "}";
}
template <class T> static std::string fkeys(const NiAnimationKeyGroup<T>& g) {
	std::string o = "[";
	for (uint32_t i = 0; i < g.GetNumKeys(); ++i) { auto k = g.GetKey(i); o += (i ? "," : "") + std::string("[") + f(k.time) + "," + f(k.value) + "]"; }
	return o + "]";
}
static std::string interp(uint32_t id) {
	auto& h = nif.GetHeader();
	if (auto* fi = h.GetBlock<NiFloatInterpolator>(id)) {
		std::string o = "{\"k\":\"float\",\"v\":" + f(fi->floatValue);
		if (auto* d = h.GetBlock<NiFloatData>(fi->dataRef)) o += ",\"keys\":" + fkeys(d->data);
		return o + "}";
	}
	if (auto* bi = h.GetBlock<NiBoolInterpolator>(id)) {
		std::string o = "{\"k\":\"bool\",\"v\":" + std::to_string(bi->boolValue);
		if (auto* d = h.GetBlock<NiBoolData>(bi->dataRef)) {
			o += ",\"keys\":[";
			for (uint32_t i = 0; i < d->data.GetNumKeys(); ++i) { auto k = d->data.GetKey(i); o += (i ? "," : "") + std::string("[") + f(k.time) + "," + std::to_string(k.value) + "]"; }
			o += "]";
		}
		return o + "}";
	}
	if (auto* ti = h.GetBlock<NiTransformInterpolator>(id)) {
		std::string o = "{\"k\":\"xf\",\"t\":" + v3(ti->translation) + ",\"s\":" + f(ti->scale);
		if (auto* d = h.GetBlock<NiTransformData>(ti->dataRef)) {
			o += ",\"scale\":" + fkeys(d->scales) + ",\"pos\":[";
			for (uint32_t i = 0; i < d->translations.GetNumKeys(); ++i) { auto k = d->translations.GetKey(i); o += (i ? "," : "") + std::string("[") + f(k.time) + "," + v3(k.value) + "]"; }
			o += "],\"zrot\":" + fkeys(d->zRotations) + ",\"xrot\":" + fkeys(d->xRotations) + ",\"yrot\":" + fkeys(d->yRotations) + ",\"quat\":[";
			for (size_t i = 0; i < d->quaternionKeys.size(); ++i) { auto& k = d->quaternionKeys[i]; o += (i ? "," : "") + std::string("[") + f(k.time) + "," + f(k.value.w) + "," + f(k.value.x) + "," + f(k.value.y) + "," + f(k.value.z) + "]"; }
			o += "]";
		}
		return o + "}";
	}
	if (auto* pi = h.GetBlock<NiPoint3Interpolator>(id)) {
		std::string o = "{\"k\":\"p3\",\"v\":" + v3(pi->point3Value);
		if (auto* d = h.GetBlock<NiPosData>(pi->dataRef)) {
			o += ",\"keys\":[";
			for (uint32_t i = 0; i < d->data.GetNumKeys(); ++i) { auto k = d->data.GetKey(i); o += (i ? "," : "") + std::string("[") + f(k.time) + "," + v3(k.value) + "]"; }
			o += "]";
		}
		return o + "}";
	}
	if (auto* b = h.GetBlock<NiObject>(id)) return std::string("{\"k\":\"") + b->GetBlockName() + "\"}";
	return "null";
}
static std::string ctlrs(uint32_t first) {
	auto& h = nif.GetHeader();
	std::string o = "[";
	bool any = false;
	for (uint32_t id = first; id != NIF_NPOS;) {
		auto* c = h.GetBlock<NiTimeController>(id);
		if (!c) break;
		o += std::string(any ? "," : "") + "{\"type\":\"" + c->GetBlockName() + "\",\"flags\":" + std::to_string(c->flags) + ",\"freq\":" + f(c->frequency) +
		     ",\"start\":" + f(c->startTime) + ",\"stop\":" + f(c->stopTime);
		any = true;
		if (auto* m = dynamic_cast<NiPSysModifierCtlr*>(c)) o += ",\"mod\":\"" + esc(m->modifierName.get()) + "\"";
		if (auto* e = dynamic_cast<BSEffectShaderPropertyFloatController*>(c)) o += ",\"var\":" + std::to_string(e->typeOfControlledVariable);
		if (auto* e = dynamic_cast<BSEffectShaderPropertyColorController*>(c)) o += ",\"var\":" + std::to_string(e->typeOfControlledColor) + ",\"color\":1";
		if (auto* s = dynamic_cast<NiSingleInterpController*>(c)) if (!s->interpolatorRef.IsEmpty()) o += ",\"interp\":" + interp(s->interpolatorRef.index);
		if (auto* ec = dynamic_cast<NiPSysEmitterCtlr*>(c)) {
			if (!ec->visInterpolatorRef.IsEmpty()) o += ",\"vis\":" + interp(ec->visInterpolatorRef.index);
			if (auto* d = h.GetBlock<NiPSysEmitterCtlrData>(ec->dataRef)) o += ",\"rate\":" + fkeys(d->floatKeys);
		}
		o += "}";
		id = c->nextControllerRef.index;
	}
	return o + "]";
}
static std::string shader(NiShader* sh, NiProperty* alpha) {
	std::string o = "{";
	if (auto* e = dynamic_cast<BSEffectShaderProperty*>(sh)) {
		o += "\"tex\":\"" + esc(e->sourceTexture.get()) + "\",\"grey\":\"" + esc(e->greyscaleTexture.get()) + "\",\"color\":" + c4(e->baseColor) +
		     ",\"cscale\":" + f(e->baseColorScale) + ",\"f1\":" + std::to_string(e->shaderFlags1) + ",\"f2\":" + std::to_string(e->shaderFlags2) +
		     ",\"clamp\":" + std::to_string(e->textureClampMode) + ",\"uvoff\":[" + f(e->uvOffset.u) + "," + f(e->uvOffset.v) + "],\"uvscale\":[" +
		     f(e->uvScale.u) + "," + f(e->uvScale.v) + "],\"falloff\":[" + f(e->falloffStartAngle) + "," + f(e->falloffStopAngle) + "," +
		     f(e->falloffStartOpacity) + "," + f(e->falloffStopOpacity) + "],\"soft\":" + f(e->softFalloffDepth) + ",\"ctl\":" + ctlrs(e->controllerRef.index);
	} else if (sh) o += "\"other\":\"" + std::string(sh->GetBlockName()) + "\"";
	if (auto* a = dynamic_cast<NiAlphaProperty*>(alpha)) o += std::string(o.size() > 1 ? "," : "") + "\"alpha\":" + std::to_string(a->flags);
	return o + "}";
}
int main(int argc, char** argv) {
	if (argc < 2 || nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	auto& h = nif.GetHeader();
	std::ostringstream out;
	out << "{\"psys\":[";
	bool first = true;
	for (uint32_t id = 0; id < h.GetNumBlocks(); ++id) {
		auto* ps = h.GetBlock<NiParticleSystem>(id);
		if (!ps) continue;
		out << (first ? "" : ",") << "{\"name\":\"" << esc(ps->name.get()) << "\",\"type\":\"" << ps->GetBlockName() << "\",\"world\":" << xf(world(ps))
		    << ",\"ws\":" << (ps->isWorldSpace ? 1 : 0) << ",\"parents\":" << parents(ps) << ",\"hidden\":" << ((ps->flags & 1) ? 1 : 0);
		first = false;
		if (auto* d = h.GetBlock<NiPSysData>(ps->psysDataRef.IsEmpty() ? ps->dataRef.index : ps->psysDataRef.index)) {
			out << ",\"max\":" << d->GetNumVertices() << ",\"sub\":[";
			for (size_t i = 0; i < d->subtexOffsets.size(); ++i) { auto& s = d->subtexOffsets[i]; out << (i ? "," : "") << "[" << f(s.x) << "," << f(s.y) << "," << f(s.z) << "," << f(s.w) << "]"; }
			out << "],\"aspect\":" << f(d->aspectRatio) << ",\"aflags\":" << d->aspectFlags;
		}
		out << ",\"shader\":" << shader(h.GetBlock<NiShader>(ps->shaderPropertyRef), h.GetBlock<NiProperty>(ps->alphaPropertyRef));
		out << ",\"mods\":[";
		bool fm = true;
		for (auto& r : ps->modifierRefs) {
			auto* m = h.GetBlock<NiPSysModifier>(r);
			if (!m) continue;
			out << (fm ? "" : ",") << "{\"type\":\"" << m->GetBlockName() << "\",\"name\":\"" << esc(m->name.get()) << "\",\"on\":" << (m->isActive ? 1 : 0);
			fm = false;
			if (auto* e = dynamic_cast<NiPSysEmitter*>(m)) {
				out << ",\"speed\":" << f(e->speed) << ",\"speedv\":" << f(e->speedVariation) << ",\"decl\":" << f(e->declination) << ",\"declv\":" << f(e->declinationVariation)
				    << ",\"plan\":" << f(e->planarAngle) << ",\"planv\":" << f(e->planarAngleVariation) << ",\"color\":" << c4(e->color) << ",\"size\":" << f(e->radius)
				    << ",\"sizev\":" << f(e->radiusVariation) << ",\"life\":" << f(e->lifeSpan) << ",\"lifev\":" << f(e->lifeSpanVariation);
				if (auto* ve = dynamic_cast<NiPSysVolumeEmitter*>(m)) if (auto* n = h.GetBlock<NiAVObject>(ve->emitterNodeRef.index)) out << ",\"node\":" << xf(world(n));
				if (auto* s = dynamic_cast<NiPSysSphereEmitter*>(m)) out << ",\"radius\":" << f(s->radius);
				if (auto* c = dynamic_cast<NiPSysCylinderEmitter*>(m)) out << ",\"radius\":" << f(c->radius) << ",\"height\":" << f(c->height);
				if (auto* me = dynamic_cast<NiPSysMeshEmitter*>(m)) {
					out << ",\"meshes\":[";
					bool fm3 = true;
					for (auto& r3 : me->meshRefs) if (auto* o3 = h.GetBlock<NiAVObject>(r3.index)) { out << (fm3 ? "" : ",") << "\"" << esc(o3->name.get()) << "\""; fm3 = false; }
					out << "],\"vtype\":" << me->velocityType << ",\"etype\":" << me->emissionType << ",\"eaxis\":" << v3(me->emissionAxis);
				}
				if (auto* b = dynamic_cast<NiPSysBoxEmitter*>(m)) out << ",\"box\":[" << f(b->width) << "," << f(b->height) << "," << f(b->depth) << "]";
			}
			if (auto* g = dynamic_cast<NiPSysGrowFadeModifier*>(m)) out << ",\"grow\":" << f(g->growTime) << ",\"fade\":" << f(g->fadeTime) << ",\"base\":" << f(g->baseScale);
			if (auto* c = dynamic_cast<NiPSysColorModifier*>(m)) if (auto* d = h.GetBlock<NiColorData>(c->dataRef)) {
				out << ",\"keys\":[";
				for (uint32_t i = 0; i < d->data.GetNumKeys(); ++i) { auto k = d->data.GetKey(i); out << (i ? "," : "") << "[" << f(k.time) << "," << c4(k.value) << "]"; }
				out << "]";
			}
			if (auto* s = dynamic_cast<BSPSysSimpleColorModifier*>(m))
				out << ",\"fin\":" << f(s->fadeInPercent) << ",\"fout\":" << f(s->fadeOutPercent) << ",\"c1end\":" << f(s->color1EndPercent) << ",\"c2start\":" << f(s->color2StartPercent)
				    << ",\"c2end\":" << f(s->color2EndPercent) << ",\"c3start\":" << f(s->color3StartPercent) << ",\"c1\":" << c4(s->color1) << ",\"c2\":" << c4(s->color2) << ",\"c3\":" << c4(s->color3);
			if (auto* r2 = dynamic_cast<NiPSysRotationModifier*>(m))
				out << ",\"rspeed\":" << f(r2->initialSpeed) << ",\"rspeedv\":" << f(r2->initialSpeedVariation) << ",\"rangle\":" << f(r2->initialAngle) << ",\"ranglev\":" << f(r2->initialAngleVariation) << ",\"rsign\":" << (r2->randomSpeedSign ? 1 : 0);
			if (auto* s = dynamic_cast<BSPSysScaleModifier*>(m)) { out << ",\"scales\":["; for (size_t i = 0; i < s->floats.size(); ++i) out << (i ? "," : "") << f(s->floats[i]); out << "]"; }
			if (auto* g = dynamic_cast<NiPSysGravityModifier*>(m)) {
				out << ",\"axis\":" << v3(g->gravityAxis) << ",\"decay\":" << f(g->decay) << ",\"strength\":" << f(g->strength) << ",\"force\":" << g->forceType << ",\"turb\":" << f(g->turbulence) << ",\"turbscale\":" << f(g->turbulenceScale) << ",\"wa\":" << (g->worldAligned ? 1 : 0);
				if (auto* n = h.GetBlock<NiAVObject>(g->gravityObjRef.index)) out << ",\"node\":" << xf(world(n));
			}
			if (auto* d = dynamic_cast<NiPSysDragModifier*>(m)) out << ",\"drag\":" << f(d->percentage) << ",\"range\":" << f(d->range);
			if (auto* s = dynamic_cast<BSPSysSubTexModifier*>(m))
				out << ",\"f0\":" << f(s->startFrame) << ",\"f0v\":" << f(s->startFrameVariation) << ",\"f1\":" << f(s->endFrame) << ",\"loop\":" << f(s->loopStartFrame) << ",\"fc\":" << f(s->frameCount) << ",\"fcv\":" << f(s->frameCountVariation);
			if (auto* b = dynamic_cast<NiPSysBombModifier*>(m)) out << ",\"bomb\":" << f(b->deltaV) << ",\"decay\":" << f(b->decay);
			if (auto* sp = dynamic_cast<NiPSysSpawnModifier*>(m)) out << ",\"gens\":" << sp->numSpawnGenerations << ",\"pspawn\":" << f(sp->percentSpawned) << ",\"mins\":" << sp->minSpawned << ",\"maxs\":" << sp->maxSpawned << ",\"sspeed\":" << f(sp->spawnSpeedVariation) << ",\"sdir\":" << f(sp->spawnDirVariation) << ",\"slife\":" << f(sp->lifeSpan) << ",\"slifev\":" << f(sp->lifeSpanVariation);
			if (auto* sv = dynamic_cast<NiPSysVortexFieldModifier*>(m)) out << ",\"vortex\":" << v3(sv->direction);
			if (auto* fm2 = dynamic_cast<NiPSysFieldModifier*>(m)) out << ",\"fmag\":" << f(fm2->magnitude) << ",\"fatt\":" << f(fm2->attenuation) << ",\"fmax\":" << f(fm2->maxDistance);
			out << "}";
		}
		out << "],\"ctl\":" << ctlrs(ps->controllerRef.index) << "}";
	}
	out << "],\"shapes\":[";
	first = true;
	for (auto* s : nif.GetShapes()) {
		if (dynamic_cast<NiParticleSystem*>(s)) continue;
		std::vector<Vector3> V; std::vector<Vector2> UV; std::vector<Triangle> T;
		nif.GetVertsForShape(s, V); nif.GetUvsForShape(s, UV); s->GetTriangles(T);
		out << (first ? "" : ",") << "{\"name\":\"" << esc(s->name.get()) << "\",\"world\":" << xf(world(s)) << ",\"hidden\":" << ((s->flags & 1) ? 1 : 0) << ",\"parents\":" << parents(s)
		    << ",\"shader\":" << shader(nif.GetShader(s), h.GetBlock<NiProperty>(s->AlphaPropertyRef()->index)) << ",\"ctl\":" << ctlrs(s->controllerRef.index);
		first = false;
		std::vector<Color4> cols;
		nif.GetColorsForShape(s, cols);
		out << ",\"v\":[";
		for (size_t i = 0; i < V.size(); ++i) out << (i ? "," : "") << f(V[i].x) << "," << f(V[i].y) << "," << f(V[i].z);
		out << "],\"uv\":[";
		for (size_t i = 0; i < UV.size(); ++i) out << (i ? "," : "") << f(UV[i].u) << "," << f(UV[i].v);
		out << "],\"col\":[";
		for (size_t i = 0; i < cols.size(); ++i) out << (i ? "," : "") << f(cols[i].r) << "," << f(cols[i].g) << "," << f(cols[i].b) << "," << f(cols[i].a);
		out << "],\"t\":[";
		for (size_t i = 0; i < T.size(); ++i) out << (i ? "," : "") << T[i].p1 << "," << T[i].p2 << "," << T[i].p3;
		out << "]}";
	}
	// the nodes' own animation (scale, spin, move), and every sequence's links (the controller manager's)
	out << "],\"nodes\":[";
	first = true;
	for (uint32_t id = 0; id < h.GetNumBlocks(); ++id) {
		auto* n = h.GetBlock<NiNode>(id);
		if (!n || n->controllerRef.IsEmpty()) continue;
		out << (first ? "" : ",") << "{\"name\":\"" << esc(n->name.get()) << "\",\"world\":" << xf(world(n)) << ",\"local\":" << xf(n->GetTransformToParent()) << ",\"parents\":" << parents(n) << ",\"ctl\":" << ctlrs(n->controllerRef.index) << "}";
		first = false;
	}
	// every object's place in the tree: its parent and its own transform (the page rebuilds the tree and plays its keys on it)
	out << "],\"tree\":[";
	first = true;
	for (uint32_t id = 0; id < h.GetNumBlocks(); ++id) {
		auto* o = h.GetBlock<NiAVObject>(id);
		if (!o) continue;
		auto* par = nif.GetParentNode(o);
		out << (first ? "" : ",") << "{\"name\":\"" << esc(o->name.get()) << "\",\"type\":\"" << o->GetBlockName() << "\",\"parent\":\"" << (par ? esc(par->name.get()) : std::string("")) << "\",\"local\":" << xf(o->GetTransformToParent()) << ",\"hidden\":" << ((o->flags & 1) ? 1 : 0) << "}";
		first = false;
	}
	out << "],\"seq\":[";
	first = true;
	for (uint32_t id = 0; id < h.GetNumBlocks(); ++id) {
		auto* s = h.GetBlock<NiControllerSequence>(id);
		if (!s) continue;
		out << (first ? "" : ",") << "{\"name\":\"" << esc(s->name.get()) << "\",\"cycle\":" << s->cycleType << ",\"start\":" << f(s->startTime) << ",\"stop\":" << f(s->stopTime) << ",\"freq\":" << f(s->frequency) << ",\"links\":[";
		first = false;
		bool fl = true;
		for (auto& l : s->controlledBlocks) {
			out << (fl ? "" : ",") << "{\"node\":\"" << esc(l.nodeName.get()) << "\",\"ctrl\":\"" << esc(l.ctrlType.get()) << "\",\"prop\":\"" << esc(l.propType.get()) << "\",\"id\":\"" << esc(l.ctrlID.get())
			    << "\",\"interp\":" << (l.interpolatorRef.IsEmpty() ? std::string("null") : interp(l.interpolatorRef.index)) << "}";
			fl = false;
		}
		out << "]}";
	}
	out << "]}\n";
	fputs(out.str().c_str(), stdout);
	return 0;
}
