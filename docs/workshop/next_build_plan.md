# Next build (after d7da111) - Noah's answers, 2026-11-25 ~03:00

What he meant by "unique": **custom weapon looks and abilities - not changing their model or adding something on top.**

## His answers
- Looks: **Fully new look per weapon.** Drop the add-ons (tools/weaponsig.py: chains, crystals, seals, rings, cards...) AND the
  8 shared families as the system. Each weapon gets looks designed from scratch for it; **he picks from new options** in the
  workshop (plan: 3 options per weapon, each its own textures and motion, all on the weapon's own surface/edges, nothing that
  changes its silhouette). Colour still pickable.
- Abilities: **One custom effect each** - our own effect per ability, made from Skyrim's textures, replacing what plays now
  (no Skyrim effect + a second laid over it).
- Previews: **Same effect in doc and game** - each ability's effect built in our own format (fxwrite job rows), so the doc
  draws exactly what the game plays and his changes carry over. Needs one-shot animation (scale / alpha / move keys) in
  fxwrite + the doc renderer + the game placing it (QueueFx / WorldFx; controllers must start when placed).
- Scythes: **Both** - our own scythes built from Skyrim's own parts (like the Keyhammer and Dragon Slayer), each keeping its
  theme (Nightcaller's/daedric, Silver Reaping, Bonereaper/skeleton, Wyrmtooth/dragon teeth, Bloodmoon/vampire, and the
  Shadowflame tiers Forsworn, Falmer, Dwarven, Dragon Priest), 25% larger; previewed in the doc for his OK first.

## Decisions from the d7da111 doc
- Conjurations: keep Skyrim's portal for all of them.
- Soulcaller's beam: keep the Staff of Magnus beam.
- Signatures: "Yes, these fit" (the ideas) - but per the clarification they become custom looks, not add-ons.
- Gravebell: "remove the pulsing".
- Class spells: done in d7da111 as Skyrim impact laid over the spell's own -> under "one custom effect each" these become our
  own custom impacts too (replacing, not layered).
