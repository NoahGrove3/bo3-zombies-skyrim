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
		switch (cls) {
		case coffer::kSummoner:
			{
				auto* av = p->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * (a_power ? 0.03f : 0.015f));
				Blast(BlastIndex("dark"), at + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, 0.35f);
				if (killed || a_z->IsDead()) {
					Echo::Thralls(OverlordNpc(), 1, 40.0f, at, 3);
				}
				break;
			}
		case coffer::kDestruction:
			BaneCast(p, a_z, { "Immolate", "Throat of the World", "Finger of the Mountain", "Daybreak" }, a_power ? 2 : 1);
			break;
		case coffer::kSpellsword:
			BaneCast(p, a_z, { "Fiery Grasp", "Freezing Grasp", "Thundering Grasp", "Searing Grasp", "Touch of Death", "Viper's Grasp" }, a_power ? 2 : 1);
			break;
		case coffer::kRanger:
			break;  // (the Ranger's magic is the swing's wind)
		default:
			g_dots.push_back({ a_z->GetHandle(), base * 0.25f, 4.0f });  // (no path yet: plain dragonfire)
			Blast(BlastIndex("dragonfire"), at + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, 0.4f);
			break;
		}
	}
	// the Ranger's wind slash: a crescent of wind down the line from every swing - hit or not - that hurls the dead far away
	void BaneWind(RE::PlayerCharacter* p, bool a_power)
	{
		const float range = a_power ? 2600.0f : 1800.0f, width = a_power ? 260.0f : 170.0f;
		const Aim aim = AimAt(range);
		RE::NiPoint3 dir = aim.dir; dir.z = 0;
		if (dir.Length() < 0.01f) dir = { 0, 1, 0 };
		dir.Unitize();
		const RE::NiPoint3 side{ -dir.y, dir.x, 0 };
		const RE::NiPoint3 at0 = p->GetPosition() + RE::NiPoint3{ 0, 0, 90.0f };
		const int steps = a_power ? 6 : 4;
		for (int k = 1; k <= steps; ++k) {  // (the crescent races out: a burst of wind a step further each beat)
			const float d = range / steps * k;
			Blast(BlastIndex("shoutarea"), at0 + dir * d, 0.035f * k, a_power ? 0.5f : 0.35f);
			if (a_power) Blast(BlastIndex("shoutarea"), at0 + dir * (d - 60.0f) + side * 140.0f * ((k & 1) ? 1.0f : -1.0f), 0.035f * k + 0.02f, 0.3f);
		}
		Audio::AtPoint(gen::Sound::unleash_hearth, at0 + dir * 200.0f);
		for (auto* f : InLine(aim, range, width)) {
			if (Damage(f, FangBase() * BaneMult() * (a_power ? 1.6f : 0.8f), false, false, true)) BaneKilled(p);
			Spellcraft::Fling(f, f->GetPosition() - dir * 120.0f - RE::NiPoint3{ 0, 0, 80.0f }, a_power ? 24.0f : 16.0f);  // (VERY far)
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
		if (!m || m->IsDeleted() || m->GetParentCell() != p->GetParentCell()) {
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
				g_baneMeteor = 0.55f;
				auto foes = Foes(p->GetPosition(), 1400.0f);
				const RE::NiPoint3 at = foes.empty() ? p->GetPosition() + RE::NiPoint3{ Rand(-600, 600), Rand(-600, 600), 0 } : foes[static_cast<std::size_t>(Rand(0, static_cast<float>(foes.size()) - 0.01f))]->GetPosition();
				// (Noah: the meteors themselves should be seen falling) Skyrim's fireball loosed from high above, falling on
				// the spot; the strike (ours) lands as it does
				SkyFireball(at);
				g_zones.push_back({ 4, at, 240.0f, 0.95f, 0.9f, FangBase() * 2.0f, 0, false, 0 });
			}
		}
	}
	void TickFang(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (p) TickBane(p, a_delta);
		if (g_weaponSwing.exchange(false) && FangInHand(p)) {
			auto* proc = p->GetActorRuntimeData().currentProcess;
			auto* high = proc ? proc->high : nullptr;
			const bool power = high && high->attackData && high->attackData->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);
			if (Classes::Get() == coffer::kRanger) BaneWind(p, power);  // (the Ranger's needs no contact: every swing)
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
						g_dots.push_back({ f->GetHandle(), t.dmg * 0.5f, 3.0f });  // (fire: it burns on)
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
