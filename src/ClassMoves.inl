// ================================================================ each class's own moves (Noah, Nov 27 workshop: "Own moves, like the Tablet" -
// a Light, a Heavy and a Special that class alone has, the tiers making them stronger; his picks, Nov 27 final):
//   Summoner     Radiant Strike / Pillar of Dawn / Ancestor's Guard          (holy)
//   Destruction  Glintstone Bolt (tap) / Arcane Ray (hold, costs magicka) / Starfall   (arcane)
//   Alchemist    Reaping Arc / Shadowfire Eruption / Philosopher's Edge       (shadowfire)
//   Spellsword   Spellthrust / Spell Pillar / Elemental Wake                  (the spell in the other hand)
// (included in Classes.cpp's unnamed namespace, after the Soulcaller's: it uses its spectral look)

float MovePw(const RE::TESForm* a_w)
{
	const int t = std::clamp(Kit::TierOfWeapon(a_w), 0, 3);
	return UpgradeMult(t) * (0.7f + 0.3f * Arcana::Power()) * AscDmg(AscOf(a_w));
}
float MoveWhole() { return Director::RoundHealth() * gen::kRounds.hpScale; }
RE::NiPoint3 MoveFwd(RE::PlayerCharacter* a_p)
{
	const float z = a_p->GetAngleZ();
	return { std::sin(z), std::cos(z), 0.0f };
}
int g_moveKills[4]{};          // kills toward each class's Special
bool g_philArmed = false;       // (Philosopher's Edge) the next kill transmutes
int g_glintBolts = 0;           // (Starfall) glintstone bolts loosed
std::unordered_map<RE::FormID, int> g_reapStacks;   // (Reaping Arc) how many sweeps have caught each one (its burn stacks)

// ---- the Spellsword's element: the spell in its other hand (0 fire 1 frost 2 shock 3 sun 4 arcane/blood), its burst and its pillar
int SsEl()
{
	switch (SpellswordElement()) {
	case coffer::kFrost: return 1;
	case coffer::kShock: return 2;
	case coffer::kSun: return 3;
	case coffer::kBlood: case coffer::kArcane: return 4;
	default: return 0;
	}
}
int SsBurst(int a_el) { static constexpr int k[5] = { 0, 1, 2, 4, 6 }; return k[std::clamp(a_el, 0, 4)]; }   // (Gun::ElemBurst)
RE::SpellItem* SsSpell()
{
	const int i = Arcana::Hands()[0] >= 0 ? Arcana::Hands()[0] : BladeSpell();
	return i >= 0 ? Arcana::Spell(i) : nullptr;
}
// the element laid on one of the dead: fire burns, frost slows (the spell itself), shock arcs to the next, sun burns, arcane drains
void SsTouch(RE::Actor* a_a, int a_el, float a_pw, bool a_arc)
{
	if (!a_a || a_a->IsDead()) return;
	auto* p = RE::PlayerCharacter::GetSingleton();
	const float whole = MoveWhole();
	switch (a_el) {
	case 0: case 3: Gun::Burn(a_a, whole * 0.04f * a_pw, 3.0f, a_el == 0 ? 0 : -1); break;
	case 1: if (auto* sp = SsSpell(); sp && p) CastFrom(p, sp, a_a); break;
	case 2:
		if (a_arc) {
			for (auto* f : NearFoes(a_a->GetPosition(), 420.0f, 4)) {
				if (f == a_a || f->IsDead()) continue;
				Gun::LightningArc(a_a->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, f->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.0f);
				Gun::AreaHurt(f->GetPosition(), 40.0f, 0.12f * a_pw, "");
				break;
			}
		}
		break;
	default: if (auto* sp = SsSpell(); sp && p) CastFrom(p, sp, a_a); break;
	}
}

// ---- a blow landed with a class weapon (Light moves that work on contact)
void MovesHit(RE::Actor* a_target, RE::TESForm* a_src, bool a_power)
{
	auto* p = RE::PlayerCharacter::GetSingleton();
	if (!p || !a_target || a_target->IsDead() || !a_src) return;
	const int cls = SwordClass(a_src);
	if (cls < 0 || cls == 1) return;
	const float pw = MovePw(a_src);
	const RE::NiPoint3 at = a_target->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f };
	if (cls == 0 && !a_power) {
		// Radiant Strike: each strike flares with sunlight that sears the dead and blinds them for a moment
		Gun::ElemBurst(4, at, 0.35f, 0.0f, 0.5f);
		Gun::Burn(a_target, MoveWhole() * 0.035f * pw, 3.0f, -1);
		Gun::Knock(a_target, 0.25f);
	} else if (cls == 3 && !a_power) {
		// Spellthrust: each thrust carries the spell in your other hand - fire burns, frost slows, shock arcs to one more
		const int el = SsEl();
		Gun::ElemBurst(SsBurst(el), at, 0.3f, 0.0f, 0.4f);
		SsTouch(a_target, el, pw, true);
	}
}

// ---- Specials that count kills
void MovesKill(RE::Actor* a_dying, RE::TESForm* a_src)
{
	auto* p = RE::PlayerCharacter::GetSingleton();
	if (!p || !a_dying || !a_src) return;
	const int cls = SwordClass(a_src);
	if (cls < 0) return;
	const float pw = MovePw(a_src);
	const RE::NiPoint3 at = a_dying->GetPosition();
	g_reapStacks.erase(a_dying->GetFormID());
	if (cls == 2) {
		// Philosopher's Edge: every 10th kill transmutes - the next of the dead you kill bursts into Cursed Essence and shadowfire
		if (g_philArmed) {
			g_philArmed = false;
			Gun::ElemBurst(9, at + RE::NiPoint3{ 0, 0, 60.0f }, 1.1f, 0.0f, 0.9f);
			Gun::BlastSound("fireball", at, 0.8f);
			Gun::AreaHurt(at, 280.0f, 0.5f * pw, "");
			for (auto* f : NearFoes(at, 280.0f, 16)) Shadowburn(f, std::clamp(Kit::TierOfWeapon(a_src), 0, 3));
			const int bonus = static_cast<int>(150.0f * Director::KillEssenceScale());
			Points::Add(bonus);
			Hud::Essence(at);
			logger::info("moves: Philosopher's Edge - a kill transmuted (+{} essence)", bonus);
		} else if (++g_moveKills[2] >= 10) {
			g_moveKills[2] = 0, g_philArmed = true;
			Hud::Hint("Philosopher's Edge: your next kill transmutes", 2.0f);
		}
	} else if (cls == 3) {
		// Elemental Wake: a kill leaves a patch of your element on the ground that burns, freezes or shocks the dead that cross it
		const int el = SsEl();
		static constexpr int kPool[5] = { 0, 1, 2, 4, 5 };   // (Gun::Puddle: fire frost shock acid sun blood)
		Gun::Puddle(kPool[el], at, 0.8f, 4.0f);
		for (int i = 1; i <= 4; ++i) {
			Tasks::Later(static_cast<float>(i), [at, el, pw] {
				Gun::AreaHurt(at, 170.0f, 0.06f * pw, "");
				for (auto* f : NearFoes(at, 170.0f, 8)) SsTouch(f, el, pw * 0.5f, false);
			});
		}
	} else if (cls == 0) {
		// Ancestor's Guard: every 20 kills a spectral warrior rises out of a pillar of light where the 20th fell and fights
		// beside you for 12 s (the fallen one's own body, a ghost of light)
		if (++g_moveKills[0] >= 20) {
			g_moveKills[0] = 0;
			auto* base = a_dying->GetActorBase();
			if (base && base->IsDynamicForm()) {
				base = nullptr;
				if (auto* lc = a_dying->extraList.GetByType<RE::ExtraLeveledCreature>()) {
					for (auto* b : { lc->originalBase, lc->templateBase }) if (auto* n = b ? b->As<RE::TESNPC>() : nullptr; n && !n->IsDynamicForm()) { base = n; break; }
				}
			}
			if (!base) return;
			Gun::SkyBeam(at, 1.4f, true);
			const float hp = MoveWhole() * 2.5f * pw;
			Tasks::Later(0.55f, [base, at, hp, h = a_dying->GetHandle()] {
				auto* pc = RE::PlayerCharacter::GetSingleton();
				if (!pc || pc->IsDead()) return;
				if (auto* a = RaiseAlly(pc, base, at, 12.0f, hp, 2)) {
					SoulLook(a, 12.0f);
					a->SetAlpha(0.55f);
					if (auto d = h.get()) d->Disable();   // (the body it rose from is gone)
					logger::info("moves: Ancestor's Guard - a spectral warrior rises for 12 s");
				}
			});
		}
	}
}

// ---- a swing (hit or miss): the Alchemist's sweep, and each class's Heavy
void MovesSwing(int a_cls, bool a_power, RE::TESForm* a_w)
{
	auto* p = RE::PlayerCharacter::GetSingleton();
	if (!p || !a_w) return;
	const int t = std::clamp(Kit::TierOfWeapon(a_w), 0, 3);
	const float pw = MovePw(a_w);
	const RE::NiPoint3 me = p->GetPosition(), fwd = MoveFwd(p);
	if (a_cls == 2 && !a_power) {
		// Reaping Arc: a wide sweep that cuts every one of the dead in front of you and sets them smouldering in shadowfire; the
		// burn stacks with each swing (up to three)
		Gun::SlashSpec sl;
		sl.model = ArcModel(0);
		sl.from = Gun::SwingFrom(), sl.dir = Gun::SwingDir(), sl.roll = Gun::SwingRoll(sl.dir);
		sl.sideways = true, sl.range = 380.0f, sl.radius = 170.0f, sl.speed = 1300.0f;
		sl.scale = (1.0f + 0.12f * t) * AscSize(AscOf(a_w));
		sl.share = BladeShare(a_w->As<RE::TESObjectWEAP>()) * 0.5f * (0.7f + 0.3f * Arcana::Power());
		sl.onHit = [t, pw](RE::Actor* a, bool killed) {
			if (!a) return;
			if (killed) { CorpseBurn(a); Gun::ElemBurst(9, a->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, 0.5f, 0.0f, 0.5f); return; }
			Shadowburn(a, t);
			const int n = std::min(3, ++g_reapStacks[a->GetFormID()]);
			if (n > 1) Gun::Burn(a, MoveWhole() * 0.03f * static_cast<float>(n - 1) * pw, 3.0f, -1);
		};
		Gun::Slash(sl);
		if (g_reapStacks.size() > 300) g_reapStacks.clear();
		return;
	}
	if (!a_power) return;
	if (a_cls == 2) {
		// Shadowfire Eruption: the blade driven into the ground - geysers of shadowfire burst up in a ring round you
		const float r = 230.0f * AscSize(AscOf(a_w));
		Gun::BlastSound("fireball", me, 0.9f);
		for (int i = 0; i < 8; ++i) {
			const float an = static_cast<float>(i) * 0.7854f;
			Gun::ElemBurst(9, me + RE::NiPoint3{ std::cos(an) * r, std::sin(an) * r, 30.0f }, 0.75f + 0.1f * t, 0.04f * static_cast<float>(i), 0.8f);
		}
		Gun::Ring("purple", me, 40.0f, r + 120.0f, 0.6f);
		Tasks::Later(0.25f, [me, r, pw, t] {
			Gun::AreaHurt(me, r + 120.0f, 0.45f * pw, "");
			for (auto* f : NearFoes(me, r + 120.0f, 20)) { Shadowburn(f, t); Gun::Knock(f, 0.6f); }
		});
		return;
	}
	if (a_cls == 3) {
		// Spell Pillar: a pillar of your element pours down on the one you strike (the nearest just ahead)
		const int el = SsEl();
		RE::NiPoint3 at = me + fwd * 170.0f;
		float best = 1e9f;
		for (auto* f : NearFoes(me + fwd * 150.0f, 280.0f, 12)) {
			const float d = f->GetPosition().GetDistance(me);
			if (d < best) best = d, at = f->GetPosition();
		}
		static constexpr const char* kPillar[5] = { "fire", "frost", "shock", "sun", "arcane" };
		Gun::QueueFx(std::string("CurseOfAlduin\\fx\\pillar_") + kPillar[el] + ".nif", at, 1.0f + 0.15f * t, 0.0f, 2.0f, false);
		if (el == 2) for (int i = 0; i < 3; ++i) Gun::LightningArc(at + RE::NiPoint3{ 0, 0, 1400.0f }, at + RE::NiPoint3{ 0, 0, 20.0f }, 0.12f * static_cast<float>(i));
		static constexpr const char* kSnd[5] = { "fireball", "frost", "runeshock", "lightmassive", "shockmass" };
		Gun::BlastSound(kSnd[el], at, 0.9f);
		const float r = 260.0f + 40.0f * t;
		Tasks::Later(0.4f, [at, r, el, pw] {
			Gun::AreaHurt(at, r, 0.5f * pw, "");
			for (auto* f : NearFoes(at, r, 16)) { SsTouch(f, el, pw, false); Gun::Knock(f, 0.5f); }
		});
		return;
	}
	if (a_cls == 0) {
		// Pillar of Dawn: both heads driven down - a pillar of sunlight pours onto the spot (the staff's own sun pillar)
		const int key = LookKey(0);
		ClassCyclone(0, key, t, pw, nullptr, me, Gun::SwingDir());
	}
}

// ---- Destruction's wand: a tap looses a glintstone bolt (0.5 s between), a hold channels the Arcane Ray (it costs magicka),
// and every 25th bolt Starfall rains twelve great bolts in front of you
void Starfall(RE::PlayerCharacter* a_p, float a_pw)
{
	const RE::NiPoint3 c = a_p->GetPosition() + MoveFwd(a_p) * 650.0f;
	static std::mt19937 rng{ std::random_device{}() };
	std::uniform_real_distribution<float> u(0.0f, 1.0f);
	Hud::Hint("Starfall", 1.5f);
	for (int i = 0; i < 12; ++i) {
		const float an = u(rng) * 6.2832f, rr = std::sqrt(u(rng)) * 320.0f;
		const RE::NiPoint3 at = c + RE::NiPoint3{ std::cos(an) * rr, std::sin(an) * rr, 0.0f };
		Tasks::Later(0.12f * static_cast<float>(i), [at, a_pw] {
			Gun::Lob(ArcModel(1), at + RE::NiPoint3{ 120.0f, -160.0f, 1600.0f }, RE::NiPoint3{ -300.0f, 400.0f, -3600.0f }, 2.2f, [a_pw](const RE::NiPoint3& hit) {
				Gun::ElemBurst(6, hit + RE::NiPoint3{ 0, 0, 30.0f }, 0.6f, 0.0f, 0.6f);
				Gun::BlastSound("shockmass", hit, 0.5f);
				Gun::AreaHurt(hit, 190.0f, 0.35f * a_pw, "");
				for (auto* f : NearFoes(hit, 190.0f, 8)) Gun::Knock(f, 0.4f);
			}, 0.0f, 0.0f);
		});
	}
}
void GlintBolt(RE::PlayerCharacter* a_p, RE::TESObjectWEAP* a_w)
{
	const float pw = MovePw(a_w);
	const RE::NiPoint3 from = WandTip(a_p);
	RE::NiPoint3 dir = Gun::SwingDir();
	// it bends a little toward the nearest of the dead near your aim (within about 20 degrees)
	{
		float best = 0.94f;
		for (auto* f : NearFoes(from + dir * 900.0f, 900.0f, 16)) {
			RE::NiPoint3 to = f->GetPosition() + RE::NiPoint3{ 0, 0, 80.0f } - from;
			const float len = to.Length();
			if (len < 1.0f || len > 2200.0f) continue;
			to /= len;
			const float d = to.Dot(dir);
			if (d > best) best = d, dir = to;
		}
	}
	Gun::SlashSpec sl;
	sl.model = ArcModel(1);
	sl.from = from, sl.dir = dir, sl.roll = Gun::SwingRoll(dir);
	sl.range = 1800.0f, sl.bolt = true, sl.speed = 1500.0f, sl.keepFrom = true, sl.stopOnHit = true;
	sl.scale = 1.1f + 0.12f * std::clamp(Kit::TierOfWeapon(a_w), 0, 3);
	sl.share = BladeShare(a_w) * (0.7f + 0.3f * Arcana::Power()) * 1.2f;
	sl.flingKill = 14.0f, sl.shatter = 0.5f;
	Gun::Slash(sl);
	WandKick(0);
	if (auto* snd = ReleaseSound(0x0002DD2A)) Audio::Game(snd->GetFormID(), a_p, 0.8f);
	if (++g_glintBolts >= 25) { g_glintBolts = 0; Starfall(a_p, pw); }
}
struct RayState { bool on = false; float t = 0, hurt = 0; };
RayState g_ray;
void RayStop()
{
	if (g_ray.on) Gun::GauntletBeamOff();
	g_ray = {};
}
// one frame of the held beam: wider and hotter the longer it's held; it drains magicka (and stops when there's none left)
void RayTick(RE::PlayerCharacter* a_p, RE::TESObjectWEAP* a_w, float a_dt)
{
	auto* av = a_p->AsActorValueOwner();
	const float grow = std::min(1.0f, g_ray.t / 2.0f);
	const float cost = (14.0f + 16.0f * grow) * a_dt;
	if (av->GetActorValue(RE::ActorValue::kMagicka) < cost + 0.5f) {
		RayStop();
		static auto told = std::chrono::steady_clock::now() - std::chrono::seconds(10);
		if (std::chrono::steady_clock::now() - told > std::chrono::seconds(3)) { told = std::chrono::steady_clock::now(); Hud::Hint("Out of magicka", 1.5f); }
		return;
	}
	av->DamageActorValue(RE::ActorValue::kMagicka, cost);
	if (!g_ray.on) { g_ray.on = true; Gun::BlastSound("shockmass", a_p->GetPosition(), 0.5f); }
	g_ray.t += a_dt;
	const RE::NiPoint3 from = WandTip(a_p);
	RE::NiPoint3 to;
	Gun::StaffBeam(5, 0.55f + 0.85f * grow, from, to);
	if ((g_ray.hurt -= a_dt) <= 0.0f) {
		g_ray.hurt = 0.2f;
		const float pw = MovePw(a_w);
		for (auto* f : Gun::LineHurt(from, to, 50.0f + 40.0f * grow, (0.05f + 0.06f * grow) * pw)) Gun::Knock(f, 0.15f);
		Gun::ElemBurst(6, to, 0.25f + 0.25f * grow, 0.0f, 0.3f);
	}
}

// ---- the Mutagen Staff's strike (Noah, Nov 27 final: "Mutating strike" - "make it cycle through the flask effects like the
// block does"): each strike marks the one struck with a mutation that bursts a second later as the next flask's element in turn
// (fire, frost, shock, acid, holy water, voidglass)
void MutagenStrike(RE::Actor* a_target, RE::TESForm* a_src, bool a_power)
{
	if (a_power || !a_target || a_target->IsDead() || !a_src || a_src != FlaskItem(6)) return;
	static int turn = 0;
	const int el = turn++ % 6;
	Gun::Motes(el == 3 ? 7 : el == 4 ? 4 : el, a_target->GetPosition(), 0.3f, 1.0f);
	Tasks::Later(1.0f, [h = a_target->GetHandle(), el] {
		auto a = h.get();
		if (!a) return;
		const RE::NiPoint3 at = a->GetPosition();
		const float share = 0.35f * UpgradeMult(kWonderUps) * (0.7f + 0.3f * Arcana::Power());
		FlaskBlast(el, kWonderUps, at, 0.8f, share, 200.0f);   // (its burst, ring, sound and blow)
	});
}
