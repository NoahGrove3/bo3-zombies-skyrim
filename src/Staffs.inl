// The wonder weapons: four Falmer staffs (Noah's pack, Elemental Staffs' chitin staves), BO3 Origins-style.
//   Ember Staff  (barrowhorn)  bolts burst into fire and set the dead burning; held: a meteor and a burning pool;
//                              upgraded: a line of fire pillars
//   Rime Staff   (frostbinder) shards slow, the third freezes solid, the frozen shatter; held: a blizzard that freezes;
//                              upgraded: the blizzard hunts the dead
//   Storm Staff  (wolfcrown)   lightning leaps between four (the sheet's chain); held: a storm ball zapping all near it;
//                              upgraded: twice as long, wider
//   Void Staff   (gravecaller) bolts drag the dead together, a kill may rise as a spectral spider; held: a void orb pulls
//                              the crowd in, then crushes it; upgraded: a wider pull, three spiders every time
// Upgraded = forged to the last level at the altar. An upgraded staff's bolt that lands by a fallen friend raises them.
// Included by Gun.cpp inside its anonymous namespace, after WonderProps.

bool Upgraded() { return ForgeLevel() >= gen::kForgeMax; }

struct StaffZone
{
	int          kind;  // 0 burning pool, 1 blizzard, 2 storm ball, 3 void orb, 4 fire pillar
	RE::NiPoint3 at;
	float        r, left, tick, dmg, fx;
	bool         hunt;
	float        crush;  // void orb: seconds until it crushes (then it ends)
};
std::vector<StaffZone> g_zones;
std::unordered_map<RE::FormID, int>   g_chill;        // Rime: shards in a row
std::unordered_map<RE::FormID, float> g_frozenUntil;  // Rime: frozen solid until (game seconds)
float g_staffClock = 0, g_spiderCd = 0;

bool IsFrozenSolid(RE::Actor* a) { auto it = g_frozenUntil.find(a->GetFormID()); return it != g_frozenUntil.end() && it->second > g_staffClock; }
void FreezeSolid(RE::Actor* a, float a_s)
{
	g_frozenUntil[a->GetFormID()] = g_staffClock + a_s;
	Director::Web(a->GetPosition(), 45.0f, a_s, 97.0f, true);
}
void Shatter(RE::Actor* a, float a_dmg)
{
	g_frozenUntil.erase(a->GetFormID());
	Damage(a, a_dmg * 1.5f, false, false, true);
	Blast(BlastIndex("icevolley"), a->GetPosition() + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, 0.4f);
	for (auto* f : Foes(a->GetPosition(), 200.0f)) {
		if (f != a) Damage(f, a_dmg * 0.5f, false, false, true);
	}
}
void PullTo(const RE::NiPoint3& c, float r, float strength)
{
	for (auto* f : Foes(c, r)) {
		RE::NiPoint3 off = f->GetPosition() - c; off.z = 0;
		if (off.Length() > 90.0f) Spellcraft::Fling(f, c, -strength);
	}
}
void MaybeSpider(const RE::NiPoint3& at, bool a_sure)
{
	if (!a_sure && (g_spiderCd > 0 || Rand(0, 1) > 0.35f)) return;
	g_spiderCd = 2.0f;
	Echo::Allies(2, 1, 25.0f, at);
}

void StaffTap(const gen::GunDef& g, const RE::NiPoint3& a_to, RE::Actor* a_hit, float a_dmg)
{
	const std::string_view id = g.id;
	const bool up = Upgraded();
	if (id != "barrowhorn" && id != "frostbinder" && id != "wolfcrown" && id != "gravecaller") return;
	if (up) Party::ReviveAt(a_to, 180.0f);  // (Origins: the upgraded staffs raise the fallen)
	if (id == "barrowhorn") {  // fire: everything the burst touches burns
		for (auto* f : Foes(a_to, 200.0f)) g_dots.push_back({ f->GetHandle(), a_dmg * 0.15f, 3.0f });
	} else if (id == "frostbinder") {
		if (a_hit && !a_hit->IsDead()) {
			if (IsFrozenSolid(a_hit)) {
				Shatter(a_hit, a_dmg);
			} else if (++g_chill[a_hit->GetFormID()] >= 3) {
				g_chill.erase(a_hit->GetFormID());
				FreezeSolid(a_hit, 3.0f);
			} else {
				Director::Web(a_hit->GetPosition(), 50.0f, 2.0f, 55.0f, true);  // slowed
			}
		}
	} else if (id == "gravecaller") {
		PullTo(a_to, 260.0f, 1.2f);
		if (a_hit && a_hit->IsDead()) MaybeSpider(a_hit->GetPosition(), false);
	}
}

bool StaffUnleash(std::string_view a_id, const Aim& a_aim, float a_base)
{
	const bool up = Upgraded();
	auto*      p = RE::PlayerCharacter::GetSingleton();
	if (a_id == "barrowhorn") {
		if (up) {  // pillars of fire down the line
			RE::NiPoint3 dir = a_aim.dir; dir.z = 0; if (dir.Length() < 0.01f) dir = { 0, 1, 0 }; dir.Unitize();
			for (int k = 1; k <= 5; ++k) {
				const RE::NiPoint3 q = p->GetPosition() + dir * (220.0f * k);
				g_zones.push_back({ 4, q, 210.0f, 0.12f * k + 0.05f, 0.12f * k, a_base * 4.0f, 0, false, 0 });
				g_zones.push_back({ 0, q, 180.0f, 5.0f + 0.12f * k, 0.12f * k + 0.3f, a_base * 0.35f, 0, false, 0 });
			}
		} else {  // a meteor where you aim, and the ground burns
			Blast(BlastIndex("skybolt"), a_aim.hit + RE::NiPoint3{ 0, 0, 30.0f }, 0.0f, 0.8f);
			Blast(BlastIndex("firestorm"), a_aim.hit + RE::NiPoint3{ 0, 0, 30.0f }, 0.35f, 1.0f);
			g_zones.push_back({ 4, a_aim.hit, 400.0f, 0.36f, 0.35f, a_base * 6.0f, 0, false, 0 });
			g_zones.push_back({ 0, a_aim.hit, 300.0f, 5.5f, 0.6f, a_base * 0.45f, 0, false, 0 });
		}
		return true;
	}
	if (a_id == "frostbinder") {
		g_zones.push_back({ 1, a_aim.hit, up ? 480.0f : 420.0f, up ? 8.0f : 6.0f, 0.0f, a_base * 0.5f, 0, up, 0 });
		Blast(BlastIndex("icevolley"), a_aim.hit, 0.0f, 0.9f);
		return true;
	}
	if (a_id == "wolfcrown") {  // (Noah: swapped with Stormreach's) Storm Bloom - StormCalling's spell, loosed where you aim
		RE::Actor* target = nullptr;
		for (auto* f : Foes(a_aim.hit, 600.0f)) { target = f; break; }
		const bool cast = BowsImpl::CastAtStrike(RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x000956, "StormCalling.esl"), a_aim.hit, a_aim.dir * 1000.0f, target, false);
		for (auto* f : Foes(a_aim.hit, up ? 520.0f : 400.0f)) {  // our numbers alongside its look
			Damage(f, a_base * (up ? 4.0f : 3.0f), false, false, true);
		}
		Blast(BlastIndex("shockmass"), a_aim.hit + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, 0.8f);
		logger::info("staffs: Storm Bloom{}", cast ? "" : " (StormCalling not found - its blast only)");
		return true;
	}
	if (a_id == "gravecaller") {
		g_zones.push_back({ 3, a_aim.hit + RE::NiPoint3{ 0, 0, 60.0f }, up ? 800.0f : 600.0f, 2.6f, 0.0f, a_base * 8.0f, 0, up, 2.5f });
		Blast(BlastIndex("darkmassive"), a_aim.hit + RE::NiPoint3{ 0, 0, 60.0f }, 0.0f, 0.6f);
		return true;
	}
	return false;
}

// (Stormreach's tier-III, once the Storm Staff's held attack) a storm ball hanging in the air, zapping all near it
void StormBallAt(const RE::NiPoint3& a_at, float a_r, float a_secs, float a_dmg, bool a_hunt)
{
	g_zones.push_back({ 2, a_at, a_r, a_secs, 0.0f, a_dmg, 0, a_hunt, 0 });
}

void TickStaffs(float a_delta)
{
	g_staffClock += a_delta;
	g_spiderCd = std::max(0.0f, g_spiderCd - a_delta);
	if (g_frozenUntil.size() > 64) std::erase_if(g_frozenUntil, [](const auto& e) { return e.second < g_staffClock; });
	if (g_chill.size() > 128) g_chill.clear();
	for (auto& z : g_zones) {
		z.left -= a_delta;
		z.fx -= a_delta;
		const bool tick = (z.tick -= a_delta) <= 0;
		switch (z.kind) {
		case 0:  // burning pool
			if (z.fx <= 0) { z.fx = 0.5f; Blast(BlastIndex("fireball"), z.at + RE::NiPoint3{ Rand(-z.r, z.r) * 0.6f, Rand(-z.r, z.r) * 0.6f, 10.0f }, 0.0f, 0.3f); }
			if (tick) { z.tick = 0.5f; for (auto* f : Foes(z.at, z.r)) { Damage(f, z.dmg, false, false, true); g_dots.push_back({ f->GetHandle(), z.dmg * 0.3f, 2.0f }); } }
			break;
		case 4:  // a fire pillar / the meteor's strike: once
			if (tick && z.tick > -1.0f) {
				z.tick = -100.0f;
				Blast(BlastIndex("firestorm"), z.at + RE::NiPoint3{ 0, 0, 20.0f }, 0.0f, z.r > 300.0f ? 1.0f : 0.6f);
				for (auto* f : Foes(z.at, z.r)) { if (!Damage(f, z.dmg, false, false, true)) Stagger(f, 0.8f); }
				z.left = 0;
			}
			break;
		case 1:  // blizzard
			if (z.hunt) {  // (upgraded) it drifts after the nearest of the dead
				RE::Actor* best = nullptr; float bd = 1500.0f;
				for (auto* f : Foes(z.at, 1500.0f)) if (const float d = f->GetPosition().GetDistance(z.at); d < bd) bd = d, best = f;
				if (best && bd > 80.0f) { RE::NiPoint3 d = best->GetPosition() - z.at; d.z = 0; d.Unitize(); z.at += d * (170.0f * a_delta); }
			}
			if (z.fx <= 0) { z.fx = 0.3f; Blast(BlastIndex("frost"), z.at + RE::NiPoint3{ Rand(-z.r, z.r) * 0.7f, Rand(-z.r, z.r) * 0.7f, 60.0f }, 0.0f, 0.4f); }
			if (tick) {
				z.tick = 0.5f;
				for (auto* f : Foes(z.at, z.r)) {
					if (Damage(f, z.dmg, false, false, true)) continue;
					if (IsFrozenSolid(f)) continue;
					if (++g_chill[f->GetFormID()] >= 3) { g_chill.erase(f->GetFormID()); FreezeSolid(f, 3.0f); }
					else Director::Web(f->GetPosition(), 50.0f, 1.0f, 60.0f, true);
				}
			}
			break;
		case 2:  // storm ball: zaps the three nearest every 0.4 s
			if (z.hunt) {  // (Noah: the tier-III storm ball follows the dead)
				RE::Actor* best = nullptr; float bd = 1800.0f;
				for (auto* f : Foes(z.at, 1800.0f)) if (const float d = f->GetPosition().GetDistance(z.at); d < bd) bd = d, best = f;
				if (best && bd > 60.0f) { RE::NiPoint3 d = best->GetPosition() + RE::NiPoint3{ 0, 0, 110.0f } - z.at; d.Unitize(); z.at += d * (260.0f * a_delta); }
			}
			if (z.fx <= 0) { z.fx = 0.45f; Blast(BlastIndex("runeshock"), z.at, 0.0f, 0.45f); }
			if (tick) {
				z.tick = 0.4f;
				auto foes = Foes(z.at, z.r);
				std::sort(foes.begin(), foes.end(), [&](RE::Actor* a, RE::Actor* b) { return a->GetPosition().GetDistance(z.at) < b->GetPosition().GetDistance(z.at); });
				int n = 0;
				for (auto* f : foes) {
					if (n++ >= 3) break;
					g_arcs.push_back({ z.at, f->GetPosition() + RE::NiPoint3{ 0, 0, 80.0f }, 0.03f * n });
					Cast(gen::kSpell_lightningbolt, f);
					if (!Damage(f, z.dmg, false, false, true)) Stagger(f, 0.3f);
				}
			}
			break;
		case 5:  // Shadow Step's shadow: draws the dead to it, then bursts
		case 6:  // Arcane Singularity: pulls them in, then detonates
			z.crush -= a_delta;
			if (z.fx <= 0) { z.fx = 0.35f; Blast(BlastIndex(z.kind == 5 ? "dark" : "constellation"), z.at + RE::NiPoint3{ 0, 0, 50.0f }, 0.0f, 0.45f); }
			if (tick) { z.tick = 0.3f; PullTo(z.at, z.r, z.kind == 5 ? 0.9f : 1.5f); }
			if (z.crush <= 0) {
				Blast(BlastIndex(z.kind == 5 ? "darkmassive" : "lightmassive"), z.at + RE::NiPoint3{ 0, 0, 50.0f }, 0.0f, 0.9f);
				for (auto* f : Foes(z.at, 340.0f)) { if (!Damage(f, z.dmg, false, false, true)) Stagger(f, 1.0f); }
				z.left = 0;
			}
			break;
		case 3:  // void orb: pulls, then crushes
			z.crush -= a_delta;
			if (z.fx <= 0) { z.fx = 0.3f; Blast(BlastIndex("dark"), z.at, 0.0f, 0.5f); }
			if (tick) { z.tick = 0.3f; PullTo(z.at, z.r, 1.5f); }
			if (z.crush <= 0) {
				Blast(BlastIndex("darkmassive"), z.at, 0.0f, 1.0f);
				int kills = 0;
				RE::NiPoint3 lastAt = z.at;
				for (auto* f : Foes(z.at, 320.0f)) {
					if (Damage(f, z.dmg, false, false, true)) { ++kills; lastAt = f->GetPosition(); }
					else Stagger(f, 1.0f);
				}
				if (z.hunt) Echo::Allies(2, 3, 25.0f, z.at);               // (upgraded) three spiders every time
				else if (kills > 0) Echo::Allies(2, std::min(kills, 2), 25.0f, lastAt);
				z.left = 0;
			}
			break;
		}
	}
	std::erase_if(g_zones, [](const StaffZone& z) { return z.left <= 0; });
}
