#pragma once
// The undead's Mixamo clips, played and blended (no game types: tools/modelconv/zprev renders its previews through
// this same code, so what Noah sees in a preview video is what moves in game).
//
// (Noah: "make the arms attack animation more fluid so it looks more realistic on all the models")
//  - an attack or a flinch is an upper-body layer: the legs keep running under it (no feet frozen mid-slide), the
//    spine twisting into the swing a little more at each vertebra, the arms and head all the swing's
//  - the run keeps its own stride through everything, so coming out of a swing never restarts the stride
//  - eased cross-fades: quick into a swing, slower back out of it
//  - follow-through: each arm joint trails the one above it by a few hundredths of a second (shoulder, elbow,
//    wrist, fingers), so a swing whips through instead of snapping between keys; the head settles the same way
//  - the swings play nearer their own speed (they were rushed up to twice over)
// (Noah: "instead of keeping their elbows locked make it more fluid ... their elbows being stuck at one angle") the
// Mixamo run holds each elbow within ten degrees the whole stride; now each elbow lives: it bends as its arm swings
// forward and opens as it swings back, and the forearm carries weight - it lags as the arm whips into a swing and
// snaps through as the arm stops (a damped spring on the elbow hinge), never past straight
// (Noah: "now do the same for the wrists") each hand hangs loose from its forearm the same way: it trails the forearm
// as the arm whips round and flicks through after it, bending back and forth and a little side to side (every bone
// of both skeletons runs along its own Z, so a wrist bends about its X and Y)
// (Noah: "the enemies wrists are bent they should be straight and animated through the attack swings") the retargeted
// clips held each hand turned 40-90 degrees off its forearm the whole time; now a hand rests straight - the skeleton's
// own bind pose (rest.zan) - and plays only the clip's motion about that hand's average pose, seen from the forearm
// (Noah: "make the enemies attacks and movement make sense with what is in the folders") each kind has its own set of
// clips (anims/<set>: risen, brute, mage, girl), anything a set lacks taken from the shared one; its feet pick the
// walk, run or sprint nearest its pace (and a sidestep when it moves sideways), cross-fading between them on one
// stride; the dances (anims/dance) are shared by all
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace zclip
{
	enum ClipId { kRun, kAttack, kPunch, kScream, kHit, kHit2, kStandUp, kAttackL,  // (kAttackL: the swing mirrored, for a left-handed blade)
		kWalk, kSprint, kIdle, kCast, kSummon, kLeap, kAttack2, kAttack3, kStrafeL, kStrafeR, kStart, kDance0, kDance1, kDance2, kDance3, kClips };
	inline constexpr const char* kClipFile[kClips] = { "run", "attack", "punch", "scream", "hit", "hit2", "standup", "attack_l",
		"walk", "sprint", "idle", "cast", "summon", "leap", "attack2", "attack3", "strafe_l", "strafe_r", "start", "dance0", "dance1", "dance2", "dance3" };
	inline constexpr int kDances = 4;
	inline bool Loops(int a_clip) { return a_clip == kRun || a_clip == kWalk || a_clip == kSprint || a_clip == kIdle || a_clip == kStrafeL || a_clip == kStrafeR || (a_clip >= kDance0 && a_clip <= kDance3); }
	inline bool Locomotion(int a_clip) { return a_clip == kRun || a_clip == kWalk || a_clip == kSprint || a_clip == kIdle || a_clip == kStrafeL || a_clip == kStrafeR; }
	using Q4 = std::array<float, 4>;  // w x y z
	struct V3f { float x = 0, y = 0, z = 0; };

	struct Clip
	{
		int                      fps = 30, frames = 0;
		float                    speed = 0;  // units a second its body travelled (the run: matched to the pace)
		std::vector<std::string> bones;
		std::vector<float>       q;    // frames * bones * (w x y z), each against its parent
		std::vector<float>       com;  // frames * 3
		float Seconds() const { return frames > 1 ? (frames - 1) / static_cast<float>(fps) : 0.0f; }
	};

	struct ClipSet
	{
		std::array<Clip, kClips> c;
		std::array<bool, kClips> own{};  // (the set's own file; the rest stood in by Fill)
		bool                     ok = false;
		std::vector<float>       upper;  // per bone: how much of a layered swing it takes (legs 0, arms 1)
		std::vector<float>       tau;    // per bone: its follow-through lag in seconds (0 none)
		std::array<int, 2>       upperArm{ -1, -1 }, forearm{ -1, -1 }, hand{ -1, -1 };  // (left, right: elbows, wrists)
		Clip                     rest;                        // (one frame: the skeleton's bind rotations; optional)
		std::array<std::array<Q4, kClips>, 2> handMean{};     // (each clip's average turn of each hand)
		// what this set lacks: the shared set's run, scream, flinches and stand-up; a cast or a summon from its swing or
		// its scream; the second and third swings from the first; the dances from the dance set. Walk, sprint, idle,
		// sidesteps and the start stay absent (the feet use what the set has)
		void Fill(const ClipSet* a_base, const ClipSet* a_dance)
		{
			auto from = [&](int k, const Clip& src) { if (!own[k] && c[k].frames == 0 && src.frames > 0) c[k] = src; };  // (first stand-in wins: the dances before the idle)
			if (a_base) for (int k : { kRun, kAttack, kPunch, kScream, kHit, kHit2, kStandUp }) from(k, a_base->c[k]);
			from(kAttackL, c[kAttack]);
			from(kCast, c[kAttack]);
			from(kSummon, c[kScream]);
			from(kLeap, c[kAttack]);
			from(kAttack2, c[kAttack]);
			from(kAttack3, c[kAttack2]);
			for (int d = kDance0; d <= kDance3; ++d) {
				if (a_dance && a_dance->own[d]) from(d, a_dance->c[d]);
				from(d, c[kIdle].frames > 0 ? c[kIdle] : c[kRun]);
			}
		}
		bool Has(int k) const { return own[k] && c[k].frames > 1; }
		void Prepare()
		{
			const auto& names = c[kRun].bones;
			upper.assign(names.size(), 1.0f), tau.assign(names.size(), 0.0f);
			for (std::size_t i = 0; i < names.size(); ++i) {
				const std::string& n = names[i];
				auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
				if (has("Pelvis") || has("Thigh") || has("Calf") || has("Foot") || has("Toe")) upper[i] = 0.0f;
				else if (has("Spine [")) upper[i] = 0.35f;
				else if (has("Spine1")) upper[i] = 0.7f;
				if (has("Clavicle")) tau[i] = 0.03f;
				else if (has("UpperArm")) tau[i] = 0.045f;
				else if (has("Forearm")) tau[i] = 0.06f;
				else if (has("Hand")) tau[i] = 0.075f;
				else if (has("Finger")) tau[i] = 0.09f;
				else if (has("Head")) tau[i] = 0.05f;
				else if (has("Neck")) tau[i] = 0.035f;
				const int side = has(" L ") ? 0 : has(" R ") ? 1 : -1;
				if (side >= 0 && has("UpperArm")) upperArm[side] = static_cast<int>(i);
				if (side >= 0 && has("Forearm")) forearm[side] = static_cast<int>(i);
				if (side >= 0 && has("Hand")) hand[side] = static_cast<int>(i);
			}
			const std::size_t nb = names.size();
			for (int side = 0; side < 2; ++side) {
				if (hand[side] < 0) continue;
				const std::size_t h = static_cast<std::size_t>(hand[side]);
				for (int k = 0; k < kClips; ++k) {
					const Clip& C = c[k];
					Q4 acc{ 0, 0, 0, 0 }, first{ 1, 0, 0, 0 };
					for (int f = 0; f < C.frames; ++f) {
						Q4 q; for (int j = 0; j < 4; ++j) q[j] = C.q[(f * nb + h) * 4 + j];
						if (f == 0) first = q;
						const float d = q[0] * first[0] + q[1] * first[1] + q[2] * first[2] + q[3] * first[3];
						for (int j = 0; j < 4; ++j) acc[j] += d < 0 ? -q[j] : q[j];
					}
					const float l = std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2] + acc[3] * acc[3]);
					handMean[side][k] = l > 1e-6f ? Q4{ acc[0] / l, acc[1] / l, acc[2] / l, acc[3] / l } : Q4{ 1, 0, 0, 0 };
				}
			}
		}
	};

	inline Q4 Slerp(const Q4& a, Q4 b, float t)
	{
		float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
		if (d < 0) { d = -d; for (auto& x : b) x = -x; }
		Q4 r;
		if (d > 0.9995f) {
			for (int i = 0; i < 4; ++i) r[i] = a[i] + (b[i] - a[i]) * t;
		} else {
			const float th = std::acos(std::clamp(d, -1.0f, 1.0f)), s = std::sin(th);
			const float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
			for (int i = 0; i < 4; ++i) r[i] = a[i] * wa + b[i] * wb;
		}
		const float l = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
		for (auto& x : r) x /= std::max(1e-6f, l);
		return r;
	}
	inline float FrameAt(const Clip& c, float t, bool loop)
	{
		float f = t * c.fps;
		const float last = static_cast<float>(c.frames - 1);
		return loop ? std::fmod(std::max(0.0f, f), last) : std::clamp(f, 0.0f, last);
	}
	inline Q4 Sample(const Clip& c, float t, bool loop, std::size_t bone)
	{
		const float f = FrameAt(c, t, loop);
		const int i0 = static_cast<int>(f), i1 = std::min(c.frames - 1, i0 + 1);
		const std::size_t nb = c.bones.size();
		Q4 a, b;
		for (int k = 0; k < 4; ++k) a[k] = c.q[(i0 * nb + bone) * 4 + k], b[k] = c.q[(i1 * nb + bone) * 4 + k];
		return Slerp(a, b, f - i0);
	}
	inline V3f SampleCom(const Clip& c, float t, bool loop)
	{
		const float f = FrameAt(c, t, loop);
		const int i0 = static_cast<int>(f), i1 = std::min(c.frames - 1, i0 + 1);
		const float u = f - i0;
		auto at = [&](int i, int k) { return c.com[i * 3 + k]; };
		return { at(i0, 0) + (at(i1, 0) - at(i0, 0)) * u, at(i0, 1) + (at(i1, 1) - at(i0, 1)) * u, at(i0, 2) + (at(i1, 2) - at(i0, 2)) * u };
	}
	inline V3f Lerp(const V3f& a, const V3f& b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }
	inline Q4 Mul(const Q4& a, const Q4& b)
	{
		return { a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3], a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
			a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1], a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0] };
	}
	inline Q4 Conj(const Q4& a) { return { a[0], -a[1], -a[2], -a[3] }; }
	inline V3f RotVec(Q4 q)  // (axis * angle)
	{
		if (q[0] < 0) for (auto& x : q) x = -x;
		const float s = std::sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
		if (s < 1e-7f) return { 2 * q[1], 2 * q[2], 2 * q[3] };
		const float a = 2 * std::atan2(s, q[0]) / s;
		return { q[1] * a, q[2] * a, q[3] * a };
	}
	inline V3f Rotate(const Q4& q, const V3f& v)
	{
		const Q4 r = Mul(Mul(q, { 0, v.x, v.y, v.z }), Conj(q));
		return { r[1], r[2], r[3] };
	}
	inline float Dot(const V3f& a, const V3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline float ClipRate(int a_clip)
	{
		return a_clip == kAttack || a_clip == kAttackL || a_clip == kAttack2 || a_clip == kAttack3 ? 1.2f : a_clip == kPunch ? 1.15f : a_clip == kHit || a_clip == kHit2 ? 1.15f : a_clip == kStandUp ? 1.15f : 1.0f;
	}
	inline bool Layered(int a_clip) { return a_clip == kAttack || a_clip == kAttackL || a_clip == kAttack2 || a_clip == kAttack3 || a_clip == kPunch || a_clip == kHit || a_clip == kHit2; }
	// what the feet are doing this frame: pace (units a second), sideways share of it (-1 left .. 1 right), frenzied
	struct Loco { float speed = 0, lateral = 0; bool frenzy = false; };

	struct Player
	{
		int             clip = -1, prevClip = -1;
		float           clipT = 0, prevT = 0, rate = 1, prevRate = 1;
		float           blend = 1, fade = 0.2f;
		float           runT = 0;            // (unused: the stride is phase now)
		float           phase = 0;           // the stride, 0..1, always going, shared by every gait so a change keeps the step
		int             loco = kRun, prevLoco = -1;
		float           locoRate = 0, prevLocoRate = 0, locoBlend = 1;
		float           still = 1;           // 1 standing (a swing takes the whole body), 0 running (only the top)
		std::vector<Q4> held;                // the follow-through: where each bone actually is
		bool            heldOk = false;
		struct Elbow { float off = 0, vel = 0, w = 0; Q4 prevU{ 1, 0, 0, 0 }, meanU{ 1, 0, 0, 0 }; bool ok = false; };
		std::array<Elbow, 2> elbow;
		struct Wrist { float x = 0, y = 0, vx = 0, vy = 0; V3f w{}; Q4 prev{ 1, 0, 0, 0 }; bool ok = false; };
		std::array<Wrist, 2> wrist;

		bool OneShotOn(const ClipSet& S) const { return clip >= 0 && !Loops(clip) && clipT < S.c[clip].Seconds(); }
		// the gait nearest the pace (a step past the current one is needed before it changes: no flicker at the edges)
		void PickLoco(const ClipSet& S, const Loco& in)
		{
			int want = kRun;
			float rate = 0;
			if (in.speed < 15.0f) {
				want = S.Has(kIdle) ? kIdle : kRun;
				rate = S.Has(kIdle) ? 1.0f : 0.0f;
			} else if (std::abs(in.lateral) > 0.65f && S.Has(in.lateral > 0 ? kStrafeR : kStrafeL)) {
				want = in.lateral > 0 ? kStrafeR : kStrafeL;
			} else {
				float best = 1e9f;
				for (int k : { kWalk, kRun, kSprint }) {
					if ((k != kRun && !S.Has(k)) || S.c[k].speed <= 1.0f) continue;
					float d = std::abs(std::log(in.speed / S.c[k].speed));
					if (k == kSprint && in.frenzy) d -= 0.6f;
					if (k == loco) d -= 0.15f;
					if (d < best) best = d, want = k;
				}
			}
			if (in.speed >= 15.0f) rate = std::clamp(in.speed / std::max(1.0f, S.c[want].speed), 0.4f, 2.2f);
			if (want != loco) prevLoco = loco, prevLocoRate = locoRate, locoBlend = 0;
			loco = want, locoRate = rate;
		}
		Q4 LocoPose(const ClipSet& S, std::size_t i) const
		{
			auto at = [&](int k) { const float t = phase * S.c[k].Seconds(); return Straight(S, k, i, Sample(S.c[k], t, true, i)); };
			Q4 q = at(loco);
			if (locoBlend < 1.0f && prevLoco >= 0) q = Slerp(at(prevLoco), q, locoBlend * locoBlend * (3 - 2 * locoBlend));
			return q;
		}
		V3f LocoCom(const ClipSet& S) const
		{
			auto at = [&](int k) { return SampleCom(S.c[k], phase * S.c[k].Seconds(), true); };
			V3f c = at(loco);
			if (locoBlend < 1.0f && prevLoco >= 0) c = Lerp(at(prevLoco), c, locoBlend * locoBlend * (3 - 2 * locoBlend));
			return c;
		}
		// start a clip (rate: its speed against its own); a no-op when it already plays
		void Play(int a_want, float a_rate, const ClipSet& S)
		{
			if (a_want == clip) { rate = a_rate; return; }
			prevClip = clip, prevT = clipT, prevRate = rate;
			clip = a_want, clipT = 0, rate = a_rate;
			blend = prevClip >= 0 ? 0.0f : 1.0f;
			// quick into a swing or a flinch, slower back out of one into the run
			fade = a_want == kRun ? (Layered(prevClip) ? 0.3f : 0.25f) : Layered(a_want) ? 0.12f : 0.22f;
			(void)S;
		}
		// a hand: straight off its forearm, moving as the clip moves it about its average
		static Q4 Straight(const ClipSet& S, int c, std::size_t i, const Q4& q)
		{
			if (S.rest.frames < 1 || S.rest.bones.size() != S.c[kRun].bones.size()) return q;
			for (int side = 0; side < 2; ++side) {
				if (S.hand[side] != static_cast<int>(i)) continue;
				Q4 r; for (int j = 0; j < 4; ++j) r[j] = S.rest.q[i * 4 + j];
				return Mul(Mul(q, Conj(S.handMean[side][c])), r);
			}
			return q;
		}
		// one clip's pose for one bone, the run under a layered swing
		Q4 PoseOf(const ClipSet& S, int c, float t, std::size_t i) const
		{
			const Q4 run = LocoPose(S, i);
			if (c < 0 || c == kRun) return run;
			const Q4 act = Straight(S, c, i, Sample(S.c[c], t, Loops(c), i));
			if (!Layered(c)) return act;
			const float m = S.upper[i] + (1.0f - S.upper[i]) * still;
			return m >= 0.999f ? act : m <= 0.001f ? run : Slerp(run, act, m);
		}
		V3f ComOf(const ClipSet& S, int c, float t) const
		{
			const V3f run = LocoCom(S);
			if (c < 0 || c == kRun) return run;
			const V3f act = SampleCom(S.c[c], t, Loops(c));
			return Layered(c) ? Lerp(run, act, still) : act;
		}
		// advance dt and pose: a_out per clip bone (against its parent), a_com the COM's place
		void Tick(const ClipSet& S, float dt, float a_runRate, std::vector<Q4>& a_out, V3f& a_com)  // (the old call: a pace as a share of the run's)
		{
			Tick(S, dt, Loco{ a_runRate * std::max(1.0f, S.c[kRun].speed), 0.0f, false }, a_out, a_com);
		}
		void Tick(const ClipSet& S, float dt, const Loco& a_in, std::vector<Q4>& a_out, V3f& a_com)
		{
			dt = std::clamp(dt, 0.0f, 0.25f);
			PickLoco(S, a_in);
			locoBlend = std::min(1.0f, locoBlend + dt / 0.25f);
			phase = std::fmod(phase + dt * locoRate / std::max(0.1f, S.c[loco].Seconds()), 1.0f);
			if (clip == kRun) rate = 1.0f;
			clipT += dt * rate, prevT += dt * prevRate;
			blend = std::min(1.0f, blend + dt / std::max(0.01f, fade));
			// standing or running: eased, so a swing begun mid-stride settles into the legs
			const float wantStill = std::clamp(1.0f - a_in.speed / 50.0f, 0.0f, 1.0f);
			still += (wantStill - still) * (1.0f - std::exp(-dt / 0.15f));
			const float w = blend * blend * blend * (blend * (blend * 6 - 15) + 10);  // (smootherstep)
			const std::size_t nb = S.c[kRun].bones.size();
			a_out.resize(nb);
			const bool snap = !heldOk || held.size() != nb;
			if (snap) held.resize(nb);
			for (std::size_t i = 0; i < nb; ++i) {
				Q4 q = PoseOf(S, clip, clipT, i);
				if (w < 1.0f && prevClip >= 0) q = Slerp(PoseOf(S, prevClip, prevT, i), q, w);
				if (snap || S.tau[i] <= 0.0f) held[i] = q;
				else held[i] = Slerp(held[i], q, 1.0f - std::exp(-dt / S.tau[i]));
				a_out[i] = held[i];
			}
			heldOk = true;
			// the elbows. (Noah: "his sword arm is getting stuck") the bend is measured as it is - the angle between the
			// upper arm and the forearm (every bone runs along its own Z) - and bent further about the axis it is already
			// bending on, so no twist in a clip (the mirrored blade swing is full of it) can pin an elbow at a limit
			for (int side = 0; side < 2; ++side) {
				const int u = S.upperArm[side], f = S.forearm[side];
				if (u < 0 || f < 0) continue;
				Elbow& e = elbow[side];
				const Q4 qu = a_out[u];
				if (!e.ok || snap || dt <= 0.0f) { e = Elbow{}; e.prevU = e.meanU = qu; e.ok = true; continue; }
				const Q4& qf = a_out[f];
				const V3f fz = Rotate(qf, { 0, 0, 1 });                       // (the forearm's line, in the upper arm's frame)
				const float bend = std::acos(std::clamp(fz.z, -1.0f, 1.0f));
				V3f axis{ -fz.y, fz.x, 0 };                                   // (Z x fz: turning about it bends further)
				const float al = std::sqrt(axis.x * axis.x + axis.y * axis.y);
				const V3f hingeX = Rotate(qf, { -1, 0, 0 });                  // (nearly straight: the forearm's own hinge)
				const float t = std::clamp(al / 0.2f, 0.0f, 1.0f);
				axis = al > 1e-5f ? V3f{ axis.x / al * t + hingeX.x * (1 - t), axis.y / al * t + hingeX.y * (1 - t), hingeX.z * (1 - t) } : hingeX;
				const float an = std::sqrt(Dot(axis, axis));
				axis = { axis.x / an, axis.y / an, axis.z / an };
				// its swing: how far forward of its own recent middle the upper arm hangs
				e.meanU = Slerp(e.meanU, qu, 1.0f - std::exp(-dt / 0.6f));
				const float swing = Dot(RotVec(Mul(Conj(e.meanU), qu)), axis);
				const float target = std::clamp(0.45f * swing, -0.35f, 0.35f);
				// the forearm's weight: the upper arm's turn rate about the hinge, its change pushing the elbow the other way
				const float wNow = Dot(RotVec(Mul(Conj(e.prevU), qu)), axis) / dt;
				const float wS = e.w + (wNow - e.w) * (1.0f - std::exp(-dt / 0.03f));
				const float acc = (wS - e.w) / dt;
				e.w = wS, e.prevU = qu;
				constexpr float k = 480.0f, c = 15.0f, g = 0.6f;
				e.vel += (-k * (e.off - target) - c * e.vel - g * acc) * dt;
				e.off += e.vel * dt;
				// never past straight, never folded shut (and where the clip itself is already past either, no further)
				const float lo = std::max(-0.45f, std::min(0.0f, 0.22f - bend)), hi = std::min(0.6f, std::max(0.0f, 2.6f - bend));
				if (e.off < lo) e.off = lo, e.vel = std::max(0.0f, e.vel);
				if (e.off > hi) e.off = hi, e.vel = std::min(0.0f, e.vel);
				const float h = e.off * 0.5f, sh = std::sin(h);
				a_out[f] = Mul(Q4{ std::cos(h), axis.x * sh, axis.y * sh, axis.z * sh }, qf);
			}
			// the wrists: the forearm's turn (carried by its upper arm), seen from the hand, its changes swinging the hand
			for (int side = 0; side < 2; ++side) {
				const int u = S.upperArm[side], f = S.forearm[side], h = S.hand[side];
				if (u < 0 || f < 0 || h < 0) continue;
				Wrist& e = wrist[side];
				const Q4 qa = Mul(a_out[u], a_out[f]);  // (the forearm against the shoulder)
				if (!e.ok || snap || dt <= 0.0f) { e = Wrist{}; e.prev = qa; e.ok = true; continue; }
				const V3f wb = RotVec(Mul(Conj(e.prev), qa));  // (its turn this frame, in its own frame)
				const V3f wh = Rotate(Conj(a_out[h]), { wb.x / dt, wb.y / dt, wb.z / dt });
				const float sm = 1.0f - std::exp(-dt / 0.03f);
				const V3f wS{ e.w.x + (wh.x - e.w.x) * sm, e.w.y + (wh.y - e.w.y) * sm, e.w.z + (wh.z - e.w.z) * sm };
				const float ax = (wS.x - e.w.x) / dt, ay = (wS.y - e.w.y) / dt;
				e.w = wS, e.prev = qa;
				constexpr float k = 200.0f, c = 7.0f;
				e.vx += (-k * e.x - c * e.vx - 1.1f * ax) * dt, e.x += e.vx * dt;
				e.vy += (-k * e.y - c * e.vy - 0.5f * ay) * dt, e.y += e.vy * dt;
				if (std::abs(e.x) > 0.7f) e.x = std::copysign(0.7f, e.x), e.vx = 0;
				if (std::abs(e.y) > 0.35f) e.y = std::copysign(0.35f, e.y), e.vy = 0;
				const float ang = std::sqrt(e.x * e.x + e.y * e.y);
				if (ang > 1e-5f) {
					const float sn = std::sin(ang * 0.5f) / ang;
					a_out[h] = Mul(a_out[h], { std::cos(ang * 0.5f), e.x * sn, e.y * sn, 0 });
				}
			}
			a_com = ComOf(S, clip, clipT);
			if (w < 1.0f && prevClip >= 0) a_com = Lerp(ComOf(S, prevClip, prevT), a_com, w);
		}
	};
}
