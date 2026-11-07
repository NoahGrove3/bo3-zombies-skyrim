	// ---- (Noah: the Horn of the Violet Wyrm - "Held in hand"; "have my character actually blow the horn with a horn sound
	// then the dragon flies in and burns enemies within 30 meters of the player for 90s at a time"; "60 seconds" before it can
	// be blown again; the wyrm "circles and strafes, hovering to breathe on big crowds"; Legendary: "Two dragons") the horn
	// in your hand (tools/wyrmhorn.py): attack lifts it to your lips (Gun's pose, from g_wh.blow) and blows; the wyrm
	// flies in from afar; 90 s on, it flies off and the horn rests 60 s. Included by Classes.cpp (its anonymous namespace).
	struct WyrmHornState
	{
		float blow = -1.0f;      // (time into the blow; -1 none)
		bool  left = false;
		bool  sounded = false, called = false;
		float cool = 0.0f;       // (the rest after the wyrm departs)
		bool  aloft = false;     // (a wyrm called and not yet gone)
		float check = 0.0f;
	};
	WyrmHornState g_wh;
	constexpr float kWhSound = 0.55f, kWhCall = 1.35f, kWhEnd = 2.1f, kWhStay = 90.0f, kWhRest = 60.0f;
	// the wyrm's body, set in game (dev: wyrmbase <plugin> <hex>) until Noah picks one from the recording
	// (Noah: "I want to use a dragon from the [Diverse Dragons] mod, use a purple one" - "Aether + violet glow") the Aether
	// Dragon, the collection's nearest to violet, wrapped in a violet glow (CallWyrm)
	std::string g_wyrmPlugin = "DiverseDragonsCollectionSE.esp";
	RE::FormID  g_wyrmBase = 0x0569A4;
	std::array<RE::ActorHandle, 2> g_wyrms{};
	std::array<float, 2>           g_wyrmGlow{};   // (seconds to its glow's renewal)

	// the horn's call: one of the installed sounds (dev: wyrmsound <plugin> <hex>) until Noah picks one from the recording
	std::string g_hornSndPlugin;
	RE::FormID  g_hornSnd = 0;
	void HornSound(RE::Actor* a_at)
	{
		if (auto* f = g_hornSnd ? Lookup(g_hornSnd, g_hornSndPlugin) : nullptr) Audio::Game(f->GetFormID(), a_at, 1.0f);
		else Audio::At(gen::Sound::shout_learned, a_at, 1.0f);
	}
	RE::TESObjectWEAP* Horn()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		return dh ? dh->LookupForm<RE::TESObjectWEAP>(gen::kWyrmHorn, gen::kPlugin) : nullptr;
	}
	bool HornHeldIn(bool a_left)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		auto* h = Horn();
		auto* o = p && h ? p->GetEquippedObject(a_left) : nullptr;
		return o && o == h;
	}
	bool WyrmAlive(const RE::ActorHandle& h)
	{
		auto a = h.get();
		return a && !a->IsDead() && !a->IsDisabled();
	}

	// one wyrm: the chosen dragon, small, on your side, placed far off and high behind you so it flies in
	void CallWyrm(int a_idx)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		RE::TESForm* base = Lookup(g_wyrmBase, g_wyrmPlugin);
		if (!base || !base->As<RE::TESNPC>()) base = Lookup(0x0569A4, "DiverseDragonsCollectionSE.esp");
		if (!base || !base->As<RE::TESNPC>()) base = Lookup(0x00C71A, "Dawnguard.esm");
		auto* npc = base ? base->As<RE::TESNPC>() : nullptr;
		if (!p || !npc || !p->GetParentCell()) { logger::warn("horn: no wyrm to call (npc {})", npc != nullptr); return; }
		if (auto old = g_wyrms[a_idx].get(); old && !old->IsDead()) {
			Console(old.get(), "SetPlayerTeammate 0");
			old->Disable(), old->SetDelete(true);
		}
		auto ref = p->PlaceObjectAtMe(npc, false);
		auto* a = ref ? ref->As<RE::Actor>() : nullptr;
		if (!a) { logger::warn("horn: the wyrm not placed"); return; }
		const float yaw = p->GetAngleZ() + 3.14159f + (a_idx ? 0.7f : -0.4f);   // (behind you, a way off, high)
		const RE::NiPoint3 at = p->GetPosition() + RE::NiPoint3{ std::sin(yaw) * 2600.0f, std::cos(yaw) * 2600.0f, 1300.0f };
		a->SetPosition(at, true);
		if (auto* fac = Director::UndeadFactionForm()) a->RemoveFromFaction(fac);
		if (auto* pf = PlayerFaction()) a->AddToFaction(pf, 0);
		Console(a, "SetPlayerTeammate 1");
		a->SetDisplayName(a_idx ? "Violet Wyrmling" : "Violet Wyrm", true);
		a->SetActivationBlocked(true);
		auto* av = a->AsActorValueOwner();
		const float hp = Director::RoundHealth() * gen::kRounds.hpScale * (a_idx ? 18.0f : 30.0f);
		av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth, std::max(0.0f, hp - av->GetPermanentActorValue(RE::ActorValue::kHealth)));
		a->SetScale(a_idx ? 0.18f : 0.25f);
		av->SetActorValue(RE::ActorValue::kSpeedMult, 22.0f);   // (Noah: "the movement speed should be a lot slower so it doesn't get stuck floating in the air")
		av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.1f);
		av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.1f);
		g_wyrms[a_idx] = a->GetHandle();
		g_wyrmGlow[a_idx] = 0.0f;   // (its violet glow put on once it is drawn: TickHorn)
		g_ownSummons.push_back({ a->GetHandle(), kWhStay });
		logger::info("horn: the {} called ({} {:06X})", a_idx ? "wyrmling" : "wyrm", g_wyrmPlugin, g_wyrmBase);
	}

	void HornPress(bool a_left)
	{
		if (g_class.load() != coffer::kSummoner) { Hud::QuestBanner("The Horn of the Violet Wyrm answers only to a Summoner", 2.0f); return; }
		if (g_wh.blow >= 0.0f) return;
		if (g_wh.aloft) { Hud::QuestBanner("The Violet Wyrm is already aloft", 1.5f); return; }
		if (g_wh.cool > 0.0f) { Hud::QuestBanner(fmt::format("The horn rests ({:.0f} s)", std::ceil(g_wh.cool)), 1.5f); return; }
		g_wh.blow = 0.0f, g_wh.left = a_left, g_wh.sounded = g_wh.called = false;
	}

	void TickHorn(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		// the blow: the horn to your lips, its call, the wyrm(s) called, the arm down
		if (g_wh.blow >= 0.0f) {
			g_wh.blow += a_delta;
			if (!HornHeldIn(g_wh.left) || p->IsDead()) g_wh.blow = -1.0f;
			else {
				if (!g_wh.sounded && g_wh.blow >= kWhSound) {
					g_wh.sounded = true;
					HornSound(p);
				}
				if (!g_wh.called && g_wh.blow >= kWhCall) {
					g_wh.called = true;
					CallWyrm(0);
					if (AfterSovngarde()) CallWyrm(1);
					g_wh.aloft = true;
					Hud::Banner("THE VIOLET WYRM ANSWERS", 3.0f);
				}
				if (g_wh.blow >= kWhEnd) g_wh.blow = -1.0f;
			}
		}
		Gun::SetHornBlow(g_wh.left, g_wh.blow);
		// the wyrms' violet glow (the void bow's membrane), renewed while they fly
		for (int i = 0; i < 2; ++i) {
			auto w = g_wyrms[i].get();
			if (!w || w->IsDead() || !w->Is3DLoaded()) continue;
			if ((g_wyrmGlow[i] -= a_delta) <= 0.0f) {
				g_wyrmGlow[i] = 25.0f;
				if (auto* fx = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(gen::kBowSkin[6], gen::kPlugin)) w->ApplyEffectShader(fx, 30.0f, nullptr, false, false, nullptr, false);
			}
		}
		// gone (its 90 s up, or slain): the horn rests a minute
		if (g_wh.aloft && !WyrmAlive(g_wyrms[0]) && !WyrmAlive(g_wyrms[1]) && g_wh.called && g_wh.blow < 0.0f) {
			g_wh.aloft = false;
			g_wh.cool = kWhRest;
			Hud::QuestBanner("The Violet Wyrm departs", 2.0f);
		}
		if (g_wh.cool > 0.0f && (g_wh.cool -= a_delta) <= 0.0f) {
			g_wh.cool = 0.0f;
			Hud::QuestBanner("The Horn of the Violet Wyrm can be blown again", 2.0f);
		}
		// (old saves: the horn was a spell - it becomes the horn in your pack)
		if ((g_wh.check -= a_delta) <= 0.0f) {
			g_wh.check = 3.0f;
			if (auto* sp = WyrmSpell(); sp && p->HasSpell(sp)) {
				p->RemoveSpell(sp);
				if (auto* h = Horn(); h && p->GetItemCount(h) <= 0) GiveItem(p, h, 1, true);
				logger::info("horn: the old spell made the horn");
			}
		}
	}
