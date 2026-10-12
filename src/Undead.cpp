#include "BO3Z.h"

#include <atomic>
#include <shared_mutex>

// The new dead (sheet: undead). Our creatures stand on Skyrim's draugr skeleton, so left alone they would move
// and fight like draugr. Here every one of them gets its own body language and its own attacks:
//  - a rig: after Skyrim has animated the skeleton each frame, we bend it (a hunched lurch, a dragging limp,
//    arms reaching for you, the head twitching, a crawler flat on its belly, a necromancer floating) and
//    reshape it (claw hands, a bloated chest), all in code, no animation files;
//  - a type: shambler, runner, crawler, wailer, bloater for the risen dead; stalker, reaper, necromancer and
//    the wyrm for the champions and Grimmvar;
//  - attacks of their own: the lunge-and-grab, the crawler's ankle bite, the wailer's scream that calls the
//    horde and drives it on, the bloater bursting when it dies, Grimmvar's roars and swipes (Director cues).
// The rig runs in the actor's animation update (Character::UpdateAnimation), possibly off the main thread:
// per-actor state is created on the main thread and only that actor's own update touches it.
namespace bo3z
{
	namespace
	{
		using Undead::Type;

		struct Def
		{
			float hunch, sway, headTilt, twitch, reach, spread, elbow, limp, lean;
			float crawl, lift, bob;          // crawl: 0..1 flat on the belly; lift: float off the ground (units)
			float hands, chest, head;        // bone scales
			float swingArms;                 // runners: arms thrown back and pumping
		};
		//                         hunch sway  tilt  twitch reach spread elbow limp  lean  crawl lift  bob   hands chest head  swing
		constexpr Def kDefs[] = {
			/* shambler  */ { 0.38f, 0.10f, 0.30f, 0.6f, 1.05f, 0.18f, 0.35f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f },
			/* runner    */ { 0.30f, 0.05f, 0.12f, 0.9f, 0.0f, 0.10f, 0.9f, 0.0f, 0.42f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
			/* crawler   */ { 0.0f, 0.12f, 0.20f, 0.7f, 0.0f, 0.25f, 0.4f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f },
			/* wailer    */ { 0.15f, 0.06f, 0.45f, 1.0f, 0.35f, 0.55f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f },
			/* bloater   */ { 0.12f, 0.22f, 0.15f, 0.2f, 0.45f, 0.45f, 0.25f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f },
			/* stalker   */ { 0.62f, 0.05f, 0.55f, 0.8f, 0.55f, 0.55f, 0.55f, 0.0f, 0.18f, 0.0f, -18.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f },
			/* reaper    */ { 0.18f, 0.04f, 0.10f, 0.1f, 0.0f, 0.12f, 0.1f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f },
			/* necro     */ { 0.10f, 0.03f, 0.10f, 0.0f, 0.55f, 0.75f, 0.5f, 0.0f, 0.0f, 0.0f, 42.0f, 7.0f, 1.0f, 1.0f, 1.0f, 0.0f },
			/* wyrm      */ { 0.50f, 0.07f, 0.20f, 0.2f, 0.70f, 0.40f, 0.45f, 0.0f, 0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f },
		};

		enum Bone { kCOM, kPelvis, kSpine1, kSpine2, kNeck, kHead, kLClav, kRClav, kLArm, kRArm, kLFore, kRFore, kLHand, kRHand, kLThigh, kRThigh, kLCalf, kRCalf, kLFoot, kRFoot, kBones };
		constexpr const char* kBoneNames[kBones] = { "NPC COM [COM ]", "NPC Pelvis [Pelv]", "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]", "NPC Neck [Neck]", "NPC Head [Head]",
			"NPC L Clavicle [LClv]", "NPC R Clavicle [RClv]", "NPC L UpperArm [LUar]", "NPC R UpperArm [RUar]", "NPC L Forearm [LLar]", "NPC R Forearm [RLar]",
			"NPC L Hand [LHnd]", "NPC R Hand [RHnd]", "NPC L Thigh [LThg]", "NPC R Thigh [RThg]", "NPC L Calf [LClf]", "NPC R Calf [RClf]",
			"NPC L Foot [Lft ]", "NPC R Foot [Rft ]" };

		// The gait, from scratch: each foot is planted on the ground and stays there (no sliding, no floating) until the
		// body has moved far enough past it; then it lifts, swings to where the body will be, and plants again, never
		// both at once. Two-bone IK bends hip and knee to reach it; the arms are placed the same way. Skyrim's draugr
		// animation is only the starting pose: what you see walking and reaching is this.
		struct Gait
		{
			float step, lift, width, lead, swing;  // step: how far the body may pass a planted foot; lift: swing height;
			                                        // width: half the stance; lead: how far ahead (in seconds of travel) a foot lands; swing: seconds per step
		};
		//                                 step   lift  width lead   swing
		constexpr Gait kGaits[] = { /* shambler */ { 30.0f, 7.0f, 11.0f, 0.30f, 0.42f },
		                            /* runner   */ { 58.0f, 17.0f, 8.0f, 0.24f, 0.21f },
		                            /* crawler  */ { 24.0f, 4.0f, 14.0f, 0.30f, 0.40f },
		                            /* wailer   */ { 34.0f, 9.0f, 9.0f, 0.30f, 0.34f },
		                            /* bloater  */ { 26.0f, 6.0f, 16.0f, 0.34f, 0.50f },
		                            /* stalker  */ { 46.0f, 13.0f, 10.0f, 0.26f, 0.26f },
		                            /* reaper   */ { 40.0f, 10.0f, 12.0f, 0.30f, 0.36f },
		                            /* necro    */ { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },    // floats: no steps
		                            /* wyrm     */ { 44.0f, 12.0f, 18.0f, 0.32f, 0.40f } };
		// each kind's own strike: how long, one arm or both, a rake across or a slam from overhead
		struct Strike { float dur; bool both, overhead; };
		constexpr Strike kStrikes[] = { /* shambler */ { 0.62f, true, false },    // both arms reach and seize
		                                /* runner   */ { 0.34f, false, false },   // quick alternating swipes
		                                /* crawler  */ { 0.40f, false, false },
		                                /* wailer   */ { 0.50f, false, false },   // a long raking claw
		                                /* bloater  */ { 0.85f, true, true },     // a two-fisted slam from overhead
		                                /* stalker  */ { 0.30f, false, false },
		                                /* reaper   */ { 0.70f, true, true },
		                                /* necro    */ { 0.55f, false, false },
		                                /* wyrm     */ { 0.70f, false, false } };
		struct Foot
		{
			RE::NiPoint3 plant{}, from{}, to{};
			float        t = -1.0f;  // >= 0: swinging (seconds into the swing)
			bool         set = false;
		};

		struct BoneState
		{
			RE::NiAVObject* node = nullptr;
			RE::NiTransform orig{}, written{};
			float           baseScale = 1.0f;  // the bone's own scale (ours is set from it, never multiplied up)
			bool            wrote = false;
		};
		struct Rig
		{
			Type                      type = Type::Shambler;
			RE::NiAVObject*           root = nullptr;
			std::array<BoneState, kBones> bones{};
			float                     phase = 0, twitchT = 0, twitchX = 0, twitchY = 0, seed = 0;
			RE::NiPoint3              lastPos{};
			float                     speed = 0, weight = 1;  // weight: how much of the rig shows (fades out in attacks)
			// cues from the fight (main thread writes, the rig reads): pose and seconds left
			std::atomic<int>          cue{ 0 };
			std::atomic<float>        cueLeft{ 0 };
			float                     cueLen = 1;
			std::array<Foot, 2>       feet{};
			float                     atkT = -1.0f;   // its own strike: seconds into the swing (-1 none)
			int                       atkSide = 1;
			bool                      wasAttacking = false;
		};
		std::shared_mutex                                    g_lock;
		std::unordered_map<RE::FormID, std::unique_ptr<Rig>> g_rigs;
		std::atomic_bool                                     g_logged{ false };

		RE::NiMatrix3 AxisAngle(RE::NiPoint3 a, float t)
		{
			a.Unitize();
			const float c = std::cos(t), s = std::sin(t), C = 1 - c;
			RE::NiMatrix3 m;
			m.entry[0][0] = c + a.x * a.x * C, m.entry[0][1] = a.x * a.y * C - a.z * s, m.entry[0][2] = a.x * a.z * C + a.y * s;
			m.entry[1][0] = a.y * a.x * C + a.z * s, m.entry[1][1] = c + a.y * a.y * C, m.entry[1][2] = a.y * a.z * C - a.x * s;
			m.entry[2][0] = a.z * a.x * C - a.y * s, m.entry[2][1] = a.z * a.y * C + a.x * s, m.entry[2][2] = c + a.z * a.z * C;
			return m;
		}

		float Ease(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }

		// Turn a bone about an axis given in the world (the actor's own forward/right/up), and/or shift it.
		void Turn(BoneState& b, const RE::NiMatrix3& D)
		{
			auto* n = b.node;
			if (!n || !n->parent) {
				return;
			}
			const RE::NiMatrix3 P = n->parent->world.rotate;
			n->local.rotate = P.Transpose() * D * P * n->local.rotate;
		}
		void Shift(BoneState& b, const RE::NiPoint3& a_world)
		{
			auto* n = b.node;
			if (!n || !n->parent) {
				return;
			}
			n->local.translate += n->parent->world.rotate.Transpose() * a_world * (1.0f / std::max(0.01f, n->parent->world.scale));
		}
		void Refresh(BoneState& b)
		{
			if (b.node) {
				RE::NiUpdateData ud;
				ud.time = 0.0f;
				b.node->UpdateDownwardPass(ud, 0);
			}
		}

		// the rotation that turns direction a onto direction b (world space)
		RE::NiMatrix3 Between(RE::NiPoint3 a, RE::NiPoint3 b, float k = 1.0f)
		{
			const float la = a.Length(), lb = b.Length();
			if (la < 1e-4f || lb < 1e-4f) {
				return AxisAngle({ 0, 0, 1 }, 0);
			}
			a = a / la, b = b / lb;
			RE::NiPoint3 ax = a.Cross(b);
			const float  ang = std::acos(std::clamp(a.Dot(b), -1.0f, 1.0f));
			if (ax.Length() < 1e-6f || ang < 1e-5f) {
				return AxisAngle({ 0, 0, 1 }, 0);
			}
			return AxisAngle(ax, ang * k);
		}
		// two-bone IK: bend root (hip / shoulder) and mid (knee / elbow) so the end (ankle / wrist) reaches the target,
		// the middle joint pointing toward pole
		void TwoBone(BoneState& root, BoneState& mid, BoneState& end, const RE::NiPoint3& target, const RE::NiPoint3& pole, float k = 1.0f)
		{
			if (!root.node || !mid.node || !end.node || k <= 0.001f) {
				return;
			}
			const RE::NiPoint3 h = root.node->world.translate, m = mid.node->world.translate, e = end.node->world.translate;
			const float L1 = (m - h).Length(), L2 = (e - m).Length();
			if (L1 < 1e-3f || L2 < 1e-3f) {
				return;
			}
			RE::NiPoint3 to = target - h;
			float        d = std::clamp(to.Length(), std::abs(L1 - L2) + 0.5f, (L1 + L2) * 0.999f);
			RE::NiPoint3 dir = to;
			dir.Unitize();
			const float a1 = (L1 * L1 + d * d - L2 * L2) / (2 * d);
			const float hh = std::sqrt(std::max(0.0f, L1 * L1 - a1 * a1));
			RE::NiPoint3 pl = pole - dir * pole.Dot(dir);
			if (pl.Length() < 1e-3f) {
				pl = (m - h) - dir * (m - h).Dot(dir);
			}
			pl.Unitize();
			const RE::NiPoint3 knee = h + dir * a1 + pl * hh;
			Turn(root, Between(m - h, knee - h, k));
			Refresh(root);
			const RE::NiPoint3 m2 = mid.node->world.translate, e2 = end.node->world.translate;
			Turn(mid, Between(e2 - m2, (h + dir * d) - m2, k));
			Refresh(mid);
		}

		void PoseRig(RE::Actor* a_actor, Rig& r, float a_delta)
		{
			auto* root = a_actor->Get3D(false);
			if (!root) {
				return;
			}
			if (root != r.root) {  // new 3D: find the bones again
				r.root = root;
				for (int i = 0; i < kBones; ++i) {
					r.bones[i] = {};
					r.bones[i].node = root->GetObjectByName(kBoneNames[i]);
					if (r.bones[i].node) {
						r.bones[i].baseScale = r.bones[i].node->local.scale;
					}
				}
			}
			// what the animation did not rewrite this frame starts from where it was, not from our last pose
			for (auto& b : r.bones) {
				if (b.node && b.wrote && std::memcmp(&b.node->local, &b.written, sizeof(RE::NiTransform)) == 0) {
					b.node->local = b.orig;
				}
				if (b.node) {
					b.orig = b.node->local;
				}
				b.wrote = false;
			}
			const Def& d = kDefs[static_cast<int>(r.type)];
			const float dt = std::clamp(a_delta, 0.0f, 0.1f);
			const auto  pos = a_actor->GetPosition();
			if (dt > 0) {
				const float v = std::hypot(pos.x - r.lastPos.x, pos.y - r.lastPos.y) / dt;
				r.speed += (std::min(v, 900.0f) - r.speed) * std::min(1.0f, dt * 6.0f);
			}
			r.lastPos = pos;
			const float moving = std::clamp(r.speed / 160.0f, 0.0f, 1.0f);
			r.phase += dt * (1.6f + 4.5f * moving) * (r.type == Type::Runner ? 1.6f : 1.0f);
			// the head jerks now and then (a dead nerve firing)
			if ((r.twitchT -= dt) <= 0) {
				r.twitchT = 0.6f + std::fmod(r.seed * 7.31f + r.phase, 2.5f);
				r.twitchX = (std::fmod(r.seed * 3.7f + r.phase * 1.3f, 2.0f) - 1.0f) * d.twitch * 0.5f;
				r.twitchY = (std::fmod(r.seed * 5.1f + r.phase * 0.7f, 2.0f) - 1.0f) * d.twitch * 0.35f;
			}
			// an attack swing of its own (Skyrim's): the arms are the attack's, the rest stays ours
			const bool attacking = a_actor->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone;
			r.weight = 1.0f;  // (attacks are ours too: see the strike below)
			const float w = r.weight;
			const int   cue = r.cue.load();
			float       cueK = 0;
			if (cue) {
				float left = r.cueLeft.load();
				left -= dt;
				r.cueLeft.store(left);
				if (left <= 0) {
					r.cue.store(0);
				} else {
					const float t = 1.0f - left / std::max(0.05f, r.cueLen);
					cueK = Ease(std::min(t / 0.2f, 1.0f)) * Ease(std::min((1.0f - t) / 0.25f, 1.0f));
				}
			}

			const float yaw = a_actor->GetAngleZ();
			const RE::NiPoint3 f{ std::sin(yaw), std::cos(yaw), 0 }, rt{ std::cos(yaw), -std::sin(yaw), 0 }, up{ 0, 0, 1 };
			const float s1 = std::sin(r.phase), s2 = std::sin(r.phase * 0.5f + r.seed);
			auto& B = r.bones;
			auto  done = [&](int i) { if (B[i].node) { B[i].written = B[i].node->local; B[i].wrote = true; } };

			// ---- the whole body: crawl flat, float, sway, a limp's dip
			{
				auto& com = B[kCOM];
				RE::NiMatrix3 D = AxisAngle(f, d.sway * s2 + d.limp * 0.08f * s1 * moving);
				if (d.crawl > 0) {
					D = AxisAngle(rt, -1.32f * d.crawl) * D;
					Shift(com, up * (-52.0f * d.crawl) + f * (18.0f * d.crawl));
				}
				if (d.lift != 0) {
					Shift(com, up * (d.lift + d.bob * std::sin(r.phase * 0.6f)));
				}
				if (d.limp > 0) {
					Shift(com, up * (-4.0f * d.limp * std::max(0.0f, s1) * moving));
				}
				if (cue == Undead::kCueCrouch) {
					Shift(com, up * (-30.0f * cueK));
				}
				Turn(com, D);
				Refresh(com), done(kCOM);
			}
			// ---- the back: hunched (or thrown back in a roar / scream)
			{
				float hunch = d.hunch + d.lean * moving;
				if (cue == Undead::kCueRoar) {
					hunch = hunch * (1 - cueK) - 0.35f * cueK;
				}
				if (cue == Undead::kCueCrouch) {
					hunch += 0.35f * cueK;
				}
				Turn(B[kSpine1], AxisAngle(rt, -hunch * 0.55f) * AxisAngle(up, 0.08f * s1 * moving));
				Refresh(B[kSpine1]), done(kSpine1);
				Turn(B[kSpine2], AxisAngle(rt, -hunch * 0.45f));
				if (B[kSpine2].node && d.chest != 1.0f) {
					B[kSpine2].node->local.scale = B[kSpine2].baseScale * d.chest;
				}
				Refresh(B[kSpine2]), done(kSpine2);
			}
			// ---- neck and head: tilted, twitching; a crawler looks up at you, a roar throws the head back
			{
				float tilt = d.headTilt * (0.6f + 0.4f * s2);
				float pitch = r.twitchY + (d.crawl > 0 ? 1.15f * d.crawl : 0.0f) + d.hunch * 0.4f;
				if (cue == Undead::kCueRoar) {
					pitch += 0.75f * cueK;
				}
				Turn(B[kNeck], AxisAngle(rt, pitch * 0.5f));
				if (B[kNeck].node && d.chest != 1.0f) {
					B[kNeck].node->local.scale = B[kNeck].baseScale / d.chest;  // the swollen chest, not a swollen head
				}
				Refresh(B[kNeck]), done(kNeck);
				Turn(B[kHead], AxisAngle(f, tilt) * AxisAngle(up, r.twitchX) * AxisAngle(rt, pitch * 0.5f));
				if (B[kHead].node && d.head != 1.0f) {
					B[kHead].node->local.scale = B[kHead].baseScale * d.head;
				}
				Refresh(B[kHead]), done(kHead);
			}
			// ---- legs: planted feet and steps, from scratch (see Gait)
			const float scale = r.root ? std::max(0.3f, r.root->world.scale) : 1.0f;
			const Gait& g = kGaits[static_cast<int>(r.type)];
			const RE::NiPoint3 vdir = f;  // the body walks where it faces (Skyrim turns it toward its path)
			if (g.step > 0 && B[kLThigh].node && B[kRThigh].node && B[kLCalf].node && B[kRCalf].node && B[kLFoot].node && B[kRFoot].node) {
				const float ground = pos.z;
				const float ankle = 9.0f * scale;
				const float spd = r.speed;
				for (int side = 0; side < 2; ++side) {  // side 0 left, 1 right
					Foot&              ft = r.feet[side];
					const float        sg = side == 0 ? -1.0f : 1.0f;
					const RE::NiPoint3 home = RE::NiPoint3{ pos.x, pos.y, ground + ankle } + rt * (sg * g.width * scale) + vdir * (spd * g.lead * (spd > 20.0f ? 1.0f : 0.0f));
					if (!ft.set || (ft.plant - home).Length() > 260.0f * scale) {  // first frame, or it was moved far (a teleport, a knock-back)
						ft.plant = home, ft.set = true, ft.t = -1.0f;
					}
					Foot& other = r.feet[1 - side];
					const float limp = (side == 0 && d.limp > 0) ? 0.45f : 1.0f;  // the dragging foot barely lifts
					if (ft.t < 0) {
						const float off = (ft.plant - home).Length();
						if (off > g.step * scale && other.t < 0) {  // too far behind: step (never both feet at once)
							ft.from = ft.plant, ft.to = home + vdir * (std::min(spd, 400.0f) * g.swing * 0.5f), ft.t = 0.0f;
						}
					}
					RE::NiPoint3 at = ft.plant;
					if (ft.t >= 0) {
						const float dur = g.swing * (r.type == Type::Runner ? 1.0f : std::clamp(1.0f - spd / 900.0f, 0.55f, 1.0f));
						ft.t += dt;
						const float u = std::clamp(ft.t / std::max(0.05f, dur), 0.0f, 1.0f);
						const float e = u * u * (3 - 2 * u);
						at = ft.from + (ft.to - ft.from) * e;
						at.z += std::sin(u * RE::NI_PI) * g.lift * scale * limp;
						if (u >= 1.0f) {
							ft.plant = ft.to, ft.t = -1.0f;
							at = ft.plant;
						}
					}
					at.z = std::max(at.z, ground + ankle);
					ft.from.z = ft.to.z = ground + ankle;
					if (ft.t < 0) {
						ft.plant.z = ground + ankle;
						at = ft.plant;
					}
					// the knee points forward (and a little out)
					TwoBone(B[side == 0 ? kLThigh : kRThigh], B[side == 0 ? kLCalf : kRCalf], B[side == 0 ? kLFoot : kRFoot], at, vdir + rt * (0.15f * sg), 1.0f);
				}
				// the hips settle: low enough that both planted feet are reached (nothing floats), with the step's bob
				{
					float need = 0;
					for (int side = 0; side < 2; ++side) {
						auto& th = B[side == 0 ? kLThigh : kRThigh];
						auto& ca = B[side == 0 ? kLCalf : kRCalf];
						auto& fo = B[side == 0 ? kLFoot : kRFoot];
						(void)th, (void)ca;
						const float miss = fo.node->world.translate.z - (r.feet[side].t >= 0 ? fo.node->world.translate.z : r.feet[side].plant.z);
						need = std::max(need, miss);
					}
					if (need > 0.5f) {  // a foot could not reach its plant: drop the body by that much and solve again
						Shift(B[kCOM], RE::NiPoint3{ 0, 0, -std::min(need, 30.0f * scale) });
						Refresh(B[kCOM]);
						for (int side = 0; side < 2; ++side) {
							const RE::NiPoint3 at = r.feet[side].t >= 0 ? B[side == 0 ? kLFoot : kRFoot].node->world.translate : r.feet[side].plant;
							TwoBone(B[side == 0 ? kLThigh : kRThigh], B[side == 0 ? kLCalf : kRCalf], B[side == 0 ? kLFoot : kRFoot], at, vdir + rt * (0.15f * (side == 0 ? -1.0f : 1.0f)), 1.0f);
						}
					}
				}
			}
			done(kLThigh), done(kRThigh), done(kLCalf), done(kRCalf), done(kLFoot), done(kRFoot);

			// ---- its own strike (no draugr swing): when Skyrim starts an attack, an arm rears back and rakes down
			const Strike& sk = kStrikes[static_cast<int>(r.type)];
			if (attacking && !r.wasAttacking) {
				r.atkT = 0.0f, r.atkSide = (r.atkSide + 1) % 2;
			}
			r.wasAttacking = attacking;
			float atk = -1.0f;
			if (r.atkT >= 0) {
				r.atkT += dt;
				atk = r.atkT / sk.dur;
				if (atk >= 1.0f) {
					r.atkT = -1.0f, atk = -1.0f;
				}
			}
			// ---- the arms, placed by IK: reaching, pumping, flung wide, raking
			{
				const float s1a = std::sin(r.phase), s1b = std::sin(r.phase + RE::NI_PI);
				RE::NiPoint3 chest = B[kSpine2].node ? B[kSpine2].node->world.translate : pos + up * (95.0f * scale);
				for (int side = 0; side < 2; ++side) {
					const float sg = side == 0 ? -1.0f : 1.0f;
					auto& clav = B[side == 0 ? kLClav : kRClav];
					auto& arm = B[side == 0 ? kLArm : kRArm];
					auto& fore = B[side == 0 ? kLFore : kRFore];
					auto& hand = B[side == 0 ? kLHand : kRHand];
					if (clav.node && d.chest != 1.0f) {
						clav.node->local.scale = clav.baseScale / d.chest;
						Refresh(clav);
					}
					done(side == 0 ? kLClav : kRClav);
					if (!arm.node || !fore.node || !hand.node) {
						continue;
					}
					const float swing = side == 0 ? s1a : s1b;
					const float sh = 1.0f * scale;
					// where this kind holds its hand (relative to the chest: forward, out to the side, up)
					float fw = 18.0f + 30.0f * d.reach, out = 9.0f + 22.0f * d.spread, upz = -18.0f + 22.0f * d.reach;
					if (d.swingArms > 0) {  // a runner pumps its arms
						fw = 6.0f + 26.0f * swing * moving, out = 10.0f, upz = -30.0f + 10.0f * std::abs(swing) * moving;
					} else {
						fw += 6.0f * std::sin(r.phase * 0.9f + side * 1.7f + r.seed);  // grasping, never quite still
						upz += 4.0f * std::sin(r.phase * 1.3f + side);
					}
					if (cue == Undead::kCueRoar || cue == Undead::kCueScream) {  // arms flung wide
						fw = fw * (1 - cueK) + 10.0f * cueK, out = out * (1 - cueK) + 48.0f * cueK, upz = upz * (1 - cueK) + 18.0f * cueK;
					}
					if (cue == Undead::kCueGrab) {
						fw = fw * (1 - cueK) + 55.0f * cueK, out = out * (1 - cueK) + 6.0f * cueK, upz = upz * (1 - cueK) + 8.0f * cueK;
					}
					if (cue == Undead::kCueChannel) {
						fw = fw * (1 - cueK) + 20.0f * cueK, upz = upz * (1 - cueK) + 40.0f * cueK;
					}
					if (atk >= 0 && (sk.both || side == r.atkSide)) {
						const float u = atk < 0.35f ? Ease(atk / 0.35f) : 1.0f;
						const float v = atk < 0.35f ? 0.0f : Ease((atk - 0.35f) / 0.45f);
						if (sk.overhead) {  // both fists high over the head, then down hard in front
							fw = (1 - u) * fw + u * (6.0f + 50.0f * v);
							out = (1 - u) * out + u * (12.0f - 4.0f * v);
							upz = (1 - u) * upz + u * (58.0f - 80.0f * v);
						} else if (sk.both) {  // both arms thrown forward to seize
							fw = (1 - u) * fw + u * (10.0f + 56.0f * v);
							out = (1 - u) * out + u * (24.0f - 16.0f * v);
							upz = (1 - u) * upz + u * (16.0f - 12.0f * v);
						} else {  // rear back high, then rake down across in front
							fw = (1 - u) * fw + u * (-4.0f + 62.0f * v);
							out = (1 - u) * out + u * (26.0f - 34.0f * v);
							upz = (1 - u) * upz + u * (34.0f - 64.0f * v);
						}
					}
					const RE::NiPoint3 target = chest + f * (fw * sh) + rt * (sg * out * sh) + up * (upz * sh);
					// the elbow bends out and down
					TwoBone(arm, fore, hand, target, rt * sg * 0.7f - up * 0.6f - f * 0.3f, 1.0f);
					done(side == 0 ? kLArm : kRArm), done(side == 0 ? kLFore : kRFore);
					if (d.hands != 1.0f) {
						hand.node->local.scale = hand.baseScale * d.hands;
						Refresh(hand);
					}
					done(side == 0 ? kLHand : kRHand);
				}
			}
			if (!g_logged.exchange(true)) {
				logger::info("undead: rig live ({} bones found on the first)", std::count_if(B.begin(), B.end(), [](const BoneState& b) { return b.node != nullptr; }));
			}
		}

		struct AnimHook
		{
			static void thunk(RE::Character* a_this, float a_delta)
			{
				func(a_this, a_delta);
				if (!a_this || a_this->IsDead() || a_this->IsInRagdollState()) {
					return;
				}
				Rig* r = nullptr;
				{
					std::shared_lock l(g_lock);
					if (auto it = g_rigs.find(a_this->GetFormID()); it != g_rigs.end()) {
						r = it->second.get();
					}
				}
				if (r) {
					try {
						PoseRig(a_this, *r, a_delta);
					} catch (...) {
					}
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// ---- the fight: what each kind does besides walking at you (main thread)
		struct Mind
		{
			RE::ActorHandle handle;
			Type            type;
			float           cd = 2.0f;        // next special move
			float           wind = -1.0f;     // wind-up left (lunge, bite, scream)
			float           lunge = -1.0f;    // lunge in flight
			RE::NiPoint3    lungeDir{};
			bool            burst = false;    // bloater: has it popped
		};
		std::vector<Mind> g_minds;

		// Glowing eyes: a glow mesh per creature (tools/eyes.py glow), hung on the head bone once the body has loaded,
		// so the eyes burn wherever the head turns, in the dark and in the light
		struct EyeState
		{
			std::string     id;
			RE::NiAVObject* root = nullptr;  // the 3D they were hung on (new 3D: hang them again)
		};
		std::unordered_map<RE::FormID, EyeState> g_eyes;
		void HangEyes(RE::Actor* a, EyeState& e)
		{
			auto* root = a->Get3D(false);
			if (!root || root == e.root) {
				return;
			}
			auto* head = root->GetObjectByName("NPC Head [Head]");
			auto* hn = head ? head->AsNode() : nullptr;
			if (!hn) {
				return;
			}
			static std::unordered_map<std::string, RE::NiPointer<RE::NiNode>> cache;
			auto& c = cache[e.id];
			if (!c) {
				const std::string path = "UndyingNights\\eyes\\" + e.id + "_glow.nif";
				RE::BSModelDB::DBTraits::ArgsType args{};
				if (RE::BSModelDB::Demand(path.c_str(), c, args) != RE::BSResource::ErrorCode::kNone || !c) {
					c.reset();
					e.root = root;  // (no glow for this one: don't ask every frame)
					static int warned = 0;
					if (warned++ < 4) {
						logger::warn("undead: no eye glow {}", path);
					}
					return;
				}
			}
			if (auto* clone = c->Clone()) {
				auto* obj = static_cast<RE::NiAVObject*>(clone);
				obj->local = RE::NiTransform{};
				hn->AttachChild(obj, true);
				RE::NiUpdateData ud;
				ud.time = 0.0f;
				obj->Update(ud);
			}
			e.root = root;
		}
		float             g_grabbed = 0, g_grabSlow = 0, g_wailBoost = 0;
		bool              g_slowed = false;  // the grab's slow is on the player
		std::mt19937      g_rng{ std::random_device{}() };
		float             Rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }

		void SetCue(RE::Actor* a, int a_cue, float a_seconds)
		{
			std::shared_lock l(g_lock);
			if (auto it = g_rigs.find(a->GetFormID()); it != g_rigs.end()) {
				it->second->cueLen = a_seconds;
				it->second->cueLeft.store(a_seconds);
				it->second->cue.store(a_cue);
			}
		}
	}

	namespace Undead
	{
		void Assign(RE::Actor* a_actor, Type a_type)
		{
			if (!a_actor) {
				return;
			}
			static bool hooked = false;
			if (!hooked) {
				hooked = true;
				REL::Relocation<std::uintptr_t> vt{ RE::VTABLE_Character[0] };
				AnimHook::func = vt.write_vfunc(0x7D, AnimHook::thunk);
				logger::info("undead: animation hook installed");
			}
			{
				std::unique_lock l(g_lock);
				auto& r = g_rigs[a_actor->GetFormID()];
				r = std::make_unique<Rig>();
				r->type = a_type;
				r->seed = Rand(0.0f, 10.0f);
				r->lastPos = a_actor->GetPosition();
			}
			std::erase_if(g_minds, [&](const Mind& m) { return m.handle.get().get() == a_actor; });
			g_minds.push_back({ a_actor->GetHandle(), a_type, Rand(1.5f, 4.0f) });
		}

		void Forget(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return;
			}
			std::unique_lock l(g_lock);
			g_rigs.erase(a_actor->GetFormID());
		}

		void SetEyes(RE::Actor* a_actor, std::string_view a_id)
		{
			if (a_actor) {
				g_eyes[a_actor->GetFormID()] = { std::string(a_id), nullptr };
			}
		}

		void CueFor(RE::Actor* a_actor, int a_cue, float a_seconds)
		{
			if (a_actor) {
				SetCue(a_actor, a_cue, a_seconds);
			}
		}

		Type Roll(int a_round)
		{
			const float u = Rand(0.0f, 1.0f);
			// every night is a mix of all the risen (Noah: no night of one model): the Risen Dead most, then the Hollow
			// Bride, the Stitched Abomination and the Drowned Wailer; a little more of the dangerous ones as nights go on
			const float runners = std::min(0.30f, 0.20f + 0.01f * a_round);
			const float crawlers = 0.0f;  // (retired: the crawl rig slid along the ground)
			const float wailers = std::min(0.14f, 0.08f + 0.006f * a_round);
			const float bloaters = std::min(0.20f, 0.14f + 0.006f * a_round);
			if (u < runners) return Type::Runner;
			if (u < runners + crawlers) return Type::Crawler;
			if (u < runners + crawlers + wailers) return Type::Wailer;
			if (u < runners + crawlers + wailers + bloaters) return Type::Bloater;
			return Type::Shambler;
		}

		float SpeedMult(Type a_type)
		{
			switch (a_type) {  // each kind its own pace (Noah: diversity)
			case Type::Runner: return 1.45f;   // the Hollow Bride sprints
			case Type::Crawler: return 0.8f;
			case Type::Bloater: return 0.62f;  // the Abomination lumbers
			case Type::Wailer: return 0.88f;
			case Type::Stalker: return 1.25f;
			case Type::Reaper: return 0.9f;
			case Type::Necro: return 0.8f;
			default: return 1.0f;
			}
		}
		float HealthMult(Type a_type)
		{
			switch (a_type) {
			case Type::Runner: return 0.7f;
			case Type::Crawler: return 0.6f;
			case Type::Bloater: return 2.4f;
			case Type::Wailer: return 1.1f;
			default: return 1.0f;
			}
		}
		float DamageMult(Type a_type)
		{
			switch (a_type) {  // how hard each kind hits
			case Type::Runner: return 0.75f;
			case Type::Bloater: return 1.6f;
			case Type::Wailer: return 0.8f;
			case Type::Stalker: return 1.2f;
			case Type::Reaper: return 1.4f;
			case Type::Necro: return 0.9f;
			default: return 1.0f;
			}
		}

		float PlayerSlow() { return g_grabSlow > 0 ? 0.35f : 1.0f; }

		void Tick(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (!a_player) {
				return;
			}
			g_grabbed = std::max(0.0f, g_grabbed - a_delta);
			g_wailBoost = std::max(0.0f, g_wailBoost - a_delta);
			// a grab holds you: the player's speed drops while the hands are on you
			{
				g_grabSlow = std::max(0.0f, g_grabSlow - a_delta);
				auto* pav = a_player->AsActorValueOwner();
				if (g_grabSlow > 0 && !g_slowed) {
					pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -55.0f);
					g_slowed = true;
				} else if (g_grabSlow <= 0 && g_slowed) {
					pav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 55.0f);
					g_slowed = false;
				}
			}
			const auto pp = a_player->GetPosition();
			for (auto& m : g_minds) {
				auto a = m.handle.get();
				if (!a) {
					m.cd = -99;
					continue;
				}
				if (a->IsDisabled() || a->IsDeleted()) {  // dawn took it: a disabled body must not reach for anyone
					m.cd = -99;
					continue;
				}
				if (!a->Is3DLoaded()) {
					continue;
				}
				if (auto it = g_eyes.find(a->GetFormID()); it != g_eyes.end()) {
					HangEyes(a.get(), it->second);
				}
				if (a->IsDead()) {
					if (m.type == Type::Bloater && !m.burst) {  // pops: a cloud of grave-rot that hurts everyone near
						m.burst = true;
						const auto at = a->GetPosition();
						Gun::FxBlast("poisongas", at + RE::NiPoint3{ 0, 0, 40 });
						Director::Hazard(at, 0.05f, 230.0f, 0.15f, "", true);
						for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
							if (auto z = h.get(); z && z.get() != a.get() && !z->IsDead() && Director::IsRoundUndead(z.get()) && z->GetPosition().GetDistance(at) < 260.0f) {
								z->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kHealth, Director::RoundHealth() * 0.6f);
							}
						}
						Forms::PlayAt(gen::Sound::bloater_burst, a.get());
					}
					m.cd = -99;
					continue;
				}
				const float dist = a->GetPosition().GetDistance(pp);
				m.cd -= a_delta;
				if (m.wind >= 0) {
					if ((m.wind -= a_delta) < 0) {
						if (m.type == Type::Wailer) {  // the scream: the horde comes, faster
							g_wailBoost = 6.0f;
							for (int i = 0; i < 3; ++i) {
								const float ang = Rand(0, 6.2832f);
								Director::RaiseAt(a->GetPosition() + RE::NiPoint3{ std::cos(ang) * 260.0f, std::sin(ang) * 260.0f, 40.0f });
							}
							for (auto& o : g_minds) {
								if (auto z = o.handle.get(); z && !z->IsDead() && z->GetPosition().GetDistance(a->GetPosition()) < 1400.0f) {
									z->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 4.0f);  // (little by little: it stacks with every scream)
								}
							}
							Forms::PlayAt(gen::Sound::wailer_scream, a.get());
						}
					}
					continue;
				}
				if (m.cd > 0) {
					continue;
				}
				switch (m.type) {
				case Type::Wailer:
					if (dist < 1800.0f) {
						m.wind = 1.1f;
						SetCue(a.get(), kCueScream, 1.6f);
						Forms::PlayAt(gen::Sound::champ_telegraph, a.get());
						m.cd = Rand(9.0f, 13.0f);
					}
					break;
				default:
					m.cd = 99;
					break;
				}
			}
			std::erase_if(g_minds, [](const Mind& m) { return m.cd <= -99; });
			// rigs of the gone (deleted, unloaded) are dropped now and then
			static float sweep = 0;
			if ((sweep += a_delta) > 5.0f) {
				sweep = 0;
				std::unique_lock l(g_lock);
				std::erase_if(g_eyes, [](const auto& e) {
					auto* f = RE::TESForm::LookupByID<RE::Actor>(e.first);
					return !f || f->IsDeleted();
				});
				std::erase_if(g_rigs, [](const auto& e) {
					auto* f = RE::TESForm::LookupByID<RE::Actor>(e.first);
					return !f || f->IsDeleted() || f->IsDead();
				});
			}
		}

		void Reset()
		{
			std::unique_lock l(g_lock);
			g_rigs.clear();
			g_minds.clear();
			g_eyes.clear();
			g_grabbed = g_grabSlow = g_wailBoost = 0;
			if (g_slowed) {
				if (auto* p = RE::PlayerCharacter::GetSingleton()) {
					p->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 55.0f);
				}
				g_slowed = false;
			}
		}
	}
}
