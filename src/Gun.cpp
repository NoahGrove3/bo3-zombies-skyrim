#include "BO3Z.h"

// The M1911 (sheet: weapons). Hitscan from the camera; no Skyrim item. While it is out, Skyrim's
// fighting controls are off so the attack button only fires the gun. G swaps back to Skyrim weapons.
namespace bo3z
{
	namespace
	{
		constexpr auto& G = gen::kGun_m1911;

		bool  g_out = false;
		bool  g_weTookFighting = false;
		float g_cooldown = 0;
		float g_reload = 0;
		std::unordered_map<RE::FormID, std::pair<std::chrono::steady_clock::time_point, bool>> g_hits;
		bool  g_loggedAim = false;

		bool CanAct()
		{
			auto* ui = RE::UI::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			return ui && player && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) && player->Is3DLoaded() && !player->IsDead();
		}

		void PutAway()
		{
			if (g_weTookFighting) {
				RE::ControlMap::GetSingleton()->ToggleControls(RE::ControlMap::UEFlag::kFighting, true, false);
				g_weTookFighting = false;
			}
			g_out = false;
			g_reload = 0;
		}

		void TakeOut()
		{
			auto* controls = RE::ControlMap::GetSingleton();
			if (!controls->IsFightingControlsEnabled()) {
				Hud::Banner("Can't draw now", 1.5f);  // a scene (like Helgen's bound hands) has fighting off
				return;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player->AsActorState()->IsWeaponDrawn()) {
				player->DrawWeaponMagicHands(false);
			}
			controls->ToggleControls(RE::ControlMap::UEFlag::kFighting, false, false);
			g_weTookFighting = true;
			g_out = true;
		}

		void StartReload()
		{
			const int clip = Save::Get(gen::kAV_clip), reserve = Save::Get(gen::kAV_reserve);
			if (g_reload > 0 || clip >= G.clip || reserve <= 0) {
				return;
			}
			g_reload = G.reloadMs / 1000.0f;
			Forms::Play(G.reload);
		}

		void FinishReload()
		{
			const int clip = Save::Get(gen::kAV_clip), reserve = Save::Get(gen::kAV_reserve);
			const int moved = std::min(G.clip - clip, reserve);
			Save::Set(gen::kAV_clip, clip + moved);
			Save::Set(gen::kAV_reserve, reserve - moved);
		}

		// Closest distance between the ray (o + s d, s >= 0, d unit) and segment [a, b]; returns {distance, s}.
		std::pair<float, float> RaySegment(const RE::NiPoint3& o, const RE::NiPoint3& d, const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			const RE::NiPoint3 v = b - a;
			const float        C = v.Dot(v);
			// Two projection passes: ray point nearest the segment, then segment point nearest that.
			float t = C > 1e-6f ? std::clamp((o - a).Dot(v) / C, 0.0f, 1.0f) : 0.0f;
			float s = std::max(0.0f, (a + v * t - o).Dot(d));
			for (int i = 0; i < 2; ++i) {
				t = C > 1e-6f ? std::clamp((o + d * s - a).Dot(v) / C, 0.0f, 1.0f) : 0.0f;
				s = std::max(0.0f, (a + v * t - o).Dot(d));
			}
			return { ((o + d * s) - (a + v * t)).Length(), s };
		}

		struct Hit
		{
			RE::Actor* actor{ nullptr };
			float      t{ FLT_MAX };
			bool       head{ false };
		};

		void Fire()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam || !cam->cameraRoot) {
				return;
			}
			const auto&  world = cam->cameraRoot->world;
			RE::NiPoint3 origin = world.translate;
			RE::NiPoint3 dir{ world.rotate.entry[0][1], world.rotate.entry[1][1], world.rotate.entry[2][1] };
			dir.Unitize();
			if (!g_loggedAim) {
				const float yaw = player->GetAngleZ(), pitch = player->GetAngleX();
				logger::info("aim check: camera dir ({:.2f},{:.2f},{:.2f}) vs player angles ({:.2f},{:.2f},{:.2f})", dir.x, dir.y, dir.z,
					std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch));
				g_loggedAim = true;
			}

			Hit best;
			auto consider = [&](RE::Actor* a) {
				if (!a || a == player || a->IsDead() || !a->Is3DLoaded()) {
					return;
				}
				const bool ours = Director::IsRoundUndead(a);
				if (!ours && !a->IsHostileToActor(player)) {
					return;  // no accidental murders
				}
				const auto feet = a->GetPosition();
				if (feet.GetDistance(origin) > G.range) {
					return;
				}
				RE::NiPoint3 headPos = feet + RE::NiPoint3{ 0, 0, 110 };
				if (auto* node = a->GetNodeByName(std::string(Director::HeadNodeFor(a)))) {
					headPos = node->world.translate;
				}
				const auto [hd, ht] = RaySegment(origin, dir, headPos, headPos);
				if (hd <= G.headRadius && ht < best.t) {
					best = { a, ht, true };
					return;
				}
				const auto [bd, bt] = RaySegment(origin, dir, feet + RE::NiPoint3{ 0, 0, 15 }, headPos);
				if (bd <= G.hitRadius && bt < best.t) {
					best = { a, bt, false };
				}
			};
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				if (auto a = h.get()) {
					consider(a.get());
				}
			}
			if (!best.actor) {
				return;
			}
			bool losFlag = false;
			if (!player->HasLineOfSight(best.actor, losFlag)) {
				return;
			}

			auto*       av = best.actor->AsActorValueOwner();
			const float damage = G.damage * (best.head ? G.headMult : 1.0f) / gen::kRounds.hpScale;
			av->DamageActorValue(RE::ActorValue::kHealth, damage);
			const bool ours = Director::IsRoundUndead(best.actor);
			g_hits[best.actor->GetFormID()] = { std::chrono::steady_clock::now(), best.head };
			const bool kill = av->GetActorValue(RE::ActorValue::kHealth) <= 0;
			if (kill) {
				best.actor->KillImpl(player, damage, true, false);
			} else {
				if (ours) {
					Points::Add(gen::kPoints_hit);
				}
				if (!best.actor->IsInCombat()) {
					best.actor->StartCombat(player);
				}
			}
			std::scoped_lock l(Hud::Lock());
			Hud::Edit().hitTime = 0.2f;
			Hud::Edit().hitKill = kill;
		}

		void TryFire()
		{
			if (g_cooldown > 0 || g_reload > 0) {
				return;
			}
			const int clip = Save::Get(gen::kAV_clip);
			if (clip <= 0) {
				Forms::Play(G.empty);
				StartReload();
				return;
			}
			Save::Set(gen::kAV_clip, clip - 1);
			g_cooldown = G.fireDelayMs / 1000.0f;
			Forms::Play(G.fire);
			Fire();
		}
	}

	namespace Gun
	{
		void Reset()
		{
			PutAway();
			g_hits.clear();
		}

		bool OnButton(const RE::ButtonEvent* a_event)
		{
			if (!a_event->IsDown() || !CanAct()) {
				return false;
			}
			const auto device = a_event->GetDevice();
			const auto code = a_event->GetIDCode();
			if (device == RE::INPUT_DEVICE::kKeyboard && code == gen::kKey_swap) {
				g_out ? PutAway() : TakeOut();
				return true;
			}
			if (!g_out) {
				return false;
			}
			if (device == RE::INPUT_DEVICE::kKeyboard && code == gen::kKey_reload) {
				StartReload();
				return true;
			}
			if ((gen::kFireIsMouse ? device == RE::INPUT_DEVICE::kMouse : device == RE::INPUT_DEVICE::kKeyboard) && code == gen::kKey_fire) {
				TryFire();
				return true;
			}
			return false;
		}

		void Tick(float a_delta)
		{
			g_cooldown = std::max(0.0f, g_cooldown - a_delta);
			if (g_reload > 0 && (g_reload -= a_delta) <= 0) {
				g_reload = 0;
				FinishReload();
			}
			if (g_out && RE::ControlMap::GetSingleton()->IsFightingControlsEnabled()) {
				// Something re-enabled fighting behind our back (a scene ended): take it again.
				RE::ControlMap::GetSingleton()->ToggleControls(RE::ControlMap::UEFlag::kFighting, false, false);
			}
			const auto now = std::chrono::steady_clock::now();
			std::erase_if(g_hits, [&](const auto& kv) { return now - kv.second.first > 2s; });

			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.gunOut = g_out;
			h.clip = Save::Get(gen::kAV_clip);
			h.reserve = Save::Get(gen::kAV_reserve);
			h.reloading = g_reload > 0;
			h.points = Points::Get();
		}

		std::pair<bool, bool> RecentHit(RE::FormID a_actor)
		{
			if (auto it = g_hits.find(a_actor); it != g_hits.end() && std::chrono::steady_clock::now() - it->second.first < 500ms) {
				return { true, it->second.second };
			}
			return { false, false };
		}
	}
}
