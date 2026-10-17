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
			RE::NiPoint3        fwd{ 0, 1, 0 };  // which way it flies on (level, in the world)
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

		// a turn of a_angle about a world axis (right-handed)
		RE::NiMatrix3 AxisAngle(RE::NiPoint3 a_axis, float a_angle)
		{
			a_axis.Unitize();
			const float   c = std::cos(a_angle), s = std::sin(a_angle), t = 1 - c, x = a_axis.x, y = a_axis.y, z = a_axis.z;
			RE::NiMatrix3 m;
			m.entry[0][0] = t * x * x + c, m.entry[0][1] = t * x * y - s * z, m.entry[0][2] = t * x * z + s * y;
			m.entry[1][0] = t * x * y + s * z, m.entry[1][1] = t * y * y + c, m.entry[1][2] = t * y * z - s * x;
			m.entry[2][0] = t * x * z - s * y, m.entry[2][1] = t * y * z + s * x, m.entry[2][2] = t * z * z + c;
			return m;
		}

		// Skyrim's own dragon sounds (Skyrim.esm), played at a reference (or kept, for a loop that must be stopped)
		namespace DragonSound
		{
			constexpr RE::FormID kRoar = 0x0003F35F, kFlap = 0x0003CE00, kFlapBig = 0x0003D122, kFire = 0x0003EF56, kLand = 0x0003F1F4;
		}
		bool PlayGameSound(RE::FormID a_id, RE::TESObjectREFR* a_at, RE::BSSoundHandle* a_keep = nullptr, float a_vol = 1.0f)
		{
			auto* d = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(a_id);
			auto* am = RE::BSAudioManager::GetSingleton();
			RE::BSSoundHandle h;
			RE::BSSoundHandle& hh = a_keep ? *a_keep : h;
			if (!d || !am || !am->GetSoundHandle(hh, static_cast<RE::BSISoundDescriptor*>(d))) {
				return false;
			}
			if (a_at) {
				hh.SetPosition(a_at->GetPosition());
				if (auto* node = a_at->Get3D()) {
					hh.SetObjectToFollow(node);
				}
			}
			if (a_vol < 0.999f) {
				hh.SetVolume(a_vol);
			}
			return hh.Play();
		}
		constexpr float kDragonScale = 0.23f;  // DG_Space's size in the dragon's nif (its copy on you starts at 0: hidden until placed)

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

		// ---- the wings (Noah: they beat only for a climb - Bond I's wing-beat, the take-off, the fly-off - and are held
		// out straight in the glide, swaying a little; folded back in a Bond III dive). The nif no longer keys them.
		// One wing-beat over a_len seconds: from rest up to the top (+1), a hard stroke down (-1.3), back to rest (0)
		float WingBeat(float a_t, float a_len)
		{
			if (a_t <= 0 || a_t >= a_len) {
				return 0.0f;
			}
			const float u = a_t / a_len;
			if (u < 0.26f) return 1.1f * std::sin(1.5708f * u / 0.26f);
			if (u < 0.62f) return 1.1f - 2.4f * Smooth((u - 0.26f) / 0.36f);
			return -1.3f * (1 - Smooth((u - 0.62f) / 0.38f));
		}
		// a_beat(lag): the beat's value a_lag seconds behind now (the elbow and wrist follow the shoulder like a whip)
		template <class F>
		void WingPose(RE::NiAVObject* a_root, float a_clock, F&& a_beat, float a_fold)
		{
			if (!a_root) {
				return;
			}
			static const char* names[2][3] = { { "DG_WingL1", "DG_WingL2", "DG_WingL3" }, { "DG_WingR1", "DG_WingR2", "DG_WingR3" } };
			constexpr float rest[3] = { 2.0f, 0.0f, 14.0f };     // (its bind droops at the tip: the wrist lifts it level)
			constexpr float amps[3] = { 56.0f, 34.0f, 24.0f };
			constexpr float lag[3] = { 0.0f, 0.07f, 0.14f };
			constexpr float foldBack[3] = { 22.0f, 30.0f, 36.0f }, foldRoll[3] = { 6.0f, -12.0f, -10.0f };
			constexpr float d = 3.14159265f / 180.0f;
			for (int sd = 0; sd < 2; ++sd) {
				const float sg = sd == 0 ? 1.0f : -1.0f;  // the left wing: +roll lifts it, +turn sweeps it back
				for (int j = 0; j < 3; ++j) {
					auto* n = a_root->GetObjectByName(names[sd][j]);
					if (!n) continue;
					const float b = a_beat(lag[j]), rising = a_beat(lag[j] - 0.02f) - b;
					const float sway = (j == 0 ? 2.5f : 1.5f) * std::sin(6.28318f * a_clock / 3.0f + j * 0.6f + sd * 0.5f);
					const float roll = rest[j] * (1 - a_fold) + sway * (1 - 0.6f * a_fold) + amps[j] * b + foldRoll[j] * a_fold;
					const float back = (rising > 0 ? (j == 0 ? 6.0f : 12.0f) * std::min(1.0f, std::abs(b) + 0.3f) : 0.0f) + foldBack[j] * a_fold;
					const float twist = (rising < 0 ? (j == 2 ? 7.0f : 3.0f) * std::abs(b) : 0.0f) + 1.2f * std::sin(6.28318f * a_clock / 3.0f + 1.0f + j);
					n->local.rotate = Rot(twist * d, sg * roll * d, sg * back * d);
				}
			}
		}
		// the wind's strips in the dragon's nif (tools/dragonfx.py): how bright (0: unseen)
		void FxLevel(RE::NiAVObject* a_root, const char* a_name, float a_level, float a_scrollV = -99.0f)
		{
			auto* o = a_root ? a_root->GetObjectByName(a_name) : nullptr;
			auto* g = o ? o->AsGeometry() : nullptr;
			if (!g) {
				return;
			}
			auto* p = g->GetGeometryRuntimeData().shaderProperty.get();
			auto* rtti = p ? p->GetRTTI() : nullptr;
			if (!rtti || std::string_view(rtti->GetName()) != "BSEffectShaderProperty") {
				return;
			}
			auto* m = static_cast<RE::BSShaderProperty*>(p)->material;
			if (m) {
				static_cast<RE::BSEffectShaderMaterial*>(m)->baseColorScale = a_level;
				if (a_scrollV > -98.0f) {  // the air streaming along it (set here each frame: always moving)
					m->texCoordOffset[0].y = a_scrollV;
				}
			}
			o->SetAppCulled(a_level < 0.01f);
		}
		// (Noah: both animated - the air streams back along the wingtip trails, and the dive's lines rush in from ahead of
		// the dragon and past it)
		void DragonWind(RE::NiAVObject* a_root, float a_trail, float a_dive)
		{
			static float clock = 0;
			static auto  last = std::chrono::steady_clock::now();
			const auto   now = std::chrono::steady_clock::now();
			const float  dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
			last = now;
			clock += dt;
			const float trailV = -std::fmod(clock * 1.6f, 1.0f), diveV = -std::fmod(clock * 3.2f, 1.0f);
			FxLevel(a_root, "DG_TrailL", a_trail, trailV);
			FxLevel(a_root, "DG_TrailR", a_trail, trailV + 0.37f);
			for (int i = 1; i <= 8; ++i) {
				FxLevel(a_root, fmt::format("DG_Wind{}", i).c_str(), a_dive, diveV + 0.13f * i);
			}
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
			if (t > 4.5f) {
				ref->Disable();
				ref->SetDelete(true);
				g_away = {};
				return;
			}
			auto* root = ref->Get3D();
			static float logT = 0;
			if ((logT -= a_delta) <= 0 || t == a_delta) {
				logT = 0.5f;
				logger::info("glider: fly-off t {:.2f} 3D {} at ({:.0f},{:.0f},{:.0f}) ref ({:.0f},{:.0f},{:.0f}) disabled {} bound ({:.0f},{:.0f},{:.0f}) r {:.0f} scale {:.2f} parent {} culled {}", t, root != nullptr,
					root ? root->world.translate.x : 0.0f, root ? root->world.translate.y : 0.0f, root ? root->world.translate.z : 0.0f,
					ref->GetPositionX(), ref->GetPositionY(), ref->GetPositionZ(), ref->IsDisabled(),
					root ? root->worldBound.center.x : 0.0f, root ? root->worldBound.center.y : 0.0f, root ? root->worldBound.center.z : 0.0f, root ? root->worldBound.radius : 0.0f,
					root ? root->world.scale : 0.0f, root && root->parent ? root->parent->name.c_str() : "-", root ? root->GetAppCulled() : false);
			}
			if (!root) {
				return;  // (still loading: it catches up with its path when it appears)
			}
			// (in the world's own axes: the dragon's frame on you is not upright - its own "up" can point anywhere)
			const auto&        R0 = g_away.start.rotate;
			const RE::NiPoint3 fwd = g_away.fwd, up{ 0, 0, 1 };
			const RE::NiPoint3 right = fwd.Cross(up);
			const float        sd = g_away.side;
			// your momentum, then its own: a lurch up as your weight leaves it, a climb, and a curve off to one side
			const RE::NiPoint3 pos = g_away.start.translate + g_away.vel * (t * 0.85f) + fwd * (260 * t + 170 * t * t) +
			                         up * (90 * std::sin(std::min(t, 0.6f) * 2.6f) + 150 * t * t) + right * (sd * 95 * t * t);
			const float climb = Smooth(t / 1.3f);
			const float roll = sd * 0.75f * std::sin(3.14159f * std::min(1.0f, t / 2.8f));
			ref->SetPosition(pos);  // (the reference itself moves: only moving its 3D left it culled where it was placed)
			root->local.translate = pos;
			root->local.rotate = AxisAngle(up, -sd * 0.55f * climb) * AxisAngle(fwd, roll) * AxisAngle(right, 0.42f * climb) * R0;
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
				const float toes = -1.1f * (1 - Smooth(t / 0.35f)) + 0.4f * Smooth(t / 0.35f) * (1 - tuck) - 0.6f * tuck;  // closed -> open -> folded
				if (auto* n = root->GetObjectByName(fmt::format("DG_Toe{}F", L).c_str())) n->local.rotate = Rot(toes, 0, 0);
				if (auto* n = root->GetObjectByName(fmt::format("DG_Toe{}B", L).c_str())) n->local.rotate = Rot(-toes, 0, 0);
			}
			// the climb away: strong wing-beats (1.1 s each), then out into a glide
			WingPose(root, t, [t](float a_lag) { const float k = t - 0.15f - a_lag; return k < 3.3f ? WingBeat(std::fmod(std::max(k, 0.0f), 1.1f), 1.1f) : 0.0f; }, 0.0f);
			DragonWind(root, 0.9f * (1 - Smooth((t - 3.2f) / 1.0f)), 0.0f);
			{
				static float lastBeat = 0;
				const float  k = std::fmod(std::max(t - 0.15f, 0.0f), 1.1f);
				if (t > 1.0f && t < 3.4f && lastBeat < 0.4f && k >= 0.4f) {
					PlayGameSound(DragonSound::kFlap, ref.get(), nullptr, 0.7f);
				}
				lastBeat = k;
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
				logger::warn("glider: the fly-off dragon's record is missing");
				return;
			}
			auto ref = a_player->PlaceObjectAtMe(st, false);
			if (!ref) {
				return;
			}
			ref->SetPosition(g_lastSpace.translate);
			logger::info("glider: the dragon lets go at ({:.0f},{:.0f},{:.0f}), scale {:.3f}/{:.3f}", g_lastSpace.translate.x, g_lastSpace.translate.y, g_lastSpace.translate.z, g_lastSpace.scale, a_baseScale);
			static std::mt19937 rng{ std::random_device{}() };
			g_away.ref = ref->CreateRefHandle();
			g_away.t = 0;
			g_away.start = g_lastSpace;
			g_away.bank = g_lastBank;
			g_away.vel = a_vel;
			{
				RE::NiPoint3 f{ a_vel.x, a_vel.y, 0 };
				if (f.Length() < 100.0f) {  // (landed: it flies off ahead of your view, where you can see it go)
					float yaw = a_player->GetAngleZ();
					if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
						const auto& m = cam->cameraRoot->world.rotate;
						yaw = std::atan2(m.entry[0][1], m.entry[1][1]);
					}
					f = { std::sin(yaw), std::cos(yaw), 0 };
				}
				f.Unitize();
				g_away.fwd = f;
				logger::info("glider: it flies off toward ({:.2f},{:.2f})", f.x, f.y);
			}
			g_away.scale = a_baseScale > 0 ? g_lastSpace.scale / a_baseScale : 1.0f;
			g_away.side = std::abs(a_roll) > 0.05f ? (a_roll > 0 ? 1.0f : -1.0f) : (rng() % 2 ? 1.0f : -1.0f);
			PlayGameSound(DragonSound::kFlapBig, ref.get());
			PlayGameSound(DragonSound::kRoar, ref.get(), nullptr, 0.8f);
		}

		// ---- the Echo Altar's dragon bonds (Noah): three, bought in order at any Echo Altar, kept for the whole game
		//   I   Wind-Rider     a quicker glide that sinks slower; jump mid-glide and it beats its wings to climb (a few
		//                      seconds between beats). Its eyes glow ice-pale.
		//   II  Talon Strike   the shout Dragon's Updraft (learned with the bond): a whirlwind lifts you high, open the glider; attack
		//                      while looking down mid-glide and it hurls you at the ground - you land in a shockwave that
		//                      throws the dead about. Its markings turn glacier blue.
		//   III Alduin's Thrall (needs a city freed from the Dirge) black scales, ember eyes - its fire breath is real now:
		//                      it turns on the dead below you, in the element of the spell in your hand (fire burns
		//                      hotter, frost slows them, lightning leaps to the next).
		// Its look: Paragliding.esp's animated object (ANIO 0x800) is given the bond's nif before you glide, the dragon
		// that flies off afterwards the same (tools/modelconv/dragonvariant.cpp, tools/dragontex.py).
		float g_flapT = -1, g_flapWait = 0;  // Bond I: the wing-beats' time, and the wait before the next
		bool  g_breathReq = false;           // Bond III: the breath asked for (right attack mid-glide)
		float g_breathCd = 0;                // ... and the 5 s before it can be again
		int   g_breathElDev = -1;            // (dev: the element to breathe, whatever is in your hand)
		RE::FormID g_breathProjDev[3]{};     // (dev: a projectile to try for fire / frost / lightning)
		float g_breathScaleDev = -1, g_beamPitchSign = 1;
		// (Noah) Bond I's climb: two small beats that lift it a little, then one great beat that carries it higher
		constexpr float kFlapSmall = 0.5f, kFlapBig = 1.15f, kFlapAll = 2 * kFlapSmall + kFlapBig;
		float FlapWing(float a_t)  // the wings' beat (WingBeat units) a_t seconds into the climb
		{
			if (a_t < 2 * kFlapSmall) return a_t < 0 ? 0.0f : 0.55f * WingBeat(std::fmod(a_t, kFlapSmall), kFlapSmall);
			return WingBeat(a_t - 2 * kFlapSmall, kFlapBig);
		}
		float FlapLift(float a_t)  // the least you rise at, each downstroke lifting you
		{
			if (a_t < 0 || a_t > kFlapAll) return -1e9f;
			if (a_t < 2 * kFlapSmall) {
				const float k = std::fmod(a_t, kFlapSmall);
				return k < 0.13f ? -1e9f : 420.0f * (1 - Smooth((k - 0.13f) / 0.37f));
			}
			const float k = a_t - 2 * kFlapSmall;
			return k < 0.3f ? -1e9f : 980.0f * (1 - Smooth((k - 0.3f) / 0.85f));
		}
		float FlapTilt(float a_t)  // its nose lifted as it beats (rad)
		{
			if (a_t < 0 || a_t > kFlapAll) return 0.0f;
			if (a_t < 2 * kFlapSmall) return 0.12f * Smooth(a_t / 0.2f);
			const float k = (a_t - 2 * kFlapSmall) / kFlapBig;
			return 0.12f + 0.2f * std::sin(3.14159f * std::min(1.0f, k * 1.2f)) - 0.12f * Smooth((k - 0.6f) / 0.4f);
		}
		int   g_liftOff = 0;                 // Bond II: 1 the Updraft's whirlwind is carrying you up
		float g_updraftAge = 99;
		bool  g_glideQueued = false;         // Activate pressed on the Updraft's way up: the glider opens at its top             // seconds since the Updraft (a glider opened soon after: the dragon comes quicker)
		struct Whirl  // (hung on your own 3D - both skeletons - so it is drawn wherever you are)
		{
			RE::NiPointer<RE::NiAVObject> node[2];
			float                         t = -1;
		};
		Whirl g_whirl;
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
			PlayGameSound(DragonSound::kLand, a_player);
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
			if (a_element >= 0 && a_element < 3 && g_breathProjDev[a_element]) {  // (dev: trying another look)
				if (auto* p = RE::TESForm::LookupByID<RE::BGSProjectile>(g_breathProjDev[a_element])) return p;
			}
			if (a_element <= 0 || a_element >= 8) {
				return fire;
			}
			// frost: the Frost Breath shout's own cone (as fire is the dragons' fire breath)
			if (a_element == 1) {
				static RE::BGSProjectile* frost = RE::TESForm::LookupByID<RE::BGSProjectile>(0x00068BD1);
				if (frost) return frost;
			}
			// lightning: a stream of our own lightning bolts (bullet_shock, id frozen in data/form_ids.json)
			if (a_element == 2) {
				static RE::BGSProjectile* bolt[3]{};
				auto*&                    b = bolt[a_element];
				if (!b) b = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSProjectile>(a_element == 1 ? 0x9E3 : 0x9E6, gen::kPlugin);
				if (b) return b;
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
				float      dmg = undead ? Director::RoundHealth() * 0.25f : a->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.05f;  // (a breath burns most of the dead it catches)
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

		// ---- Dragon's Updraft (Bond II's shout): taught whole once the bond is bought, and put in the shout slot if it's empty
		float g_shoutCheck = 0;
		RE::TESShout* UpdraftShout()
		{
			return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESShout>(gen::kUpdraftShout, gen::kPlugin);
		}
		void EnsureUpdraft(RE::PlayerCharacter* a_player)
		{
			auto* sh = UpdraftShout();
			if (!sh || Bond() < 2 || a_player->HasShout(sh)) {
				return;
			}
			for (auto w : gen::kUpdraftWords) {
				if (auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(w, gen::kPlugin)) {
					Gun::Console(fmt::format("player.teachword {:08X}", f->GetFormID()));
					Gun::Console(fmt::format("player.unlockword {:08X}", f->GetFormID()));
				}
			}
			a_player->AddShout(sh);
			if (!a_player->GetActorRuntimeData().selectedPower) {
				if (auto* em = RE::ActorEquipManager::GetSingleton()) {
					em->EquipShout(a_player, sh);
				}
			}
			Hud::Banner("Dragon's Updraft learned: shout it, then open your glider", 4.0f);
			logger::info("glider: Dragon's Updraft learned ({:08X})", sh->GetFormID());
		}
		// the whirlwind round you (CurseOfAlduin\fx\whirlwind.nif, tools/whirlwind.py): spinning, swelling, fading
		void WhirlGone()
		{
			for (auto& n : g_whirl.node) {
				if (n && n->parent) {
					n->parent->DetachChild(n.get());
				}
				n.reset();
			}
			g_whirl.t = -1;
		}
		void WhirlTick(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (g_whirl.t < 0) {
				return;
			}
			g_whirl.t += a_delta;
			const float t = g_whirl.t;
			if (t > 2.2f) {
				WhirlGone();
				return;
			}
			const float level = 1.8f * Smooth(t / 0.18f) * (1 - Smooth((t - 1.4f) / 0.7f));
			for (auto& n : g_whirl.node) {
				if (!n) continue;
				n->local.translate = { 0, 0, -60.0f * Smooth(t / 1.0f) };
				n->local.rotate = Rot(0, 0, -t * 9.0f);
				n->local.scale = 0.7f + 0.5f * Smooth(t / 0.8f);
				for (int i = 1; i <= 6; ++i) {
					FxLevel(n.get(), fmt::format("WW{}", i).c_str(), level);
				}
				RE::NiUpdateData ud;
				n->Update(ud);
			}
		}
		void WhirlStart(RE::PlayerCharacter* a_player)
		{
			WhirlGone();
			static RE::NiPointer<RE::NiNode> model;
			if (!model) {
				RE::BSModelDB::DBTraits::ArgsType args{};
				if (RE::BSModelDB::Demand("CurseOfAlduin\\fx\\whirlwind.nif", model, args) != RE::BSResource::ErrorCode::kNone || !model) {
					model.reset();
					logger::warn("glider: no whirlwind model");
					return;
				}
			}
			for (int i = 0; i < 2; ++i) {
				auto* root = a_player->Get3D(i == 1);
				auto* rn = root ? root->AsNode() : nullptr;
				if (!rn) continue;
				if (auto* clone = model->Clone()) {
					auto* obj = static_cast<RE::NiAVObject*>(clone);
					obj->local = RE::NiTransform{};
					rn->AttachChild(obj, true);
					g_whirl.node[i].reset(obj);
				}
			}
			g_whirl.t = 0;
			logger::info("glider: the whirlwind round you ({} / {})", g_whirl.node[0] ? "3rd" : "-", g_whirl.node[1] ? "1st" : "-");
		}
		void Updraft(RE::PlayerCharacter* a_player)
		{
			if (!a_player || g_hadDragon || g_liftOff != 0 || !a_player->GetCharController()) {
				return;
			}
			if (OnGround(a_player)) {
				a_player->SetPosition(a_player->GetPosition() + RE::NiPoint3{ 0, 0, 20.0f }, true);  // off the ground
			}
			g_liftOff = 1, g_takeT = 0, g_updraftAge = 0;
			Forms::PlayAt(gen::Sound::shout_gale, a_player);
			WhirlStart(a_player);
			logger::info("glider: Dragon's Updraft");
		}

		void BondTick(RE::PlayerCharacter* a_player, float a_delta)
		{
			ApplyBondLook();
			g_flapWait = std::max(0.0f, g_flapWait - a_delta);
			g_breathCd = std::max(0.0f, g_breathCd - a_delta);
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
			// Bond II's shout, Dragon's Updraft (Noah): a whirlwind round you carries you high into the air; open the
			// glider whenever you like (Activate) and the dragon comes for you
			g_updraftAge += a_delta;
			WhirlTick(a_player, a_delta);
			if (g_liftOff == 1) {
				g_takeT += a_delta;
				auto*        cc = a_player->GetCharController();
				RE::NiPoint3 v{};
				if (cc) {
					GetVel(cc, v);
				}
				// up about 14 m in 1.15 s, slowing to the top; then it holds you there a moment, barely sinking, so there is
				// time to open the glider (pressed on the way up, it opens at the top by itself)
				if (cc && g_takeT < 1.75f && !g_hadDragon) {
					cc->context.currentState = RE::hkpCharacterStateType::kInAir;
					v.z = g_takeT < 1.15f ? 60.0f + 1600.0f * (1 - Smooth(g_takeT / 1.15f)) : 60.0f - 140.0f * Smooth((g_takeT - 1.15f) / 0.6f);
					SetVel(cc, v);
				}
				if (g_glideQueued && g_takeT > 1.15f && !g_hadDragon) {
					g_glideQueued = false;
					if (!a_player->NotifyAnimationGraph("StartPara")) {
						DevTools::PressKey(RE::INPUT_DEVICE::kKeyboard, 0x12, 0.05f);
					}
					logger::info("glider: opened at the Updraft's top");
				}
				if (g_takeT > 1.75f || g_hadDragon) {
					g_liftOff = 0, g_glideQueued = false;
				}
			}
			if ((g_shoutCheck -= a_delta) <= 0) {
				g_shoutCheck = 2.0f;
				EnsureUpdraft(a_player);
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

		// The talons on your hands (Noah: closing on them, not hanging open): while it holds you each hind leg is bent
		// to reach your hand (two joints turned until the foot is at it, a little each frame) and the dragon itself
		// drawn the last of the way, so the closed claws sit round your grip
		RE::BSSoundHandle g_breathSound;
		float             g_dodgeT = -1;     // a dodge's push: its time, and which way
		RE::NiPoint3      g_dodgeDir{};
		float             g_divePitch = 0;   // Bond III: the dragon's nose down in a dive
		float        g_ik[2][2]{};   // the legs' bend toward your hands (hip, knee), radians on top of their own motion
		RE::NiPoint3 g_grabOff{};    // the dragon moved (DG_Space's own units) so its feet reach your hands
		float        g_grip = 0;     // how firmly it holds you (0 in the swoop .. 1)

		float SignedAngle(RE::NiPoint3 a_from, RE::NiPoint3 a_to, const RE::NiPoint3& a_axis)
		{
			a_from = a_from - a_axis * a_from.Dot(a_axis);
			a_to = a_to - a_axis * a_to.Dot(a_axis);
			return std::atan2(a_axis.Dot(a_from.Cross(a_to)), a_from.Dot(a_to));
		}

		void Grip(RE::PlayerCharacter* a_player, RE::NiAVObject* a_space, RE::NiAVObject* const (&a_leg)[2][3], bool a_first, float a_dt)
		{
			auto* root = a_player->Get3D(a_first);
			if (!root || !a_space || !a_space->parent || g_grip <= 0.01f) {
				return;
			}
			RE::NiUpdateData ud;
			ud.time = 0.0f;
			a_space->UpdateDownwardPass(ud, 0);  // (this frame's pose, before reaching)
			RE::NiPoint3 resid{};
			int          n = 0;
			for (int sd = 0; sd < 2; ++sd) {
				auto* hand = root->GetObjectByName(sd == 0 ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]");
				auto* fing = root->GetObjectByName(sd == 0 ? "NPC L Finger20 [LF20]" : "NPC R Finger20 [RF20]");
				if (!hand || !a_leg[sd][0] || !a_leg[sd][1] || !a_leg[sd][2]) {
					continue;
				}
				const RE::NiPoint3 target = fing ? (hand->world.translate + fing->world.translate) * 0.5f : hand->world.translate;  // the palm
				for (int it = 0; it < 2; ++it) {
					for (int j = 1; j >= 0; --j) {
						auto*              J = a_leg[sd][j];
						const auto&        P = J->parent->world.rotate;
						RE::NiPoint3       axis{ P.entry[0][0], P.entry[1][0], P.entry[2][0] };
						const RE::NiPoint3 end = a_leg[sd][2]->world.translate;
						const float        d = std::clamp(SignedAngle(end - J->world.translate, target - J->world.translate, axis), -0.25f, 0.25f) * g_grip;
						g_ik[sd][j] = std::clamp(g_ik[sd][j] + d, -1.3f, 1.3f);
						J->local.rotate = Rot(d, 0, 0) * J->local.rotate;
						J->UpdateDownwardPass(ud, 0);
					}
				}
				resid += target - a_leg[sd][2]->world.translate;
				++n;
			}
			if (n) {  // what the legs could not reach: the dragon comes down to you
				const auto&  R = a_space->parent->world.rotate;
				const float  sc = std::max(0.001f, a_space->parent->world.scale);
				RE::NiPoint3 r = resid * (1.0f / n);
				RE::NiPoint3 local{ R.entry[0][0] * r.x + R.entry[1][0] * r.y + R.entry[2][0] * r.z, R.entry[0][1] * r.x + R.entry[1][1] * r.y + R.entry[2][1] * r.z,
					R.entry[0][2] * r.x + R.entry[1][2] * r.y + R.entry[2][2] * r.z };
				g_grabOff = g_grabOff + local * (1.0f / sc) * std::min(1.0f, a_dt * 5.0f) * g_grip;
				const float len = g_grabOff.Length();
				if (len > 70.0f) g_grabOff = g_grabOff * (70.0f / len);
				static float logT = 0;
				if ((logT -= a_dt) <= 0) {
					logT = 2.0f;
					logger::info("glider: grip - feet {:.1f} from the hands, dragon moved ({:.0f},{:.0f},{:.0f}), legs {:.2f}/{:.2f} {:.2f}/{:.2f}", r.Length(),
						g_grabOff.x, g_grabOff.y, g_grabOff.z, g_ik[0][0], g_ik[0][1], g_ik[1][0], g_ik[1][1]);
				}
			}
		}

		void DevBreath(int a_element)  // dev: Bond III's breath now, in that element (0 fire, 1 frost, 2 lightning)
		{
			g_breathElDev = a_element;
			g_breathReq = true;
		}
		void DevBreathProj(int a_element, RE::FormID a_proj, float a_scale, float a_beamSign)  // dev: try a breath look
		{
			if (a_element >= 0 && a_element < 3) g_breathProjDev[a_element] = a_proj;
			g_breathScaleDev = a_scale, g_beamPitchSign = a_beamSign;
		}
		void DevFlap()  // dev: Bond I's climbing wing-beats, as if jump were pressed mid-glide
		{
			if (g_hadDragon && g_flapT < 0) {
				g_flapT = 0, g_flapWait = kFlapAll + 1.5f;
			}
		}
		void DevTakeOff()  // dev: Bond II's Updraft, as if shouted
		{
			Updraft(RE::PlayerCharacter::GetSingleton());
		}
		bool OnSpellCast(RE::FormID a_spell)  // the Updraft shout spoken (any word count)
		{
			for (auto s : gen::kUpdraftSpells) {
				if (auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(s, gen::kPlugin); f && f->GetFormID() == a_spell) {
					if (Bond() >= 2) {
						Updraft(RE::PlayerCharacter::GetSingleton());
					}
					return true;
				}
			}
			return false;
		}

		void DevFlyOff()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!p || !cam || !cam->cameraRoot) {
				return;
			}
			const auto&  m = cam->cameraRoot->world.rotate;
			const float  yaw = std::atan2(m.entry[0][1], m.entry[1][1]);
			RE::NiPoint3 f{ std::sin(yaw), std::cos(yaw), 0 };
			g_lastSpace.translate = p->GetPosition() + f * 700.0f + RE::NiPoint3{ 0, 0, 250 };
			g_lastSpace.rotate = AxisAngle({ 0, 0, 1 }, -yaw);
			g_lastSpace.scale = 0.23f;
			g_lastBank = Rot(0, 0, 0);
			LetGo(p, {}, 0.0f, 0.23f);
		}

		// the dive's rush of air: a wind roar that swells with it, and the view a little wider (eased back out after)
		RE::BSSoundHandle g_windSound;
		void DiveRush(RE::PlayerCharacter* a_player, float a_k)
		{
			static float fovAdd = 0, level = 0;
			level += (a_k - level) * 0.12f;
			if (level > 0.03f) {
				if (!g_windSound.IsValid()) {
					PlayGameSound(0x00108279, a_player, &g_windSound, 0.01f);  // AMBKagrenzelWindShaftLPM (a loop)
				}
				if (g_windSound.IsValid()) {
					g_windSound.SetVolume(std::min(1.0f, level * 1.1f));
				}
			} else if (g_windSound.IsValid()) {
				g_windSound.Stop();
			}
			if (auto* cam = RE::PlayerCamera::GetSingleton()) {
				const float want = 9.0f * level;
				cam->GetRuntimeData2().worldFOV += want - fovAdd;
				fovAdd = want;
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
			RE::NiAVObject* toeN[2][2][2]{};  // [skeleton][side][front, back]
			static bool     letGone = false, sawGliding = false;  // (the glide ended - the dragon already flew off - while its copy still hangs on you)
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
						toeN[i][sd][0] = root->GetObjectByName(sd == 0 ? "DG_ToeLF" : "DG_ToeRF");
						toeN[i][sd][1] = root->GetObjectByName(sd == 0 ? "DG_ToeLB" : "DG_ToeRB");
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
				if (g_hadDragon && !letGone) {  // the glider was just put away: the dragon lets go and flies off
					LetGo(a_player, vel, roll, kDragonScale);
				}
				g_hadDragon = false, letGone = false, sawGliding = false;
				if (g_breathSound.IsValid()) g_breathSound.Stop();
				DiveRush(a_player, 0.0f);  // (eases the view back)
				lastYaw = yaw, lastZ = z, turn = 0, breath = 0, jaw = 0, breathIn = 4.0f, deployT = -1, roll = 0;
				g_armHoldOn = false, g_armHold = 0, g_flapT = -1, g_liftT = -1, g_grip = 0;
				g_grabOff = {};
				std::memset(g_ik, 0, sizeof(g_ik));

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
				for (auto* sp : space) if (sp) baseTrans = sp->local.translate;
				baseScale = kDragonScale;
				PlayGameSound(DragonSound::kRoar, a_player, nullptr, 0.7f);
				PlayGameSound(DragonSound::kFlapBig, a_player);
			}
			// the paraglider's own state: the glide ended (landing, Activate) though its copy of the dragon still hangs on
			// you for the put-away - the dragon lets go now, and the copy is hidden
			{
				bool pg = false;
				const bool known = a_player->GetGraphVariableBool("bParaGliding", pg);
				if (known && pg) {
					sawGliding = true;
				}
				// (Noah: never the glider's own put-away - its copy of the dragon lying at your feet): landed, or the glide
				// over without the paraglider ever saying it began, the copy goes too
				const bool landed = deployT > 0.25f && OnGround(a_player);
				if (((known && sawGliding && !pg) || landed) && !letGone) {
					letGone = true;
					if (g_hadDragon) {
						LetGo(a_player, vel, roll, kDragonScale);
					}
					if (g_breathSound.IsValid()) g_breathSound.Stop();
				}
			}
			if (letGone) {
				// a new glide while the paraglider still has its copy on you (it had landed, then you took off again
				// before it put it away): the dragon comes for you afresh - otherwise you fell with no dragon at all
				static bool  touched = false;
				static float airT = 0;
				bool         pg2 = false;
				a_player->GetGraphVariableBool("bParaGliding", pg2);
				if (OnGround(a_player)) {
					touched = true, airT = 0;
				} else {
					airT = pg2 ? airT + a_delta : 0.0f;
				}
				if (touched && airT > 0.25f) {
					touched = false, airT = 0;
					letGone = false, sawGliding = pg2, deployT = -1, g_hadDragon = false, g_grip = 0;
					logger::info("glider: a new glide before the last was put away - the dragon comes again");
				} else {
					for (auto* sp : space) if (sp) sp->local.scale = 0.0f;
					g_armHoldOn = false, g_armHold = 0;
					lastPos = pos;
					return;
				}
			}
			deployT += a_delta;
			// the swoop: 1.15 s, or 0.6 s when it catches you at the top of a take-off leap (so you never hang there)
			static bool quickSwoop = false;
			if (deployT <= a_delta * 1.5f) {
				quickSwoop = g_updraftAge < 4.0f;  // (lifted by the Updraft: it comes for you quicker)
			}
			const float kSwoop = quickSwoop ? 0.6f : 1.15f;
			// your arms stay down until it reaches you, then go up to its talons as it takes you
			g_armHoldOn = true;
			g_armHold = 1 - Smooth((deployT - (kSwoop - 0.38f)) / 0.38f);
			const float u = std::clamp(deployT / kSwoop, 0.0f, 1.0f);
			const float e = u * u * u * (u * (u * 6 - 15) + 10);  // smootherstep
			// (Noah: no sharp turn at the end - it comes in from behind on a gentle curve, and eases round to face ahead)
			const RE::NiPoint3 P0{ 260, -720, 300 }, P1{ 190, -400, 30 }, P2{ 0, 0, 0 };
			const RE::NiPoint3 at = P0 * ((1 - e) * (1 - e)) + P1 * (2 * (1 - e) * e) + P2 * (e * e);
			RE::NiPoint3 tan = (P1 - P0) * (2 * (1 - e)) + (P2 - P1) * (2 * e);
			const float  inSwoop = 1 - Smooth((u - 0.45f) / 0.55f);
			const float  swYaw = std::atan2(-tan.x, std::max(1.0f, tan.y)) * inSwoop;
			const float  swPitch = std::atan2(tan.z, std::sqrt(tan.x * tan.x + tan.y * tan.y)) * inSwoop;
			const float  swRoll = 0.55f * std::sin(3.14159f * std::min(1.0f, u * 1.15f)) * inSwoop;
			for (int i = 0; i < 2; ++i) {
				if (space[i]) {
					space[i]->local.translate = baseTrans + at + g_grabOff * g_grip;
					if (baseScale > 0) space[i]->local.scale = baseScale;
				}
				if (fold[i][0]) fold[i][0]->local.rotate = Rot(0, 0, 0);
				if (fold[i][1]) fold[i][1]->local.rotate = Rot(0, 0, 0);
				// the wings: straight out in the glide, swaying; beating only for a climb (Bond I's wing-beat, the
				// take-off's lift); swept back in a Bond III dive
				const float flapT = g_flapT, liftT = g_liftT;
				WingPose(a_player->Get3D(i == 1), deployT, [flapT, liftT](float a_lag) {
					return (flapT >= 0 ? FlapWing(flapT - a_lag) : 0.0f) + WingBeat(liftT - a_lag, 1.3f);
				}, std::clamp(g_divePitch / 0.42f, 0.0f, 1.0f));
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
				const float tuck = (breathing_ ? std::min(1.0f, breathT_ * 3.0f) : 0.0f) * (1 - clutch);  // (never while it holds you)
				const float open = regrip < 0.5f ? 0.2f * std::sin(3.14159f * regrip / 0.5f) : 0.0f;  // a re-grip: a flex of the talons
				g_grip = clutch;
				for (int sd = 0; sd < 2; ++sd) {
					const float ph = sd * 1.3f;
					const float idleHip = 0.12f + 0.07f * std::sin(legT * 2.1f + ph), idleKnee = -0.10f + 0.08f * std::sin(legT * 2.1f + 0.7f + ph);
					const float idleFoot = 1.05f + 0.05f * std::sin(legT * 2.1f + 1.4f + ph) - 0.7f * open;  // talons closed round your hand
					float hip = idleHip * clutch + 0.75f * reach - 0.45f * tuck;
					float knee = idleKnee * clutch - 0.15f * reach + 0.6f * tuck;
					float foot = idleFoot * clutch - 0.6f * reach + 0.3f * tuck;
					if (u < 0.35f) {  // early in the swoop: trailing behind it as it dives
						const float trail = 1 - u / 0.35f;
						hip -= 0.5f * trail, knee += 0.3f * trail;
					}
					for (int i = 0; i < 2; ++i) {
						if (legN[i][sd][0]) legN[i][sd][0]->local.rotate = Rot(hip + g_ik[sd][0] * clutch * (1 - tuck), 0, 0);
						if (legN[i][sd][1]) legN[i][sd][1]->local.rotate = Rot(knee + g_ik[sd][1] * clutch * (1 - tuck), 0, 0);
						if (legN[i][sd][2]) legN[i][sd][2]->local.rotate = Rot(foot, 0, 0);
						// the talons: spread as it reaches for you, then closed round your hand (front ones back, the back
						// one forward); a re-grip loosens them a little
						const float toes = 0.5f * reach - 1.15f * clutch + 0.45f * open * clutch;
						if (toeN[i][sd][0]) toeN[i][sd][0]->local.rotate = Rot(toes, 0, 0);
						if (toeN[i][sd][1]) toeN[i][sd][1]->local.rotate = Rot(-toes * 0.9f, 0, 0);
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
			roll += (std::clamp(turn * 0.55f, -0.75f, 0.75f) - roll) * std::min(1.0f, dt * 3.0f);
			static float swing = 0;  // the body swung round into the turn (head first)
			swing += (std::clamp(-turn * 0.3f, -0.4f, 0.4f) - swing) * std::min(1.0f, dt * 2.5f);
			// (Bond III: nose down in a dive; Noah: nose up as it beats its wings to climb)
			const float tilt = g_flapT >= 0 ? FlapTilt(g_flapT) : 0.0f;
			pitch += (std::clamp(fall * 0.0006f, -0.18f, 0.08f) - g_divePitch - tilt - pitch) * std::min(1.0f, dt * (tilt > 0 ? 4.0f : 2.0f));
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
			// (Noah: the breath is Bond III's alone, and yours to call - right attack / right trigger mid-glide, 5 s apart)
			breathIn = 9.0f;
			if (g_breathReq && breath <= 0 && thrall && deployT > kSwoop) {
				breath = 1.6f;
				sinceBreath = 0;
				PlayGameSound(DragonSound::kFire, a_player, &g_breathSound);
				logger::info("glider: the dragon breathes ({})", g_breathElDev >= 0 ? g_breathElDev : HandElement());
			}
			g_breathReq = false;
			if (!thrall) {
				breath = 0;
			}
			const bool breathing = breath > 0;
			if (breathing && (breath -= dt) <= 0 && g_breathSound.IsValid()) {
				g_breathSound.Stop();
			}
			breathing_ = breathing, breathT_ = breathing ? breathT_ + dt : 0.0f;
			jaw += ((breathing || (thrall && breathIn < 0.35f) ? 0.38f : 0.03f * (0.5f + 0.5f * std::sin(wanderT * 0.9f))) - jaw) * std::min(1.0f, dt * 8.0f);
			for (int i = 0; i < 2; ++i) {
				if (bank[i]) {
					bank[i]->local.rotate = Rot(pitch * (1 - inSwoop) + swPitch, roll * (1 - inSwoop) + swRoll, swYaw + swing * (1 - inSwoop));
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
			if (g_flapT >= 0) {
				const float before = g_flapT;
				g_flapT += dt;
				for (const float at : { 0.13f, kFlapSmall + 0.13f }) {  // each small downstroke heard, then the great one
					if (before < at && g_flapT >= at) PlayGameSound(DragonSound::kFlap, a_player, nullptr, 0.7f);
				}
				if (before < 2 * kFlapSmall + 0.3f && g_flapT >= 2 * kFlapSmall + 0.3f) {
					PlayGameSound(DragonSound::kFlapBig, a_player);
				}
				if (g_flapT > kFlapAll) {
					g_flapT = -1;
				}
			}
			if (auto* cc = a_player->GetCharController(); cc && !g_dive) {
				static RE::NiPoint3 glide{};
				static float        fallV = 0, speed = 950;
				RE::NiPoint3        v{};
				GetVel(cc, v);
				const int  bond = Bond();
				const bool hanging = false;
				if (deployT <= a_delta * 1.5f) {
					glide = { v.x, v.y, 0 };
					fallV = std::max(v.z, -250.0f);
					speed = std::max(950.0f, std::sqrt(v.x * v.x + v.y * v.y));
				}
				bool set = false;
				// (Noah: you don't glide before it has you) until its talons close you fall as you were falling
				const bool beforeGrab = deployT < kSwoop * 0.9f;
				if (beforeGrab) {
					fallV = std::max(fallV - 900.0f * dt, -1100.0f);
					v.z = fallV;
					set = true;
				}
				if (bond >= 1) {
					const RE::NiPoint3 heading{ std::sin(yaw), std::cos(yaw), 0 };
					// Bond III: where you look steers the dive - look down and it folds its wings and dives, gathering
					// speed; look up and that speed carries you on and up into the glide
					float lookDown = 0;
					if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
						lookDown = std::asin(std::clamp(-cam->cameraRoot->world.rotate.entry[2][1], -1.0f, 1.0f));
					}
					float want = 950.0f;
					g_divePitch += ((bond >= 3 && !beforeGrab ? std::clamp(lookDown, -0.35f, 0.9f) * 0.6f : 0.0f) - g_divePitch) * std::min(1.0f, dt * 3.0f);
					if (bond >= 3 && !beforeGrab && !hanging) {
						// (Noah: after a dive, look back up and it carries on gliding on that speed - with speed enough, up)
						// height is bought with speed: climbing bleeds it in proportion, gliding level keeps most of it
						if (lookDown > 0.15f) {
							speed = std::min(2800.0f, speed + 1300.0f * std::sin(lookDown) * dt);
						} else if (lookDown < -0.05f && speed > 1000.0f) {
							speed = std::max(950.0f, speed - (90.0f + 0.5f * (speed - 950.0f) * std::sin(-lookDown) * 1.6f) * dt);
						} else {
							speed = std::max(950.0f, speed - 90.0f * dt);
						}
						want = speed;
					}
					want = hanging ? 600.0f : want;  // (the leap's run carries on while it comes)
					glide = glide + (heading * want - glide) * std::min(1.0f, dt * (hanging ? 2.0f : bond >= 3 ? 2.0f : 0.9f));
					v.x = glide.x, v.y = glide.y;
					if (!beforeGrab) {
						v.z = std::max(v.z, -105.0f);  // (the glide alone sinks about half again faster)
						if (bond >= 3 && !hanging) {
							static float vz = 0;  // (eased: out of a dive it levels off and glides on, no sudden stop)
							float        wantZ = -105.0f;
							if (lookDown > 0.15f) {
								wantZ = -speed * std::sin(lookDown) * 0.9f;  // the dive
							} else if (lookDown < -0.05f && speed > 1000.0f) {
								wantZ = (speed - 950.0f) * std::sin(-lookDown) * 1.6f;  // the speed into height
							}
							if (deployT <= kSwoop) {
								vz = v.z;
							}
							vz += (wantZ - vz) * std::min(1.0f, dt * (wantZ < vz ? 3.0f : 2.0f));
							v.z = vz;
						}
					}
					if (hanging) {
						v.z = -160.0f * Smooth(deployT / (kSwoop * 0.88f));  // the leap's peak, just tipping over
					}
					if (g_liftT >= 0) {
						g_liftT += dt;
						v.z = std::max(v.z, 1150.0f * (1 - Smooth(g_liftT / 1.3f)));
						if (g_liftT > 1.3f) g_liftT = -1;
					}
					if (g_flapT >= 0) {  // each downstroke lifts you
						v.z = std::max(v.z, FlapLift(g_flapT));
					}
					set = true;
				}
				if (set) {
					SetVel(cc, v);
				}
			}
			// the wind: a streak off each wingtip (brighter the faster you go), and in a Bond III dive the air rushing
			// past - streaks all round, its roar, the view widening a little as you gather speed
			{
				const float spd = vel.Length();
				const float held = Smooth((deployT - kSwoop * 0.6f) / 0.5f);
				// (Noah: the wingtip trails fade out while the wings beat, and smoothly back in as it glides again)
				const bool  flapping = g_flapT >= 0 || g_liftT >= 0;
				static float trailK = 0, diveK = 0;
				const float trailWant = flapping ? 0.0f : held * (0.55f + 0.65f * Smooth((spd - 800.0f) / 1400.0f));
				trailK += (trailWant - trailK) * std::min(1.0f, dt * (flapping ? 7.0f : 2.2f));
				const float diveWant = Smooth((g_divePitch - 0.08f) / 0.3f) * Smooth((spd - 1100.0f) / 1100.0f);
				diveK += (diveWant - diveK) * std::min(1.0f, dt * 3.0f);
				if (deployT <= a_delta * 1.5f) {
					trailK = 0, diveK = 0;
				}
				for (int i = 0; i < 2; ++i) {
					DragonWind(a_player->Get3D(i == 1), trailK, diveK * 1.4f);
				}
				const float dive = diveK;
				DiveRush(a_player, dive);
			}
			{  // the talons on your hands (the skeleton you see)
				const bool first = RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
				const int  k = first && space[1] ? 1 : 0;
				Grip(a_player, space[k], legN[k], k == 1, dt);
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
				const int          el = g_breathElDev >= 0 ? g_breathElDev : thrall ? HandElement() : 0;
				RE::BGSProjectile* fire = BreathProjectile(el);  // the dragons' own breath (Bond III: your spell's element)
				if (fire) {
					const auto&        hm = head->world.rotate;
					RE::NiPoint3       d{ hm.entry[0][1], hm.entry[1][1], hm.entry[2][1] - 0.25f };  // its head's forward, a little down
					if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {  // where you aim
						const auto& cm = cam->cameraRoot->world.rotate;
						d = { cm.entry[0][1], cm.entry[1][1], cm.entry[2][1] };
					}
					if (auto t = target.get(); thrall && t && !t->IsDead()) {  // (onto the dead near your aim)
						RE::NiPoint3 to = t->GetPosition() + RE::NiPoint3{ 0, 0, 50 } - mouth->world.translate;
						to.Unitize();
						RE::NiPoint3 dn = d;
						dn.Unitize();
						if (to.x * dn.x + to.y * dn.y + to.z * dn.z > 0.94f) d = to;
					}
					d.Unitize();
					if (thrall && (hitT -= 0.05f) <= 0) {
						hitT = 0.2f;
						BreathHits(a_player, mouth->world.translate, d, el);
					}
					const RE::NiPoint3 from = mouth->world.translate + d * 10.0f;
					const float        flat = std::sqrt(d.x * d.x + d.y * d.y);
					RE::Projectile::ProjectileRot r{ -std::atan2(d.z, std::max(1e-3f, flat)), std::atan2(d.x, d.y) };
					if (!fire->IsFlamethrower()) {  // (a stream of bolts: fanned a little, like breath)
						std::uniform_real_distribution<float> j(-1.0f, 1.0f);
						r.x += 0.05f * j(rng), r.z += 0.08f * j(rng);
					}
					RE::Projectile::LaunchData    ld(fire, a_player, from, r);
					ld.useOrigin = true;
					ld.autoAim = false;
					ld.noDamageOutsideCombat = true;
					ld.power = 0.0f;
					ld.scale = g_breathScaleDev > 0 ? g_breathScaleDev : el == 0 || el >= 3 ? 0.45f : el == 1 ? 0.6f : 0.7f;  // (Noah: a smaller breath - its reach is the same)
					// (a beam - the lightning - is aimed by its shooter's own facing, not the launch angles: you face the breath
					// for the moment it leaves)
					auto&       ang = a_player->data.angle;  // (x pitch, z yaw)
					const float keepX = ang.x, keepZ = ang.z;
					if (fire->IsBeam()) {
						ang.x = r.x * g_beamPitchSign, ang.z = r.z;
						ld.angleX = r.x * g_beamPitchSign;
					}
					RE::ProjectileHandle h;
					RE::Projectile::Launch(&h, ld);
					if (fire->IsBeam()) {
						ang.x = keepX, ang.z = keepZ;
					}
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
				"Shout Dragon's Updraft: a whirlwind lifts you, then open the glider  -  attack looking down mid-glide: the dive",
				"Right attack mid-glide: its breath, in your spell's element (every 5 s)" };
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

		// The dodge (Noah: rolls backwards too, and every way in first person): TK Dodge's rolls, but which way is ours -
		// the stick or WASD against the camera (not against where the body happens to face: True Directional Movement
		// turns you to run, so its own choice always came out "forward"). In third person the body is turned to the
		// camera first, so back is back. TK Dodge RE's own key is off (its ini: DodgeHotkey = 0).
		void Dodge()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			static std::chrono::steady_clock::time_point last{};
			const auto now = std::chrono::steady_clock::now();
			if (!p || (ui && ui->GameIsPaused()) || now - last < std::chrono::milliseconds(650) || Riding() || g_hadDragon || !OnGround(p)) {
				return;
			}
			auto* st = p->AsActorState();
			if (st->IsSneaking() || st->IsSwimming() || p->IsDead()) {
				return;
			}
			auto* av = p->AsActorValueOwner();
			if (av->GetActorValue(RE::ActorValue::kStamina) < 10.0f) {
				return;
			}
			float camYaw = p->GetAngleZ();
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
				const auto& m = cam->cameraRoot->world.rotate;
				camYaw = std::atan2(m.entry[0][1], m.entry[1][1]);
			}
			const char* ev = "TKDodgeBack";  // (standing still: a step back, as TK Dodge does)
			RE::NiPoint3 dir{ -std::sin(camYaw), -std::cos(camYaw), 0 };
			if (auto* pc = RE::PlayerControls::GetSingleton()) {
				const auto in = pc->data.moveInputVec;  // x right, y forward - against the camera
				if (in.x * in.x + in.y * in.y > 0.04f) {
					ev = std::abs(in.y) >= std::abs(in.x) ? (in.y > 0 ? "TKDodgeForward" : "TKDodgeBack") : (in.x > 0 ? "TKDodgeRight" : "TKDodgeLeft");
					// (snapped to the roll's own direction)
					const RE::NiPoint3 f{ std::sin(camYaw), std::cos(camYaw), 0 }, r{ std::cos(camYaw), -std::sin(camYaw), 0 };
					dir = std::abs(in.y) >= std::abs(in.x) ? (in.y > 0 ? f : f * -1.0f) : (in.x > 0 ? r : r * -1.0f);
				}
			}
			p->SetHeading(camYaw);  // face where you look: the roll goes the way you meant
			if (!p->NotifyAnimationGraph(ev)) {
				logger::info("dodge: {} not taken", ev);
				return;
			}
			last = now;
			av->DamageActorValue(RE::ActorValue::kStamina, 10.0f);
			Perks::Dodged(0.35f);
			g_dodgeT = 0, g_dodgeDir = dir;  // (Noah: farther - first person barely moved) carried along, below
		}

		void OnButton(const RE::ButtonEvent* a_event)
		{
			// the dodge (Left Alt; on a controller a quick tap of sprint, LB / L1 - holding it still sprints)
			if (a_event && a_event->GetDevice() == RE::INPUT_DEVICE::kKeyboard && a_event->GetIDCode() == 0x38 && a_event->IsDown()) {
				Dodge();
			}
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
							Dodge();
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
				if (free && ev == ue->jump && g_hadDragon && g_flapT < 0 && g_flapWait <= 0) {  // I: the climbing wing-beats
					g_flapT = 0, g_flapWait = kFlapAll + 1.5f;
				}
				if (free && bond >= 2 && ev == ue->activate && g_liftOff == 1 && !g_hadDragon && g_takeT < 1.15f) {
					g_glideQueued = true;
				}
				// III: right attack (right trigger) breathes (5 s apart); II: attack looking down dives - the left hand
				// (left trigger) for Bond III, whose right is the breath
				const bool breathKey = bond >= 3 && ev == ue->rightAttack;
				if (free && breathKey && g_hadDragon && !g_dive) {
					if (g_breathCd <= 0) {
						g_breathReq = true;
						g_breathCd = 5.0f;
					}
				}
				if (free && bond >= 2 && !breathKey && (ev == ue->rightAttack || ev == ue->leftAttack) && g_hadDragon && !g_dive) {  // II: the dive
					float down = 0;
					if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
						down = -cam->cameraRoot->world.rotate.entry[2][1];
					}
					if (down > 0.42f) {
						g_dive = true, g_diveT = 0;
						if (!player->NotifyAnimationGraph("EndPara")) {  // (the glider closed: it lets you go)
							DevTools::PressKey(RE::INPUT_DEVICE::kKeyboard, 0x12, 0.05f);
						}
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
			// (Noah: no gliding before it has you - the whole body, not just the arms, keeps its fall until the talons close)
			constexpr int                kN = 15;
			static constexpr const char* kBones[kN] = { "NPC Pelvis [Pelv]", "NPC L Clavicle [LClv]", "NPC R Clavicle [RClv]", "NPC L UpperArm [LUar]",
				"NPC R UpperArm [RUar]", "NPC L Forearm [LLar]", "NPC R Forearm [RLar]", "NPC Spine [Spn0]", "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]",
				"NPC Neck [Neck]", "NPC L Thigh [LThg]", "NPC R Thigh [RThg]", "NPC L Calf [LClf]", "NPC R Calf [RClf]" };
			constexpr int                kHist = 10;  // (a few frames back: the glide's raise can start a frame before we see the dragon)
			static RE::NiMatrix3         hist[2][kN][kHist]{};
			static int                   n[2]{}, head[2]{};
			static RE::NiMatrix3         held[2][kN]{};
			static bool                  holding[2]{};
			for (int f = 0; f < 2; ++f) {
				auto* root = a_player->Get3D(f == 1);
				if (!root) {
					continue;
				}
				RE::NiAVObject* b[kN]{};
				bool            all = true;
				for (int i = 0; i < kN; ++i) {
					b[i] = root->GetObjectByName(kBones[i]);
					all = all && b[i];
				}
				if (!all) {
					continue;
				}
				if (!g_armHoldOn) {
					holding[f] = false;
					for (int i = 0; i < kN; ++i) hist[f][i][head[f]] = b[i]->local.rotate;
					head[f] = (head[f] + 1) % kHist;
					n[f] = std::min(n[f] + 1, kHist);
					continue;
				}
				if (!holding[f]) {
					holding[f] = true;
					const int oldest = n[f] < kHist ? 0 : head[f];
					for (int i = 0; i < kN; ++i) held[f][i] = n[f] > 0 ? hist[f][i][oldest] : b[i]->local.rotate;
				}
				n[f] = 0, head[f] = 0;
				if (g_armHold <= 0) {
					continue;
				}
				for (int i = 0; i < kN; ++i) b[i]->local.rotate = Slerp(b[i]->local.rotate, held[f][i], g_armHold);
				RE::NiUpdateData ud;
				ud.time = 0.0f;
				b[0]->UpdateDownwardPass(ud, 0);  // (the pelvis: everything under it)
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
			if (g_dodgeT >= 0) {  // the dodge's push: a quick shove along the roll, easing off over its length
				g_dodgeT += a_delta;
				if (auto* dcc = player->GetCharController(); dcc && g_dodgeT < 0.45f) {
					RE::NiPoint3 dv{};
					GetVel(dcc, dv);
					const float k = 1.0f - Smooth(g_dodgeT / 0.45f);
					const float sp = 820.0f * k;
					dv.x = g_dodgeDir.x * sp + dv.x * (1 - k), dv.y = g_dodgeDir.y * sp + dv.y * (1 - k);
					SetVel(dcc, dv);
				} else {
					g_dodgeT = -1;
				}
			}
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
