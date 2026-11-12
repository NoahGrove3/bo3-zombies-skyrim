// (Noah: "I really want to go through and organize all the tabs and all of the sliders in each tab in the race menu, so
// it's a lot more organized") RaceMenu's character creator, regrouped by body area into eight tabs - RACE, HEAD, FACE,
// EYES & BROWS, HAIR, BODY, SKIN & PAINT, EXPRESSIONS - with the body sliders in sections, and the duplicates and the
// extras he chose to hide taken out ("hide them and hide the himbo morphs tab"). Nothing in RaceMenu's files changes:
// its lists are rearranged in the open menu, again whenever it rebuilds them (a race or sex change, a preset).
#include "BO3Z.h"

namespace bo3z::RaceTidy
{
	namespace
	{
		// RaceMenu's own category bits (its tabs filter on them)
		constexpr std::uint32_t kRace = 2, kBody = 4, kHead = 8, kFace = 16, kEyes = 32, kBrow = 64, kMouth = 128, kHair = 256,
								kExtra = 512, kExpr = 1024, kColors = 8192, kMakeup = 16384, kBodyPaint = 32768, kHandPaint = 65536,
								kFootPaint = 131072;
		constexpr std::uint32_t kSkin = kColors | kMakeup | kBodyPaint | kHandPaint | kFootPaint;   // (one tab for all of it)
		constexpr std::uint32_t kHidden = 1u << 24;   // (no tab has it)

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}
		bool Has(const std::string& a_low, std::initializer_list<const char*> a_any)
		{
			for (auto* w : a_any) {
				if (a_low.find(w) != std::string::npos) return true;
			}
			return false;
		}

		// where one slider goes: its tab, and its place in that tab (lower first). -1: left as it is
		struct Place
		{
			std::uint32_t flag;
			int           order;
		};
		std::optional<Place> Route(const std::string& a_text, const std::string& a_callback, std::uint32_t a_flag, int a_type)
		{
			const std::string t = Lower(a_text), cb = Lower(a_callback);
			if (a_type == 1) return Place{ kRace, 0 };   // (the races themselves)
			if (a_type == 3) return std::nullopt;        // (paint textures: RaceMenu's own grids, by their own tabs' bits)
			// the extras hidden (his answer: nipples, growth, pregnant, the Hayden feminine shapes; and the two left-over helpers)
			if (Has(t, { "nipple", "areola", "growth", "pregnan", "hayden", "legacy pecs", "belly button match" })) return Place{ kHidden, 0 };
			// RACE
			if (t == "sex") return Place{ kRace, 100 };
			if (cb == "changeheadpreset") return Place{ kRace, 101 };
			// HEAD
			if (cb == "changeheadsize" || t == "$head") return Place{ kHead, 0 };
			if (t == "complexion") return Place{ kHead, 10 };
			if (t == "scars") return Place{ kHead, 20 };
			if (Has(t, { "scars0" })) return Place{ kHead, 21 };
			if (Has(t, { "ears" })) return Place{ kHead, 30 };
			// FACE (nose, cheeks, jaw, chin, mouth)
			if (Has(t, { "nose type" })) return Place{ kFace, 0 };
			if (Has(t, { "nose height" })) return Place{ kFace, 1 };
			if (Has(t, { "nose length" })) return Place{ kFace, 2 };
			if (Has(t, { "cheekbone" })) return Place{ kFace, 10 };
			if (Has(t, { "jaw" })) return Place{ kFace, 20 };
			if (Has(t, { "chin width", "chin length", "chin forward" })) return Place{ kFace, 30 };
			if (Has(t, { "mouth shape" })) return Place{ kFace, 40 };
			if (Has(t, { "mouth" })) return Place{ kFace, 41 };
			// EYES & BROWS
			if (Has(t, { "eye shape" })) return Place{ kEyes, 0 };
			if (Has(t, { "eye color" })) return Place{ kEyes, 1 };
			if (Has(t, { "eye height", "eye width", "eye depth" })) return Place{ kEyes, 2 };
			if (Has(t, { "eyeshape", "_aes_" })) return Place{ kEyes, 3 };
			if (Has(t, { "brow" })) return Place{ kEyes, 10 };
			// HAIR
			if (t == "hair" || t == "facial hair") return Place{ kHair, t == "hair" ? 0 : 1 };
			if (t == "hair color") return Place{ kHair, 2 };
			// SKIN & PAINT (every color, the dirt and the war paint)
			if (t == "skin tone") return Place{ kSkin, 0 };
			if (Has(t, { "dirt" })) return Place{ kSkin, t == "dirt" ? 50 : 51 };
			if (Has(t, { "war paint" })) return Place{ kSkin, t == "war paint" ? 60 : 61 };
			if (cb == "changetintingmask" || (a_flag & kColors)) return Place{ kSkin, 10 };
			// BODY: height and weight, then the body mods' sliders by section
			if (cb == "changeheight" || t == "$height") return Place{ kBody, 0 };
			if (t == "weight") return Place{ kBody, 1 };
			if (cb.rfind("rsm_bodymorph", 0) == 0 || cb.find("morph") != std::string::npos || a_flag == 0) {
				if (Has(t, { "body -", "overall", "preset" })) return Place{ kBody, 100 };
				if (Has(t, { "torso - back", "back" })) return Place{ kBody, 400 };
				if (Has(t, { "torso", "waist", "hip", "rib", "sternum", "clavicle", "chest" })) return Place{ kBody, 200 };
				if (Has(t, { "pec", "breast" })) return Place{ kBody, 300 };
				if (Has(t, { "belly", "abs" })) return Place{ kBody, 350 };
				if (Has(t, { "trap", "shoulder", "delt", "bicep", "tricep", "forearm", "arm", "hand", "neck" })) return Place{ kBody, 500 };
				if (Has(t, { "butt", "glute" })) return Place{ kBody, 600 };
				if (Has(t, { "leg", "femur", "thigh", "knee", "calf", "calve", "shin", "ankle", "feet", "foot" })) return Place{ kBody, 700 };
				return Place{ kBody, 900 };
			}
			// what's left keeps its own tab, if that tab is still shown
			if (a_flag & (kRace | kHead | kFace | kEyes | kHair | kBody | kExpr)) return std::nullopt;
			if (a_flag & kBrow) return Place{ kEyes, 10 };
			if (a_flag & kMouth) return Place{ kFace, 41 };
			return std::nullopt;
		}

		// the tabs: what each becomes (an empty name: hidden)
		struct Tab
		{
			const char*   name;
			std::uint32_t flag;
			int           priority;
		};
		std::optional<Tab> Retab(const std::string& a_text, std::uint32_t a_flag)
		{
			const std::string t = Lower(a_text);
			if (t == "race") return Tab{ "RACE", kRace, -975 };
			if (t == "head") return Tab{ "HEAD", kHead, -950 };
			if (t == "face") return Tab{ "FACE", kFace, -925 };
			if (t == "eyes") return Tab{ "EYES & BROWS", kEyes, -900 };
			if (t == "hair") return Tab{ "HAIR", kHair, -875 };
			if (t == "body") return Tab{ "BODY", kBody, -850 };
			if (t == "$colors" || t == "colors") return Tab{ "SKIN & PAINT", kSkin, -825 };
			if (t == "$expressions" || t == "expressions") return Tab{ "", kExpr, -800 };   // (kept: its own name)
			(void)a_flag;
			return Tab{ nullptr, 0, 0 };   // all else hidden: ALL, BROW, MOUTH, EXTRA, MAKEUP, the paints, BODY SCALES, the morph tabs
		}

		bool Num(const RE::GFxValue& a_o, const char* a_k, double& a_out)
		{
			RE::GFxValue v;
			if (!a_o.GetMember(a_k, &v) || !v.IsNumber()) return false;
			a_out = v.GetNumber();
			return true;
		}
		std::string Str(const RE::GFxValue& a_o, const char* a_k)
		{
			RE::GFxValue v;
			return a_o.GetMember(a_k, &v) && v.IsString() ? std::string(v.GetString()) : std::string();
		}

		// the categories: renamed, given their new bits, the rest taken out
		bool TidyTabs(RE::GFxMovieView* a_mv, RE::GFxValue& a_list)
		{
			RE::GFxValue entries;
			if (!a_list.GetMember("_entryList", &entries) || !entries.IsArray()) return false;
			const auto n = entries.GetArraySize();
			RE::GFxValue first;
			if (n && entries.GetElement(0, &first) && first.IsObject()) {
				RE::GFxValue mark;
				if (first.GetMember("caTidy", &mark) && mark.IsBool()) return false;   // (done, and not rebuilt since)
			}
			std::vector<std::pair<int, RE::GFxValue>> keep;
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue e;
				if (!entries.GetElement(i, &e) || !e.IsObject()) continue;
				double f = 0;
				Num(e, "flag", f);
				const auto tab = Retab(Str(e, "text"), static_cast<std::uint32_t>(f));
				if (!tab || !tab->name) continue;
				if (*tab->name) e.SetMember("text", RE::GFxValue(tab->name));
				e.SetMember("flag", RE::GFxValue(static_cast<double>(tab->flag)));
				e.SetMember("priority", RE::GFxValue(static_cast<double>(tab->priority)));
				e.SetMember("textFilter", RE::GFxValue());   // (a bit filter, not a name one)
				keep.emplace_back(tab->priority, e);
			}
			if (keep.empty()) return false;
			std::stable_sort(keep.begin(), keep.end(), [](auto& a, auto& b) { return a.first < b.first; });
			entries.SetArraySize(0);
			for (std::size_t i = 0; i < keep.size(); ++i) {
				keep[i].second.SetMember("itemIndex", RE::GFxValue(static_cast<double>(i)));
				keep[i].second.SetMember("caTidy", RE::GFxValue(true));
				entries.PushBack(keep[i].second);
			}
			(void)a_mv;
			a_list.Invoke("InvalidateData");
			return true;
		}

		// the sliders: each to its tab, in order within it
		int TidySliders(RE::GFxValue& a_list)
		{
			RE::GFxValue entries;
			if (!a_list.GetMember("_entryList", &entries) || !entries.IsArray()) return 0;
			const auto n = entries.GetArraySize();
			bool fresh = false;
			struct Row
			{
				RE::GFxValue e;
				std::uint32_t flag;
				int           order;
				std::uint32_t at;
			};
			std::vector<Row> rows;
			rows.reserve(n);
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue e;
				if (!entries.GetElement(i, &e) || !e.IsObject()) continue;
				RE::GFxValue mark;
				if (!(e.GetMember("caTidy", &mark) && mark.IsBool())) fresh = true;
				double f = 0, type = 0;
				Num(e, "filterFlag", f), Num(e, "type", type);
				double done = 0;
				if (Num(e, "caFlag", done)) f = done;   // (moved already: from where it first was)
				const auto to = Route(Str(e, "text"), Str(e, "callbackName"), static_cast<std::uint32_t>(f), static_cast<int>(type));
				rows.push_back({ e, to ? to->flag : static_cast<std::uint32_t>(f), to ? to->order : 1000, i });
				if (!mark.IsBool()) e.SetMember("caFlag", RE::GFxValue(f));
			}
			if (!fresh) return 0;
			std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.order != b.order ? a.order < b.order : a.at < b.at; });
			entries.SetArraySize(0);
			int moved = 0;
			for (std::size_t i = 0; i < rows.size(); ++i) {
				auto& r = rows[i];
				double was = 0;
				Num(r.e, "filterFlag", was);
				if (static_cast<std::uint32_t>(was) != r.flag) ++moved;
				r.e.SetMember("filterFlag", RE::GFxValue(static_cast<double>(r.flag)));
				r.e.SetMember("caTidy", RE::GFxValue(true));
				entries.PushBack(r.e);
			}
			a_list.Invoke("InvalidateData");
			return moved;
		}
	}

	void Tick()
	{
		static auto last = std::chrono::steady_clock::now();
		const auto  now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::milliseconds(150)) return;
		last = now;
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME)) return;
		auto menu = ui->GetMenu(RE::RaceSexMenu::MENU_NAME);
		auto* mv = menu ? menu->uiMovie.get() : nullptr;
		if (!mv) return;
		static constexpr const char* kPanel = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.racePanel";
		RE::GFxValue items;
		const bool haveItems = mv->GetVariable(&items, (std::string(kPanel) + ".itemList").c_str()) && items.IsObject();
		int tabs = 0;
		for (const char* p : { ".categoryButtons.categoryList", ".slidingCategoryList.categoryList" }) {
			RE::GFxValue cats;
			if (mv->GetVariable(&cats, (std::string(kPanel) + p).c_str()) && cats.IsObject() && TidyTabs(mv, cats)) ++tabs;
		}
		const int moved = haveItems ? TidySliders(items) : 0;
		if (tabs || moved) {
			static int told = 0;
			if (told++ < 12) logger::info("race menu: tidied ({} tab lists, {} sliders moved)", tabs, moved);
		}
	}
}
