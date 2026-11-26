#include "CurseOfAlduin.h"
#include "WorkshopPicks.h"

// (Noah, 2026-11-26: "make the volume on all of the weapon sound effects 20% ... I assigned all the sound effects I want to use
// for the weapons. Can you finalize these?" - then "all my picks at once" for the in-game test) what he chose in the workshop doc,
// played in the game: each ability's own sound (Skyrim's, the DLCs', Apocalypse's, Mysticism's or ours) at 20%, Skyrim's own
// weapon sounds at 20% for every weapon, his hit feel by weight, a power attack's kill thrown, and the trials' new sounds.
namespace coa::Picks
{
	namespace
	{
		RE::BGSSoundDescriptorForm* Form(const Snd& a_s)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh && a_s.plugin ? dh->LookupForm<RE::BGSSoundDescriptorForm>(a_s.id, a_s.plugin) : nullptr;
		}
		// one sound where it happens: carried on you, softer the further off it went off (as Gun::BlastSound)
		void PlayOne(const Snd& a_s, const RE::NiPoint3& a_at, float a_vol)
		{
			auto go = [s = a_s, at = a_at, a_vol]() {
				auto* p = RE::PlayerCharacter::GetSingleton();
				if (!p) return;
				const float d = p->GetPosition().GetDistance(at);
				const float v = std::clamp(1.0f - (d - 350.0f) / 2400.0f, 0.25f, 1.0f) * s.gain * a_vol;
				if (s.ours >= 0) {
					Audio::Mine(static_cast<gen::Sound>(s.ours), at, std::min(1.0f, v));
				} else if (auto* f = Form(s)) {
					Audio::Game(f->GetFormID(), p, std::min(1.0f, v));
				}
			};
			if (a_s.delay > 0.01f) Tasks::Later(a_s.delay, go);
			else go();
		}
		const CardSnd* Find(std::string_view a_card, std::string_view a_slot)
		{
			for (const auto& c : kCardSnd) if (a_card == c.card && a_slot == c.slot) return &c;
			return nullptr;
		}
		const char* CardOf(std::string_view a_owner)
		{
			for (const auto& [o, c] : kOwnerCard) if (a_owner == o) return c;
			return nullptr;
		}
		// a moment's sound once per beat (an ability's parts, several dead struck at once, all call it in the same moment)
		bool Fresh(std::string_view a_key)
		{
			static std::unordered_map<std::string, std::chrono::steady_clock::time_point> last;
			const auto now = std::chrono::steady_clock::now();
			auto& t = last[std::string(a_key)];
			if (now - t < std::chrono::milliseconds(110)) return false;
			t = now;
			return true;
		}
		int  g_crowd = 0;
		float g_crowdT = 0;
	}

	void OnDataLoaded()
	{
		// (Noah: "Every weapon in the game") Skyrim's own draw, sheathe, swing and impact sounds at 20% - about 14 dB down
		const auto down = static_cast<std::uint16_t>(std::lround(-20.0 * std::log10(kWeaponVol) * 100.0));
		int n = 0;
		auto* dh = RE::TESDataHandler::GetSingleton();
		for (const auto& [plugin, id] : kQuietSndr) {
			auto* f = dh ? dh->LookupForm<RE::BGSSoundDescriptorForm>(id, plugin) : nullptr;
			auto* sd = f ? skyrim_cast<RE::BGSStandardSoundDef*>(f->soundDescriptor) : nullptr;
			if (!sd) continue;
			sd->soundCharacteristics.staticAttenuation = static_cast<std::uint16_t>(std::min<int>(10000, sd->soundCharacteristics.staticAttenuation + down));
			++n;
		}
		logger::info("picks: {} of Skyrim's weapon sounds {:.1f} dB down (20%); {} ability sounds, {} trial sounds of Noah's", n, down / 100.0, std::size(kCardSnd), std::size(kTrialSnd));
	}

	bool Card(std::string_view a_card, std::string_view a_slot, const RE::NiPoint3& a_at)
	{
		const auto* c = Find(a_card, a_slot);
		if (!c) return false;
		if (Fresh(std::string(a_card) + ":" + std::string(a_slot))) {
			for (int i = 0; i < c->n; ++i) PlayOne(c->s[i], a_at, 1.0f);
		}
		return true;
	}
	bool Owner(std::string_view a_owner, std::string_view a_slot, const RE::NiPoint3& a_at)
	{
		const char* card = CardOf(a_owner);
		return card && Card(card, a_slot, a_at);
	}
	bool OwnerHasSound(std::string_view a_owner)
	{
		const char* card = CardOf(a_owner);
		return card && Find(card, "impact");
	}

	bool Trial(std::string_view a_event, const RE::NiPoint3* a_at)
	{
		for (const auto& c : kTrialSnd) {
			if (a_event != c.card) continue;
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (!p) return true;
			for (int i = 0; i < c.n; ++i) PlayOne(c.s[i], a_at ? *a_at : p->GetPosition(), 1.0f);
			return true;
		}
		return false;
	}
	bool HasTrial(std::string_view a_event)
	{
		for (const auto& c : kTrialSnd) if (a_event == c.card) return true;
		return false;
	}
	void TrialLoop(bool a_on)
	{
		static RE::BSSoundHandle h;
		static bool on = false;
		const CardSnd* c = nullptr;
		for (const auto& t : kTrialSnd) if (std::string_view("inside") == t.card) c = &t;
		if (!c) return;
		if (a_on && !on) {
			auto* f = Form(c->s[0]);
			auto* am = RE::BSAudioManager::GetSingleton();
			auto* p = RE::PlayerCharacter::GetSingleton();
			if (f && am && p && am->GetSoundHandle(h, static_cast<RE::BSISoundDescriptor*>(f))) {
				h.SetPosition(p->GetPosition());
				if (auto* node = p->Get3D()) h.SetObjectToFollow(node);
				h.SetVolume(0.8f);
				h.Play();
				on = true;
			}
		} else if (!a_on && on) {
			h.FadeOutAndRelease(800);
			on = false;
		}
	}

	// (the workshop's Hit feel) a melee blow on one of the dead: the picture holds for a beat by the weapon's weight - longer on a
	// power attack, which also shakes the view; a power attack that kills throws the body
	void OnMeleeHit(RE::Actor* a_target, const RE::TESForm* a_source, bool a_heavy)
	{
		auto* w = a_source ? a_source->As<RE::TESObjectWEAP>() : nullptr;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!w || !p || !a_target || w->IsBow() || w->IsCrossbow() || w->IsStaff()) return;
		int k = 1;
		switch (w->GetWeaponType()) {
		case RE::WEAPON_TYPE::kHandToHandMelee:
		case RE::WEAPON_TYPE::kOneHandDagger: k = 0; break;
		case RE::WEAPON_TYPE::kTwoHandSword:
		case RE::WEAPON_TYPE::kTwoHandAxe: k = 2; break;
		default: k = 1; break;
		}
		static auto last = std::chrono::steady_clock::time_point{};
		const auto  now = std::chrono::steady_clock::now();
		if (now - last > std::chrono::milliseconds(140)) {
			last = now;
			if (const float ms = a_heavy ? kStopH[k] : kStopL[k]; ms > 0.5f) Director::SlowTime(ms / 1000.0f, 0.05f);
			if (a_heavy && kShakeH[k] > 0.5f && Settings::ScreenShake()) RE::ShakeCamera(kShakeH[k] / 100.0f * 0.6f, p->GetPosition(), 0.25f);
		}
		// (The dead: "Heavies throw them") a power attack's killing blow hurls the body back - after the night's own damage for the
		// blow has landed (next on the main thread), before its death is dealt
		if (kHeavyThrows && a_heavy && Director::IsRoundUndead(a_target)) {
			Tasks::Add([h = a_target->GetHandle()] {
				auto a = h.get();
				auto* pl = RE::PlayerCharacter::GetSingleton();
				if (!a || !pl || a->IsDeleted()) return;
				if (a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) > 0.0f && !a->IsDead()) return;
				if (auto* proc = pl->GetActorRuntimeData().currentProcess) {
					RE::NiPoint3 away = a->GetPosition() - pl->GetPosition();
					away.z = 0;
					if (away.Length() < 1.0f) away = { 0, 1, 0 };
					away.Unitize();
					proc->KnockExplosion(a.get(), a->GetPosition() - away * 60.0f + RE::NiPoint3{ 0, 0, 55.0f }, 5.0f);   // (from waist height behind, as every kill's fall - Director - but three times as hard)
				}
			});
		}
	}

	int Crowd() { return g_crowd; }
	void Tick(float a_delta)
	{
		if ((g_crowdT -= a_delta) > 0) return;
		g_crowdT = 0.5f;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p) return;
		int n = 0;
		const auto pp = p->GetPosition();
		for (auto h : Director::AliveHandles()) if (auto a = h.get(); a && !a->IsDead() && a->GetPosition().GetDistance(pp) < 3000.0f) ++n;
		g_crowd = n;
	}
}
