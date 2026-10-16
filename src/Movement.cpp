#include "BO3Z.h"

// Fast, smooth movement on Skyrim's controller (sheet: movement), aiming for an arena-shooter feel:
//  - quicker run and sprint (SpeedMult), cheaper sprinting, a bit more jump;
//  - slide: crouch while sprinting -> you shoot forward in the direction you were running and ease off;
//  - slide-jump: jumping out of a slide carries the speed into the air;
//  - air strafing: steer a little while airborne.
// Speed changes use a temporary SpeedMult (CarryWeight nudged so the game picks it up at once);
// the slide and air control also write the character controller's velocity directly.
namespace bo3z
{
	namespace
	{
		constexpr float kHavok = 0.0142875f;  // Skyrim units -> Havok units

		float        g_applied = 0;
		float        g_slide = 0;
		bool         g_wasSneaking = false;
		RE::NiPoint3 g_slideDir{};
		float        g_carry = 0;   // horizontal speed carried into a jump from a slide (units/s)
		bool         g_wasAir = false;
		bool         g_gmst = false;
		bool         g_wantSlide = false;   // crouch pressed while sprinting
		bool         g_wantJumpCarry = false;
		float        g_air = 0;             // seconds airborne
		bool         g_forcedSneak = false;
		float        g_smooth = 0;          // SpeedMult boost, eased toward what we want (no jolts sprint <-> run <-> strafe)
		float        g_slideSpeed = 0;      // this slide's starting speed
		RE::NiPoint3 g_groundVel{};         // last horizontal velocity on the ground (carried into jumps)
		RE::NiPoint3 g_takeoff{};           // horizontal velocity to keep through the start of a jump
		bool         g_movt = false;

		void Apply(float a_want)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || std::abs(a_want - g_applied) < 0.5f) {
				return;
			}
			auto* av = player->AsActorValueOwner();
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, a_want - g_applied);
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
			g_applied = a_want;
		}

		void TuneGameSettings()
		{
			if (g_gmst) {
				return;
			}
			g_gmst = true;
			auto* gs = RE::GameSettingCollection::GetSingleton();
			if (!gs) {
				return;
			}
			auto scale = [&](const char* a_name, float a_mult) {
				if (auto* s = gs->GetSetting(a_name)) {
					const float before = s->GetFloat();
					s->data.f = before * a_mult;
					logger::info("movement: {} {:.2f} -> {:.2f}", a_name, before, s->data.f);
				}
			};
			scale("fSprintStaminaDrainMult", gen::kSprintStamina);
			scale("fJumpHeightMin", gen::kJumpHeight);
			// no fall damage: a fall never hurts (parkour, the shrine's heights, SkyParkour's climbs)
			for (auto [name, v] : { std::pair{ "fJumpFallHeightMin", 1.0e6f }, std::pair{ "fJumpFallHeightMult", 0.0f } }) {
				if (auto* s = gs->GetSetting(name)) {
					s->data.f = v;
					logger::info("movement: {} -> {} (no fall damage)", name, v);
				}
			}
		}

		// Skyrim runs slower sideways and backwards than forwards, and turns stiffly while running: strafing
		// left/right felt like wading. The humanoid movement types get Apex-like strafe/back speeds and quicker turns.
		void TuneMovementTypes()
		{
			if (g_movt) {
				return;
			}
			g_movt = true;
			using D = RE::Movement::SPEED_DIRECTIONS;
			int n = 0;
			// Skyrim.esm: NPC_Default, Sneaking, Sprinting, 1HM, 2HM, Bow, BowDrawn, Magic, MagicCasting, Attacking, Blocking
			for (const RE::FormID id : { 0x03580D, 0x03580B, 0x034D9C, 0x069CD8, 0x069CD9, 0x069CDA, 0x03580C, 0x069CDB, 0x069CDC, 0x0A0BC2, 0x035B4C }) {
				auto* mt = RE::TESForm::LookupByID<RE::BGSMovementType>(id);
				if (!mt) {
					continue;
				}
				const std::string name(mt->movementTypeData.typeName.c_str());
				auto& sp = mt->movementTypeData.defaultData;
				for (int k = 0; k < 2; ++k) {
					const float fwd = sp.speeds[D::kForward][k];
					sp.speeds[D::kLeft][k] = std::max(sp.speeds[D::kLeft][k], fwd * gen::kStrafeSpeed);
					sp.speeds[D::kRight][k] = std::max(sp.speeds[D::kRight][k], fwd * gen::kStrafeSpeed);
					sp.speeds[D::kBack][k] = std::max(sp.speeds[D::kBack][k], fwd * gen::kBackSpeed);
				}
				sp.rotateWhileMovingRun *= gen::kTurnRate;
				logger::info("movement: {} run fwd {:.0f} side {:.0f} back {:.0f}, turn {:.2f}", name, sp.speeds[D::kForward][1], sp.speeds[D::kLeft][1], sp.speeds[D::kBack][1], sp.rotateWhileMovingRun);
				++n;
			}
			if (!n) {
				logger::warn("movement: no humanoid movement types found");
			}
		}

		RE::NiPoint3 Heading(RE::PlayerCharacter* a_player)
		{
			const float a = a_player->GetAngleZ();
			return { std::sin(a), std::cos(a), 0 };
		}

		// Camera-relative wish direction from WASD / left stick.
		RE::NiPoint3 WishDir(RE::PlayerCharacter* a_player)
		{
			auto* pc = RE::PlayerControls::GetSingleton();
			if (!pc) {
				return {};
			}
			const auto in = pc->data.moveInputVec;  // x = strafe, y = forward
			if (in.x * in.x + in.y * in.y < 0.01f) {
				return {};
			}
			const float  a = a_player->GetAngleZ();
			RE::NiPoint3 fwd{ std::sin(a), std::cos(a), 0 }, right{ std::cos(a), -std::sin(a), 0 };
			auto         d = fwd * in.y + right * in.x;
			const float  len = std::sqrt(d.x * d.x + d.y * d.y);
			return len > 0 ? d / len : RE::NiPoint3{};
		}

		bool GetVel(RE::bhkCharacterController* a_cc, RE::NiPoint3& a_out)
		{
			RE::hkVector4 v;
			a_cc->GetLinearVelocityImpl(v);
			float q[4];
			_mm_storeu_ps(q, v.quad);
			a_out = { q[0] / kHavok, q[1] / kHavok, q[2] / kHavok };
			return true;
		}
		void SetVel(RE::bhkCharacterController* a_cc, const RE::NiPoint3& a_v)
		{
			RE::hkVector4 v(a_v.x * kHavok, a_v.y * kHavok, a_v.z * kHavok, 0.0f);
			a_cc->SetLinearVelocityImpl(v);
		}
	}

	namespace Movement
	{
		// Skyrim's Paraglider: everyone carries the glider (Paragliding.esp's "Paraglider", local 0x802) - its own way
		// of getting one is a little quest; here it is in your pack. Its script sets the behavior flag the glide needs
		// when it enters your inventory; the flag is also kept set here (a load or a new body can drop it).
		// The dragon glider, alive (its model: tools/modelconv/dragonglider.cpp - wingbeats, breathing, the tail's wave are
		// its own loop): while you glide, the game leans it into your turns, turns its head toward where you look (and
		// lets it look about on its own), and every so often it opens its jaws and breathes a stream of dragonfire ahead
		// - only for show: the fire carries no spell and harms nothing.
		RE::NiMatrix3 Rot(float a_pitch, float a_roll, float a_yaw)  // radians: about X (nose up), Y (roll), Z (turn)
		{
			const float cx = std::cos(a_pitch), sx = std::sin(a_pitch), cy = std::cos(a_roll), sy = std::sin(a_roll), cz = std::cos(a_yaw), sz = std::sin(a_yaw);
			RE::NiMatrix3 x, y, z;
			x.entry[0][0] = 1, x.entry[0][1] = 0, x.entry[0][2] = 0, x.entry[1][0] = 0, x.entry[1][1] = cx, x.entry[1][2] = -sx, x.entry[2][0] = 0, x.entry[2][1] = sx, x.entry[2][2] = cx;
			y.entry[0][0] = cy, y.entry[0][1] = 0, y.entry[0][2] = sy, y.entry[1][0] = 0, y.entry[1][1] = 1, y.entry[1][2] = 0, y.entry[2][0] = -sy, y.entry[2][1] = 0, y.entry[2][2] = cy;
			z.entry[0][0] = cz, z.entry[0][1] = -sz, z.entry[0][2] = 0, z.entry[1][0] = sz, z.entry[1][1] = cz, z.entry[1][2] = 0, z.entry[2][0] = 0, z.entry[2][1] = 0, z.entry[2][2] = 1;
			return z * y * x;
		}
		// ---- the dragon's goodbye: when the glider is put away the dragon lets go of you and flies on - a separate
		// reference in the world (its own nif: CurseOfAlduin\fx\dragon_away.nif, the glider's dragon on its own) placed
		// where it was holding you, carrying on forward, climbing and banking away, its talons opening then drawn up
		struct Away
		{
			RE::ObjectRefHandle ref;
			float               t = -1;
			RE::NiTransform     start;
			RE::NiMatrix3       bank;
			RE::NiPoint3        vel;
			float               scale = 1, side = 1;
		};
		Away            g_away;
		RE::NiTransform g_lastSpace;       // where the dragon was last frame (DG_Space's world transform)
		RE::NiMatrix3   g_lastBank;
		bool            g_hadDragon = false;
		float           g_armHold = 0;     // 1: your arms stay down (the dragon hasn't reached you yet), 0: the glide's own pose
		bool            g_armHoldOn = false;

		float Smooth(float a_x)
		{
			a_x = std::clamp(a_x, 0.0f, 1.0f);
			return a_x * a_x * (3 - 2 * a_x);
		}

		// rotations blended through quaternions
		struct Quat
		{
			float w, x, y, z;
		};
		Quat ToQuat(const RE::NiMatrix3& m)
		{
			const auto& e = m.entry;
			Quat        q;
			const float tr = e[0][0] + e[1][1] + e[2][2];
			if (tr > 0) {
				const float s = std::sqrt(tr + 1.0f) * 2;
				q = { 0.25f * s, (e[2][1] - e[1][2]) / s, (e[0][2] - e[2][0]) / s, (e[1][0] - e[0][1]) / s };
			} else if (e[0][0] > e[1][1] && e[0][0] > e[2][2]) {
				const float s = std::sqrt(1.0f + e[0][0] - e[1][1] - e[2][2]) * 2;
				q = { (e[2][1] - e[1][2]) / s, 0.25f * s, (e[0][1] + e[1][0]) / s, (e[0][2] + e[2][0]) / s };
			} else if (e[1][1] > e[2][2]) {
				const float s = std::sqrt(1.0f + e[1][1] - e[0][0] - e[2][2]) * 2;
				q = { (e[0][2] - e[2][0]) / s, (e[0][1] + e[1][0]) / s, 0.25f * s, (e[1][2] + e[2][1]) / s };
			} else {
				const float s = std::sqrt(1.0f + e[2][2] - e[0][0] - e[1][1]) * 2;
				q = { (e[1][0] - e[0][1]) / s, (e[0][2] + e[2][0]) / s, (e[1][2] + e[2][1]) / s, 0.25f * s };
			}
			return q;
		}
		RE::NiMatrix3 FromQuat(Quat q)
		{
			const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
			if (n < 1e-6f) return Rot(0, 0, 0);
			q.w /= n, q.x /= n, q.y /= n, q.z /= n;
			RE::NiMatrix3 m;
			m.entry[0][0] = 1 - 2 * (q.y * q.y + q.z * q.z), m.entry[0][1] = 2 * (q.x * q.y - q.z * q.w), m.entry[0][2] = 2 * (q.x * q.z + q.y * q.w);
			m.entry[1][0] = 2 * (q.x * q.y + q.z * q.w), m.entry[1][1] = 1 - 2 * (q.x * q.x + q.z * q.z), m.entry[1][2] = 2 * (q.y * q.z - q.x * q.w);
			m.entry[2][0] = 2 * (q.x * q.z - q.y * q.w), m.entry[2][1] = 2 * (q.y * q.z + q.x * q.w), m.entry[2][2] = 1 - 2 * (q.x * q.x + q.y * q.y);
			return m;
		}
		RE::NiMatrix3 Slerp(const RE::NiMatrix3& a_from, const RE::NiMatrix3& a_to, float a_t)
		{
			if (a_t <= 0) return a_from;
			if (a_t >= 1) return a_to;
			Quat  a = ToQuat(a_from), b = ToQuat(a_to);
			float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
			if (d < 0) b = { -b.w, -b.x, -b.y, -b.z }, d = -d;
			float ka = 1 - a_t, kb = a_t;
			if (d < 0.9995f) {
				const float th = std::acos(std::min(1.0f, d)), sn = std::sin(th);
				ka = std::sin((1 - a_t) * th) / sn, kb = std::sin(a_t * th) / sn;
			}
			return FromQuat({ a.w * ka + b.w * kb, a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb });
		}

		void AwayTick(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (g_away.t < 0) {
				return;
			}
			g_away.t += a_delta;
			const float t = g_away.t;
			auto        ref = g_away.ref.get();
			if (!ref) {
				g_away.t = -1;
				return;
			}
			if (t > 3.6f) {
				ref->Disable();
				ref->SetDelete(true);
				g_away = {};
				return;
			}
			auto* root = ref->Get3D();
			if (!root) {
				return;  // (still loading: it catches up with its path when it appears)
			}
			const auto&        R0 = g_away.start.rotate;
			RE::NiPoint3       right{ R0.entry[0][0], R0.entry[1][0], R0.entry[2][0] };
			RE::NiPoint3       fwd{ R0.entry[0][1], R0.entry[1][1], R0.entry[2][1] };
			RE::NiPoint3       up{ R0.entry[0][2], R0.entry[1][2], R0.entry[2][2] };
			const float        sd = g_away.side;
			// your momentum, then its own: a lurch up as your weight leaves it, a climb, and a curve off to one side
			const RE::NiPoint3 pos = g_away.start.translate + g_away.vel * (t * 0.85f) + fwd * (260 * t + 170 * t * t) +
			                         up * (90 * std::sin(std::min(t, 0.6f) * 2.6f) + 150 * t * t) + right * (sd * 95 * t * t);
			const float climb = Smooth(t / 1.3f);
			const float roll = sd * 0.75f * std::sin(3.14159f * std::min(1.0f, t / 2.8f));
			root->local.translate = pos;
			root->local.rotate = R0 * Rot(0.42f * climb, roll, -sd * 0.55f * climb);
			root->local.scale = g_away.scale;
			if (auto* bank = root->GetObjectByName("DG_Bank")) {  // (from the lean it had as it let go, straightening out)
				bank->local.rotate = Slerp(g_away.bank, Rot(0, 0, 0), Smooth(t / 0.6f));
			}
			if (auto* look = root->GetObjectByName("DG_Look")) {  // looking where it turns
				look->local.rotate = Rot(0.1f * climb, 0, -sd * 0.35f * Smooth(t / 0.9f));
			}
			// talons: open as it lets you go, then the legs drawn up under it for the flight
			const float open = std::sin(3.14159f * std::clamp(t / 0.45f, 0.0f, 1.0f));
			const float tuck = Smooth((t - 0.3f) / 0.7f);
			for (int s2 = 0; s2 < 2; ++s2) {
				const char* L = s2 == 0 ? "L" : "R";
				if (auto* n = root->GetObjectByName(fmt::format("DG_Leg{}1", L).c_str())) n->local.rotate = Rot(0.12f * (1 - tuck) - 0.55f * tuck, 0, 0);
				if (auto* n = root->GetObjectByName(fmt::format("DG_Leg{}2", L).c_str())) n->local.rotate = Rot(-0.1f * (1 - tuck) + 0.7f * tuck, 0, 0);
				if (auto* n = root->GetObjectByName(fmt::format("DG_Leg{}3", L).c_str())) n->local.rotate = Rot(0.4f * (1 - tuck) - 0.8f * open + 0.35f * tuck, 0, 0);
			}
			RE::NiUpdateData ud;
			root->Update(ud);
		}

		void LetGo(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_vel, float a_roll, float a_baseScale)
		{
			if (g_away.t >= 0) {  // (the last one is still in the air: it goes now)
				if (auto old = g_away.ref.get()) {
					old->Disable();
					old->SetDelete(true);
				}
				g_away = {};
			}
			auto* st = Forms::Ours(gen::kDragonAway);
			if (!st) {
				return;
			}
			auto ref = a_player->PlaceObjectAtMe(st, false);
			if (!ref) {
				return;
			}
			ref->SetPosition(g_lastSpace.translate);
			static std::mt19937 rng{ std::random_device{}() };
			g_away.ref = ref->CreateRefHandle();
			g_away.t = 0;
			g_away.start = g_lastSpace;
			g_away.bank = g_lastBank;
			g_away.vel = a_vel;
			g_away.scale = a_baseScale > 0 ? g_lastSpace.scale / a_baseScale : 1.0f;
			g_away.side = std::abs(a_roll) > 0.05f ? (a_roll > 0 ? 1.0f : -1.0f) : (rng() % 2 ? 1.0f : -1.0f);
		}

		// ---- the Echo Altar's dragon bonds (Noah): three, bought in order at any Echo Altar, kept for the whole game
		//   I   Wind-Rider     a quicker glide that sinks slower; jump mid-glide and it beats its wings to climb (a few
		//                      seconds between beats). Its eyes glow ice-pale.
		//   II  Talon Strike   sprint and press Activate on open ground: it swoops in and lifts you off your feet; attack
		//                      while looking down mid-glide and it hurls you at the ground - you land in a shockwave that
		//                      throws the dead about. Its markings turn glacier blue.
		//   III Alduin's Thrall (needs a city freed from the Dirge) black scales, ember eyes - its fire breath is real now:
		//                      it turns on the dead below you, in the element of the spell in your hand (fire burns
		//                      hotter, frost slows them, lightning leaps to the next).
		// Its look: Paragliding.esp's animated object (ANIO 0x800) is given the bond's nif before you glide, the dragon
		// that flies off afterwards the same (tools/modelconv/dragonvariant.cpp, tools/dragontex.py).
		float g_flapT = -1, g_flapWait = 0;  // Bond I: a wing-beat's time, and the wait before the next
		int   g_liftOff = 0;                 // Bond II: 1 hopped up (the glider is being opened), 2 hanging there as it comes
		float g_takeT = 0, g_liftT = -1;
		bool  g_dive = false;                // Bond II: hurled at the ground
		float g_diveT = 0;
		struct Fx
		{
			RE::ObjectRefHandle ref;
			float               t;
		};
		std::vector<Fx> g_fx;
		struct Slowed
		{
			RE::ActorHandle a;
			float           t;
		};
		std::vector<Slowed> g_slowed;  // Bond III frost breath: the dead it slowed, until it wears off

		int Bond() { return std::clamp(Save::DragonBond(), 0, 3); }

		void ApplyBondLook()
		{
			static int applied = -1;
			const int  b = Bond();
			if (b == applied) {
				return;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* anio = dh ? dh->LookupForm<RE::TESObjectANIO>(0x800, "Paragliding.esp") : nullptr;
			if (!anio) {
				return;
			}
			static constexpr const char* kGlider[4] = { "NickNak\\ParaGlider.nif", "NickNak\\ParaGlider_b1.nif", "NickNak\\ParaGlider_b2.nif", "NickNak\\ParaGlider_b3.nif" };
			static constexpr const char* kAway[4] = { "CurseOfAlduin\\fx\\dragon_away.nif", "CurseOfAlduin\\fx\\dragon_away_b1.nif", "CurseOfAlduin\\fx\\dragon_away_b2.nif", "CurseOfAlduin\\fx\\dragon_away_b3.nif" };
			anio->SetModel(kGlider[b]);
			if (auto* st = Forms::Ours(gen::kDragonAway); st && st->As<RE::TESObjectSTAT>()) {
				st->As<RE::TESObjectSTAT>()->SetModel(kAway[b]);
			}
			applied = b;
			logger::info("glider: the dragon's bond {} ({})", b, kGlider[b]);
		}

		void Hurt(RE::Actor* a_actor, float a_damage)
		{
			auto* av = a_actor->AsActorValueOwner();
			av->DamageActorValue(RE::ActorValue::kHealth, a_damage);
			if (av->GetActorValue(RE::ActorValue::kHealth) <= 0) {
				a_actor->KillImpl(RE::PlayerCharacter::GetSingleton(), a_damage, true, false);
			}
		}
		bool Foe(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
		{
			return a_actor && a_actor != a_player && !a_actor->IsDead() &&
			       (Director::IsRoundUndead(a_actor) || (a_actor->IsHostileToActor(a_player) && !a_actor->IsPlayerTeammate()));
		}

		bool OnGround(RE::PlayerCharacter* a_player)
		{
			auto* cc = a_player->GetCharController();
			return cc && cc->context.currentState == RE::hkpCharacterStateType::kOnGround;
		}

		// Bond II: the dive's landing - a shockwave
		void Slam(RE::PlayerCharacter* a_player)
		{
			const auto at = a_player->GetPosition();
			RE::ShakeCamera(1.2f, at, 0.7f);
			Forms::PlayAt(gen::Sound::shout_quake, a_player);
			if (auto* fx = Forms::Ours(gen::kRiseFx)) {
				if (auto r = a_player->PlaceObjectAtMe(fx, false)) {
					r->SetPosition(at);
					g_fx.push_back({ r->CreateRefHandle(), 3.0f });
				}
			}
			constexpr float kR = 460.0f;
			int             n = 0;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!Foe(a.get(), a_player)) {
					continue;
				}
				const float d = a->GetPosition().GetDistance(at);
				if (d > kR) {
					continue;
				}
				const float k = 1.0f - 0.6f * d / kR;
				Hurt(a.get(), Director::IsRoundUndead(a.get()) ? Director::RoundHealth() * 0.55f * k
				                                               : a->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.12f * k);
				if (!a->IsDead()) {
					a->SetGraphVariableFloat("staggerMagnitude", 1.0f);
					a->NotifyAnimationGraph("staggerStart");
				}
				++n;
			}
			logger::info("glider: dive landed, {} caught in the shockwave", n);
		}

		// Bond III: the breath's look in each element - a stream spell of that element from the Coffer (fire: the
		// dragons' own breath)
		RE::BGSProjectile* BreathProjectile(int a_element)
		{
			static std::array<RE::BGSProjectile*, 8> cache{};
			static std::array<bool, 8>               looked{};
			static RE::BGSProjectile*                fire = RE::TESForm::LookupByID<RE::BGSProjectile>(0x000EBB75);
			if (a_element <= 0 || a_element >= 8) {
				return fire;
			}
			if (!looked[a_element] && Arcana::Count() > 0) {
				looked[a_element] = true;
				for (int i = 0; i < 256 && !cache[a_element]; ++i) {
					auto* sp = Arcana::Spell(i);
					if (!sp || !Arcana::Concentration(i) || Arcana::ElementOf(i) != a_element) {
						continue;
					}
					for (auto* e : sp->effects) {
						auto* pr = e && e->baseEffect ? e->baseEffect->data.projectileBase : nullptr;
						if (pr && pr->IsFlamethrower()) {
							cache[a_element] = pr;
							break;
						}
					}
				}
			}
			return cache[a_element] ? cache[a_element] : fire;
		}
		int HandElement()
		{
			const auto h = Arcana::Hands();
			const int  sp = h[1] >= 0 ? h[1] : h[0];
			return sp >= 0 ? Arcana::ElementOf(sp) : 0;
		}

		// Bond III: what the breath turns on - the nearest of the dead below and ahead of you
		RE::Actor* BreathTarget(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from)
		{
			const float  yaw = a_player->GetAngleZ();
			RE::NiPoint3 fwd{ std::sin(yaw), std::cos(yaw), 0 };
			RE::Actor*   best = nullptr;
			float        bestD = 1700.0f;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!Foe(a.get(), a_player)) {
					continue;
				}
				RE::NiPoint3 to = a->GetPosition() + RE::NiPoint3{ 0, 0, 60 } - a_from;
				const float  d = to.Length();
				if (d > bestD || to.z > -80.0f) {
					continue;
				}
				RE::NiPoint3 flat{ to.x, to.y, 0 };
				const float  fl = flat.Length();
				if (fl > 1 && (flat.x * fwd.x + flat.y * fwd.y) / fl < 0.25f && fl > 250.0f) {
					continue;  // (behind you, and not right under you)
				}
				best = a.get(), bestD = d;
			}
			return best;
		}
		void BreathHits(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, const RE::NiPoint3& a_dir, int a_element)
		{
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!Foe(a.get(), a_player)) {
					continue;
				}
				RE::NiPoint3 to = a->GetPosition() + RE::NiPoint3{ 0, 0, 60 } - a_from;
				const float  d = to.Length();
				if (d > 1800.0f || d < 1 || (to.x * a_dir.x + to.y * a_dir.y + to.z * a_dir.z) / d < 0.90f) {
					continue;
				}
				const bool undead = Director::IsRoundUndead(a.get());
				float      dmg = undead ? Director::RoundHealth() * 0.10f : a->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.025f;
				if (a_element == 0) {
					dmg *= 1.25f;  // fire: hotter
				}
				Hurt(a.get(), dmg);
				if (a->IsDead()) {
					continue;
				}
				if (a_element == 1) {  // frost: slowed for three seconds
					bool already = false;
					for (auto& sl : g_slowed) already = already || sl.a.get().get() == a.get();
					if (!already) {
						auto* av = a->AsActorValueOwner();
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -45.0f);
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
						g_slowed.push_back({ a->GetHandle(), 3.0f });
					}
				} else if (a_element == 2) {  // lightning: it leaps to the nearest other
					RE::Actor* next = nullptr;
					float      nd = 380.0f;
					for (auto& h2 : RE::ProcessLists::GetSingleton()->highActorHandles) {
						auto b = h2.get();
						if (b.get() == a.get() || !Foe(b.get(), a_player)) continue;
						const float dd = b->GetPosition().GetDistance(a->GetPosition());
						if (dd < nd) next = b.get(), nd = dd;
					}
					if (next) Hurt(next, dmg * 0.5f);
				}
			}
		}

		void BondTick(RE::PlayerCharacter* a_player, float a_delta)
		{
			ApplyBondLook();
			g_flapWait = std::max(0.0f, g_flapWait - a_delta);
			for (auto it = g_fx.begin(); it != g_fx.end();) {
				if ((it->t -= a_delta) <= 0) {
					if (auto r = it->ref.get()) {
						r->Disable();
						r->SetDelete(true);
					}
					it = g_fx.erase(it);
				} else {
					++it;
				}
			}
			for (auto it = g_slowed.begin(); it != g_slowed.end();) {
				if ((it->t -= a_delta) <= 0) {
					if (auto a = it->a.get()) {
						auto* av = a->AsActorValueOwner();
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 45.0f);
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
					}
					it = g_slowed.erase(it);
				} else {
					++it;
				}
			}
			// Bond II take-off: hopped up, the glider is opened (Activate, as you would) - twice if the first was early
			if (g_liftOff == 1) {
				const float before = g_takeT;
				g_takeT += a_delta;
				if (auto* cc = a_player->GetCharController(); cc && g_takeT < 0.35f && !OnGround(a_player)) {  // the hop, higher
					RE::NiPoint3 v{};
					GetVel(cc, v);
					v.z = std::max(v.z, 520.0f);
					SetVel(cc, v);
				}
				for (const float at : { 0.12f, 0.42f }) {
					if (before < at && g_takeT >= at && !g_hadDragon) {
						DevTools::PressKey(RE::INPUT_DEVICE::kKeyboard, 0x12, 0.05f);
					}
				}
				if (g_hadDragon) {
					g_liftOff = 2;
				} else if (g_takeT > 1.0f) {
					g_liftOff = 0;  // (the glider never opened: just a hop)
					logger::info("glider: take-off - the glider did not open");
				}
			}
			// Bond II dive: hurled at the ground until you hit it
			if (g_dive) {
				g_diveT += a_delta;
				auto* cc = a_player->GetCharController();
				if ((g_diveT > 0.15f && OnGround(a_player)) || g_diveT > 6.0f) {
					g_dive = false;
					if (g_diveT <= 6.0f) {
						Slam(a_player);
					}
				} else if (cc) {
					const float  yaw = a_player->GetAngleZ();
					RE::NiPoint3 v{ std::sin(yaw) * 520.0f, std::cos(yaw) * 520.0f, -2600.0f };
					SetVel(cc, v);
				}
			}
		}

		void Dragon(RE::PlayerCharacter* a_player, float a_delta)
		{
			static float lastYaw = 0, turn = 0, wanderT = 0, breathIn = 6.0f, breath = 0, jaw = 0, shotT = 0, lookYaw = 0, lookPitch = 0, roll = 0, pitch = 0;
			static float lastZ = 0, deployT = -1, baseScale = 0, legT = 0, regrip = 9, regripIn = 4.0f, breathT_ = 0;
			static bool  breathing_ = false;
			static RE::NiPoint3 baseTrans{};
			static std::mt19937 rng{ std::random_device{}() };
			RE::NiAVObject* bank[2]{};
			RE::NiAVObject* look[2]{};
			RE::NiAVObject* jawN[2]{};
			RE::NiAVObject* mouth = nullptr;
			RE::NiAVObject* head = nullptr;
			RE::NiAVObject* space[2]{};
			RE::NiAVObject* fold[2][2]{};
			RE::NiAVObject* legN[2][2][3]{};
			for (int i = 0; i < 2; ++i) {
				if (auto* root = a_player->Get3D(i == 1)) {
					bank[i] = root->GetObjectByName("DG_Bank");
					look[i] = root->GetObjectByName("DG_Look");
					jawN[i] = root->GetObjectByName("DG_Jaw");
					space[i] = root->GetObjectByName("DG_Space");
					fold[i][0] = root->GetObjectByName("DG_WingL0");
					fold[i][1] = root->GetObjectByName("DG_WingR0");
					for (int sd = 0; sd < 2; ++sd) {
						for (int j = 0; j < 3; ++j) {
							legN[i][sd][j] = root->GetObjectByName(fmt::format("DG_Leg{}{}", sd == 0 ? "L" : "R", j + 1).c_str());
						}
					}
					if (bank[i] && !mouth) {
						mouth = root->GetObjectByName("DG_Mouth");
						head = root->GetObjectByName("DG_Head");
					}
				}
			}
			const float yaw = a_player->GetAngleZ(), z = a_player->GetPositionZ();
			static RE::NiPoint3 lastPos{}, vel{};
			const RE::NiPoint3  pos = a_player->GetPosition();
			AwayTick(a_player, a_delta);
			if (!bank[0] && !bank[1]) {  // not gliding
				if (g_hadDragon) {  // the glider was just put away: the dragon lets go and flies off
					g_hadDragon = false;
					LetGo(a_player, vel, roll, baseScale);
				}
				lastYaw = yaw, lastZ = z, turn = 0, breath = 0, jaw = 0, breathIn = 4.0f, deployT = -1, roll = 0;
				g_armHoldOn = false, g_armHold = 0, g_flapT = -1, g_liftT = -1;
				if (g_liftOff == 2) {
					g_liftOff = 0;
				}
				lastPos = pos;
				return;
			}
			if (a_delta > 1e-3f) {
				vel = vel + ((pos - lastPos) * (1.0f / a_delta) - vel) * std::min(1.0f, a_delta * 6.0f);
			}
			lastPos = pos;
			// the glider opens: the dragon comes for you - it swoops in from high behind you on one side, banking round in
			// a curve with its wings beating, its hind legs reaching out ahead of it, and at the last moment its talons
			// close on your arms and it carries you on into the glide (every frame, eased; 1.4 seconds)
			if (deployT < 0) {
				deployT = 0;
				for (auto* sp : space) if (sp) baseScale = sp->local.scale, baseTrans = sp->local.translate;
			}
			deployT += a_delta;
			constexpr float kSwoop = 1.4f;
			// your arms stay down until it reaches you, then go up to its talons as it takes you
			g_armHoldOn = true;
			g_armHold = 1 - Smooth((deployT - (kSwoop - 0.38f)) / 0.38f);
			const float u = std::clamp(deployT / kSwoop, 0.0f, 1.0f);
			const float e = u * u * u * (u * (u * 6 - 15) + 10);  // smootherstep
			const RE::NiPoint3 P0{ 260, -720, 300 }, P1{ 330, -140, -30 }, P2{ 0, 0, 0 };
			const RE::NiPoint3 at = P0 * ((1 - e) * (1 - e)) + P1 * (2 * (1 - e) * e) + P2 * (e * e);
			RE::NiPoint3 tan = (P1 - P0) * (2 * (1 - e)) + (P2 - P1) * (2 * e);
			const float  inSwoop = 1 - std::clamp((u - 0.8f) / 0.2f, 0.0f, 1.0f);
			const float  swYaw = std::atan2(-tan.x, std::max(1.0f, tan.y)) * inSwoop;
			const float  swPitch = std::atan2(tan.z, std::sqrt(tan.x * tan.x + tan.y * tan.y)) * inSwoop;
			const float  swRoll = 0.55f * std::sin(3.14159f * std::min(1.0f, u * 1.15f)) * inSwoop;
			for (int i = 0; i < 2; ++i) {
				if (space[i]) {
					space[i]->local.translate = baseTrans + at;
					if (baseScale > 0) space[i]->local.scale = baseScale;
				}
				// Bond I's wing-beat: both wings thrown up, then a great stroke down
				float beat = 0;
				if (g_flapT >= 0) {
					beat = g_flapT < 0.22f ? 0.85f * std::sin(1.5708f * g_flapT / 0.22f)
					     : g_flapT < 0.55f ? 0.85f - 2.0f * Smooth((g_flapT - 0.22f) / 0.33f)
					                       : -1.15f * (1 - Smooth((g_flapT - 0.55f) / 0.35f));
				}
				if (fold[i][0]) fold[i][0]->local.rotate = Rot(0, beat, 0);
				if (fold[i][1]) fold[i][1]->local.rotate = Rot(0, -beat, 0);
			}
			// the legs: reaching out in the swoop, clutching as it takes you; then holding on, flexing their grip, now
			// and then shifting it (talons opening and closing); drawn up while it breathes fire
			{
				legT += a_delta;
				if ((regripIn -= a_delta) <= 0) {
					regrip = 0.0f;
					regripIn = std::uniform_real_distribution<float>(3.5f, 7.0f)(rng);
				}
				regrip += a_delta;
				const float reach = std::clamp((u - 0.35f) / 0.4f, 0.0f, 1.0f) * (1 - std::clamp((u - 0.88f) / 0.12f, 0.0f, 1.0f));
				const float clutch = std::clamp((u - 0.85f) / 0.15f, 0.0f, 1.0f);
				const float tuck = breathing_ ? std::min(1.0f, breathT_ * 3.0f) : 0.0f;
				const float open = regrip < 0.5f ? std::sin(3.14159f * regrip / 0.5f) : 0.0f;  // a re-grip: open and close
				for (int sd = 0; sd < 2; ++sd) {
					const float ph = sd * 1.3f;
					const float idleHip = 0.12f + 0.07f * std::sin(legT * 2.1f + ph), idleKnee = -0.10f + 0.08f * std::sin(legT * 2.1f + 0.7f + ph);
					const float idleFoot = 0.40f + 0.06f * std::sin(legT * 2.1f + 1.4f + ph) - 0.7f * open;
					float hip = idleHip * clutch + 0.75f * reach - 0.45f * tuck;
					float knee = idleKnee * clutch - 0.15f * reach + 0.6f * tuck;
					float foot = idleFoot * clutch - 0.6f * reach + 0.3f * tuck;
					if (u < 0.35f) {  // early in the swoop: trailing behind it as it dives
						const float trail = 1 - u / 0.35f;
						hip -= 0.5f * trail, knee += 0.3f * trail;
					}
					for (int i = 0; i < 2; ++i) {
						if (legN[i][sd][0]) legN[i][sd][0]->local.rotate = Rot(hip, 0, 0);
						if (legN[i][sd][1]) legN[i][sd][1]->local.rotate = Rot(knee, 0, 0);
						if (legN[i][sd][2]) legN[i][sd][2]->local.rotate = Rot(foot, 0, 0);
					}
				}
			}
			const float dt = std::max(1e-3f, a_delta);
			float dy = yaw - lastYaw;
			while (dy > 3.14159f) dy -= 6.28318f;
			while (dy < -3.14159f) dy += 6.28318f;
			lastYaw = yaw;
			turn += (std::clamp(dy / dt, -2.5f, 2.5f) - turn) * std::min(1.0f, dt * 4.0f);   // rad/s, smoothed
			const float fall = (z - lastZ) / dt;
			lastZ = z;
			// lean into the turn, nose down a little as you sink
			roll += (std::clamp(turn * 0.35f, -0.45f, 0.45f) - roll) * std::min(1.0f, dt * 3.0f);
			pitch += (std::clamp(fall * 0.0006f, -0.18f, 0.08f) - pitch) * std::min(1.0f, dt * 2.0f);
			// the head: toward where you look (the camera), into the turn, and wandering on its own
			wanderT += dt;
			float camYaw = yaw;
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
				const auto& m = cam->cameraRoot->world.rotate;
				camYaw = std::atan2(m.entry[0][1], m.entry[1][1]);
			}
			float rel = camYaw - yaw;
			while (rel > 3.14159f) rel -= 6.28318f;
			while (rel < -3.14159f) rel += 6.28318f;
			const float wanderYaw = 0.22f * std::sin(wanderT * 0.53f) + 0.12f * std::sin(wanderT * 1.31f + 1.7f);
			const float wanderPitch = 0.10f * std::sin(wanderT * 0.71f + 0.4f) - (breath > 0 ? 0.18f : 0.0f);
			lookYaw += (std::clamp(-rel * 0.6f + turn * 0.25f + wanderYaw, -0.7f, 0.7f) - lookYaw) * std::min(1.0f, dt * 2.5f);
			lookPitch += (wanderPitch - lookPitch) * std::min(1.0f, dt * 2.5f);
			// fire: now and then, a breath of a second and a half (Bond III: whenever the dead are below, a few seconds apart)
			static float       sinceBreath = 0, seekT = 0;
			static RE::ActorHandle target;
			const bool         thrall = Bond() >= 3 && mouth != nullptr;
			sinceBreath += dt;
			if (thrall && (seekT -= dt) <= 0) {
				seekT = 0.25f;
				auto* t = BreathTarget(a_player, mouth->world.translate);
				target = t ? t->GetHandle() : RE::ActorHandle{};
			}
			if (breath <= 0 && thrall && target.get() && sinceBreath > 3.2f && deployT > kSwoop) {
				breathIn = 0;
			}
			if (breath <= 0 && (breathIn -= dt) <= 0) {
				breath = 1.6f;
				sinceBreath = 0;
				breathIn = std::uniform_real_distribution<float>(6.0f, 11.0f)(rng);
			}
			const bool breathing = breath > 0;
			if (breathing) breath -= dt;
			breathing_ = breathing, breathT_ = breathing ? breathT_ + dt : 0.0f;
			jaw += ((breathing || breathIn < 0.35f ? 0.38f : 0.03f * (0.5f + 0.5f * std::sin(wanderT * 0.9f))) - jaw) * std::min(1.0f, dt * 8.0f);
			for (int i = 0; i < 2; ++i) {
				if (bank[i]) {
					bank[i]->local.rotate = Rot(pitch * (1 - inSwoop) + swPitch, roll * (1 - inSwoop) + swRoll, swYaw);
				}
				if (look[i]) {
					look[i]->local.rotate = Rot(lookPitch, 0, lookYaw);
				}
				if (jawN[i]) {
					jawN[i]->local.rotate = Rot(-jaw, 0, 0);
				}
			}
			// the bonds' flight: Bond I glides quicker and sinks slower, beats its wings to climb; Bond II's take-off hangs you
			// there while it comes, then carries you up
			if (g_flapT >= 0 && (g_flapT += dt) > 0.9f) {
				g_flapT = -1;
			}
			if (auto* cc = a_player->GetCharController(); cc && Bond() >= 1 && !g_dive) {
				static RE::NiPoint3 glide{};
				RE::NiPoint3        v{};
				GetVel(cc, v);
				if (deployT <= a_delta * 1.5f) {
					glide = { v.x, v.y, 0 };
				}
				const RE::NiPoint3 heading{ std::sin(yaw), std::cos(yaw), 0 };
				const bool  hanging = g_liftOff == 2 && deployT < kSwoop * 0.88f;
				const float want = hanging ? 0.0f : 950.0f;
				glide = glide + (heading * want - glide) * std::min(1.0f, dt * (hanging ? 4.0f : 0.9f));
				v.x = glide.x, v.y = glide.y;
				v.z = std::max(v.z, -105.0f);  // (the glide alone sinks about half again faster)
				if (hanging) {
					v.z = 12.0f;
				} else if (g_liftOff == 2) {  // its talons close: up you go
					g_liftOff = 0, g_liftT = 0;
					Forms::PlayAt(gen::Sound::shout_dragon, a_player);
				}
				if (g_liftT >= 0) {
					g_liftT += dt;
					v.z = std::max(v.z, 1150.0f * (1 - Smooth(g_liftT / 1.3f)));
					if (g_liftT > 1.3f) g_liftT = -1;
				}
				if (g_flapT >= 0 && g_flapT > 0.22f) {  // the downstroke lifts you
					v.z = std::max(v.z, 720.0f * (1 - Smooth((g_flapT - 0.22f) / 0.68f)));
				}
				SetVel(cc, v);
			}
			{  // where it is now: if the glider goes away this frame, the dragon in the world starts from here
				const bool first = RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
				const int  k = (first && space[1]) || !space[0] ? 1 : 0;
				if (space[k]) {
					g_lastSpace = space[k]->world;
					g_hadDragon = true;
					if (bank[k]) g_lastBank = bank[k]->local.rotate;
				}
			}
			static float hitT = 0;
			if (breathing && breath < 1.45f && mouth && head && (shotT -= dt) <= 0) {
				shotT = 0.05f;
				const int          el = thrall ? HandElement() : 0;
				RE::BGSProjectile* fire = BreathProjectile(el);  // the dragons' own breath (Bond III: your spell's element)
				if (fire) {
					const auto&        hm = head->world.rotate;
					RE::NiPoint3       d{ hm.entry[0][1], hm.entry[1][1], hm.entry[2][1] - 0.25f };  // its head's forward, a little down
					if (auto t = target.get(); thrall && t && !t->IsDead()) {  // Bond III: at the dead it found
						d = t->GetPosition() + RE::NiPoint3{ 0, 0, 50 } - mouth->world.translate;
					}
					d.Unitize();
					if (thrall && (hitT -= 0.05f) <= 0) {
						hitT = 0.2f;
						BreathHits(a_player, mouth->world.translate, d, el);
					}
					const RE::NiPoint3 from = mouth->world.translate + d * 10.0f;
					const float        flat = std::sqrt(d.x * d.x + d.y * d.y);
					RE::Projectile::ProjectileRot r{ -std::atan2(d.z, std::max(1e-3f, flat)), std::atan2(d.x, d.y) };
					RE::Projectile::LaunchData    ld(fire, a_player, from, r);
					ld.useOrigin = true;
					ld.autoAim = false;
					ld.noDamageOutsideCombat = true;
					ld.power = 0.0f;
					RE::ProjectileHandle h;
					RE::Projectile::Launch(&h, ld);
				}
			}
		}

		void Glider(RE::PlayerCharacter* a_player, float a_delta)
		{
			static float      every = 0;
			static RE::TESBoundObject* item = nullptr;
			static int        tries = 0;
			if ((every -= a_delta) > 0) {
				return;
			}
			every = 3.0f;
			if (!item && tries < 40) {  // (looked for again until found: the first look can come before the plugins are in)
				++tries;
				if (auto* dh = RE::TESDataHandler::GetSingleton()) {
					const auto* mod = dh->LookupModByName("Paragliding.esp");
					item = dh->LookupForm<RE::TESBoundObject>(0x802, "Paragliding.esp");
					if (!item && mod) {  // by its name among the misc items of that plugin
						for (auto* m : dh->GetFormArray<RE::TESObjectMISC>()) {
							if (m && m->GetFile(0) == mod && m->GetName() && std::string_view(m->GetName()).find("aragl") != std::string_view::npos) {
								item = m;
								break;
							}
						}
					}
					if (item || tries == 1 || tries == 40) {
						logger::info("glider: Paragliding.esp {} (loaded {}, index {:X}), the glider {:08X}", item ? "found" : "not found", mod != nullptr,
							mod ? (mod->IsLight() ? 0xFE : mod->GetCompileIndex()) : 0xFF, item ? item->GetFormID() : 0);
					}
				}
			}
			if (!item || !a_player->Is3DLoaded()) {
				return;
			}
			const auto have = a_player->GetInventoryCounts([](RE::TESBoundObject& o) { return &o == item; });
			if (have.empty()) {
				a_player->AddObjectToContainer(item, nullptr, 1, nullptr);
				logger::info("glider: the paraglider put in your pack");
			}
			std::int32_t on = 0;
			if (!a_player->GetGraphVariableInt("hasparaglider", on) || on != 1) {
				a_player->SetGraphVariableInt("hasparaglider", 1);
			}
		}

		bool BondLocked()
		{
			if (Bond() != 2) {
				return false;
			}
			for (int t = 0; t < static_cast<int>(gen::kTowns.size()); ++t) {
				if (Curse::Freed(t)) {
					return false;
				}
			}
			return true;
		}

		void BuyBond(RE::TESObjectREFR* a_shrine)
		{
			static constexpr const char* kName[3] = { "WIND-RIDER", "TALON STRIKE", "ALDUIN'S THRALL" };
			static constexpr const char* kHow[3] = { "Jump while gliding: it beats its wings and climbs",
				"Sprint + Activate on open ground: it lifts you  -  attack looking down mid-glide: the dive",
				"Its breath burns the dead below, in the element of the spell in your hand" };
			const int b = Bond();
			if (b >= 3) {
				Hud::Banner("Your dragon has made every bond", 2.5f);
				return;
			}
			if (BondLocked()) {
				Hud::Banner("The third bond waits until a city is freed from the Dirge", 3.0f);
				return;
			}
			if (!Points::Spend(kBondCost[b])) {
				return;
			}
			Save::SetDragonBond(b + 1);
			ApplyBondLook();
			if (a_shrine) {
				Forms::PlayAt(gen::Sound::shout_dragon, a_shrine);
			}
			logger::info("glider: dragon bond {} made", b + 1);
			Hud::Banner(std::string("DRAGON BOND: ") + kName[b] + "  -  " + kHow[b], 5.0f);
		}

		bool Riding()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p && p->IsOnMount();
		}

		void OnButton(const RE::ButtonEvent* a_event)
		{
			// TK Dodge on a controller: it only knows its keyboard key (Left Alt), so a quick tap of the pad's sprint
			// button (LB / L1) presses that key for it - a tap dodges, holding still sprints
			if (a_event && a_event->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
				static std::chrono::steady_clock::time_point down{};
				static bool held = false;
				auto* ue0 = RE::UserEvents::GetSingleton();
				if (ue0 && a_event->QUserEvent() == ue0->sprint) {
					if (a_event->IsDown()) {
						down = std::chrono::steady_clock::now(), held = true;
					} else if (a_event->IsUp() && held) {
						held = false;
						if (std::chrono::steady_clock::now() - down < std::chrono::milliseconds(220) && !Riding()) {
							DevTools::PressKey(RE::INPUT_DEVICE::kKeyboard, 0x38, 0.05f);  // (TK Dodge RE.ini DodgeHotkey = 56)
						}
					}
				}
			}
			if (!a_event || !a_event->IsDown()) {
				return;
			}
			auto*       ue = RE::UserEvents::GetSingleton();
			auto*       player = RE::PlayerCharacter::GetSingleton();
			const auto& ev = a_event->QUserEvent();
			if (!ue || !player) {
				return;
			}
			// the dragon's bonds
			if (const int bond = Bond(); bond >= 1) {
				auto* ui = RE::UI::GetSingleton();
				const bool free = !ui || !ui->GameIsPaused();
				if (free && ev == ue->jump && g_hadDragon && g_flapT < 0 && g_flapWait <= 0) {  // I: a wing-beat
					g_flapT = 0, g_flapWait = 3.5f;
					Forms::PlayAt(gen::Sound::shout_gale, player);
				}
				if (free && bond >= 2 && ev == ue->activate && !g_hadDragon && g_liftOff == 0 && OnGround(player) &&
					player->AsActorState()->IsSprinting()) {  // II: take-off, when Activate has nothing to take
					auto* pick = RE::CrosshairPickData::GetSingleton();
					if (!pick || !pick->target.get()) {
						if (player->GetCharController()) {
							DevTools::PressKey(RE::INPUT_DEVICE::kKeyboard, 0x39, 0.05f);  // a jump (Space), carried higher below
							g_liftOff = 1, g_takeT = 0;
							Forms::PlayAt(gen::Sound::shout_gale, player);
							logger::info("glider: take-off");
						}
					}
				}
				if (free && bond >= 2 && (ev == ue->rightAttack || ev == ue->leftAttack) && g_hadDragon && !g_dive) {  // II: the dive
					float down = 0;
					if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
						down = -cam->cameraRoot->world.rotate.entry[2][1];
					}
					if (down > 0.42f) {
						g_dive = true, g_diveT = 0;
						DevTools::PressKey(RE::INPUT_DEVICE::kKeyboard, 0x12, 0.05f);  // (the glider closed: it lets you go)
						Forms::PlayAt(gen::Sound::shout_gale, player);
						logger::info("glider: dive");
					}
				}
			}
			// (the slide is gone: crouch is Skyrim's crouch again)
			if (ev == ue->jump && g_slide > 0) {
				g_wantJumpCarry = true;
			}
		}

		void Reset()
		{
			g_applied = 0, g_slide = 0, g_carry = 0, g_air = 0, g_smooth = 0;
			g_wasAir = false, g_wantSlide = false, g_wantJumpCarry = false, g_forcedSneak = false;
		}

		// After the animation each frame (Gun's pose hook): while the dragon is still on its way your arms keep the pose
		// they had before the glider opened (the glide's own animation raises them at once); as it reaches you they
		// go up to its talons
		void PoseGlideArms(RE::PlayerCharacter* a_player)
		{
			static constexpr const char* kBones[6] = { "NPC L Clavicle [LClv]", "NPC R Clavicle [RClv]", "NPC L UpperArm [LUar]",
				"NPC R UpperArm [RUar]", "NPC L Forearm [LLar]", "NPC R Forearm [RLar]" };
			constexpr int                kHist = 10;  // (a few frames back: the glide's raise can start a frame before we see the dragon)
			static RE::NiMatrix3         hist[2][6][kHist]{};
			static int                   n[2]{}, head[2]{};
			static RE::NiMatrix3         held[2][6]{};
			static bool                  holding[2]{};
			for (int f = 0; f < 2; ++f) {
				auto* root = a_player->Get3D(f == 1);
				if (!root) {
					continue;
				}
				RE::NiAVObject* b[6]{};
				bool            all = true;
				for (int i = 0; i < 6; ++i) {
					b[i] = root->GetObjectByName(kBones[i]);
					all = all && b[i];
				}
				if (!all) {
					continue;
				}
				if (!g_armHoldOn) {
					holding[f] = false;
					for (int i = 0; i < 6; ++i) hist[f][i][head[f]] = b[i]->local.rotate;
					head[f] = (head[f] + 1) % kHist;
					n[f] = std::min(n[f] + 1, kHist);
					continue;
				}
				if (!holding[f]) {
					holding[f] = true;
					const int oldest = n[f] < kHist ? 0 : head[f];
					for (int i = 0; i < 6; ++i) held[f][i] = n[f] > 0 ? hist[f][i][oldest] : b[i]->local.rotate;
				}
				n[f] = 0, head[f] = 0;
				if (g_armHold <= 0) {
					continue;
				}
				for (int i = 0; i < 6; ++i) b[i]->local.rotate = Slerp(b[i]->local.rotate, held[f][i], g_armHold);
				RE::NiUpdateData ud;
				ud.time = 0.0f;
				b[0]->UpdateDownwardPass(ud, 0);
				b[1]->UpdateDownwardPass(ud, 0);
			}
		}

		void Tick(float a_delta)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_delta <= 0) {
				return;
			}
			TuneGameSettings();
			TuneMovementTypes();
			auto*      st = player->AsActorState();
			auto*      cc = player->GetCharController();
			const bool sprint = st->IsSprinting();
			Glider(player, a_delta);
			BondTick(player, a_delta);
			Dragon(player, a_delta);
			if (cc) {  // no fall damage at all: the fall is always measured from where you are now, so every landing is from nowhere
				// (the fall's time is left alone: Better Jumping allows a jump while it is short - zeroing it gave endless jumps)
				cc->fallStartHeight = player->GetPositionZ();
			}
			const bool air = cc && cc->context.currentState != RE::hkpCharacterStateType::kOnGround;
			g_air = air ? g_air + a_delta : 0;
			RE::NiPoint3 v{};
			if (cc) {
				GetVel(cc, v);
			}
			const RE::NiPoint3 hv{ v.x, v.y, 0 };
			const float        hs = std::sqrt(hv.x * hv.x + hv.y * hv.y);

			// ---- slide: sprint + crouch. You keep (and boost) your speed, can steer a little, and it eases off.
			if (g_wantSlide) {
				g_wantSlide = false;
				if (!air && cc) {
					g_slide = gen::kSlideSeconds;
					g_slideDir = hs > 50.0f ? hv / hs : Heading(player);
					if (auto w = WishDir(player); w.x != 0 || w.y != 0) {
						g_slideDir = w;
					}
					g_slideSpeed = std::max(gen::kSlideSpeed, hs * 1.2f);
					// the crouch key also toggles Skyrim's sneak: the slide owns that crouch and stands you up after
					st->actorState1.sneaking = 1;
					player->SetGraphVariableBool("IsSneaking", true);
					player->NotifyAnimationGraph("SneakStart");
					g_forcedSneak = true;
					Forms::Play(gen::Sound::knife);  // whoosh
				}
			}
			const bool sliding = g_slide > 0;
			if (sliding && cc && !air) {
				// steer toward the stick, slowly (about 70 degrees a second)
				if (auto w = WishDir(player); w.x != 0 || w.y != 0) {
					const float cross = g_slideDir.x * w.y - g_slideDir.y * w.x, dot = g_slideDir.x * w.x + g_slideDir.y * w.y;
					const float turn = std::clamp(std::atan2(cross, dot), -1.2f * a_delta, 1.2f * a_delta);
					const float c = std::cos(turn), sn = std::sin(turn);
					g_slideDir = { g_slideDir.x * c - g_slideDir.y * sn, g_slideDir.x * sn + g_slideDir.y * c, 0 };
				}
				// ease out: full speed first, settling to ~60% by the end (a smooth curve, no step)
				const float t = 1.0f - g_slide / gen::kSlideSeconds;
				const float speed = g_slideSpeed * (1.0f - 0.4f * t * t);
				SetVel(cc, { g_slideDir.x * speed, g_slideDir.y * speed, std::min(v.z, 0.0f) });
			}

			// ---- momentum: whatever speed you leave the ground with (run, sprint or slide), you keep in the air
			if (!air) {
				g_groundVel = sliding ? g_slideDir * (g_slideSpeed * (1.0f - 0.4f * std::pow(1.0f - g_slide / gen::kSlideSeconds, 2.0f))) : hv;
			}
			if (air && !g_wasAir) {
				g_takeoff = g_groundVel * gen::kSlideJump;
				if (sliding) {  // jumping cancels the slide (stand up in the air)
					g_slide = 0.0001f;
				}
			}
			if (cc && air && !(g_hadDragon && Bond() >= 1) && !g_dive && g_liftOff == 0) {  // (the dragon's bonds fly you themselves)
				const float ts = std::sqrt(g_takeoff.x * g_takeoff.x + g_takeoff.y * g_takeoff.y);
				RE::NiPoint3 h = hv;
				// Skyrim's jump drops your run speed on takeoff: the first moments of the jump keep what you had
				if (g_air < 0.25f && hs < ts * 0.97f) {
					h = g_takeoff;
				}
				// air strafing: small steering, never faster than you took off (or the sheet's cap)
				if (gen::kAirControl > 0 && g_air > 0.12f) {
					if (auto w = WishDir(player); w.x != 0 || w.y != 0) {
						h = { h.x + w.x * gen::kAirControl * a_delta, h.y + w.y * gen::kAirControl * a_delta, 0 };
						const float cap = std::max({ gen::kAirMax, ts, 300.0f }), after = std::sqrt(h.x * h.x + h.y * h.y);
						if (after > cap) {
							h = h * (cap / after);
						}
					}
				}
				if (std::abs(h.x - hv.x) + std::abs(h.y - hv.y) > 1.0f) {
					SetVel(cc, { h.x, h.y, v.z });
				}
			}
			g_wasAir = air;
			g_wantJumpCarry = false;

			const float before = g_slide;
			g_slide = std::max(0.0f, g_slide - a_delta);
			if (before > 0 && g_slide <= 0 && g_forcedSneak) {
				st->actorState1.sneaking = 0;  // stand back up
				player->SetGraphVariableBool("IsSneaking", false);
				player->NotifyAnimationGraph("SneakStop");
				g_forcedSneak = false;
			}

			// ---- run/sprint speed, eased so switching sprint <-> run <-> strafe never jolts
			float want = gen::kRunBoost + (sprint ? gen::kSprintBoost : 0.0f) + gen::kAdsSpeed * Gun::AdsBlend();
			if (g_slide > 0) {
				want += gen::kSprintBoost;  // the crouch would otherwise drag the controller back to sneak speed
			}
			g_smooth += (want - g_smooth) * std::min(1.0f, a_delta * gen::kGroundAccel * 0.5f);
			Apply(g_smooth);
		}
	}
}
