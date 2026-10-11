#include "BO3Z.h"

// Undying Nights audio (Spec 12): every sound the mod plays goes through here.
//  - one-shots: 2D (UI, the player's own casts) or 3D (at a reference, following it, or at a point), optionally
//    delayed (a bolt's impact when it lands);
//  - keyed loops (charge hums, the ward circle) that stop on request, and are stopped when their owner dies,
//    unloads or is disabled;
//  - rate limits: the same sound at most every 60 ms, at most 10 world one-shots in any 250 ms, each undead
//    voice event limited per body and idle chatter capped across the horde, so a 40-zombie round stays clear;
//  - voices: each kind of undead (and each champion and the boss) has its own idle, attack, hurt and death set;
//    dead and despawned bodies are never voiced;
//  - music through Skyrim's own music system (the music slider, ducking, fade-outs): the main menu track
//    (sMainMenuMusic), Grimmvar's fight and a city freed (our MUSC records).
// Sound is always local: in co-op each game plays the cues for what it sees (the host's night state arrives
// over the network, the sounds don't).
namespace bo3z::Audio
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		struct Loop
		{
			RE::BSSoundHandle   handle;
			RE::ObjectRefHandle owner;
			bool                owned{ false };
		};
		std::unordered_map<std::uint64_t, Loop>          g_loops;
		std::array<Clock::time_point, gen::kSounds.size()> g_last{};
		std::deque<Clock::time_point>                    g_world;  // recent world one-shots (the voice budget)
		struct Pending
		{
			gen::Sound   s;
			RE::NiPoint3 at;
			float        delay, vol;
		};
		std::vector<Pending> g_pending;
		struct VoiceState
		{
			Clock::time_point idle{}, attack{}, hurt{};
			bool              attacking{ false }, died{ false };
		};
		std::unordered_map<RE::FormID, VoiceState> g_voice;
		std::deque<Clock::time_point>              g_idles;  // horde-wide idle chatter

		RE::BGSSoundDescriptorForm* Desc(gen::Sound a_s)
		{
			static std::array<RE::BGSSoundDescriptorForm*, gen::kSounds.size()> cache{};
			static std::array<bool, gen::kSounds.size()>                       tried{};
			const auto i = static_cast<std::size_t>(a_s);
			if (i >= cache.size()) {
				return nullptr;
			}
			if (!tried[i]) {
				tried[i] = true;
				auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(gen::kSounds[i].localId, gen::kSounds[i].plugin);
				cache[i] = f ? f->As<RE::BGSSoundDescriptorForm>() : nullptr;
				if (!cache[i]) {
					logger::warn("audio: {} missing", gen::kSounds[i].editorId);
				}
			}
			return cache[i];
		}

		bool Allow(gen::Sound a_s, bool a_world)
		{
			const auto now = Clock::now();
			auto&      last = g_last[static_cast<std::size_t>(a_s)];
			if (now - last < std::chrono::milliseconds(60)) {
				return false;  // the same sound twice in one beat (two events for one hit)
			}
			if (a_world) {
				while (!g_world.empty() && now - g_world.front() > std::chrono::milliseconds(250)) {
					g_world.pop_front();
				}
				if (g_world.size() >= 10) {
					return false;  // the horde's budget is spent this instant
				}
				g_world.push_back(now);
			}
			last = now;
			return true;
		}

		bool Start(RE::BSSoundHandle& a_h, gen::Sound a_s, RE::TESObjectREFR* a_follow, const RE::NiPoint3* a_at, float a_vol)
		{
			auto* d = Desc(a_s);
			if (!d || !RE::BSAudioManager::GetSingleton()->GetSoundHandle(a_h, static_cast<RE::BSISoundDescriptor*>(d))) {
				return false;
			}
			if (a_follow) {
				a_h.SetPosition(a_follow->GetPosition());
				if (auto* node = a_follow->Get3D()) {
					a_h.SetObjectToFollow(node);
				}
			} else if (a_at) {
				a_h.SetPosition(*a_at);
			}
			if (a_vol < 0.999f) {
				a_h.SetVolume(std::clamp(a_vol, 0.0f, 1.0f));
			}
			return a_h.Play();
		}

		bool Alive(RE::TESObjectREFR* a_r)
		{
			if (!a_r || a_r->IsDeleted() || a_r->IsDisabled() || !a_r->Is3DLoaded()) {
				return false;
			}
			auto* a = a_r->As<RE::Actor>();
			return !a || !a->IsDead();
		}
	}

	void Ui(gen::Sound a_s, float a_vol)
	{
		if (!Allow(a_s, false)) {
			return;
		}
		RE::BSSoundHandle h;
		Start(h, a_s, nullptr, nullptr, a_vol);
	}

	void At(gen::Sound a_s, RE::TESObjectREFR* a_ref, float a_vol)
	{
		if (!a_ref) {
			return Ui(a_s, a_vol);
		}
		if (!Allow(a_s, true)) {
			return;
		}
		RE::BSSoundHandle h;
		Start(h, a_s, a_ref, nullptr, a_vol);
	}

	void AtPoint(gen::Sound a_s, const RE::NiPoint3& a_at, float a_delay, float a_vol)
	{
		if (a_delay > 0.01f) {
			if (g_pending.size() < 48) {
				g_pending.push_back({ a_s, a_at, a_delay, a_vol });
			}
			return;
		}
		if (!Allow(a_s, true)) {
			return;
		}
		RE::BSSoundHandle h;
		Start(h, a_s, nullptr, &a_at, a_vol);
	}

	void LoopStart(std::uint64_t a_key, gen::Sound a_s, RE::TESObjectREFR* a_owner)
	{
		if (auto it = g_loops.find(a_key); it != g_loops.end()) {
			if (it->second.handle.IsPlaying()) {
				return;  // already humming: a loop never stacks
			}
			g_loops.erase(it);
		}
		Loop l;
		if (Start(l.handle, a_s, a_owner, nullptr, 1.0f)) {
			l.owned = a_owner != nullptr;
			if (a_owner) {
				l.owner = a_owner->CreateRefHandle();
			}
			g_loops[a_key] = std::move(l);
		}
	}

	void LoopStop(std::uint64_t a_key, int a_fadeMs)
	{
		if (auto it = g_loops.find(a_key); it != g_loops.end()) {
			if (a_fadeMs > 0) {
				it->second.handle.FadeOutAndRelease(static_cast<std::uint16_t>(a_fadeMs));
			} else {
				it->second.handle.Stop();
			}
			g_loops.erase(it);
		}
	}

	bool Looping(std::uint64_t a_key) { return g_loops.contains(a_key); }

	// ---- the undead's voices
	namespace
	{
		struct VoiceSet { gen::Sound idle, attack, hurt, death; };
		using S = gen::Sound;
		constexpr VoiceSet kVoices[] = {
			{ S::vo_shambler_idle, S::vo_shambler_attack, S::vo_shambler_hurt, S::vo_shambler_death },
			{ S::vo_runner_idle, S::vo_runner_attack, S::vo_runner_hurt, S::vo_runner_death },
			{ S::vo_wailer_idle, S::vo_wailer_attack, S::vo_wailer_hurt, S::vo_wailer_death },
			{ S::vo_bloater_idle, S::vo_bloater_attack, S::vo_bloater_hurt, S::vo_bloater_death },
			{ S::vo_stalker_idle, S::vo_stalker_attack, S::vo_stalker_hurt, S::vo_stalker_death },
			{ S::vo_reaper_idle, S::vo_reaper_attack, S::vo_reaper_hurt, S::vo_reaper_death },
			{ S::vo_necro_idle, S::vo_necro_attack, S::vo_necro_hurt, S::vo_necro_death },
			{ S::vo_knight_idle, S::vo_knight_attack, S::vo_knight_hurt, S::vo_knight_death },
			{ S::vo_king_idle, S::vo_king_attack, S::vo_king_hurt, S::vo_king_death },
			{ S::vo_lich_idle, S::vo_lich_attack, S::vo_lich_hurt, S::vo_lich_death },
			{ S::boss_growl, S::boss_claw_wind, S::vo_bloater_hurt, S::boss_death },
		};
		int VoiceOf(RE::Actor* a_actor)
		{
			const int row = Director::EnemyRow(a_actor);
			if (row < 0) {
				return -1;
			}
			const std::string_view id = gen::kEnemies[row].id;
			if (id == "zombie") return 0;
			if (id == "runner") return 1;
			if (id == "wailer") return 2;
			if (id == "bloater") return 3;
			if (id == "shadow") return 4;
			if (id == "reaper") return 5;
			if (id == "necro" || id == "skeleton") return 6;
			if (id == "knight") return 7;
			if (id == "king") return 8;
			if (id == "lich") return 9;
			if (id == "boss") return 10;
			return 0;
		}
	}

	void Voice(RE::Actor* a_actor, VoiceEvent a_ev)
	{
		if (!a_actor || a_actor->IsDisabled() || a_actor->IsDeleted()) {
			return;
		}
		const int v = VoiceOf(a_actor);
		if (v < 0) {
			return;
		}
		auto&      st = g_voice[a_actor->GetFormID()];
		const auto now = Clock::now();
		const auto& set = kVoices[v];
		switch (a_ev) {
		case VoiceEvent::kDeath:
			if (st.died) {
				return;
			}
			st.died = true;
			At(set.death, a_actor);
			return;
		case VoiceEvent::kHurt:
			if (a_actor->IsDead() || now - st.hurt < 1400ms) {
				return;
			}
			st.hurt = now;
			At(set.hurt, a_actor, 0.85f);
			return;
		case VoiceEvent::kAttack:
			if (a_actor->IsDead() || now - st.attack < 1100ms) {
				return;
			}
			st.attack = now;
			At(set.attack, a_actor);
			return;
		case VoiceEvent::kIdle:
			if (a_actor->IsDead()) {
				return;
			}
			while (!g_idles.empty() && now - g_idles.front() > 2s) {
				g_idles.pop_front();
			}
			if (g_idles.size() >= 3) {
				return;  // three moans in two seconds is a horde; more is noise
			}
			g_idles.push_back(now);
			st.idle = now;
			At(set.idle, a_actor, 0.8f);
			return;
		}
	}

	// ---- per frame: delayed impacts, loops whose owner is gone, idle chatter and attack swings of the horde
	void Tick(float a_delta)
	{
		for (auto& p : g_pending) {
			p.delay -= a_delta;
		}
		std::vector<Pending> due;
		std::erase_if(g_pending, [&](const Pending& p) {
			if (p.delay <= 0) {
				due.push_back(p);
				return true;
			}
			return false;
		});
		for (const auto& p : due) {
			AtPoint(p.s, p.at, 0.0f, p.vol);
		}
		for (auto it = g_loops.begin(); it != g_loops.end();) {
			auto& l = it->second;
			const bool gone = l.owned && !Alive(l.owner.get().get());
			if (gone) {
				l.handle.FadeOutAndRelease(250);
			}
			it = gone ? g_loops.erase(it) : std::next(it);
		}
		static float scan = 0;
		if ((scan -= a_delta) > 0) {
			return;
		}
		scan = 0.15f;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto pp = player->GetPosition();
		static std::mt19937 rng{ std::random_device{}() };
		for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
			auto a = h.get();
			if (!a || a->IsDead() || a->IsDisabled() || !Director::IsRoundUndead(a.get())) {
				continue;
			}
			const float dist = a->GetPosition().GetDistance(pp);
			if (dist > 2600.0f) {
				continue;
			}
			auto& st = g_voice[a->GetFormID()];
			// a swing begins: its voice (the claws' whoosh is the weapon's own sound)
			const bool attacking = a->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone;
			if (attacking && !st.attacking) {
				Voice(a.get(), VoiceEvent::kAttack);
			}
			st.attacking = attacking;
			// idle moans, now and then, more often when near
			const auto since = std::chrono::duration<float>(Clock::now() - st.idle).count();
			const float every = dist < 900.0f ? 5.0f : 9.0f;
			if (since > every && std::uniform_real_distribution<float>(0, 1)(rng) < 0.06f) {
				Voice(a.get(), VoiceEvent::kIdle);
			}
		}
		static float sweep = 0;
		if ((sweep += 0.15f) > 10.0f) {  // forget the gone
			sweep = 0;
			std::erase_if(g_voice, [](const auto& e) {
				auto* f = RE::TESForm::LookupByID<RE::Actor>(e.first);
				return !f || f->IsDeleted() || (f->IsDead() && e.second.died);
			});
		}
	}

	void StopAll()
	{
		for (auto& [k, l] : g_loops) {
			l.handle.Stop();
		}
		g_loops.clear();
		g_pending.clear();
		g_voice.clear();
	}

	// ---- music
	namespace
	{
		RE::BGSMusicType* Music(std::string_view a_key)
		{
			const auto  id = a_key == "boss" ? gen::kMusicBoss : gen::kMusicFreed;
			auto*       f = RE::TESDataHandler::GetSingleton()->LookupForm(id, gen::kPlugin);
			return f ? f->As<RE::BGSMusicType>() : nullptr;
		}
		bool g_bossMusic = false;
	}

	void BossMusic(bool a_on)
	{
		if (a_on == g_bossMusic) {
			return;
		}
		if (auto* m = Music("boss")) {
			if (a_on) {
				m->DoPlay();
			} else {
				m->DoFinish(true);
			}
			g_bossMusic = a_on;
		}
	}

	void FreedMusic()
	{
		if (auto* m = Music("freed")) {
			m->DoPlay();
		}
	}

	// The main menu plays our track instead of Skyrim's theme: the game reads this setting each time the menu
	// opens, so it is set as early as the settings exist (and again whenever we see the menu).
	void SetMenuMusic()
	{
		static const char kPath[] = "\\Data\\Music\\UndyingNights\\mus_menu.wav";
		auto* ini = RE::INISettingCollection::GetSingleton();
		auto* s = ini ? ini->GetSetting("sMainMenuMusic:General") : nullptr;
		if (s && s->GetType() == RE::Setting::Type::kString && (!s->data.s || std::strcmp(s->data.s, kPath) != 0)) {
			s->data.s = const_cast<char*>(kPath);
			logger::info("audio: main menu music set to {}", kPath);
		}
	}
}
