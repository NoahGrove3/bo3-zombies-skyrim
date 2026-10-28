#include "BO3Z.h"
#include "DragonPoses.h"

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
		// ---- the dragon's goodbye: when the glider is put away the dragon lets go of you and flies on - its own nif
		// (CurseOfAlduin\fx\dragon_away[_bN].nif, the glider's dragon on its own) cloned and hung on your own 3D (both
		// skeletons, like the whirlwind), placed in the world each frame where it should be: carrying on forward, climbing and
		// banking away, its talons opening then drawn up. (It used to be a reference placed in the world and moved about -
		// opening the map while one was in flight crashed the game, so it is no reference at all now.)
		struct Away
		{
			RE::NiPointer<RE::NiAVObject> node[2];   // its copy on your third- and first-person 3D
			RE::NiPointer<RE::NiNode>     host[2];   // ... and what each hangs from
			float                         t = -1;
			RE::NiTransform               start;
			RE::NiMatrix3                 bank;
			RE::NiPoint3                  vel;
			RE::NiPoint3                  fwd{ 0, 1, 0 };  // which way it flies on (level, in the world)
			float                         scale = 1, side = 1;
		};
		Away            g_away;
		RE::NiTransform g_lastSpace;       // where the dragon was last frame (DG_Space's world transform)
		RE::NiMatrix3   g_lastBank;
		bool            g_hadDragon = false;
		int   g_fpGlide = 0;  // 1: turning to third person, 2: gliding from first person
		float g_fpGlideT = 0, g_fpZoomWas = -1;
		bool  g_fpWant = false;
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
		// the new dragon's own poses (tools/dragonbuild.py writes src/DragonPoses.h): its wings folded for the dive
		const RE::NiMatrix3* DivePose(const char* a_bone)
		{
			static std::unordered_map<std::string, RE::NiMatrix3> m;
			if (m.empty()) {
				for (auto& q : DragonPoses::kDive) m[q.bone] = FromQuat({ q.w, q.x, q.y, q.z });
			}
			auto it = m.find(a_bone);
			return it == m.end() ? nullptr : &it->second;
		}
		template <class F>
		void WingPose(RE::NiAVObject* a_root, float a_clock, F&& a_beat, float a_fold, float a_lean = 0.0f)
		{
			if (!a_root) {
				return;
			}
			static const char* names[2][3] = { { "DG_WingL1", "DG_WingL2", "DG_WingL3" }, { "DG_WingR1", "DG_WingR2", "DG_WingR3" } };
			static const char* fingers[2][4] = { { "DG_WingLF11", "DG_WingLF21", "DG_WingLF31", "DG_WingLF41" }, { "DG_WingRF11", "DG_WingRF21", "DG_WingRF31", "DG_WingRF41" } };
			// (the new dragon's arm bends at every joint: up high, the stroke down shallower)
			constexpr float ampsUp[3] = { 34.0f, 18.0f, 12.0f }, ampsDown[3] = { 24.0f, 10.0f, 6.0f };
			constexpr float lag[3] = { 0.0f, 0.07f, 0.14f };
			constexpr float d = 3.14159265f / 180.0f;
			for (int sd = 0; sd < 2; ++sd) {
				const float sg = sd == 0 ? 1.0f : -1.0f;  // the left wing: +roll lifts it, +turn sweeps it back
				float       upFold = 0;                   // (the upstroke: the membrane half folds, the fingers drawn together)
				for (int j = 0; j < 3; ++j) {
					auto* n = a_root->GetObjectByName(names[sd][j]);
					if (!n) continue;
					const float b = a_beat(lag[j]), rising = a_beat(lag[j] - 0.02f) - b;
					if (j == 1 && rising > 0) upFold = 0.35f * std::min(1.0f, std::abs(b) + 0.3f);
					const float sway = (j == 0 ? 2.5f : 1.5f) * std::sin(6.28318f * a_clock / 3.0f + j * 0.6f + sd * 0.5f);
					// (in a turn the outer wing is lifted a little more, the inner one let down: a_lean +1 leaning hard right)
					const float lean = (sd == 0 ? 1.0f : -1.0f) * a_lean * (j == 0 ? 9.0f : 5.0f) * (1 - a_fold);
					const float roll = sway * (1 - a_fold) + (b > 0 ? ampsUp[j] : ampsDown[j]) * b + lean;
					const float back = rising > 0 ? (j == 0 ? 6.0f : 10.0f) * std::min(1.0f, std::abs(b) + 0.3f) : 0.0f;
					const float twist = (rising < 0 ? (j == 2 ? 7.0f : 3.0f) * std::abs(b) : 0.0f) + 1.2f * std::sin(6.28318f * a_clock / 3.0f + 1.0f + j);
					RE::NiMatrix3 r = Rot(twist * d, sg * roll * d, sg * back * d);
					// (Noah: in the dive the wings fold in close along its body, like a stooping bird's)
					if (auto* fp = DivePose(names[sd][j]); fp && a_fold > 0) r = Slerp(r, *fp, a_fold);
					n->local.rotate = r;
				}
				const float ff = std::max(a_fold, upFold);
				for (int k = 0; k < 4; ++k) {
					auto* n = a_root->GetObjectByName(fingers[sd][k]);
					auto* fp = DivePose(fingers[sd][k]);
					if (!n || !fp) continue;
					// (gliding, the membrane ripples a little along the fingers)
					const RE::NiMatrix3 ripple = Rot(0, sg * 1.5f * d * std::sin(6.28318f * a_clock / 1.7f + k * 0.9f + sd), 0);
					n->local.rotate = Slerp(ripple, *fp, ff);
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
			// (the trail's ribbon follows the tip's path: its wisps drift back along it a little slower than the air stands still)
			const float trailV = -std::fmod(clock * 4.7f, 1.0f), diveV = -std::fmod(clock * 3.2f, 1.0f);
			FxLevel(a_root, "DG_TrailL", a_trail, trailV);
			FxLevel(a_root, "DG_TrailR", a_trail, trailV + 0.37f);
			(void)a_dive;  // (Noah: no streaks off its body any more - only the wingtips')
			(void)diveV;
		}

		// ---- the wingtip trails (Noah: see-through, flowing with the wings - never a solid line): each is a ribbon skinned
		// to twelve bones (tools/dragonfx.py: DG_TrlL0..11 / DG_TrlR0..11) that are put, every frame, where the wingtip was
		// 0, 0.03, 0.06 ... seconds ago and turned as the wing was then - so the ribbon is the very path the tip drew through
		// the air, curving with every beat, turn and dive, widening and fading as it ages
		constexpr int   kTrailBones = 12;
		constexpr float kTrailStep = 0.03f;
		struct TrailHist
		{
			struct S
			{
				float        t;
				RE::NiPoint3 p, x;  // where the tip was, and its span axis then
			};
			static constexpr int kN = 128;
			S     buf[2][kN]{};
			int   n[2]{}, head[2]{};
			float clock = 0;
			void  Reset() { n[0] = n[1] = head[0] = head[1] = 0; }
			const S& Newest(int a_sd) const { return buf[a_sd][(head[a_sd] - 1 + kN) % kN]; }
			void  Push(int a_sd, const RE::NiPoint3& a_p, const RE::NiPoint3& a_x)
			{
				buf[a_sd][head[a_sd]] = { clock, a_p, a_x };
				head[a_sd] = (head[a_sd] + 1) % kN;
				n[a_sd] = std::min(n[a_sd] + 1, kN);
			}
			bool At(int a_sd, float a_age, RE::NiPoint3& a_p, RE::NiPoint3& a_x) const
			{
				if (n[a_sd] == 0) return false;
				const float want = clock - a_age;
				const S*    newer = &Newest(a_sd);
				if (want >= newer->t) {
					a_p = newer->p, a_x = newer->x;
					return true;
				}
				for (int i = 1; i < n[a_sd]; ++i) {
					const S* older = &buf[a_sd][(head[a_sd] - 1 - i + 2 * kN) % kN];
					if (older->t <= want) {
						const float k = (want - older->t) / std::max(1e-5f, newer->t - older->t);
						a_p = older->p + (newer->p - older->p) * k;
						a_x = older->x + (newer->x - older->x) * k;
						return true;
					}
					newer = older;
				}
				a_p = newer->p, a_x = newer->x;  // (no older: the trail is still growing out of the tip)
				return true;
			}
		};
		TrailHist g_trail;      // the dragon that holds you
		TrailHist g_awayTrail;  // the one flying off
		void TrailSample(RE::NiAVObject* a_root, TrailHist& a_h, float a_dt)
		{
			if (!a_root) return;
			a_h.clock += std::clamp(a_dt, 0.0f, 0.1f);
			for (int sd = 0; sd < 2; ++sd) {
				auto* tip = a_root->GetObjectByName(sd == 0 ? "DG_TipL" : "DG_TipR");
				if (!tip || tip->world.scale < 1e-4f) continue;
				const auto&        m = tip->world.rotate;
				const RE::NiPoint3 x{ m.entry[0][0], m.entry[1][0], m.entry[2][0] };
				if (a_h.n[sd] == 0 || a_h.clock - a_h.Newest(sd).t >= 0.006f) a_h.Push(sd, tip->world.translate, x);
			}
		}
		void TrailPlace(RE::NiAVObject* a_root, const TrailHist& a_h, float a_scale)
		{
			if (!a_root || a_scale < 1e-4f) return;
			RE::NiUpdateData ud;
			ud.time = 0.0f;
			for (int sd = 0; sd < 2; ++sd) {
				for (int k = 0; k < kTrailBones; ++k) {
					auto* bone = a_root->GetObjectByName(fmt::format("DG_Trl{}{}", sd == 0 ? "L" : "R", k).c_str());
					if (!bone || !bone->parent || bone->parent->world.scale < 1e-4f) continue;
					RE::NiPoint3 p, x, pa, pb, xa, xb;
					if (!a_h.At(sd, k * kTrailStep, p, x)) continue;
					a_h.At(sd, std::max(0.0f, (k - 0.5f) * kTrailStep), pa, xa);
					a_h.At(sd, (k + 0.5f) * kTrailStep, pb, xb);
					RE::NiPoint3 Y = pa - pb;  // (toward the tip: the way the air came from)
					const auto&  P = bone->parent->world;
					if (Y.Length() < 0.5f) Y = { P.rotate.entry[0][1], P.rotate.entry[1][1], P.rotate.entry[2][1] };
					Y.Unitize();
					RE::NiPoint3 X = x - Y * x.Dot(Y);
					if (X.Length() < 1e-3f) X = Y.Cross({ 0, 0, 1 });
					X.Unitize();
					const RE::NiPoint3 Z = X.Cross(Y);
					RE::NiMatrix3      R;
					R.entry[0][0] = X.x, R.entry[1][0] = X.y, R.entry[2][0] = X.z;
					R.entry[0][1] = Y.x, R.entry[1][1] = Y.y, R.entry[2][1] = Y.z;
					R.entry[0][2] = Z.x, R.entry[1][2] = Z.y, R.entry[2][2] = Z.z;
					float sc = a_scale * (1.0f + 0.5f * k / float(kTrailBones - 1));  // (the air spreads as it ages)
				// (it thins to nothing as it nears the camera - passing right by the lens it filled the view as a grey band)
				if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
					const float dc = (p - cam->cameraRoot->world.translate).Length();
					sc *= std::clamp((dc - 120.0f) / 380.0f, 0.0f, 1.0f);
				}
					bone->local.rotate = P.rotate.Transpose() * R;
					bone->local.translate = P.rotate.Transpose() * (p - P.translate) * (1.0f / P.scale);
					bone->local.scale = sc / P.scale;
					bone->UpdateDownwardPass(ud, 0);
				}
			}
		}

		void Talons(RE::NiAVObject* a_root, float a_curl);
		void AwayGone()
		{
			for (int i = 0; i < 2; ++i) {
				if (auto& n = g_away.node[i]; n && n->parent) n->parent->DetachChild(n.get());
				g_away.node[i].reset();
				g_away.host[i].reset();
			}
			g_away.t = -1;
		}
		void AwayTick(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (g_away.t < 0) {
				return;
			}
			g_away.t += a_delta;
			const float t = g_away.t;
			if (t > 4.5f || (!g_away.node[0] && !g_away.node[1])) {
				AwayGone();
				return;
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
			RE::NiTransform W;
			W.translate = pos;
			W.rotate = AxisAngle(up, -sd * 0.55f * climb) * AxisAngle(fwd, roll) * AxisAngle(right, 0.42f * climb) * R0;
			W.scale = g_away.scale;
			const bool first = RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
			for (int i = 0; i < 2; ++i) {
				auto& node = g_away.node[i];
				if (!node) continue;
				// (Noah: you went invisible with it as you landed) it hangs in the world now, not on you - a fade node of its own
				// under yours hid you both until it was gone
				auto* hostNow = g_away.host[i].get();
				if (!hostNow || node->parent != hostNow) {  // (the cell's 3D was rebuilt under it: it is gone)
					AwayGone();
					return;
				}
				const auto& H = hostNow->world;
				const float hs = std::max(1e-4f, H.scale);
				node->local.rotate = H.rotate.Transpose() * W.rotate;
				node->local.translate = H.rotate.Transpose() * (W.translate - H.translate) * (1.0f / hs);
				node->local.scale = W.scale / hs;
				auto* root = node.get();
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
					const auto& k = DragonPoses::kLegGlide;
					const auto& dv = DragonPoses::kLegDive;  // (streamlined for the climb away)
					if (auto* n = root->GetObjectByName(fmt::format("DG_Leg{}1", L).c_str())) n->local.rotate = Rot(k[0] + (dv[0] - k[0]) * tuck, 0, 0);
					if (auto* n = root->GetObjectByName(fmt::format("DG_Leg{}2", L).c_str())) n->local.rotate = Rot(k[1] + (dv[1] - k[1]) * tuck, 0, 0);
					if (auto* n = root->GetObjectByName(fmt::format("DG_Leg{}3", L).c_str())) n->local.rotate = Rot(k[2] + (dv[2] - k[2]) * tuck + 0.1f * open, 0, 0);
				}
				Talons(root, DragonPoses::kToesGlide + (DragonPoses::kToesDive - DragonPoses::kToesGlide) * tuck);
				// the climb away: strong wing-beats (1.1 s each), then out into a glide
				WingPose(root, t, [t](float a_lag) { const float k = t - 0.15f - a_lag; return k < 3.3f ? WingBeat(std::fmod(std::max(k, 0.0f), 1.1f), 1.1f) : 0.0f; }, 0.0f);
				DragonWind(root, 0.75f * (1 - Smooth((t - 3.2f) / 1.0f)), 0.0f);
				RE::NiUpdateData ud;
				root->Update(ud);
			}
			{  // its trails, from where the tips were while it held you on
				auto* seen = g_away.node[first && g_away.node[1] ? 1 : 0].get();
				if (!seen) seen = g_away.node[0] ? g_away.node[0].get() : g_away.node[1].get();
				TrailSample(seen, g_awayTrail, a_delta);
				for (auto& n : g_away.node) {
					if (n) TrailPlace(n.get(), g_awayTrail, W.scale);
				}
			}
			{
				static float lastBeat = 0;
				const float  k = std::fmod(std::max(t - 0.15f, 0.0f), 1.1f);
				if (t > 1.0f && t < 3.4f && lastBeat < 0.4f && k >= 0.4f) {
					PlayGameSound(DragonSound::kFlap, a_player, nullptr, 0.7f);
				}
				lastBeat = k;
			}
		}

		const char* AwayModel(int a_bond)
		{
			static constexpr const char* kAway[5] = { "CurseOfAlduin\\fx\\dragon_away.nif", "CurseOfAlduin\\fx\\dragon_away_b1.nif", "CurseOfAlduin\\fx\\dragon_away_b2.nif",
				"CurseOfAlduin\\fx\\dragon_away_b3.nif", "CurseOfAlduin\\fx\\dragon_away_b4.nif" };
			return kAway[std::clamp(a_bond, 0, 4)];
		}
		int Bond();
		int Look() { return Echo::BoneDragon() ? 4 : std::clamp(Save::DragonBond(), 0, 3); }  // (Stage 5: the Bone-Wyrm's skin)
		void LetGo(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_vel, float a_roll, float a_baseScale)
		{
			AwayGone();  // (the last one, if still in the air, goes now)
			static RE::NiPointer<RE::NiNode> model[5];
			const int b = Look();
			if (!model[b]) {
				RE::BSModelDB::DBTraits::ArgsType args{};
				if (RE::BSModelDB::Demand(AwayModel(b), model[b], args) != RE::BSResource::ErrorCode::kNone || !model[b]) {
					model[b].reset();
					logger::warn("glider: no fly-off dragon model ({})", AwayModel(b));
					return;
				}
			}
			int made = 0;
			{
				// in the world - under the node your body hangs from in the cell, never under you
				auto* me = a_player->Get3D(false);
				auto* host = me && me->parent ? me->parent : nullptr;
				if (host) {
					if (auto* clone = model[b]->Clone()) {
						auto* obj = static_cast<RE::NiAVObject*>(clone);
						obj->local = RE::NiTransform{};
						obj->local.scale = 0.0f;  // (unseen until AwayTick places it, this same frame)
						host->AttachChild(obj, true);
						g_away.node[0].reset(obj);
						g_away.host[0].reset(host);
						++made;
					}
				}
			}
			if (!made) {
				return;
			}
			logger::info("glider: the dragon lets go at ({:.0f},{:.0f},{:.0f}), scale {:.3f}/{:.3f}", g_lastSpace.translate.x, g_lastSpace.translate.y, g_lastSpace.translate.z, g_lastSpace.scale, a_baseScale);
			static std::mt19937 rng{ std::random_device{}() };
			g_away.t = 0;
			g_away.start = g_lastSpace;
			g_away.bank = g_lastBank;
			g_away.vel = a_vel;
			g_awayTrail = g_trail;  // (its trails carry straight on from the ones it had)
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
			}
			g_away.scale = a_baseScale > 0 ? g_lastSpace.scale / a_baseScale : 1.0f;
			g_away.side = std::abs(a_roll) > 0.05f ? (a_roll > 0 ? 1.0f : -1.0f) : (rng() % 2 ? 1.0f : -1.0f);
			AwayTick(a_player, 0.0f);
			PlayGameSound(DragonSound::kFlapBig, a_player);
			PlayGameSound(DragonSound::kRoar, a_player, nullptr, 0.8f);
		}

		// ---- the Echo Altar's dragon bonds (Noah): three, bought in order at any Echo Altar, kept for the whole game
		//   I   Wind-Rider     a quicker glide that sinks slower; jump mid-glide and it beats its wings to climb (a few
		//                      seconds between beats). Its eyes glow ice-pale.
		//   II  Talon Strike   the shout Dragon's Updraft (learned with the bond): a whirlwind lifts you high, open the glider; attack
		//                      while looking down mid-glide and it hurls you at the ground - you land in a shockwave that
		//                      throws the dead about. Its markings turn glacier blue.
		//   III Alduin's Thrall (needs a city freed from the Curse) black scales, ember eyes - its fire breath is real now:
		//                      it turns on the dead below you, in the element of the spell in your hand (fire burns
		//                      hotter, frost slows them, lightning leaps to the next).
		// Its look: Paragliding.esp's animated object (ANIO 0x800) is given the bond's nif before you glide, the dragon
		// that flies off afterwards the same (tools/modelconv/dragonvariant.cpp, tools/dragontex.py).
		float g_flapT = -1, g_flapWait = 0;  // Bond I: the wing-beats' time, and the wait before the next
		bool  g_breathReq = false;           // Bond III: the breath asked for (right attack mid-glide)
		float g_breathCd = 0;                // ... and the 5 s before it can be again
		int   g_breathElDev = -1;            // (dev: the element to breathe, whatever is in your hand)
		float g_breathSway = 0;              // its head's sweep during the breath (rad)
		RE::FormID g_breathProjDev[3]{};     // (dev: a projectile to try for fire / frost / lightning)
		float g_breathScaleDev = -1, g_breathSpellDev = 1;
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
		constexpr float kUpdraftRise = 1.4f, kUpdraftEnd = 2.1f;  // its climb, then its hold at the top
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
			const int  b = Look();
			if (b == applied) {
				return;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* anio = dh ? dh->LookupForm<RE::TESObjectANIO>(0x800, "Paragliding.esp") : nullptr;
			if (!anio) {
				return;
			}
			static constexpr const char* kGlider[5] = { "NickNak\\ParaGlider.nif", "NickNak\\ParaGlider_b1.nif", "NickNak\\ParaGlider_b2.nif", "NickNak\\ParaGlider_b3.nif", "NickNak\\ParaGlider_b4.nif" };
			anio->SetModel(kGlider[b]);  // (the fly-off is the same bond's dragon_away nif, cloned: LetGo)
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
			       Director::IsFoe(a_actor);
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

		// the breath: the dragons' own fire breath (Noah: fire only - no more frost or lightning), in its bond's colour
		// (TintBreath)
		RE::BGSProjectile* BreathProjectile()
		{
			static RE::BGSProjectile* fire = RE::TESForm::LookupByID<RE::BGSProjectile>(0x000EBB75);
			if (g_breathProjDev[0]) {  // (dev: trying another look)
				if (auto* p = RE::TESForm::LookupByID<RE::BGSProjectile>(g_breathProjDev[0])) return p;
			}
			return fire;
		}

		// its fire in its bond's colour: each breath launched is watched until its model is in, then its effect shaders
		// are given the bond's greyscale-to-palette ramp (tools/firepalette.py) - (Noah) before any bond Skyrim's own fire,
		// Bond I blue, Bond II yellow, Bond III green
		struct Tinted
		{
			RE::ProjectileHandle h;
			int                  bond;
			float                age;
		};
		std::vector<Tinted> g_tinted;
		void TintBreath(float a_dt)
		{
			static RE::NiPointer<RE::NiTexture> pal[3];
			static const char*                   kPal[3] = { "textures\\CurseOfAlduin\\Dragon\\fire_b1.dds", "textures\\CurseOfAlduin\\Dragon\\fire_b2.dds",
				"textures\\CurseOfAlduin\\Dragon\\fire_b3.dds" };
			for (auto it = g_tinted.begin(); it != g_tinted.end();) {
				auto p = it->h.get();
				it->age += a_dt;
				auto* root = p ? p->Get3D() : nullptr;
				if (!p || it->age > 3.0f) {
					it = g_tinted.erase(it);
					continue;
				}
				if (!root) {
					++it;
					continue;
				}
				const int k = std::clamp(it->bond, 1, 3) - 1;
				if (!pal[k]) {
					RE::BSShaderManager::GetTexture(kPal[k], true, pal[k], false);
				}
				int n = 0;
				if (pal[k]) {
					RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
						auto* prop = g->GetGeometryRuntimeData().shaderProperty.get();
						auto* rtti = prop ? prop->GetRTTI() : nullptr;
						if (!rtti || std::string_view(rtti->GetName()) != "BSEffectShaderProperty") {
							return RE::BSVisit::BSVisitControl::kContinue;
						}
						auto* sp = static_cast<RE::BSShaderProperty*>(prop);
						auto* m = static_cast<RE::BSEffectShaderMaterial*>(sp->material);
						if (!m) {
							return RE::BSVisit::BSVisitControl::kContinue;
						}
						// (a material of its own: the one it came with is shared by every fire breath, the real dragons' too)
						if (auto* nm = static_cast<RE::BSEffectShaderMaterial*>(m->Create())) {
							nm->CopyMembers(m);
							nm->greyscaleTexture = RE::NiPointer<RE::NiSourceTexture>(static_cast<RE::NiSourceTexture*>(pal[k].get()));
							nm->greyscaleTexturePath = kPal[k];
							sp->SetMaterial(nm, true);
							nm->~BSEffectShaderMaterial();
							RE::free(nm);
						}
						sp->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteColor);
						++n;
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
				static int logged = 0;
				if (logged < 6) {
					++logged;
					logger::info("glider: breath tinted for Bond {} ({} effect shaders)", it->bond, n);
				}
				it = g_tinted.erase(it);
			}
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
		// (Noah: base damage balanced, then 25% more for each bond - x1, x1.25, x1.5, x1.75): everything of the dead inside the
		// fire (a cone widening from its jaws, 16 m long) is burned every 0.2 s - about 60% of a round's undead health a
		// second before any bond, so a full breath kills most of what it is held on; others lose 15% of their health a second
		float BreathMult() { return 1.0f + 0.25f * static_cast<float>(std::clamp(Save::DragonBond(), 0, 3)); }
		constexpr float kBreathReach = 1650.0f;
		void Ignite(RE::Actor* a_actor);
		void BreathHits(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, const RE::NiPoint3& a_dir)
		{
			const float mult = BreathMult();
			int         n = 0;
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto a = h.get();
				if (!Foe(a.get(), a_player)) {
					continue;
				}
				const RE::NiPoint3 to = a->GetPosition() + RE::NiPoint3{ 0, 0, 60 } - a_from;
				const float        along = to.Dot(a_dir);
				if (along < 0.0f || along > kBreathReach) {
					continue;
				}
				const float side = (to - a_dir * along).Length();
				if (side > 75.0f + 0.13f * along) {  // (the fire widens as it goes)
					continue;
				}
				const bool  undead = Director::IsRoundUndead(a.get());
				const float dmg = undead ? Director::RoundHealth() * 0.12f : a->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.03f;
				Hurt(a.get(), dmg * mult);
				Ignite(a.get());
				++n;
			}
			static float logT = 0;
			if (n && (logT -= 0.2f) <= 0) {
				logT = 1.0f;
				logger::info("glider: the breath burns {} (x{:.2f})", n, mult);
			}
		}

		// the breath (Noah: the Fire Breath shout's own fire): where it aims, and how far into it it is
		struct BreathAim
		{
			float        grow = 0, level = 0;  // (level: how much of its fire shows - in as it starts, out as it ends)
			RE::NiPoint3 dir{ 0, 1, 0 };
		};
		BreathAim g_breath;
		// its fire (Noah: from the dragon, burning the dead only): Skyrim's own Fire Breath flame (magic\magfirebreathshout.nif)
		// hung at the dragon's jaws - a copy on your third- and first-person 3D, placed every frame by SwingBody, so it comes
		// out of the jaws, sweeps with the head and flies on with you. Only a look: the burning is BreathHits' alone.
		RE::NiPointer<RE::NiAVObject> g_flame[2];
		void FlameGone()
		{
			for (auto& n : g_flame) {
				if (n && n->parent) n->parent->DetachChild(n.get());
				n.reset();
			}
		}
		RE::NiMatrix3 AimAlong(RE::NiPoint3 a_dir)  // a turn taking +Y to a_dir, kept level across
		{
			a_dir.Unitize();
			RE::NiPoint3 X = a_dir.Cross({ 0, 0, 1 });
			if (X.Length() < 1e-3f) X = { 1, 0, 0 };
			X.Unitize();
			const RE::NiPoint3 Z = X.Cross(a_dir);
			RE::NiMatrix3      R;
			R.entry[0][0] = X.x, R.entry[1][0] = X.y, R.entry[2][0] = X.z;
			R.entry[0][1] = a_dir.x, R.entry[1][1] = a_dir.y, R.entry[2][1] = a_dir.z;
			R.entry[0][2] = Z.x, R.entry[1][2] = Z.y, R.entry[2][2] = Z.z;
			return R;
		}
		void FlamePose(int a_f, RE::NiAVObject* a_root, RE::NiAVObject* a_space)
		{
			auto& node = g_flame[a_f];
			auto* host = a_root ? a_root->AsNode() : nullptr;
			auto* mouth = a_space ? a_space->GetObjectByName("DG_Mouth") : nullptr;
			if (g_breath.level <= 0.001f || !host || !mouth) {
				if (node && node->parent) node->parent->DetachChild(node.get());
				node.reset();
				return;
			}
			if (!node || node->parent != host) {
				static RE::NiPointer<RE::NiNode> model;
				if (!model) {
					RE::BSModelDB::DBTraits::ArgsType args{};
					if (RE::BSModelDB::Demand("Magic\\magfirebreathshout.nif", model, args) != RE::BSResource::ErrorCode::kNone || !model) {
						model.reset();
						static bool told = false;
						if (!told) told = true, logger::warn("glider: no fire breath model");
						return;
					}
				}
				if (node && node->parent) node->parent->DetachChild(node.get());
				auto* clone = static_cast<RE::NiAVObject*>(model->Clone());
				if (!clone) return;
				clone->local = RE::NiTransform{};
				clone->local.scale = 0.0f;
				host->AttachChild(clone, true);
				node.reset(clone);
				RE::NiTimeController::StartAnimations(clone);  // (its flames' own animation, from now)
				logger::info("glider: the fire breath on your {} 3D", a_f == 0 ? "third-person" : "first-person");
			}
			// the flame's length: about 14 m at full (dev: breathproj 0 0 <scale>)
			const float base = g_breathScaleDev > 0.0f ? g_breathScaleDev : 9.0f;
			RE::NiTransform W;
			W.rotate = AimAlong(g_breath.dir);
			W.translate = mouth->world.translate + g_breath.dir * (20.0f * mouth->world.scale);
			W.scale = base * (0.35f + 0.65f * g_breath.grow) * std::max(0.05f, g_breath.level);
			const auto& H = host->world;
			const float hs = std::max(1e-4f, H.scale);
			node->local.rotate = H.rotate.Transpose() * W.rotate;
			node->local.translate = H.rotate.Transpose() * (W.translate - H.translate) * (1.0f / hs);
			node->local.scale = W.scale / hs;
			// (no update of our own: your body's update this frame places it - and runs its flames' animation on the game's
			// own clock, which a second update of ours would fight)
		}

		// the burned: alight for 3 s after the fire leaves them (a little more burning, Skyrim's fire on them), and the dead
		// among them crumble to ash (an ash pile you can loot, as Skyrim's own fire leaves)
		struct Burning
		{
			RE::ActorHandle h;
			float           left, tick;
			bool            ashed;
		};
		std::vector<Burning> g_burning;
		void Ignite(RE::Actor* a_actor)
		{
			static auto* fx = RE::TESForm::LookupByID<RE::TESEffectShader>(0x0001B212);  // FireFXShader
			const auto   h = a_actor->GetHandle();
			for (auto& b : g_burning) {
				if (b.h == h) {
					b.left = 3.0f;
					return;
				}
			}
			if (fx) a_actor->ApplyEffectShader(fx, 3.0f);
			g_burning.push_back({ h, 3.0f, 0.5f, false });
		}
		void BurnTick(float a_dt)
		{
			static auto* ashBase = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000001B);  // DefaultAshPile1
			const float mult = BreathMult();
			for (auto it = g_burning.begin(); it != g_burning.end();) {
				auto a = it->h.get();
				if (!a) {
					it = g_burning.erase(it);
					continue;
				}
				it->left -= a_dt;
				if (a->IsDead()) {
					// to ash, a moment after it falls
					if (!it->ashed && it->left < 2.0f && ashBase && a->Is3DLoaded()) {
						it->ashed = true;
						if (!a->extraList.GetByType<RE::ExtraAshPileRef>()) {
							if (auto ash = a->PlaceObjectAtMe(ashBase, false)) {
								a->extraList.Add(new RE::ExtraAshPileRef(ash->CreateRefHandle()));
								if (auto* r3 = a->Get3D()) r3->SetAppCulled(true);
								logger::info("glider: burned to ash");
							}
						}
					}
					if (it->left <= 0) {
						it = g_burning.erase(it);
					} else {
						++it;
					}
					continue;
				}
				if ((it->tick -= a_dt) <= 0) {
					it->tick = 0.5f;
					const bool undead = Director::IsRoundUndead(a.get());
					Hurt(a.get(), (undead ? Director::RoundHealth() * 0.03f : a->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth) * 0.01f) * mult);
					if (a->IsDead()) it->left = std::max(it->left, 2.2f);
				}
				if (it->left <= 0) {
					it = g_burning.erase(it);
				} else {
					++it;
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
			// (Noah) Whiterun's Thu'um: Dragon's Updraft is the shout Whiterun's Echo Altar gives - not a dragon bond's
			auto* sh = UpdraftShout();
			if (!sh) {
				return;
			}
			if (!Curse::Freed(1)) {
				return;
			}
			if (a_player->HasShout(sh)) {
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
			Hud::Banner("WHITERUN'S THU'UM: Dragon's Updraft - shout it and a whirlwind lifts you; open your glider at the top", 5.0f);
			logger::info("glider: Dragon's Updraft learned ({:08X})", sh->GetFormID());
		}
		void TestKit()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			Save::SetDragonBond(3);
			ApplyBondLook();
			auto* sh = UpdraftShout();
			if (!p || !sh || p->HasShout(sh)) {
				return;
			}
			for (auto w : gen::kUpdraftWords) {
				if (auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(w, gen::kPlugin)) {
					Gun::Console(fmt::format("player.teachword {:08X}", f->GetFormID()));
					Gun::Console(fmt::format("player.unlockword {:08X}", f->GetFormID()));
				}
			}
			p->AddShout(sh);
			logger::info("testkit: Dragon's Updraft and the third bond");
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
			if (t > 2.6f) {
				WhirlGone();
				return;
			}
			const float level = 1.0f * Smooth(t / 0.18f) * (1 - Smooth((t - 1.7f) / 0.8f));  // (Noah: transparent - faint layers of air)
			for (int w = 0; w < 2; ++w) {
				auto& n = g_whirl.node[w];
				if (!n) continue;
				n->local.translate = { 0, 0, -60.0f * Smooth(t / 1.0f) };
				n->local.rotate = Rot(0, 0, -t * 9.0f);
				n->local.scale = 0.7f + 0.5f * Smooth(t / 0.8f);
				// (Noah: blinding in first person) thin as air: faint seen from behind you, fainter still from inside it
				const float see = w == 1 ? 0.12f : 0.35f;
				for (int i = 1; i <= 6; ++i) {
					FxLevel(n.get(), fmt::format("WW{}", i).c_str(), level * see);
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
				logger::info("glider: Updraft not taken (dragon {}, lifting {})", g_hadDragon, g_liftOff);
				return;
			}
			if (OnGround(a_player)) {
				a_player->SetPosition(a_player->GetPosition() + RE::NiPoint3{ 0, 0, 20.0f }, true);  // off the ground
			}
			g_liftOff = 1, g_takeT = 0, g_updraftAge = 0;
			// (Noah: your character jumps as the whirlwind takes you)
			const bool jumped = a_player->NotifyAnimationGraph("JumpStandingStart") || a_player->NotifyAnimationGraph("JumpUp") ||
			                    a_player->NotifyAnimationGraph("JumpDirectionalStart");
			logger::info("glider: Updraft jump animation {}", jumped ? "played" : "not taken");
			Forms::PlayAt(gen::Sound::shout_gale, a_player);
			WhirlStart(a_player);
			logger::info("glider: Dragon's Updraft");
		}

		void BondTick(RE::PlayerCharacter* a_player, float a_delta)
		{
			ApplyBondLook();
			g_flapWait = std::max(0.0f, g_flapWait - a_delta);
			g_breathCd = std::max(0.0f, g_breathCd - a_delta);
			TintBreath(a_delta);
			BurnTick(a_delta);
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
				// (Noah: higher) up about 27 m in 1.4 s, slowing to the top; then it holds you there a moment, barely sinking, so
				// there is time to open the glider (pressed on the way up, it opens at the top by itself)
				if (cc && g_takeT < kUpdraftEnd && !g_hadDragon) {
					cc->context.currentState = RE::hkpCharacterStateType::kInAir;
					v.z = g_takeT < kUpdraftRise ? 60.0f + 2600.0f * (1 - Smooth(g_takeT / kUpdraftRise)) : 60.0f - 140.0f * Smooth((g_takeT - kUpdraftRise) / 0.7f);
					SetVel(cc, v);
				}
				if (g_glideQueued && g_takeT > kUpdraftRise && !g_hadDragon) {
					g_glideQueued = false;
					if (!a_player->NotifyAnimationGraph("StartPara")) {
						DevTools::PressKey(RE::INPUT_DEVICE::kKeyboard, 0x12, 0.05f);
					}
					logger::info("glider: opened at the Updraft's top");
				}
				if (g_takeT > kUpdraftEnd || g_hadDragon) {
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
				if ((g_diveT > 0.15f && OnGround(a_player)) || g_diveT > 30.0f) {  // (from high up it can be a long way down)
					g_dive = false;
					if (g_diveT <= 30.0f) {
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
		RE::NiPoint3 g_grabOff{};    // the dragon moved (DG_Space's own units) so its bar reaches your hands
		RE::NiPoint3 g_sway{}, g_swayV{};  // (Noah: hanging from it, you swing) the dragon's lead on you, in its own space
		float        g_grip = 0;     // how firmly it holds you (0 in the swoop .. 1)

		// the talons: each joint curled about its own axis (src/DragonPoses.h kToes), a_curl of a full curl
		void Talons(RE::NiAVObject* a_root, float a_curl)
		{
			if (!a_root) return;
			for (auto& t : DragonPoses::kToes) {
				if (auto* n = a_root->GetObjectByName(t.bone)) n->local.rotate = AxisAngle({ t.x, t.y, t.z }, -a_curl * t.amount);
			}
		}
		float g_trap = 0;  // the harness's bar swung forward (+) or back about its rings (rad)

		float SignedAngle(RE::NiPoint3 a_from, RE::NiPoint3 a_to, const RE::NiPoint3& a_axis)
		{
			a_from = a_from - a_axis * a_from.Dot(a_axis);
			a_to = a_to - a_axis * a_to.Dot(a_axis);
			return std::atan2(a_axis.Dot(a_from.Cross(a_to)), a_from.Dot(a_to));
		}

		// ---- the glide (Noah): the dragon is called, swoops in from high behind you and catches you by its harness; then
		// you fly it with the left stick / WASD - forward dives (gathering speed), back pulls up (spending it on height),
		// nothing held glides; left / right turn it, sharply, its whole body leaning into the turn
		bool         g_holding = false;   // its harness in your hands (from the catch until it lets go)
		bool         g_place = false;     // a glide is on: SwingBody (the pose hook) places the dragon in the world this frame
		bool         g_placed = false;    // ... and it has (from then on nothing else moves its DG_Space)
		float        g_deployT = -1, g_swoopLen = 1.45f;
		RE::NiPoint3 g_swoopOff{};        // the swoop: where it is from where it will hold you (your heading's frame: x right, y ahead, z up)
		float        g_glideYaw = 0;      // your heading through the glide (ours: set on you every frame)
		bool         g_yawOwned = false;
		float        g_gamma = 0, g_speed = 950.0f;  // the flight path's angle (rad, + climbing) and its speed
		float        g_inX = 0, g_inY = 0;           // the stick (eased)
		float        g_roll = 0;                     // the dragon's lean (+: its right wing down)
		float        g_catchK = 0;                   // 0 at the catch .. 1 once the controls are fully yours
		float        g_stickX = 0, g_stickY = 0;     // the left stick, as its own events last said
		bool         g_keyF = false, g_keyB = false, g_keyL = false, g_keyR = false;
		bool         g_sprintKey = false;  // sprint held (omni-sprint in first person)
		float        g_omni = 0;           // the omni-sprint's share now (eased 0..1)
		bool         g_chaseOn = false;              // (dev: the filming camera is turning the view itself)
		const RE::NiMatrix3* BindRot(const char* a_name);
		float BankSign() { return Settings::DragonBankReverse() ? -1.0f : 1.0f; }
		float DiveFold() { return Smooth((g_divePitch - 0.15f) / 0.55f); }  // the wings drawn in, 0 .. 1
		// the stick or WASD, x right and y forward: the game's own reading, or ours from the events if it has none
		RE::NiPoint2 MoveInput()
		{
			RE::NiPoint2 in{};
			if (auto* pc = RE::PlayerControls::GetSingleton()) in = pc->data.moveInputVec;
			RE::NiPoint2 own{ (g_keyR ? 1.0f : 0.0f) - (g_keyL ? 1.0f : 0.0f), (g_keyF ? 1.0f : 0.0f) - (g_keyB ? 1.0f : 0.0f) };
			if (std::abs(g_stickX) > std::abs(own.x)) own.x = g_stickX;
			if (std::abs(g_stickY) > std::abs(own.y)) own.y = g_stickY;
			// (the keys' and stick's own events come first: True Directional Movement rewrites the game's reading to "forward"
			// whichever way you push - back and the sides all dived)
			if (own.x != 0.0f || own.y != 0.0f) in = own;
			auto dz = [](float a) { return std::abs(a) < 0.18f ? 0.0f : (a - std::copysign(0.18f, a)) / 0.82f; };
			return { std::clamp(dz(in.x), -1.0f, 1.0f), std::clamp(dz(in.y), -1.0f, 1.0f) };
		}
		// how far you are above the ground (or water) here - the land's height only (buildings aside): 1e9 unknown
		float HeightAboveGround(RE::PlayerCharacter* a_player)
		{
			const auto p = a_player->GetPosition();
			float      g = -1e9f;
			if (auto* tes = RE::TES::GetSingleton()) {
				float h = 0;
				if (tes->GetLandHeight(p, h)) g = h;
				if (auto* cell = a_player->GetParentCell()) {
					float w = 0;
					if (cell->GetWaterHeight(p, w) && w < 1e8f) g = std::max(g, w);
				}
			}
			return g > -1e8f ? p.z - g : 1e9f;
		}
		// (Noah: the camera kept turning on its own while gliding, and a dodge snapped it round) turn yourself without
		// turning the view: in third person the camera looks along your heading plus its free rotation, so whatever turns
		// you (the glide's steering, a dodge - and True Directional Movement turning you toward the stick) the free
		// rotation is set so the camera stays exactly where you left it
		float ViewYaw(RE::PlayerCharacter* a_p)
		{
			auto* pcam = RE::PlayerCamera::GetSingleton();
			if (pcam && pcam->currentState && pcam->currentState->id == RE::CameraState::kThirdPerson) {
				auto* tp = static_cast<RE::ThirdPersonState*>(pcam->currentState.get());
				return a_p->GetAngleZ() + (tp->freeRotationEnabled ? tp->freeRotation.x : 0.0f);
			}
			return a_p->GetAngleZ();
		}
		void TurnKeepView(RE::PlayerCharacter* a_p, float a_yaw)
		{
			auto* pcam = RE::PlayerCamera::GetSingleton();
			if (pcam && pcam->currentState && pcam->currentState->id == RE::CameraState::kThirdPerson) {
				auto*       tp = static_cast<RE::ThirdPersonState*>(pcam->currentState.get());
				const float view = ViewYaw(a_p);
				a_p->SetHeading(a_yaw);
				float fx = view - a_yaw;
				while (fx > 3.14159f) fx -= 6.28318f;
				while (fx < -3.14159f) fx += 6.28318f;
				tp->freeRotationEnabled = true;
				tp->freeRotation.x = fx;
				return;
			}
			a_p->SetHeading(a_yaw);
		}
		// (Noah: weapons away just before you take the harness) what is in your hands is hidden while you hang from it
		// (the bow, a blade, a shield, the spells' hand glow nodes), and shown again when the dragon lets go
		void HideHands(bool a_hide)
		{
			static bool hidden = false;
			if (hidden == a_hide) {
				return;
			}
			hidden = a_hide;
			auto* p = RE::PlayerCharacter::GetSingleton();
			for (const bool fp : { false, true }) {
				auto* root = p ? p->Get3D(fp) : nullptr;
				for (const char* n : { "WEAPON", "SHIELD", "AnimObjectR", "AnimObjectL" }) {
					if (auto* o = root ? root->GetObjectByName(n) : nullptr) {
						o->SetAppCulled(a_hide);
					}
				}
			}
			logger::info("glider: weapons {}", a_hide ? "put out of sight for the harness" : "back in hand");
		}
		void EndHold()
		{
			HideHands(false);
			g_holding = false, g_place = false, g_placed = false, g_yawOwned = false, g_grip = 0;
			g_breath.grow = 0, g_breath.level = 0;
		}

		void DevBreath(int a_element)  // dev: Bond III's breath now, in that element (0 fire, 1 frost, 2 lightning)
		{
			g_breathElDev = a_element;
			g_breathReq = true;
		}
		void DevBreathProj(int a_element, RE::FormID a_proj, float a_scale, float a_spell)  // dev: try a breath look
		{
			if (a_element >= 0 && a_element < 3) g_breathProjDev[a_element] = a_proj;
			g_breathScaleDev = a_scale, g_breathSpellDev = a_spell;
		}
		void DevFlap()  // dev: Bond I's climbing wing-beats, as if jump were pressed mid-glide
		{
			if (g_holding && g_flapT < 0) {
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
					// (Noah: the whirlwind never came) once you know the shout it works - the bond teaches it, it no longer
					// also checks the bond each time you speak it
					logger::info("glider: Dragon's Updraft spoken (Bond {})", Bond());
					Updraft(RE::PlayerCharacter::GetSingleton());
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
			static float lastYaw = 0, turn = 0, wanderT = 0, breath = 0, jaw = 0, lookYaw = 0, lookPitch = 0, roll = 0, pitch = 0;
			static float deployT = -1, baseScale = 0, legT = 0, breathT_ = 0;
			static RE::NiPoint3 baseTrans{};
			RE::NiAVObject* bank[2]{};
			RE::NiAVObject* look[2]{};
			RE::NiAVObject* jawN[2]{};
			RE::NiAVObject* mouth = nullptr;
			RE::NiAVObject* head = nullptr;
			RE::NiAVObject* space[2]{};
			RE::NiAVObject* fold[2][2]{};
			RE::NiAVObject* legN[2][2][3]{};
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
					}
					if (bank[i] && !mouth) {
						mouth = root->GetObjectByName("DG_Mouth");
						head = root->GetObjectByName("DG_Head");
					}
				}
			}
			const float yaw = a_player->GetAngleZ();
			static RE::NiPoint3 lastPos{}, vel{};
			const RE::NiPoint3  pos = a_player->GetPosition();
			AwayTick(a_player, a_delta);
			static float preFallV = 0;  // (how fast you were falling as you called it: the glider's opening slows you before we see it)
			if (!bank[0] && !bank[1]) {  // not gliding
				if (g_hadDragon && !letGone) {  // the glider was just put away: the dragon lets go and flies off
					LetGo(a_player, vel, roll, kDragonScale);
				}
				g_hadDragon = false, letGone = false, sawGliding = false;
				EndHold();
				if (g_breathSound.IsValid()) g_breathSound.Stop();
				DiveRush(a_player, 0.0f);  // (eases the view back)
				lastYaw = yaw, turn = 0, breath = 0, jaw = 0, deployT = -1, roll = 0, g_roll = 0, pitch = 0;
				g_armHoldOn = false, g_armHold = 0, g_flapT = -1, g_liftT = -1;
				g_deployT = -1, g_divePitch = 0, g_inX = g_inY = 0;
				g_grabOff = {};
				std::memset(g_ik, 0, sizeof(g_ik));
				g_trap = 0;
				if (a_delta > 1e-3f) preFallV += ((pos.z - lastPos.z) / a_delta - preFallV) * std::min(1.0f, a_delta * 12.0f);
				{
					// (Noah: running in the air, slowly sinking) the paraglider's slow fall left on with no glider out - in the air,
					// no dragon, sinking slowly and steadily for half a second: it is ended
					static float stuckT = 0;
					bool         pgNow = false;
					a_player->GetGraphVariableBool("bParaGliding", pgNow);
					const bool slowSink = !OnGround(a_player) && !pgNow && g_liftOff == 0 && preFallV < -40.0f && preFallV > -420.0f;
					stuckT = slowSink ? stuckT + a_delta : 0.0f;
					static bool  forceFall = false;
					static float fallVz = 0;
					if (stuckT > 0.6f && !forceFall) {
						stuckT = 0;
						a_player->NotifyAnimationGraph("EndPara");
						forceFall = true, fallVz = preFallV;
						logger::info("glider: the paraglider's slow fall was left on with no glider out ({:.0f}/s) - ended", preFallV);
					}
					if (forceFall) {  // (and you fall as anyone falls until you land, whatever still slows you)
						if (OnGround(a_player) || pgNow || g_liftOff != 0) {
							forceFall = false;
						} else if (auto* cc = a_player->GetCharController()) {
							RE::NiPoint3 fv{};
							GetVel(cc, fv);
							fallVz = std::max(fallVz - 1300.0f * a_delta, -2400.0f);
							if (fv.z > fallVz) {
								fv.z = fallVz;
								SetVel(cc, fv);
							}
						}
					}
				}
				lastPos = pos;
				return;
			}
			static RE::NiPoint3 acc{};
			if (a_delta > 1e-3f) {
				const RE::NiPoint3 was = vel;
				vel = vel + ((pos - lastPos) * (1.0f / a_delta) - vel) * std::min(1.0f, a_delta * 6.0f);
				acc = acc + ((vel - was) * (1.0f / a_delta) - acc) * std::min(1.0f, a_delta * 4.0f);
			}
			{
				// the dragon's lead on you, on a spring (you hang below it like a pendulum): ahead of you as it speeds up,
				// in toward the turn, lifting on each downstroke and dipping as the wings rise, out ahead in a dive
				const float  yawNow = a_player->GetAngleZ();
				const RE::NiPoint3 F{ std::sin(yawNow), std::cos(yawNow), 0 };
				const float  aF = acc.x * F.x + acc.y * F.y;
				static float lastYawS = yawNow;
				float        dyS = yawNow - lastYawS;
				while (dyS > 3.14159f) dyS -= 6.28318f;
				while (dyS < -3.14159f) dyS += 6.28318f;
				lastYawS = yawNow;
				const float turnS = std::clamp(dyS / std::max(a_delta, 1e-3f), -2.5f, 2.5f);
				const float beat = g_flapT >= 0 ? FlapWing(g_flapT) : 0.0f;
				const float fold = DiveFold();
				const RE::NiPoint3 want{ std::clamp(turnS * 4.0f, -10.0f, 10.0f), std::clamp(aF * 0.03f, -30.0f, 30.0f) + 22.0f * fold, -12.0f * beat - 8.0f * fold };
				const float dt2 = std::min(a_delta, 0.05f);
				g_swayV = g_swayV + ((want - g_sway) * 30.0f - g_swayV * 8.0f) * dt2;  // (a little under-damped: it overshoots, swings back)
				g_sway = g_sway + g_swayV * dt2;
			}
			lastPos = pos;
			// the glider opens: the dragon comes for you (every frame, eased)
			static bool quickSwoop = false;
			if (deployT < 0) {
				deployT = 0;
				for (auto* sp : space) if (sp) baseTrans = sp->local.translate;
				baseScale = kDragonScale;
				g_glideYaw = yaw, g_yawOwned = true, g_holding = false, g_grip = 0;
				g_trail.Reset();
				BindRot("NPC Pelvis [Pelv]");  // (the skeleton's rest pose read here, on the game's thread, not in the pose hook)
				g_inX = g_inY = 0, g_roll = 0, roll = 0, pitch = 0, g_divePitch = 0;
				// the swoop's length: 1.45 s - quicker after the Updraft, and never longer than your fall to the ground
				// takes (so it always has you before you land)
				quickSwoop = g_updraftAge < 4.0f;
				float len = quickSwoop ? 0.65f : 1.0f;  // (Noah: no hanging in the air waiting for it)
				const float h = HeightAboveGround(a_player);
				if (h < 1e8f) {
					RE::NiPoint3 v0{};
					if (auto* cc0 = a_player->GetCharController()) GetVel(cc0, v0);
					const float down = std::max(0.0f, -std::min(v0.z, preFallV));
					const float tGround = (-down + std::sqrt(down * down + 2.0f * 980.0f * std::max(0.0f, h - 60.0f))) / 980.0f;
					len = std::clamp(std::min(len, tGround * 0.8f), 0.45f, len);
				}
				g_swoopLen = len;
				PlayGameSound(DragonSound::kRoar, a_player, nullptr, 0.7f);
				PlayGameSound(DragonSound::kFlapBig, a_player);
				logger::info("glider: the dragon is called - it reaches you in {:.2f} s", len);
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
				// (Noah: running in the air, slowly sinking) only a real landing - on the ground a moment, not brushing the
				// ledge you jumped from as the glider opened
				static float groundT = 0;
				groundT = OnGround(a_player) ? groundT + a_delta : 0.0f;
				const bool landed = deployT > 0.6f && groundT > 0.12f;
				if (((known && sawGliding && !pg) || landed) && !letGone) {
					letGone = true;
					if (g_hadDragon) {
						LetGo(a_player, vel, roll, kDragonScale);
					}
					// (Noah: you and the dragon vanished after landing; the Updraft never worked again) it has let you go:
					// nothing of the glide holds on to you any more
					g_hadDragon = false;
					EndHold();
					// ... and the paraglider's own slow fall ends with it (left on, you ran in the air with no glider at all)
					a_player->NotifyAnimationGraph("EndPara");
					g_armHoldOn = false, g_armHold = 0, g_flapT = -1, g_liftT = -1, g_divePitch = 0;
					if (g_breathSound.IsValid()) g_breathSound.Stop();
					DiveRush(a_player, 0.0f);
					logger::info("glider: {} - the dragon lets go", landed ? "landed" : "the glide is over");
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
					letGone = false, sawGliding = pg2, deployT = -1, g_hadDragon = false;
					EndHold();
					logger::info("glider: a new glide before the last was put away - the dragon comes again");
				} else {
					for (auto* sp : space) if (sp) sp->local.scale = 0.0f;
					g_armHoldOn = false, g_armHold = 0;
					DiveRush(a_player, 0.0f);
					lastPos = pos;
					return;
				}
			}
			deployT += a_delta;
			g_deployT = deployT;
			const float kSwoop = g_swoopLen;
			g_place = true;
			if (g_yawOwned) {
				TurnKeepView(a_player, g_glideYaw);  // (yours to steer, with the stick: nothing else turns you mid-glide)
			}
			// your body keeps its fall until it has you (the paraglider's own opening animation flips you over: held at
			// least until that has played out, about a second), then eases into the hang
			g_armHoldOn = true;
			constexpr float kEquipAnim = 1.0f;
			g_armHold = 1 - Smooth((deployT - std::max(kSwoop - 0.12f, kEquipAnim)) / 0.3f);
			const float u = std::clamp(deployT / kSwoop, 0.0f, 1.0f);
			// the swoop (Noah: smooth, no jumping about): from high behind you and off to one side it comes down on a long
			// curve - fast at first, slowing to your own speed as it reaches you (no jolt at the catch) - dipping a little
			// below and rising into you, so the harness comes up into your hands
			const float  e = 1 - (1 - u) * (1 - u) * (1 - u);
			const RE::NiPoint3 P0{ 420, -1700, 700 }, C1{ 460, -1100, 470 }, C2{ 170, -340, -60 }, P3{ 0, 0, 0 };
			const float  ie = 1 - e;
			g_swoopOff = P0 * (ie * ie * ie) + C1 * (3 * ie * ie * e) + C2 * (3 * ie * e * e) + P3 * (e * e * e);
			const RE::NiPoint3 tan = (C1 - P0) * (3 * ie * ie) + (C2 - C1) * (6 * ie * e) + (P3 - C2) * (3 * e * e);
			const float  inSwoop = 1 - Smooth((u - 0.55f) / 0.45f);
			const float  swYaw = std::atan2(-tan.x, std::max(1.0f, tan.y)) * inSwoop;
			const float  swPitch = std::atan2(tan.z, std::sqrt(tan.x * tan.x + tan.y * tan.y)) * inSwoop;
			const float  swRoll = -0.5f * BankSign() * std::sin(3.14159f * std::min(1.0f, u * 1.1f)) * inSwoop;  // (leaning into its curve)
			// its wings beat as it comes (whole strokes, ending as it flares them to brake into you), then glide
			const float flareAt = std::max(0.2f, kSwoop - 0.5f);
			const int   nBeats = std::max(1, static_cast<int>(std::lround(flareAt / 0.62f)));
			const float beatLen = flareAt / nBeats;
			auto swoopBeat = [flareAt, beatLen](float a_t) {
				if (a_t <= 0) return 0.0f;
				if (a_t < flareAt) return 0.85f * WingBeat(std::fmod(a_t, beatLen), beatLen);
				return 0.7f * std::sin(3.14159f * std::clamp((a_t - flareAt) / 0.85f, 0.0f, 1.0f));
			};
			{
				static float lastK = 0;
				const float  k = deployT < flareAt ? std::fmod(deployT, beatLen) / beatLen : 0.0f;
				if (deployT < flareAt && lastK < 0.45f && k >= 0.45f) PlayGameSound(DragonSound::kFlap, a_player, nullptr, 0.6f);
				lastK = k;
			}
			for (int i = 0; i < 2; ++i) {
				if (space[i] && !g_placed) {  // (where it hangs until the pose hook places it: SwingBody puts it in the world)
					space[i]->local.translate = baseTrans;
					if (baseScale > 0) space[i]->local.scale = baseScale;
				}
				if (fold[i][0]) fold[i][0]->local.rotate = Rot(0, 0, 0);
				if (fold[i][1]) fold[i][1]->local.rotate = Rot(0, 0, 0);
				// the wings: straight out in the glide, swaying; beating only to come for you and for a climb (Bond I's
				// wing-beat, the take-off's lift); drawn in for a dive; the outer wing lifted a little more in a turn
				const float flapT = g_flapT, liftT = g_liftT, dT = deployT;
				WingPose(a_player->Get3D(i == 1), deployT, [flapT, liftT, dT, swoopBeat](float a_lag) {
					return (flapT >= 0 ? FlapWing(flapT - a_lag) : 0.0f) + WingBeat(liftT - a_lag, 1.3f) + swoopBeat(dT - a_lag);
				}, DiveFold(), g_roll / 0.85f);
			}
			// the legs: trailing loose as it swoops in, then stretched back under it toes to its tail; swept further back in
			// a dive, swung forward with each downstroke of a climb and back as the wings rise
			{
				legT += a_delta;
				g_grip = Smooth((u - 0.88f) / 0.12f);
				if (u >= 0.8f) {
					HideHands(true);  // a moment before the bar reaches your hands
				}
				if (!g_holding && u >= 0.95f) {
					g_holding = true;
					logger::info("glider: caught - the harness is in your hands");
				}
				const float diveFold = DiveFold();
				const float tuck = Smooth(u);
				static float pump = 0;
				const float  beat = g_flapT >= 0 ? FlapWing(g_flapT - 0.08f) : 0.0f;
				const float  pumpTo = g_flapT >= 0 ? std::clamp(0.5f - 0.5f * beat / 1.1f, 0.0f, 1.0f) : 0.0f;
				pump += (pumpTo - pump) * std::min(1.0f, a_delta * 7.0f);
				const auto& G = DragonPoses::kLegGlide;
				const auto& Dv = DragonPoses::kLegDive;
				const auto& Fl = DragonPoses::kLegFlap;
				for (int sd = 0; sd < 2; ++sd) {
					const float ph = sd * 1.3f, lagS = sd * 0.06f;  // (the two legs not quite together)
					const float calm = (1 - diveFold) * (1 - pump);
					float k[3];
					for (int j = 0; j < 3; ++j) {
						const float held = G[j] * (1 - diveFold) + Dv[j] * diveFold;
						k[j] = held * (1 - pump) + Fl[j] * pump;
					}
					const float dangle[3] = { 0.15f, 0.2f, 0.3f };
					const float hip = dangle[0] * (1 - tuck) + k[0] * tuck + 0.06f * std::sin(legT * 1.5f + ph) * calm + lagS * pump;
					const float knee = dangle[1] * (1 - tuck) + k[1] * tuck + 0.08f * std::sin(legT * 1.5f + 0.7f + ph) * calm;
					const float foot = dangle[2] * (1 - tuck) + k[2] * tuck + 0.08f * std::sin(legT * 2.1f + 1.4f + ph) * calm;
					for (int i = 0; i < 2; ++i) {
						if (legN[i][sd][0]) legN[i][sd][0]->local.rotate = Rot(hip, 0, 0);
						if (legN[i][sd][1]) legN[i][sd][1]->local.rotate = Rot(knee, 0, 0);
						if (legN[i][sd][2]) legN[i][sd][2]->local.rotate = Rot(foot, 0, 0);
					}
				}
				const float toes = (DragonPoses::kToesGlide * (1 - diveFold) + DragonPoses::kToesDive * diveFold) * (1 - pump) + DragonPoses::kToesFlap * pump;
				for (int i = 0; i < 2; ++i) {
					Talons(a_player->Get3D(i == 1), toes + 0.05f * std::sin(legT * 1.1f) * (1 - diveFold));
				}
			}
			const float dt = std::max(1e-3f, a_delta);
			// the flight (Noah: the left stick / WASD - forward dives, back pulls up, nothing held glides; left / right turn,
			// much more sharply than before) - after the catch, eased in over 0.7 s
			const int   bond = Bond();
			const float cruise = bond >= 1 ? 950.0f : 820.0f;   // (Bond I glides quicker and sinks slower)
			const float sinkG = bond >= 1 ? -0.11f : -0.15f;
			const float top = 4800.0f + 500.0f * bond;  // (Noah: a dive builds a LOT more speed, and it carries on through the glide)
			if (auto* cc = a_player->GetCharController(); cc && !g_dive) {
				static RE::NiPoint3 fallH{}, glideV{};
				static float        fallV = 0;
				RE::NiPoint3        v{};
				GetVel(cc, v);
				if (deployT <= a_delta * 1.5f) {
					fallH = { v.x, v.y, 0 };
					// (Noah: you floated a moment before it took you) your fall carries straight on - as fast as you were
					// falling before the glider opened, not the slow drift it starts you on
					fallV = std::max(std::min(v.z, preFallV), -1400.0f);
					glideV = v;
				}
				if (deployT < kSwoop) {
					// (Noah: no gliding before it has you) until the harness is in your hands you fall as you were falling
					fallV = std::max(fallV - 980.0f * dt, -1400.0f);
					fallH = fallH * (1 - std::min(1.0f, 0.3f * dt));
					v = { fallH.x, fallH.y, fallV };
					const float h = std::sqrt(fallH.x * fallH.x + fallH.y * fallH.y);
					// (what the glide starts from at the catch: the dragon takes your fall into its own flight - never more than a
					// steep dive, so it pulls out quickly)
					g_gamma = std::max(std::atan2(fallV, std::max(h, 1.0f)), -0.6f);
					g_speed = std::clamp(std::sqrt(h * h + fallV * fallV), cruise, top);
					g_catchK = 0;
					glideV = v;
				} else {
					g_catchK = Smooth((deployT - kSwoop) / 0.7f);
					const auto in = MoveInput();
					float      iy = Settings::DragonInvertDive() ? -in.y : in.y;
					// (forward still held from the sprint you jumped from is not a dive: it counts once let go of)
					static bool latched = false;
					if (deployT - a_delta < kSwoop) latched = std::abs(in.y) > 0.3f;
					if (latched && std::abs(in.y) <= 0.3f) latched = false;
					if (latched) iy = 0.0f;
					g_inY += (iy - g_inY) * std::min(1.0f, dt * 5.0f);
					g_inX += (in.x - g_inX) * std::min(1.0f, dt * 8.0f);
					// the path's angle: gliding gently down; diving up to 60 degrees down; pulling up to 32 up while the
					// speed lasts (it can't climb on nothing: as the speed runs out the nose comes down to a glide)
					float gT = sinkG - 0.05f * std::abs(g_inX);  // (a turn costs a little height)
					if (g_inY > 0) {
						gT += (-1.05f - gT) * g_inY;
					} else if (g_inY < 0) {
						const float climbMax = 0.55f * std::clamp((g_speed - 720.0f) / 900.0f, 0.0f, 1.0f);
						gT += (std::max(climbMax, gT) - gT) * -g_inY;
					}
					gT = sinkG + (gT - sinkG) * g_catchK;
					g_gamma += (gT - g_gamma) * std::min(1.0f, dt * (gT < g_gamma ? 2.2f : 1.7f));
					// speed: gained going down, spent climbing, and back to its cruising speed level (height bought with speed)
					// (a dive pours speed in - gravity's whole pull and more; a climb spends it at half that; and once level it
					// bleeds away slowly, so a dive's speed carries far through the glide)
					const float slope = std::sin(g_gamma) - std::sin(sinkG);
					const float drag = g_speed > cruise ? 0.09f : 0.45f;
					g_speed += ((slope < 0 ? -2900.0f : -1300.0f) * slope - drag * (g_speed - cruise)) * dt;
					g_speed = std::clamp(g_speed, 600.0f, top);
					// turning: up to 100 degrees a second
					const float rate = 1.75f * g_inX * g_catchK;
					g_glideYaw += rate * dt;
					while (g_glideYaw > 3.14159f) g_glideYaw -= 6.28318f;
					while (g_glideYaw < -3.14159f) g_glideYaw += 6.28318f;
					TurnKeepView(a_player, g_glideYaw);
					const RE::NiPoint3 hd{ std::sin(g_glideYaw), std::cos(g_glideYaw), 0 };
					RE::NiPoint3       want = hd * (g_speed * std::cos(g_gamma));
					want.z = g_speed * std::sin(g_gamma);
					glideV = glideV + (want - glideV) * std::min(1.0f, dt * (2.0f + 6.0f * g_catchK));  // (eased out of the fall)
					v = glideV;
					if (g_liftT >= 0) {
						g_liftT += dt;
						v.z = std::max(v.z, 1150.0f * (1 - Smooth(g_liftT / 1.3f)));
						if (g_liftT > 1.3f) g_liftT = -1;
					}
					if (g_flapT >= 0) {  // (Bond I) each downstroke lifts you
						v.z = std::max(v.z, FlapLift(g_flapT));
					}
					if (v.z > glideV.z + 1.0f) {  // (a climb by the wings: carried on from there, not dropped back)
						glideV.z = v.z;
						g_gamma = std::atan2(v.z, std::max(1.0f, std::sqrt(v.x * v.x + v.y * v.y)));
					}
				}
				SetVel(cc, v);
			}
			// the dragon's nose down in a dive (its wings drawn in), up as it climbs
			g_divePitch += (std::clamp(sinkG - g_gamma, 0.0f, 1.2f) * g_grip * g_catchK - g_divePitch) * std::min(1.0f, dt * 4.0f);
			// the camera comes round behind you as you turn, unless you are looking about yourself
			{
				static float idle = 0;
				auto*        pcs = RE::PlayerControls::GetSingleton();
				const bool   looking = pcs && (std::abs(pcs->data.lookInputVec.x) + std::abs(pcs->data.lookInputVec.y) > 0.02f);
				idle = looking ? 0.0f : idle + dt;
				auto* pcam = RE::PlayerCamera::GetSingleton();
				// (Noah: on a controller the camera fought him as he looked about) it no longer swings back behind you on its
				// own - the view is yours alone while you glide
				constexpr bool kRecentre = false;
				if (kRecentre && !g_chaseOn && g_catchK > 0.5f && idle > 0.8f && pcam && pcam->currentState && pcam->currentState->id == RE::CameraState::kThirdPerson) {
					auto* tp = static_cast<RE::ThirdPersonState*>(pcam->currentState.get());
					if (tp->freeRotationEnabled) {
						float& fx = tp->freeRotation.x;
						while (fx > 3.14159f) fx -= 6.28318f;
						while (fx < -3.14159f) fx += 6.28318f;
						fx -= fx * std::min(1.0f, dt * 1.6f * Smooth((idle - 0.8f) / 0.6f));
					}
				}
				// (Noah: after a cliff jump the camera sometimes ended up high over the dragon, looking down) while you glide
				// the view's own tilt (free rotation's pitch) is eased back to none and your look is kept from tipping
				// past a steep dive's angle; if the camera still climbs far above you it is logged (for the next fix)
				if (!g_chaseOn && pcam && pcam->currentState && pcam->currentState->id == RE::CameraState::kThirdPerson) {
					auto* tp = static_cast<RE::ThirdPersonState*>(pcam->currentState.get());
					tp->freeRotation.y -= tp->freeRotation.y * std::min(1.0f, dt * 6.0f);
					if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
						auto& ax = pl->data.angle.x;
						ax = std::clamp(ax, -1.2f, 1.05f);
						if (pcam->cameraRoot) {
							const float above = pcam->cameraRoot->world.translate.z - (pl->GetPosition().z + 120.0f);
							static float told = 0;
							told -= dt;
							if (above > 260.0f && told <= 0) {
								told = 5.0f;
								logger::warn("glider: camera {:.0f} above you (pitch {:.2f}, free {:.2f}/{:.2f}, zoom {:.2f})", above, ax, tp->freeRotation.x, tp->freeRotation.y, tp->currentZoomOffset);
								tp->freeRotation.y = 0.0f;
							}
						}
					}
				}
				// ... and your steering never drags the view round: as the dragon turns you, the camera is turned back by as
				// much, so it stays where your right stick / mouse put it
				static float camYawWas = 0;
				static bool  camHeld = false;
				if (false && !g_chaseOn && g_yawOwned && pcam && pcam->currentState && pcam->currentState->id == RE::CameraState::kThirdPerson) {  // (TurnKeepView does it now)
					auto* tp = static_cast<RE::ThirdPersonState*>(pcam->currentState.get());
					if (!camHeld) {
						camHeld = true, camYawWas = g_glideYaw;
						if (!tp->freeRotationEnabled) tp->freeRotationEnabled = true, tp->freeRotation.x = 0.0f;
					}
					float d = g_glideYaw - camYawWas;
					while (d > 3.14159f) d -= 6.28318f;
					while (d < -3.14159f) d += 6.28318f;
					camYawWas = g_glideYaw;
					if (tp->freeRotationEnabled) tp->freeRotation.x -= d;
				} else {
					camHeld = false;
				}
			}
			float dy = yaw - lastYaw;
			while (dy > 3.14159f) dy -= 6.28318f;
			while (dy < -3.14159f) dy += 6.28318f;
			lastYaw = yaw;
			turn += (std::clamp(dy / dt, -2.5f, 2.5f) - turn) * std::min(1.0f, dt * 6.0f);   // rad/s, smoothed
			// (Noah: it turns its whole body - not just facing the new way) it leans hard into the turn (a real flyer's
			// bank: turning left, its left wing dips; the MCM can turn the lean the other way) and swings round head first
			roll += (BankSign() * std::clamp(turn * 0.5f, -0.9f, 0.9f) - roll) * std::min(1.0f, dt * 3.5f);
			g_roll = roll;
			static float swing = 0;
			swing += (std::clamp(-turn * 0.35f, -0.45f, 0.45f) - swing) * std::min(1.0f, dt * 3.0f);
			// its nose follows the flight path (down in a dive, up in a climb), lifted as it beats its wings
			const float tilt = g_flapT >= 0 ? FlapTilt(g_flapT) : 0.0f;
			pitch += (std::clamp(g_gamma * 0.85f, -1.0f, 0.5f) + tilt - pitch) * std::min(1.0f, dt * (tilt > 0 ? 4.0f : 2.5f));
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
			// (Noah: during the breath its head sweeps left and right, the fire with it - more of the dead caught)
			const float breathSway = breath > 0 ? 0.42f * std::sin(breathT_ * 4.2f) : 0.0f;
			g_breathSway = breathSway;
			lookYaw += (std::clamp(-rel * 0.6f + turn * 0.25f + wanderYaw + breathSway, -0.9f, 0.9f) - lookYaw) * std::min(1.0f, dt * (breath > 0 ? 9.0f : 2.5f));
			lookPitch += (wanderPitch - lookPitch) * std::min(1.0f, dt * 2.5f);
			// the breath: yours to call - right attack / right trigger mid-glide, 5 s apart
			static RE::ActorHandle target;
			static float           seekT = 0;
			const bool             canBreathe = mouth != nullptr;
			if (canBreathe && (seekT -= dt) <= 0) {
				seekT = 0.25f;
				auto* t = BreathTarget(a_player, mouth->world.translate);
				target = t ? t->GetHandle() : RE::ActorHandle{};
			}
			if (g_breathReq && breath <= 0 && canBreathe && g_holding) {
				breath = 1.6f;
				PlayGameSound(DragonSound::kFire, a_player, &g_breathSound);
				logger::info("glider: the dragon breathes (Bond {})", Bond());
			}
			g_breathReq = false;
			if (!canBreathe) {
				breath = 0;
			}
			const bool breathing = breath > 0;
			if (breathing && (breath -= dt) <= 0 && g_breathSound.IsValid()) {
				g_breathSound.Stop();
			}
			breathT_ = breathing ? breathT_ + dt : 0.0f;
			jaw += ((breathing ? 0.38f : 0.03f * (0.5f + 0.5f * std::sin(wanderT * 0.9f))) - jaw) * std::min(1.0f, dt * 8.0f);
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
			// Bond I's climbing wing-beats
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
			// the wind: a trail off each wingtip (Noah: see-through - fainter, brighter the faster you go), and in a dive the
			// air rushing past - streaks round its nose, its roar, the view widening a little as you gather speed
			{
				const float spd = vel.Length();
				const float held = Smooth((deployT - kSwoop * 0.6f) / 0.5f);
				const bool  flapping = g_flapT >= 0 || g_liftT >= 0;
				static float trailK = 0, diveK = 0;
				const float folded = Smooth((DiveFold() - 0.3f) / 0.5f);  // (no wingtip trail once the wings are drawn right in)
				const float trailWant = flapping ? 0.15f : held * (1 - folded) * (0.45f + 0.45f * Smooth((spd - 800.0f) / 1400.0f));
				trailK += (trailWant - trailK) * std::min(1.0f, dt * (flapping || folded > 0.2f ? 6.0f : 2.2f));
				const float diveWant = Smooth((g_divePitch - 0.12f) / 0.35f) * Smooth((spd - 1100.0f) / 1100.0f);
				diveK += (diveWant - diveK) * std::min(1.0f, dt * 3.0f);
				if (deployT <= a_delta * 1.5f) {
					trailK = 0, diveK = 0;
				}
				for (int i = 0; i < 2; ++i) {
					DragonWind(a_player->Get3D(i == 1), trailK, 0.0f);  // (Noah: no streaks off its body - only the wingtips')
				}
				DiveRush(a_player, diveK);
			}
			for (int i = 0; i < 2; ++i) {  // (the harness's bar, swung by SwingBody)
				if (auto* tr = space[i] ? space[i]->GetObjectByName("DG_Trapeze") : nullptr) tr->local.rotate = Rot(g_trap, 0, 0);
			}
			{  // where it is now: if the glider goes away this frame, the dragon in the world starts from here
				const bool first = RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
				const int  k = (first && space[1]) || !space[0] ? 1 : 0;
				if (space[k] && space[k]->world.scale > 1e-4f) {
					g_lastSpace = space[k]->world;
					g_hadDragon = true;
					if (bank[k]) g_lastBank = bank[k]->local.rotate;
				}
			}
			// the breath: the Fire Breath shout from its jaws toward where you aim (onto the dead near your aim), sweeping with its
			// head - grown out in a fifth of a second, burning everything in it, fading at the end (drawn by SwingBody)
			{
				static float hitT = 0;
				if (breathing && mouth && head) {
					// where you look, but down toward the ground ahead (Noah: the dead below; and seen from behind it is a jet of
					// fire sweeping down ahead of you - aimed exactly along your view it was only a dot behind its head)
					float aimYaw = yaw, aimDown = 0.0f;
					if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
						const auto& cm = cam->cameraRoot->world.rotate;
						aimYaw = std::atan2(cm.entry[0][1], cm.entry[1][1]);
						aimDown = std::asin(std::clamp(-cm.entry[2][1], -1.0f, 1.0f));
					}
					const float pitchDown = std::clamp(aimDown + 0.45f, 0.3f, 1.35f);
					RE::NiPoint3 d{ std::sin(aimYaw) * std::cos(pitchDown), std::cos(aimYaw) * std::cos(pitchDown), -std::sin(pitchDown) };
					d.Unitize();
					if (auto t = target.get(); t && !t->IsDead()) {
						RE::NiPoint3 to = t->GetPosition() + RE::NiPoint3{ 0, 0, 50 } - mouth->world.translate;
						to.Unitize();
						if (to.Dot(d) > 0.94f) d = to;
					}
					{  // (the sweep of its head)
						const float c = std::cos(-g_breathSway), s = std::sin(-g_breathSway);
						d = { d.x * c - d.y * s, d.x * s + d.y * c, d.z };
					}
					const float age = 1.6f - breath;
					if (age < 0.12f) g_breath.dir = d;
					g_breath.dir = g_breath.dir + (d - g_breath.dir) * std::min(1.0f, dt * 10.0f);
					g_breath.dir.Unitize();
					g_breath.grow = Smooth((age - 0.1f) / 0.22f);
					// (Noah: from the dragon, burning the dead only) its flame is drawn at its jaws by SwingBody (FlamePose)
					g_breath.level = Smooth((age - 0.04f) / 0.15f) * (1 - Smooth((0.2f - breath) / 0.2f));
					if (g_breath.grow > 0.5f && (hitT -= dt) <= 0) {
						hitT = 0.2f;
						BreathHits(a_player, mouth->world.translate, g_breath.dir);
					}
				} else {
					g_breath.grow = 0, g_breath.level = 0, hitT = 0;
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
			{
				// (Noah) the glider in your pack is the dragon's harness: its name says your bond, its picture is your dragon
				// (by its bond's look) - and the paraglider's own contrails (drawn under the glider's wingtips, so under the
				// dragon's wings) are swapped for nothing: the dragon draws its own wingtip trails
				static int named = -1;
				const int  b = Look();
				if (b != named) {
					named = b;
					static constexpr const char* kBond[5] = { "Unbonded", "Bond I", "Bond II", "Bond III", "Bone-Wyrm" };
					// (Noah: "cannot equip this item") it is never equipped - it is called in the air: the name says how
					if (auto* fn = item->As<RE::TESFullName>()) fn->fullName = fmt::format("Dragon Harness ({}) - jump, then Activate", kBond[std::clamp(b, 0, 4)]);
					if (auto* md = item->As<RE::TESModel>()) md->SetModel(fmt::format("CurseOfAlduin\\fx\\dragon_away{}.nif", b > 0 ? fmt::format("_b{}", std::clamp(b, 1, 4)) : std::string()).c_str());
					logger::info("glider: the harness in your pack - {}", kBond[std::clamp(b, 0, 4)]);
				}
				static bool trailsOff = false;
				if (!trailsOff) {
					trailsOff = true;
					auto* art = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSArtObject>(0x49D5, "Paragliding.esp");
					if (art) art->SetModel("Interface\\Logo\\logo.nif");  // (an empty model)
					logger::info("glider: the paraglider's contrails {}", art ? "removed" : "not found");
				}
			}
			const auto have = a_player->GetInventoryCounts([](RE::TESBoundObject& o) { return &o == item; });
			// (Noah) the dragon is yours once Whiterun's shrine is won - its Curse lifted - not before
			constexpr int kWhiterun = 1;
			const bool    unlocked = Curse::Freed(kWhiterun) || Gun::TestKitOn();
			static int    wasUnlocked = -1;
			if (!unlocked) {
				if (!have.empty()) {
					a_player->RemoveItem(item, have.begin()->second, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					logger::info("glider: the harness is not yours yet (Whiterun's shrine not won)");
				}
				std::int32_t on = 0;
				if (a_player->GetGraphVariableInt("hasparaglider", on) && on != 0) {
					a_player->SetGraphVariableInt("hasparaglider", 0);
				}
				wasUnlocked = 0;
				return;
			}
			if (have.empty()) {
				a_player->AddObjectToContainer(item, nullptr, 1, nullptr);
				logger::info("glider: the dragon harness put in your pack");
				if (wasUnlocked == 0) {
					Hud::Banner("Whiterun's shrine is won - a dragon answers your call (jump, then Activate mid-air)", 6.0f);
				}
			}
			wasUnlocked = 1;
			std::int32_t on = 0;
			if (!a_player->GetGraphVariableInt("hasparaglider", on) || on != 1) {
				a_player->SetGraphVariableInt("hasparaglider", 1);
			}
		}

		// (Noah) no dragon bond, no Updraft, no glider, before Whiterun's Echo Altar is won
		constexpr int kWhiterunTown = 1;
		bool BondLocked() { return !Curse::Freed(kWhiterunTown) && !Gun::TestKitOn(); }

		void BuyBond(RE::TESObjectREFR* a_shrine)
		{
			static constexpr const char* kName[3] = { "WIND-RIDER", "TALON STRIKE", "ALDUIN'S THRALL" };
			static constexpr const char* kHow[3] = { "Jump while gliding: it beats its wings and climbs  -  its fire turns blue",
				"Attack looking down mid-glide: the dive, landing in a shockwave  -  its fire turns yellow",
				"Its fire burns hotter, and turns green" };
			const int b = Bond();
			if (b >= 3) {
				Hud::Banner("Your dragon has made every bond", 2.5f);
				return;
			}
			if (BondLocked()) {
				Hud::Banner("The dragon's bonds wait until Whiterun's Echo Altar is won", 3.0f);
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
		float g_shadowT = 0;
		bool g_dodgeGhost = false;  // the roll's i-frames (setghost)
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
			if (Eggs::ShadowStep()) {  // (Noah: the Nightingale's Debt) the dodge hides you from the dead for two seconds
				g_shadowT = 2.0f;
				av->SetActorValue(RE::ActorValue::kInvisibility, 1.0f);
			}
			const float camYaw = ViewYaw(p);  // (where the camera looks: your heading plus its free rotation)
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
			TurnKeepView(p, camYaw);  // face where you look (the camera stays put): the roll goes the way you meant
			if (!p->NotifyAnimationGraph(ev)) {
				// (Noah's last seconds before dying: six rolls refused in a row - staggered, held) the roll's animation
				// can't start, but the shove still carries you out of reach (no i-frames without the roll)
				static int said = 0;
				if (said++ < 20) logger::info("dodge: {} not taken - shoved anyway", ev);
				last = now;
				av->DamageActorValue(RE::ActorValue::kStamina, 10.0f);
				g_dodgeT = 0, g_dodgeDir = dir;
				return;
			}
			last = now;
			av->DamageActorValue(RE::ActorValue::kStamina, 10.0f);
			Perks::Dodged(0.55f);
			Gun::Console("setghost 1");  // (Noah: untouchable for the whole roll - blows, arrows and spells pass through)
			g_dodgeGhost = true;
			g_dodgeT = 0, g_dodgeDir = dir;  // (Noah: farther - first person barely moved) carried along, below
		}

		// the left stick's own events (the glide reads it when the game's movement reading has nothing)
		bool Dodging() { return g_dodgeT >= 0.0f && g_dodgeT < 0.6f; }
		bool Gliding() { return g_hadDragon || g_dive; }
		void OnStick(const RE::ThumbstickEvent* a_event)
		{
			if (a_event && a_event->IsLeft()) {
				g_stickX = a_event->xValue, g_stickY = a_event->yValue;
			}
		}
		void OnButton(const RE::ButtonEvent* a_event)
		{
			if (auto* ue0 = RE::UserEvents::GetSingleton(); a_event && ue0) {  // WASD held, for the glide (MoveInput)
				const auto& q = a_event->QUserEvent();
				const bool  held = a_event->IsPressed();
				if (q == ue0->forward) g_keyF = held;
				else if (q == ue0->back) g_keyB = held;
				else if (q == ue0->strafeLeft) g_keyL = held;
				else if (q == ue0->strafeRight) g_keyR = held;
				if (q == ue0->sprint) g_sprintKey = held;
			}
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
			if (ev == ue->activate) {
				if (auto* ui = RE::UI::GetSingleton(); !ui || !ui->GameIsPaused()) {
					g_fpWant = true;  // (FirstPersonGlide: only in first person, in the air)
				}
			}
			// the dragon's bonds
			if (const int bond = Bond(); bond >= 0) {  // (the breath is the dragon's from the start; the rest come with the bonds)
				auto* ui = RE::UI::GetSingleton();
				const bool free = !ui || !ui->GameIsPaused();
				if (free && bond >= 1 && ev == ue->jump && g_holding && g_flapT < 0 && g_flapWait <= 0) {  // I: the climbing wing-beats
					g_flapT = 0, g_flapWait = kFlapAll + 1.5f;
				}
				if (free && bond >= 2 && ev == ue->activate && g_liftOff == 1 && !g_hadDragon && g_takeT < kUpdraftRise) {
					g_glideQueued = true;
				}
				// III: right attack (right trigger) breathes (5 s apart); II: attack looking down dives - the left hand
				// (left trigger) for Bond III, whose right is the breath
				// (Noah: every bond breathes now - right attack; attack looking down is still Bond II's dive)
				float lookDown = 0;
				if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
					lookDown = -cam->cameraRoot->world.rotate.entry[2][1];
				}
				const bool breathKey = ev == ue->rightAttack && !(bond >= 2 && lookDown > 0.42f);
				if (free && breathKey && g_holding && !g_dive) {
					if (g_breathCd <= 0) {
						g_breathReq = true;
						g_breathCd = 5.0f;
					}
				}
				if (free && bond >= 2 && !breathKey && (ev == ue->rightAttack || ev == ue->leftAttack) && g_holding && !g_dive) {  // II: the dive
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
			g_fpGlide = 0, g_fpWant = false;
			g_wasAir = false, g_wantSlide = false, g_wantJumpCarry = false, g_forcedSneak = false;
			g_dodgeT = -1.0f, g_dodgeGhost = false;  // (the audit: a roll's leftover shove landed on the loaded game)
		}

		// ---- hanging from the harness (after the animation each frame: Gun's pose hook). In order:
		//   1. your body as the glide's animation left it (last frame's swing undone)
		//   2. the dragon placed in the world (Noah: it jumped about before it reached you - it was hung on the paraglider's
		//      animated node, which its opening animation throws around): turned to your heading, its harness bar where
		//      your raised hands reach, plus the swoop's way still to come and its own sway
		//   3. your body swung about the bar like a pendulum (subtle: back as it speeds up, out with its lean in a turn,
		//      a jolt with each wing-beat, a slow sway of its own); the bar swings with you about its rings
		//   4. your arms reached up (Noah: higher, at the harness's width, as you fall) and then onto its two handles - a
		//      two-bone reach for each arm, the hands turned so the palms wrap the grips, fingers open until they close on them
		//   5. its wingtip trails, now that it is where it is drawn
		bool g_swingFresh = true, g_legFresh = true;  // (a new glide: your body's root and legs to be taken afresh)
		// the character skeleton's own rest pose (fingers and legs straight): what a hand opens to and a fall's legs lean to
		const RE::NiMatrix3* BindRot(const char* a_name)
		{
			static std::unordered_map<std::string, RE::NiMatrix3> m;
			static bool                                           tried = false;
			if (!tried) {
				tried = true;
				RE::NiPointer<RE::NiNode>        sk;
				RE::BSModelDB::DBTraits::ArgsType args{};
				if (RE::BSModelDB::Demand("actors\\character\\character assets\\skeleton.nif", sk, args) == RE::BSResource::ErrorCode::kNone && sk) {
					RE::BSVisit::TraverseScenegraphObjects(sk.get(), [&](RE::NiAVObject* o) {
						if (o && !o->name.empty()) m[o->name.c_str()] = o->local.rotate;
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
				logger::info("glider: the skeleton's rest pose ({} bones)", m.size());
			}
			auto it = m.find(a_name);
			return it == m.end() ? nullptr : &it->second;
		}
		// a bone turned (in the world) by the least turn taking a_from onto a_to, a_w of the way
		void TurnBone(RE::NiAVObject* a_bone, RE::NiPoint3 a_from, RE::NiPoint3 a_to, float a_w, RE::NiUpdateData& a_ud)
		{
			if (!a_bone || !a_bone->parent || a_w <= 0.001f || a_from.Length() < 1e-4f || a_to.Length() < 1e-4f) return;
			a_from.Unitize(), a_to.Unitize();
			const RE::NiPoint3 ax = a_from.Cross(a_to);
			const float        s = ax.Length(), c = a_from.Dot(a_to);
			if (s < 1e-6f) return;
			const RE::NiMatrix3 W = AxisAngle(ax, std::atan2(s, c) * std::min(a_w, 1.0f));
			a_bone->local.rotate = a_bone->parent->world.rotate.Transpose() * (W * a_bone->world.rotate);
			a_bone->UpdateDownwardPass(a_ud, 0);
		}
		// an arm's two-bone reach: the elbow put where both bones meet the target (bent out toward a_pole), then each bone
		// turned onto it
		void ArmReach(RE::NiAVObject* a_ua, RE::NiAVObject* a_fa, RE::NiAVObject* a_hand, const RE::NiPoint3& a_target, RE::NiPoint3 a_pole, float a_w, RE::NiUpdateData& a_ud)
		{
			if (!a_ua || !a_fa || !a_hand || a_w <= 0.001f) return;
			const RE::NiPoint3 S = a_ua->world.translate, E = a_fa->world.translate, H = a_hand->world.translate;
			const float        a = (E - S).Length(), b = (H - E).Length();
			RE::NiPoint3       to = a_target - S;
			const float        len = to.Length();
			if (a < 1e-3f || b < 1e-3f || len < 1e-3f) return;
			const RE::NiPoint3 dir = to * (1.0f / len);
			const float        d = std::clamp(len, std::abs(a - b) + 0.5f, (a + b) * 0.995f);
			const float        x = (a * a - b * b + d * d) / (2 * d), h = std::sqrt(std::max(0.0f, a * a - x * x));
			a_pole = a_pole - dir * a_pole.Dot(dir);
			if (a_pole.Length() < 1e-3f) {
				a_pole = (E - S) - dir * (E - S).Dot(dir);
				if (a_pole.Length() < 1e-3f) return;
			}
			a_pole.Unitize();
			const RE::NiPoint3 E2 = S + dir * x + a_pole * h;
			TurnBone(a_ua, E - S, E2 - S, a_w, a_ud);
			const RE::NiPoint3 E3 = a_fa->world.translate, H3 = a_hand->world.translate;
			TurnBone(a_fa, H3 - E3, (S + dir * d) - E3, a_w, a_ud);
		}
		// a frame from a pointing direction and a second one across it (columns: a_f, a_k made square to it, their cross)
		RE::NiMatrix3 Frame(RE::NiPoint3 a_f, RE::NiPoint3 a_k)
		{
			a_f.Unitize();
			a_k = a_k - a_f * a_k.Dot(a_f);
			if (a_k.Length() < 1e-4f) a_k = a_f.Cross({ 0, 0, 1 });
			a_k.Unitize();
			const RE::NiPoint3 z = a_f.Cross(a_k);
			RE::NiMatrix3      m;
			m.entry[0][0] = a_f.x, m.entry[1][0] = a_f.y, m.entry[2][0] = a_f.z;
			m.entry[0][1] = a_k.x, m.entry[1][1] = a_k.y, m.entry[2][1] = a_k.z;
			m.entry[0][2] = z.x, m.entry[1][2] = z.y, m.entry[2][2] = z.z;
			return m;
		}
		struct Arm
		{
			RE::NiAVObject *clav, *ua, *fa, *hand, *f10, *f20, *f40;
			bool            ok() const { return ua && fa && hand && f10 && f20 && f40; }
		};
		Arm GetArm(RE::NiAVObject* a_root, bool a_left)
		{
			const char* s = a_left ? "L" : "R";
			auto        n = [&](const char* a_fmt) { return a_root->GetObjectByName(fmt::format(fmt::runtime(a_fmt), s, s).c_str()); };
			return { n("NPC {} Clavicle [{}Clv]"), n("NPC {} UpperArm [{}Uar]"), n("NPC {} Forearm [{}Lar]"), n("NPC {} Hand [{}Hnd]"), n("NPC {} Finger10 [{}F10]"),
				n("NPC {} Finger20 [{}F20]"), n("NPC {} Finger40 [{}F40]") };
		}
		// where the hand holds: in its fingers' crease, just off the palm
		RE::NiPoint3 GripPoint(const Arm& a_arm, bool a_left)
		{
			const RE::NiPoint3 h = a_arm.hand->world.translate, f = a_arm.f20->world.translate - h, k = a_arm.f10->world.translate - a_arm.f40->world.translate;
			RE::NiPoint3       palm = a_left ? f.Cross(k) : k.Cross(f);  // (the palm's side: overhand, it faces ahead)
			if (palm.Length() > 1e-4f) palm.Unitize();
			return h + f * 0.8f + palm * (1.8f * a_arm.hand->world.scale);
		}
		// the hand turned: its fingers along a_fingers, its knuckles (index to little finger) along a_knuckles
		void TurnHand(const Arm& a_arm, const RE::NiPoint3& a_fingers, const RE::NiPoint3& a_knuckles, float a_w, RE::NiUpdateData& a_ud)
		{
			if (a_w <= 0.001f || !a_arm.hand->parent) return;
			const RE::NiMatrix3 A = Frame(a_arm.f20->world.translate - a_arm.hand->world.translate, a_arm.f10->world.translate - a_arm.f40->world.translate);
			const RE::NiMatrix3 B = Frame(a_fingers, a_knuckles);
			const RE::NiMatrix3 R = Slerp(Rot(0, 0, 0), B * A.Transpose(), std::min(a_w, 1.0f));
			a_arm.hand->local.rotate = a_arm.hand->parent->world.rotate.Transpose() * (R * a_arm.hand->world.rotate);
			a_arm.hand->UpdateDownwardPass(a_ud, 0);
		}
		// the fingers opened toward the skeleton's rest (straight), a_open of the way
		void OpenFingers(RE::NiAVObject* a_root, bool a_left, float a_open, RE::NiUpdateData& a_ud)
		{
			if (a_open <= 0.001f) return;
			const char* s = a_left ? "L" : "R";
			for (int f = 0; f < 5; ++f) {
				for (int j = 0; j < 3; ++j) {
					const std::string name = fmt::format("NPC {} Finger{}{} [{}F{}{}]", s, f, j, s, f, j);
					auto*             b = a_root->GetObjectByName(name.c_str());
					const auto*       rest = BindRot(name.c_str());
					if (b && rest) b->local.rotate = Slerp(b->local.rotate, *rest, a_open * (f == 0 ? 0.5f : 0.85f));
				}
			}
			if (auto* hand = a_root->GetObjectByName(fmt::format("NPC {} Hand [{}Hnd]", s, s).c_str())) hand->UpdateDownwardPass(a_ud, 0);
		}

		void SwingCom(RE::NiAVObject* com, const RE::NiPoint3& a_pivot, const RE::NiPoint3& F, const RE::NiPoint3& Rt, float a_th, float a_ph, RE::NiUpdateData& u0)
		{
			// the whole body turned about the bar (your hands stay on it)
			const RE::NiMatrix3 W = AxisAngle(F, a_ph) * AxisAngle(Rt, a_th);
			const RE::NiPoint3  wpos = a_pivot + W * (com->world.translate - a_pivot);
			const RE::NiMatrix3 wrot = W * com->world.rotate;
			const auto&         P = com->parent->world;
			const float         ps = std::max(1e-4f, P.scale);
			com->local.rotate = P.rotate.Transpose() * wrot;
			com->local.translate = P.rotate.Transpose() * (wpos - P.translate) * (1.0f / ps);
			com->UpdateDownwardPass(u0, 0);
		}

		void SwingBody(RE::PlayerCharacter* a_player)
		{
			static auto  last = std::chrono::steady_clock::now();
			const auto   now = std::chrono::steady_clock::now();
			const float  dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.05f);
			last = now;
			static float th = 0, thV = 0, ph = 0, phV = 0, clock = 0, kick = 0, lastBeat = 0, on = 0, trap = 0, trapV = 0;
			static RE::NiPoint3 lastVel{};
			static RE::NiTransform com0{};
			static RE::NiAVObject* comWas = nullptr;
			static bool            haveCom0 = false;
			static RE::NiPoint3    hs[2]{};  // your shoulders from your feet (your heading's frame), eased
			static bool            hsFresh[2] = { true, true };
			RE::NiUpdateData       u0;
			u0.time = 0.0f;
			RE::NiPoint3 v{};
			if (auto* cc = a_player->GetCharController()) GetVel(cc, v);
			on += ((g_holding && g_grip > 0.5f && g_armHold <= 0.001f ? 1.0f : 0.0f) - on) * std::min(1.0f, dt * 3.0f);  // (eased in and out)
			if (!g_place) {
				if (haveCom0 && on > 0.001f) {  // (the glide ended mid-swing: the body given back as the animation had it)
					if (auto* r = a_player->Get3D(false)) {
						if (auto* com = r->GetObjectByName("NPC COM [COM ]"); com && com == comWas) {
							com->local = com0;
							com->UpdateDownwardPass(u0, 0);
						}
					}
				}
				FlameGone();
				on = 0, haveCom0 = false, g_swingFresh = true, g_legFresh = true;
				th = thV = ph = phV = kick = trap = trapV = 0, lastVel = v, g_trap = 0;
				hsFresh[0] = hsFresh[1] = true;
				return;
			}
			clock += dt;
			const float        yaw = g_yawOwned ? g_glideYaw : a_player->GetAngleZ();
			const RE::NiMatrix3 Ry = AxisAngle({ 0, 0, 1 }, -yaw);
			const RE::NiPoint3 F{ std::sin(yaw), std::cos(yaw), 0 }, Rt{ std::cos(yaw), -std::sin(yaw), 0 }, Up{ 0, 0, 1 };
			// the pendulum
			const RE::NiPoint3 acc = (v - lastVel) * (1.0f / std::max(dt, 1e-3f));
			lastVel = v;
			const float fold = DiveFold();
			const float aF = acc.x * F.x + acc.y * F.y;
			const float beat = g_flapT >= 0 ? FlapWing(g_flapT) : 0.0f;
			if (beat < -0.3f && lastBeat >= -0.3f) {  // each downstroke of a climb: a jolt up - the legs kick forward and swing back
				thV += g_flapT > 2 * kFlapSmall ? 0.5f : 0.25f;
				kick = 1.0f;
			}
			lastBeat = beat;
			kick = std::max(0.0f, kick - dt * 1.6f);
			// (Noah: in a dive you lean back toward its tail - the straps bending back with the rush of air)
			const float thT = std::clamp(-aF * 0.00015f, -0.12f, 0.12f) - 0.36f * fold + 0.025f * std::sin(clock * 1.25f) - 0.03f;
			// (in a turn you hang square to its leaning harness: swung out with its lean)
			const float phT = std::clamp(g_roll * 0.8f, -0.7f, 0.7f) + 0.018f * std::sin(clock * 0.85f + 1.0f);
			thV += (14.0f * (thT - th) - 5.0f * thV) * dt, th += thV * dt;  // (a pendulum, well damped)
			phV += (14.0f * (phT - ph) - 5.5f * phV) * dt, ph += phV * dt;
			th = std::clamp(th, -0.55f, 0.2f), ph = std::clamp(ph, -0.75f, 0.75f);
			// the harness's two straps (Noah: like stiff rope, not rigid poles): the bar follows your swing on a springy, lightly
			// damped tether - lagging a jolt and overshooting a little - and is blown back further toward the tail in a dive
			{
				const float trapT = th - 0.25f * fold - std::clamp(aF * 0.00008f, -0.08f, 0.08f);
				trapV += (38.0f * (trapT - trap) - 3.2f * trapV) * dt, trap += trapV * dt;
				trap = std::clamp(trap, -0.95f, 0.35f);
			}
			g_trap = trap * on;  // (the bar swings with you about its rings)
			const float reachW = Smooth((g_deployT - 0.1f) / 0.4f);  // (your arms go up as the glider opens)
			const bool  first = RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
			RE::NiAVObject* seen = nullptr;
			float           seenScale = 0;
			for (int f = 0; f < 2; ++f) {
				auto* root = a_player->Get3D(f == 1);
				auto* space = root ? root->GetObjectByName("DG_Space") : nullptr;
				if (!space || !space->parent || space->parent->world.scale < 1e-4f) continue;
				const Arm L = GetArm(root, true), R = GetArm(root, false);
				if (!L.ok() || !R.ok()) continue;
				auto* com = f == 0 ? root->GetObjectByName("NPC COM [COM ]") : nullptr;
				// 1. last frame's swing undone (from the body's root as the glide's animation first set it - starting from
				// whatever the frame before left piled the turns up, and you spun round the bar)
				if (com && com->parent) {
					if (comWas != com || g_swingFresh || !haveCom0) com0 = com->local, haveCom0 = true;
					comWas = com;
					g_swingFresh = false;
					com->local = com0;
					com->UpdateDownwardPass(u0, 0);
				}
				// 2. the dragon in the world: its bar where your hands reach up to
				const RE::NiPoint3 rootT = root->world.translate;
				const float        rs = std::max(0.1f, root->world.scale);
				const RE::NiPoint3 mid = (L.ua->world.translate + R.ua->world.translate) * 0.5f;
				const RE::NiPoint3 off = Ry.Transpose() * (mid - rootT);
				if (hsFresh[f]) hs[f] = off, hsFresh[f] = false;
				hs[f] = hs[f] + (off - hs[f]) * std::min(1.0f, dt * 3.0f);
				hs[f].z = std::clamp(hs[f].z, 70.0f * rs, 170.0f * rs);
				hs[f].x = std::clamp(hs[f].x, -25.0f * rs, 25.0f * rs);
				hs[f].y = std::clamp(hs[f].y, -25.0f * rs, 25.0f * rs);
				const float armLen = (L.fa->world.translate - L.ua->world.translate).Length() + (L.hand->world.translate - L.fa->world.translate).Length() +
				                     0.8f * (L.f20->world.translate - L.hand->world.translate).Length();
				const RE::NiPoint3 barAt = rootT + Ry * (hs[f] + RE::NiPoint3{ 0, 5.0f * rs, armLen * 0.93f });
				RE::NiTransform    W;
				W.rotate = Ry;
				W.scale = kDragonScale * rs;
				W.translate = barAt;
				auto place = [&]() {
					const auto& P = space->parent->world;
					space->local.rotate = P.rotate.Transpose() * W.rotate;
					space->local.translate = P.rotate.Transpose() * (W.translate - P.translate) * (1.0f / P.scale);
					space->local.scale = W.scale / P.scale;
					space->UpdateDownwardPass(u0, 0);
				};
				place();
				// the bar keeps square to you (Noah: your arms crossed in a turn): it leans with your own swing about it, not
				// with the dragon's bank and its swing round into the turn - the straps take up the difference from its rings
				if (auto* tr = space->GetObjectByName("DG_Trapeze"); tr && tr->parent) {
					const RE::NiMatrix3 want = AxisAngle(F, ph * on) * space->world.rotate * Rot(g_trap, 0, 0);
					tr->local.rotate = tr->parent->world.rotate.Transpose() * want;
					tr->UpdateDownwardPass(u0, 0);
				}
				auto* hl = space->GetObjectByName("DG_HandleL");
				auto* hr = space->GetObjectByName("DG_HandleR");
				if (hl && hr) {
					W.translate = W.translate + (barAt - (hl->world.translate + hr->world.translate) * 0.5f);
				}
				W.translate = W.translate + Ry * (g_swoopOff * rs) + Ry * (g_sway * (W.scale * g_grip));
				place();
				g_placed = true;
				if (f == (first ? 1 : 0) || !seen) seen = space, seenScale = W.scale;
				const RE::NiPoint3 bar = hl && hr ? (hl->world.translate + hr->world.translate) * 0.5f : barAt;
				// 3. your body swung about the bar
				if (com && com->parent && on > 0.001f) {
					SwingCom(com, bar, F, Rt, th * on, ph * on, u0);
					static const char*   legs[4] = { "NPC L Thigh [LThg]", "NPC R Thigh [RThg]", "NPC L Calf [LClf]", "NPC R Calf [RClf]" };
					static RE::NiMatrix3 leg0[4]{};
					for (int i = 0; i < 4; ++i) {  // the legs a little behind the body (and bent at the knee by a beat's kick), not quite together
						auto* n = root->GetObjectByName(legs[i]);
						if (!n || !n->parent) continue;
						if (g_legFresh) leg0[i] = n->local.rotate;  // (as at the glide's start - each frame from there, never piling up)
						n->local.rotate = leg0[i];
						n->UpdateDownwardPass(u0, 0);
						const bool  calf = i >= 2;
						const float side = (i % 2 == 0 ? 1.0f : -1.0f) * 0.035f * std::sin(clock * 1.7f + (calf ? 0.6f : 0.0f));
						const float lagA = calf ? (-0.035f * thV - 0.25f * kick * std::sin(kick * 3.14159f)) : (-0.025f * thV + 0.15f * kick * std::sin(kick * 3.14159f));
						const RE::NiMatrix3 Wl = AxisAngle(Rt, (side + lagA + (calf ? -0.15f * fold : 0.0f)) * on);
						n->local.rotate = n->parent->world.rotate.Transpose() * (Wl * n->world.rotate);
						n->UpdateDownwardPass(u0, 0);
					}
					g_legFresh = false;
				}
				// 4. your arms: up at the harness's width as you fall (reaching for it), then onto its handles as it takes you
				{
					const float  w = std::max(reachW, g_grip);
					const RE::NiPoint3 sMid = (L.ua->world.translate + R.ua->world.translate) * 0.5f;
					const float  halfW = hl && hr ? 0.5f * (hl->world.translate - hr->world.translate).Length() : 84.0f * W.scale;
					for (int sd = 0; sd < 2; ++sd) {
						const bool  left = sd == 0;
						const Arm&  A = left ? L : R;
						const float out = left ? -1.0f : 1.0f;  // (your left is -right)
						// which handle is this hand's: the nearer to its shoulder
						RE::NiAVObject* mine = nullptr;
						RE::NiAVObject* other = nullptr;
						if (hl && hr) {
							const bool lNear = (hl->world.translate - L.ua->world.translate).Length() <= (hr->world.translate - L.ua->world.translate).Length();
							mine = (left == lNear) ? hl : hr;
							other = mine == hl ? hr : hl;
						}
						const RE::NiPoint3 flail = Up * (1.2f * std::sin(clock * 5.3f + sd * 1.9f)) + F * (1.5f * std::sin(clock * 3.7f + sd * 0.8f));
						const RE::NiPoint3 reach = sMid + Rt * (out * halfW * 1.05f) + Up * (armLen * 0.9f) + F * 6.0f + flail * (1 - g_grip);
						const RE::NiPoint3 grip = mine ? mine->world.translate : reach;
						const RE::NiPoint3 target = reach + (grip - reach) * g_grip;
						// the hand's turn: fingers up (a little ahead), knuckles in along the bar toward the other hand
						// (Noah: the hands twisted in a turn) the hand keeps to your body's own swing - its lean and pitch about the bar -
						// so the wrist stays straight with the forearm; only the knuckles' line follows the bar a little
						const RE::NiMatrix3 Wb = AxisAngle(F, ph * on) * AxisAngle(Rt, th * on);
						RE::NiPoint3 hUp = Wb * Up, hFwd = Wb * F, inward = Wb * (Rt * -out);
						if (mine && other && g_grip > 0.01f) {
							RE::NiPoint3 in2 = other->world.translate - mine->world.translate;
							in2 = in2 - hUp * in2.Dot(hUp);
							if (in2.Length() > 1e-3f) {
								in2.Unitize();
								inward = inward + (in2 - inward) * (0.35f * g_grip);
								inward.Unitize();
							}
						}
						const RE::NiPoint3 fingers = hUp * 0.95f + hFwd * 0.22f;
						// (Noah: the arms bent the wrong way) a hang's elbows: a little out to the side and forward, as on a
						// pull-up bar - never folded back behind you
						const RE::NiPoint3 pole = Rt * out * 0.55f + F * 0.45f - Up * 0.1f;
						if (A.clav && g_grip < 0.999f) {  // (the shoulder lifted as the arm reaches up)
							const RE::NiPoint3 c = A.ua->world.translate - A.clav->world.translate;
							TurnBone(A.clav, c, c + Up * (c.Length() * 0.3f), reachW * (1 - g_grip) * 0.8f, u0);
						}
						ArmReach(A.ua, A.fa, A.hand, target - (GripPoint(A, left) - A.hand->world.translate), pole, w, u0);
						TurnHand(A, fingers, inward, w, u0);
						ArmReach(A.ua, A.fa, A.hand, target - (GripPoint(A, left) - A.hand->world.translate), pole, w, u0);
						TurnHand(A, fingers, inward, w, u0);
						OpenFingers(root, left, reachW * (1 - g_grip), u0);
					}
				}
				// 5. its fire, at its jaws
				FlamePose(f, root, space);
			}
			// its wingtip trails: sampled from the copy you see, drawn on both
			if (seen) {
				TrailSample(seen, g_trail, dt);
				for (int f = 0; f < 2; ++f) {
					auto* root = a_player->Get3D(f == 1);
					if (auto* sp = root ? root->GetObjectByName("DG_Space") : nullptr) TrailPlace(sp, g_trail, seenScale);
				}
			}
			static float logT = 0;
			if ((logT -= dt) <= 0) {
				logT = 2.0f;
				logger::info("glider: hanging - swing pitch {:.2f} roll {:.2f}, grip {:.2f}, reach {:.2f}, lean {:.2f}, speed {:.0f}, path {:.2f}", th, ph, g_grip, reachW, g_roll, g_speed, g_gamma);
			}
		}

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
					// (Noah: a sprint-jump left you in an odd pose for the fall) the fall's body: part of the way back toward
					// standing straight, the legs mostly straight below you (the arms reach up for the harness: SwingBody)
					for (int i = 0; i < kN; ++i) {
						const float k = i >= 11 ? 0.6f : (i == 0 || i >= 7) ? 0.35f : 0.0f;
						if (const auto* rest = k > 0 ? BindRot(kBones[i]) : nullptr) held[f][i] = Slerp(held[f][i], *rest, k);
					}
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
			// (Noah: the swung legs were meant to be the dragon's - its legs sweep back in the dive; yours are left alone)
		}

		// dev: a camera that follows you (the free camera, placed each frame) - for filming the dragon
		struct Chase { bool on = false; float dist = 600, yaw = 0, pitch = 15, height = 0, spin = 0; } g_chase;
		// (the free camera stops you flying: this turns the ordinary over-the-shoulder camera round you instead)
		void DevChase(bool a_on, float a_dist, float a_yaw, float a_pitch, float a_height, float a_spin)
		{
			g_chase = { a_on, a_dist, a_yaw, a_pitch, a_height, a_spin };
			g_chaseOn = a_on;
			if (!a_on) {
				auto* pc = RE::PlayerCamera::GetSingleton();
				if (pc && pc->currentState && pc->currentState->id == RE::CameraState::kThirdPerson) {
					auto* tp = static_cast<RE::ThirdPersonState*>(pc->currentState.get());
					tp->freeRotation = { 0, 0 };
				}
			}
		}
		void ChaseTick(RE::PlayerCharacter*, float a_delta)
		{
			if (!g_chase.on) return;
			auto* pc = RE::PlayerCamera::GetSingleton();
			if (!pc || !pc->currentState || pc->currentState->id != RE::CameraState::kThirdPerson) return;
			g_chase.yaw += g_chase.spin * a_delta;
			auto* tp = static_cast<RE::ThirdPersonState*>(pc->currentState.get());
			tp->freeRotationEnabled = true;
			tp->freeRotation.x = g_chase.yaw * 0.0174533f;
			tp->freeRotation.y = 0.0f;  // (its pitch is your own view's)
			const float zoom = std::clamp(g_chase.dist, 0.0f, 1.0f);  // (0 close .. 1 far)
			tp->targetZoomOffset = zoom;
			tp->currentZoomOffset += (zoom - tp->currentZoomOffset) * std::min(1.0f, a_delta * 4.0f);
		}

		// (Noah: in first person the glider never opened) the paraglider's animations live only in Skyrim's third-person
		// body: Activate in the air in first person turns the camera round to just behind your head (close in), opens the
		// glider, and gives you back first person as you land
		void FirstPersonGlide(RE::PlayerCharacter* a_p, float a_delta)
		{
			auto* cam = RE::PlayerCamera::GetSingleton();
			if (!cam) {
				return;
			}
			auto third = [&]() -> RE::ThirdPersonState* {
				return cam->currentState && cam->currentState->id == RE::CameraState::kThirdPerson ? static_cast<RE::ThirdPersonState*>(cam->currentState.get()) : nullptr;
			};
			if (g_fpWant) {
				g_fpWant = false;
				bool pg = false;
				a_p->GetGraphVariableBool("bParaGliding", pg);
				if (g_fpGlide == 0 && !BondLocked() && cam->IsInFirstPerson() && !OnGround(a_p) && !pg && !Riding()) {  // (the glider is yours once Whiterun is won)
					cam->ForceThirdPerson();
					g_fpGlide = 1, g_fpGlideT = 0, g_fpZoomWas = -1;
					logger::info("glider: opened from first person - the camera turns round for the glide");
				}
			}
			// (Noah: in first person the glider glitched out) however it was opened - the paraglider's own key, Dragon's
			// Updraft, a jump off a cliff - if it is open while the camera is in first person, the camera goes to third
			// person and the glider is called again there, so its animations play on the body they were made for
			if (g_fpGlide == 0 && cam->IsInFirstPerson() && !Riding()) {
				bool pg = false;
				a_p->GetGraphVariableBool("bParaGliding", pg);
				if (pg) {
					cam->ForceThirdPerson();
					g_fpGlide = 1, g_fpGlideT = 0, g_fpZoomWas = -1;
					logger::info("glider: open in first person - to third person, the glider called again");
				}
			}
			if (g_fpGlide == 0) {
				return;
			}
			g_fpGlideT += a_delta;
			if (auto* tp = third()) {
				if (g_fpZoomWas < 0) g_fpZoomWas = tp->targetZoomOffset;
				tp->targetZoomOffset = 0.05f;  // close behind your head
			}
			if (g_fpGlide == 1 && g_fpGlideT > 0.1f) {
				if (!a_p->NotifyAnimationGraph("StartPara")) {
					DevTools::PressKey(RE::INPUT_DEVICE::kKeyboard, 0x12, 0.05f);
				}
				g_fpGlide = 2, g_fpGlideT = 0;
			} else if (g_fpGlide == 2 && g_fpGlideT > 0.4f && OnGround(a_p)) {
				if (auto* tp = third(); tp && g_fpZoomWas >= 0) {
					tp->targetZoomOffset = tp->currentZoomOffset = g_fpZoomWas;
				}
				cam->ForceFirstPerson();
				g_fpGlide = 0;
				logger::info("glider: landed - back to first person");
			}
		}

		void Tick(float a_delta)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_delta <= 0) {
				return;
			}
			ChaseTick(player, a_delta);
			if (g_shadowT > 0 && (g_shadowT -= a_delta) <= 0) {
				player->AsActorValueOwner()->SetActorValue(RE::ActorValue::kInvisibility, 0.0f);
			}
			FirstPersonGlide(player, a_delta);
			if (auto* ss = RE::ScreenSplatter::GetSingleton(); ss && ss->uiCount > 0) {
				ss->Clear();  // (Skyrim's blood on the screen: the HUD's red edges say you were hit)
			}
			if (auto* ui = RE::UI::GetSingleton(); ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) || ui->IsMenuOpen(RE::MapMenu::MENU_NAME))) {
				g_keyF = g_keyB = g_keyL = g_keyR = false;  // (a key let go of while a menu had it is never heard: start again)
				g_sprintKey = false;
				g_stickX = g_stickY = 0;
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
				if (g_dodgeGhost && g_dodgeT > 0.55f) {
					g_dodgeGhost = false;
					if (!Party::SoloFallen()) Gun::Console("setghost 0");  // (the audit: a fall mid-roll kept its own ghost)
				}
				// (Noah: "increase the distance of the dodge roll - I don't get far enough out of range") about 4.5 m of shove,
				// was 2.6 m: a roll now clears a swing's reach
				if (auto* dcc = player->GetCharController(); dcc && g_dodgeT < 0.5f) {
					RE::NiPoint3 dv{};
					GetVel(dcc, dv);
					const float k = 1.0f - Smooth(g_dodgeT / 0.5f);
					const float sp = 1280.0f * k;
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
			if (cc && air && !g_place && !g_dive && g_liftOff == 0) {  // (the dragon flies you itself)
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
			// (Noah: "sprint in all directions like omni-movement in first person ... sprint backwards while casting spells or
			// drawing a bow") in first person, sprint held while moving any way - back, sideways, casting, a bow drawn - runs
			// you at sprint speed (Skyrim only sprints forward with empty hands, so its own sprint is left to that). It costs
			// stamina as a sprint does, and ends when the stamina does.
			{
				auto*       cam = RE::PlayerCamera::GetSingleton();
				auto*       pcs = RE::PlayerControls::GetSingleton();
				const auto  mv = pcs ? pcs->data.moveInputVec : RE::NiPoint2{};
				const bool  moving = mv.x * mv.x + mv.y * mv.y > 0.09f;
				auto*       av = player->AsActorValueOwner();
				const float stam = av->GetActorValue(RE::ActorValue::kStamina);
				const bool  omni = g_sprintKey && moving && !sprint && !air && cam && cam->IsInFirstPerson() && !st->IsSneaking() && stam > 1.0f && !Riding() && g_slide <= 0;
				g_omni += ((omni ? 1.0f : 0.0f) - g_omni) * std::min(1.0f, a_delta * 8.0f);
				if (g_omni > 0.01f) {
					want += g_omni * ((100.0f + gen::kRunBoost) * 0.45f + gen::kSprintBoost);  // (Skyrim's sprint is about 1.45x its run)
					if (omni) av->DamageActorValue(RE::ActorValue::kStamina, a_delta * 7.0f * gen::kSprintStamina);
				}
			}
			if (g_slide > 0) {
				want += gen::kSprintBoost;  // the crouch would otherwise drag the controller back to sneak speed
			}
			g_smooth += (want - g_smooth) * std::min(1.0f, a_delta * gen::kGroundAccel * 0.5f);
			Apply(g_smooth);
		}
	}
}
