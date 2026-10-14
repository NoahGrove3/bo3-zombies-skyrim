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
		};

		std::mutex                                     g_lock;
		std::unordered_map<std::uint64_t, Remote>      g_remote;   // by Steam id
		std::vector<std::uint64_t>                     g_reviveIn;  // revives addressed to us (Steam ids of the helpers)
		std::vector<std::uint8_t>                      g_outRevive;  // a revive to send (target id, 8 bytes)
		Life                                           g_life = Life::Alive;
		float                                          g_left = 0;     // seconds left while down
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
			if (!p || a_on == g_essentialSet) {
				return;
			}
			auto& f = p->GetActorRuntimeData().boolFlags;
			if (a_on) {
				f.set(RE::Actor::BOOL_FLAGS::kEssential);
			} else {
				f.reset(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery);
			}
			g_essentialSet = a_on;
			logger::info("party: you {} fall instead of dying", a_on ? "now" : "no longer");
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
				s += Curse::Raw(t) / 1000;
			}
			return s;
		}
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
					marks.push_back({ r.name, x, 1.0f - y, at.GetDistance(p->GetPosition()), z > 0.0f, static_cast<Life>(r.state), hp, std::atan2(at.x - p->GetPosition().x, at.y - p->GetPosition().y) });
				}
			}
			std::scoped_lock l(g_lock);
			g_marks = std::move(marks);
		}
		const bool active = Active();
		SetEssential(active);
		std::scoped_lock l(g_lock);
		g_at = p->GetPosition();
		if (!active) {
			if (g_life != Life::Alive) {
				Rise(1.0f, "On your feet");
			}
			g_remote.clear();
			return;
		}
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
				Rise(1.0f, sum > g_riteSum ? "The rite is done: you rise again" : "Dawn: you rise again");
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
				std::thread([] {
					std::this_thread::sleep_for(std::chrono::seconds(4));
					SKSE::GetTaskInterface()->AddTask([] {
						g_wipeLoading = false;
						if (auto* sl = RE::BGSSaveLoadManager::GetSingleton()) {
							sl->LoadMostRecentSaveGame();
						}
					});
				}).detach();
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

	bool OnButton(const RE::ButtonEvent* a_b)
	{
		if (!a_b) {
			return false;
		}
		const auto dev = a_b->GetDevice();
		const auto id = a_b->GetIDCode();
		if ((dev == RE::INPUT_DEVICE::kKeyboard && id == 18) || (dev == RE::INPUT_DEVICE::kGamepad && id == 0x1000)) {
			g_activateHeld = a_b->IsPressed();
		}
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
		if (g_outRevive.size() == 8) {
			std::memcpy(&o.revive, g_outRevive.data(), 8);
			g_outRevive.clear();
		}
		return o;
	}

	void Heard(std::uint64_t a_from, const std::string& a_name, std::uint8_t a_life, const RE::NiPoint3& a_at, float a_left)
	{
		std::scoped_lock l(g_lock);
		auto& r = g_remote[a_from];
		if (a_life == static_cast<std::uint8_t>(Life::Down) && r.state != a_life) {
			SKSE::GetTaskInterface()->AddTask([] { Forms::Play(gen::Sound::rite_fail); });  // a friend just went down: you hear it
		}
		r.state = a_life, r.at = a_at, r.left = a_left, r.heard = Clock::now(), r.name = a_name;
	}

	void Revived(std::uint64_t a_by)
	{
		std::scoped_lock l(g_lock);
		g_reviveIn.push_back(a_by);
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
