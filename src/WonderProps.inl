// The wonder weapons in your hands (Noah's models, tools/wonderprops.py): while one is drawn its model hangs on the
// bones that hold it, in first person and in third - Gravecaller's staff in the right hand, the Frostbinder gauntlets
// on both hands, the Wolf Queen's Crown on your head (and, in first person, a small one turning over your left hand),
// the Horn of the Dragon Priests in the left hand. Each breathes with its colour while held, sheds its element's motes,
// and moves with every shot: a tap kicks it, a charged attack swings it through (Gun's Spellcraft::Launch sets the
// kick). Included by Gun.cpp inside its anonymous namespace, after Spellcraft and Bows.
namespace Props
{
	struct Piece
	{
		const char*  node;          // the bone it hangs on
		const char*  model;         // under Meshes
		bool         first, third;  // in which body
		RE::NiPoint3 at;            // in the bone's space
		float        scale;
		int          kind;          // 0 staff, 1 gauntlet, 2 crown worn, 3 crown floating, 4 horn
	};
	struct Wonder
	{
		std::string_view id;
		std::vector<Piece> pieces;
	};
	const std::vector<Wonder>& Wonders()
	{
		// (Noah: the wonders are the four Falmer staffs now - real staffs in the hand, nothing hung on the bones)
		static const std::vector<Wonder> w{
		};
		return w;
	}

	// (Noah: "the Madness Sword ... an animated enchantment effect ... animated from the start, and ... stronger as it's
	// upgraded") its dark-red fire (tools/madflame.py) on the WEAPON node of both bodies while it is drawn: Base, I, II,
	// Legendary each their own, the Ascends burning a little bigger still
	struct MadFire { Spellcraft::Fx fx; RE::NiNode* on = nullptr; };
	MadFire g_mad[2];
	int     g_madLook = -1;
	void TickMad()
	{
		auto*     p = RE::PlayerCharacter::GetSingleton();
		const int t = p ? Kit::MadTier() : -1;
		const int look = t < 0 ? -1 : std::min(t, 3);
		for (int b = 0; b < 2; ++b) {
			auto& m = g_mad[b];
			auto* root = (p && t >= 0) ? p->Get3D(b == 0) : nullptr;
			auto* wn = root ? root->GetObjectByName("WEAPON") : nullptr;
			auto* node = wn ? wn->AsNode() : nullptr;
			if (!node || look < 0) { m.fx.Detach(); m.on = nullptr; continue; }
			if (look != g_madLook || !m.fx.obj) m.fx.Detach(), m.fx = Spellcraft::Fx{ Spellcraft::LoadPath("CurseOfAlduin\\fx\\madflame_" + std::to_string(look) + ".nif") };
			if (!m.fx.obj) continue;
			if (m.fx.obj->parent != node) m.fx.Attach(node), m.on = node;
			m.fx.obj->local.scale = 1.0f + 0.04f * static_cast<float>(std::max(0, t - 3));
		}
		g_madLook = look;
	}

	// (Noah: the Mutagen Gauntlet - "Alchemist's glove ... very very high quality ... with passive animations around the
	// gauntlet") the glove (tools/gauntlet.py) worn on the hand that holds the wonder - drawn or not - in both bodies: hung on
	// that hand's own bone (the model is in its frame; the left hand's is the mirror)
	Gun::GauntletState g_gs;   // (Classes::TickGauntlet sets it: Gun::SetGauntlet)
	// the parts of a glove the beam's state shows and hides (by name: tools/gauntlet.py)
	struct GloveParts
	{
		RE::NiAVObject* root = nullptr;
		RE::NiAVObject* top[6]{};       // Liq<e>_T: the liquid's surface (its column skinned to it)
		float           zB[6]{}, zT[6]{};   // its foot's and full surface's heights
		RE::NiAVObject* body[6]{};
		RE::NiAVObject* glow[6]{};
		float           shown[6]{ 1, 1, 1, 1, 1, 1 };   // the level shown (easing after the real one, as liquid settles)
		RE::NiAVObject* tube[6]{};
		RE::NiAVObject* bub[6]{};
		RE::NiAVObject* vglow[6]{};
		RE::NiAVObject* tint[6]{};
		RE::NiAVObject* flare[6]{};
		RE::NiAVObject* core = nullptr;
	};
	void FindParts(GloveParts& a_p, RE::NiAVObject* a_root)
	{
		a_p = GloveParts{};
		a_p.root = a_root;
		std::function<void(RE::NiAVObject*)> walk = [&](RE::NiAVObject* o) {
			if (!o) return;
			const std::string_view n = o->name.c_str();
			auto el = [&](std::string_view pre) { return n.size() > pre.size() && n.substr(0, pre.size()) == pre ? n[pre.size()] - '0' : -1; };
			if (int e = el("Liq"); e >= 0 && e < 6 && n.size() == 6 && n[4] == '_') {
				if (n[5] == 'T') a_p.top[e] = o, a_p.zT[e] = o->local.translate.z;
				else if (n[5] == 'B') a_p.zB[e] = o->local.translate.z;
			}
			else if (int eb = el("LiqBody"); eb >= 0 && eb < 6) a_p.body[eb] = o;
			else if (int eg = el("LiqGlow"); eg >= 0 && eg < 6) a_p.glow[eg] = o;
			else if (int e1 = el("TubeLight"); e1 >= 0 && e1 < 6) a_p.tube[e1] = o;
			else if (int e2 = el("VialBubbles"); e2 >= 0 && e2 < 6) a_p.bub[e2] = o;
			else if (int e3 = el("VialGlow"); e3 >= 0 && e3 < 6 && n.find("_bb") == std::string_view::npos) a_p.vglow[e3] = o;
			else if (int e4 = el("OrbTint"); e4 >= 0 && e4 < 6) a_p.tint[e4] = o;
			else if (int e5 = el("OrbFlare"); e5 >= 0 && e5 < 6 && n.find("_bb") == std::string_view::npos) a_p.flare[e5] = o;
			else if (n == "OrbCore_bb") a_p.core = o;
			if (auto* nd = o->AsNode()) for (auto& c : nd->GetChildren()) walk(c.get());
		};
		walk(a_root);
	}
	void ShowParts(GloveParts& a_p, float a_delta)
	{
		const auto& s = g_gs;
		for (int e = 0; e < 6; ++e) {
			// the surface eased after the true level (liquid settling, never stepping), a faint ripple while it moves
			float& d = a_p.shown[e];
			const float want = std::clamp(s.fill[e], 0.0f, 1.0f);
			d += (want - d) * std::min(1.0f, a_delta * 5.0f);
			const float f = d;
			if (a_p.top[e]) {
				const float moving = std::min(1.0f, std::abs(want - d) * 40.0f) + (s.active == e && s.phase == 2 ? 1.0f : 0.0f);
				const float ripple = 0.03f * std::min(1.0f, moving) * std::sin(static_cast<float>(Spellcraft::g_time) * 9.0f + e);
				a_p.top[e]->local.translate.z = a_p.zB[e] + (a_p.zT[e] - a_p.zB[e]) * std::max(0.03f, f) + ripple * (f > 0.05f ? 1.0f : 0.0f);
				RE::NiUpdateData ud;
				ud.time = 0.0f;
				a_p.top[e]->Update(ud);
			}
			if (a_p.body[e]) a_p.body[e]->SetAppCulled(f < 0.015f);
			if (a_p.glow[e]) a_p.glow[e]->SetAppCulled(f < 0.015f);
			const bool pour = s.phase == 2 && s.active == e;
			const bool next = s.phase == 1 && s.active == e;
			if (a_p.tube[e]) a_p.tube[e]->SetAppCulled(!pour);
			if (a_p.bub[e]) a_p.bub[e]->SetAppCulled(f < 0.05f);
			if (a_p.vglow[e]) a_p.vglow[e]->SetAppCulled(f < 0.05f);
			if (a_p.tint[e]) a_p.tint[e]->SetAppCulled(!(pour || next));
			if (a_p.flare[e]) a_p.flare[e]->SetAppCulled(!pour);
		}
	}
	GloveParts g_parts[2][2];
	struct Glove { Spellcraft::Fx fx; RE::NiNode* on = nullptr; };
	Glove g_glove[2][2];   // [hand: right, left][body: third, first]
	float g_glDelta = 0.016f;
	void TickGauntlet(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		for (int h = 0; h < 2; ++h) {
			const bool L = h == 1;
			g_glDelta = a_delta;
			const bool want = p && Classes::FlaskElementInHand(L) == 6;
			for (int b = 0; b < 2; ++b) {
				auto& g = g_glove[h][b];
				auto* root = want ? p->Get3D(b == 1) : nullptr;
				auto* bone = root ? root->GetObjectByName(L ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]") : nullptr;
				auto* node = bone ? bone->AsNode() : nullptr;
				if (!node) { if (g.fx.obj) g.fx.Detach(); g.on = nullptr; continue; }
				if (!g.fx.obj) g.fx = Spellcraft::Fx{ Spellcraft::LoadPath(L ? "CurseOfAlduin\\alchemist\\gauntlet_l.nif" : "CurseOfAlduin\\alchemist\\gauntlet_r.nif") };
				if (!g.fx.obj) continue;
				if (g.fx.obj->parent != node) {
					g.fx.Attach(node), g.on = node;
					g.fx.obj->local = RE::NiTransform{};
					logger::info("gauntlet: the glove on the {} hand ({} person)", L ? "left" : "right", b == 1 ? "first" : "third");
				}
				auto& gp = g_parts[h][b];
				if (gp.root != g.fx.obj.get()) FindParts(gp, g.fx.obj.get());
				ShowParts(gp, g_glDelta);
			}
		}
	}

	// the palm orb's place in the world: the glove on the hand that holds it, in the body you are seeing
	bool PalmOrb(RE::NiPoint3& a_at)
	{
		auto* cam = RE::PlayerCamera::GetSingleton();
		const int b = cam && cam->IsInFirstPerson() ? 1 : 0;
		const int h = g_gs.left ? 1 : 0;
		auto* core = g_parts[h][b].core;
		if (!core || !g_glove[h][b].fx.obj || !g_glove[h][b].fx.obj->parent) return false;
		a_at = core->world.translate;
		return true;
	}
	// the beam: segments of the element's beam (tools/gauntlet.py gauntlet_beam_<el>.nif, 64 units along +Y) laid end to
	// end from the palm to where it strikes, each face-on to the camera; a flare at each end
	// (seen in game: models hung on the scene's root were never drawn - the renderer drops them) each piece of the beam is a
	// placed object wearing its model (WorldFx), posed every frame
	struct GBeam { std::vector<WorldFx> segs; int el = -1; WorldFx f0, f1; bool f0on = false; };
	GBeam g_gbeam;
	constexpr float kGSeg = 256.0f;   // (tools/gauntlet.py: a beam segment's length along +Y)
	void GBeamOff()
	{
		for (auto& f : g_gbeam.segs) f.Detach();
		g_gbeam.segs.clear();
		g_gbeam.f0.Detach(), g_gbeam.f1.Detach();
		g_gbeam.f0 = WorldFx{}, g_gbeam.f1 = WorldFx{};
		g_gbeam.el = -1;
	}
	void GBeamLay(int a_el, const RE::NiPoint3& a, const RE::NiPoint3& b, float w)
	{
		static constexpr const char* kName[6] = { "fire", "frost", "shock", "acid", "holy", "void" };
		a_el = std::clamp(a_el, 0, 5);
		const std::string seg = std::string("CurseOfAlduin\\alchemist\\gauntlet_beam_") + kName[a_el] + ".nif";
		if (a_el != g_gbeam.el) {
			GBeamOff();
			g_gbeam.el = a_el;
			g_gbeam.f0 = WorldFx{ std::string("CurseOfAlduin\\alchemist\\gauntlet_flare_") + kName[a_el] + ".nif" };
			g_gbeam.f1 = WorldFx{ std::string("CurseOfAlduin\\alchemist\\gauntlet_flare_") + kName[a_el] + ".nif" };
		}
		const RE::NiPoint3 d = b - a;
		const float len = d.Length();
		const float step = kGSeg * w;
		const int n = std::clamp(static_cast<int>(std::ceil(len / std::max(1.0f, step))), 1, 40);
		while (static_cast<int>(g_gbeam.segs.size()) > n) { g_gbeam.segs.back().Detach(); g_gbeam.segs.pop_back(); }
		while (static_cast<int>(g_gbeam.segs.size()) < n) g_gbeam.segs.emplace_back(seg);
		RE::NiPoint3 prev = a;
		for (int i = 0; i < n; ++i) {
			const RE::NiPoint3 next = a + d * (static_cast<float>(i + 1) / n);
			g_gbeam.segs[i].Place(prev, Spellcraft::FacingCam(next - prev, (prev + next) * 0.5f), std::max(0.01f, (next - prev).Length() / kGSeg));
			prev = next;
		}
		const float pulse = 1.0f + 0.12f * std::sin(static_cast<float>(Spellcraft::g_time) * 23.0f);
		g_gbeam.f0.Place(a, Spellcraft::FaceView(), 0.32f * w * pulse);
		g_gbeam.f1.Place(b, Spellcraft::FaceView(), 1.4f * w * pulse);
		if (static float said = 0; (said -= 0.016f) <= 0) {
			said = 1.0f;
			logger::info("gauntlet beam: {} segments, {:.0f} long, drawn {}", n, len, g_gbeam.segs.empty() ? false : g_gbeam.segs[0].drawn);
		}
	}

	void ChargedLook(int a_w);
	struct Hung
	{
		Spellcraft::Fx fx;
		int            wonder = -1, piece = -1;
		bool           first = false;
	};
	std::vector<Hung> g_hung;
	int               g_wonder = -1;
	float             g_t = 0, g_mote = 0;
	RE::NiAVObject*   g_root1 = nullptr;
	RE::NiAVObject*   g_root3 = nullptr;

	void Clear()
	{
		for (auto& h : g_hung) {
			h.fx.Detach();
		}
		g_hung.clear();
	}

	int SpellRow(std::string_view a_id)
	{
		for (int r = 0; r < static_cast<int>(gen::kSpellcraft.size()); ++r) {
			if (gen::kSpellcraft[r].id == a_id) {
				return r;
			}
		}
		return -1;
	}

	RE::NiMatrix3 Rot(float a_x, float a_y, float a_z)
	{
		RE::NiMatrix3 m;
		m.SetEulerAnglesXYZ(a_x, a_y, a_z);
		return m;
	}

	void Hang(RE::PlayerCharacter* p, int a_w)
	{
		Clear();
		const auto& W = Wonders()[a_w];
		for (int k = 0; k < static_cast<int>(W.pieces.size()); ++k) {
			const auto& pc = W.pieces[k];
			for (bool first : { true, false }) {
				if ((first && !pc.first) || (!first && !pc.third)) {
					continue;
				}
				auto* root = p->Get3D(first);
				auto* bone = root ? root->GetObjectByName(pc.node) : nullptr;
				auto* node = bone ? bone->AsNode() : nullptr;
				if (!node) {
					static int said = 0;
					if (said++ < 8) logger::warn("wonder props: no {} in the {} body", pc.node, first ? "first-person" : "third-person");
					continue;
				}
				Hung h;
				h.fx = Spellcraft::Fx{ Spellcraft::LoadPath(pc.model) };
				if (!h.fx.obj) {
					continue;
				}
				h.fx.Attach(node);
				h.wonder = a_w, h.piece = k, h.first = first;
				g_hung.push_back(std::move(h));
			}
		}
		logger::info("wonder props: {} in hand ({} pieces hung)", W.id, g_hung.size());
	}

	void Tick(float a_delta, int a_gun)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		int   w = -1;
		if (p && a_gun >= 0 && a_gun < static_cast<int>(gen::kGuns.size())) {
			for (int i = 0; i < static_cast<int>(Wonders().size()); ++i) {
				if (Wonders()[i].id == gen::kGuns[a_gun].id) {
					w = i;
				}
			}
		}
		// a new body (a load, a race change, first person rebuilt) or another wonder: hang them again
		auto* r1 = p ? p->Get3D(true) : nullptr;
		auto* r3 = p ? p->Get3D(false) : nullptr;
		if (w != g_wonder || r1 != g_root1 || r3 != g_root3) {
			Clear();
			g_wonder = w, g_root1 = r1, g_root3 = r3;
			if (w >= 0 && (r1 || r3)) {
				Hang(p, w);
			}
		}
		if (w < 0 || g_hung.empty()) {
			return;
		}
		g_t += a_delta;
		// the attack motion: in fast, out slow
		float kick = 0.0f;
		if (g_propKickT == 0.0f && g_propKickBig) {
			ChargedLook(w);
		}
		if (g_propKickT >= 0) {
			const float dur = g_propKickBig ? 0.7f : 0.3f;
			const float u = g_propKickT / dur;
			kick = u < 0.2f ? u / 0.2f : 1.0f - (u - 0.2f) / 0.8f;
			kick = kick * kick * (3 - 2 * kick);
			if ((g_propKickT += a_delta) >= dur) {
				g_propKickT = -1.0f;
			}
		}
		const bool  big = g_propKickBig;
		const float breathe = 1.0f + 0.02f * std::sin(g_t * 2.4f);
		const int   row = SpellRow(Wonders()[w].id);
		for (auto& h : g_hung) {
			const auto& pc = Wonders()[w].pieces[h.piece];
			RE::NiPoint3  at = pc.at;
			RE::NiMatrix3 rot;  // (identity)
			float         sc = pc.scale * breathe;
			switch (pc.kind) {
			case 0:  // the staff: a tap jabs it forward along its length; the call drives it up, then down into the ground
				at.y += (big ? 14.0f : 7.0f) * kick;
				rot = Rot(big ? -0.5f * kick : -0.15f * kick, 0, 0);
				break;
			case 1:  // the gauntlets: a punch out along the fingers; the charged grip clenches them bigger, rimed
				at.z += (big ? 5.0f : 3.0f) * kick;
				sc *= 1.0f + (big ? 0.18f : 0.06f) * kick;
				break;
			case 2:  // the crown worn: it flares
				sc *= 1.0f + (big ? 0.12f : 0.04f) * kick;
				break;
			case 3:  // the small crown over your hand: turning, bobbing, leaping up when it calls
				at.z += 1.2f * std::sin(g_t * 1.7f) + (big ? 9.0f : 3.0f) * kick;
				rot = Rot(0, g_t * 0.9f + (big ? 6.28f * kick : 0.0f), 0);
				sc *= 1.0f + (big ? 0.5f : 0.15f) * kick;
				break;
			case 4:  // the horn: raised and tipped back as it sounds
				at.z += (big ? 6.0f : 3.0f) * kick;
				rot = Rot((big ? 0.55f : 0.25f) * kick, 0, 0);
				break;
			}
			h.fx.obj->local.translate = at;
			h.fx.obj->local.rotate = rot;
			h.fx.obj->local.scale = std::max(0.0001f, sc);
			RE::NiUpdateData ud;
			h.fx.obj->Update(ud);
			h.fx.Show(true);
		}
		// its element's motes drift off it (the staff's head, the knuckles, the crown, the horn's bell), thicker when it fires
		if (row >= 0 && (g_mote -= a_delta) <= 0) {
			g_mote = kick > 0 ? 0.08f : 0.45f;
			const bool fp = Spellcraft::FirstPerson();
			for (auto& h : g_hung) {
				if (h.first != fp) {
					continue;
				}
				const auto& pc = Wonders()[w].pieces[h.piece];
				const RE::NiPoint3 tipLocal = pc.kind == 0 ? RE::NiPoint3{ 0, 70.0f, 0 } : pc.kind == 1 ? RE::NiPoint3{ 0, 1.0f, 8.0f } : pc.kind == 4 ? RE::NiPoint3{ 0, 24.0f, -4.0f } : RE::NiPoint3{ 0, 0, 10.0f };
				const auto& o = h.fx.obj->world;
				Spellcraft::BurstAt(row, o.translate + o.rotate * tipLocal * o.scale, kick > 0 ? 14.0f : 6.0f, 0.25f);
			}
		}
	}

	// ---- the wonders' charged attacks, in the look of Elemental Mastery Magic (Noah's download, by its author) ---------
	// its models placed in the world for a few seconds where the attack lands (activators, so the engine plays them)
	struct WorldFx
	{
		RE::ObjectRefHandle ref;
		float               left;
	};
	std::vector<WorldFx> g_world;
	RE::TESObjectREFR* PlaceFx(int a_kind, const RE::NiPoint3& a_at, float a_scale)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* base = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESBoundObject>(gen::kEmmFx[a_kind], gen::kPlugin);
		auto  ref = p && base ? p->PlaceObjectAtMe(base, false) : nullptr;
		if (!ref) {
			return nullptr;
		}
		ref->SetPosition(a_at);
		if (a_scale != 1.0f) {
			ref->SetScale(a_scale);
		}
		return ref.get();
	}
	void Spawn(int a_kind, const RE::NiPoint3& a_at, float a_seconds, float a_scale = 1.0f)
	{
		if (auto* r = PlaceFx(a_kind, a_at, a_scale)) {
			g_world.push_back({ r->CreateRefHandle(), a_seconds });
		}
	}
	void Gone(RE::ObjectRefHandle& a_h)
	{
		if (auto r = a_h.get()) {
			r->Disable();
			r->SetDelete(true);
		}
		a_h = {};
	}
	void TickWorld(float a_delta)
	{
		for (auto& w : g_world) {
			if ((w.left -= a_delta) <= 0) {
				Gone(w.ref);
			}
		}
		std::erase_if(g_world, [](const WorldFx& w) { return w.left <= 0; });
	}
	void ChargedLook(int a_w)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) {
			return;
		}
		const auto aim = AimAt(3000.0f);
		RE::NiPoint3 ground = aim.hit;
		if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(ground, land) && land > ground.z - 400.0f) {
			ground.z = land;
		}
		switch (a_w) {
		case 0: Spawn(0, ground, 3.0f, 1.2f); break;                                             // Gravecaller: the barrow opens
		case 1: Spawn(1, ground, 3.0f, 1.3f); break;                                             // Frostbinder: crystals of ice burst
		case 2: Spawn(2, p->GetPosition() + RE::NiPoint3{ 0, 0, 110.0f }, 2.5f, 0.9f); break;  // the Crown: the Queen's storm-orb
		case 3: Spawn(3, ground, 5.0f, 1.0f); break;                                             // the Horn: the war-host's storm
		}
	}

	// ---- Alduin's Fang (Stage 5): the strongest weapon in the game -----------------------------------------------------
	// every blow burns the dead it strikes and the fire leaps to those beside them; a charged (power) swing looses a
	// tornado of fire that walks forward through the dead (Elemental Mastery Magic's tornado)
	RE::TESObjectWEAP* FangForm() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kFangWeap, gen::kPlugin); }
	bool FangInHand(RE::PlayerCharacter* p)
	{
		auto* f = FangForm();
		return p && f && (p->GetEquippedObject(false) == f || p->GetEquippedObject(true) == f);
	}
	// a tornado (Elemental Mastery Magic's, recoloured per element: fire, frost, shock, wind, sun, blood) that walks the
	// ground for a while, hurting what it passes through - slow, and (a_chase) turning after the nearest of the dead
	struct Twister
	{
		RE::ObjectRefHandle ref;
		RE::NiPoint3        at, dir;
		float               left, tick, dmg, speed;
		bool                chase;
		int                 row, kind;
		std::unordered_map<RE::FormID, float> cd;  // (each body pulled or thrown once in a while)
	};
	std::vector<Twister> g_twisters;
	float FangBase() { return Director::RoundHealth() * gen::kRounds.hpScale; }  // a zombie's whole night health, in BO3 units
	// (the class audit: a flat one zombie a hit - weaker than an ascended Madness Sword by the end) the endgame's blade stands
	// above every kit's best: a fully ascended item's curve, with the night's power
	float BaneMult() { return 0.3f * UpgradeMult(14) * (0.7f + 0.3f * Arcana::Power()); }
	void Tornado(int a_kind, const RE::NiPoint3& a_at, RE::NiPoint3 a_dir, float a_seconds, float a_damage, bool a_chase, int a_row)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		const auto     id = gen::kTornado[std::clamp(a_kind, 0, 5)];
		RE::TESForm*   raw = p ? RE::TESDataHandler::GetSingleton()->LookupForm(id, gen::kPlugin) : nullptr;
		if (!raw) raw = Forms::Ours(id);
		auto* base = raw && raw->IsBoundObject() ? static_cast<RE::TESBoundObject*>(raw) : nullptr;
		auto  ref = base ? p->PlaceObjectAtMe(base, false) : nullptr;
		if (!ref) {
			logger::warn("tornado: kind {} ({:03X}) could not be placed ({}, form type {})", a_kind, id, base ? "placing failed" : "its form is missing",
				raw ? static_cast<int>(raw->GetFormType()) : -1);
			return;
		}
		RE::NiPoint3 at = a_at;
		if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(at, land) && std::abs(land - at.z) < 400.0f) {
			at.z = land;
		}
		ref->SetPosition(at);
		a_dir.z = 0;
		if (a_dir.Length() < 0.01f) {
			a_dir = { 0, 1, 0 };
		}
		a_dir.Unitize();
		g_twisters.push_back({ ref->CreateRefHandle(), at, a_dir, a_seconds, 0.0f, a_damage, a_chase ? 230.0f : 480.0f, a_chase, a_row, a_kind, {} });
		Audio::AtPoint(gen::Sound::unleash_hearth, at);
		logger::info("tornado: kind {} for {:.0f} s{}", a_kind, a_seconds, a_chase ? ", hunting the dead" : "");
	}
	// ---- Alduin's Bane (the story's last weapon, in the Fang's slot) -------------------------------------------------
	// its magic is your path's (below); kills fill its soul meter (20) and heal you a little; a full meter calls Alduin's
	// shade - meteors fall on the dead round you for 10 s; while it is in your hands your shouts return twice as fast
	int   g_baneSouls = 0;
	float g_baneStorm = 0, g_baneMeteor = 0;
	void BaneKilled(RE::PlayerCharacter* p)
	{
		auto* av = p->AsActorValueOwner();
		av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.04f);
		if (g_baneStorm > 0) return;
		if (++g_baneSouls >= 20) {
			g_baneSouls = 0, g_baneStorm = 10.0f, g_baneMeteor = 0;
			Forms::Play(gen::Sound::champ_arrive);
			Hud::Banner("WORLD-EATER'S END - Alduin's shade answers", 3.0f);
		} else if (g_baneSouls == 10 || g_baneSouls == 15) {
			Hud::Banner(fmt::format("Alduin's Bane - {} of 20 souls", g_baneSouls), 1.5f);
		}
	}
	// (Noah: "One fixed weapon ... I want the magic abilities to depend on which class is selected while holding it") the
	// Bane's magic answers your path. Summoner: the strongest necromancy - what it kills rises as a Draugr Death Overlord
	// to fight for you (three at most, 40 s each), and every blow drinks a little life. Battle-mage: every blow looses a
	// master Destruction spell on what it struck (Immolate, Throat of the World, Finger of the Mountain, Daybreak in turn).
	// Spellsword: every blow carries a master Grasp (Fiery, Freezing, Thundering, Searing, Touch of Death, Viper's - in
	// turn). These strike on contact only; the Ranger's needs none: every swing sends a wind slash down the line that hurls
	// the dead VERY far (a power attack: a wider, longer one).
	RE::SpellItem* CofferSpell(std::string_view a_name)
	{
		for (int i = 0; i < static_cast<int>(coffer::kSpells.size()); ++i) {
			if (a_name == coffer::kSpells[i].name) return Arcana::Spell(i);
		}
		return nullptr;
	}
	RE::TESNPC* OverlordNpc()
	{
		static RE::TESNPC* npc = nullptr;
		if (npc) return npc;
		npc = [] {
			auto* sp = CofferSpell("Conjure Draugr Death Overlord");
			if (sp) {
				for (auto* e : sp->effects) {
					if (auto* m = e ? e->baseEffect : nullptr; m && m->data.associatedForm) {
						if (auto* n = m->data.associatedForm->As<RE::TESNPC>()) return n;
					}
				}
			}
			return static_cast<RE::TESNPC*>(nullptr);
		}();
		return npc;
	}
	float g_baneCast = 0;  // (one master spell a third of a second at most: the screen never drowns)
	int   g_baneTurn = 0;
	void BaneCast(RE::PlayerCharacter* p, RE::Actor* a_z, std::initializer_list<std::string_view> a_names, int a_n)
	{
		if (g_baneCast > 0) return;
		g_baneCast = 0.33f;
		std::vector<RE::SpellItem*> pool;
		for (auto nm : a_names) if (auto* sp = CofferSpell(nm)) pool.push_back(sp);
		if (pool.empty()) return;
		auto* mc = p->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
		for (int k = 0; mc && k < a_n; ++k) {
			mc->CastSpellImmediate(pool[static_cast<std::size_t>(g_baneTurn++) % pool.size()], false, a_z, 1.0f, false, 0.0f, p);
		}
	}
	void FangHit(RE::Actor* a_z, bool a_power)
	{
		if (!a_z || a_z->IsDead()) {
			return;
		}
		auto*       p = RE::PlayerCharacter::GetSingleton();
		const float base = FangBase();
		const int   cls = Classes::Get();
		const auto  at = a_z->GetPosition();
		const bool  killed = Damage(a_z, base * BaneMult() * (a_power ? 2.0f : 1.0f), false, true);
		if (killed && p) BaneKilled(p);
		if (!p) return;
		// (Noah: "Alduin's Bane - the same moves for any class") every class: the wind slash from each swing (BaneWind), the souls
		(void)cls, (void)at;
	}
	// the Ranger's wind slash: a crescent of wind down the line from every swing - hit or not - that hurls the dead far away
	// (Noah: "for Alduin's Bane and the Dragon Slayer, their light attacks should be a close-range area of damage, and the
	// charged attack longer range") a light swing: a burst of wind round the blade's arc that throws back all near you; a
	// power attack: the long crescent down the line
	void BaneClose(RE::PlayerCharacter* p)
	{
		const Aim aim = AimAt(400.0f);
		RE::NiPoint3 dir = aim.dir; dir.z = 0;
		if (dir.Length() < 0.01f) dir = { 0, 1, 0 };
		dir.Unitize();
		const RE::NiPoint3 at = p->GetPosition() + dir * 150.0f + RE::NiPoint3{ 0, 0, 80.0f };
		Blast(BlastIndex("shoutarea"), at, 0.0f, 0.45f);
		Blast(BlastIndex("shoutarea"), at + dir * 120.0f, 0.05f, 0.35f);
		Audio::AtPoint(gen::Sound::unleash_hearth, at);
		for (auto* f : Foes(p->GetPosition() + dir * 120.0f, 380.0f)) {
			if (Damage(f, FangBase() * BaneMult() * 0.9f, false, false, true)) BaneKilled(p);
			Spellcraft::Fling(f, p->GetPosition() - RE::NiPoint3{ 0, 0, 80.0f }, 10.0f);
		}
	}
	// (Noah: "I want alduins bane's light attack to send the slashes like we are talking about and heavy attacks send a medium
	// size meteor in 5m in front of the player and every 20 kills rains meteors on enemies for 10 seconds") a light swing: a
	// long blade of pale light down the line through every one of the dead in its path, throwing them back; a heavy one: a
	// meteor out of the sky 5 m before you, the dead round it struck hard and hurled away (the 20 souls' rain: TickBane)
	void BaneWind(RE::PlayerCharacter* p, bool a_power)
	{
		static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(5);
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(200)) return;   // (one swing heard from both bodies)
		last = now;
		if (!a_power) {
			const RE::NiPoint3 dir = Gun::SwingDir();
			Gun::SlashSpec sl;
			sl.model = "CurseOfAlduin\\fx\\arc_bane.nif";
			sl.from = Gun::SwingFrom();
			sl.dir = dir;
			sl.roll = Gun::SwingRoll(dir);
			sl.speed = 1800.0f, sl.range = 1800.0f, sl.radius = 150.0f, sl.scale = 1.3f;
			sl.damage = FangBase() * BaneMult();   // (Noah: "the same damage as the swords light attack") its own light blow (FangHit)
			sl.flingKill = 20.0f;                  // (the ones it kills go flying - the Bane's far)
			sl.onHit = [](RE::Actor* a, bool killed) {
				auto* pl = RE::PlayerCharacter::GetSingleton();
				// (Noah: "there is a delay with the wind animation. Enemies fly away before the wind explosion goes off") the wind
				// bursts on each one it strikes, the moment it strikes - as they're thrown, not after
				if (a) Gun::ElemBurst(3, a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.45f, 0.0f, 0.4f);
				if (a && pl && killed) BaneKilled(pl);
			};
			Gun::Slash(sl);
			return;
		}
		// (Noah: "Dragon shout blast" - "a huge Unrelenting Force shockwave in a cone, flinging all the dead far away") the heavy
		// swing: the Thu'um itself - Skyrim's Unrelenting Force wave, huge, tearing down the line before you and throwing every
		// one of the dead it meets far away
		{
			const RE::NiPoint3 dir = Gun::SwingDir();
			Gun::SlashSpec sl;
			sl.model = "CurseOfAlduin\\fx\\bane_shout.nif";
			sl.from = Gun::SwingFrom();
			sl.dir = dir;
			sl.roll = 0.0f;
			sl.speed = 2400.0f, sl.range = 1500.0f, sl.radius = 340.0f, sl.scale = 1.6f;
			sl.share = 0.0f, sl.damage = FangBase() * BaneMult() * 2.4f;
			sl.flingKill = 26.0f;
			const RE::NiPoint3 me = p->GetPosition();
			sl.onHit = [me](RE::Actor* a, bool killed) {
				auto* pl = RE::PlayerCharacter::GetSingleton();
				if (!a) return;
				if (!killed) Spellcraft::Fling(a, me - RE::NiPoint3{ 0, 0, 80.0f }, 18.0f);   // (the living thrown too)
				else if (pl) BaneKilled(pl);
			};
			Gun::Slash(sl);
			Gun::FxBlast("shoutarea", p->GetPosition() + dir * 120.0f);
			Gun::BlastSound("shoutarea", p->GetPosition(), 1.0f);
			logger::info("swords: Alduin's Bane - the Thu'um");
			return;
		}
	}
	// (Noah: "the dragon slayer sword ... is supposed to send lightning slashes similar to Alduin's Bane but not as strong in
	// damage") the Spellsword's wonder: every swing looses a crescent of lightning down the line - a power attack a wider,
	// longer one - at about two fifths of the Bane's wind, and it knocks the dead back rather than hurling them away
	RE::TESObjectWEAP* SlayerForm()
	{
		static RE::TESObjectWEAP* w = nullptr;
		if (!w) if (auto* dh = RE::TESDataHandler::GetSingleton()) w = dh->LookupForm<RE::TESObjectWEAP>(gen::kSlayerWeap, gen::kPlugin);
		return w;
	}
	bool SlayerInHand(RE::PlayerCharacter* p)
	{
		auto* w = SlayerForm();
		return p && w && (p->GetEquippedObject(false) == w || p->GetEquippedObject(true) == w);
	}
	void SlayerSlash(RE::PlayerCharacter* p, bool a_power)
	{
		if (!a_power) {  // a light swing: a crack of lightning round the blade's arc, close in
			const Aim aim0 = AimAt(400.0f);
			RE::NiPoint3 dir0 = aim0.dir; dir0.z = 0;
			if (dir0.Length() < 0.01f) dir0 = { 0, 1, 0 };
			dir0.Unitize();
			const RE::NiPoint3 c = p->GetPosition() + dir0 * 140.0f + RE::NiPoint3{ 0, 0, 80.0f };
			Blast(BlastIndex("shockmass"), c, 0.0f, 0.4f);
			Blast(BlastIndex("runeshock"), c + dir0 * 110.0f, 0.04f, 0.3f);
			Audio::AtPoint(gen::Sound::unleash_stormcaller, c);
			for (auto* f : Foes(p->GetPosition() + dir0 * 110.0f, 340.0f)) {
				Damage(f, FangBase() * BaneMult() * 0.4f, false, false, true);
				Spellcraft::Fling(f, p->GetPosition() - RE::NiPoint3{ 0, 0, 80.0f }, 4.0f);
			}
			return;
		}
		const float range = 2600.0f, width = 230.0f;
		const Aim aim = AimAt(range);
		RE::NiPoint3 dir = aim.dir; dir.z = 0;
		if (dir.Length() < 0.01f) dir = { 0, 1, 0 };
		dir.Unitize();
		const RE::NiPoint3 side{ -dir.y, dir.x, 0 };
		const RE::NiPoint3 at0 = p->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f };
		const int steps = a_power ? 5 : 4;
		for (int k = 1; k <= steps; ++k) {  // (the crescent races out: a crack of lightning a step further each beat)
			const float dd = range / steps * k;
			Blast(BlastIndex("shockmass"), at0 + dir * dd, 0.04f * k, a_power ? 0.45f : 0.32f);
			if (a_power || (k & 1)) Blast(BlastIndex("runeshock"), at0 + dir * (dd - 50.0f) + side * 110.0f * ((k & 1) ? 1.0f : -1.0f), 0.04f * k + 0.02f, 0.28f);
		}
		Audio::AtPoint(gen::Sound::unleash_stormcaller, at0 + dir * 200.0f);
		for (auto* f : InLine(aim, range, width)) {
			Damage(f, FangBase() * BaneMult() * (a_power ? 0.65f : 0.32f), false, false, true);
			Spellcraft::Fling(f, f->GetPosition() - dir * 120.0f - RE::NiPoint3{ 0, 0, 80.0f }, a_power ? 7.0f : 4.0f);
		}
	}
	RE::ObjectRefHandle g_skyMark;
	void SkyFireball(const RE::NiPoint3& a_at)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		static auto* ball = RE::TESForm::LookupByID<RE::SpellItem>(0x0001C789);  // Fireball
		auto* xm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000003B);
		if (!p || !ball || !xm) return;
		auto m = g_skyMark.get();
		if (!m || m->IsDeleted() || m->IsDisabled() || !m->GetParentCell() || !m->GetParentCell()->IsAttached() || m->GetParentCell() != p->GetParentCell()) {
			auto ref = p->PlaceObjectAtMe(xm, false);
			if (!ref) return;
			g_skyMark = ref->CreateRefHandle();
			m = ref;
		}
		// a target to fall on: the nearest of the dead there, else a marker on the ground
		RE::Actor* tgt = nullptr;
		for (auto* f : Foes(a_at, 120.0f)) { tgt = f; break; }
		m->SetPosition(a_at + RE::NiPoint3{ Rand(-250.0f, 250.0f), Rand(-250.0f, 250.0f), 1800.0f });
		m->SetAngle({ 1.45f, 0.0f, Rand(0.0f, 6.28f) });  // (facing down, a little slanted: with no one there it falls on the spot)
		// (Noah: a crash some way into Alduin's shade) never a fireball loosed at one of the dead that is dying, gone or
		// unloaded - it falls on the spot instead
		if (tgt && (tgt->IsDead() || tgt->IsDisabled() || !tgt->Is3DLoaded())) tgt = nullptr;
		if (auto* c = m->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
			c->CastSpellImmediate(ball, false, tgt, 1.0f, false, 0.0f, p);
		}
	}
	void TickBane(RE::PlayerCharacter* p, float a_delta)
	{
		g_baneCast = std::max(0.0f, g_baneCast - a_delta);
		// your shouts return twice as fast while you hold it: the shout's cooldown runs down a second time each frame
		// (the audit: halving ShoutRecoveryMult was saved with the game and halved again on every load - a lasting change)
		if (FangInHand(p)) {
			auto& vt = p->GetActorRuntimeData().voiceTimer;
			if (vt > 0) vt = std::max(0.0f, vt - a_delta);
		}
		if (g_baneStorm > 0) {  // Alduin's shade: meteors on the dead round you
			g_baneStorm -= a_delta;
			if ((g_baneMeteor -= a_delta) <= 0) {
				g_baneMeteor = 0.6f;  // (fewer in the air at once)
				auto foes = Foes(p->GetPosition(), 1400.0f);
				const RE::NiPoint3 at = foes.empty() ? p->GetPosition() + RE::NiPoint3{ Rand(-600, 600), Rand(-600, 600), 0 } : foes[static_cast<std::size_t>(Rand(0, static_cast<float>(foes.size()) - 0.01f))]->GetPosition();
				// (Noah: the meteors themselves should be seen falling) Skyrim's fireball loosed from high above, falling on
				// the spot; the strike (ours) lands as it does
				// (Noah: "every 20 kills rains meteors on enemies for 10 seconds") ours, its fire tail seen the whole way down; its
				// strike where it lands
				Gun::Meteor(at, false, 1.2f, [](const RE::NiPoint3& hit) {
					Gun::MeteorImpact(hit, false, 0.9f);
					g_zones.push_back({ 4, hit, 240.0f, 0.15f, 0.0f, FangBase() * 2.0f, 0, false, 0 });   // (struck at once: it has landed)
				});
			}
		}
	}
	void TickFang(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (p) TickBane(p, a_delta);
		const int dev = g_devSwing.exchange(0);
		if ((g_weaponSwing.exchange(false) || dev) && p) {
			auto* proc = p->GetActorRuntimeData().currentProcess;
			auto* high = proc ? proc->high : nullptr;
			const bool power = dev ? dev == 2 : high && high->attackData && high->attackData->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);
			if (FangInHand(p)) BaneWind(p, power);  // (Noah: every class's every swing - wind slashes down the line that blow the dead FAR away)
			else if (SlayerInHand(p)) Classes::SlayerSwing(power);  // (Dragonbreaker: its embers spent on a charged swing)
			else Classes::MaceSwing(power);  // (the Alchemist's Shadowflame mace: its flames go out with every swing)
		}
		for (auto& t : g_twisters) {
			if (t.chase) {  // turn, slowly, toward the nearest of the dead
				RE::Actor* best = nullptr;
				float      bd = 1600.0f;
				for (auto* f : Foes(t.at, 1600.0f)) {
					if (const float d = f->GetPosition().GetDistance(t.at); d < bd) {
						bd = d, best = f;
					}
				}
				if (best && bd > 60.0f) {
					RE::NiPoint3 want = best->GetPosition() - t.at;
					want.z = 0;
					want.Unitize();
					t.dir = t.dir + (want - t.dir) * std::min(1.0f, a_delta * 2.5f);
					t.dir.z = 0;
					if (t.dir.Length() > 0.01f) {
						t.dir.Unitize();
					}
				}
			}
			t.at += t.dir * (t.speed * a_delta);
			if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(t.at, land) && std::abs(land - t.at.z) < 300.0f) {
				t.at.z = land;
			}
			if (auto r = t.ref.get()) {
				r->SetPosition(t.at);
			}
			for (auto it = t.cd.begin(); it != t.cd.end();) {
				it = (it->second -= a_delta) <= 0 ? t.cd.erase(it) : std::next(it);
			}
			// (Noah: the bows' tornadoes drag in the dead close by and throw them up through the funnel, out of its top)
			if (t.chase) {
				auto* proc = RE::PlayerCharacter::GetSingleton()->GetActorRuntimeData().currentProcess;
				for (auto* f : Foes(t.at, 700.0f)) {
					if (f->IsDead() || t.cd.contains(f->GetFormID()) || !proc) {
						continue;
					}
					RE::NiPoint3 off = f->GetPosition() - t.at;
					off.z = 0;
					const float d = off.Length();
					if (d > 150.0f) {
						Spellcraft::Fling(f, t.at, -(1.6f + 1.4f * (1.0f - d / 700.0f)));  // drawn in
						t.cd[f->GetFormID()] = 0.45f;
					} else {
						// up through the middle: thrown from beneath, straight up
						proc->KnockExplosion(f, f->GetPosition() - RE::NiPoint3{ off.x * 0.3f, off.y * 0.3f, 220.0f }, 7.5f);
						if (t.row >= 0) {
							Spellcraft::BurstAt(t.row, t.at + RE::NiPoint3{ 0, 0, 300.0f }, 70.0f, 0.4f);
						}
						t.cd[f->GetFormID()] = 1.6f;
					}
				}
			}
			if ((t.tick -= a_delta) <= 0) {
				t.tick = 0.25f;
				for (auto* f : Foes(t.at, 230.0f)) {
					Damage(f, t.dmg, false, false, true);
					if (t.kind == 0) {
						g_dots.push_back({ f->GetHandle(), t.dmg * 0.5f, 3.0f, 0 });  // (fire: it burns on)
					}
					if (!f->IsDead() && !t.chase) {
						Stagger(f, 0.4f);
					}
				}
				if (t.row >= 0) {
					Spellcraft::BurstAt(t.row, t.at + RE::NiPoint3{ 0, 0, 90.0f }, 60.0f, 0.3f);
				}
			}
			if ((t.left -= a_delta) <= 0) {
				Gone(t.ref);
			}
		}
		std::erase_if(g_twisters, [](const Twister& t) { return t.left <= 0; });
	}
}
