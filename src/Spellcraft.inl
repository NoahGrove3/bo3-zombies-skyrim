// The ten spells (sheet: spellcraft; looks: tools/spellfx.py; Noah's list, section 3). Included by Gun.cpp inside its
// anonymous namespace, after the shared helpers (Damage, Foes, AimAt, HeadPos, AxisAngle...).
//
//   left mouse   tap: the spell's own projectile from the right hand. Hold it a moment and the hand glows and hums;
//                past kTapFull the shot leaves empowered.
//   right mouse  both hands come together and build ONE orb between them; it swells, and at full power it pulses and
//                flashes. Let go at full: the spell's charged attack. Let go early: it gutters out (a quick tap still
//                shoots).
//   beams        (Arcane Lance) a visible beam from the orb, both hands pushed forward, held 2.5 s, piercing.
//
// Every projectile is ours: it flies, it is stopped by the world, and it hurts only what it touches (never on the click).
namespace Spellcraft
{
	constexpr float kTapFull = 0.6f;       // left-click hold: empowered from here
	constexpr float kTapEmpower = 1.5f;    // its damage and size
	constexpr float kBeamSeconds = 2.5f;   // Arcane Lance: how long the beam holds
	constexpr float kFreezeSeconds = 5.0f; // Cryo Freeze
	constexpr float kRootSeconds = 4.0f;   // Rootgrasp
	constexpr float kSeg = 64.0f;          // beam meshes are 64-unit segments along +Y (tools/spellfx.py)

	int Row(int a_gun)
	{
		static const auto rows = [] {
			std::array<int, gen::kGuns.size()> r{};
			for (std::size_t i = 0; i < r.size(); ++i) {
				r[i] = -1;
				for (std::size_t j = 0; j < gen::kSpellcraft.size(); ++j) {
					if (gen::kSpellcraft[j].id == gen::kGuns[i].id) {
						r[i] = static_cast<int>(j);
					}
				}
			}
			return r;
		}();
		return a_gun >= 0 && a_gun < static_cast<int>(rows.size()) ? rows[a_gun] : -1;
	}
	bool Is(int a_gun) { return Row(a_gun) >= 0; }

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

		void Attach(RE::NiNode* a_parent)
		{
			if (!obj || a_parent == parent) {
				return;
			}
			Detach();
			if (a_parent) {
				a_parent->AttachChild(obj.get(), true);
				parent = a_parent;
			}
		}
		void Detach()
		{
			if (obj && parent) {
				parent->DetachChild2(obj.get());
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
			if (!obj || !parent) {
				return;
			}
			const auto&        pw = parent->world;
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
	struct Shot
	{
		int          row, gun;
		RE::NiPoint3 pos, vel;
		float        life, radius, damage, scale;
		int          pierce;
		float        homing = 0, gravity = 0;
		bool         arrow = false;  // one of the altar's bows' arrows (its own mesh; a small glow at its head)
		int          bow = -1, tier = 0;  // ...which bow, at which reforge tier (Bows.inl: its hit and its landing)
		bool         full = false;        // ...loosed at full draw
		int          effect = -2;         // the effect it lays on what it strikes (-2: the spell's own)
		int          ult = -1;  // a charged attack's carrier (mortar, wind ball, singularity, soul wisp) or -1 for a tap
		std::vector<RE::FormID> struck;
		Fx           fx;   // the trail (along the flight)
		Ball         ball; // the charged ball of energy itself
		float        spin = 0;
		float        spark = 0;  // (an arrow: seconds to its next shed mote)
		float        flown = 0;  // how far it has come (an arrow ignores the world for its first stretch: the bow, your own arms)
		std::uint64_t loop = 0;
	};
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
	struct Hold  // frozen or rooted: the AI held still
	{
		RE::ActorHandle a;
		float           t;
	};
	struct Well  // Singularity: a black hole that drags the dead in, then collapses
	{
		int          row;
		RE::NiPoint3 at;
		float        t, tick = 0;
		Ball         ball;
	};

	std::vector<Shot>   g_shots2;
	// the altar's bows (Bows.inl): what their arrows do when they strike a body, or the world
	void (*g_bowHit)(const Shot&, RE::Actor*, const RE::NiPoint3&, bool) = nullptr;
	void (*g_bowGround)(const Shot&, const RE::NiPoint3&) = nullptr;
	std::vector<Burst>  g_bursts;
	std::vector<Streak> g_streaks;
	std::vector<Hold>   g_held;
	std::vector<Well>   g_wells;
	struct Pending { float t; RE::NiPoint3 at; int row; float dmg, radius; };
	std::vector<Pending> g_pending;  // Ice-Hand's Grip: the frozen burst a moment after the freeze
	std::uint64_t       g_loopId = 0;

	bool  g_tapHeld = false, g_orbHeld = false, g_tapFullSaid = false, g_orbFullSaid = false, g_orbStartSaid = false;
	float g_tapT = 0, g_orbT = 0, g_cd = 0, g_ultCd = 0;
	float g_handW = 0;  // how far the hands are drawn together (0 rest .. 1 the orb), eased
	float g_beamT = -1; // Arcane Lance: seconds left
	float g_beamTick = 0, g_time = 0, g_pushT = -1;
	int   g_beamRow = -1;
	Beam  g_beam;
	Fx    g_orb, g_hand;
	int   g_orbRow = -1, g_handRow = -1;
	Ball  g_ball[2], g_orbBall;
	float g_ballShown[2] = { 0, 0 }, g_orbLastT = 0;
	Fx    g_aura[2], g_flash[2];
	int   g_auraRow[2] = { -1, -1 }, g_flashRow[2] = { -1, -1 };
	float g_flashT[2] = { -1, -1 }, g_auraSpin[2] = { 0, 0 };
	int   g_ballRow[2] = { -1, -1 };
	float g_ballSpin[2] = { 0, 0 };
	RE::NiPoint3 g_orbAt{};
	bool  g_orbAtValid = false;
	float g_mpLast = -1;

	const gen::SpellcraftDef& Def(int a_row) { return gen::kSpellcraft[a_row]; }
	float Mult() { return Perks::DamageMult() * CrewMult(0) * (1.0f + gen::kForgeStep * ForgeLevel()); }
	int   Snd(int a_gun, int gen::GunSounds::*a_m) { return a_gun >= 0 ? gen::kGunSounds[a_gun].*a_m : -1; }
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

	// magicka: ours to spend (the spell form's own cost is shown in the Magic menu; the engine's charge is handed back)
	bool Spend(float a_cost)
	{
		auto* av = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
		const float cost = a_cost * Perks::ManaMult() * Difficulty::Mana();
		if (av->GetActorValue(RE::ActorValue::kMagicka) < cost) {
			Forms::Play(gen::Sound::charge_fizzle);
			Hud::Banner("Not enough magicka", 1.2f);
			return false;
		}
		av->DamageActorValue(RE::ActorValue::kMagicka, cost);
		g_manaQuiet = 0;
		g_mpLast = av->GetActorValue(RE::ActorValue::kMagicka);
		return true;
	}

	RE::BGSArtObject* Art(std::uint32_t a_id)
	{
		auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(a_id, gen::kPlugin);
		return f ? f->As<RE::BGSArtObject>() : nullptr;
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

	bool Held(RE::Actor* a)
	{
		return a && std::any_of(g_held.begin(), g_held.end(), [&](const Hold& h) { return h.t > 0 && h.a.get().get() == a; });
	}
	void Release(RE::Actor* a)
	{
		for (auto& h : g_held) {
			if (h.a.get().get() == a) {
				h.t = 0;
				a->EnableAI(true);
			}
		}
	}

	void HoldStill(RE::Actor* a, float a_seconds, std::uint32_t a_art)
	{
		if (!a || a->IsDead()) {
			return;
		}
		for (auto& h : g_held) {
			if (h.a.get().get() == a) {
				h.t = std::max(h.t, a_seconds);
				return;
			}
		}
		a->EnableAI(false);
		if (auto* art = Art(a_art)) {
			a->ApplyArtObject(art, a_seconds);
		}
		g_held.push_back({ a->GetHandle(), a_seconds });
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

	// one blow from a spell: damage, then what this spell does to what it hit
	bool Strike(int a_row, RE::Actor* a, float a_dmg, bool a_head, int a_effect, const RE::NiPoint3& a_at)
	{
		if (!a || a->IsDead()) {
			return false;
		}
		const auto& d = Def(a_row);
		float dmg = a_dmg;
		if (a_effect == gen::kFxHoly && Director::IsRoundUndead(a)) {
			dmg *= 1.25f;  // holy light sears the dead
		}
		bool shattered = false;
		if (a_effect == gen::kFxShatter && Held(a)) {
			Release(a);
			dmg *= 2.2f;
			shattered = true;
		}
		bool burning = false;
		for (auto& dd : g_dots) burning |= dd.target.get().get() == a;
		g_fecElem = FecOfEffect(a_effect == -2 ? d.effect : a_effect);
		if (g_fecElem < 0) g_fecElem = FecOfRow(a_row);  // (a bow's arrow: its element's death even without an effect)
		const bool killed = Damage(a, dmg, a_head, false);
		g_fecElem = -1;
		if (killed && shattered) Stats::Style("SHATTER", 40);
		if (killed && burning && a_effect != gen::kFxIgnite) Stats::Style("COMBUSTION", 40);
		if (killed && (a_effect == gen::kFxChain) && a->IsInWater()) Stats::Style("STORM SURGE", 40);
		if (shattered) {
			const RE::NiPoint3 c = a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f };
			BurstAt(a_row, c, 220.0f, 0.5f);
			Gun::FxBlast("frost", c);
			for (auto* f : Foes(c, 240.0f)) {
				if (f != a) {
					Damage(f, a_dmg * 0.9f, false, false);
				}
			}
			a_effect = gen::kFxNone;
		}
		auto*      pav = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
		switch (a_effect) {
		case gen::kFxIgnite:
			if (!killed) {
				g_dots.push_back({ a->GetHandle(), dmg * 0.25f, 3.0f });
				if (auto* art = Art(gen::kArtBurn)) {
					a->ApplyArtObject(art, 3.0f);
				}
			}
			break;
		case gen::kFxFreeze:
			if (!killed) {
				HoldStill(a, kFreezeSeconds, gen::kArtIceShell);
			}
			break;
		case gen::kFxShatter:  // Frostbinder: binds in ice; strike what is already bound and it bursts, the shards cutting all round it
			if (!killed) {
				HoldStill(a, 3.0f, gen::kArtIceShell);
			}
			break;
		case gen::kFxPoison:
			if (!killed) {
				g_dots.push_back({ a->GetHandle(), dmg * 0.3f, 4.0f });
				if (auto* art = Art(gen::kArtPoison)) {
					a->ApplyArtObject(art, 4.0f);
				}
			}
			break;
		case gen::kFxDrain:
			pav->RestoreActorValue(RE::ActorValue::kHealth, pav->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.03f);
			break;
		case gen::kFxPush:
			if (!killed) {
				Fling(a, a_at, 1.4f);
			}
			break;
		case gen::kFxHoly:
			if (!killed && Rand(0, 1) < 0.35f) {
				Stagger(a, 0.6f);
			}
			break;
		case gen::kFxPull:
			for (auto* f : Foes(a_at, 260.0f)) {
				if (f != a) {
					Fling(f, a_at, -0.9f);
				}
			}
			break;
		case gen::kFxChain:
		{  // leaps on to three more nearby, weaker each time, a crackling streak between
			std::vector<RE::Actor*> chained{ a };
			RE::Actor*              cur = a;
			float                   cd = dmg;
			for (int k = 0; k < 3; ++k) {
				RE::Actor* next = nullptr;
				float      nd = 450.0f;
				for (auto* f : Foes(cur->GetPosition(), 450.0f)) {
					if (std::find(chained.begin(), chained.end(), f) == chained.end()) {
						if (const float dd = f->GetPosition().GetDistance(cur->GetPosition()); dd < nd) {
							nd = dd, next = f;
						}
					}
				}
				if (!next) {
					break;
				}
				cd *= 0.7f;
				StreakAt(a_row, HeadPos(cur) - RE::NiPoint3{ 0, 0, 30 }, HeadPos(next) - RE::NiPoint3{ 0, 0, 30 }, 0.5f, 0.18f, true);
				Damage(next, cd, false, false);
				chained.push_back(next);
				cur = next;
			}
			break;
		}
		default:
			break;
		}
		(void)d;
		return killed;
	}

	// everything within a radius of a point (a burst, a nova)
	int Area(int a_row, const RE::NiPoint3& a_at, float a_radius, float a_dmg, int a_effect)
	{
		int n = 0;
		for (auto* f : Foes(a_at, a_radius)) {
			const float fall = 1.0f - 0.4f * std::min(1.0f, f->GetPosition().GetDistance(a_at) / std::max(1.0f, a_radius));
			Strike(a_row, f, a_dmg * fall, false, a_effect, a_at);
			++n;
		}
		return n;
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
	RE::NiPoint3 HandAt(RE::PlayerCharacter* p)
	{
		if (FirstPerson()) {
			return ViewPoint(48.0f, 13.0f, 15.0f);
		}
		if (auto* m = Bone(p, "NPC R MagicNode [RMag]")) {
			return m->world.translate;
		}
		return AimAt(100.0f).eye;
	}
	RE::NiPoint3 OrbAt(RE::PlayerCharacter* p)
	{
		if (FirstPerson()) {
			return ViewPoint(52.0f, 0.0f, 16.0f);
		}
		return g_orbAtValid ? g_orbAt : HandAt(p);
	}

	// ---------------------------------------------------------------- projectiles
	void Launch(int a_row, int a_gun, const RE::NiPoint3& a_from, RE::NiPoint3 a_dir, float a_speed, float a_dmg, float a_radius, int a_pierce, float a_scale, int a_ult = -1, bool a_arrow = false)
	{
		a_dir.Unitize();
		if (a_gun >= 0 && a_gun < static_cast<int>(gen::kGuns.size()) && gen::kGuns[a_gun].cls == "wonder") {
			g_propKickT = 0.0f, g_propKickBig = a_ult >= 0;   // (the wonder's model kicks with the shot)
		}
		const auto& d = Def(a_row);
		Shot s{ a_row, a_gun, a_from, a_dir * a_speed, 2.4f, a_radius, a_dmg, a_scale, a_pierce };
		s.homing = a_arrow ? 0.0f : d.homing;
		s.ult = a_ult;
		s.arrow = a_arrow;
		if (a_ult == gen::kUltMortar) {
			s.gravity = 1300.0f, s.life = 4.0f;
		} else if (a_ult == gen::kUltSingularity) {
			s.life = 0.9f;
		} else if (a_ult == gen::kUltWindBall) {
			s.life = 1.6f;
		} else if (a_ult == gen::kUltSoulStorm) {
			s.homing = 7.0f, s.life = 3.5f;
		}
		const bool carrier = a_ult >= 0 && a_ult != gen::kUltSoulStorm;
		if (!carrier) {
			s.fx.obj = Load(d.id, a_arrow ? "arrow" : "bolt");  // its trail (the altar's bows: an energy arrow and its trail)
			if (s.fx.obj) {
				s.fx.Attach(WorldRoot());
				s.fx.Place(s.pos, Facing(a_dir), 1.2f * a_scale);
			}
		}
		s.ball.Need(a_row);
		s.ball.Attach(WorldRoot());
		s.ball.Place(s.pos, AxisAngle(a_dir, 0.0f), (carrier ? 2.4f : a_arrow ? 0.5f : 0.62f) * a_scale);
		if (const int tr = Snd(a_gun, &gen::GunSounds::travel); tr >= 0 && g_shots2.size() < 6) {
			s.loop = 0x5C00000000ull + (++g_loopId);
			Audio::LoopAt(s.loop, static_cast<gen::Sound>(tr), s.pos, 0.7f);
		}
		g_shots2.push_back(std::move(s));
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

	// The body a spell can touch: every bone of the skeleton as a capsule (limbs thin, trunk and head thicker), read
	// once a frame per body, so a hit lands wherever the model is (arms, legs, head; the big ones too).
	struct BoneCap { RE::NiPoint3 a, b; float r; bool head; };
	struct BodyShape { float at = -1; std::vector<BoneCap> caps; };
	std::unordered_map<RE::FormID, BodyShape> g_bodies;

	bool Skip(std::string_view n)
	{
		for (const char* bad : { "eapon", "EAPON", "hield", "HIELD", "uiver", "UIVER", "Magic", "Camera", "AnimObject", "x_", "Look", "Scb", "SCB", "Tail" }) {
			if (n.find(bad) != std::string_view::npos) {
				return true;
			}
		}
		return false;
	}
	void Walk(RE::NiNode* n, BodyShape& b, float s, int depth)
	{
		if (!n || depth > 40) {
			return;
		}
		for (auto& c : n->GetChildren()) {
			auto* cn = c ? c->AsNode() : nullptr;
			if (!cn) {
				continue;
			}
			const std::string_view nm = cn->name.c_str() ? cn->name.c_str() : "";
			if (Skip(nm)) {
				continue;
			}
			const float len = n->world.translate.GetDistance(cn->world.translate);
			if (len > 0.5f && len < 90.0f * s) {
				const std::string_view pn = n->name.c_str() ? n->name.c_str() : "";
				const bool head = nm.find("Head") != std::string_view::npos || pn.find("Head") != std::string_view::npos;
				const bool trunk = pn.find("Spine") != std::string_view::npos || pn.find("Pelvis") != std::string_view::npos || pn.find("Neck") != std::string_view::npos ||
				                   pn.find("Root") != std::string_view::npos || nm.find("Spine") != std::string_view::npos;
				b.caps.push_back({ n->world.translate, cn->world.translate, (head ? 12.0f : trunk ? 17.0f : 9.0f) * s, head });
			}
			Walk(cn, b, s, depth + 1);
		}
	}
	const BodyShape& Body(RE::Actor* a)
	{
		auto& b = g_bodies[a->GetFormID()];
		if (b.at == g_time) {
			return b;
		}
		b.at = g_time;
		b.caps.clear();
		auto* root = a->Get3D(false);
		if (auto* rn = root ? root->AsNode() : nullptr) {
			auto* start = rn->GetObjectByName("NPC Root [Root]");
			auto* sn = start && start->AsNode() ? start->AsNode() : rn;
			Walk(sn, b, std::max(0.3f, rn->world.scale), 0);
		}
		if (b.caps.size() < 4) {  // no skeleton to read: a capsule from the feet to the head
			b.caps.clear();
			b.caps.push_back({ a->GetPosition() + RE::NiPoint3{ 0, 0, 15 }, HeadPos(a) - RE::NiPoint3{ 0, 0, 12 }, 26.0f, false });
			b.caps.push_back({ HeadPos(a) - RE::NiPoint3{ 0, 0, 12 }, HeadPos(a) + RE::NiPoint3{ 0, 0, 8 }, 14.0f, true });
		}
		return b;
	}
	// does a ball of radius a_r at p touch this body? (head: the nearest bone it touched was the head's)
	bool Touches(RE::Actor* a, const RE::NiPoint3& p, float a_r, bool* a_head = nullptr)
	{
		float best = 1e9f;
		bool  hit = false, head = false;
		for (const auto& c : Body(a).caps) {
			const float d = PointSeg(p, c.a, c.b) - c.r;
			if (d <= a_r && d < best) {
				best = d, hit = true, head = c.head;
			}
		}
		if (a_head) {
			*a_head = head;
		}
		return hit;
	}
	// does a line (a beam) of half-width a_w pass through this body?
	bool Crosses(RE::Actor* a, const RE::NiPoint3& from, const RE::NiPoint3& to, float a_w)
	{
		for (const auto& c : Body(a).caps) {
			for (float t : { 0.0f, 0.5f, 1.0f }) {
				if (PointSeg(c.a + (c.b - c.a) * t, from, to) <= a_w + c.r) {
					return true;
				}
			}
		}
		return false;
	}

	void Detonate(Shot& s, const RE::NiPoint3& at)
	{
		const auto& d = Def(s.row);
		switch (s.ult) {
		case gen::kUltMortar:
			Area(s.row, at, d.ultRadius, d.ultDamage * Mult(), gen::kFxIgnite);
			BurstAt(s.row, at, d.ultRadius * 0.8f, 0.6f);
			PlayAt(Snd(s.gun, &gen::GunSounds::boom), at);
			break;
		case gen::kUltWindBall:
			for (auto* f : Foes(at, d.ultRadius)) {
				const bool killed = Strike(s.row, f, d.ultDamage * Mult() * (1.0f - 0.5f * std::min(1.0f, f->GetPosition().GetDistance(at) / d.ultRadius)), false, gen::kFxNone, at);
				if (!killed) {
					Fling(f, at, 6.0f);  // flung far from the blast
				} else {
					Fling(f, at, 4.0f);
				}
			}
			BurstAt(s.row, at, d.ultRadius, 0.55f);
			PlayAt(Snd(s.gun, &gen::GunSounds::boom), at);
			break;
		case gen::kUltSingularity:
		{
			Well w{ s.row, at, 2.6f };
			w.ball.Need(s.row);
			w.ball.Attach(WorldRoot());
			w.ball.Place(at, Spin(0), 3.0f);
			g_wells.push_back(std::move(w));
			PlayAt(Snd(s.gun, &gen::GunSounds::charge), at);
			break;
		}
		default:
			PlayAt(Snd(s.gun, &gen::GunSounds::hit), at, 0.9f);
			BurstAt(s.row, at, std::max(40.0f, s.radius * 2.5f), 0.3f);
			break;
		}
	}

	void TickShots(float a_dt)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		for (auto& s : g_shots2) {
			s.life -= a_dt;
			if (s.life <= 0) {
				if (s.ult >= 0 && s.ult != gen::kUltSoulStorm) {
					Detonate(s, s.pos);  // carriers burst where they run out
				}
				continue;
			}
			// homing: lean toward the nearest of the dead ahead
			if (s.homing > 0) {
				RE::Actor* best = nullptr;
				float      bd = 2500.0f;
				RE::NiPoint3 dir = s.vel;
				dir.Unitize();
				for (auto* f : Foes(s.pos, 2500.0f)) {
					if (std::find(s.struck.begin(), s.struck.end(), f->GetFormID()) != s.struck.end()) {
						continue;
					}
					const RE::NiPoint3 to = HeadPos(f) - RE::NiPoint3{ 0, 0, 35 } - s.pos;
					if (to.Dot(dir) > 0 && to.Length() < bd) {
						bd = to.Length(), best = f;
					}
				}
				if (best) {
					RE::NiPoint3 to = HeadPos(best) - RE::NiPoint3{ 0, 0, 35 } - s.pos;
					to.Unitize();
					const float sp = s.vel.Length();
					dir = dir + (to - dir) * std::min(1.0f, s.homing * a_dt);
					dir.Unitize();
					s.vel = dir * sp;
				}
			}
			s.vel.z -= s.gravity * a_dt;
			const RE::NiPoint3 from = s.pos, to = s.pos + s.vel * a_dt;
			float              wf = WorldHit(from, to);
			if (s.arrow && s.flown < 90.0f && wf <= 1.0f && (to - from).Length() * wf + s.flown < 90.0f) {
				wf = 2.0f;  // (still leaving the bow)
			}
			s.flown += (to - from).Length();
			if (s.arrow) {  // (Noah: an elemental trail behind the arrow in flight) a streak of the element along its path, lingering
				const RE::NiPoint3 tail = wf <= 1.0f ? from + (to - from) * wf : to;
				// (Noah: still no trail to be seen) wide enough to read at a bow's speed, and lingering most of a second
				StreakAt(s.row, from, tail, (s.full ? 2.8f : 2.0f) * s.scale, 0.9f, Def(s.row).style == 3);
				s.spark -= a_dt;
				if (s.spark <= 0) {  // and motes of it shed as it goes
					s.spark = 0.03f;
					BurstAt(s.row, tail, 24.0f, 0.5f);
				}
			}
			const RE::NiPoint3 end = wf <= 1.0f ? from + (to - from) * wf : to;
			// what it touches on the way: substeps no longer than its radius
			const float len = (end - from).Length();
			const int   n = std::clamp(static_cast<int>(len / std::max(8.0f, s.radius)) + 1, 1, 40);
			bool        stop = false;
			const auto  foes = Foes(from, len + 400.0f);
			// a body struck: true when the shot ends there
			auto strike = [&](RE::Actor* f, const RE::NiPoint3& p, bool head2) {
				s.struck.push_back(f->GetFormID());
				if (s.ult >= 0 && s.ult != gen::kUltSoulStorm) {  // a carrier bursts on the first body
					s.pos = p;
					Detonate(s, p);
					s.life = 0;
					return true;
				}
				const int   gi = s.gun;
				const float hm = head2 ? gen::kGuns[gi].headMult * Perks::HeadMult() : 1.0f;
				const float pierceFall = s.struck.size() > 1 ? 0.85f : 1.0f;
				const bool  killedNow = Strike(s.row, f, s.damage * hm * pierceFall, head2, s.ult == gen::kUltSoulStorm ? gen::kFxNone : s.effect != -2 ? s.effect : Def(s.row).effect, p);
				if (s.bow >= 0 && g_bowHit) {
					g_bowHit(s, f, p, killedNow);
				}
				PlayAt(Snd(s.gun, &gen::GunSounds::hit), p, 0.85f);
				if (static_cast<int>(s.struck.size()) >= s.pierce) {
					BurstAt(s.row, p, std::max(36.0f, s.radius * 2.0f), 0.25f);
					s.life = 0;
					s.pos = p;
					return true;
				}
				return false;
			};
			for (int i = 1; i <= n && !stop; ++i) {
				const RE::NiPoint3 p = from + (end - from) * (static_cast<float>(i) / n);
				for (auto* f : foes) {
					if (f->IsDead() || std::find(s.struck.begin(), s.struck.end(), f->GetFormID()) != s.struck.end()) {
						continue;
					}
					if ((p - f->GetPosition()).Length() > 600.0f) {
						continue;  // far off: not worth reading its bones
					}
					bool head2 = false;
					if (!Touches(f, p, s.radius, &head2)) {
						continue;
					}
					if (strike(f, p, head2)) {
						stop = true;
						break;
					}
				}
				s.pos = p;
			}
			if (!stop && wf <= 1.0f) {
				// (Noah: spells and arrows that seemed to hit did nothing) the ray stops on the body's own collision (its
				// capsule), a hand's width outside the bones it is tested against: a stop right at someone counts as them
				for (auto* f : foes) {
					if (f->IsDead() || std::find(s.struck.begin(), s.struck.end(), f->GetFormID()) != s.struck.end()) {
						continue;
					}
					const auto  fp = f->GetPosition();
					const float hz = HeadPos(f).z;
					const float dh = std::hypot(end.x - fp.x, end.y - fp.y);
					if (dh < 62.0f + s.radius && end.z > fp.z - 10.0f && end.z < hz + 30.0f) {
						if (strike(f, end, end.z > hz - 22.0f)) {
							stop = true;
						}
						break;
					}
				}
			}
			if (!stop && wf <= 1.0f) {  // the world stopped it
				if (s.bow >= 0) {
					static int said = 0;
					if (said++ < 8) {
						logger::info("bows: arrow struck the world after {:.0f} units", s.flown);
					}
				}
				s.pos = end;
				if (s.bow >= 0 && g_bowGround) {
					g_bowGround(s, end);
				}
				Detonate(s, end - s.vel * (2.0f / std::max(1.0f, s.vel.Length())));
				s.life = 0;
			}
			if (s.life > 0) {
				s.pos = end;
				const bool carrier = s.ult >= 0 && s.ult != gen::kUltSoulStorm;
				s.spin += a_dt * 9.0f;
				const float breathe = 1.0f + 0.07f * std::sin(g_time * 14.0f + s.pos.x * 0.01f);
				if (s.fx.obj) {
					s.fx.Place(s.pos, Facing(s.vel), 1.2f * s.scale);
				}
				s.ball.Place(s.pos, Facing(s.vel) * AxisAngle({ 0, 1, 0 }, s.spin) * AxisAngle({ 1, 0, 0 }, s.spin * 0.6f), (carrier ? 2.4f : s.arrow ? 0.5f : 0.62f) * s.scale * breathe);
				if (s.loop) {
					Audio::LoopMove(s.loop, s.pos);
				}
			}
		}
		for (auto& s : g_shots2) {
			if (s.life <= 0) {
				s.fx.Detach();
				s.ball.Detach();
				if (s.loop) {
					Audio::LoopStop(s.loop, 120);
				}
			}
		}
		std::erase_if(g_shots2, [](const Shot& s) { return s.life <= 0; });
		(void)player;
	}

	// dev: three of this spell's meshes held still in front of the eye (bolt, burst, orb), to see what renders
	struct Probe { Fx fx; RE::NiPoint3 at; float t; float scale; };
	std::vector<Probe> g_probes;
	void SpawnProbe(int a_row)
	{
		// centre column, top to bottom: bolt, orb, burst; then a burst on each side
		struct P { const char* part; float fwd, right, down, scale; };
		const P ps[] = { { "bolt", 200, 0, -45, 1.0f }, { "orb", 200, 0, 0, 1.0f }, { "burst", 200, 0, 45, 0.5f },
			{ "burst", 200, -70, 0, 0.5f }, { "burst", 200, 70, 0, 0.5f }, { "bolt", 200, 70, -45, 1.0f } };
		for (const auto& q : ps) {
			Probe p{ { Load(Def(a_row).id, q.part) }, ViewPoint(q.fwd, q.right, q.down), 10.0f, q.scale };
			if (p.fx.obj) {
				p.fx.Attach(WorldRoot());
				g_probes.push_back(std::move(p));
			}
		}
		logger::info("probe: {} meshes placed", g_probes.size());
	}
	void TickFx(float a_dt)
	{
		for (auto& p : g_probes) {
			p.t -= a_dt;
			p.fx.Place(p.at, Spin(g_time), p.scale);
			if (p.t <= 0) {
				p.fx.Detach();
			}
		}
		std::erase_if(g_probes, [](const Probe& p) { return p.t <= 0; });
		for (auto& b : g_bursts) {
			b.t += a_dt;
			const float u = b.t / b.dur;
			// swells fast, then folds away
			const float sz = u < 0.7f ? EaseOut(u / 0.7f) : 1.0f - EaseInOut((u - 0.7f) / 0.3f);
			if (b.fx.parent) {
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
		for (auto& h : g_held) {
			auto a = h.a.get();
			h.t -= a_dt;
			if (!a || a->IsDead() || h.t <= 0) {
				if (a) {
					a->EnableAI(true);
				}
				h.t = 0;
			}
		}
		std::erase_if(g_held, [](const Hold& h) { return h.t <= 0; });
		for (auto& w : g_wells) {
			w.t -= a_dt;
			const auto& d = Def(w.row);
			if ((w.tick -= a_dt) <= 0) {
				w.tick = 0.4f;
				for (auto* f : Foes(w.at, d.ultRadius)) {
					if (f->GetPosition().GetDistance(w.at) > 90.0f) {
						Fling(f, w.at, -1.1f);  // dragged in
					}
					Damage(f, d.ultDamage * Mult() * 0.06f, false, false, true);
				}
			}
			w.ball.Place(w.at, Spin(g_time * 5.0f) * AxisAngle({ 1, 0, 0 }, g_time * 2.0f), 3.0f + 0.6f * std::sin(g_time * 9.0f));
			if (w.t <= 0) {  // the collapse
				Area(w.row, w.at, d.ultRadius * 0.5f, d.ultDamage * Mult(), gen::kFxNone);
				BurstAt(w.row, w.at, d.ultRadius * 0.6f, 0.5f);
				const int gun = [&] { for (int i = 0; i < static_cast<int>(gen::kGuns.size()); ++i) if (Row(i) == w.row) return i; return -1; }();
				PlayAt(Snd(gun, &gen::GunSounds::boom), w.at);
				w.ball.Detach();
			}
		}
		std::erase_if(g_wells, [](const Well& w) { return w.t <= 0; });
	}

	// ---------------------------------------------------------------- casting
	void Tap(int a_gun, bool a_empowered)
	{
		const int row = Row(a_gun);
		if (row < 0 || g_cd > 0) {
			return;
		}
		const auto& g = gen::kGuns[a_gun];
		const auto& d = Def(row);
		if (!Spend(g.mana * (a_empowered ? 1.5f : 1.0f))) {
			g_cd = 0.35f;
			return;
		}
		auto*       p = RE::PlayerCharacter::GetSingleton();
		const Aim   aim = AimAt(std::min(g.range, 8000.0f));
		const RE::NiPoint3 from = HandAt(p);
		const float k = a_empowered ? kTapEmpower : 1.0f;
		const float dmg = d.damage * Mult() * k;
		Forms::Play(g.fireSnd);
		g_flashT[1] = 0.0f;  // the right hand flares as the shot leaves
		++g_shots;
		// aim from the hand to what the crosshair is on
		RE::NiPoint3 dir = aim.hit - from;
		dir.Unitize();
		switch (d.tap) {
		case gen::kTapFan:
			for (int i = 0; i < d.count; ++i) {
				const float off = (d.count > 1 ? (static_cast<float>(i) / (d.count - 1) - 0.5f) : 0.0f) * 2.0f * d.spread;
				const RE::NiPoint3 dd = AxisAngle({ 0, 0, 1 }, off * RE::NI_PI / 180.0f) * dir;
				Launch(row, a_gun, from, Spread(dd, 1.0f), d.speed, dmg, d.radius * k, d.pierce, k);
			}
			break;
		case gen::kTapGust:  // a broad wave: slower, wide, cuts through three or four
			Launch(row, a_gun, from, dir, d.speed, dmg, d.radius * k, d.pierce + (a_empowered ? 1 : 0), 1.4f * k);
			break;
		default:
			Launch(row, a_gun, from, dir, d.speed, dmg, d.radius * k, d.pierce, k);
			break;
		}
		g_cd = g.fireDelayMs / 1000.0f * Perks::FireDelayMult();
		Perks::OnCast(g_shots);
	}

	void Ult(int a_gun)
	{
		const int row = Row(a_gun);
		if (row < 0) {
			return;
		}
		const auto& g = gen::kGuns[a_gun];
		const auto& d = Def(row);
		if (g_ultCd > 0) {
			Play2D(Snd(a_gun, &gen::GunSounds::fizzle));
			return;
		}
		if (!Spend(g.mana * g.chargeMana)) {
			return;
		}
		g_ultCd = g.chargeCooldown * Perks::ChargeMult();
		auto*       p = RE::PlayerCharacter::GetSingleton();
		const Aim   aim = AimAt(8000.0f);
		const RE::NiPoint3 from = OrbAt(p);
		const RE::NiPoint3 feet = p->GetPosition() + RE::NiPoint3{ 0, 0, 40 };
		RE::NiPoint3       dir = aim.hit - from;
		dir.Unitize();
		const float dmg = d.ultDamage * Mult();
		Forms::Play(g.chargeSnd);  // the spell's own unleash
		g_flashT[0] = g_flashT[1] = 0.0f;
		BurstAt(row, from + dir * 28.0f, 55.0f, 0.38f);  // the power bursts out of the hands as it is let go
		Audio::Ui(gen::Sound::release_power, 0.7f);
		Hud::Banner(std::string(d.ultName), 1.2f);
		g_pushT = 0.0f;
		switch (d.ult) {
		case gen::kUltLance:
			g_beamT = kBeamSeconds, g_beamTick = 0, g_beamRow = row;
			g_beam.row = row;
			break;
		case gen::kUltMortar:
		{
			RE::NiPoint3 lob = dir + RE::NiPoint3{ 0, 0, 0.35f };
			Launch(row, a_gun, from, lob, 2600.0f, dmg, 30.0f, 1, 1.0f, d.ult);
			break;
		}
		case gen::kUltWindBall:
			Launch(row, a_gun, from, dir, 2400.0f, dmg, 40.0f, 1, 1.0f, d.ult);
			break;
		case gen::kUltSingularity:
			Launch(row, a_gun, from, dir, 1500.0f, dmg, 30.0f, 1, 1.0f, d.ult);
			break;
		case gen::kUltSoulStorm:
			for (int i = 0; i < 6; ++i) {
				const RE::NiPoint3 dd = AxisAngle({ 0, 0, 1 }, (i - 2.5f) * 0.32f) * (dir + RE::NiPoint3{ 0, 0, 0.25f });
				Launch(row, a_gun, from, dd, d.speed, dmg, d.radius, 1, 1.3f, d.ult);
			}
			break;
		case gen::kUltNova:
			for (auto* f : Foes(feet, d.ultRadius)) {
				if (!Strike(row, f, dmg, false, gen::kFxNone, feet)) {
					HoldStill(f, kFreezeSeconds, gen::kArtIceShell);
				}
			}
			BurstAt(row, feet, d.ultRadius, 0.6f);
			PlayAt(Snd(a_gun, &gen::GunSounds::boom), feet);
			break;
		case gen::kUltBloodNova:
		{
			const int n = Area(row, feet, d.ultRadius, dmg, gen::kFxNone);
			auto*     av = p->AsActorValueOwner();
			av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * std::min(0.4f, 0.05f * n));
			BurstAt(row, feet, d.ultRadius, 0.6f);
			PlayAt(Snd(a_gun, &gen::GunSounds::boom), feet);
			break;
		}
		case gen::kUltSunburst:
			for (auto* f : Foes(feet, d.ultRadius)) {
				if (!Strike(row, f, dmg, false, gen::kFxIgnite, feet)) {
					Stagger(f, 0.9f);  // blinded
				}
			}
			BurstAt(row, feet, d.ultRadius, 0.7f);
			PlayAt(Snd(a_gun, &gen::GunSounds::boom), feet);
			break;
		case gen::kUltThunder:
		{
			const RE::NiPoint3 at = aim.hit;
			StreakAt(row, at + RE::NiPoint3{ Rand(-80, 80), Rand(-80, 80), 1600.0f }, at, 2.2f, 0.35f, true);
			for (auto* f : Foes(at, d.ultRadius)) {
				if (!Strike(row, f, dmg * (1.0f - 0.4f * std::min(1.0f, f->GetPosition().GetDistance(at) / d.ultRadius)), false, gen::kFxNone, at)) {
					Stagger(f, 1.0f);
				}
			}
			BurstAt(row, at, d.ultRadius * 0.8f, 0.5f);
			PlayAt(Snd(a_gun, &gen::GunSounds::boom), at);
			break;
		}
		case gen::kUltRoots:
		{
			const RE::NiPoint3 at = aim.hit;
			for (auto* f : Foes(at, d.ultRadius)) {
				if (!Strike(row, f, dmg, false, gen::kFxPoison, at)) {
					HoldStill(f, kRootSeconds, gen::kArtRoots);
				}
			}
			BurstAt(row, at, d.ultRadius, 0.6f);
			PlayAt(Snd(a_gun, &gen::GunSounds::boom), at);
			break;
		}
		case gen::kUltGravecall:  // Gravecaller: the ground breaks open round you and the barrow's warriors climb out to fight for you
			Area(row, feet, d.ultRadius, dmg, gen::kFxDrain);
			BurstAt(row, feet, d.ultRadius, 0.8f);
			Gun::FxBlast("dark", feet);
			PlayAt(Snd(a_gun, &gen::GunSounds::boom), feet);
			Echo::Allies(0, 3, 25.0f, feet);
			break;
		case gen::kUltCataclysm:  // Frostbinder: everything round you is bound in ice - and a heartbeat later it bursts
			for (auto* f : Foes(feet, d.ultRadius)) {
				HoldStill(f, 1.6f, gen::kArtIceShell);
			}
			BurstAt(row, feet, d.ultRadius, 0.7f);
			Gun::FxBlast("icevolley", feet);
			g_pending.push_back({ 1.1f, feet, row, dmg, d.ultRadius });
			break;
		case gen::kUltWolfpack:  // the Crown: the Queen's howl staggers the dead, and her spirit-wolves run with you
			for (auto* f : Foes(feet, d.ultRadius)) {
				if (!Strike(row, f, dmg, false, gen::kFxNone, feet)) {
					Stagger(f, 0.9f);
				}
			}
			BurstAt(row, feet, d.ultRadius, 0.7f);
			PlayAt(Snd(a_gun, &gen::GunSounds::boom), feet);
			Echo::Allies(1, 3, 30.0f, feet);
			break;
		case gen::kUltWarhost:  // the Horn: a line of the war-host's spectral blades charges down the field in front of you
		{
			RE::NiPoint3 flat{ dir.x, dir.y, 0 };
			if (flat.Length() < 0.01f) {
				flat = { 0, 1, 0 };
			}
			flat.Unitize();
			const RE::NiPoint3 side{ flat.y, -flat.x, 0 };
			for (int i = 0; i < 7; ++i) {
				const float off = (i - 3) * 95.0f;
				Launch(row, a_gun, feet + RE::NiPoint3{ 0, 0, 30.0f } + side * off - flat * std::abs(off) * 0.25f, flat, 1500.0f, dmg, d.ultRadius, 99, 1.6f);
			}
			Gun::FxBlast("shoutarea", feet);
			PlayAt(Snd(a_gun, &gen::GunSounds::boom), feet);
			break;
		}
		default:
			break;
		}
	}

	void TickPending(float a_dt)
	{
		for (auto& q : g_pending) {
			if ((q.t -= a_dt) > 0) {
				continue;
			}
			for (auto* f : Foes(q.at, q.radius * 1.1f)) {  // the bound burst: frozen or not, they all take the shards
				Release(f);
				const float fall = 1.0f - 0.35f * std::min(1.0f, f->GetPosition().GetDistance(q.at) / std::max(1.0f, q.radius));
				Damage(f, q.dmg * fall, false, false);
			}
			BurstAt(q.row, q.at, q.radius * 1.1f, 0.6f);
			Gun::FxBlast("frost", q.at);
		}
		std::erase_if(g_pending, [](const Pending& q) { return q.t <= 0; });
	}

	void TickBeam(float a_dt)
	{
		if (g_beamT < 0) {
			return;
		}
		g_beamT -= a_dt;
		const int gun = CurrentGun();
		if (g_beamT <= 0 || Row(gun) != g_beamRow) {
			g_beam.Clear();
			g_beamT = -1;
			Audio::LoopStop(Audio::kLoopSpell, 200);
			return;
		}
		const auto& d = Def(g_beamRow);
		const Aim   aim = AimAt(4500.0f);
		RE::NiPoint3 from = OrbAt(RE::PlayerCharacter::GetSingleton());
		{  // the beam leaves a little ahead of the hands, so it does not fill the view
			RE::NiPoint3 fd = aim.hit - from;
			fd.Unitize();
			from += fd * 20.0f;
		}
		const float grow = std::min(1.0f, (kBeamSeconds - g_beamT) / 0.15f) * std::min(1.0f, g_beamT / 0.2f);
		g_beam.Lay(from, aim.hit, (d.ultRadius / 55.0f) * (0.4f + 0.6f * grow) * (1.0f + 0.07f * std::sin(g_time * 40.0f)));
		if (!Audio::Looping(Audio::kLoopSpell)) {
			if (const int hum = Snd(gun, &gen::GunSounds::charge); hum >= 0) {
				Audio::LoopStart(Audio::kLoopSpell, static_cast<gen::Sound>(hum));
			}
		}
		if ((g_beamTick -= a_dt) <= 0) {  // the beam bites five times a second, through everything in its line
			g_beamTick = 0.2f;
			RE::NiPoint3 dir = aim.hit - from;
			const float  len = dir.Length();
			dir.Unitize();
			for (auto* f : Foes(from, len + 60.0f)) {
				if (Crosses(f, from, aim.hit, d.ultRadius * 0.5f)) {
					Strike(g_beamRow, f, d.ultDamage * Mult() * 0.2f, false, gen::kFxNone, f->GetPosition());
				}
			}
			PlayAt(Snd(gun, &gen::GunSounds::hit), aim.hit, 0.6f);
		}
	}

	// ---------------------------------------------------------------- input (from Gun::OnButton)
	void Left(int a_gun, bool a_down, bool a_up)
	{
		if (g_orbHeld || g_beamT > 0) {
			return;
		}
		if (a_down) {
			if (g_cd > 0.05f) {  // still recovering from the last cast: the click does nothing (no gesture, no sound)
				g_tapHeld = false;
				return;
			}
			g_tapHeld = true, g_tapT = 0, g_tapFullSaid = false;
			SendAttack(false, 1.0f, 0.0f);
		} else if (a_up && g_tapHeld) {
			g_tapHeld = false;
			SendAttack(false, 0.0f, 0.1f);
			Audio::LoopStop(Audio::kLoopSpell, 120);
			Tap(a_gun, g_tapT >= kTapFull);
			g_tapT = 0;
		}
	}
	void Right(int a_gun, bool a_down, bool a_up)
	{
		if (g_beamT > 0) {
			return;
		}
		if (a_down) {
			g_tapHeld = false;
			g_orbHeld = true, g_orbT = 0, g_orbFullSaid = false, g_orbStartSaid = false;
			SendAttack(false, 1.0f, 0.0f);
			SendAttack(true, 1.0f, 0.0f);
		} else if (a_up && g_orbHeld) {
			g_orbHeld = false;
			SendAttack(false, 0.0f, std::max(0.05f, g_orbT));
			SendAttack(true, 0.0f, std::max(0.05f, g_orbT));
			Audio::LoopStop(Audio::kLoopSpell, 150);
			const float full = gen::kGuns[a_gun].chargeSeconds * Perks::ChargeMult();
			if (g_orbT >= full) {
				Ult(a_gun);
			} else if (g_orbT < 0.3f) {
				Tap(a_gun, false);  // a quick dual tap is still a shot
			} else {
				Play2D(Snd(a_gun, &gen::GunSounds::fizzle));  // let go too early: the power gutters out
			}
			g_orbT = 0;
		}
	}

	// ---------------------------------------------------------------- the hands, posed (after Skyrim animated them)
	struct Arm
	{
		RE::NiTransform orig, written;
		RE::NiAVObject* bone = nullptr;
	};
	Arm g_arms[2][2];  // [first person?][right?]

	void PoseHands(RE::PlayerCharacter* p)
	{
		auto* cam = RE::PlayerCamera::GetSingleton();
		if (!cam || !cam->cameraRoot) {
			return;
		}
		const bool first = cam->IsInFirstPerson();
		auto*      root = p->Get3D(first);
		if (!root) {
			return;
		}
		const float w = EaseInOut(g_handW);
		const auto& cw = cam->cameraRoot->world;
		RE::NiPoint3 f{ cw.rotate.entry[0][1], cw.rotate.entry[1][1], cw.rotate.entry[2][1] };
		RE::NiPoint3 u{ cw.rotate.entry[0][2], cw.rotate.entry[1][2], cw.rotate.entry[2][2] };
		RE::NiPoint3 r{ cw.rotate.entry[0][0], cw.rotate.entry[1][0], cw.rotate.entry[2][0] };
		f.Unitize(), u.Unitize(), r.Unitize();
		// pushed forward for the beam (and the moment of release), drawn in close while the orb builds
		const float push = g_beamT > 0 ? 1.0f : (g_pushT >= 0 ? std::max(0.0f, 1.0f - g_pushT / 0.45f) : 0.0f);
		const float dist = (first ? 40.0f : 52.0f) + 16.0f * push;
		const RE::NiPoint3 centre = (first ? cw.translate : p->GetPosition() + RE::NiPoint3{ 0, 0, 105 }) + f * dist - u * (first ? 15.0f : 2.0f);
		const float shake = (g_orbHeld ? 0.6f : 0.0f) + push * 1.2f;
		for (int side = 0; side < 2; ++side) {
			const bool right = side == 1;
			Arm&       st = g_arms[first ? 1 : 0][side];
			auto*      ua = root->GetObjectByName(right ? "NPC R UpperArm [RUar]" : "NPC L UpperArm [LUar]");
			auto*      hand = root->GetObjectByName(right ? "NPC R Hand [RHnd]" : "NPC L Hand [LHnd]");
			if (!ua || !hand || !ua->parent) {
				continue;
			}
			if (ua == st.bone && std::memcmp(&ua->local, &st.written, sizeof(RE::NiTransform)) == 0) {
				ua->local = st.orig;  // not re-animated this frame: start from the clean pose
			}
			st.bone = ua;
			if (w < 0.002f) {
				st.bone = nullptr;
				continue;
			}
			const RE::NiPoint3 target = centre + r * ((right ? 1.0f : -1.0f) * (first ? 7.5f : 10.0f)) +
			                            RE::NiPoint3{ std::sin(g_time * 13.0f + side), std::sin(g_time * 17.0f + 2.0f * side), std::sin(g_time * 11.0f + 1.0f) } * (shake * 0.25f);  // a smooth tremble, never a jitter
			RE::NiPoint3 a = hand->world.translate - ua->world.translate, b = target - ua->world.translate;
			if (a.Length() < 1e-3f || b.Length() < 1e-3f) {
				continue;
			}
			a.Unitize(), b.Unitize();
			RE::NiPoint3 axis = a.Cross(b);
			const float  ang = std::acos(std::clamp(a.Dot(b), -1.0f, 1.0f));
			if (axis.Length() < 1e-5f || ang < 1e-4f) {
				continue;
			}
			const RE::NiMatrix3 D = AxisAngle(axis, ang * w);
			const RE::NiMatrix3 P = ua->parent->world.rotate;
			st.orig = ua->local;
			ua->local.rotate = P.Transpose() * D * P * ua->local.rotate;
			st.written = ua->local;
			RE::NiUpdateData ud;
			ud.time = 0.0f;
			ua->UpdateDownwardPass(ud, 0);
		}
	}

	// the orb between both hands and the glow in the right hand, placed on the skeleton you are seeing
	void PlaceHandFx(RE::PlayerCharacter* p)
	{
		auto* cam = RE::PlayerCamera::GetSingleton();
		const bool first = cam && cam->IsInFirstPerson();
		auto*      root3d = p->Get3D(first);
		auto*      root = root3d ? root3d->AsNode() : nullptr;
		const int  gun = CurrentGun(), row = Row(gun);
		auto*      rm = root ? root->GetObjectByName("NPC R MagicNode [RMag]") : nullptr;
		auto*      lm = root ? root->GetObjectByName("NPC L MagicNode [LMag]") : nullptr;
		g_orbAtValid = false;
		// the orb
		const bool orbOn = row >= 0 && root && rm && lm && (g_orbHeld || g_beamT > 0) && GunOut();
		if (orbOn) {
			g_orbBall.Need(row);
		}
		if (orbOn && g_orbBall.glow.obj) {
			g_orbBall.Attach(root);
			const float full = gen::kGuns[gun].chargeSeconds * Perks::ChargeMult();
			const float c = g_beamT > 0 ? 1.0f : std::clamp(g_orbT / std::max(0.1f, full), 0.0f, 1.0f);
			float       sz = (first ? 0.06f : 0.2f) + (first ? 0.16f : 0.5f) * EaseOut(c);  // the orb mesh's glow is 30 across: a fist-sized ball at full
			if (g_beamT > 0) {
				sz *= 0.55f;  // the beam carries the power away: the ball between the hands shrinks so the beam reads
			}
			// smooth: the size eases toward where the charge says it should be (no jumps between frames)
			static float shown = 0.0f;
			shown += (sz - shown) * std::min(1.0f, 12.0f * std::clamp(g_time - g_orbLastT, 0.0f, 0.1f));
			g_orbLastT = g_time;
			float pulse = 1.0f;
			if (c >= 1.0f && g_beamT <= 0) {  // full: a slow, even throb and a brighter glow
				pulse = 1.0f + 0.08f * std::sin(g_time * 7.0f);
			}
			static float spin = 0;
			spin += 0.016f * (2.0f + 10.0f * c);
			const RE::NiPoint3 at = (rm->world.translate + lm->world.translate) * 0.5f;
			g_orbBall.Show(true);
			g_orbBall.Place(at, AxisAngle({ 0, 0, 1 }, spin) * AxisAngle({ 1, 0, 0 }, spin * 0.43f), shown * pulse, c >= 1.0f ? 1.25f : 1.0f);
			g_orbAt = at, g_orbAtValid = true;
		} else if (g_orbBall.glow.obj) {
			g_orbBall.Show(false);
		}
		// a ball of the spell's power turning in each hand, always while the spell is out; it spins faster as a cast
		// gathers (the right hand for a left-click charge, both for the two-handed charge) and pulses when full
		static float lastT = g_time;
		const float  dtp = std::clamp(g_time - lastT, 0.0f, 0.1f);
		lastT = g_time;
		const bool ballsOn = row >= 0 && rm && lm && GunOut() && g_beamT <= 0;
		for (int side = 0; side < 2; ++side) {
			auto& fx = g_ball[side];
			auto* node = side == 1 ? rm : lm;
			if (ballsOn) {
				fx.Need(row);
			}
			if (!ballsOn || !fx.glow.obj || !node || !node->AsNode()) {
				fx.Show(false);
				for (Fx* x : { &g_aura[side], &g_flash[side] }) {
					if (x->obj) {
						x->Show(false);
					}
				}
				continue;
			}
			fx.Attach(node->AsNode());
			const float full = gen::kGuns[gun].chargeSeconds * Perks::ChargeMult();
			const float tapC = side == 1 && g_tapHeld ? std::clamp((g_tapT - 0.12f) / (kTapFull - 0.12f), 0.0f, 1.0f) : 0.0f;
			const float orbC = g_orbHeld ? std::clamp(g_orbT / std::max(0.1f, full), 0.0f, 1.0f) : 0.0f;
			const float c = std::max(tapC, orbC);
			g_ballSpin[side] += dtp * (2.2f + 16.0f * c * c) * (side ? 1.0f : -1.0f);
			float sz = (first ? 0.13f : 0.2f) * (1.0f + 0.6f * tapC) * (1.0f - 0.55f * orbC);  // (both pour into the orb between the hands)
			const bool ready = (tapC >= 1.0f) || (orbC >= 1.0f);
			if (ready) {
				sz *= 1.0f + 0.08f * std::sin(g_time * 8.0f);  // full: an even throb
			}
			g_ballShown[side] += (sz - g_ballShown[side]) * std::min(1.0f, 12.0f * dtp);
			fx.Show(true);
			fx.Place(node->world.translate, AxisAngle({ 0.25f, 0.3f, 1.0f }, g_ballSpin[side]) * AxisAngle({ 1, 0, 0 }, 0.6f + g_ballSpin[side] * 0.3f), g_ballShown[side], ready ? 1.25f : 1.0f);
			// the aura: the hand burns with the spell while a cast gathers, growing and flaring with the charge
			auto& au = g_aura[side];
			if (g_auraRow[side] != row) {
				au.Detach();
				au = Fx{ Load(Def(row).id, "aura") };
				g_auraRow[side] = row;
			}
			if (au.obj) {
				if (true) {  // always alive round the hands (a living aura), swelling and spinning up with any charge
					au.Attach(node->AsNode());
					g_auraSpin[side] += dtp * (1.6f + 11.0f * c) * (side ? -1.0f : 1.0f);
					const float flick = 1.0f + 0.14f * std::sin(g_time * 23.0f + side * 2.0f) + 0.09f * std::sin(g_time * 37.0f + side) + 0.05f * std::sin(g_time * 61.0f);
					const float as = (first ? 0.17f : 0.32f) * (0.45f + 0.8f * c) * flick * (ready ? 1.2f : 1.0f);
					au.Show(true);
					au.Place(node->world.translate, AxisAngle({ 0, 0, 1 }, g_auraSpin[side]) * AxisAngle({ 1, 0, 0 }, g_auraSpin[side] * 0.37f), as);
				} else {
					au.Show(false);
				}
			}
			// the flare as a cast leaves the hand
			auto& fl = g_flash[side];
			if (g_flashRow[side] != row) {
				fl.Detach();
				fl = Fx{ Load(Def(row).id, "burst") };
				g_flashRow[side] = row;
			}
			if (fl.obj) {
				if (g_flashT[side] >= 0) {
					g_flashT[side] += dtp;
					const float u = g_flashT[side] / 0.22f;
					if (u >= 1.0f) {
						g_flashT[side] = -1;
						fl.Show(false);
					} else {
						fl.Attach(node->AsNode());
						fl.Show(true);
						fl.Place(node->world.translate, AxisAngle({ 0, 0, 1 }, g_time * 4.0f), (first ? 0.24f : 0.4f) * std::sin(u * RE::NI_PI) * (1.0f + 0.8f * u));
					}
				} else {
					fl.Show(false);
				}
			}
		}
	}

	void OnPose(RE::PlayerCharacter* p)
	{
		PoseHands(p);
		PlaceHandFx(p);
	}

	// ---------------------------------------------------------------- every frame (Gun::Tick)
	void Tick(float a_dt)
	{
		g_time += a_dt;
		g_cd = std::max(0.0f, g_cd - a_dt);
		g_ultCd = std::max(0.0f, g_ultCd - a_dt);
		const int  gun = CurrentGun();
		const bool live = Is(gun) && GunOut();
		if (!live) {
			if (g_tapHeld || g_orbHeld) {
				Audio::LoopStop(Audio::kLoopSpell, 150);
			}
			g_tapHeld = g_orbHeld = false;
		}
		// the engine's own charge for casting the hand spell is handed back (ours is spent in Spend); never while a
		// Skyrim spell is being cast in the other hand: those cost their magicka as they always did
		{
			auto*       pl = RE::PlayerCharacter::GetSingleton();
			auto*       av = pl->AsActorValueOwner();
			const float mp = av->GetActorValue(RE::ActorValue::kMagicka);
			bool        skyrimCasting = false;
			for (auto src : { RE::MagicSystem::CastingSource::kLeftHand, RE::MagicSystem::CastingSource::kRightHand, RE::MagicSystem::CastingSource::kOther }) {
				auto* mc = pl->GetMagicCaster(src);
				if (mc && mc->currentSpell && mc->state.get() != RE::MagicCaster::State::kNone && Forms::GunIndexOf(mc->currentSpell) < 0) {
					skyrimCasting = true;
				}
			}
			if (live && !skyrimCasting && g_mpLast >= 0 && mp < g_mpLast - 0.01f) {
				av->RestoreActorValue(RE::ActorValue::kMagicka, g_mpLast - mp);
			}
			g_mpLast = av->GetActorValue(RE::ActorValue::kMagicka);
		}
		// left hold: the hand glows and hums; a peak when it is empowered
		if (live && g_tapHeld) {
			const float before = g_tapT;
			g_tapT += a_dt;
			if (before < 0.12f && g_tapT >= 0.12f) {
				Play2D(Snd(gun, &gen::GunSounds::start), 0.6f);
				if (const int hum = Snd(gun, &gen::GunSounds::charge); hum >= 0) {
					Audio::LoopStart(Audio::kLoopSpell, static_cast<gen::Sound>(hum));
				}
			}
			if (g_tapT >= kTapFull && !g_tapFullSaid) {
				Play2D(Snd(gun, &gen::GunSounds::full), 0.7f);
				g_tapFullSaid = true;
			}
		}
		// right hold: the orb builds between both hands
		if (live && g_orbHeld) {
			g_orbT += a_dt;
			if (g_orbT >= 0.15f && !g_orbStartSaid) {
				Play2D(Snd(gun, &gen::GunSounds::start));
				if (const int hum = Snd(gun, &gen::GunSounds::charge); hum >= 0) {
					Audio::LoopStart(Audio::kLoopSpell, static_cast<gen::Sound>(hum));
				}
				g_orbStartSaid = true;
			}
			if (g_orbT >= gen::kGuns[gun].chargeSeconds * Perks::ChargeMult() && !g_orbFullSaid) {
				Play2D(Snd(gun, &gen::GunSounds::full));
				g_orbFullSaid = true;
			}
		}
		const float want = live && (g_orbHeld || g_beamT > 0 || (g_pushT >= 0 && g_pushT < 0.45f)) ? 1.0f : 0.0f;
		g_handW += (want - g_handW) * std::min(1.0f, a_dt * (want > g_handW ? 9.0f : 6.0f));
		if (g_pushT >= 0 && (g_pushT += a_dt) > 0.6f) {
			g_pushT = -1;
		}
		if (g_bodies.size() > 256) {
			g_bodies.clear();
		}
		TickBeam(a_dt);
		TickPending(a_dt);
		TickShots(a_dt);
		TickFx(a_dt);
		// the HUD: this spell's colour (the crosshair ring is gone; the spell itself shows the charge)
	}

	void Reset()
	{
		for (auto& s : g_shots2) {
			s.fx.Detach();
			s.ball.Detach();  // (the audit: a shot in flight at a load left its glowing ball in the new world)
			if (s.loop) {
				Audio::LoopStop(s.loop, 0);
			}
		}
		g_shots2.clear();
		for (auto& b : g_bursts) {
			b.fx.Detach();
		}
		g_bursts.clear();
		for (auto& s : g_streaks) {
			s.beam.Clear();
		}
		g_streaks.clear();
		for (auto& w : g_wells) w.ball.Detach();
		g_wells.clear();
		for (auto& h : g_held) {
			if (auto a = h.a.get()) {
				a->EnableAI(true);
			}
		}
		g_held.clear();
		g_pending.clear();
		for (auto& w : g_wells) {
			w.ball.Detach();
		}
		g_wells.clear();
		g_beam.Clear();
		g_beamT = -1;
		g_orb.Detach(), g_hand.Detach();
		for (int i = 0; i < 2; ++i) {
			g_ball[i].Detach(), g_aura[i].Detach(), g_flash[i].Detach();
			g_ball[i] = {}, g_aura[i] = {}, g_flash[i] = {};
			g_orbBall.Detach();
			g_orbBall = {};
			g_ballRow[i] = g_auraRow[i] = g_flashRow[i] = -1;
		}
		g_orb = {}, g_hand = {};
		g_orbRow = g_handRow = -1;
		g_tapHeld = g_orbHeld = false;
		g_handW = 0;
		g_bodies.clear();
	}
}
