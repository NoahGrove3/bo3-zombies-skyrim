# The Tablet of Mysteries (Noah, 2026-11-08 - approved design)

The mod's mystery box. Pay Cursed Essence, it rolls, you get a random **donated weapon**: stronger than your kit, but
worn out - it has limited uses and is lost if you fall.

## Theme: "used and forgotten"
- An old floating stone tablet, cracked and patched with iron straps and rusted rivets (Noah: no broken piece floating
  off it, no white patches; the pedestal in the tablet's own stone and manner).
- Glyphs flicker unevenly like a failing lantern; candle stubs and old coins on the pedestal.
- The back (Noah: option 4): the front's glyph border, and inside it tallies and names scratched in by past owners.
- 4K textures (stone, metal, emissive glyph layers).
- Idle: slow bob and turn, uneven glyph glow, faint motes.
- Roll (animated on the model, not an overlay): glyph rings grind round unevenly, sparks spit from the cracks, ghostly
  weapon outlines flicker in its centre slowing like a reel, the won weapon rises out of the face - scratched, notched,
  bound with cloth - and turns there until taken (12 s, then it fades).
- A battered (stronger) win: the light goes gold and the tablet rings like a bell.
- The skull: the tablet cracks, a Dragon Priest's mask appears and laughs, your essence is refunded, the tablet flies
  off to another freed city.

## Where
- Its own stand (not the Echo Altar), at one freed city at a time; appears once the first city is freed.
- Shown on the map and compass.
- After a random 4-8 rolls the skull can come up and the tablet moves.
- Co-op: one tablet everyone sees; each player rolls their own weapons.

## Price
- 1,500 to spin; +500 for every spin or repair; back to 1,500 at dawn. After night 10 the whole price is 3% more a
  night (the essence economy, 2026-11-08).
- Repair ("refill") at the tablet costs the same as the current spin price and raises it too (no farming).

## Taking a weapon
- Equips at once; whatever you held goes back to your inventory. The weapon is in your inventory too.
- One donated weapon at a time: a new roll replaces it. Lost if you fall.
- Description ends: "Donated to the Tablet of Mysteries. Worn with use, and lost if you fall."

## Balance
- Worn: one tier above your class sword's current tier, more uses.
- Battered: two tiers above, fewer uses.
- One hit / shot / cast = one use; at zero it breaks and falls apart in your hands.
- Durability shown like Minecraft: a green-yellow-red bar on the HUD weapon card; the inventory name shows what's left,
  e.g. "Gravebell Mace (62%)" (Skyrim's inventory can't draw a bar).
- Not affected by class brews, armour bonuses or Echo Altar upgrades; can't be upgraded at the altar.

## Odds (Noah: "a 15% chance at stronger weapons")
- Worn 83% (split evenly), Battered 15% (split evenly), Skull 2%.

## The weapons (Noah's picks: 1, 2, 4, 6, 9, 10, 11, 12, 13, 14)
Worn (+1 tier):
- Stormbound Knuckles (fists, 350 punches): brass knuckles round cracked storm gems; each punch arcs lightning to 2 more;
  a heavy punch slams the ground in a shock ring. (Noah: Galvaknuckles-style)
- Gravebell Mace (mace, 300 hits): a cracked temple bell on a pole; every 5th hit stuns the dead within 5 m.
- Saltrice Sickle (dagger, 320 hits): a salt-crusted sickle; the struck take 30% more from everything for 4 s.
- Riverstone Axe (battleaxe, 240 hits): waterlogged; hits knock the dead back; a heavy attack splits the ground in a line.
- Rattlebone Bow (bow, 200 arrows): sinew and old bones; every arrow bounces to a second enemy.
- Lanternwick Staff (staff, 90 casts): a cracked lantern on a stick; an orb that pulls the dead in, then bursts.

Battered (+2 tiers):
- Emberclaw Wraps (fists, 160 punches): charred wraps with coals sewn in; punches set the dead burning, every 4th punch
  throws a fireball.
- Eclipse Edge (greatsword, 120 hits): sun and moon halves; light slashes alternate them; a heavy attack calls a 6 s
  eclipse that blinds and slows the dead.
- The Wandering Star (staff, 40 casts): a dim caged star that bounces between up to 8 enemies.
- Whisperwood Longbow (bow, 80 arrows): still sprouting leaves; arrows root the dead and heal you a little per enemy held.
- (Noah, 2026-11-08) The Grimoire (battered, one-handed spell book, 30 casts; the strongest of the tablet's weapons - it
  must look and feel strong): an actual spell book held open in the hand; casting comes from the open book - pages
  turning, magical visuals rising off them. (Noah: idea 1) The Unwritten Codex: each cast flips to a random chapter and its
  glyphs lift off the page into the spell - Fire: a meteor where you aim; Frost: a line of ice spikes bursting out of
  the ground toward the target, freezing all it hits; Storm: a bolt chaining through up to 10 of the dead; Void: a black
  orb that pulls the dead together and crushes them. Every 10th cast is a "chapter's end": all four at once.
  Casting: the book snaps open, its pages flip on their own, glyphs and embers rise off them, a spell circle forms in the
  air before it; at its last use it burns to ash in the hand.

Fists: (Noah: "real hand wraps or elemental brass knuckles") gloves worn in the hands' slot while held, skinned to the
hand and finger bones so they close with the fist (tools/tabletgloves.py); whatever gloves you wore go back on after.

## Built (2026-11-08)
- Models: tools/tabletweapons.py (4K, Nordic runes and knotwork, a dull worn finish), the gloves tools/tabletgloves.py;
  the Codex hangs open on the right palm while it is the spell in that hand.
- Powers: src/Tablet.cpp OnStrike / CastPower. The Eclipse's "blind" is a 6 s hold (the dead frozen in place), on a
  10 s cooldown; the Stormbound slam and the Riverstone split have a 1 s cooldown. The staffs and the Codex cast nothing
  of Skyrim's own: their effects are all ours.
