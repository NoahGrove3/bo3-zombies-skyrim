// The wonder weapons in your hands (Noah's models, tools/wonderprops.py): while one is drawn its model hangs on the
// bones that hold it, in first person and in third - Gravecaller's staff in the right hand, the Frostbinder gauntlets
// on both hands, the Wolf Queen's Crown on your head (and, in first person, a small one turning over your left hand),
// the Horn of the Barrow-Jarls in the left hand. Each breathes with its colour while held, sheds its element's motes,
// and moves with every shot: a tap kicks it, a charged attack swings it through (Gun's Spellcraft::Launch sets the
// kick). Included by Gun.cpp inside its anonymous namespace, after Spellcraft and Bows.
namespace Props
{
	struct Piece
	{
		const char*  node;          // the bone it hangs on
		const char*  model;         // under Meshes
		bool         first, third;  // in which body
		RE::NiPoint3 at;            // in the bone's space
		float        scale;
		int          kind;          // 0 staff, 1 gauntlet, 2 crown worn, 3 crown floating, 4 horn
	};
	struct Wonder
	{
		std::string_view id;
		std::vector<Piece> pieces;
	};
	const std::vector<Wonder>& Wonders()
	{
		static const std::vector<Wonder> w{
			{ "gravecaller", { { "WEAPON", "CurseOfAlduin\\wonder\\gravecaller.nif", true, true, { 0, 0, 0 }, 1.0f, 0 } } },
			{ "frostbinder", { { "NPC R Hand [RHnd]", "CurseOfAlduin\\wonder\\frostbinder_r.nif", true, true, { 0, 0, 0 }, 1.0f, 1 },
								 { "NPC L Hand [LHnd]", "CurseOfAlduin\\wonder\\frostbinder_l.nif", true, true, { 0, 0, 0 }, 1.0f, 1 } } },
			{ "wolfcrown", { { "NPC Head [Head]", "CurseOfAlduin\\wonder\\wolfcrown.nif", false, true, { 0, 1.0f, 6.0f }, 1.0f, 2 },
							   { "NPC L Hand [LHnd]", "CurseOfAlduin\\wonder\\wolfcrown.nif", true, false, { 0, 9.0f, 7.0f }, 0.38f, 3 } } },
			{ "barrowhorn", { { "SHIELD", "CurseOfAlduin\\wonder\\barrowhorn.nif", true, true, { 0, 0, 0 }, 1.0f, 4 } } },
		};
		return w;
	}

	struct Hung
	{
		Spellcraft::Fx fx;
		int            wonder = -1, piece = -1;
		bool           first = false;
	};
	std::vector<Hung> g_hung;
	int               g_wonder = -1;
	float             g_t = 0, g_mote = 0;
	RE::NiAVObject*   g_root1 = nullptr;
	RE::NiAVObject*   g_root3 = nullptr;

	void Clear()
	{
		for (auto& h : g_hung) {
			h.fx.Detach();
		}
		g_hung.clear();
	}

	int SpellRow(std::string_view a_id)
	{
		for (int r = 0; r < static_cast<int>(gen::kSpellcraft.size()); ++r) {
			if (gen::kSpellcraft[r].id == a_id) {
				return r;
			}
		}
		return -1;
	}

	RE::NiMatrix3 Rot(float a_x, float a_y, float a_z)
	{
		RE::NiMatrix3 m;
		m.SetEulerAnglesXYZ(a_x, a_y, a_z);
		return m;
	}

	void Hang(RE::PlayerCharacter* p, int a_w)
	{
		Clear();
		const auto& W = Wonders()[a_w];
		for (int k = 0; k < static_cast<int>(W.pieces.size()); ++k) {
			const auto& pc = W.pieces[k];
			for (bool first : { true, false }) {
				if ((first && !pc.first) || (!first && !pc.third)) {
					continue;
				}
				auto* root = p->Get3D(first);
				auto* bone = root ? root->GetObjectByName(pc.node) : nullptr;
				auto* node = bone ? bone->AsNode() : nullptr;
				if (!node) {
					static int said = 0;
					if (said++ < 8) logger::warn("wonder props: no {} in the {} body", pc.node, first ? "first-person" : "third-person");
					continue;
				}
				Hung h;
				h.fx = Spellcraft::Fx{ Spellcraft::LoadPath(pc.model) };
				if (!h.fx.obj) {
					continue;
				}
				h.fx.Attach(node);
				h.wonder = a_w, h.piece = k, h.first = first;
				g_hung.push_back(std::move(h));
			}
		}
		logger::info("wonder props: {} in hand ({} pieces hung)", W.id, g_hung.size());
	}

	void Tick(float a_delta, int a_gun)
	{
		auto* p = RE::PlayerCharacter::GetSingleton();
		int   w = -1;
		if (p && a_gun >= 0 && a_gun < static_cast<int>(gen::kGuns.size())) {
			for (int i = 0; i < static_cast<int>(Wonders().size()); ++i) {
				if (Wonders()[i].id == gen::kGuns[a_gun].id) {
					w = i;
				}
			}
		}
		// a new body (a load, a race change, first person rebuilt) or another wonder: hang them again
		auto* r1 = p ? p->Get3D(true) : nullptr;
		auto* r3 = p ? p->Get3D(false) : nullptr;
		if (w != g_wonder || r1 != g_root1 || r3 != g_root3) {
			Clear();
			g_wonder = w, g_root1 = r1, g_root3 = r3;
			if (w >= 0 && (r1 || r3)) {
				Hang(p, w);
			}
		}
		if (w < 0 || g_hung.empty()) {
			return;
		}
		g_t += a_delta;
		// the attack motion: in fast, out slow
		float kick = 0.0f;
		if (g_propKickT >= 0) {
			const float dur = g_propKickBig ? 0.7f : 0.3f;
			const float u = g_propKickT / dur;
			kick = u < 0.2f ? u / 0.2f : 1.0f - (u - 0.2f) / 0.8f;
			kick = kick * kick * (3 - 2 * kick);
			if ((g_propKickT += a_delta) >= dur) {
				g_propKickT = -1.0f;
			}
		}
		const bool  big = g_propKickBig;
		const float breathe = 1.0f + 0.02f * std::sin(g_t * 2.4f);
		const int   row = SpellRow(Wonders()[w].id);
		for (auto& h : g_hung) {
			const auto& pc = Wonders()[w].pieces[h.piece];
			RE::NiPoint3  at = pc.at;
			RE::NiMatrix3 rot;  // (identity)
			float         sc = pc.scale * breathe;
			switch (pc.kind) {
			case 0:  // the staff: a tap jabs it forward along its length; the call drives it up, then down into the ground
				at.y += (big ? 14.0f : 7.0f) * kick;
				rot = Rot(big ? -0.5f * kick : -0.15f * kick, 0, 0);
				break;
			case 1:  // the gauntlets: a punch out along the fingers; the charged grip clenches them bigger, rimed
				at.z += (big ? 5.0f : 3.0f) * kick;
				sc *= 1.0f + (big ? 0.18f : 0.06f) * kick;
				break;
			case 2:  // the crown worn: it flares
				sc *= 1.0f + (big ? 0.12f : 0.04f) * kick;
				break;
			case 3:  // the small crown over your hand: turning, bobbing, leaping up when it calls
				at.z += 1.2f * std::sin(g_t * 1.7f) + (big ? 9.0f : 3.0f) * kick;
				rot = Rot(0, g_t * 0.9f + (big ? 6.28f * kick : 0.0f), 0);
				sc *= 1.0f + (big ? 0.5f : 0.15f) * kick;
				break;
			case 4:  // the horn: raised and tipped back as it sounds
				at.z += (big ? 6.0f : 3.0f) * kick;
				rot = Rot((big ? 0.55f : 0.25f) * kick, 0, 0);
				break;
			}
			h.fx.obj->local.translate = at;
			h.fx.obj->local.rotate = rot;
			h.fx.obj->local.scale = std::max(0.0001f, sc);
			RE::NiUpdateData ud;
			h.fx.obj->Update(ud);
			h.fx.Show(true);
		}
		// its element's motes drift off it (the staff's head, the knuckles, the crown, the horn's bell), thicker when it fires
		if (row >= 0 && (g_mote -= a_delta) <= 0) {
			g_mote = kick > 0 ? 0.08f : 0.45f;
			const bool fp = Spellcraft::FirstPerson();
			for (auto& h : g_hung) {
				if (h.first != fp) {
					continue;
				}
				const auto& pc = Wonders()[w].pieces[h.piece];
				const RE::NiPoint3 tipLocal = pc.kind == 0 ? RE::NiPoint3{ 0, 70.0f, 0 } : pc.kind == 1 ? RE::NiPoint3{ 0, 1.0f, 8.0f } : pc.kind == 4 ? RE::NiPoint3{ 0, 24.0f, -4.0f } : RE::NiPoint3{ 0, 0, 10.0f };
				const auto& o = h.fx.obj->world;
				Spellcraft::BurstAt(row, o.translate + o.rotate * tipLocal * o.scale, kick > 0 ? 14.0f : 6.0f, 0.25f);
			}
		}
	}
}
