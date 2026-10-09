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
			/* shambler  */ { 0.38f, 0.10f, 0.30f, 0.6f, 1.05f, 0.18f, 0.35f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.35f, 1.0f, 1.0f, 0.0f },
			/* runner    */ { 0.30f, 0.05f, 0.12f, 0.9f, 0.0f, 0.10f, 0.9f, 0.0f, 0.42f, 0.0f, 0.0f, 0.0f, 1.3f, 1.0f, 0.95f, 1.0f },
			/* crawler   */ { 0.0f, 0.12f, 0.20f, 0.7f, 0.0f, 0.25f, 0.4f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.45f, 1.0f, 1.0f, 0.0f },
			/* wailer    */ { 0.15f, 0.06f, 0.45f, 1.0f, 0.35f, 0.55f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.6f, 0.92f, 1.12f, 0.0f },
			/* bloater   */ { 0.12f, 0.22f, 0.15f, 0.2f, 0.45f, 0.45f, 0.25f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 1.2f, 1.45f, 0.85f, 0.0f },
			/* stalker   */ { 0.62f, 0.05f, 0.55f, 0.8f, 0.55f, 0.55f, 0.55f, 0.0f, 0.18f, 0.0f, -18.0f, 0.0f, 1.75f, 1.0f, 0.95f, 0.0f },
			/* reaper    */ { 0.18f, 0.04f, 0.10f, 0.1f, 0.0f, 0.12f, 0.1f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 1.25f, 1.12f, 0.9f, 0.0f },
			/* necro     */ { 0.10f, 0.03f, 0.10f, 0.0f, 0.55f, 0.75f, 0.5f, 0.0f, 0.0f, 0.0f, 42.0f, 7.0f, 1.2f, 1.0f, 1.0f, 0.0f },
			/* wyrm      */ { 0.50f, 0.07f, 0.20f, 0.2f, 0.70f, 0.40f, 0.45f, 0.0f, 0.12f, 0.0f, 0.0f, 0.0f, 1.9f, 1.18f, 1.08f, 0.0f },
		};

		enum Bone { kCOM, kPelvis, kSpine1, kSpine2, kNeck, kHead, kLClav, kRClav, kLArm, kRArm, kLFore, kRFore, kLHand, kRHand, kLThigh, kRThigh, kBones };
		constexpr const char* kBoneNames[kBones] = { "NPC COM [COM ]", "NPC Pelvis [Pelv]", "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]", "NPC Neck [Neck]", "NPC Head [Head]",
			"NPC L Clavicle [LClv]", "NPC R Clavicle [RClv]", "NPC L UpperArm [LUar]", "NPC R UpperArm [RUar]", "NPC L Forearm [LLar]", "NPC R Forearm [RLar]",
			"NPC L Hand [LHnd]", "NPC R Hand [RHnd]", "NPC L Thigh [LThg]", "NPC R Thigh [RThg]" };

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
			r.weight += ((attacking ? 0.25f : 1.0f) - r.weight) * std::min(1.0f, dt * 8.0f);
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
			// ---- the arms
			for (int side = 0; side < 2; ++side) {
				const float sg = side == 0 ? -1.0f : 1.0f;  // left, right
				auto& clav = B[side == 0 ? kLClav : kRClav];
				auto& arm = B[side == 0 ? kLArm : kRArm];
				auto& fore = B[side == 0 ? kLFore : kRFore];
				auto& hand = B[side == 0 ? kLHand : kRHand];
				if (clav.node && d.chest != 1.0f) {
					clav.node->local.scale = clav.baseScale / d.chest;
					Refresh(clav);
				}
				done(side == 0 ? kLClav : kRClav);
				float reach = d.reach * w, spread = d.spread * w, elbow = d.elbow * w;
				float swing = 0;
				if (d.swingArms > 0) {  // a runner's arms pump behind it
					reach = -0.55f * d.swingArms * moving * w;
					swing = 0.7f * std::sin(r.phase + (side ? RE::NI_PI : 0)) * moving * w;
				} else {
					swing = 0.12f * std::sin(r.phase * 0.9f + side * 1.7f + r.seed) * w;  // grasping, never quite still
				}
				if (d.crawl > 0) {  // a crawler's arms haul it along: reach far ahead, plant, pull
					const float c = std::sin(r.phase + (side ? RE::NI_PI : 0));
					reach = (1.6f + 0.6f * c) * d.crawl;
					spread = 0.35f;
					elbow = 0.6f + 0.5f * std::max(0.0f, -c);
				}
				if (cue == Undead::kCueRoar || cue == Undead::kCueScream) {  // arms flung wide
					reach = reach * (1 - cueK) + 0.6f * cueK;
					spread = spread * (1 - cueK) + 1.25f * cueK;
				}
				if (cue == Undead::kCueGrab) {  // both arms up, lunging to seize
					reach = reach * (1 - cueK) + 1.5f * cueK;
					spread = spread * (1 - cueK) + 0.15f * cueK;
				}
				if ((cue == Undead::kCueSwipeL && side == 0) || (cue == Undead::kCueSwipeR && side == 1)) {  // a great raking sweep
					reach = 1.2f * cueK + reach * (1 - cueK);
					spread = (1.4f - 2.2f * Ease(1.0f - r.cueLeft.load() / std::max(0.05f, r.cueLen))) * cueK + spread * (1 - cueK);
				}
				if (cue == Undead::kCueChannel) {  // a necromancer's raised hands
					reach = 1.9f * cueK + reach * (1 - cueK);
				}
				Turn(arm, AxisAngle(rt, reach + swing) * AxisAngle(f, -sg * spread));
				Refresh(arm), done(side == 0 ? kLArm : kRArm);
				Turn(fore, AxisAngle(rt, elbow));
				Refresh(fore), done(side == 0 ? kLFore : kRFore);
				if (hand.node && d.hands != 1.0f) {  // claws: hands too big for the arms that carry them
					hand.node->local.scale = hand.baseScale * d.hands;
					Refresh(hand);
				}
				done(side == 0 ? kLHand : kRHand);
			}
			// ---- legs: the shambler's dragging foot; a floating necromancer's hang loose
			{
				if (d.limp > 0) {
					Turn(B[kLThigh], AxisAngle(rt, 0.22f * d.limp) * AxisAngle(f, 0.12f * d.limp));
					Refresh(B[kLThigh]);
				}
				if (d.lift > 20.0f) {
					for (int i : { kLThigh, kRThigh }) {
						Turn(B[i], AxisAngle(rt, 0.25f + 0.05f * std::sin(r.phase * 0.6f + i)));
						Refresh(B[i]);
					}
				}
				done(kLThigh), done(kRThigh);
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

		void CueFor(RE::Actor* a_actor, int a_cue, float a_seconds)
		{
			if (a_actor) {
				SetCue(a_actor, a_cue, a_seconds);
			}
		}

		Type Roll(int a_round)
		{
			const float u = Rand(0.0f, 1.0f);
			const float runners = a_round >= 3 ? std::min(0.32f, 0.10f + 0.02f * a_round) : 0.0f;
			const float crawlers = a_round >= 2 ? 0.14f : 0.0f;
			const float wailers = a_round >= 6 ? 0.05f : 0.0f;
			const float bloaters = a_round >= 4 ? 0.07f : 0.0f;
			if (u < runners) return Type::Runner;
			if (u < runners + crawlers) return Type::Crawler;
			if (u < runners + crawlers + wailers) return Type::Wailer;
			if (u < runners + crawlers + wailers + bloaters) return Type::Bloater;
			return Type::Shambler;
		}

		float SpeedMult(Type a_type)
		{
			switch (a_type) {
			case Type::Runner: return 1.55f;
			case Type::Crawler: return 0.8f;
			case Type::Bloater: return 0.72f;
			case Type::Wailer: return 0.95f;
			default: return 1.0f;
			}
		}
		float HealthMult(Type a_type)
		{
			switch (a_type) {
			case Type::Runner: return 0.8f;
			case Type::Crawler: return 0.6f;
			case Type::Bloater: return 2.2f;
			case Type::Wailer: return 1.2f;
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
				// the lunge in flight: it throws itself at you
				if (m.lunge >= 0) {
					m.lunge -= a_delta;
					const float step = (m.type == Type::Crawler ? 480.0f : 760.0f) * a_delta;
					if (dist > 70.0f) {
						a->SetPosition(a->GetPosition() + m.lungeDir * step, true);
					}
					if (m.lunge < 0 || dist < 80.0f) {
						m.lunge = -1;
						if (dist < 110.0f && g_grabbed <= 0) {
							g_grabbed = 1.2f;
							g_grabSlow = m.type == Type::Crawler ? 1.6f : 1.1f;
							Director::HurtPlayer(m.type == Type::Crawler ? 0.08f : 0.12f, m.type != Type::Crawler);
							Forms::PlayAt(gen::Sound::grab, a.get());
							Hud::Banner(m.type == Type::Crawler ? "It has your ankle!" : "GRABBED!", 0.9f);
						}
					}
					continue;
				}
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
							Hud::Banner("A Wailer screams - the dead come running!", 2.0f);
							Forms::PlayAt(gen::Sound::wailer_scream, a.get());
						} else {
							RE::NiPoint3 dir = pp - a->GetPosition();
							dir.z = 0;
							const float len = dir.Length();
							if (len > 1.0f) {
								m.lungeDir = dir * (1.0f / len);
								m.lunge = m.type == Type::Crawler ? 0.28f : 0.22f;
								SetCue(a.get(), kCueGrab, 0.6f);
							}
						}
					}
					continue;
				}
				if (m.cd > 0) {
					continue;
				}
				switch (m.type) {
				case Type::Shambler:
				case Type::Runner:
					if (dist < (m.type == Type::Runner ? 330.0f : 240.0f) && dist > 90.0f) {
						m.wind = m.type == Type::Runner ? 0.25f : 0.45f;  // arms come up, then it throws itself at you
						SetCue(a.get(), kCueGrab, m.wind + 0.5f);
						Forms::PlayAt(gen::Sound::rise, a.get());
						m.cd = Rand(4.5f, 7.0f);
					}
					break;
				case Type::Crawler:
					if (dist < 200.0f && dist > 60.0f) {
						m.wind = 0.3f;
						m.cd = Rand(3.0f, 5.0f);
					}
					break;
				case Type::Wailer:
					if (dist < 1800.0f) {
						m.wind = 1.1f;
						SetCue(a.get(), kCueScream, 1.6f);
						Forms::PlayAt(gen::Sound::hound_night, a.get());
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
