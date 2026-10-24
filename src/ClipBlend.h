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
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace zclip
{
	enum ClipId { kRun, kAttack, kPunch, kScream, kHit, kHit2, kStandUp, kClips };
	inline constexpr const char* kClipFile[kClips] = { "run", "attack", "punch", "scream", "hit", "hit2", "standup" };
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
		bool                     ok = false;
		std::vector<float>       upper;  // per bone: how much of a layered swing it takes (legs 0, arms 1)
		std::vector<float>       tau;    // per bone: its follow-through lag in seconds (0 none)
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
	inline float ClipRate(int a_clip)
	{
		return a_clip == kAttack ? 1.2f : a_clip == kPunch ? 1.15f : a_clip == kHit || a_clip == kHit2 ? 1.15f : a_clip == kStandUp ? 1.15f : 1.0f;
	}
	inline bool Layered(int a_clip) { return a_clip == kAttack || a_clip == kPunch || a_clip == kHit || a_clip == kHit2; }

	struct Player
	{
		int             clip = -1, prevClip = -1;
		float           clipT = 0, prevT = 0, rate = 1, prevRate = 1;
		float           blend = 1, fade = 0.2f;
		float           runT = 0;            // the stride, always going
		float           still = 1;           // 1 standing (a swing takes the whole body), 0 running (only the top)
		std::vector<Q4> held;                // the follow-through: where each bone actually is
		bool            heldOk = false;

		bool OneShotOn(const ClipSet& S) const { return clip >= 0 && clip != kRun && clipT < S.c[clip].Seconds(); }
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
		// one clip's pose for one bone, the run under a layered swing
		Q4 PoseOf(const ClipSet& S, int c, float t, std::size_t i) const
		{
			const Q4 run = Sample(S.c[kRun], runT, true, i);
			if (c < 0 || c == kRun) return run;
			const Q4 act = Sample(S.c[c], t, false, i);
			if (!Layered(c)) return act;
			const float m = S.upper[i] + (1.0f - S.upper[i]) * still;
			return m >= 0.999f ? act : m <= 0.001f ? run : Slerp(run, act, m);
		}
		V3f ComOf(const ClipSet& S, int c, float t) const
		{
			const V3f run = SampleCom(S.c[kRun], runT, true);
			if (c < 0 || c == kRun) return run;
			const V3f act = SampleCom(S.c[c], t, false);
			return Layered(c) ? Lerp(run, act, still) : act;
		}
		// advance dt and pose: a_out per clip bone (against its parent), a_com the COM's place
		void Tick(const ClipSet& S, float dt, float a_runRate, std::vector<Q4>& a_out, V3f& a_com)
		{
			dt = std::clamp(dt, 0.0f, 0.25f);
			runT += dt * a_runRate;
			if (clip == kRun) rate = a_runRate;
			if (prevClip == kRun) prevRate = a_runRate;
			clipT += dt * rate, prevT += dt * prevRate;
			blend = std::min(1.0f, blend + dt / std::max(0.01f, fade));
			// standing or running: eased, so a swing begun mid-stride settles into the legs
			const float wantStill = std::clamp(1.0f - a_runRate / 0.35f, 0.0f, 1.0f);
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
			a_com = ComOf(S, clip, clipT);
			if (w < 1.0f && prevClip >= 0) a_com = Lerp(ComOf(S, prevClip, prevT), a_com, w);
		}
	};
}
