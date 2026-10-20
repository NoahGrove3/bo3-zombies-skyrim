#include "BO3Z.h"

// (Noah: night atmosphere you can feel) the sky's lightning at a wave's start (brighter for a themed wave and the
// mini-boss's warning) with thunder rolling in after it, and whispers in the dark when you are close to death. The
// rest of the night's mood is drawn by the HUD from the night's progress (the sky reddening toward the last wave) and
// laid in the world by the Director (the ground fog thickening). Settings: Effects (reduce flashes, fog strength).
namespace bo3z::Atmos
{
	namespace
	{
		struct Thunder { float in, vol; };
		std::vector<Thunder> g_thunder;
		RE::BSSoundHandle    g_whisper;
	}

	void Lightning(float a_intensity)
	{
		a_intensity = std::clamp(a_intensity, 0.0f, 1.0f);
		{
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.lightningI = h.lightningT > 0 ? std::max(h.lightningI, a_intensity) : a_intensity;
			h.lightningT = 0.45f;
		}
		g_thunder.push_back({ 0.3f + 0.9f * (1.0f - a_intensity), 0.5f + 0.5f * a_intensity });
	}

	void Tick(float a_delta)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		for (auto& t : g_thunder) {
			if ((t.in -= a_delta) <= 0 && t.in > -50.0f) {
				Audio::Game(t.vol > 0.85f ? 0x0010F1F1 : 0x000E7522, p, t.vol);  // AMBWeatherThunderExtra / Distant
				t.in = -99.0f;
			}
		}
		std::erase_if(g_thunder, [](const Thunder& t) { return t.in <= -50.0f; });
		// close to death at night: whispers
		bool low = false;
		if (p && Director::Fighting() && !p->IsDead()) {
			auto* av = p->AsActorValueOwner();
			low = av->GetActorValue(RE::ActorValue::kHealth) < 0.25f * av->GetPermanentActorValue(RE::ActorValue::kHealth);
		}
		if (low && !g_whisper.IsValid()) {
			auto* d = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x00021705);  // QSTWordWallWhisper2DLPSD (a loop)
			if (auto* am = RE::BSAudioManager::GetSingleton(); d && am && am->GetSoundHandle(g_whisper, static_cast<RE::BSISoundDescriptor*>(d))) {
				g_whisper.SetVolume(0.45f);
				g_whisper.Play();
			}
		} else if (!low && g_whisper.IsValid()) {
			g_whisper.FadeOutAndRelease(800);
		}
	}
}
