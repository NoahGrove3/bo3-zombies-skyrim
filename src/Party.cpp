#include "BO3Z.h"

// Co-op life and death (only while in a party with friends; alone, Skyrim's own death and reload stand):
//   down     at zero health you fall (Skyrim's bleedout) instead of dying, for 30 seconds
//   revive   a teammate who stands by you and holds Activate (E / pad A) for 3 seconds brings you back up
//   dead     if nobody does, death takes your blessings and you watch the living (Left / Right arrows, or the
//            mouse buttons, switch who) until a shrine's rite is completed or dawn comes; then you rise again
//   wiped    if every player in the party is dead at once, everyone's game loads its most recent save
// Every player tells the others where they stand four times a second (Net carries it, kPlayer); a revive is a
// message to the fallen (kRevive). The game thread runs this; Net (the render thread) moves the messages.
namespace bo3z::Party
{
	// (the audit: the roll's and the fall's "ghost" fought - one's end undid the other's) one count of reasons; the console
	// is told only when the first reason comes and the last one goes, and a load starts it again from none
	namespace
	{
		std::uint32_t g_ghostWhy = 0;
	}
	void Ghost(std::uint32_t a_why, bool a_on)
	{
		const std::uint32_t was = g_ghostWhy;
		g_ghostWhy = a_on ? (g_ghostWhy | a_why) : (g_ghostWhy & ~a_why);
		if ((was == 0) != (g_ghostWhy == 0)) Gun::Console(g_ghostWhy ? "setghost 1" : "setghost 0");
	}

	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr float kDownSeconds = 30.0f, kReviveSeconds = 3.0f, kReviveReach = 170.0f;

		struct Remote
		{
			std::uint8_t     state{ 0 };  // Life
			RE::NiPoint3     at{};
			float            left{ 0 };
			Clock::time_point heard{};
			RE::ActorHandle  puppet;      // Skyrim Together's figure of that player in our world
			std::string      name;
			std::int8_t      wonder{ -1 };  // the class wonders they have won (bits, class order; -1 not heard)
			std::int8_t      cls{ -1 };     // their class (-1 none chosen)
			std::uint32_t    credit{ 0 };   // their own trials per city (3 bits a city)
		};

		std::mutex                                     g_lock;
		std::unordered_map<std::uint64_t, Remote>      g_remote;   // by Steam id
		std::vector<std::uint64_t>                     g_reviveIn;  // revives addressed to us (Steam ids of the helpers)
		std::vector<std::uint8_t>                      g_outRevive;  // a revive to send (target id, 8 bytes)
		Life                                           g_life = Life::Alive;
		float                                          g_left = 0;     // seconds left while down
		std::atomic<int>                               g_wonder{ 0 };  // the class wonders we have won (bits)
		RE::NiPoint3                                   g_at{};
		float                                          g_reviveT = 0;  // our hold on a fallen friend
		std::uint64_t                                  g_reviveWho = 0;
		bool                                           g_activateHeld = false;
		int                                            g_watch = 0;  // which living friend we watch
		int                                            g_watchStep = 0;
		std::string                                    g_watchName;
		int                                            g_riteSum = -1;
		bool                                           g_wasFighting = false;
		bool                                           g_essentialSet = false;
		std::atomic<bool>                              g_wipeLoading{ false };
		std::vector<View::Mark>                        g_marks;  // friends on screen (game thread, for the HUD)

		bool Active() { return Net::InLobby() && Net::Members() > 0; }

		RE::PlayerCharacter* P() { return RE::PlayerCharacter::GetSingleton(); }

		void SetEssential(bool a_on)
		{
			auto* p = P();
			if (!p) {
				return;
			}
			auto& f = p->GetActorRuntimeData().boolFlags;
			if (a_on == f.all(RE::Actor::BOOL_FLAGS::kEssential) && a_on == g_essentialSet) {
				return;  // (as it should be - read off the body each time: Helgen's own hold sets and clears it too)
			}
			if (a_on) {
				f.set(RE::Actor::BOOL_FLAGS::kEssential);
			} else {
				f.reset(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery);
			}
			if (a_on != g_essentialSet) logger::info("party: you {} fall instead of dying", a_on ? "now" : "no longer");  // (only on a change: something clears the flag every frame and it is set back)
			g_essentialSet = a_on;
		}

		// Skyrim Together's figure of a friend: the living actor nearest to where that friend says they stand
		RE::Actor* Puppet(Remote& r)
		{
			if (auto a = r.puppet.get(); a && !a->IsDisabled() && a->GetPosition().GetDistance(r.at) < 400.0f) {
				return a.get();
			}
			RE::Actor* best = nullptr;
			float      bestD = 400.0f;
			if (auto* pl = RE::ProcessLists::GetSingleton()) {
				for (auto& h : pl->highActorHandles) {
					auto a = h.get();
					if (!a || a.get() == P() || a->IsDead() || Director::IsRoundUndead(a.get())) {
						continue;
					}
					const float d = a->GetPosition().GetDistance(r.at);
					if (d < bestD) {
						bestD = d, best = a.get();
					}
				}
			}
			r.puppet = best ? best->GetHandle() : RE::ActorHandle{};
			return best;
		}

		void Camera(RE::Actor* a_on)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam) {
				return;
			}
			if (a_on && a_on != P()) {
				cam->cameraTarget = a_on->GetHandle();
				cam->ForceThirdPerson();
			} else {
				cam->cameraTarget = P()->GetHandle();
				cam->ForceFirstPerson();
			}
		}

		void HoldDown()
		{
			auto* p = P();
			auto& f = p->GetActorRuntimeData().boolFlags;
			f.set(RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery);  // only a friend (or the rite) lifts you
			auto*       av = p->AsActorValueOwner();
			const float hp = av->GetActorValue(RE::ActorValue::kHealth);
			if (hp > 1.0f) {
				av->DamageActorValue(RE::ActorValue::kHealth, hp - 1.0f);
			}
		}

		void Rise(float a_share, const char* a_why)
		{
			auto* p = P();
			auto& f = p->GetActorRuntimeData().boolFlags;
			f.reset(RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery);
			auto*       av = p->AsActorValueOwner();
			const float mx = av->GetPermanentActorValue(RE::ActorValue::kHealth), hp = av->GetActorValue(RE::ActorValue::kHealth);
			av->RestoreActorValue(RE::ActorValue::kHealth, std::max(0.0f, mx * a_share - hp));
			p->NotifyAnimationGraph("BleedOutStop");
			p->NotifyAnimationGraph("bleedOutStop");
			if (g_life == Life::Dead) {
				Camera(nullptr);
			}
			g_life = Life::Alive;
			g_left = 0;
			Hud::Banner(a_why, 3.0f);
			logger::info("party: up again ({})", a_why);
		}

		int RiteSum()
		{
			int s = 0;
			for (int t = 0; t < static_cast<int>(gen::kCurseCities.size()); ++t) {
				s += Curse::Steps(t);
			}
			return s;
		}
	}

	// (Noah: "when I died, it respawned me in the save that I was using for a different character") the newest save of
	// the character you are playing - never another's, however recent; false when it has none yet
	namespace { std::string g_lastSave; }  // the newest of our dawn saves: a wiped party (and a fall) comes back to it
	bool LoadOwnLatest()
	{
		auto* sl = RE::BGSSaveLoadManager::GetSingleton();
		if (!sl) return false;
		sl->PopulateSaveList();
		const std::uint32_t me = sl->currentCharacterID;
		const std::string   tag = fmt::format("{:08X}", me);
		RE::BGSSaveLoadFileEntry* best = nullptr;
		std::uint64_t             bestWhen = 0;
		for (auto* e : sl->saveGameList) {
			if (!e) continue;
			if (!e->loaded) e->PopulateFileEntryData();
			const std::string fn = e->fileName.c_str() ? e->fileName.c_str() : "";
			// (the save list's own character number didn't match the game's: our dawn saves carry it in their name too)
			if (fn.empty() || (e->characterID != me && fn.find(tag) == std::string::npos)) continue;
			const std::uint64_t when = (static_cast<std::uint64_t>(e->fileTime.hi) << 32) | e->fileTime.lo;
			if (!best || when > bestWhen) best = e, bestWhen = when;
		}
		if (!best) {
			if (!g_lastSave.empty()) {  // (this session's own dawn save, whatever the list says)
				logger::info("fallen: loading this session's dawn save '{}'", g_lastSave);
				sl->Load(g_lastSave.c_str());
				return true;
			}
			int n = 0;
			for (auto* e : sl->saveGameList) if (e && n++ < 6) logger::info("fallen: in the list: '{}' character {:08X}", e->fileName.c_str() ? e->fileName.c_str() : "", e->characterID);
			logger::info("fallen: no save of this character ({:08X}) yet", me);
			return false;
		}
		const std::string file = best->fileName.c_str();
		logger::info("fallen: loading this character's newest save '{}'", file);
		sl->Load(file.c_str());
		return true;
	}

	// ---- (Noah: "BO3 death style instead of reloading a save, so we don't experience that crashing anymore") alone, you
	// never die: at zero health you fall, the world slows and darkens, the night's tally comes up (Hud: DrawDeath), and
	// you rise where you fell - your blessings gone, the wave's dead gone with them and the wave begun again. No save is
	// ever loaded (every crash after a death was in that load).
	namespace
	{
		float           g_soloT = -1.0f;  // seconds since you fell (-1: standing)
		bool            g_soloPressed = false;
		bool            g_soloLoading = false;
		std::atomic<int> g_fallGen{ 0 };  // (the audit) each fall, rise and load a new number: a stale save-load task does nothing
		float           g_soloLoadAt = 0;
		constexpr float kSoloAuto = 8.0f, kSoloMayRise = 3.2f;
		// (Noah: "the death screen came up but I could still move and fight") while you lie there Skyrim's own controls are
		// off too - moving, fighting, jumping, sneaking, activating - and you go down on your knees
		constexpr auto kFallControls = static_cast<RE::UserEvents::USER_EVENT_FLAG>(
			static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kMovement) | static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kFighting) |
			static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kJumping) | static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kSneaking) |
			static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kActivate) | static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kPOVSwitch));
		bool g_controlsOff = false;
		void FallControls(bool a_off)
		{
			if (a_off == g_controlsOff) return;
			g_controlsOff = a_off;
			if (auto* cm = RE::ControlMap::GetSingleton()) cm->ToggleControls(kFallControls, !a_off, false);
		}
		bool g_fallFirstPerson = false;
		// (Noah: "a death animation for my character when 'You have fallen' shows") you collapse where you stand - the body
		// goes limp (Skyrim's own ragdoll), seen from behind in third person; your view comes back as it was when you rise
		void Collapse(RE::PlayerCharacter* p)
		{
			if (auto* cam = RE::PlayerCamera::GetSingleton()) {
				g_fallFirstPerson = cam->IsInFirstPerson();
				if (g_fallFirstPerson) cam->ForceThirdPerson();
			}
			if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
				const auto hd = vm->GetObjectHandlePolicy()->GetHandleForObject(p->GetFormType(), p);
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
				vm->DispatchMethodCall(hd, "ObjectReference", "PushActorAway", RE::MakeFunctionArguments(static_cast<RE::Actor*>(p), 1.0f), none);
			}
			// (Noah: "my character stands up again after dying") held limp on the ground until you rise: paralysed
			p->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 1.0f);
		}
		void Uncollapse(RE::PlayerCharacter* p)
		{
			if (p && p->AsActorValueOwner()->GetActorValue(RE::ActorValue::kParalysis) > 0.0f) p->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
		}
		void SoloFall(RE::PlayerCharacter* p)
		{
			FallControls(true);
			p->NotifyAnimationGraph("BleedoutStart");
			Collapse(p);
			g_soloT = 0.0f, g_soloPressed = false, ++g_fallGen;
			Audio::LoopStop(Audio::kLoopCharge, 150), Audio::LoopStop(Audio::kLoopSpell, 150);  // (a charging hum no longer plays on through the fall)
			HoldDown();
			Ghost(kGhostFall, true);  // (nothing more lands on you while you lie there)
			Director::SlowTime(1.1f, 0.3f);
			Audio::Duck(10.0f);
			Forms::Play(gen::Sound::rite_fail);
			const auto line = Stats::Tonight();
			int perks = 0;
			for (int i = 0; i < static_cast<int>(gen::kPerks.size()); ++i) perks += Save::PerkStacks(i);
			{
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.deathT = 0.0f, h.deathRise = 0.0f, h.deathReady = false;
				h.deathNight = h.round, h.deathWave = h.wave, h.deathWaves = h.waves;
				h.deathKills = line.kills, h.deathHeads = line.heads, h.deathBest = line.best, h.deathSeptims = line.septims, h.deathPerks = perks;
			}
			Stats::OnHurt();
			logger::info("fallen: you fell (night {}, {} kills tonight) - no save is loaded, the wave begins again", line.night, line.kills);
			(void)p;
		}
		void SoloRise(RE::PlayerCharacter* p)
		{
			Perks::LoseAll();  // (the cost: every blessing)
			Director::RestartWave();
			Director::NormalTime();
			auto& f = p->GetActorRuntimeData().boolFlags;
			f.reset(RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery);
			auto*       av = p->AsActorValueOwner();
			const float mx = av->GetPermanentActorValue(RE::ActorValue::kHealth);
			av->RestoreActorValue(RE::ActorValue::kHealth, mx);
			av->RestoreActorValue(RE::ActorValue::kStamina, av->GetPermanentActorValue(RE::ActorValue::kStamina));
			av->RestoreActorValue(RE::ActorValue::kMagicka, av->GetPermanentActorValue(RE::ActorValue::kMagicka));
			if (p->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kEssentialDown || p->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kBleedout) {
				p->AsActorState()->actorState1.lifeState = RE::ACTOR_LIFE_STATE::kAlive;
			}
			p->NotifyAnimationGraph("BleedOutStop");
			p->NotifyAnimationGraph("bleedOutStop");
			p->NotifyAnimationGraph("IdleForceDefaultState");
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().deathT = -1.0f;
			}
			++g_fallGen, g_soloLoading = false;
			FallControls(false);
			Uncollapse(p);
			if (g_fallFirstPerson) {
				g_fallFirstPerson = false;
				if (auto* cam = RE::PlayerCamera::GetSingleton()) cam->ForceFirstPerson();
			}
			{
				std::scoped_lock l(Hud::Lock());
				Hud::Edit().deathLoading = false;
			}
			Hud::Banner("YOU RISE AGAIN", 3.0f);
			Forms::Play(gen::Sound::round_start);
			// (Noah: "my character was just running in place on the ground") the body could stay in its fall's ragdoll with the
			// legs still walking: a moment on, you are set down where you lie, whole and standing (a move onto yourself
			// rebuilds the body's state), and your controls are yours again
			Tasks::Later(0.6f, [] {
				auto* pl = RE::PlayerCharacter::GetSingleton();
				if (!pl || pl->IsDead() || g_soloT >= 0.0f) return;
				pl->MoveTo(pl);
				pl->NotifyAnimationGraph("IdleForceDefaultState");
				FallControls(false);
				logger::info("fallen: stood up (knock state was {})", static_cast<int>(pl->AsActorState()->GetKnockState()));
			});
			g_soloT = -1.0f;
			// a breath of safety as you stand: ghostly another two seconds
			Tasks::Later(2.0f, [] { if (g_soloT < 0.0f) Ghost(kGhostFall, false); });
			logger::info("fallen: risen again");
		}
		void TickSolo(RE::PlayerCharacter* p, float a_delta)
		{
			// (the audit: a death the essential flag couldn't stop - a script's kill - is Skyrim's own to reload; ours on top
			// would start a second load)
			if (p->IsDead()) return;
			auto*       av = p->AsActorValueOwner();
			const float hp = av->GetActorValue(RE::ActorValue::kHealth);
			if (g_soloT < 0.0f) {
				// (Helgen's own hold, the Hearthfire Tonic and its grace see to a fall themselves)
				if (!Save::Flag(Save::kFlagMoved) || Perks::SelfRevive()) return;
				if (hp <= 1.0f || p->AsActorState()->IsBleedingOut()) SoloFall(p);
				return;
			}
			// (real seconds: the slowed world doesn't slow the screen)
			const float dt = std::clamp(RE::GetSecondsSinceLastFrame(), 0.0f, 0.1f);
			(void)a_delta;
			g_soloT += dt;
			HoldDown();
			const float rise = std::clamp(g_soloT / kSoloAuto, 0.0f, 1.0f);
			const bool  ready = g_soloT >= kSoloMayRise;
			{
				std::scoped_lock l(Hud::Lock());
				auto& h = Hud::Edit();
				h.deathT = g_soloT, h.deathRise = rise, h.deathReady = ready;
				h.hurtTime = 0.0f;
			}
			// (Noah: "it shouldn't revive me where I died - it should go back to my save, and say that it's doing that") the
			// screen says so, then your most recent save loads (you are still alive in the game as it loads: on your knees,
			// not dead - the state every crash after a death loaded from). Should no save load in a few seconds (none made
			// yet), you rise where you fell instead.
			if (!g_soloLoading && (g_soloT >= kSoloAuto || (ready && g_soloPressed))) {
				g_soloLoading = true, g_soloLoadAt = g_soloT;
				{
					std::scoped_lock l(Hud::Lock());
					Hud::Edit().deathLoading = true;
				}
				Director::NormalTime();
				logger::info("fallen: loading the most recent save");
				Tasks::Later(1.2f, [gen = g_fallGen.load()] {
						auto* ui = RE::UI::GetSingleton();
						if (gen != g_fallGen.load() || !g_soloLoading || g_soloT < 0.0f || (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME))) return;
						LoadOwnLatest();  // (none yet: you rise where you fell, a few seconds on)
				});
			}
			if (g_soloLoading && g_soloT - g_soloLoadAt > 6.0f) {
				logger::info("fallen: no save loaded - rising where you fell");
				g_soloLoading = false;
				{
					std::scoped_lock l(Hud::Lock());
					Hud::Edit().deathLoading = false;
				}
				SoloRise(p);
			}
		}
	}

	// ---- the party's save points (dawn, and a freed city's dawn) ----
	namespace
	{
		std::string           g_freedName;     // the city whose freeing brings this dawn
		std::atomic<bool>     g_saving{ false };
	}

	void CityFreed(int a_town)
	{
		if (a_town >= 0 && a_town < static_cast<int>(gen::kTowns.size())) {
			g_freedName = std::string(gen::kTowns[a_town]);
		}
	}

	void DawnSave(int a_night)
	{
		if (g_saving.exchange(true)) {
			return;
		}
		std::string what = g_freedName.empty() ? "Dawn after Night " + std::to_string(a_night) : g_freedName + " Freed (Night " + std::to_string(a_night) + ")";
		g_freedName.clear();
		// a few seconds on: the fallen have risen with the light (Tick), the dead have crumbled
		Tasks::Later(4.0f, [what] {
				g_saving = false;
				auto* pl = RE::PlayerCharacter::GetSingleton();
				auto* sl = RE::BGSSaveLoadManager::GetSingleton();
				auto* ui = RE::UI::GetSingleton();
				if (!pl || !sl || pl->IsDead() || g_wipeLoading || (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME))) {
					logger::info("party save: skipped ({})", what);
					return;
				}
				// (audit: a new file every dawn piled up) one dawn save per character, written over each morning
				// (and one per character, not per name: two characters called the same never share a file)
				const std::string name = fmt::format("CurseOfAlduin - {} - Dawn - {:08X}", pl->GetName(), sl->currentCharacterID);
				sl->Save(name.c_str());
				g_lastSave = name;
				Hud::Banner("GAME SAVED - " + what, 3.0f);
				logger::info("party save: '{}'", name);
		});
	}

	void Tick(float a_delta)
	{
		auto* p = P();
		if (!p || !p->Is3DLoaded()) {
			return;
		}
		{  // each friend's marker: over their figure (Skyrim Together's), else where they say they stand
			std::vector<View::Mark> marks;
			if (auto* cam = RE::Main::WorldRootCamera(); cam && Active()) {
				std::scoped_lock l(g_lock);
				for (auto& [id, r] : g_remote) {
					if (Clock::now() - r.heard > std::chrono::seconds(5)) {
						continue;
					}
					RE::NiPoint3 at = r.at;
					float        hp = r.state == static_cast<std::uint8_t>(Life::Alive) ? 1.0f : 0.0f;
					if (auto* pu = Puppet(r)) {
						at = pu->GetPosition();
						auto*       av = pu->AsActorValueOwner();
						const float mx = std::max(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth));
						hp = std::clamp(av->GetActorValue(RE::ActorValue::kHealth) / mx, 0.0f, 1.0f);
					}
					at.z += 150.0f;
					float x = 0, y = 0, z = 0;
					cam->WorldPtToScreenPt3(at, x, y, z, 1e-5f);
					marks.push_back({ r.name, x, 1.0f - y, at.GetDistance(p->GetPosition()), z > 0.0f, static_cast<Life>(r.state), hp, std::atan2(at.x - p->GetPosition().x, at.y - p->GetPosition().y), r.cls });
				}
			}
			std::scoped_lock l(g_lock);
			g_marks = std::move(marks);
		}
		const bool active = Active();
		SetEssential(true);  // (alone too: a fall is the BO3 fall below, never Skyrim's death and its reload)
		std::scoped_lock l(g_lock);
		g_at = p->GetPosition();
		if (!active) {
			if (g_life != Life::Alive) {
				Rise(1.0f, "On your feet");
			}
			g_remote.clear();
			TickSolo(p, a_delta);
			return;
		}
		if (g_soloT >= 0.0f) SoloRise(p);  // (a party joined while you lay there)
		// fallen: zero health (Skyrim holds an essential player in bleedout instead of killing them)
		auto*       av = p->AsActorValueOwner();
		const float hp = av->GetActorValue(RE::ActorValue::kHealth);
		if (g_life == Life::Alive && (hp <= 1.0f || p->AsActorState()->IsBleedingOut())) {
			g_life = Life::Down, g_left = kDownSeconds;
			Hud::Banner("YOU ARE DOWN", 3.0f);
			logger::info("party: down");
		}
		if (g_life == Life::Down) {
			HoldDown();
			for (auto who : g_reviveIn) {
				Rise(0.5f, ("Revived by " + (g_remote.contains(who) ? g_remote[who].name : std::string("a friend"))).c_str());
				break;
			}
			if (g_life == Life::Down && (g_left -= a_delta) <= 0) {
				g_life = Life::Dead;
				Perks::LoseAll();  // death takes every blessing
				Hud::Banner("YOU DIED - watch the living", 4.0f);
				logger::info("party: dead (spectating)");
			}
		}
		g_reviveIn.clear();
		// the living we can watch
		std::vector<std::uint64_t> living;
		for (auto& [id, r] : g_remote) {
			if (r.state == static_cast<std::uint8_t>(Life::Alive) && Clock::now() - r.heard < std::chrono::seconds(3)) {
				living.push_back(id);
			}
		}
		std::sort(living.begin(), living.end());
		if (g_life == Life::Dead) {
			HoldDown();
			// back when a rite is completed anywhere (the shrine's work) or at dawn
			const int  sum = RiteSum();
			const bool fighting = Director::Fighting();
			if ((g_riteSum >= 0 && sum > g_riteSum) || (g_wasFighting && !fighting)) {
				Rise(1.0f, sum > g_riteSum ? "A trial is done: you rise again" : "Dawn: you rise again");
			} else {
				g_watch += g_watchStep;
				g_watchStep = 0;
				if (!living.empty()) {
					g_watch = ((g_watch % static_cast<int>(living.size())) + static_cast<int>(living.size())) % static_cast<int>(living.size());
					auto& r = g_remote[living[g_watch]];
					g_watchName = r.name;
					Camera(Puppet(r));
				} else {
					g_watchName.clear();
					// (the stability audit: the camera left on a friend's puppet that was deleted)
					if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraTarget.get().get() != P()) Camera(nullptr);
				}
			}
		}
		g_riteSum = RiteSum();
		g_wasFighting = Director::Fighting();
		// everyone dead at once: every game goes back to its last save
		if (g_life == Life::Dead && living.empty() && !g_wipeLoading) {
			bool anyDown = false;
			for (auto& [id, r] : g_remote) {
				anyDown |= r.state == static_cast<std::uint8_t>(Life::Down) && Clock::now() - r.heard < std::chrono::seconds(3);
			}
			if (!anyDown) {
				g_wipeLoading = true;
				Hud::Banner("THE PARTY HAS FALLEN", 4.0f);
				logger::info("party: everyone is dead: loading the most recent save");
				Tasks::Later(4.0f, [] {
						g_wipeLoading = false;
						if (auto* sl = RE::BGSSaveLoadManager::GetSingleton()) {
							if (!g_lastSave.empty()) {  // the party's last dawn: the same moment on every game
								logger::info("party: loading the dawn save '{}'", g_lastSave);
								sl->Load(g_lastSave.c_str());
							} else {
								LoadOwnLatest();
							}
						}
				});
			}
		}
		// reviving a fallen friend: stand by them and hold Activate
		std::uint64_t nearId = 0;
		if (g_life == Life::Alive) {
			for (auto& [id, r] : g_remote) {
				if (r.state == static_cast<std::uint8_t>(Life::Down) && Clock::now() - r.heard < std::chrono::seconds(3) && r.at.GetDistance(g_at) < kReviveReach) {
					nearId = id;
				}
			}
		}
		if (nearId && g_activateHeld) {
			if (nearId != g_reviveWho) {
				g_reviveWho = nearId, g_reviveT = 0;
			}
			if ((g_reviveT += a_delta) >= kReviveSeconds) {
				g_outRevive.resize(8);
				std::memcpy(g_outRevive.data(), &nearId, 8);
				Hud::Banner("You revived " + g_remote[nearId].name, 2.5f);
				Points::Add(100);
				Stats::Note(Stats::kRevive);
				if (g_remote[nearId].left < 5.0f) {
					Tasks::Add([] { Stats::Style("CLUTCH REVIVE", 100); });
				}
				g_reviveT = 0, g_reviveWho = 0;
			}
		} else {
			g_reviveT = std::max(0.0f, g_reviveT - a_delta * 2);
			if (!nearId) {
				g_reviveWho = 0;
			}
		}
		if (nearId && !g_reviveWho) {
			g_reviveWho = nearId;
		}
	}

	bool SoloFallen() { return g_soloT >= 0.0f; }
	bool HoldsDeath() { return g_essentialSet && !g_wipeLoading; }

	void OnLoad()
	{
		if (g_soloT >= 0.0f || g_soloLoading) logger::info("fallen: your save loaded - the fall is over");
		g_soloT = -1.0f, g_soloLoading = false, g_soloPressed = false;
		Director::NormalTime();
		++g_fallGen;
		FallControls(false);
		// (the audit: a wiped party's load kept you "dead" and pinned at 1 health) the party's life state starts over too
		g_life = Life::Alive, g_left = 0, g_reviveIn.clear(), g_watch = 0, g_wasFighting = false, g_riteSum = -1;
		if (auto* p = RE::PlayerCharacter::GetSingleton()) p->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery), Uncollapse(p);  // (and never paralysed by a fall a save carried)
		g_ghostWhy = 0;
		Tasks::Add([] { Gun::Console("setghost 0"); });  // (a quicksave made mid-roll or mid-fall carried the ghost flag)
		std::scoped_lock l(Hud::Lock());
		Hud::Edit().deathT = -1.0f, Hud::Edit().deathLoading = false;
	}

	bool OnButton(const RE::ButtonEvent* a_b)
	{
		if (!a_b) {
			return false;
		}
		const auto dev = a_b->GetDevice();
		const auto id = a_b->GetIDCode();
		if ((dev == RE::INPUT_DEVICE::kKeyboard && id == 18) || (dev == RE::INPUT_DEVICE::kGamepad && id == 0x1000)) {
			g_activateHeld = a_b->IsPressed();
			if (g_soloT >= 0.0f && a_b->IsDown()) g_soloPressed = true;
		}
		if (g_soloT >= 0.0f) return true;  // (fallen: nothing else answers)
		std::scoped_lock l(g_lock);
		if (g_life != Life::Dead || !a_b->IsDown()) {
			return g_life != Life::Alive;
		}
		if ((dev == RE::INPUT_DEVICE::kKeyboard && id == 205) || (dev == RE::INPUT_DEVICE::kMouse && id == 1) || (dev == RE::INPUT_DEVICE::kGamepad && id == 0x0008)) {
			++g_watchStep;  // right arrow, right mouse, d-pad right
		}
		if ((dev == RE::INPUT_DEVICE::kKeyboard && id == 203) || (dev == RE::INPUT_DEVICE::kMouse && id == 0) || (dev == RE::INPUT_DEVICE::kGamepad && id == 0x0004)) {
			--g_watchStep;  // left arrow, left mouse, d-pad left
		}
		return true;
	}

	Life State() { return g_life; }

	View Now()
	{
		std::scoped_lock l(g_lock);
		View v;
		v.life = g_life;
		v.left = g_left;
		v.watching = g_watchName;
		v.reviveFrac = g_reviveWho ? std::clamp(g_reviveT / kReviveSeconds, 0.0f, 1.0f) : -1.0f;
		if (g_reviveWho && g_remote.contains(g_reviveWho)) {
			v.reviveName = g_remote[g_reviveWho].name;
		}
		for (auto& [id, r] : g_remote) {
			if (r.state == static_cast<std::uint8_t>(Life::Down) && Clock::now() - r.heard < std::chrono::seconds(3)) {
				v.fallen.push_back({ r.name, r.left, r.at.GetDistance(g_at) });
			}
		}
		v.marks = g_marks;
		return v;
	}

	// ---- Net's side (render thread) ----
	Out Outgoing()
	{
		std::scoped_lock l(g_lock);
		Out o;
		o.life = static_cast<std::uint8_t>(g_life);
		o.at = g_at;
		o.left = g_left;
		o.revive = 0;
		o.wonder = static_cast<std::int8_t>(g_wonder.load());
		o.cls = static_cast<std::int8_t>(Classes::Get());
		if (g_outRevive.size() == 8) {
			std::memcpy(&o.revive, g_outRevive.data(), 8);
			g_outRevive.clear();
		}
		return o;
	}

	// (Noah: "I want all 4 to be done for coop or in solo") the wonders each of us has won, as bits (class order): told to
	// the party, and every friend's heard is pooled for the night - the three dragons wake when the four are won between us
	void SetWonder(int a_mask) { g_wonder = a_mask; }
	namespace { int g_pooled = 0; }
	void ResetWonders() { g_pooled = 0; }
	int  PartyWonders()
	{
		int& pooled = g_pooled;
		std::scoped_lock l(g_lock);
		for (auto& [id, r] : g_remote) {
			if (r.wonder > 0) pooled |= r.wonder & 0xF;
		}
		return pooled;
	}
	std::string WonderHolder(int) { return {}; }  // (retired: the staffs' one-wielder rule)
	void Heard(std::uint64_t a_from, const std::string& a_name, std::uint8_t a_life, const RE::NiPoint3& a_at, float a_left, std::int8_t a_wonder, std::int8_t a_cls, std::uint32_t a_credit)
	{
		std::scoped_lock l(g_lock);
		auto& r = g_remote[a_from];
		if (a_life == static_cast<std::uint8_t>(Life::Down) && r.state != a_life) {
			Tasks::Add([] { Forms::Play(gen::Sound::rite_fail); });  // a friend just went down: you hear it
		}
		r.state = a_life, r.at = a_at, r.left = a_left, r.heard = Clock::now(), r.name = a_name, r.wonder = a_wonder, r.cls = a_cls, r.credit = a_credit;
	}

	// (Noah: in a party every player takes three trials at an altar) who of the party (heard in the last 15 s) still lacks them
	int FriendsShort(int a_town, int a_need, std::string* a_names)
	{
		std::scoped_lock l(g_lock);
		int n = 0;
		for (const auto& [id, r] : g_remote) {
			if (Clock::now() - r.heard > std::chrono::seconds(15) || a_town < 0 || a_town >= 10) continue;
			if (static_cast<int>((r.credit >> (3 * a_town)) & 7) < a_need) {
				++n;
				if (a_names) *a_names += (a_names->empty() ? "" : ", ") + r.name;
			}
		}
		return n;
	}
	int FriendCount()
	{
		std::scoped_lock l(g_lock);
		int n = 0;
		for (const auto& [id, r] : g_remote) n += Clock::now() - r.heard < std::chrono::seconds(15) ? 1 : 0;
		return n;
	}

	void Revived(std::uint64_t a_by)
	{
		std::scoped_lock l(g_lock);
		g_reviveIn.push_back(a_by);
	}

	// (Noah: the upgraded staffs revive, as Origins' did) a bolt from one lands by a fallen friend: they get up
	void ReviveAt(const RE::NiPoint3& a_at, float a_radius)
	{
		std::scoped_lock l(g_lock);
		if (g_life != Life::Alive) return;
		for (auto& [id, r] : g_remote) {
			if (r.state == static_cast<std::uint8_t>(Life::Down) && Clock::now() - r.heard < std::chrono::seconds(3) && r.at.GetDistance(a_at) < a_radius) {
				g_outRevive.resize(8);
				std::memcpy(g_outRevive.data(), &id, 8);
				const std::string who = r.name;
				Tasks::Add([who] { Hud::Banner("Your staff raised " + who, 2.5f); Points::Add(100); Stats::Note(Stats::kRevive); });
				return;
			}
		}
	}

	void Forget(std::uint64_t a_who)
	{
		std::scoped_lock l(g_lock);
		g_remote.erase(a_who);
	}

	// the nearest friend's distance from a point (a huge number alone)
	float FriendDistance(const RE::NiPoint3& a_at)
	{
		std::scoped_lock l(g_lock);
		float best = 1.0e9f;
		for (auto& [id, r] : g_remote) {
			if (Clock::now() - r.heard < std::chrono::seconds(5)) {
				best = std::min(best, r.at.GetDistance(a_at));
			}
		}
		return best;
	}

	// one of the other players (Skyrim Together's figure of them: near where they say they stand)
	bool IsFriend(RE::Actor* a_actor)
	{
		if (!a_actor || a_actor == RE::PlayerCharacter::GetSingleton()) {
			return false;
		}
		std::scoped_lock l(g_lock);
		for (auto& [id, r] : g_remote) {
			if (r.puppet.get().get() == a_actor || (Clock::now() - r.heard < std::chrono::seconds(5) && r.at.GetDistance(a_actor->GetPosition()) < 300.0f)) {
				return true;
			}
		}
		return false;
	}
}
