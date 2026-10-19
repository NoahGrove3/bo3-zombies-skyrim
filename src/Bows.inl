// The Echo Altar's bows (sheet: bows; models tools/bows.py; arrows tools/spellfx.py). Included by Gun.cpp inside its
// anonymous namespace, after Spellcraft.inl (their shots, bursts and glows are the spells').
//
//   one carried at a time (the altar's second wheel, "Call a Bow"; a new one takes the old one's place)
//   the bow is Skyrim's: its draw animation (and any bow animation mod), its arrow on the string, full movement while
//   drawn (src/Events.cpp FreeBowMovement). Its own arrow is a harmless blink: the shot that leaves is the element's
//   energy arrow (Spellcraft::Launch, arrow mesh), flying, struck and bursting like the spell it belongs to, with that
//   spell's effect (burn, freeze, chain, gust, sear, drain).
//   drawing: the element gathers at the arrow's head, growing with the pull, a flash when it is full. A full draw hits
//   hardest (a quick shot about a third); each reforge tier adds power and a new attack (below: Noah's list).
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
	// (Noah: each tier draws faster) the bow's speed (Skyrim scales the draw animation by it): x1, x1.2, x1.4, x1.65
	constexpr float kDrawSpeed[4] = { 1.0f, 1.2f, 1.4f, 1.65f };
	float DrawSpeed(int i) { return i >= 0 && i < kN ? kDrawSpeed[std::clamp<int>(g_level[i], 0, 3)] : 1.0f; }
	void  ApplySpeed(int i)
	{
		if (auto* w = Weap(i)) {
			w->weaponData.speed = DrawSpeed(i);
		}
	}
	void ApplyAllSpeeds()
	{
		for (int k = 0; k < kN; ++k) {
			ApplySpeed(k);
		}
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
				ApplySpeed(k);
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
		if (Spellcraft::FirstPerson()) {  // (Noah: the glow sat over the crosshair) at the arrow's head, resting on the bow hand
			if (auto* h = Spellcraft::Bone(p, "NPC L Hand [LHnd]")) {
				return h->world.translate + AimAt(80.0f).dir * 10.0f + RE::NiPoint3{ 0, 0, 4.0f };
			}
			return Spellcraft::ViewPoint(62.0f, -2.5f, 5.0f);
		}
		if (auto* m = Spellcraft::Bone(p, "NPC L MagicNode [LMag]")) {
			return m->world.translate;
		}
		return AimAt(80.0f).eye;
	}

	// ---- each reforge tier is a new attack (Noah's list): power grows with every tier too
	//   Emberstring  0 burns   I fireball burst   II cinder trail on the ground   III full draw: a volley of five meteors
	//   Rimehorn     0 slows   I full draw freezes solid   II frozen ones shatter into shards   III full draw: a lance freezing a line
	//   Stormreach   0 jumps to one more   I chain lightning (three)   II storm mark: the struck one shocks those round it   III full draw: a thunderbolt from the sky
	//   Skyhowl      0 knockback   I full draw: the gale   II a cyclone where it lands   III full draw: the tempest, flung skyward, a cyclone behind
	//   Dawnspire    0 sears and staggers   I a radiant burst round it   II full draw: a sunbeam through everything   III full draw: dawnbreak - the weak turn to ash
	//   Thirstbow    0 heals you a little   I bleeds   II a blood tether that feeds you   III kills burst into orbs of blood: health and power
	enum class Kind { Fire, Frost, Shock, Wind, Sun, Blood };
	constexpr const char* kTierNames[6][4] = {
		{ "Ember Arrow", "Flare Shot", "Cinder Trail", "Fire Tornado" },
		{ "Frost Arrow", "Deep Freeze", "Shatter", "Ice Tornado" },
		{ "Spark Arrow", "Chain Lightning", "Storm Mark", "Lightning Tornado" },
		{ "Gust Arrow", "The Gale", "Cyclone Arrow", "Tornado" },
		{ "Sunlit Arrow", "Radiant Burst", "Sunbeam", "Sunfire Tornado" },
		{ "Leech Arrow", "Hemorrhage", "Blood Tether", "Blood Tornado" } };
	// (Noah: each bow's last upgrade - a tornado of its element where the arrow lands, slow, hunting the dead for 5 s)
	Kind KindOf(int i)
	{
		const std::string_view e = gen::kBows[i].element;
		return e == "fire" ? Kind::Fire : e == "frost" ? Kind::Frost : e == "shock" ? Kind::Shock : e == "wind" ? Kind::Wind : e == "sun" ? Kind::Sun : Kind::Blood;
	}

	struct Patch { int row; RE::NiPoint3 at; float r, t, tick, dps; };            // cinders on the ground
	struct Mark { RE::ActorHandle a; int row; float t, tick, dmg; };               // a storm mark
	struct Cyclone { int row; RE::NiPoint3 at; float r, t, tick, spin; };          // a whirlwind that drags the dead in
	struct Tether { RE::ActorHandle a; int row; float t, tick, dmg; int jumps; };  // a blood tether
	struct Orb { RE::NiPoint3 at; float t; Spellcraft::Ball ball; };               // an orb of blood flying home
	struct Meteor { int row, gun; RE::NiPoint3 to; float delay, dmg; };            // a falling arrow of the volley
	std::vector<Patch>   g_patches;
	std::vector<Mark>    g_marks;
	std::vector<Cyclone> g_cyclones;
	std::vector<Tether>  g_tethers;
	std::vector<Orb>     g_orbs;
	std::vector<Meteor>  g_meteors;
	float                g_harvest = 0;   // Crimson Harvest: seconds of +damage left

	bool Frozen(RE::Actor* a)
	{
		for (auto& h : Spellcraft::g_held) {
			if (h.a.get().get() == a && h.t > 0) {
				return true;
			}
		}
		return false;
	}
	void Heal(float a_frac)
	{
		auto* av = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
		av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth) * a_frac);
	}
	void ZapChain(int row, RE::Actor* a_from, float a_dmg, int a_jumps)
	{
		std::vector<RE::Actor*> done{ a_from };
		RE::Actor*              cur = a_from;
		float                   d = a_dmg;
		for (int k = 0; k < a_jumps; ++k) {
			RE::Actor* next = nullptr;
			float      best = 450.0f;
			for (auto* f : Foes(cur->GetPosition(), 450.0f)) {
				if (std::ranges::find(done, f) == done.end()) {
					if (const float dd = f->GetPosition().GetDistance(cur->GetPosition()); dd < best) {
						best = dd, next = f;
					}
				}
			}
			if (!next) {
				break;
			}
			d *= 0.7f;
			Spellcraft::StreakAt(row, HeadPos(cur) - RE::NiPoint3{ 0, 0, 30 }, HeadPos(next) - RE::NiPoint3{ 0, 0, 30 }, 0.5f, 0.18f, true);
			Damage(next, d, false, false);
			done.push_back(next);
			cur = next;
		}
	}
	void AddCyclone(int row, const RE::NiPoint3& at, float r, float secs)
	{
		g_cyclones.push_back({ row, at, r, secs, 0.0f, 0.0f });
		Spellcraft::BurstAt(row, at, r * 0.6f, 0.5f);
	}
	void Dawnbreak(int row, const RE::NiPoint3& at, float dmg)
	{
		for (auto* f : Foes(at, 450.0f)) {
			auto*       av = f->AsActorValueOwner();
			const float frac = av->GetActorValue(RE::ActorValue::kHealth) / std::max(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth));
			if (Director::IsRoundUndead(f) && !Director::IsBoss(f) && frac < 0.35f) {
				Damage(f, 1.0e7f, false, false);  // turned to ash
			} else {
				Spellcraft::Strike(row, f, dmg * 1.2f, false, gen::kFxHoly, at);
			}
		}
		Spellcraft::BurstAt(row, at, 260.0f, 0.8f);
		Spellcraft::BurstAt(row, at + RE::NiPoint3{ 0, 0, 60 }, 160.0f, 0.5f);
		Blast(BlastIndex("turnundead"), at, 0.0f, 0.8f);
	}

	// an arrow of ours strikes a body
	// every arrow lands with its element: Skyrim's own explosion of that element and a burst of the spell's light
	void Impact(const Spellcraft::Shot& s, const RE::NiPoint3& p)
	{
		static constexpr const char* kSmall[6] = { "fireball", "frost", "runeshock", "shoutarea", "light", "bloodplague" };
		static constexpr const char* kBig[6] = { "firestorm", "icevolley", "shockmass", "giantslam", "lightmassive", "darkmassive" };
		const int k = static_cast<int>(KindOf(s.bow));
		// (Noah: from the first tier the arrow lands as a small burst) everything close round the impact is hurt too
		Spellcraft::Area(s.row, p, 140.0f + 30.0f * s.tier, s.damage * 0.55f, gen::kFxNone);
		Gun::FxBlast(s.full ? kBig[k] : kSmall[k], p);
		Spellcraft::BurstAt(s.row, p, s.full ? 150.0f : 80.0f, s.full ? 0.55f : 0.35f);
	}

	void TornadoOf(const Spellcraft::Shot& s, const RE::NiPoint3& p)
	{
		if (s.tier >= 3 && s.full) {
			Props::Tornado(static_cast<int>(KindOf(s.bow)), p, s.vel, 5.0f, s.damage * 0.2f, true, s.row);
		}
	}
	void OnHit(const Spellcraft::Shot& s, RE::Actor* a, const RE::NiPoint3& p, bool killed)
	{
		Impact(s, p);
		TornadoOf(s, a->GetPosition());
		const int  i = s.bow, t = s.tier, row = s.row;
		const float dmg = s.damage;
		switch (KindOf(i)) {
		case Kind::Fire:
			if (t >= 1) {  // Flare Shot: it bursts into a fireball
				Spellcraft::Area(row, p, 190.0f, dmg * 0.5f, gen::kFxIgnite);
				Spellcraft::BurstAt(row, p, 110.0f, 0.4f);
			}
			if (t >= 2) {  // Cinder Trail: the ground behind it burns
				const RE::NiPoint3 back = s.vel.Length() > 1 ? s.vel * (1.0f / s.vel.Length()) : RE::NiPoint3{ 0, 1, 0 };
				for (int k = 0; k < 5; ++k) {
					g_patches.push_back({ row, a->GetPosition() - back * (110.0f * k), 95.0f, 5.0f, 0.0f, dmg * 0.18f });
				}
			}
			break;
		case Kind::Frost:
			if (t < 1 || !s.full) {
				Director::Web(a->GetPosition(), 70.0f, 3.0f, 60.0f, true);  // slowed
			}
			if (t >= 2 && Frozen(a) && !killed) {  // Shatter: the frozen one bursts into shards
				Damage(a, dmg * 1.5f, false, false);
				Spellcraft::Area(row, p, 220.0f, dmg * 0.6f, gen::kFxNone);
				Spellcraft::BurstAt(row, p, 140.0f, 0.5f);
			}
			break;
		case Kind::Shock:
			if (t == 0) {
				ZapChain(row, a, dmg, 1);
			}
			if (t >= 2 && !killed) {  // Storm Mark
				g_marks.push_back({ a->GetHandle(), row, 4.0f, 0.0f, dmg * 0.25f });
			}
			if (false && t >= 3 && s.full) {  // Thunderstrike
				Spellcraft::StreakAt(row, p + RE::NiPoint3{ Rand(-60, 60), Rand(-60, 60), 1400.0f }, p, 1.4f, 0.35f, true);
				for (auto* f : Foes(p, 330.0f)) {
					Spellcraft::Strike(row, f, dmg * 1.5f, false, gen::kFxNone, p);
					Stagger(f, 1.0f);
				}
				Spellcraft::BurstAt(row, p, 220.0f, 0.6f);
				Blast(BlastIndex("skybolt"), p, 0.0f, 0.7f);
			}
			break;
		case Kind::Wind:
			if (t >= 2) {  // Cyclone Arrow
				AddCyclone(row, p, 360.0f, 3.0f);
			}
			break;
		case Kind::Sun:
			if (t >= 1) {  // Radiant Burst
				for (auto* f : Foes(p, 210.0f)) {
					if (f != a && Director::IsRoundUndead(f)) {
						Spellcraft::Strike(row, f, dmg * 0.4f, false, gen::kFxHoly, p);
					}
				}
				Spellcraft::BurstAt(row, p, 120.0f, 0.4f);
			}
			if (false && t >= 3 && s.full) {
				Dawnbreak(row, p, dmg);
			}
			break;
		case Kind::Blood:
			if (t >= 1 && !killed) {  // Hemorrhage
				g_dots.push_back({ a->GetHandle(), dmg * 0.35f / 4.0f, 4.0f });
			}
			if (t >= 2 && !killed) {  // Blood Tether
				g_tethers.clear();
				g_tethers.push_back({ a->GetHandle(), row, 6.0f, 0.0f, dmg * 0.15f, 3 });
			}
			if (t >= 3 && killed) {  // Crimson Harvest
				for (int k = 0; k < 3; ++k) {
					Orb o{ p + RE::NiPoint3{ Rand(-40, 40), Rand(-40, 40), Rand(0, 50) }, 0.0f, {} };
					o.ball.Need(row);
					o.ball.Attach(Spellcraft::WorldRoot());
					g_orbs.push_back(std::move(o));
				}
			}
			break;
		}
	}
	// an arrow of ours lands in the world
	void OnGround(const Spellcraft::Shot& s, const RE::NiPoint3& p)
	{
		Impact(s, p);
		TornadoOf(s, p);
		const int t = s.tier, row = s.row;
		switch (KindOf(s.bow)) {
		case Kind::Fire:
			if (t >= 1) {
				Spellcraft::Area(row, p, 190.0f, s.damage * 0.5f, gen::kFxIgnite);
				Spellcraft::BurstAt(row, p, 110.0f, 0.4f);
			}
			if (t >= 2) {
				g_patches.push_back({ row, p, 120.0f, 5.0f, 0.0f, s.damage * 0.18f });
			}
			break;
		case Kind::Wind:
			if (t >= 2) {
				AddCyclone(row, p, 360.0f, 3.0f);
			}
			break;
		case Kind::Sun:
			if (false && t >= 3 && s.full) {
				Dawnbreak(row, p, s.damage);
			}
			break;
		case Kind::Shock:
			if (false && t >= 3 && s.full) {
				Spellcraft::StreakAt(row, p + RE::NiPoint3{ 0, 0, 1400.0f }, p, 1.4f, 0.35f, true);
				for (auto* f : Foes(p, 330.0f)) {
					Spellcraft::Strike(row, f, s.damage * 1.5f, false, gen::kFxNone, p);
					Stagger(f, 1.0f);
				}
				Spellcraft::BurstAt(row, p, 220.0f, 0.6f);
			}
			break;
		default:
			break;
		}
	}

	// (Noah: no shot unless the bow is fully drawn) let go early and nothing flies: our shot isn't loosed, and Skyrim's own
	// arrow (already on its way) is hidden and harmless; a soft fizzle of the element at the string says why
	float g_hideArrow = 0;
	int   g_rideRow = -1;  // the loosed arrow's element (TickRide)
	float g_rideWait = 0;
	void Fizzle(int i)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		Spellcraft::BurstAt(gen::kBows[i].spellRow, HeadAt(p), 10.0f, 0.2f);
		g_hideArrow = 0.6f;
		static int told = 0;
		if (told++ < 2) {
			Hud::Banner("Draw the bow all the way to loose an arrow", 2.5f);
		}
	}
	void TickHideArrow(float a_delta)
	{
		if (g_hideArrow <= 0) {
			return;
		}
		g_hideArrow -= a_delta;
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* mgr = RE::Projectile::Manager::GetSingleton();
		if (!p || !mgr) {
			return;
		}
		RE::BSSpinLockGuard lock(mgr->projectileLock);
		for (auto* list : { &mgr->limited, &mgr->unlimited, &mgr->pending }) {
			for (auto& h : *list) {
				auto pr = h.get();
				if (!pr || pr->GetFormType() != RE::FormType::ProjectileArrow || pr->GetProjectileRuntimeData().shooter.get().get() != p) {
					continue;
				}
				if (auto* o = pr->Get3D()) {
					o->SetAppCulled(true);
				}
				pr->GetProjectileRuntimeData().weaponDamage = 0.0f;
			}
		}
	}

	void Fire(int i, float a_draw)
	{
		if (a_draw < 0.98f) {
			Fizzle(i);
			return;
		}
		g_rideRow = gen::kBows[i].spellRow, g_rideWait = 0.6f;  // (the glow goes with the arrow: TickRide)
		auto*       p = RE::PlayerCharacter::GetSingleton();
		const int   row = gen::kBows[i].spellRow, gun = GunOf(i), t = g_level[i];
		const Aim   aim = AimAt(8000.0f);
		const auto  from = HeadAt(p);
		const float dmg = ShotDamage(i, a_draw) * (g_harvest > 0 ? 1.25f : 1.0f);
		const bool  full = a_draw >= 0.98f;
		const Kind  kind = KindOf(i);
		Spellcraft::g_bowHit = &OnHit;
		Spellcraft::g_bowGround = &OnGround;
		if (full) {
			Spellcraft::BurstAt(row, from, 22.0f, 0.18f);  // (a flash off the string)
		}
		// the spell's own voice as the arrow leaves: its cast, or at full draw its unleash
		Forms::Play(full ? gen::kGuns[gun].chargeSnd : gen::kGuns[gun].fireSnd);
		// the full-draw attacks that are not a single arrow
		if (full && kind == Kind::Wind && t >= 1 && t < 3) {  // The Gale (I-II; at III the arrow flies, for its tornado)
			const bool tempest = t >= 3;
			const auto cone = InCone(aim, (tempest ? 1600.0f : 1200.0f), tempest ? 48.0f : 34.0f);
			for (auto* f : cone) {
				Spellcraft::Strike(row, f, dmg * 0.75f, false, gen::kFxNone, aim.eye);
				if (tempest) {
					Spellcraft::Fling(f, f->GetPosition() - aim.dir * 60.0f - RE::NiPoint3{ 0, 0, 200.0f }, 5.0f);  // flung skyward
				} else {
					Spellcraft::Fling(f, aim.eye - aim.dir * 120.0f, 3.4f);
				}
			}
			for (int k = 1; k <= (tempest ? 6 : 4); ++k) {
				Spellcraft::BurstAt(row, aim.eye + aim.dir * (260.0f * k), 70.0f + 34.0f * k, 0.5f);
			}
			if (tempest) {
				AddCyclone(row, aim.eye + aim.dir * std::min(900.0f, aim.hit.GetDistance(aim.eye)), 420.0f, 4.0f);
			}
			Spellcraft::PlayAt(Spellcraft::Snd(gun, &gen::GunSounds::boom), from);
			return;
		}
		if (false && full && kind == Kind::Fire && t >= 3) {  // (Meteor Volley: given way to the Fire Tornado): one arrow up, five come down
			Spellcraft::Launch(row, gun, from, aim.dir + RE::NiPoint3{ 0, 0, 1.2f }, 5000.0f, 0.0f, 9.0f, 1, 1.0f, -1, true);
			Spellcraft::g_shots2.back().life = 0.35f;
			for (int k = 0; k < 5; ++k) {
				const RE::NiPoint3 to = aim.hit + RE::NiPoint3{ Rand(-220, 220), Rand(-220, 220), 0 };
				g_meteors.push_back({ row, gun, to, 0.45f + 0.16f * k, dmg });
			}
			return;
		}
		if (false && full && kind == Kind::Frost && t >= 3) {  // (Glacial Lance: given way to the Ice Tornado): a line frozen
			const auto line = InLine(aim, 2400.0f, 70.0f);
			for (auto* f : line) {
				Spellcraft::Strike(row, f, dmg * 1.2f, false, gen::kFxFreeze, HeadPos(f));
			}
			Spellcraft::StreakAt(row, from, aim.eye + aim.dir * std::min(2400.0f, aim.hit.GetDistance(aim.eye)), 1.2f, 0.5f, false);
			Spellcraft::BurstAt(row, aim.hit, 120.0f, 0.5f);
			Spellcraft::PlayAt(Spellcraft::Snd(gun, &gen::GunSounds::boom), from);
			return;
		}
		if (full && kind == Kind::Sun && t >= 2 && t < 3) {  // Sunbeam: through everything
			for (auto* f : InLine(aim, 2600.0f, 55.0f)) {
				Spellcraft::Strike(row, f, dmg, false, gen::kFxHoly, HeadPos(f));
			}
			Spellcraft::StreakAt(row, from, aim.eye + aim.dir * std::min(2600.0f, aim.hit.GetDistance(aim.eye)), 1.0f, 0.4f, false);
			return;
		}
		// an arrow, its effect by bow and tier
		RE::NiPoint3 dir = aim.hit - from;
		if (dir.Length() < 1.0f) {
			dir = aim.dir;
		}
		dir.Unitize();
		int effect = -2;
		switch (kind) {
		case Kind::Fire: effect = gen::kFxIgnite; break;
		case Kind::Frost: effect = full && t >= 1 ? gen::kFxFreeze : gen::kFxNone; break;
		case Kind::Shock: effect = t >= 1 ? gen::kFxChain : gen::kFxNone; break;
		case Kind::Wind: effect = gen::kFxPush; break;
		case Kind::Sun: effect = gen::kFxHoly; break;
		case Kind::Blood: effect = gen::kFxDrain; break;
		}
		Spellcraft::BurstAt(row, from + dir * 30.0f, full ? 26.0f : 16.0f, 0.18f);  // (the string's snap: a flash off the arrow's head)
		Spellcraft::Launch(row, gun, from, dir, 4800.0f, dmg, 30.0f, 1, full ? 1.25f : 1.0f, -1, true);  // (Noah: only some hits counted - a wider body to strike with)
		static int said = 0;
		if (said++ < 8) {
			logger::info("bows: arrow away from ({:.0f},{:.0f},{:.0f}) toward ({:.0f},{:.0f},{:.0f}), {} shots in flight", from.x, from.y, from.z, aim.hit.x, aim.hit.y, aim.hit.z, Spellcraft::g_shots2.size());
		}
		auto& sh = Spellcraft::g_shots2.back();
		sh.bow = i, sh.tier = t, sh.full = full, sh.effect = effect;
	}

	void TickTiers(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		g_harvest = std::max(0.0f, g_harvest - a_delta);
		for (auto& m : g_meteors) {  // the volley comes down
			if ((m.delay -= a_delta) <= 0 && m.delay > -100.0f) {
				const RE::NiPoint3 top = m.to + RE::NiPoint3{ Rand(-80, 80), Rand(-80, 80), 1100.0f };
				Spellcraft::Launch(m.row, m.gun, top, m.to - top, 3800.0f, m.dmg, 14.0f, 1, 1.6f, -1, true);
				auto& sh = Spellcraft::g_shots2.back();
				sh.bow = 0, sh.tier = 1, sh.full = false, sh.effect = gen::kFxIgnite;  // (each bursts like a Flare Shot)
				for (int b = 0; b < kN; ++b) {
					if (KindOf(b) == Kind::Fire) sh.bow = b;
				}
				m.delay = -1000.0f;
			}
		}
		std::erase_if(g_meteors, [](const Meteor& m) { return m.delay <= -100.0f; });
		for (auto& c : g_patches) {  // cinders
			c.t -= a_delta;
			if ((c.tick -= a_delta) <= 0) {
				c.tick = 0.5f;
				for (auto* f : Foes(c.at, c.r)) {
					Damage(f, c.dps * 0.5f, false, false, true);
				}
				Spellcraft::BurstAt(c.row, c.at + RE::NiPoint3{ Rand(-30, 30), Rand(-30, 30), 10 }, c.r * 0.5f, 0.45f);
			}
		}
		std::erase_if(g_patches, [](const Patch& c) { return c.t <= 0; });
		for (auto& m : g_marks) {  // storm marks
			m.t -= a_delta;
			auto a = m.a.get();
			if (!a || a->IsDead()) {
				m.t = 0;
				continue;
			}
			if ((m.tick -= a_delta) <= 0) {
				m.tick = 0.5f;
				int n = 0;
				for (auto* f : Foes(a->GetPosition(), 350.0f)) {
					if (f != a.get() && n++ < 2) {
						Spellcraft::StreakAt(m.row, HeadPos(a.get()) - RE::NiPoint3{ 0, 0, 25 }, HeadPos(f) - RE::NiPoint3{ 0, 0, 25 }, 0.45f, 0.15f, true);
						Damage(f, m.dmg, false, false, true);
					}
				}
			}
		}
		std::erase_if(g_marks, [](const Mark& m) { return m.t <= 0; });
		for (auto& c : g_cyclones) {  // cyclones drag the dead in and hold them
			c.t -= a_delta;
			c.spin += a_delta;
			if ((c.tick -= a_delta) <= 0) {
				c.tick = 0.25f;
				for (auto* f : Foes(c.at, c.r)) {
					if (f->GetPosition().GetDistance(c.at) > 70.0f) {
						Spellcraft::Fling(f, c.at, -0.6f);
					}
					Director::Web(f->GetPosition(), 50.0f, 0.4f, 95.0f);
				}
				Spellcraft::BurstAt(c.row, c.at + RE::NiPoint3{ 0, 0, 40.0f + 60.0f * std::fmod(c.spin, 1.0f) }, 90.0f, 0.3f);
			}
		}
		std::erase_if(g_cyclones, [](const Cyclone& c) { return c.t <= 0; });
		for (auto& b : g_tethers) {  // the blood tether feeds you
			b.t -= a_delta;
			auto a = b.a.get();
			if (!a || a->IsDead() || a->GetPosition().GetDistance(p->GetPosition()) > 1600.0f) {
				RE::Actor* next = nullptr;
				if (b.jumps > 0 && a) {
					float best = 600.0f;
					for (auto* f : Foes(a->GetPosition(), 600.0f)) {
						if (f != a.get() && f->GetPosition().GetDistance(a->GetPosition()) < best) {
							best = f->GetPosition().GetDistance(a->GetPosition()), next = f;
						}
					}
				}
				if (next) {
					b.a = next->GetHandle(), --b.jumps, b.t = std::max(b.t, 3.0f);
				} else {
					b.t = 0;
				}
				continue;
			}
			if ((b.tick -= a_delta) <= 0) {
				b.tick = 0.5f;
				Damage(a.get(), b.dmg, false, false, true);
				Heal(0.02f);
				Spellcraft::StreakAt(b.row, HeadAt(p), HeadPos(a.get()) - RE::NiPoint3{ 0, 0, 30 }, 0.35f, 0.45f, false);
			}
		}
		std::erase_if(g_tethers, [](const Tether& b) { return b.t <= 0; });
		for (auto& o : g_orbs) {  // orbs of blood fly home
			o.t += a_delta;
			const RE::NiPoint3 home = p->GetPosition() + RE::NiPoint3{ 0, 0, 90 };
			RE::NiPoint3       d = home - o.at;
			const float        len = d.Length();
			if (len < 40.0f || o.t > 3.0f) {
				Heal(0.04f);
				g_harvest = 5.0f;
				o.t = 99.0f;
				continue;
			}
			d.Unitize();
			o.at += d * std::min(len, (300.0f + 900.0f * o.t) * a_delta);
			o.ball.Show(true);
			o.ball.Place(o.at, Spellcraft::Spin(o.t * 5), 0.22f, 1.4f);
		}
		for (auto& o : g_orbs) {
			if (o.t >= 99.0f) {
				o.ball.Detach();
			}
		}
		std::erase_if(g_orbs, [](const Orb& o) { return o.t >= 99.0f; });
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
		int            kind = 0;       // 0 billboard glow, 1 spinning 3D piece, 2 arc segment, 3 a drop running along,
		                               // 4 the runes along a limb, 5 a runic crest off a tip, 6 a ring of runes turning round the limb
	};
	std::vector<Mote> g_motes;
	int               g_moteBow = -1, g_moteTier = -1;
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
		g_moteBow = -1, g_moteTier = -1;
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
		// (Noah: more of the bow wakes with every reforge) tier I: a band of runes lights along every limb; II: a runic
		// crest of light off each tip and a ring of runes turning round the inner limbs; III: rings round the tips too
		const int t = g_level[i];
		for (int b = 0; b < 4 && t >= 1; ++b) {
			AddMote(id, "runes", b, 1.5f, kBoneLen[b] - 1.5f, 4);
		}
		if (t >= 2) {
			for (const int b : { 1, 3 }) {
				AddMote(id, "crest", b, kBoneLen[b] - 0.5f, kBoneLen[b] - 0.5f, 5);
			}
			for (const int b : { 0, 2 }) {
				AddMote(id, "ring", b, kBoneLen[b] * 0.55f, kBoneLen[b] * 0.55f, 6);
			}
		}
		if (t >= 3) {
			for (const int b : { 1, 3 }) {
				AddMote(id, "ring", b, kBoneLen[b] * 0.7f, kBoneLen[b] * 0.7f, 6);
				AddMote(id, "hand", b, kBoneLen[b] - 0.5f, kBoneLen[b] - 0.5f, 0);
			}
		}
		g_moteBow = i, g_moteTier = t;
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
		if (g_moteBow != i || g_moteTier != g_level[i]) {
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
		const float pull = (1.0f + 0.8f * g_draw) * (1.0f + 0.12f * g_level[i]);  // everything flares as the bow is drawn (and burns brighter each tier)
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
			case 4:  // the runes along the limb, breathing
				m.fx.Show(true);
				SetLocal(m.fx, { m.x0, 0.0f, 0.0f }, RE::NiMatrix3{}, (m.x1 - m.x0) / 20.0f * (0.96f + 0.04f * std::sin(t * 2.2f)));
				break;
			case 5:  // a crest of light off the tip, flickering like a flame held still
				m.fx.Show(true);
				SetLocal(m.fx, { m.x0, 0.0f, 0.0f }, RE::NiMatrix3{}, (0.9f + 0.1f * std::sin(t * 6.0f) + 0.05f * std::sin(t * 17.0f)) * pull);
				break;
			case 6:  // a ring of runes turning round the limb
				m.fx.Show(true);
				SetLocal(m.fx, { m.x0, 0.0f, 0.0f }, AxisAngle({ 1, 0, 0 }, t * 1.4f), 0.9f * pull);
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

	// (Noah: letting go did nothing) the release, heard from the animation itself: a draw animation mod can skip the
	// releasing states between two frames, but it still sends the bow's release event
	std::atomic<bool> g_releaseHeard{ false };
	class AnimWatch : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_e, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
		{
			if (a_e && a_e->tag.c_str()) {
				std::string t(a_e->tag.c_str());
				std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (t.find("arrowrelease") != std::string::npos || t == "bowrelease" || t == "bowreleased") {
					g_releaseHeard = true;
				}
				if (t == "bowdrawn") {
					g_bowFull = true;
				}
				if (t == "weaponswing") {
					g_weaponSwing = true;
				}
				static std::set<std::string> seen;
				if ((t.find("bow") != std::string::npos || t.find("arrow") != std::string::npos) && seen.size() < 40 && seen.insert(t).second) {
					logger::info("bows: animation event '{}'", t);
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	AnimWatch     g_animWatch;

	// (Noah: the element should charge on the arrowhead as you draw, and stay on it) the arrow's head of light, hung on
	// the nocked arrow itself (the ammunition's model, found in the body the player sees) and grown with the draw
	Spellcraft::Fx  g_tip;
	int             g_tipRow = -1;
	float           g_tipT = 0;
	RE::NiAVObject* FindArrow(RE::NiAVObject* a_o, const RE::NiPoint3& a_near, int a_depth = 0)
	{
		if (!a_o || a_depth > 40) {
			return nullptr;
		}
		const char* n = a_o->name.c_str();
		if (n && (std::string_view(n) == "Arrow:0" || std::string_view(n) == "Arrow") && a_o->world.translate.GetDistance(a_near) < 160.0f && !a_o->GetFlags().any(RE::NiAVObject::Flag::kHidden)) {
			return a_o;
		}
		if (auto* node = a_o->AsNode()) {
			for (auto& c : node->GetChildren()) {
				if (auto* f = FindArrow(c.get(), a_near, a_depth + 1)) {
					return f;
				}
			}
		}
		return nullptr;
	}
	// (Noah: the element sat on the hand, not the arrow's tip) the tip is worked out from the hands themselves: the
	// nock rides the string hand, the arrow points through the bow hand, and its head is an arrow's length from the
	// nock (never short of just past the bow hand). The glow hangs on the bow hand's bone at that point, so it moves
	// with the arms in the same frame (no drift)
	constexpr float kArrowLen = 66.0f;  // the quiver model's arrow: nock at -65, head at 0
	bool ArrowTip(RE::PlayerCharacter* p, RE::NiAVObject*& a_hand, RE::NiPoint3& a_tip)
	{
		auto* root = p->Get3D(Spellcraft::FirstPerson());
		auto* l = root ? root->GetObjectByName("NPC L Hand [LHnd]") : nullptr;
		auto* r = root ? root->GetObjectByName("NPC R Hand [RHnd]") : nullptr;
		if (!l || !r) {
			return false;
		}
		RE::NiPoint3 d = l->world.translate - r->world.translate;
		const float  span = d.Length();
		if (span < 5.0f) {
			return false;
		}
		d = d / span;
		a_tip = r->world.translate + d * std::max(kArrowLen, span + 8.0f);
		a_hand = l;
		return true;
	}
	void TickTip(RE::PlayerCharacter* p, int i, bool a_drawing, float a_delta)
	{
		if (!a_drawing || i < 0) {
			g_tip.Show(false);
			return;
		}
		const int row = gen::kBows[i].spellRow;
		if (g_tipRow != row) {
			g_tip.Detach();
			g_tip = Spellcraft::Fx{ Spellcraft::Load(gen::kSpellcraft[row].id, "tip") };
			g_tipRow = row;
		}
		if (!g_tip.obj) {
			return;
		}
		g_tipT += a_delta;
		const float     grow = 0.25f + 0.75f * g_draw;
		const float     flick = g_draw >= 0.98f ? 1.0f + 0.08f * std::sin(g_tipT * 18.0f) : 1.0f;
		RE::NiAVObject* hand = nullptr;
		RE::NiPoint3    tip;
		if (ArrowTip(p, hand, tip) && hand->AsNode()) {
			auto* host = hand->AsNode();
			if (g_tip.parent != host) {
				if (auto* o = g_tip.obj.get(); o->parent) {
					o->parent->DetachChild2(o);
				}
				host->AttachChild(g_tip.obj.get(), true);
				g_tip.parent = host;
			}
			const auto& w = hand->world;
			const RE::NiPoint3 at = (w.rotate.Transpose() * (tip - w.translate)) / std::max(0.01f, w.scale);
			SetLocal(g_tip, at, RE::NiMatrix3{}, grow * flick / std::max(0.01f, w.scale));
		} else {  // (no hands found: in front of the bow hand, along the aim)
			const auto hp = HeadAt(p);
			g_tip.Attach(Spellcraft::WorldRoot());
			g_tip.Place(hp + AimAt(80.0f).dir * 20.0f, Spellcraft::Facing(AimAt(80.0f).dir), grow * flick);
		}
		g_tip.Show(true);
	}

	// (Noah: the effect should fly with the arrow) once loosed, the element rides the arrow itself: a copy of the
	// tip's glow hung on the head of the player's newest arrow in flight
	std::set<RE::Projectile*> g_ridden;
	void TickRide(float a_delta)
	{
		if (g_rideWait <= 0 || g_rideRow < 0) {
			return;
		}
		g_rideWait -= a_delta;
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* mgr = RE::Projectile::Manager::GetSingleton();
		if (!p || !mgr) {
			return;
		}
		RE::BSSpinLockGuard lock(mgr->projectileLock);
		for (auto* list : { &mgr->limited, &mgr->unlimited, &mgr->pending }) {
			for (auto& h : *list) {
				auto pr = h.get();
				if (!pr || pr->GetFormType() != RE::FormType::ProjectileArrow || pr->GetProjectileRuntimeData().shooter.get().get() != p || g_ridden.contains(pr.get())) {
					continue;
				}
				auto* node = pr->Get3D() ? pr->Get3D()->AsNode() : nullptr;
				if (!node) {
					continue;  // (not drawn yet: the next frame)
				}
				if (auto fx = Spellcraft::Load(gen::kSpellcraft[g_rideRow].id, "tip")) {
					fx->local.translate = { 0.0f, 34.0f, 0.0f };  // the flight model's head (+Y)
					fx->local.scale = 1.0f;
					node->AttachChild(fx.get(), true);
					RE::NiUpdateData ud;
					fx->Update(ud);
				}
				g_ridden.insert(pr.get());
				if (g_ridden.size() > 64) {
					g_ridden.clear();
					g_ridden.insert(pr.get());
				}
				g_rideWait = 0;
				return;
			}
		}
	}
	RE::NiAVObject* g_watched = nullptr;
	float         g_sinceFire = 10.0f;

	void Tick(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !p->Is3DLoaded()) {
			return;
		}
		static float reequip = -1;
		// (the third-person body only: Get3D() flips with the camera, and every flip re-equipped the bow - Noah's fast
		// unequip/re-equip)
		if (auto* root = p->Get3D(false); root != g_watched) {  // (a new body after a load or a race change: listen to its graph)
			p->RemoveAnimationGraphEventSink(&g_animWatch);
			p->AddAnimationGraphEventSink(&g_animWatch);
			g_watched = root;
			reequip = 1.5f;  // (once per new body: after a load)  // (Noah: after a load the bow was invisible until put away and drawn again) - so we do that for you
		}
		if (reequip > 0 && (reequip -= a_delta) <= 0) {
			if (const int hb = Held(); hb >= 0) {
				if (auto* w = Weap(hb); w && p->GetEquippedObject(false) == w) {
					if (auto* em = RE::ActorEquipManager::GetSingleton()) {
						em->UnequipObject(p, w, nullptr, 1, nullptr, false, false, false, true);
						em->EquipObject(p, w, nullptr, 1, nullptr, false, false, false, true);
						logger::info("bows: {} put on again after the load (its model shows)", gen::kBows[hb].label);
					}
				}
			}
		}
		TickHideArrow(a_delta);
		TickRide(a_delta);
		g_sinceFire += a_delta;
		const int i = Held();
		static float quiver = 0;
		if (i >= 0 && (quiver -= a_delta) <= 0) {
			quiver = 1.0f;
			TopUp(i, false);
		}
		using S = RE::ATTACK_STATE_ENUM;
		const auto st = p->AsActorState()->GetAttackState();
		const bool drawing = i >= 0 && (st == S::kBowDraw || st == S::kBowAttached || st == S::kBowDrawn);
		if (drawing && !g_wasDrawing) {
			g_bowFull = false;
		}
		if (drawing && st == S::kBowDrawn) {
			g_bowFull = true;
		}
		g_bowDrawing = drawing;
		if (!drawing) {
			g_bowHeldRelease = false;
		} else if (g_bowHeldRelease && g_bowFull) {
			g_bowHeldRelease = false;
			SendFire(0.0f, 1.0f);  // the early release, now: the arrow leaves at the peak
			logger::info("bows: an early release loosed at the peak of the draw");
		}
		if (drawing && !g_wasDrawing) {  // (Noah: the bows sound like their spells) the spell's charge rises as the string comes back
			Spellcraft::Play2D(Spellcraft::Snd(GunOf(i), &gen::GunSounds::start), 0.7f);
			if (const int hum = Spellcraft::Snd(GunOf(i), &gen::GunSounds::charge); hum >= 0) {
				Audio::LoopStart(Audio::kLoopCharge, static_cast<gen::Sound>(hum));
			}
		}
		if (!drawing && g_wasDrawing) {
			Audio::LoopStop(Audio::kLoopCharge, 150);
		}
		if (drawing) {
			// the draw's own clock never counts as full: only Skyrim's drawn state does (no shot short of the peak)
			g_draw = g_bowFull ? 1.0f : std::min(0.95f, g_draw + a_delta * DrawSpeed(i) / 0.9f);
			const int row = gen::kBows[i].spellRow;
			if (g_ballRow != row) {
				g_ball.Detach();
				g_ball.Need(row);
				g_ball.Attach(Spellcraft::WorldRoot());
				g_ballRow = row;
			}
			g_spin += a_delta * (1.5f + 4.0f * g_draw);
			// (Noah: the glow drifted off the arrow when you moved) the arrowhead's element now lives in the arrow's own
			// model (tools/bowarrows.py), riding the nocked arrow exactly: no separate ball
			g_ball.Show(false);
			const float breathe = g_draw >= 0.98f ? 1.0f + 0.12f * std::sin(g_spin * 4.0f) : 1.0f;
			g_ball.Place(HeadAt(p), Spellcraft::Spin(g_spin), (0.1f + 0.32f * g_draw) * breathe, 1.0f + 0.6f * g_draw);
			if (g_draw >= 0.98f && !g_fullSaid) {
				g_fullSaid = true;
				Spellcraft::Play2D(Spellcraft::Snd(GunOf(i), &gen::GunSounds::full), 0.8f);
			}
		} else {
			const bool released = st == S::kBowReleasing || st == S::kBowReleased || st == S::kBowNextAttack || st == S::kBowFollowThrough;
			const bool heard = g_releaseHeard.exchange(false);
			// let go: the release state, the release event, or simply the draw ending with the bow still out and some pull on it
			if (g_wasDrawing && i >= 0 && g_sinceFire > 0.2f && (released || heard)) {  // (state 0 after a draw is a cancel: bowReset)
				Fire(i, g_draw);
				g_sinceFire = 0;
				static int said = 0;
				if (said++ < 5) {
					logger::info("bows: {} loosed at {:.0f}% draw (state {}, event {})", gen::kBows[i].label, g_draw * 100.0f, static_cast<int>(st), heard);
				}
			}
			g_draw = 0, g_fullSaid = false;
			HideBall();
		}
		if (drawing && g_releaseHeard.exchange(false) && i >= 0 && g_sinceFire > 0.2f) {  // (the event before the state caught up)
			Fire(i, g_draw);
			g_sinceFire = 0;
			g_draw = 0, g_fullSaid = false;
		}
		g_wasDrawing = drawing;
		TickMotes(p, i, a_delta);
		TickTip(p, i, drawing, a_delta);
		TickTiers(a_delta);
		std::scoped_lock l(Hud::Lock());
		Hud::Edit().bowHeld = i;
		Hud::Edit().bowDraw = drawing ? g_draw : 0.0f;
	}

	void Reset()
	{
		ClearMotes();
		for (auto& o : g_orbs) {
			o.ball.Detach();
		}
		g_orbs.clear(), g_patches.clear(), g_marks.clear(), g_cyclones.clear(), g_tethers.clear(), g_meteors.clear(), g_harvest = 0;
		g_ball.Detach();
		g_tip.Detach();
		g_tipRow = -1;
		g_ballRow = -1, g_draw = 0, g_wasDrawing = false, g_fullSaid = false;
	}
}
