// ================================================================ the workshop's own effects, baked (Noah, Nov 27 test of 3edd0b7: "I want the
// preview effects in this workshop to be exactly how it shows in game if not better") - each one the preview plays rendered frame by
// frame into one atlas (tools/fxbake.py, src/BakedFx.h). Here each is a card hung in the scene, turned every frame to face you
// upright (seen the same from every side - never edge-on), stepped through its atlas cell by cell from its first frame, once, and
// taken away at its end. Its material is its own copy, so two of the same burst play each their own frame.
// (included in Gun.cpp's namespace, after TickEBursts)

struct BakedOne
{
	Spellcraft::Fx fx;
	const baked::Def* d = nullptr;
	RE::NiPoint3 at;
	float t = 0, delay = 0, k = 1.0f, fade = 1.0f;
	std::vector<RE::BSEffectShaderMaterial*> mats;
	bool done = false;
};
std::vector<BakedOne> g_baked;
const baked::Def* BakedDef(std::string_view a_name)
{
	for (const auto& d : baked::kDefs) if (d.name == a_name) return &d;
	return nullptr;
}
// a_k: the game's units for one of the workshop's (kBakedScale is its everyday size: the dead stand as tall as the preview's)
bool BakedAt(std::string_view a_name, const RE::NiPoint3& a_at, float a_k, float a_delay)
{
	const auto* d = BakedDef(a_name);
	if (!d) { if (static int said = 0; said++ < 8) logger::warn("baked: no effect {}", a_name); return false; }
	if (g_baked.size() > 40) return false;   // (never a screen of them)
	BakedOne b;
	b.d = d, b.at = a_at, b.k = a_k, b.delay = a_delay;
	g_baked.push_back(std::move(b));
	return true;
}
float BakedHalfWidth(std::string_view a_name) { const auto* d = BakedDef(a_name); return d ? d->w * 0.5f : 0.0f; }
void TickBaked(float a_delta)
{
	auto* cam = RE::PlayerCamera::GetSingleton();
	const RE::NiPoint3 eye = cam && cam->cameraRoot ? cam->cameraRoot->world.translate : RE::NiPoint3{};
	for (auto& b : g_baked) {
		b.t += a_delta;
		if (b.t < b.delay) continue;
		const float age = b.t - b.delay;
		if (!b.fx.obj) {
			b.fx = Spellcraft::Fx{ Spellcraft::LoadPath(std::string("CurseOfAlduin\\baked\\") + std::string(b.d->name) + ".nif") };
			if (!b.fx.obj) { b.done = true; continue; }
			b.fx.Attach(Spellcraft::WorldRoot());
			// its own material (the model's is shared by every copy in the game)
			RE::BSVisit::TraverseScenegraphGeometries(b.fx.obj.get(), [&](RE::BSGeometry* g) {
				auto* ep = netimmerse_cast<RE::BSEffectShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get());
				auto* m = ep ? static_cast<RE::BSEffectShaderMaterial*>(ep->material) : nullptr;
				if (!m) return RE::BSVisit::BSVisitControl::kContinue;
				if (auto* nm = static_cast<RE::BSEffectShaderMaterial*>(m->Create())) {
					nm->CopyMembers(m);
					ep->SetMaterial(nm, true);
					nm->~BSEffectShaderMaterial();
					RE::free(nm);
				}
				if (auto* own = static_cast<RE::BSEffectShaderMaterial*>(ep->material)) b.mats.push_back(own);
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (static int said = 0; said++ < 30) logger::info("baked: {} at ({:.0f},{:.0f},{:.0f}) x{:.2f}, {:.1f} s", b.d->name, b.at.x, b.at.y, b.at.z, b.k, b.d->dur);
		}
		if (age >= b.d->dur) {
			b.fx.Detach();
			b.done = true;
			continue;
		}
		// the frame: its cell in the atlas
		const int f = std::clamp(static_cast<int>(age / b.d->dur * static_cast<float>(b.d->frames)), 0, b.d->frames - 1);
		const float u = static_cast<float>(f % b.d->cols) * b.d->stepU, v = static_cast<float>(f / b.d->cols) * b.d->stepV;
		for (auto* m : b.mats) m->texCoordOffset[0] = { u, v }, m->texCoordOffset[1] = { u, v };
		// upright, turned to you: the card's front (its local -Y) toward the eye, about the vertical
		RE::NiPoint3 to = eye - b.at;
		to.z = 0.0f;
		const float yaw = (to.Length() > 1.0f ? std::atan2(to.x, to.y) : 0.0f) + 3.14159265f;   // (its -Y to you: seen from its front, not mirrored)
		RE::NiMatrix3 r;
		const float c = std::cos(yaw), s = std::sin(yaw);
		r.entry[0][0] = c, r.entry[0][1] = s, r.entry[0][2] = 0;
		r.entry[1][0] = -s, r.entry[1][1] = c, r.entry[1][2] = 0;
		r.entry[2][0] = 0, r.entry[2][1] = 0, r.entry[2][2] = 1;
		b.fx.Place(b.at, r, b.k);
	}
	std::erase_if(g_baked, [](const BakedOne& b) { return b.done; });
}
