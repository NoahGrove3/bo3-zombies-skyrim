#include "BO3Z.h"

// (Noah) the night's score and the player's records: kills, headshots, the best streak (kills without being hit),
// septims earned, style points, revives, waves and mini-bosses - tonight's as it goes, last night's when it ends, and a
// character's lifetime records (kept beside the log: CurseOfAlduin-records.txt, a line per character). Shown in the
// Party tab of the System menu with each friend's own (they send theirs: Net kStats). Style: small bonuses for playing
// well, a tag beside the septims as each is earned.
namespace bo3z::Stats
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		std::mutex g_lock;
		Line       g_now, g_last;
		Records    g_rec;
		std::string g_recName;  // whose records are loaded
		int        g_streak = 0, g_headRun = 0;
		std::deque<Clock::time_point> g_recentKills;
		struct Remote { std::string name; Line line; Clock::time_point heard; };
		std::unordered_map<std::uint64_t, Remote> g_remote;

		std::filesystem::path File()
		{
			auto dir = SKSE::log::log_directory();
			return dir ? *dir / "CurseOfAlduin-records.txt" : std::filesystem::path("CurseOfAlduin-records.txt");
		}
		std::string Who()
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			return p && p->GetName() ? std::string(p->GetName()) : std::string();
		}
		void LoadFor(const std::string& a_who)
		{
			g_rec = {};
			g_recName = a_who;
			std::ifstream in(File());
			std::string   line;
			while (std::getline(in, line)) {
				const auto bar = line.find('|');
				if (bar == std::string::npos || line.substr(0, bar) != a_who) continue;
				std::istringstream v(line.substr(bar + 1));
				v >> g_rec.bestNight >> g_rec.kills >> g_rec.heads >> g_rec.nights >> g_rec.minis >> g_rec.bestStreak >> g_rec.quests >> g_rec.wonders;
			}
		}
		void SaveRecords()
		{
			if (g_recName.empty()) return;
			std::vector<std::string> lines;
			{
				std::ifstream in(File());
				std::string   line;
				while (std::getline(in, line)) {
					if (line.rfind(g_recName + "|", 0) != 0 && !line.empty()) lines.push_back(line);
				}
			}
			lines.push_back(fmt::format("{}|{} {} {} {} {} {} {} {}", g_recName, g_rec.bestNight, g_rec.kills, g_rec.heads, g_rec.nights, g_rec.minis, g_rec.bestStreak, g_rec.quests, g_rec.wonders));
			std::ofstream out(File(), std::ios::trunc);
			for (auto& l : lines) out << l << "\n";
		}
		void Ensure()
		{
			const auto who = Who();
			if (!who.empty() && who != g_recName) LoadFor(who);
		}
		void Tag(std::string_view a_tag, int a_points)
		{
			std::scoped_lock l(Hud::Lock());
			auto& h = Hud::Edit();
			h.styleTag = fmt::format("+{} {}", a_points, a_tag);
			h.styleTime = 1.8f;
		}
	}

	void Style(std::string_view a_tag, int a_points)
	{
		{
			std::scoped_lock l(g_lock);
			static std::unordered_map<std::string, Clock::time_point> last;  // (one of each tag a second and a half)
			auto& t = last[std::string(a_tag.substr(0, a_tag.find(" x")))];
			if (Clock::now() - t < std::chrono::milliseconds(1500)) return;
			t = Clock::now();
			g_now.style += a_points;
		}
		Points::Add(a_points);
		Tag(a_tag, static_cast<int>(a_points * 0.8f));
		logger::info("style: {} +{}", a_tag, a_points);
	}

	void OnKill(RE::Actor* a_dying, bool a_gun, bool a_head, bool a_melee, bool a_bow, float a_dist)
	{
		std::unique_lock l(g_lock);
		Ensure();
		++g_now.kills, ++g_rec.kills;
		if (a_head) ++g_now.heads, ++g_rec.heads;
		g_now.best = std::max(g_now.best, ++g_streak);
		g_rec.bestStreak = std::max(g_rec.bestStreak, g_streak);
		// style
		const auto now = Clock::now();
		g_recentKills.push_back(now);
		while (!g_recentKills.empty() && now - g_recentKills.front() > std::chrono::seconds(2)) g_recentKills.pop_front();
		const int  multi = static_cast<int>(g_recentKills.size());
		g_headRun = a_head ? g_headRun + 1 : (a_gun ? 0 : g_headRun);
		const bool headStreak = a_head && g_headRun > 0 && g_headRun % 5 == 0;
		l.unlock();
		if (multi == 3) Style("MULTI-KILL", 50);
		else if (multi > 3) Style("MULTI-KILL x" + std::to_string(multi), 25);
		if (headStreak) Style("HEADSHOT STREAK", 50);
		if (a_bow && a_dist >= 2800.0f) Style("LONG SHOT", 60);
		if (Movement::Gliding()) Style("DIVE KILL", 75);
		if (a_melee && a_dying && (Director::IsElite(a_dying) || Director::IsBoss(a_dying) || (Director::EnemyRow(a_dying) >= 0 && gen::kEnemies[Director::EnemyRow(a_dying)].kind == 1))) {
			Style("EXECUTIONER", 100);
		}
	}

	void OnHurt()
	{
		Eggs::OnHurt();
		std::scoped_lock l(g_lock);
		g_streak = 0;
	}
	void OnEarned(int a_septims)
	{
		std::scoped_lock l(g_lock);
		g_now.septims += a_septims;
	}
	void Note(Kind a_kind)
	{
		std::scoped_lock l(g_lock);
		Ensure();
		switch (a_kind) {
		case kMiniBoss: ++g_now.minis, ++g_rec.minis; break;
		case kRevive: ++g_now.revives; break;
		case kWave: ++g_now.waves; break;
		case kQuest: ++g_rec.quests; break;
		case kWonder: ++g_rec.wonders; break;
		}
	}
	void NightBegins(int a_night)
	{
		std::scoped_lock l(g_lock);
		Ensure();
		g_now = {};
		g_now.night = a_night;
		g_streak = 0, g_headRun = 0;
	}
	void NightEnds(int a_night)
	{
		{
			std::scoped_lock l(g_lock);
			Ensure();
			g_now.night = a_night;
			g_last = g_now;
			++g_rec.nights;
			g_rec.bestNight = std::max(g_rec.bestNight, a_night);
			SaveRecords();
		}
		Hud::Banner(fmt::format("NIGHT {} SURVIVED - your stats are in the Party tab", a_night), 5.0f);
	}
	Line Tonight() { std::scoped_lock l(g_lock); return g_now; }
	Line LastNight() { std::scoped_lock l(g_lock); return g_last; }
	Records Lifetime() { std::scoped_lock l(g_lock); Ensure(); return g_rec; }
	void Heard(std::uint64_t a_from, const std::string& a_name, const Line& a_line)
	{
		std::scoped_lock l(g_lock);
		g_remote[a_from] = { a_name, a_line, Clock::now() };
	}
	std::vector<Friend> Party()
	{
		std::scoped_lock l(g_lock);
		std::vector<Friend> out;
		for (auto& [id, r] : g_remote) {
			if (Clock::now() - r.heard < std::chrono::seconds(10)) out.push_back({ r.name, r.line });
		}
		return out;
	}
}
