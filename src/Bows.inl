// The Echo Altar's bows (sheet: bows; models tools/bows.py; arrows tools/spellfx.py). Included by Gun.cpp inside its
// anonymous namespace, after Spellcraft.inl (their shots, bursts and glows are the spells').
//
//   one carried at a time (the altar's second wheel, "Call a Bow"; a new one takes the old one's place)
//   the bow is Skyrim's: its draw animation (and any bow animation mod), its arrow on the string, full movement while
//   drawn (src/Events.cpp FreeBowMovement). Its own arrow is a harmless blink: the shot that leaves is the element's
//   energy arrow (Spellcraft::Launch, arrow mesh), flying, struck and bursting like the spell it belongs to, with that
//   spell's effect (burn, freeze, chain, gust, sear, drain).
//   drawing: the element gathers at the arrow's head, growing with the pull, a flash when it is full. A full draw hits
//   hardest (a quick shot about a third); reforged: +power, then arrows that pierce, then three arrows at once.
//   Skyhowl (the wind bow), drawn full, looses a blast of wind instead: everything before you hurled away (a Thundergun).
//   Damage: a share of a zombie's night health (sheet: share), the night's power curve, reforging, perks; headshots double.
namespace BowsImpl
{
	constexpr int kN = static_cast<int>(gen::kBows.size());
	std::array<std::uint8_t, gen::kBows.size()> g_level{};
	float                    g_draw = 0;       // 0..1 how far the bow is drawn (eased with the animation)
	bool                     g_wasDrawing = false, g_fullSaid = false;
	int                      g_ballRow = -1;
	Spellcraft::Ball         g_ball;
	float                    g_spin = 0;

	RE::TESObjectWEAP* Weap(int i) { return i >= 0 && i < kN ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectWEAP>(gen::kBows[i].weap, gen::kPlugin) : nullptr; }
	RE::TESAmmo*       Ammo(int i) { return i >= 0 && i < kN ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESAmmo>(gen::kBows[i].ammo, gen::kPlugin) : nullptr; }
	int Of(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return -1;
		}
		for (int i = 0; i < kN; ++i) {
			if (Weap(i) == a_form) {
				return i;
			}
		}
		return -1;
	}
	int Held()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) {
			return -1;
		}
		const int r = Of(p->GetEquippedObject(false));
		return r >= 0 ? r : Of(p->GetEquippedObject(true));
	}
	int Carried()
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		for (int i = 0; p && i < kN; ++i) {
			if (auto* w = Weap(i); w && p->GetItemCount(w) > 0) {
				return i;
			}
		}
		return -1;
	}
	int GunOf(int i)  // the spell's row in the weapons sheet (its sounds, its headshot multiplier)
	{
		const auto id = gen::kSpellcraft[gen::kBows[i].spellRow].id;
		for (int g = 0; g < static_cast<int>(gen::kGuns.size()); ++g) {
			if (gen::kGuns[g].id == id) {
				return g;
			}
		}
		return 0;
	}
	bool Wind(int i) { return gen::kSpellcraft[gen::kBows[i].spellRow].id == "embermortar"; }

	// its arrows never run out: a quiver kept full, and on the string
	void TopUp(int i, bool a_equip)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* am = Ammo(i);
		if (!p || !am) {
			return;
		}
		if (const int n = p->GetItemCount(am); n < 40) {
			p->AddObjectToContainer(am, nullptr, 60 - n, nullptr);
		}
		if (a_equip || p->GetCurrentAmmo() != am) {
			if (auto* em = RE::ActorEquipManager::GetSingleton()) {
				em->EquipObject(p, am, nullptr, 1, nullptr, false, false, false, true);
			}
		}
	}

	void Give(int i)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* w = Weap(i);
		if (!p || !w) {
			return;
		}
		for (int k = 0; k < kN; ++k) {  // one bow at a time: the old one (and its arrows) goes back to the altar
			if (k == i) {
				continue;
			}
			if (auto* ow = Weap(k); ow && p->GetItemCount(ow) > 0) {
				p->RemoveItem(ow, p->GetItemCount(ow), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				g_level[k] = 0;
				logger::info("bows: {} given back", gen::kBows[k].label);
			}
			if (auto* oa = Ammo(k); oa && p->GetItemCount(oa) > 0) {
				p->RemoveItem(oa, p->GetItemCount(oa), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
		}
		if (p->GetItemCount(w) <= 0) {
			p->AddObjectToContainer(w, nullptr, 1, nullptr);
		}
		TopUp(i, false);
		if (auto* em = RE::ActorEquipManager::GetSingleton()) {
			em->EquipObject(p, w, nullptr, 1, nullptr, false, false, true, true);
		}
		TopUp(i, true);
		logger::info("bows: {} in hand", gen::kBows[i].label);
	}

	float ShotDamage(int i, float a_draw)
	{
		const float share = gen::kBows[i].share * (1.0f + gen::kForgeStep * g_level[i]);
		const float pull = 0.35f + 0.65f * std::clamp(a_draw, 0.0f, 1.0f);
		return Director::RoundHealth() * gen::kRounds.hpScale * share * pull * (0.7f + 0.3f * Arcana::Power()) * Perks::DamageMult();
	}

	// where the arrow's head sits while drawn: in first person a point in the view (the arms have their own camera),
	// in third person the bow hand
	RE::NiPoint3 HeadAt(RE::PlayerCharacter* p)
	{
		if (Spellcraft::FirstPerson()) {
			return Spellcraft::ViewPoint(62.0f, -2.5f, 5.0f);
		}
		if (auto* m = Spellcraft::Bone(p, "NPC L MagicNode [LMag]")) {
			return m->world.translate;
		}
		return AimAt(80.0f).eye;
	}

	void Fire(int i, float a_draw)
	{
		auto*       p = RE::PlayerCharacter::GetSingleton();
		const int   row = gen::kBows[i].spellRow, gun = GunOf(i), lv = g_level[i];
		const Aim   aim = AimAt(8000.0f);
		const auto  from = HeadAt(p);
		const float dmg = ShotDamage(i, a_draw);
		const bool  full = a_draw >= 0.98f;
		if (Wind(i) && full) {  // Skyhowl drawn full: the gale itself - everything before you hurled away
			const auto cone = InCone(aim, 1200.0f + 150.0f * lv, 34.0f);
			for (auto* f : cone) {
				Spellcraft::Strike(row, f, dmg * 0.75f, false, gen::kFxNone, aim.eye);
				Spellcraft::Fling(f, aim.eye - aim.dir * 120.0f, 3.2f + 0.6f * lv);
			}
			for (int k = 1; k <= 4; ++k) {
				Spellcraft::BurstAt(row, aim.eye + aim.dir * (260.0f * k), 70.0f + 30.0f * k, 0.5f);
			}
			Spellcraft::PlayAt(Spellcraft::Snd(gun, &gen::GunSounds::boom), from);
			logger::info("bows: Skyhowl's gale ({} hurled)", cone.size());
			return;
		}
		RE::NiPoint3 dir = aim.hit - from;
		if (dir.Length() < 1.0f) {
			dir = aim.dir;
		}
		dir.Unitize();
		const int   pierce = 1 + (lv >= 2 ? 1 : 0);
		const int   arrows = lv >= 3 ? 3 : 1;
		const float scale = full ? 1.25f : 1.0f;
		for (int k = 0; k < arrows; ++k) {
			const float yaw = (k - (arrows - 1) * 0.5f) * 0.07f;  // a fan of three, a few degrees apart
			const RE::NiPoint3 d{ dir.x * std::cos(yaw) - dir.y * std::sin(yaw), dir.x * std::sin(yaw) + dir.y * std::cos(yaw), dir.z };
			Spellcraft::Launch(row, gun, from, d, 6500.0f, dmg * (k == 0 ? 1.0f : 0.6f), 9.0f, pierce, scale, -1, true);
		}
		if (full) {
			Spellcraft::BurstAt(row, from, 22.0f, 0.18f);  // (a flash off the string)
		}
	}

	// ---- the bow alive in your hands (Noah: lightning coursing through the lightning bow): the element's own effect
	// meshes hung on the bow's limb bones (they bend with the draw, and in first person they are drawn with the arms):
	//   shock  arcs crackling along both limbs, sparks flickering at the tips
	//   fire   tongues of flame licking up the limbs, embers glowing
	//   frost  cold glints and ice crystals breathing along the limbs
	//   wind   gusts whirling round the limbs and tips
	//   sun    motes of light pulsing along the limbs, a halo at the grip
	//   blood  drops of blood welling at the grip and running down to the tips
	struct Mote
	{
		Spellcraft::Fx fx;
		int            bone = 0;       // 0..3: up 1, up 2, low 1, low 2
		float          x0 = 0, x1 = 0; // along the bone (its local +X)
		float          phase = 0;
		int            kind = 0;       // 0 billboard glow, 1 spinning 3D piece, 2 arc segment, 3 a drop running along
	};
	std::vector<Mote> g_motes;
	int               g_moteBow = -1;
	float             g_moteT = 0, g_arcJitter = 0;
	std::array<RE::NiNode*, 4> g_bones{};
	constexpr const char* kBones[4] = { "Bow_UpBone1", "Bow_UpBone2", "Bow_LoBone1", "Bow_LoBone2" };
	constexpr float       kBoneLen[4] = { 20.9f, 28.0f, 20.9f, 28.0f };

	void ClearMotes()
	{
		for (auto& m : g_motes) {  // (detached from wherever the engine has them now: a re-equipped bow frees its old bones)
			if (auto* o = m.fx.obj.get(); o && o->parent) {
				o->parent->DetachChild2(o);
			}
			m.fx.parent = nullptr;
		}
		g_motes.clear();
		g_moteBow = -1;
		g_bones.fill(nullptr);
	}
	void AddMote(std::string_view a_id, const char* a_part, int a_bone, float a_x0, float a_x1, int a_kind)
	{
		Mote m;
		m.fx = Spellcraft::Fx{ Spellcraft::Load(a_id, a_part) };
		if (!m.fx.obj) {
			return;
		}
		m.bone = a_bone, m.x0 = a_x0, m.x1 = a_x1, m.kind = a_kind, m.phase = Rand(0.0f, 6.28f);
		g_motes.push_back(std::move(m));
	}
	void BuildMotes(int i)
	{
		ClearMotes();
		const auto id = gen::kSpellcraft[gen::kBows[i].spellRow].id;
		const std::string_view el = gen::kBows[i].element;
		for (int b = 0; b < 4; ++b) {
			const float L = kBoneLen[b];
			if (el == "shock") {
				AddMote(id, "beam", b, 1.5f, L * 0.5f, 2);
				AddMote(id, "beam", b, L * 0.5f, L - 1.0f, 2);
				AddMote(id, "hand", b, L - 1.0f, L - 1.0f, 0);
			} else if (el == "fire") {
				AddMote(id, "aura", b, L * 0.35f, L * 0.35f, 1);
				AddMote(id, "aura", b, L * 0.8f, L * 0.8f, 1);
				AddMote(id, "hand", b, L * 0.6f, L * 0.6f, 0);
			} else if (el == "frost") {
				AddMote(id, "hand", b, L * 0.3f, L * 0.3f, 0);
				AddMote(id, "hand", b, L * 0.75f, L * 0.75f, 0);
				AddMote(id, "aura", b, L * 0.55f, L * 0.55f, 1);
			} else if (el == "wind") {
				AddMote(id, "aura", b, L * 0.5f, L * 0.5f, 1);
				AddMote(id, "hand", b, L * 0.9f, L * 0.9f, 0);
			} else if (el == "sun") {
				AddMote(id, "hand", b, L * 0.25f, L * 0.25f, 0);
				AddMote(id, "hand", b, L * 0.7f, L * 0.7f, 0);
				AddMote(id, "aura", b, L * 0.95f, L * 0.95f, 1);
			} else {  // blood: drops welling at the grip and running down each limb
				AddMote(id, "hand", b, 0.0f, L, 3);
				AddMote(id, "hand", b, 0.0f, L, 3);
				g_motes.back().phase = 3.14f;
			}
		}
		g_moteBow = i;
	}
	void SetLocal(Spellcraft::Fx& a_fx, const RE::NiPoint3& a_at, const RE::NiMatrix3& a_rot, float a_scale)
	{
		auto* o = a_fx.obj.get();
		if (!o) {
			return;
		}
		o->local.translate = a_at;
		o->local.rotate = a_rot;
		o->local.scale = std::max(0.0001f, a_scale);
		RE::NiUpdateData ud;
		ud.time = 0.0f;
		o->Update(ud);
	}
	void TickMotes(RE::PlayerCharacter* p, int i, float a_delta)
	{
		const bool out = i >= 0 && p->AsActorState()->IsWeaponDrawn();
		if (!out) {
			if (!g_motes.empty()) {
				ClearMotes();
			}
			return;
		}
		if (g_moteBow != i) {
			BuildMotes(i);
		}
		// the bow's limb bones, in the view the player sees (first person: the arms' own skeleton)
		auto* root = p->Get3D(Spellcraft::FirstPerson());
		for (int b = 0; b < 4; ++b) {
			auto* o = root ? root->GetObjectByName(kBones[b]) : nullptr;
			g_bones[b] = o ? o->AsNode() : nullptr;
		}
		g_moteT += a_delta;
		const bool rejit = (g_arcJitter -= a_delta) <= 0;
		if (rejit) {
			g_arcJitter = 0.055f;  // (lightning: a new path every few frames)
		}
		const float pull = 1.0f + 0.8f * g_draw;  // everything flares as the bow is drawn
		for (auto& m : g_motes) {
			auto* bone = g_bones[m.bone];
			if (!bone) {
				m.fx.Show(false);
				continue;
			}
			if (auto* o = m.fx.obj.get(); o->parent != bone) {
				if (o->parent) {
					o->parent->DetachChild2(o);
				}
				bone->AttachChild(o, true);
				m.fx.parent = bone;
			}
			const float t = g_moteT + m.phase;
			const RE::NiMatrix3 face = bone->world.rotate.Transpose() * Spellcraft::FaceView(t);
			switch (m.kind) {
			case 0:  // a glow faced to the eye, breathing and flickering
				{
					const float fl = 0.75f + 0.25f * std::sin(t * 7.0f) + 0.1f * std::sin(t * 23.0f);
					m.fx.Show(true);
					SetLocal(m.fx, { m.x0, 0.0f, 0.0f }, face, 0.28f * fl * pull);
				}
				break;
			case 1:  // a 3D piece (tongues, crystals, gusts) turning in place
				m.fx.Show(true);
				SetLocal(m.fx, { m.x0, 0.0f, 0.0f }, AxisAngle({ 1, 0, 0 }, t * 2.6f) * AxisAngle({ 0, 0, 1 }, t * 1.3f),
					(0.2f + 0.05f * std::sin(t * 5.0f)) * pull);
				break;
			case 2:  // an arc: a jagged segment along the limb, a new path every few frames, sometimes dark
				if (rejit) {
					const bool on = Rand(0.0f, 1.0f) < 0.8f;
					m.fx.Show(on);
					if (on) {
						const RE::NiPoint3 a{ m.x0, Rand(-1.6f, 1.6f), Rand(-1.6f, 1.6f) }, b{ m.x1, Rand(-1.6f, 1.6f), Rand(-1.6f, 1.6f) };
						const RE::NiPoint3 d = b - a;
						SetLocal(m.fx, a, Spellcraft::Facing(d), d.Length() / Spellcraft::kSeg * pull);
					}
				}
				break;
			default:  // a drop running down the limb, swelling then fading as it reaches the tip
				{
					const float u = std::fmod(t * 0.35f, 1.0f);
					m.fx.Show(true);
					SetLocal(m.fx, { m.x0 + (m.x1 - m.x0) * u, -1.2f, 0.0f }, face, 0.22f * std::sin(u * 3.14159f) * pull + 0.02f);
				}
				break;
			}
		}
	}

	void HideBall()
	{
		if (g_ballRow >= 0) {
			g_ball.Show(false);
		}
	}

	void Tick(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !p->Is3DLoaded()) {
			return;
		}
		const int i = Held();
		static float quiver = 0;
		if (i >= 0 && (quiver -= a_delta) <= 0) {
			quiver = 1.0f;
			TopUp(i, false);
		}
		using S = RE::ATTACK_STATE_ENUM;
		const auto st = p->AsActorState()->GetAttackState();
		const bool drawing = i >= 0 && (st == S::kBowDraw || st == S::kBowAttached || st == S::kBowDrawn);
		if (drawing) {
			g_draw = std::min(1.0f, g_draw + a_delta / 0.9f);
			if (st == S::kBowDrawn) {
				g_draw = std::max(g_draw, 0.85f + 0.15f * g_draw);
			}
			const int row = gen::kBows[i].spellRow;
			if (g_ballRow != row) {
				g_ball.Detach();
				g_ball.Need(row);
				g_ball.Attach(Spellcraft::WorldRoot());
				g_ballRow = row;
			}
			g_spin += a_delta * (1.5f + 4.0f * g_draw);
			g_ball.Show(true);
			const float breathe = g_draw >= 0.98f ? 1.0f + 0.12f * std::sin(g_spin * 4.0f) : 1.0f;
			g_ball.Place(HeadAt(p), Spellcraft::Spin(g_spin), (0.1f + 0.32f * g_draw) * breathe, 1.0f + 0.6f * g_draw);
			if (g_draw >= 0.98f && !g_fullSaid) {
				g_fullSaid = true;
				Spellcraft::Play2D(Spellcraft::Snd(GunOf(i), &gen::GunSounds::full), 0.8f);
				Spellcraft::BurstAt(row, HeadAt(p), 18.0f, 0.2f);
			}
		} else {
			if (g_wasDrawing && i >= 0 && (st == S::kBowReleasing || st == S::kBowReleased || st == S::kBowNextAttack || st == S::kBowFollowThrough)) {
				Fire(i, g_draw);
			}
			g_draw = 0, g_fullSaid = false;
			HideBall();
		}
		g_wasDrawing = drawing;
		TickMotes(p, i, a_delta);
		std::scoped_lock l(Hud::Lock());
		Hud::Edit().bowHeld = i;
		Hud::Edit().bowDraw = drawing ? g_draw : 0.0f;
	}

	void Reset()
	{
		ClearMotes();
		g_ball.Detach();
		g_ballRow = -1, g_draw = 0, g_wasDrawing = false, g_fullSaid = false;
	}
}
