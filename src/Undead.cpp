#include "BO3Z.h"
#include "ClipBlend.h"
#include "Perf.h"

#include <atomic>
#include <cstring>
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
			RE::NiPointer<RE::NiAVObject> root;              // (held: a freed skeleton's address can't come back as "the same 3D" - the audit)
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
			// (the Mixamo clips) what it plays now and what it is blending out of
			int                       clipSet = -1;     // 0 draugr skeleton, 1 human; -1 not looked at yet, -2 none
			zclip::Player             player;
			std::vector<zclip::Q4>    pose;
			zclip::V3f                com;
			int                       nextAttack = 0;
			std::atomic<float>        hitAt{ -1.0f };   // a flinch asked for (main thread writes)
			std::vector<RE::NiAVObject*> clipNodes;     // the clip's bones on this body (by the clip's bone list)
			RE::NiAVObject*           comNode = nullptr;
			std::atomic<bool>         volatileOne{ false };  // (the Volatile: its own claws)
			std::atomic<bool>         leftBlade{ false };    // (Noah: "make him swing the sword arm on his left hand") the Mutant
			std::atomic<bool>         turned{ false };       // (the Risen Dead, wounded: frenzied, its claws faster)
			std::atomic<int>          wantSet{ -1 };         // (its row's own clips: SetEyes)
			std::atomic<float>        danceT{ 0 };           // (the Dance Of Death: seconds of dancing left)
			int                       danceK = 0;
			float                     lateral = 0;           // its sideways share of its pace (-1 left .. 1 right)
			float                     ahead = 1;             // its forward share of its pace (-1 backing off .. 1 straight on)
			float                     bodyYaw = 0;           // (no sidesteps in its set) the body turned toward where it goes (radians)
			RE::NiMatrix3             comBase, comWrote;     // (the COM's turn before ours, and as we left it)
			bool                      comTurned = false;
			std::vector<RE::NiMatrix3> poseM;            // (the pose as written: put back on a far body's off frames)
			RE::NiPoint3              comT{};
			std::uint32_t             lodTick = 0;
			float                     lodDt = 0;
			std::atomic<int>          gaitCap{ -1 };         // its fastest gait (main thread: SetPace / its frenzy)
			std::atomic<float>        warmup{ 2.5f };        // (Noah: "start walking, then run") seconds of walking left before it takes its gait
			std::atomic<int>          setNow{ -1 };          // its clip set, for the main thread
			std::atomic<float>        gaitV{ 0 };            // the clip speed its legs play now (0: standing or a full-body move)
			RE::NiPoint3              locoLast{};
			bool                      startDone = false;
		};
		std::shared_mutex                                    g_lock;
		std::unordered_map<RE::FormID, std::shared_ptr<Rig>> g_rigs;  // (shared: the animation threads keep theirs alive while the game thread replaces or drops it)
		std::atomic_bool                                     g_logged{ false };
		// (Noah: "the enemies don't have any attack animations") how the posing goes, told in the log now and then
		std::atomic<std::uint64_t> g_hookCalls{ 0 }, g_hookRigs{ 0 }, g_clipPosed{ 0 }, g_clipNo{ 0 };
		std::atomic_bool           g_camOn{ false };  // the camera's update poses them (CamHook)
		std::atomic<std::uint64_t> g_camCalls{ 0 }, g_camSkip{ 0 }, g_graphCalls{ 0 }, g_graphPoses{ 0 };
		std::atomic_bool           g_graphOn{ false };  // the animation graph's own update poses them (GraphHook)

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

		bool ClipPose(RE::Actor* a_actor, Rig& r, float dt, bool attacking, int cue);  // (below: the Mixamo clips)
		bool UsesClips(Type a_t);
		std::mutex                                 g_graveLock;
		std::vector<RE::NiPointer<RE::NiAVObject>> g_grave;  // skeletons a rig let go of, released on the main thread
		void PoseRig(RE::Actor* a_actor, Rig& r, float a_delta)
		{
			Perf::Scope perf(Perf::kAnim);
			auto* root = a_actor->Get3D(false);
			if (!root) {
				return;
			}
			if (root != r.root.get()) {  // new 3D: find the bones again
				if (r.root) {  // (the old skeleton is let go on the main thread, never freed here on the animation thread)
					std::scoped_lock gl(g_graveLock);
					g_grave.push_back(std::move(r.root));
				}
				r.root = RE::NiPointer<RE::NiAVObject>(root);
				// (Noah: "some of them are floating along the floor ... move like Draugr") the clips' bones too: a body set
				// down somewhere else (or loaded again) has new bones - the old ones were posed in its place, not these
				r.clipNodes.clear(), r.comNode = nullptr;
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
				// (Noah: "they start sliding towards me while standing before the walk -> run animations start") the pace is
				// taken up at once when it rises - the legs start with the body - and let go of more gently
				const float target = std::min(v, 900.0f);
				r.speed += (target - r.speed) * std::min(1.0f, dt * (target > r.speed ? 12.0f : 6.0f));  // (too quick and the frame-to-frame jitter flickered the gaits)
			}
			const RE::NiPoint3 prevPos = r.lastPos;
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

			if ((UsesClips(r.type) || r.wantSet.load(std::memory_order_relaxed) == 6) && ClipPose(a_actor, r, dt, attacking, cue)) {
				return;  // (the Mixamo clips moved it)
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
			// the way the body is actually going (sideways, backwards too: the feet step that way, nothing slides)
			RE::NiPoint3 vdir = f;
			{
				RE::NiPoint3 mv{ pos.x - prevPos.x, pos.y - prevPos.y, 0 };
				if (dt > 0 && mv.Length() / dt > 25.0f && mv.Length() < 200.0f) {
					mv.Unitize();
					vdir = mv;
				}
			}
			std::array<RE::NiPoint3, 2> tgt{};
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
					tgt[side] = at;
					TwoBone(B[side == 0 ? kLThigh : kRThigh], B[side == 0 ? kLCalf : kRCalf], B[side == 0 ? kLFoot : kRFoot], at, f + rt * (0.15f * sg), 1.0f);  // (knees always point the way it faces)
				}
				// the hips settle: low enough that both planted feet are reached (nothing floats), with the step's bob
				{
					float need = 0;
					for (int side = 0; side < 2; ++side) {
						auto& th = B[side == 0 ? kLThigh : kRThigh];
						auto& ca = B[side == 0 ? kLCalf : kRCalf];
						auto& fo = B[side == 0 ? kLFoot : kRFoot];
						(void)th, (void)ca;
						const float miss = fo.node->world.translate.z - tgt[side].z;  // a foot left hanging above where it should be
						need = std::max(need, miss);
					}
					if (need > 0.5f) {  // a foot could not reach its plant: drop the body by that much and solve again
						Shift(B[kCOM], RE::NiPoint3{ 0, 0, -std::min(need, 45.0f * scale) });
						Refresh(B[kCOM]);
						for (int side = 0; side < 2; ++side) {
							TwoBone(B[side == 0 ? kLThigh : kRThigh], B[side == 0 ? kLCalf : kRCalf], B[side == 0 ? kLFoot : kRFoot], tgt[side], f + rt * (0.15f * (side == 0 ? -1.0f : 1.0f)), 1.0f);
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

		// ---------------------------------------------------------------- the Mixamo clips
		// (Noah: "Dying Light zombies" - sprinting, lunging, screaming, getting up off the ground) clips from Mixamo,
		// retargeted onto Skyrim's draugr and human skeletons by tools/zprev (bake): per bone its rotation against its
		// parent each frame, and the COM's place (a run's drift across the ground taken out; the game moves the body)
		using namespace zclip;  // (src/ClipBlend.h: the clips' blending, shared with the preview renderer)
		// 0 draugr, 1 human (the shared sets), 2 risen, 3 brute, 4 mage, 5 girl, 6 mutant (each kind its own: Noah's folders)
		constexpr int          kSets = 7;
		constexpr const char*  kSetName[kSets] = { "draugr", "human", "risen", "brute", "mage", "girl", "mutant" };
		std::array<ClipSet, kSets> g_clipSets;
		ClipSet                g_dance;
		std::once_flag         g_clipsLoaded;
		int SetOfRow(std::string_view a_id) { return a_id == "zombie" ? 2 : a_id == "volatile" ? 3 : a_id == "mage" ? 4 : a_id == "girl" ? 5 : a_id == "bonebrute" ? 6 : -1; }
		bool LoadClip(const std::string& a_path, Clip& a_out)
		{
			std::ifstream f(a_path, std::ios::binary);
			if (!f) return false;
			char magic[4]; std::uint32_t fps = 0, nf = 0, nb = 0; float speed = 0;
			f.read(magic, 4); f.read(reinterpret_cast<char*>(&fps), 4); f.read(reinterpret_cast<char*>(&nf), 4); f.read(reinterpret_cast<char*>(&nb), 4); f.read(reinterpret_cast<char*>(&speed), 4);
			if (!f || std::memcmp(magic, "ZAN1", 4) != 0 || nf == 0 || nb == 0 || nb > 256 || nf > 10000) return false;
			a_out.fps = static_cast<int>(fps), a_out.frames = static_cast<int>(nf), a_out.speed = speed;
			a_out.bones.resize(nb);
			for (auto& b : a_out.bones) {
				std::uint8_t l = 0; f.read(reinterpret_cast<char*>(&l), 1);
				b.resize(l); f.read(b.data(), l);
			}
			a_out.q.resize(static_cast<std::size_t>(nf) * nb * 4);
			a_out.com.resize(static_cast<std::size_t>(nf) * 3);
			f.read(reinterpret_cast<char*>(a_out.q.data()), a_out.q.size() * 4);
			f.read(reinterpret_cast<char*>(a_out.com.data()), a_out.com.size() * 4);
			return static_cast<bool>(f);
		}
		void LoadClips()
		{
			std::call_once(g_clipsLoaded, [] {
				const std::string root = "Data\\SKSE\\Plugins\\CurseOfAlduin\\anims\\";
				for (int d = kDance0; d <= kDance3; ++d) g_dance.own[d] = LoadClip(root + "dance\\" + kClipFile[d] + ".zan", g_dance.c[d]);
				for (int k = 0; k < kSets; ++k) {
					auto& S = g_clipSets[k];
					for (int c = 0; c < kClips; ++c) S.own[c] = LoadClip(root + kSetName[k] + "\\" + kClipFile[c] + ".zan", S.c[c]);
					const bool shared = k < 2;
					bool core = true;
					if (shared) for (int c : { kRun, kAttack, kPunch, kScream, kHit, kHit2, kStandUp }) core &= S.own[c];
					else core = g_clipSets[0].ok || S.own[kRun];
					S.Fill(shared ? nullptr : &g_clipSets[0], &g_dance);
					// (a clip not lined up with the run - a stale file - is dropped for what stands in for it)
					for (int c = 1; c < kClips; ++c) {
						if (S.c[c].frames > 0 && S.c[c].bones != S.c[kRun].bones) S.c[c] = Clip{}, S.own[c] = false;
					}
					S.Fill(shared ? nullptr : &g_clipSets[0], &g_dance);
					S.ok = core && S.c[kRun].frames > 1;
					LoadClip(root + (k == 1 ? "human" : "draugr") + "\\rest.zan", S.rest);  // (optional: straight wrists)
					if (S.ok) S.Prepare();
					logger::info("undead: the {} clips {}", kSetName[k], S.ok ? "loaded" : "missing");
				}
			});
		}
		bool UsesClips(Type a_t) { return a_t == Type::Shambler || a_t == Type::Runner || a_t == Type::Wailer || a_t == Type::Bloater; }
		int SkeletonOf(RE::Actor* a_actor)
		{
			auto* race = a_actor->GetRace();
			if (!race) return 1;
			for (auto& m : race->skeletonModels) {
				std::string p = m.model.c_str();
				std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (p.find("draugr") != std::string::npos) return 0;
			}
			return 1;
		}
		RE::NiMatrix3 MatOf(const Q4& q)
		{
			const float w = q[0], x = q[1], y = q[2], z = q[3];
			RE::NiMatrix3 m;
			m.entry[0][0] = 1 - 2 * (y * y + z * z), m.entry[0][1] = 2 * (x * y - w * z), m.entry[0][2] = 2 * (x * z + w * y);
			m.entry[1][0] = 2 * (x * y + w * z), m.entry[1][1] = 1 - 2 * (x * x + z * z), m.entry[1][2] = 2 * (y * z - w * x);
			m.entry[2][0] = 2 * (x * z - w * y), m.entry[2][1] = 2 * (y * z + w * x), m.entry[2][2] = 1 - 2 * (x * x + y * y);
			return m;
		}
		// (each kind's swing speed against the clip's own)
		float RateFor(const Rig& r, int c)
		{
			float rate = ClipRate(c);
			const bool swing = c == kAttack || c == kAttackL || c == kAttack2 || c == kAttack3 || c == kPunch;
			switch (r.clipSet) {
			case 2: if (swing) rate *= r.turned.load() ? 1.3f : 1.1f; break;  // the Risen Dead: quick claws, quicker frenzied
			case 3: if (swing) rate *= 0.85f; break;                          // the Brute: slow, heavy swings
			case 5: if (swing) rate *= 0.8f; break;                           // the zombie girl: slower than the Risen Dead
			case 6: if (swing) rate *= 0.95f; break;                          // the Skeleton Mutant
			default:
				if ((c == kAttack || c == kAttackL) && (r.volatileOne.load() || r.type == Type::Runner)) rate *= r.volatileOne.load() ? 1.3f : 1.12f;
				if (c == kPunch && r.type == Type::Wailer) rate *= 0.9f;
				if (r.turned.load() && (c == kAttack || c == kPunch)) rate *= 1.3f;
			}
			return rate;
		}
		// true: the clips posed it this frame (the old rig stays out)
		// the gait it plays now: its own (walk or run - never a sprint), but a walk for its first steps out of the portal
		int EffCap(const Rig& r)
		{
			int cap = r.gaitCap.load(std::memory_order_relaxed);
			if (cap == zclip::kSprint) cap = zclip::kRun;
			if (cap == zclip::kRun && r.warmup.load(std::memory_order_relaxed) > 0) cap = zclip::kWalk;
			return cap;
		}
		bool ClipPose(RE::Actor* a_actor, Rig& r, float dt, bool attacking, int cue)
		{
			if (r.clipSet == -1) {
				LoadClips();
				const int k = SkeletonOf(a_actor);
				r.clipSet = g_clipSets[k].ok ? k : -2;
				if (r.clipSet == -2) logger::warn("undead: {} {:08X} has no clips for its skeleton ({}) - Skyrim's own animations", a_actor->GetName(), a_actor->GetFormID(), kSetName[k]);
			}
			// its row's own clips once known (SetEyes): the same bones, so only the player starts over
			if (const int w = r.wantSet.load(); w >= 0 && w != r.clipSet && r.clipSet != -2 && g_clipSets[w].ok) {
				r.clipSet = w, r.player = Player{};
				r.clipNodes.clear(), r.comNode = nullptr;  // (its own set's bones, by its own names)
			}
			if (r.clipSet < 0 || !r.root) {
				g_clipNo.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			const ClipSet& S = g_clipSets[r.clipSet];
			if (r.clipNodes.empty()) {
				const auto& names = S.c[kRun].bones;
				r.clipNodes.resize(names.size(), nullptr);
				for (std::size_t i = 0; i < names.size(); ++i) r.clipNodes[i] = r.root->GetObjectByName(names[i]);
				r.comNode = r.root->GetObjectByName("NPC COM [COM ]");
				static std::atomic_int told{ 0 };
				if (told.fetch_add(1) < 24) {
					const auto found = std::count_if(r.clipNodes.begin(), r.clipNodes.end(), [](RE::NiAVObject* n) { return n != nullptr; });
					logger::info("undead: {} {:08X} posed by the {} clips ({} of {} bones found, com {})", a_actor->GetName(), a_actor->GetFormID(), kSetName[r.clipSet], found, names.size(), r.comNode != nullptr);
				}
			}
			auto& P = r.player;
			// (Noah: "incredibly optimized") far bodies (past 40 m) work out a new pose every other frame - the one between
			// puts back the last pose (cheap), so nothing ever shows Skyrim's own animation between - and their springs get
			// both frames' time at once
			if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && !r.clipNodes.empty() && r.pose.size() == r.clipNodes.size()) {
				const auto ap = a_actor->GetPosition(), pp = pl->GetPosition();
				const float d2 = (ap.x - pp.x) * (ap.x - pp.x) + (ap.y - pp.y) * (ap.y - pp.y);
				if (d2 > 2800.0f * 2800.0f && (++r.lodTick & 1) && r.danceT.load() <= 0) {
					r.lodDt += dt;
					for (std::size_t i = 0; i < r.clipNodes.size(); ++i) {
						if (auto* n = r.clipNodes[i]) n->local.rotate = r.poseM[i];
					}
					if (r.comNode) {
						r.comNode->local.translate = r.comT;
						if (r.comTurned) r.comNode->local.rotate = r.comWrote;
						RE::NiUpdateData ud;
						ud.time = 0.0f;
						r.comNode->UpdateDownwardPass(ud, 0);
					}
					return true;
				}
				dt += r.lodDt, r.lodDt = 0;
			}
			// its feet: pace, and how much of it is sideways (for a sidestep)
			Loco loco{ r.speed, 0.0f, r.turned.load() };
			{
				const auto pos = a_actor->GetPosition();
				if (dt > 0) {
					const float vx = (pos.x - r.locoLast.x) / dt, vy = (pos.y - r.locoLast.y) / dt, v = std::hypot(vx, vy);
					const float yaw = a_actor->GetAngleZ();
					const float lat = v > 20.0f ? (vx * std::cos(yaw) - vy * std::sin(yaw)) / v : 0.0f;
					const float fwd = v > 20.0f ? (vx * std::sin(yaw) + vy * std::cos(yaw)) / v : 1.0f;
					r.lateral += (lat - r.lateral) * std::min(1.0f, dt * 5.0f);
					r.ahead += (fwd - r.ahead) * std::min(1.0f, dt * 5.0f);
				}
				r.locoLast = pos;
				loco.lateral = r.lateral;
				loco.cap = EffCap(r);
				if (r.speed >= 10.0f && r.warmup.load(std::memory_order_relaxed) > 0) r.warmup.store(r.warmup.load(std::memory_order_relaxed) - dt, std::memory_order_relaxed);
			}
			// what it should be doing
			const bool oneShotOn = P.OneShotOn(S);
			int want = -1;
			if (float dl = r.danceT.load(); dl > 0) {  // the Dance Of Death: nothing else until it ends
				r.danceT.store(dl - dt);
				want = kDance0 + r.danceK % kDances;
			}
			else if (cue == Undead::kCueStandUp) want = kStandUp;
			else if (cue == Undead::kCueCast) want = kCast;
			else if (cue == Undead::kCueLeap) want = kLeap;
			else if (cue == Undead::kCueScream || cue == Undead::kCueRoar) want = r.clipSet == 4 ? kSummon : kScream;
			else if (attacking && !r.wasAttacking) {
				// (Noah: each its own attacks) the Risen Dead claw left and right; the Brute swipes three ways and punches;
				// the mage blasts; the zombie girl claws, and now and then brings both arms down
				const int n = r.nextAttack++;
				switch (r.clipSet) {
				case 2: want = (n & 1) ? kAttackL : kAttack; break;
				case 3: { static constexpr int kB[4] = { kAttack, kAttack2, kAttack3, kPunch }; want = kB[(n * 7 + 3) % 4]; break; }
				case 4: want = kCast; break;
				case 6: want = (n % 3 == 2) ? kPunch : kAttack; break;  // (the Skeleton Mutant: swipes, and now and then a hammer punch)
				case 5: want = (n % 3 == 2) ? kPunch : kAttack; break;
				default:
					if (r.leftBlade.load()) want = kAttackL;
					else if (r.volatileOne.load() || r.type == Type::Runner) want = kAttack;
					else if (r.type == Type::Wailer) want = (n % 3 == 2) ? kAttack : kPunch;
					else want = (n % 3 == 2) ? kPunch : kAttack;
				}
			}
			else if (const float h = r.hitAt.exchange(-1.0f); h >= 0 && !(oneShotOn && P.clip != kHit && P.clip != kHit2)) want = (r.nextAttack & 1) ? kHit : kHit2;
			r.wasAttacking = attacking;
			if (want < 0) {
				if (oneShotOn && P.clip != kStandUp) want = P.clip;
				else if (P.clip == kStandUp && cue == Undead::kCueStandUp) want = kStandUp;
				else if (P.clip == kStandUp && !r.startDone && S.Has(kStart)) want = kStart, r.startDone = true;  // (up off the ground and straight into a sprint)
				else want = kRun;
			}
			g_clipPosed.fetch_add(1, std::memory_order_relaxed);
			P.Play(want, want == kRun ? 1.0f : RateFor(r, want), S);
			P.Tick(S, dt, loco, r.pose, r.com);
			r.poseM.resize(r.clipNodes.size());
			for (std::size_t i = 0; i < r.clipNodes.size() && i < r.pose.size(); ++i) {
				r.poseM[i] = MatOf(r.pose[i]);
				if (auto* n = r.clipNodes[i]) n->local.rotate = r.poseM[i];
			}
			r.setNow.store(r.clipSet, std::memory_order_relaxed);
			r.gaitV.store(P.clip != kRun ? -1.0f : P.loco == kIdle ? 0.0f : S.c[P.loco].speed, std::memory_order_relaxed);  // (-1 a full-body move, 0 standing)
			// (Noah: "when they face me and move left or right they float along the floor doing a forward walk") a set
			// with no sidesteps turns its whole body - the COM, which carries the hips and the spine - toward where it is
			// really going, up to 75 degrees off its facing, and back square on as it stops or swings
			if (r.comNode) {
				float want = 0;
				if (!S.Has(kStrafeL) && P.clip == kRun && r.speed > 30.0f && r.ahead > -0.35f)
					want = std::clamp(std::atan2(r.lateral, std::max(r.ahead, 0.15f)), -1.3f, 1.3f);
				r.bodyYaw += (want - r.bodyYaw) * std::min(1.0f, dt * 6.0f);
			}
			if (r.comNode) {
				r.comNode->local.translate = { r.com.x, r.com.y, r.com.z };
				r.comT = r.comNode->local.translate;
				// (the graph writes the COM's turn each frame; where it did not, what we wrote is taken back off first)
				const bool ours = r.comTurned && std::memcmp(&r.comNode->local.rotate, &r.comWrote, sizeof(RE::NiMatrix3)) == 0;
				if (ours) r.comNode->local.rotate = r.comBase;
				r.comTurned = false;
				if (std::abs(r.bodyYaw) > 0.01f) {
					// (Skyrim's heading turns clockwise from above: to its right is a turn the other way about the root's Z)
					const float c = std::cos(-r.bodyYaw), sn = std::sin(-r.bodyYaw);
					r.comBase = r.comNode->local.rotate;
					RE::NiMatrix3 z;
					z.entry[0][0] = c, z.entry[0][1] = -sn, z.entry[0][2] = 0;
					z.entry[1][0] = sn, z.entry[1][1] = c, z.entry[1][2] = 0;
					z.entry[2][0] = 0, z.entry[2][1] = 0, z.entry[2][2] = 1;
					r.comNode->local.rotate = z * r.comNode->local.rotate;
					r.comNode->local.translate = { c * r.com.x - sn * r.com.y, sn * r.com.x + c * r.com.y, r.com.z };
					r.comWrote = r.comNode->local.rotate, r.comTurned = true;
					r.comT = r.comNode->local.translate;
				}
				RE::NiUpdateData ud;
				ud.time = 0.0f;
				r.comNode->UpdateDownwardPass(ud, 0);
			}
			return true;
		}

		struct AnimHook
		{
			static void thunk(RE::Character* a_this, float a_delta)
			{
				func(a_this, a_delta);
				g_hookCalls.fetch_add(1, std::memory_order_relaxed);
				if (!a_this || a_this->IsDead() || a_this->IsInRagdollState()) {
					return;
				}
				std::shared_ptr<Rig> r;
				{
					std::shared_lock l(g_lock);
					if (auto it = g_rigs.find(a_this->GetFormID()); it != g_rigs.end()) {
						r = it->second;
					}
				}
				if (r && (g_camOn.load() || g_graphOn.load())) {
					g_hookRigs.fetch_add(1, std::memory_order_relaxed);
					return;  // (the camera's update poses them: once a frame, not twice)
				}
				if (r) {
					g_hookRigs.fetch_add(1, std::memory_order_relaxed);
					try {
						PoseRig(a_this, *r, a_delta);
					} catch (...) {
					}
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// (Noah: "they still move like Draugr ... they don't swing when they deal damage" - the log showed the hook above is
		// never called in his game: no animation update reaches it, not one) the poses are put on from the camera's update
		// instead: on the main thread, once a frame, after the world's animation has run and before the frame is drawn
		std::atomic<std::uint64_t> g_camPoses{ 0 };
		struct CamHook
		{
			static void thunk(RE::TESCamera* a_this)
			{
				func(a_this);
				g_camCalls.fetch_add(1, std::memory_order_relaxed);
				if (g_graphOn.load()) return;  // (the graph's update poses them)
				g_camOn = true;
				auto* ui = RE::UI::GetSingleton();
				if (ui && ui->GameIsPaused()) return;
				const float dt = std::clamp(RE::GetSecondsSinceLastFrame(), 0.0f, 0.1f);
				std::vector<std::pair<RE::FormID, std::shared_ptr<Rig>>> rigs;
				{
					std::shared_lock l(g_lock);
					rigs.reserve(g_rigs.size());
					for (auto& [id, r] : g_rigs) rigs.emplace_back(id, r);
				}
				for (auto& [id, r] : rigs) {
					auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
					if (!a || !r || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded() || a->IsInRagdollState()) {
						g_camSkip.fetch_add(1, std::memory_order_relaxed);
						continue;
					}
					try {
						PoseRig(a, *r, dt);
						g_camPoses.fetch_add(1, std::memory_order_relaxed);
					} catch (...) {
					}
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// (Noah: still Draugr moves, with both tables hooked - the log: neither is ever called in his game) the one place
		// every body's animation is worked out: its animation graph's update (IAnimationGraphManagerHolder::
		// UpdateAnimationGraphManager). It is called directly, never through a table, so it is hooked where it begins: its
		// first instructions are copied out to run before it continues, and a jump to ours is written in their place.
		namespace Detour
		{
			// the length of one of the instructions a function usually begins with (0: not one we know - no hook)
			std::size_t ModRm(const std::uint8_t* p)
			{
				const std::uint8_t m = p[0], mod = m >> 6, rm = m & 7;
				if (mod == 3) return 1;
				if (mod == 0 && rm == 5) return 0;  // (rip-relative: can't be copied as it is)
				std::size_t n = 1;
				if (rm == 4) {
					++n;
					if (mod == 0 && (p[1] & 7) == 5) n += 4;
				}
				return n + (mod == 1 ? 1 : mod == 2 ? 4 : 0);
			}
			std::size_t Len(const std::uint8_t* p)
			{
				std::size_t pre = 0;
				if ((p[0] & 0xF0) == 0x40) pre = 1;  // REX
				const std::uint8_t op = p[pre];
				if (op >= 0x50 && op <= 0x57) return pre + 1;                                    // push r
				if (op == 0x89 || op == 0x8B || op == 0x8D || op == 0x33 || op == 0x31 || op == 0x2B || op == 0x03 || op == 0x85) {  // mov, lea, xor, sub, add, test
					const std::size_t m = ModRm(p + pre + 1);
					return m ? pre + 1 + m : 0;
				}
				if (op == 0x83) { const std::size_t m = ModRm(p + pre + 1); return m ? pre + 1 + m + 1 : 0; }   // op r/m, imm8
				if (op == 0x81) { const std::size_t m = ModRm(p + pre + 1); return m ? pre + 1 + m + 4 : 0; }   // op r/m, imm32
				if (op == 0x0F && (p[pre + 1] == 0x29 || p[pre + 1] == 0x28 || p[pre + 1] == 0x11 || p[pre + 1] == 0x10)) {  // movaps / movups
					const std::size_t m = ModRm(p + pre + 2);
					return m ? pre + 2 + m : 0;
				}
				if (op == 0xB8 + (op & 7) && op >= 0xB8 && op <= 0xBF) return pre + 1 + ((p[0] == 0x48 || p[0] == 0x49) ? 8 : 4);  // mov r, imm
				return 0;
			}
			// (Noah: "failed to create trampoline" at launch - no free memory near the game for one of our own) SKSE's
			// shared one, made at load (src/Events.cpp InstallLoadGuard asks for room enough)
			SKSE::Trampoline& Tramp() { return SKSE::GetTrampoline(); }
			// hooks the function at a_at to run a_to; returns what runs the original (0: not hooked)
			std::uintptr_t Install(std::uintptr_t a_at, std::uintptr_t a_to, std::string_view a_name)
			{
				const auto* p = reinterpret_cast<const std::uint8_t*>(a_at);
				std::string bytes;
				for (int i = 0; i < 16; ++i) bytes += fmt::format("{:02X} ", p[i]);
				auto& t = Tramp();
				if (t.free_size() < 64) {
					logger::warn("undead: {} not hooked - no room left in SKSE's trampoline", a_name);
					return 0;
				}
				std::uint8_t* stub = nullptr;
				std::size_t   n = 0;
				if (p[0] == 0xE9) {  // already hooked by another mod (a jump): we go first, then on to theirs
					const std::uintptr_t dst = a_at + 5 + *reinterpret_cast<const std::int32_t*>(p + 1);
					stub = static_cast<std::uint8_t*>(t.allocate(14));
					stub[0] = 0xFF, stub[1] = 0x25, std::memset(stub + 2, 0, 4), std::memcpy(stub + 6, &dst, 8);
				} else {
					while (n < 5) {
						const std::size_t l = Len(p + n);
						if (!l) {
							logger::warn("undead: {} not hooked - it begins with what we can't move ({})", a_name, bytes);
							return 0;
						}
						n += l;
					}
					stub = static_cast<std::uint8_t*>(t.allocate(n + 14));
					std::memcpy(stub, p, n);
					const std::uintptr_t back = a_at + n;
					stub[n] = 0xFF, stub[n + 1] = 0x25, std::memset(stub + n + 2, 0, 4), std::memcpy(stub + n + 6, &back, 8);
				}
				t.write_branch<5>(a_at, a_to, true);
				logger::info("undead: {} hooked where it begins ({} bytes moved: {})", a_name, n, bytes);
				return reinterpret_cast<std::uintptr_t>(stub);
			}
		}
		struct GraphHook
		{
			static bool thunk(RE::IAnimationGraphManagerHolder* a_this, const RE::BSAnimationUpdateData& a_data)
			{
				const bool r = func(a_this, a_data);
				g_graphCalls.fetch_add(1, std::memory_order_relaxed);
				auto* refr = a_data.refr;
				if (!refr || refr->GetFormType() != RE::FormType::ActorCharacter) return r;
				auto* a = static_cast<RE::Actor*>(refr);
				if (a->IsPlayerRef() || a->IsDead() || a->IsInRagdollState()) return r;
				std::shared_ptr<Rig> rig;
				{
					std::shared_lock l(g_lock);
					if (auto it = g_rigs.find(a->GetFormID()); it != g_rigs.end()) rig = it->second;
				}
				if (rig) {
					g_graphOn = true;
					try {
						PoseRig(a, *rig, std::clamp(a_data.deltaTime, 0.0f, 0.1f));
						g_graphPoses.fetch_add(1, std::memory_order_relaxed);
					} catch (...) {
					}
				}
				return r;
			}
			static inline bool (*func)(RE::IAnimationGraphManagerHolder*, const RE::BSAnimationUpdateData&) = nullptr;
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
			bool            volatileOne = false;
			// (Noah: "something that sets them apart from the rest other than their look") each common kind's own trick
			bool            leaper = false;   // the Draugr Bride: leaps on you from mid range
			bool            turns = false;    // the Risen Dead: wounded, it frenzies
			bool            turned = false;
			bool            brute = false;    // the Volatile: an armoured hide (Gun: half damage unless the head or the back)
			bool            caster = false;   // the mage: a bolt at you now and then (and its summoning scream: Wailer)
			float           castCd = 4.0f, castWind = -1.0f;
			float           pinT = 0;         // held where it stands (casting, summoning)
			RE::NiPoint3    pinAt{};
			RE::NiPoint3    pounceFrom{}, pounceTo{};
			float           pounceT = -1.0f;  // seconds into its pounce (-1 none; below 0 and above -1: its wind-up)
			// (Noah: "the enemy's actual movement syncs with their movement animations") its gait's own speed held
			float           gaitMod = 0;      // our share of its SpeedMult (temporary)
			float           gaitT = 0;        // to the next look at its pace
			float           paceVar = 1;      // (each its own stride: a few percent either way)
			int             lastCap = -2;     // the gait it was last held to (a change is met in one step, not eased - no sliding)
			RE::NiPoint3    gaitPos{};
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
				const std::string path = "CurseOfAlduin\\eyes\\" + e.id + "_glow.nif";
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

		// the mage's bolt: Skyrim's own Firebolt, cast from its hands at you
		void CastBolt(RE::Actor* a_from, RE::Actor* a_at)
		{
			static auto* bolt = RE::TESForm::LookupByID<RE::SpellItem>(0x0007E882);  // (the flame atronach's Firebolt: a creature's own spell)
			if (!bolt || !a_from || !a_at) return;
			if (auto* c = a_from->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) c->CastSpellImmediate(bolt, false, a_at, 1.0f, false, 0.0f, a_from);
		}
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
		// (at the main menu, before any body moves: code is rewritten only while nothing runs it)
		void InstallHooks()
		{
			static bool done = false;
			if (done) return;
			done = true;
			if (const auto orig = Detour::Install(REL::Relocation<std::uintptr_t>{ RELOCATION_ID(32155, 32899) }.address(), reinterpret_cast<std::uintptr_t>(&GraphHook::thunk), "the animation graph's update")) {
				GraphHook::func = reinterpret_cast<decltype(GraphHook::func)>(orig);
			}
		}
		void Assign(RE::Actor* a_actor, Type a_type)
		{
			if (!a_actor) {
				return;
			}
			static bool hooked = false;
			if (!hooked) {
				hooked = true;
				// (Noah: still Draugr moves - the log: neither hook was ever called) the table of functions these bodies
				// really use is read off a live one (the game's listed table for Character is not theirs here - the same
				// was found for its magic: src/Events.cpp HookHarm), and the camera's off the camera itself
				const std::uintptr_t base = REL::Module::get().base();
				const std::uintptr_t liveA = *reinterpret_cast<std::uintptr_t*>(a_actor);
				const std::uintptr_t statA = REL::Relocation<std::uintptr_t>{ RE::VTABLE_Character[0] }.address();
				REL::Relocation<std::uintptr_t> vt{ liveA };
				AnimHook::func = vt.write_vfunc(0x7D, AnimHook::thunk);
				auto*                pcam = RE::PlayerCamera::GetSingleton();
				const std::uintptr_t liveC = pcam ? *reinterpret_cast<std::uintptr_t*>(pcam) : 0;
				const std::uintptr_t statC = REL::Relocation<std::uintptr_t>{ RE::VTABLE_PlayerCamera[0] }.address();
				if (liveC) {
					REL::Relocation<std::uintptr_t> cam{ liveC };
					CamHook::func = cam.write_vfunc(0x2, CamHook::thunk);
				}
				logger::info("undead: animation hooks installed - the body's table +{:X} (listed +{:X}), the camera's +{:X} (listed +{:X})", liveA - base, statA - base, liveC ? liveC - base : 0, statC - base);
			}
			{
				std::unique_lock l(g_lock);
				auto& r = g_rigs[a_actor->GetFormID()];
				r = std::make_shared<Rig>();
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
				{  // (its row's own clips: Noah's folders)
					std::shared_lock l(g_lock);
					if (auto it = g_rigs.find(a_actor->GetFormID()); it != g_rigs.end()) it->second->wantSet.store(SetOfRow(a_id));
				}
				// each common kind's own trick (Noah: "something that sets them apart from the rest other than their look")
				for (auto& m : g_minds) {
					if (m.handle.get().get() != a_actor) continue;
					// (Noah: "the zombies shouldn't have a frenzy mode - it makes them look broken; keep them one speed") the Risen Dead no longer turn
					else if (a_id == "runner") m.leaper = true, m.cd = Rand(2.0f, 4.0f);
					else if (a_id == "mage") m.caster = true, m.castCd = Rand(2.5f, 5.0f), m.cd = Rand(6.0f, 10.0f);
					else if (a_id == "volatile") {
						m.brute = true, m.leaper = true, m.cd = Rand(4.0f, 8.0f);
						auto* av = a_actor->AsActorValueOwner();
						av->SetActorValue(RE::ActorValue::kAttackDamageMult, av->GetActorValue(RE::ActorValue::kAttackDamageMult) * 2.0f);  // (Noah: slower, more health and damage)
					}
				}
			}
		}

		// (Noah: the Dance Of Death) the dead dance where they stand: their own clips play a dance (one of the four, at
		// random), their minds stop (no step, no swing) and they take half again as much from every blow - for a_seconds
		std::vector<std::pair<RE::ActorHandle, float>> g_dancers;
		bool Dance(RE::Actor* a_actor, float a_seconds)
		{
			if (!a_actor || a_actor->IsDead()) return false;
			{
				std::shared_lock l(g_lock);
				if (auto it = g_rigs.find(a_actor->GetFormID()); it != g_rigs.end()) {
					it->second->danceK = static_cast<int>(Rand(0.0f, 3.999f));
					it->second->danceT.store(a_seconds);
				}
			}
			a_actor->EnableAI(false);
			a_actor->StopCombat();
			for (auto& d : g_dancers) {
				if (d.first.get().get() == a_actor) { d.second = a_seconds; return true; }
			}
			g_dancers.push_back({ a_actor->GetHandle(), a_seconds });
			return true;
		}
		bool IsDancing(RE::Actor* a_actor)
		{
			if (!a_actor) return false;
			for (auto& d : g_dancers) {
				if (d.second > 0 && d.first.get().get() == a_actor) return true;
			}
			return false;
		}
		void TickDancers(float a_delta)
		{
			for (auto& d : g_dancers) {
				d.second -= a_delta;
				if (d.second <= 0) {
					if (auto a = d.first.get(); a && !a->IsDead()) a->EnableAI(true);
				}
			}
			std::erase_if(g_dancers, [](const auto& d) { return d.second <= 0; });
		}

		bool IsBrute(RE::Actor* a_actor)
		{
			if (!a_actor) return false;
			for (auto& m : g_minds) {
				if (m.brute && m.handle.get().get() == a_actor) return true;
			}
			return false;
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
			// (Noah: "fast runners that sprint" like Dying Light's - no lurching shamblers) runners are the night now: most of
			// it from the first, nearly all of it later; a few wailers and bloaters for the horde's variety
			const float wailers = std::min(0.07f, 0.04f + 0.003f * a_round);
			const float bloaters = 0.0f;  // (Noah: the Sewn Thrall left the night)
			const float runners = 1.0f - wailers - bloaters - std::max(0.0f, 0.25f - 0.03f * a_round);  // (a quarter shamble on night 1, none from night 9)
			const float crawlers = 0.0f;  // (retired: the crawl rig slid along the ground)
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
			case Type::Runner: return 0.9f;   // (the night's main body now: nearly a full zombie's health)
			case Type::Crawler: return 0.6f;
			case Type::Bloater: return 2.4f;
			case Type::Wailer: return 1.1f;
			default: return 1.0f;
			}
		}
		float DamageMult(Type a_type)
		{
			switch (a_type) {  // how hard each kind hits
			case Type::Runner: return 0.85f;
			case Type::Bloater: return 1.6f;
			case Type::Wailer: return 0.8f;
			case Type::Stalker: return 1.2f;
			case Type::Reaper: return 1.4f;
			case Type::Necro: return 0.9f;
			default: return 1.0f;
			}
		}

		float PlayerSlow() { return g_grabSlow > 0 ? 0.35f : 1.0f; }

		std::shared_ptr<Rig> RigOf(RE::Actor* a_actor)
		{
			std::shared_lock l(g_lock);
			auto it = g_rigs.find(a_actor->GetFormID());
			return it != g_rigs.end() ? it->second : nullptr;
		}
		// the speed its legs are made for: its gait's clip (a set without that gait: the next one down it has)
		float GaitClipSpeed(int a_set, int a_cap)
		{
			if (a_set < 0 || a_set >= kSets || !g_clipSets[a_set].ok) return 0;
			const ClipSet& S = g_clipSets[a_set];
			if (a_cap == zclip::kSprint && !S.Has(zclip::kSprint)) a_cap = zclip::kRun;
			if (a_cap == zclip::kWalk && !S.Has(zclip::kWalk)) a_cap = zclip::kRun;
			return S.c[a_cap].speed;
		}
		// four times a second: its pace against its gait's clip, and its SpeedMult eased so the two meet (the game's own
		// walk / run speeds are nothing like a Mixamo stride). Its other speed changes (its frenzy, a web, the hurry
		// after a lost chase) still count: they scale the pace wanted, and the legs speed up or slow down with them
		void SyncGait(RE::Actor* a, Mind& m, float dt, float dist)
		{
			if ((m.gaitT -= dt) > 0) return;
			const float span = 0.25f - m.gaitT;
			m.gaitT = 0.25f;
			const auto pos = a->GetPosition();
			const float pace = std::hypot(pos.x - m.gaitPos.x, pos.y - m.gaitPos.y) / std::max(0.05f, span);
			m.gaitPos = pos;
			auto r = RigOf(a);
			if (!r || !(UsesClips(r->type) || r->wantSet.load() == 6)) return;
			const int   cap = EffCap(*r);
			const float legs = r->gaitV.load();
			if (legs < 0) return;  // (a swing, a flinch, a scream: leave it be)
			const float clipV = cap >= 0 ? GaitClipSpeed(r->setNow.load(), cap) : legs;
			auto* av = a->AsActorValueOwner();
			const float cur = av->GetActorValue(RE::ActorValue::kSpeedMult);
			const float other = cur - m.gaitMod;
			// (Noah's clips: one stood in its idle stance creeping along) any pace at all counts - a body held too slow by
			// the last correction (the game had dropped from its run to its walk) is brought back up to its gait, never
			// left creeping under the legs' standing threshold
			if (clipV < 1.0f || pace < 4.0f || pace > 2000.0f || m.pinT > 0 || dist < 170.0f || Director::Rising(a) || cur < 1.0f) return;
			const float base = std::max(1.0f, av->GetPermanentActorValue(RE::ActorValue::kSpeedMult));
			const float target = clipV * m.paceVar * std::clamp(other / base, 0.2f, 2.5f);
			const float want = std::clamp(cur * target / pace - other, -0.9f * other, 3.0f * other);
			const bool  changed = cap != m.lastCap;
			m.lastCap = cap;
			const float step = (want - m.gaitMod) * (changed ? 1.0f : 0.5f);
			if (std::abs(step) < 1.5f) return;
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, step);
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);  // (so the new speed takes at once)
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
			m.gaitMod += step;
		}
		void FreshStep(RE::Actor* a_actor)
		{
			if (auto r = a_actor ? RigOf(a_actor) : nullptr) r->warmup.store(2.5f);
		}
		void SetPace(RE::Actor* a_actor, float a_scale)
		{
			if (!a_actor) return;
			auto r = RigOf(a_actor);
			if (!r) return;
			// BO3: the round's pace decides how many walk, run or sprint (the Hollow Bride's 1.45 and up: mostly sprinters)
			const float u = Rand(0.0f, 1.0f);
			float pWalk, pSprint;
			if (a_scale < 1.05f) pWalk = 0.75f, pSprint = 0.0f;
			else if (a_scale < 1.2f) pWalk = 0.4f, pSprint = 0.0f;
			else if (a_scale < 1.35f) pWalk = 0.15f, pSprint = 0.25f;
			else pWalk = 0.0f, pSprint = 0.6f;
			// (Noah: "the male zombie movement should be slower") the Risen Dead mostly walk, and run a little under their clip
			const bool risen = r->wantSet.load() == 2;
			if (risen) {
				pWalk = a_scale < 1.05f ? 0.9f : a_scale < 1.2f ? 0.7f : a_scale < 1.35f ? 0.5f : 0.3f;
				pSprint = a_scale < 1.2f ? 0.0f : a_scale < 1.35f ? 0.08f : 0.18f;
			}
			int cap = u < pWalk ? zclip::kWalk : zclip::kRun;  // (Noah: "no sprinting - only walking and running")
			(void)pSprint;
			if (r->wantSet.load() == 6) cap = zclip::kRun;  // (the Skeleton Mutant: it runs you down)
			if (r->wantSet.load() == 4) cap = -1;  // (the mage: as the game moves it - it walks, runs and sidesteps to keep its distance)
			r->gaitCap.store(cap);
			for (auto& m : g_minds) {
				if (m.handle.get().get() == a_actor) m.paceVar = risen ? Rand(0.82f, 0.92f) : Rand(0.93f, 1.07f), m.gaitPos = a_actor->GetPosition();
			}
		}

		bool HasClips(RE::Actor* a_actor)
		{
			if (!a_actor) return false;
			std::shared_ptr<Rig> r;
			{
				std::shared_lock l(g_lock);
				if (auto it = g_rigs.find(a_actor->GetFormID()); it != g_rigs.end()) r = it->second;
			}
			if (!r || !UsesClips(r->type)) return false;
			LoadClips();
			return g_clipSets[SkeletonOf(a_actor)].ok;
		}
		float StandUpSeconds()
		{
			LoadClips();
			const auto& c = g_clipSets[0].ok ? g_clipSets[0].c[zclip::kStandUp] : g_clipSets[1].c[zclip::kStandUp];
			return std::max(0.5f, c.Seconds() / zclip::ClipRate(zclip::kStandUp));
		}
		void SetVolatile(RE::Actor* a_actor)
		{
			if (!a_actor) return;
			{
				std::shared_lock l(g_lock);
				if (auto it = g_rigs.find(a_actor->GetFormID()); it != g_rigs.end()) it->second->volatileOne.store(true);
			}
			for (auto& m : g_minds) {
				if (m.handle.get().get() == a_actor) m.volatileOne = true, m.cd = Rand(3.0f, 5.0f);
			}
		}
		void OnHit(RE::Actor* a_actor)
		{
			if (!a_actor) return;
			static std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> last;
			const auto now = std::chrono::steady_clock::now();
			auto& t = last[a_actor->GetFormID()];
			if (now - t < std::chrono::milliseconds(2200)) return;  // (a flinch now and then, not every pellet)
			t = now;
			if (last.size() > 400) last.clear();
			std::shared_lock l(g_lock);
			if (auto it = g_rigs.find(a_actor->GetFormID()); it != g_rigs.end()) it->second->hitAt.store(1.0f);
		}

		void Tick(RE::PlayerCharacter* a_player, float a_delta)
		{
			if (!a_player) {
				return;
			}
			g_grabbed = std::max(0.0f, g_grabbed - a_delta);
			g_wailBoost = std::max(0.0f, g_wailBoost - a_delta);
			TickDancers(a_delta);
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
			std::vector<RE::NiPoint3> raise;  // (the audit: the scream spawned inside this loop - a push_back on g_minds left it dangling)
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
						Director::Hazard(at, 0.05f, 230.0f, 0.15f, "", false);  // (no stagger: Noah)
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
				if (IsDancing(a.get())) continue;  // (the Dance Of Death: no tricks while it dances)
				SyncGait(a.get(), m, a_delta, dist);
				m.cd -= a_delta;
				// the Risen Dead turns: hurt past half, it shrieks and comes on in a frenzy - faster feet, faster claws
				if (m.turns && !m.turned) {
					auto* zav = a->AsActorValueOwner();
					const float mx = zav->GetPermanentActorValue(RE::ActorValue::kHealth);
					if (mx > 0 && zav->GetActorValue(RE::ActorValue::kHealth) < 0.5f * mx) {
						m.turned = true;
						zav->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, 20.0f);
						{
							std::shared_lock l(g_lock);
							if (auto it = g_rigs.find(a->GetFormID()); it != g_rigs.end()) it->second->turned.store(true), it->second->gaitCap.store(zclip::kRun);  // (Noah: slower - a frenzied Risen Dead runs, never sprints)
						}
						// (no roar: Noah - combat only; its frenzy shows in its pace and its claws)
						Audio::Voice(a.get(), Audio::VoiceEvent::kHurt);
					}
				}
				// the pounce (the Volatile risen by noise: long and often; the Draugr Bride: a shorter leap): from mid range, a
				// leap that closes most of the gap in a third of a second (only with a clear line to you, never through a
				// wall), its claws coming as it lands
				// (the mage, the summoning brute) held where it stands while it casts
				if (m.pinT > 0) {
					m.pinT -= a_delta;
					a->SetPosition(m.pinAt, true);
				}
				// the mage's bolt: a moment's wind-up (its cast), then a spell of fire at you - only with a clear line
				if (m.caster) {
					if (m.castWind >= 0) {
						if ((m.castWind -= a_delta) < 0) {
							bool los = false;
							if (a->HasLineOfSight(a_player, los) && los) CastBolt(a.get(), a_player);
						}
					} else if ((m.castCd -= a_delta) <= 0 && m.wind < 0) {
						bool los = false;
						if (dist > 350.0f && dist < 2600.0f && a->HasLineOfSight(a_player, los) && los) {
							m.castWind = 0.95f;
							m.castCd = Rand(5.0f, 8.0f);
							m.pinT = 1.6f, m.pinAt = a->GetPosition();
							SetCue(a.get(), kCueCast, 0.3f);
							Forms::PlayAt(gen::Sound::champ_telegraph, a.get());
						} else {
							m.castCd = 0.5f;
						}
					}
				}
				if (m.volatileOne || m.leaper) {
					// (the Brute's jump attack: a crouch before it leaves the ground - its clip - then a heavy arc)
					const float pMin = m.volatileOne ? 260.0f : m.brute ? 280.0f : 220.0f, pMax = m.volatileOne ? 760.0f : m.brute ? 650.0f : 540.0f;
					const float pDur = m.volatileOne ? 0.35f : m.brute ? 0.75f : 0.4f, pH = m.volatileOne ? 70.0f : m.brute ? 85.0f : 55.0f;
					if (m.pounceT < 0 && m.pounceT > -1.0f) {  // its wind-up
						m.pounceT += a_delta;
						a->SetPosition(m.pounceFrom, true);
						if (m.pounceT >= 0) m.pounceT = 0.0f;
						continue;
					}
					if (m.pounceT >= 0) {
						m.pounceT += a_delta;
						const float u = std::min(1.0f, m.pounceT / pDur);
						RE::NiPoint3 at = m.pounceFrom + (m.pounceTo - m.pounceFrom) * u;
						at.z += std::sin(u * RE::NI_PI) * pH;
						a->SetPosition(at, true);
						if (u >= 1.0f) {
							m.pounceT = -1.0f;
							a->MoveToNearestNavmesh();
						}
						continue;
					}
					bool los = false;
					if (m.cd <= 0 && dist > pMin && dist < pMax && a->HasLineOfSight(a_player, los) && los) {
						RE::NiPoint3 dir = pp - a->GetPosition();
						dir.z = 0;
						const float d = dir.Length();
						dir.Unitize();
						m.pounceFrom = a->GetPosition();
						m.pounceTo = m.pounceFrom + dir * std::max(0.0f, d - 120.0f);
						if (float land = 0; RE::TES::GetSingleton()->GetLandHeight(m.pounceTo, land) && std::abs(land - m.pounceTo.z) < 120.0f) m.pounceTo.z = land;
						else m.pounceTo.z = m.pounceFrom.z;
						m.pounceT = m.brute ? -0.55f : 0.0f;
						m.cd = m.volatileOne ? Rand(5.0f, 7.5f) : m.brute ? Rand(7.0f, 11.0f) : Rand(6.0f, 9.0f);
						SetCue(a.get(), m.brute ? kCueLeap : kCueGrab, m.brute ? 0.3f : 0.5f);
						Forms::PlayAt(gen::Sound::champ_telegraph, a.get());
						continue;
					}
					if (m.volatileOne) continue;  // (its scream came when it rose; no other specials)
				}
				if (m.wind >= 0) {
					if ((m.wind -= a_delta) < 0) {
						if (m.type == Type::Wailer) {  // the scream: the horde comes, faster
							g_wailBoost = 6.0f;
							for (int i = 0; i < 3; ++i) {
								const float ang = Rand(0, 6.2832f);
								raise.push_back(a->GetPosition() + RE::NiPoint3{ std::cos(ang) * 260.0f, std::sin(ang) * 260.0f, 40.0f });
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
					if (dist < 1800.0f && m.castWind < 0) {
						m.wind = 1.1f;
						if (m.caster) m.pinT = 3.0f, m.pinAt = a->GetPosition(), m.castCd = std::max(m.castCd, 3.5f);  // (the mage stands to summon)
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
			for (const auto& at : raise) Director::RaiseAt(at);  // (after the loop: a spawn adds to g_minds)
			std::erase_if(g_minds, [](const Mind& m) { return m.cd <= -99; });
			// rigs of the gone (deleted, unloaded) are dropped now and then
			static float told = 0;
			if ((told += a_delta) > 15.0f) {
				told = 0;
				std::size_t rigs = 0;
				{
					std::shared_lock l(g_lock);
					rigs = g_rigs.size();
				}
				if (rigs > 0) logger::info("undead: {} rigs - animation updates {} ({} of ours), camera updates {} (poses {}, skipped {}), graph updates {} (poses {}), clip poses {}, no clip set {}", rigs, g_hookCalls.exchange(0), g_hookRigs.exchange(0), g_camCalls.exchange(0), g_camPoses.exchange(0), g_camSkip.exchange(0), g_graphCalls.exchange(0), g_graphPoses.exchange(0), g_clipPosed.exchange(0), g_clipNo.exchange(0));
			}
			static float sweep = 0;
			if ((sweep += a_delta) > 5.0f) {
				sweep = 0;
				std::unique_lock l(g_lock);
				std::erase_if(g_eyes, [](const auto& e) {
					auto* f = RE::TESForm::LookupByID<RE::Actor>(e.first);
					return !f || f->IsDeleted();
				});
				{
					std::vector<RE::NiPointer<RE::NiAVObject>> drop;
					{
						std::scoped_lock gl(g_graveLock);
						drop.swap(g_grave);
					}
				}
				std::erase_if(g_rigs, [](const auto& e) {
					auto* f = RE::TESForm::LookupByID<RE::Actor>(e.first);
					return !f || f->IsDeleted() || f->IsDead();
				});
			}
		}

		void Reset()
		{
			for (auto& d : g_dancers) {
				if (auto a = d.first.get(); a && !a->IsDead()) a->EnableAI(true);
			}
			g_dancers.clear();
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
