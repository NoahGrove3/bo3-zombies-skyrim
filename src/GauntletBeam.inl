	// ---- (Noah: the Mutagen Gauntlet "shoot a beam coming from the palm/glass ball while holding attack ... cycle through all
	// six left to right visibly showing the vials on the wrist drain going through the tubes to the glass ball in the palm, each
	// cycle should take 5 seconds for the vial to drain then to the next vial ... a charge up animation like a hand gesture
	// then a push ... 5 seconds for each vial 30 second total cast time before having to recharge the vials which should be a
	// 5 second cooldown animation with the vials refilling"; "if the attack is stopped before all vials are empty it should
	// recharge the used vials during the cooldown in between attacking") held: the gesture (0.66 s), then the beam from the
	// palm's orb - fire, frost, shock, acid, holy, void, each vial pouring for 5 s - piercing every one of the dead along it,
	// each element's touch on them; let go and the emptied vials fill again (the whole six in 5 s); all six emptied, it can't
	// be used again until they're full. Included by Classes.cpp (its anonymous namespace, after the flasks' helpers).
	struct GauntletBeamState
	{
		float fill[6]{ 1, 1, 1, 1, 1, 1 };
		int   phase = 0;          // 0 idle (refilling), 1 the gesture, 2 the beam
		float t = 0, since = 99, tick = 0, fxTick = 0, burst = 0;
		bool  held = false, left = false, locked = false;
		int   lastEl = -1;
	};
	GauntletBeamState g_gb;
	std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_gbSlow;   // (frost: slowed until)
	constexpr float kGbGesture = 0.66f, kGbVial = 5.0f, kGbRefill = 5.0f, kGbWidth = 0.45f;
	constexpr std::uint64_t kGbLoop = 0x6A07B0A1ull;
	int GbFirst() { for (int e = 0; e < 6; ++e) if (g_gb.fill[e] > 0.001f) return e; return -1; }
	void GauntletPress(bool a_left)
	{
		if (g_gb.locked) { Hud::QuestBanner("The Mutagen Gauntlet's vials are refilling", 1.5f); return; }
		g_gb.held = true, g_gb.left = a_left;
	}
	void GbStop()
	{
		if (g_gb.phase == 2) { g_gb.since = 0; Audio::LoopStop(kGbLoop, 250); Gun::GauntletBeamOff(); }
		g_gb.phase = 0, g_gb.t = 0, g_gb.lastEl = -1;
	}
	void GbStrike(int e, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_dt)
	{
		const bool  legend = AfterSovngarde();
		const float whole = Director::RoundHealth() * gen::kRounds.hpScale;
		// (its strength: about two thrown flasks' worth a second on everything in it; Holy Water's light a quarter more)
		const float dps = 1.8f * FlaskShare(legend ? 3 : 1) * (e == 4 ? 1.25f : 1.0f);
		const auto hit = Gun::LineHurt(a_from, a_to, 45.0f, dps * a_dt);
		if ((g_gb.fxTick -= a_dt) > 0) return;
		g_gb.fxTick = 0.6f;
		const auto now = std::chrono::steady_clock::now();
		int arcs = 0;
		for (auto* f : hit) {
			if (!f || f->IsDead()) continue;
			switch (e) {
			case 0: Gun::Burn(f, whole * 0.05f, 1.2f); break;                      // fire: set burning
			case 1: {                                                               // frost: slowed to half, 2 s
				auto it = g_gbSlow.find(f->GetFormID());
				if (it == g_gbSlow.end() || it->second < now) {
					f->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -50.0f);
					Tasks::Later(2.0f, [h = f->GetHandle()]() { if (auto a = h.get()) a->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 50.0f); });
					g_gbSlow[f->GetFormID()] = now + std::chrono::milliseconds(2000);
				}
				break;
			}
			case 2:                                                                 // shock: lightning leaps to two more
				if (arcs < 3) {
					++arcs;
					int n = 0;
					for (auto* o : NearFoes(f->GetPosition(), 450.0f, 6)) {
						if (o == f || o->IsDead() || std::find(hit.begin(), hit.end(), o) != hit.end() || n >= 2) continue;
						Gun::LightningArc(f->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, o->GetPosition() + RE::NiPoint3{ 0, 0, 70.0f }, 0.05f * static_cast<float>(n++));
						Gun::AreaHurt(o->GetPosition(), 80.0f, 0.3f * dps, "");
					}
				}
				break;
			case 3: Gun::Burn(f, whole * 0.06f, 1.8f); break;                      // acid: eats at them
			case 5: Gun::Knock(f, 0.6f); break;                                      // void: staggered where they stand
			default: break;                                                          // holy: its light burns harder (above)
			}
		}
		if (g_gbSlow.size() > 300) g_gbSlow.clear();
	}
	void TickGauntletBeam(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		const int hand = FlaskHeld(false) == 6 ? 0 : (FlaskHeld(true) == 6 ? 1 : -1);
		auto* ui = RE::UI::GetSingleton();
		const bool ok = p && hand >= 0 && FlaskInHand(hand == 1) && !p->IsDead() && !ItemMenusOpen() && g_class.load() == coffer::kAlchemist &&
		                !(ui && ui->GameIsPaused());
		if (hand >= 0) g_gb.left = hand == 1;
		if (!ok) g_gb.held = false;
		switch (g_gb.phase) {
		case 0:
			if (g_gb.held && !g_gb.locked && GbFirst() >= 0) {
				g_gb.phase = 1, g_gb.t = 0;
				Audio::At(gen::Sound::charge_up, p, 0.8f);
			}
			break;
		case 1:
			g_gb.t += a_delta;
			if (!g_gb.held) GbStop();
			else if (g_gb.t >= kGbGesture) {
				g_gb.phase = 2, g_gb.t = 0, g_gb.tick = 0, g_gb.fxTick = 0, g_gb.burst = 0;
				Audio::At(gen::Sound::release_power, p, 1.0f);
			}
			break;
		case 2: {
			g_gb.t += a_delta;
			if (!g_gb.held) { GbStop(); break; }
			const int e = GbFirst();
			if (e < 0) {
				GbStop();
				g_gb.locked = true;
				Hud::QuestBanner("The Mutagen Gauntlet's vials are spent - refilling", 2.0f);
				break;
			}
			g_gb.fill[e] = std::max(0.0f, g_gb.fill[e] - a_delta / kGbVial);
			RE::NiPoint3 from, to;
			if (Gun::GauntletBeam(e, kGbWidth, from, to)) {
				if (e != g_gb.lastEl) {
					static constexpr const char* kSnd[6] = { "fireball", "frost", "runeshock", "poisongas", "light", "lightmassive" };
					Gun::BlastSound(kSnd[e], from, 0.7f);
					g_gb.lastEl = e;
					logger::info("alchemist: the Mutagen Gauntlet's beam pours its {} vial", e);
				}
				Audio::LoopAt(kGbLoop, gen::Sound::rite_hum, from, 0.9f);
				if ((g_gb.tick -= a_delta) <= 0) {
					g_gb.tick = 0.2f;
					GbStrike(e, from, to, 0.2f);
				}
				if ((g_gb.burst -= a_delta) <= 0) {   // (where it strikes: the element bursting, again and again)
					g_gb.burst = 0.45f;
					Gun::ElemBurst(kFlaskBurst[e], to, 0.7f, 0.0f, 0.6f);
				}
			}
			break;
		}
		default: break;
		}
		// the vials filling again whenever it isn't pouring: the emptied ones, in order, the whole six in 5 s
		if (g_gb.phase == 0) {
			g_gb.since += a_delta;
			// (Noah: "slow and very smooth so it looks like liquid") every emptied vial filling at once, slowly - the whole six
			// over the 5 s - easing in as it starts
			if (g_gb.since > 0.4f) {
				const float ramp = std::min(1.0f, (g_gb.since - 0.4f) / 0.6f);
				for (float& f : g_gb.fill) f = std::min(1.0f, f + a_delta * ramp / kGbRefill);
			}
			if (g_gb.locked && std::all_of(std::begin(g_gb.fill), std::end(g_gb.fill), [](float f) { return f >= 1.0f; })) {
				g_gb.locked = false;
				Hud::QuestBanner("The Mutagen Gauntlet is ready", 1.5f);
			}
		}
		Gun::GauntletState st;
		for (int e = 0; e < 6; ++e) st.fill[e] = g_gb.fill[e];
		st.phase = g_gb.phase, st.t = g_gb.t, st.left = g_gb.left;
		st.active = g_gb.phase >= 1 ? GbFirst() : -1;
		Gun::SetGauntlet(st);
	}
