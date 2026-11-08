// Our effect meshes in the scene (Noah's old "ten spells" left the game; what stays is their visual kit - the bursts and
// streaks in a spell row's colours (sheet: spellcraft; looks: tools/spellfx.py), the placed effect pieces, and the small
// helpers the weapons share: knocking a body away, the hands' bones, the view's points, world rays). Included by Gun.cpp
// inside its anonymous namespace, after the shared helpers (Damage, Foes, AimAt, HeadPos, AxisAngle...).
namespace Spellcraft
{
	constexpr float kSeg = 64.0f;          // beam meshes are 64-unit segments along +Y (tools/spellfx.py)

	// ---------------------------------------------------------------- visuals: our effect meshes in the scene
	RE::NiNode* WorldRoot()
	{
		auto& st = RE::BSShaderManager::State::GetSingleton();
		return st.shadowSceneNode[0];
	}

	RE::NiPointer<RE::NiAVObject> LoadPath(const std::string& path);
	RE::NiPointer<RE::NiAVObject> Load(std::string_view a_id, const char* a_part)
	{
		return LoadPath("CurseOfAlduin\\fx\\" + std::string(a_id) + "_" + a_part + ".nif");
	}
	// any of our models, by its path under Meshes (a fresh copy each time, never culled)
	RE::NiPointer<RE::NiAVObject> LoadPath(const std::string& path)
	{
		static std::unordered_map<std::string, RE::NiPointer<RE::NiNode>> cache;
		static std::unordered_map<std::string, bool> missing;
		if (missing.contains(path)) {
			return {};
		}
		auto& c = cache[path];
		if (!c) {
			RE::BSModelDB::DBTraits::ArgsType args{};
			if (RE::BSModelDB::Demand(path.c_str(), c, args) != RE::BSResource::ErrorCode::kNone || !c) {
				c.reset();
				const std::string alt = "Meshes\\" + path;
				if (RE::BSModelDB::Demand(alt.c_str(), c, args) != RE::BSResource::ErrorCode::kNone || !c) {
					c.reset();
					missing[path] = true;
					logger::warn("spellcraft: could not load {}", path);
					return {};
				}
			}
		}
		auto* clone = c->Clone();
		auto* obj = clone ? static_cast<RE::NiAVObject*>(clone) : nullptr;
		// never culled: hung in the scene by hand, outside any cell's culling bounds, the engine dropped them anywhere
		// off the middle of the view
		std::function<void(RE::NiAVObject*)> mark = [&](RE::NiAVObject* o) {
			if (!o) {
				return;
			}
			o->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw, RE::NiAVObject::Flag::kIgnoreFade);
			if (auto* n = o->AsNode()) {
				for (auto& ch : n->GetChildren()) {
					mark(ch.get());
				}
			}
		};
		mark(obj);
		return RE::NiPointer<RE::NiAVObject>(obj);
	}

	struct Fx
	{
		RE::NiPointer<RE::NiAVObject> obj;
		RE::NiNode*                   parent = nullptr;

		// (the audit: the stored parent outlived a body rebuilt on load or an arrow loosed, and Detach wrote into freed
		// memory) the object's own live parent is the only one trusted: a freed node has already let go of it
		void Attach(RE::NiNode* a_parent)
		{
			if (!obj) return;
			if (!a_parent) {
				Detach();
				return;
			}
			if (obj->parent == a_parent) {
				parent = a_parent;
				return;
			}
			Detach();
			a_parent->AttachChild(obj.get(), true);
			parent = a_parent;
		}
		void Detach()
		{
			if (obj && obj->parent) {
				obj->parent->DetachChild2(obj.get());
			}
			parent = nullptr;
		}
		void Show(bool a_on)
		{
			if (obj) {
				obj->SetAppCulled(!a_on);
			}
		}
		// place in world space (the parent's transform is undone)
		void Place(const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_scale)
		{
			if (!obj || !obj->parent) {
				return;
			}
			const auto&        pw = obj->parent->world;
			const RE::NiMatrix3 inv = pw.rotate.Transpose();
			const float        s = std::max(0.0001f, pw.scale);
			obj->local.translate = (inv * (a_at - pw.translate)) / s;
			obj->local.rotate = inv * a_rot;
			obj->local.scale = std::max(0.0001f, a_scale / s);
			RE::NiUpdateData ud;
			ud.time = 0.0f;
			obj->Update(ud);
		}
	};

	// a rotation whose +Y points along d
	RE::NiMatrix3 Facing(RE::NiPoint3 d)
	{
		if (d.Length() < 1e-4f) {
			d = { 0, 1, 0 };
		}
		d.Unitize();
		RE::NiPoint3 x = d.Cross({ 0, 0, 1 });
		if (x.Length() < 1e-3f) {
			x = { 1, 0, 0 };
		}
		x.Unitize();
		const RE::NiPoint3 z = x.Cross(d);
		RE::NiMatrix3      m;
		const float        xv[3] = { x.x, x.y, x.z }, yv[3] = { d.x, d.y, d.z }, zv[3] = { z.x, z.y, z.z };
		for (int i = 0; i < 3; ++i) {
			m.entry[i][0] = xv[i], m.entry[i][1] = yv[i], m.entry[i][2] = zv[i];
		}
		return m;
	}
	// +Y along d, and the flat face (local XY) turned toward the camera: a beam never goes edge-on and vanishes
	RE::NiMatrix3 FacingCam(RE::NiPoint3 d, const RE::NiPoint3& a_mid)
	{
		auto* cam = RE::PlayerCamera::GetSingleton();
		if (!cam || !cam->cameraRoot || d.Length() < 1e-4f) {
			return Facing(d);
		}
		d.Unitize();
		RE::NiPoint3 z = cam->cameraRoot->world.translate - a_mid;
		z = z - d * z.Dot(d);
		if (z.Length() < 1e-3f) {
			return Facing(d);
		}
		z.Unitize();
		const RE::NiPoint3 x = d.Cross(z);
		RE::NiMatrix3      m;
		const float        xv[3] = { x.x, x.y, x.z }, yv[3] = { d.x, d.y, d.z }, zv[3] = { z.x, z.y, z.z };
		for (int i = 0; i < 3; ++i) {
			m.entry[i][0] = xv[i], m.entry[i][1] = yv[i], m.entry[i][2] = zv[i];
		}
		return m;
	}
	// flat glows (local XZ plane) turned square to the eye: no crossed planes, no edges, from any side
	RE::NiMatrix3 FaceView(float a_roll = 0.0f)
	{
		auto* cam = RE::PlayerCamera::GetSingleton();
		RE::NiMatrix3 m;
		if (!cam || !cam->cameraRoot) {
			return AxisAngle({ 0, 0, 1 }, 0);
		}
		const auto& w = cam->cameraRoot->world.rotate;
		const RE::NiPoint3 r{ w.entry[0][0], w.entry[1][0], w.entry[2][0] }, f{ w.entry[0][1], w.entry[1][1], w.entry[2][1] }, u{ w.entry[0][2], w.entry[1][2], w.entry[2][2] };
		const RE::NiPoint3 x = -r, y = -f, z = u;
		const float xv[3] = { x.x, x.y, x.z }, yv[3] = { y.x, y.y, y.z }, zv[3] = { z.x, z.y, z.z };
		for (int i = 0; i < 3; ++i) {
			m.entry[i][0] = xv[i], m.entry[i][1] = yv[i], m.entry[i][2] = zv[i];
		}
		return a_roll != 0.0f ? m * AxisAngle({ 0, 1, 0 }, a_roll) : m;
	}

	RE::NiMatrix3 Spin(float a)  // a turn round world Z (bursts and novas)
	{
		return AxisAngle({ 0, 0, 1 }, a);
	}
	// A ball of the spell's power: its glow (faced to the eye) and its rings (turning in 3D round it).
	struct Ball
	{
		Fx  glow, rings;
		int row = -1;
		void Need(int a_row)
		{
			if (row == a_row && glow.obj) {
				return;
			}
			Detach();
			glow = Fx{ Load(gen::kSpellcraft[a_row].id, "glow") };
			rings = Fx{ Load(gen::kSpellcraft[a_row].id, "orb") };
			row = a_row;
		}
		void Attach(RE::NiNode* a_parent) { glow.Attach(a_parent), rings.Attach(a_parent); }
		void Detach() { glow.Detach(), rings.Detach(); }
		void Show(bool a_on) { glow.Show(a_on), rings.Show(a_on); }
		void Place(const RE::NiPoint3& a_at, const RE::NiMatrix3& a_spin, float a_scale, float a_glowMult = 1.0f)
		{
			glow.Place(a_at, FaceView(), a_scale * a_glowMult);
			rings.Place(a_at, a_spin, a_scale);
		}
	};



	// A beam (or a lightning streak): segments laid end to end from a to b, at width w (uniform scale, so a wider beam
	// takes fewer, longer segments).
	struct Beam
	{
		int             row = -1;
		std::vector<Fx> segs;
		void Lay(const RE::NiPoint3& a, const RE::NiPoint3& b, float w, bool a_jitter = false)
		{
			auto* root = WorldRoot();
			const RE::NiPoint3 d = b - a;
			const float        len = d.Length();
			const float        step = kSeg * w;
			const int          n = std::clamp(static_cast<int>(std::ceil(len / std::max(1.0f, step))), 1, 80);
			while (static_cast<int>(segs.size()) < n) {
				Fx f{ Load(gen::kSpellcraft[row].id, "beam") };
				if (!f.obj) {
					return;
				}
				f.Attach(root);
				segs.push_back(std::move(f));
			}
			const float sc = len / (n * kSeg);  // stretch the last bit evenly: each segment exactly covers its share
			RE::NiPoint3 prev = a;
			for (int i = 0; i < static_cast<int>(segs.size()); ++i) {
				if (i >= n) {
					segs[i].Show(false);
					continue;
				}
				RE::NiPoint3 next = a + d * (static_cast<float>(i + 1) / n);
				if (a_jitter && i + 1 < n) {
					next += RE::NiPoint3{ Rand(-1, 1), Rand(-1, 1), Rand(-1, 1) } * (14.0f * w);
				}
				segs[i].Show(true);
				segs[i].Place(prev, FacingCam(next - prev, (prev + next) * 0.5f), std::max(sc, (next - prev).Length() / kSeg));
				prev = next;
			}
		}
		void Clear()
		{
			for (auto& s : segs) {
				s.Detach();
			}
			segs.clear();
		}
	};

	// ---------------------------------------------------------------- state
	struct Burst
	{
		Fx    fx;
		float t = 0, dur, size;
		float spin;
		RE::NiPoint3 at;
	};
	struct Streak
	{
		Beam  beam;
		RE::NiPoint3 a, b;
		float t, w;
		bool  jitter;
	};
	std::vector<Burst>  g_bursts;
	std::vector<Streak> g_streaks;
	float g_time = 0;

	const gen::SpellcraftDef& Def(int a_row) { return gen::kSpellcraft[a_row]; }
	void  Play2D(int a_s, float a_vol = 1.0f)
	{
		if (a_s >= 0) {
			Audio::Ui(static_cast<gen::Sound>(a_s), a_vol);
		}
	}
	void PlayAt(int a_s, const RE::NiPoint3& a_at, float a_vol = 1.0f)
	{
		if (a_s >= 0) {
			Audio::AtPoint(static_cast<gen::Sound>(a_s), a_at, 0.0f, a_vol);
		}
	}

	void BurstAt(int a_row, const RE::NiPoint3& a_at, float a_radius, float a_dur = 0.45f)
	{
		if (g_bursts.size() >= 240) return;  // (the audit: a volley of arrows built hundreds of these - a hitch with the horde)
		Burst b{ { Load(Def(a_row).id, "burst") }, 0.0f, a_dur, a_radius / 40.0f, Rand(0, 6.28f), a_at };
		if (!b.fx.obj) {
			return;
		}
		b.fx.Attach(WorldRoot());
		b.fx.Place(a_at, Spin(b.spin), 0.01f);
		g_bursts.push_back(std::move(b));
		if (a_dur > 0.0f && a_radius > 0.0f && g_bursts.size() < 140) {  // the after-wave: a wider, slower second ring, and sparks of the spell thrown round
			Burst w{ { Load(Def(a_row).id, "burst") }, 0.0f, a_dur * 1.7f, a_radius * 1.6f / 40.0f, Rand(0, 6.28f), a_at };
			if (w.fx.obj) {
				w.fx.Attach(WorldRoot());
				w.fx.Place(a_at, Spin(w.spin), 0.01f);
				g_bursts.push_back(std::move(w));
			}
			for (int i = 0; i < 3; ++i) {
				const RE::NiPoint3 off{ Rand(-1, 1), Rand(-1, 1), Rand(0, 1) };
				Burst m{ { Load(Def(a_row).id, "aura") }, 0.0f, a_dur * 0.8f, a_radius * 0.35f / 13.0f, Rand(0, 6.28f), a_at + off * (a_radius * 0.6f) };
				if (m.fx.obj) {
					m.fx.Attach(WorldRoot());
					m.fx.Place(m.at, Spin(m.spin), 0.01f);
					g_bursts.push_back(std::move(m));
				}
			}
		}
	}

	void StreakAt(int a_row, const RE::NiPoint3& a, const RE::NiPoint3& b, float w, float t, bool jitter)
	{
		if (g_streaks.size() >= 200) return;
		Streak s{ {}, a, b, t, w, jitter };
		s.beam.row = a_row;
		s.beam.Lay(a, b, w, jitter);
		g_streaks.push_back(std::move(s));
	}

	// knock a body away from (or, with a negative push, toward) a point; big pushes send it flying
	void Fling(RE::Actor* a, const RE::NiPoint3& a_from, float a_mag)
	{
		auto* proc = RE::PlayerCharacter::GetSingleton()->GetActorRuntimeData().currentProcess;
		if (!proc || !a || a->IsDead()) {
			return;
		}
		RE::NiPoint3 from = a_from;
		if (a_mag < 0) {  // a pull: the push comes from the far side of it
			from = a->GetPosition() + (a->GetPosition() - a_from);
			a_mag = -a_mag;
		}
		from.z = a->GetPosition().z + 20.0f;
		proc->KnockExplosion(a, from, a_mag);
	}

	// ---------------------------------------------------------------- the hands: where spells leave from
	RE::NiAVObject* Bone(RE::PlayerCharacter* p, const char* a_name)
	{
		auto* cam = RE::PlayerCamera::GetSingleton();
		auto* root = p->Get3D(cam && cam->IsInFirstPerson());
		return root ? root->GetObjectByName(a_name) : nullptr;
	}
	// Where a spell leaves the hands, in the world. In first person the arms are drawn by their own camera (their
	// nodes do not sit where they appear in the world), so the point is taken from the view itself: just below and
	// right of the eye for the right hand, centre-low for the orb between both.
	bool FirstPerson()
	{
		auto* cam = RE::PlayerCamera::GetSingleton();
		return cam && cam->IsInFirstPerson();
	}
	RE::NiPoint3 ViewPoint(float a_fwd, float a_right, float a_down)
	{
		auto* cam = RE::PlayerCamera::GetSingleton();
		if (!cam || !cam->cameraRoot) {
			return RE::PlayerCharacter::GetSingleton()->GetPosition() + RE::NiPoint3{ 0, 0, 110 };
		}
		const auto&  w = cam->cameraRoot->world;
		const RE::NiPoint3 f{ w.rotate.entry[0][1], w.rotate.entry[1][1], w.rotate.entry[2][1] };
		const RE::NiPoint3 r{ w.rotate.entry[0][0], w.rotate.entry[1][0], w.rotate.entry[2][0] };
		const RE::NiPoint3 u{ w.rotate.entry[0][2], w.rotate.entry[1][2], w.rotate.entry[2][2] };
		return w.translate + f * a_fwd + r * a_right - u * a_down;
	}
	// a world hit along a segment (stopped by walls and ground), as a fraction 0..1, or >1 for none
	float WorldHit(const RE::NiPoint3& a, const RE::NiPoint3& b)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* world = player->GetParentCell() ? player->GetParentCell()->GetbhkWorld() : nullptr;
		if (!world) {
			return 2.0f;
		}
		const float s = RE::bhkWorld::GetWorldScale();
		RE::CFilter self{};
		player->GetCollisionFilterInfo(self);
		RE::bhkPickData pick;
		pick.rayInput.from = RE::hkVector4(a * s);
		pick.rayInput.to = RE::hkVector4(b * s);
		pick.rayInput.filterInfo.SetSystemGroup(self.GetSystemGroup());
		pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
		{
			RE::BSReadLockGuard lock(world->worldLock);
			world->PickObject(pick);
		}
		return pick.rayOutput.HasHit() ? pick.rayOutput.hitFraction : 2.0f;
	}

	float PointSeg(const RE::NiPoint3& p, const RE::NiPoint3& a, const RE::NiPoint3& b)
	{
		const RE::NiPoint3 v = b - a;
		const float        c = v.Dot(v);
		const float        t = c > 1e-6f ? std::clamp((p - a).Dot(v) / c, 0.0f, 1.0f) : 0.0f;
		return (a + v * t - p).Length();
	}

	void TickFx(float a_dt)
	{
		for (auto& b : g_bursts) {
			b.t += a_dt;
			const float u = b.t / b.dur;
			// swells fast, then folds away
			const float sz = u < 0.7f ? EaseOut(u / 0.7f) : 1.0f - EaseInOut((u - 0.7f) / 0.3f);
			if (b.fx.obj && b.fx.obj->parent) {
				b.fx.Place(b.at, Spin(b.spin + b.t * 2.0f), std::max(0.01f, b.size * sz));
			}
		}
		for (auto& b : g_bursts) {
			if (b.t >= b.dur) {
				b.fx.Detach();
			}
		}
		std::erase_if(g_bursts, [](const Burst& b) { return b.t >= b.dur; });
		for (auto& s : g_streaks) {
			s.t -= a_dt;
			if (s.jitter && s.t > 0) {
				s.beam.Lay(s.a, s.b, s.w, true);
			}
			if (s.t <= 0) {
				s.beam.Clear();
			}
		}
		std::erase_if(g_streaks, [](const Streak& s) { return s.t <= 0; });
	}

	// ---------------------------------------------------------------- every frame (Gun::Tick)
	void Tick(float a_dt)
	{
		g_time += a_dt;
		TickFx(a_dt);
	}

	void Reset()
	{
		for (auto& b : g_bursts) {
			b.fx.Detach();
		}
		g_bursts.clear();
		for (auto& s : g_streaks) {
			s.beam.Clear();
		}
		g_streaks.clear();
	}
}
