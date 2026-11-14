// The wonders' looks and blows (Included by Gun.cpp inside its anonymous namespace, after Spellcraft): the Mutagen
// Staff's vials, orb and beam; Alduin's Bane and Fang; the Dragon Slayer's slash; the sky's fireballs.
namespace Props
{
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
	float g_glDelta = 0.016f;
	// (Noah: the Mutagen Gauntlet made a staff - "Orb in a cage of vials") its model (tools/mutagenstaff.py, root MutagenStaff)
	// is the weapon itself, drawn by Skyrim in either body: found there each frame, its vials' levels, tubes and orb shown
	RE::NiAVObject* g_staff[2]{};   // [body: third, first]
	void TickGauntlet(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		g_glDelta = a_delta;
		const bool held = p && (Classes::FlaskElementInHand(false) == 6 || Classes::FlaskElementInHand(true) == 6);
		for (int b = 0; b < 2; ++b) {
			auto* root = held ? p->Get3D(b == 1) : nullptr;
			auto* st = root ? root->GetObjectByName("MutagenStaff") : nullptr;
			g_staff[b] = st;
			if (!st) continue;
			auto& gp = g_parts[0][b];
			if (gp.root != st) {
				FindParts(gp, st);
				logger::info("mutagen staff: found in the {} body (orb {}, vials {})", b == 1 ? "first-person" : "third-person", gp.core != nullptr,
					std::count_if(std::begin(gp.top), std::end(gp.top), [](auto* x) { return x != nullptr; }));
			}
			ShowParts(gp, g_glDelta);
		}
	}

	// the orb's place in the world: the staff in the body you are seeing
	bool PalmOrb(RE::NiPoint3& a_at)
	{
		auto* cam = RE::PlayerCamera::GetSingleton();
		const int b = cam && cam->IsInFirstPerson() ? 1 : 0;
		auto* core = g_staff[b] ? g_parts[0][b].core : nullptr;
		if (!core) return false;
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

	void Gone(RE::ObjectRefHandle& a_h)
	{
		if (auto r = a_h.get()) {
			r->Disable();
			r->SetDelete(true);
		}
		a_h = {};
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
		(void)cls, (void)at;
		// (Noah: "Each light attack hit landed on an enemy should call a meteor from the sky and land on where the enemy was
		// struck" - "Meteor only", "Large") every light blow that lands calls one of the great meteors (the 20 souls' rain's size)
		// down on the one struck, chasing it; where it lands the dead round it are struck hard (the heavy: BaneWind's tornado)
		if (!a_power && !killed) {
			Gun::MeteorAt(a_z, false, 1.2f, [](const RE::NiPoint3& hit) {
				Gun::MeteorImpact(hit, false, 0.9f);
				g_zones.push_back({ 4, hit, 260.0f, 0.15f, 0.0f, FangBase() * BaneMult() * 1.5f, 0, false, 0 });
			});
		} else if (!a_power) {
			Gun::Meteor(at, false, 1.2f, [](const RE::NiPoint3& hit) {   // (the one struck already dead: the meteor still falls where it stood)
				Gun::MeteorImpact(hit, false, 0.9f);
				g_zones.push_back({ 4, hit, 260.0f, 0.15f, 0.0f, FangBase() * BaneMult() * 1.5f, 0, false, 0 });
			});
		}
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
		if (!a_power) return;   // (Noah: the light attack is the meteor on each hit - FangHit; no slash)
		// (Noah, then: "change Alduin's Bane heavy attack to a wind shockwave instead of a lingering tornado" - "Both together")
		// one burst from you: Skyrim's Unrelenting Force blast rolling out all round, and a bright white ring racing across
		// the ground behind it; every one of the dead within 7 m struck as the wave reaches it and thrown far off (the
		// whirlwind's own blow and throw)
		const RE::NiPoint3 c = p->GetPosition();
		const int force = BlastIndex("shoutarea");
		Blast(force, c + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, 0.9f);
		for (int k = 0; k < 6; ++k) {   // (the wave's front, out on every side)
			const float yaw = k * (RE::NI_TWO_PI / 6.0f);
			Blast(force, c + RE::NiPoint3{ std::cos(yaw) * 260.0f, std::sin(yaw) * 260.0f, 50.0f }, 0.08f, 0.5f);
		}
		Gun::Ring("white", c, 30.0f, 520.0f, 0.55f);
		Gun::Ring("white", c, 20.0f, 340.0f, 0.4f);
		Gun::BlastSound("lightmassive", c, 0.7f);
		for (auto* f : Foes(c, 500.0f)) {
			const float d = f->GetPosition().GetDistance(c);
			Tasks::Later(0.05f + d / 1100.0f, [h = f->GetHandle()]() {   // (struck as the wave reaches it)
				auto a = h.get();
				auto* pl = RE::PlayerCharacter::GetSingleton();
				if (!a || !pl || a->IsDead()) return;
				Gun::ElemBurst(3, a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.5f, 0.0f, 0.4f);   // (wind)
				const bool killed = Damage(a.get(), FangBase() * BaneMult() * 1.6f, false, false, true);
				Spellcraft::Fling(a.get(), pl->GetPosition() - RE::NiPoint3{ 0, 0, 80.0f }, killed ? 26.0f : 20.0f);
				if (killed) BaneKilled(pl);
			});
		}
		logger::info("bane: the wind shockwave");
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
