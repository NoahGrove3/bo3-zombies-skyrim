"""Each weapon's own looks (Noah, 2026-11-25: "custom weapon looks and abilities not changing their model or adding something on
top" - "Fully new look per weapon": not the eight shared families, not pieces laid over the steel; each weapon's looks designed
for it, three to pick from, all made of light on its own edges and faces).

Each look is layers of light on the weapon's own surface - sheets along its edges and skins over its faces (tools/weaponfx.py
art_part draws them, fitted to the model) - each layer its own texture (2K; ours and Skyrim's own enchantment membranes), its
own motion, width, speed and colour. A weapon's three looks come from what it does (its description) and its picked colour.

    import weaponlooks; weaponlooks.LOOKS3[key] -> [look, look, look]   (look: {name, desc, edges: [...], faces: [...]})
"""


def E(tex, w, wc, per, rep, gain, along='contour', col='c1', flip=None, fps=None, blend='add', dark=False):
    """a sheet along the edges: its width out and across the face, seconds per scroll, repeats per 40 units, gain, which way it
    runs (contour round the outline; axis hilt to tip; axisdown tip to hilt), its colour (c1 / c2 / a colour name)"""
    return dict(tex=tex, w=w, wc=wc, per=per, rep=rep, gain=gain, along=along, col=col, flip=flip, fps=fps, blend=blend, dark=dark)


def Fc(tex, how, tile, gain, scroll=0.0, pulse=None, flip=None, fps=None, blend='add', dark=False, col='c1'):
    """a skin over both faces: how it lies (all / edge: near the edges / band: down the middle), its tile size, gain, scroll"""
    return dict(tex=tex, how=how, tile=tile, gain=gain, scroll=scroll, pulse=pulse, flip=flip, fps=fps, blend=blend, dark=dark, col=col)


def L(name, desc, edges, faces=()):
    return dict(name=name, desc=desc, edges=list(edges), faces=list(faces))


BOLT = dict(flip=(8, 4), fps=26)
# ---------------------------------------------------------------- each weapon: its two colours and its three looks
W = {}
W['priest_vahlok'] = (('white', 'green'), [
    L('Jailor\'s Iron', 'Cold white light locked in the steel\'s cracks, a slow green pulse running through them like a heartbeat in a cell.',
      [E('molten_strip', 2.0, 1.2, 4.5, 0.9, 0.9)], [Fc('cracks', 'all', 16.0, 1.4, pulse=(3.8, 0.35, 1.2)), Fc('sk_enchfill', 'band', 30.0, 0.3, scroll=6.0, col='c2')]),
    L('Shackle Light', 'A thin bright line round the head\'s outline, beads of light crawling slowly round it like links being dragged.',
      [E('molten_strip', 1.4, 0.8, 9.0, 2.4, 1.3), E('sig_band', 2.4, 1.2, 3.0, 0.4, 0.6, col='c2')], [Fc('haze', 'edge', 20.0, 0.25, scroll=7.0)]),
    L('Warden\'s Glow', 'A soft green-white glow seeping out of the steel and hanging close to it.',
      [E('mist_strip1', 3.6, 2.2, 7.0, 0.8, 0.28, col='c2'), E('stream_strip', 1.2, 0.8, 2.2, 1.0, 0.7)], [Fc('sk_vapour', 'all', 22.0, 0.25, scroll=9.0)])])
W['priest_nahkriin'] = (('white', 'iceblue'), [
    L('Blinding Mist', 'White mist pouring off both edges, with a slow blinding sheen sweeping up the steel every few seconds.',
      [E('mist_strip0', 5.0, 2.8, 6.0, 0.8, 0.3), E('sig_band', 3.0, 2.0, 4.5, 0.25, 0.5)], [Fc('sk_vapour', 'band', 28.0, 0.22, scroll=7.0)]),
    L('Skuldafn Fog', 'A cold, low fog that clings to the blade and creeps toward the hilt.',
      [E('vapour_strip', 6.0, 3.6, 5.0, 0.9, 0.45, along='axisdown', col='c2'), E('mist_strip1', 3.0, 1.8, 4.0, 1.2, 0.22)], [Fc('sk_frost', 'all', 24.0, 0.22, scroll=10.0)]),
    L('Veil of Sovngarde', 'Thin white veils streaming tipward off the edges, as if the mist were the door it guards.',
      [E('wisp_strip', 6.5, 3.4, 2.4, 0.7, 0.4, along='axis'), E('stream_strip', 1.0, 0.6, 2.0, 1.0, 0.5, col='c2')], [Fc('haze', 'edge', 24.0, 0.3, scroll=6.0)])])
W['priest_volsung'] = (('white', 'blue'), [
    L('Gale Smoke', 'Pale smoke torn back off the edge in long streaks, as if the blade were always moving fast.',
      [E('sig_streak', 5.5, 2.6, 0.9, 0.5, 0.9, along='axisdown'), E('shade_strip', 5.0, 2.8, 2.6, 0.9, 0.7, along='axisdown', blend='alpha', dark=True)], [Fc('sk_swirls', 'edge', 26.0, 0.4, scroll=4.0, blend='alpha', dark=True)]),
    L('Thin Air', 'A clear, cutting shimmer: fine wind lines racing along the edge and a blue glint at its rim.',
      [E('sig_streak', 3.6, 1.6, 0.6, 0.7, 1.1, along='axis'), E('stream_strip', 1.0, 0.6, 1.2, 1.0, 0.6, col='c2')], [Fc('haze', 'edge', 20.0, 0.25, scroll=3.0)]),
    L('Peak Storm', 'Grey storm-cloud rolling round the blade with pale light breaking through it.',
      [E('mist_strip0', 5.5, 3.0, 4.0, 0.8, 0.3), E('sig_streak', 4.0, 2.0, 1.1, 0.6, 0.6, along='axisdown', col='c2')], [Fc('sk_vapour', 'all', 22.0, 0.3, scroll=5.0)])])
W['priest_zahkriisos'] = (('iceblue', 'white'), [
    L('Glacier Heart', 'Rime needles growing out of both edges and frost spreading over the steel, glinting.',
      [E('rime_strip', 3.8, 2.0, 24.0, 1.4, 1.4), E('vapour_strip', 6.0, 3.6, 5.0, 0.9, 0.45, along='axisdown', col='c2')], [Fc('frost_shell', 'edge', 18.0, 1.5, pulse=(3.4, 0.75, 1.1)), Fc('sk_frost', 'all', 24.0, 0.3, scroll=9.0)]),
    L('Black Ice', 'Deep blue cold welling in the middle of the blade, its edges white and still.',
      [E('molten_strip', 1.8, 1.0, 6.0, 1.0, 0.8, col='c2')], [Fc('frost_shell', 'band', 16.0, 1.2), Fc('sk_frost', 'band', 20.0, 0.4, scroll=12.0)]),
    L('Sea of Ghosts', 'Freezing vapour pouring off the blade and sinking, the cold of the ice it crossed.',
      [E('vapour_strip', 7.5, 4.0, 4.0, 0.8, 0.6, along='axisdown'), E('rime_strip', 2.4, 1.2, 30.0, 1.8, 0.8, col='c2')], [Fc('haze', 'edge', 24.0, 0.3, scroll=6.0)])])
W['priest_otar'] = (('blue', 'white'), [
    L('Mad Storm', 'Real lightning cracking along the edges and across the steel, never still.',
      [E('bolt_edge', 4.6, 2.6, 0, 1, 1.35, **BOLT), E('stream_strip', 1.6, 1.0, 0.7, 2.0, 0.55, col='c2')], [Fc('bolt_shell', 'all', 24.0, 1.1, flip=(8, 4), fps=22)]),
    L('Charged Steel', 'The steel humming with a white charge, Skyrim\'s shock web crawling over it.',
      [E('stream_strip', 1.4, 0.9, 0.5, 2.4, 0.9, col='c2')], [Fc('sk_shock', 'all', 18.0, 0.7, scroll=1.6), Fc('haze', 'edge', 20.0, 0.3, scroll=2.0)]),
    L('Ragnvald\'s Seal', 'Bolts held down to thin flickering threads along the edge, a storm kept in.',
      [E('bolt_edge', 2.2, 1.2, 0, 1, 1.1, **BOLT), E('molten_strip', 1.2, 0.8, 2.0, 2.0, 0.7)], [Fc('sk_shock', 'band', 22.0, 0.35, scroll=3.0)])])
W['priest_rahgot'] = (('poison', 'green'), [
    L('Venom Fire', 'Sickly green fire licking up the blade from the hilt to the point.',
      [E('fire_rise0', 6.5, 3.6, 0.9, 1.1, 0.75, along='axis'), E('fire_rise1', 4.5, 2.6, 0.55, 1.8, 0.5, along='axis', col='c2')], [Fc('sk_enchfill', 'all', 30.0, 0.3, scroll=3.0)]),
    L('Dripping Venom', 'Venom beading along the edges and running toward the point, a poisoned sheen on the steel.',
      [E('sig_drops', 2.6, 1.4, 3.0, 0.6, 1.0, along='axis'), E('stream_strip', 1.2, 0.8, 2.0, 1.0, 0.5)], [Fc('sk_vapour', 'band', 22.0, 0.3, scroll=6.0, col='c2')]),
    L('Forelhost Rot', 'A thick green rot-smoke curling off the blade and hanging round it.',
      [E('mist_strip0', 5.0, 2.8, 5.0, 0.9, 0.35), E('shade_strip', 4.5, 2.6, 3.0, 0.9, 0.6, along='axis', blend='alpha', dark=True)], [Fc('sk_swirls', 'all', 22.0, 0.45, scroll=6.0, blend='alpha', dark=True)])])
W['priest_krosis'] = (('red', 'white'), [
    L('Weeping Mist', 'Red mist that weeps: slow streaks running down the edges toward the hilt, like tears.',
      [E('mist_strip1', 4.2, 2.6, 5.0, 1.2, 0.26), E('sig_drops', 1.8, 1.0, 5.5, 0.7, 0.9, along='axisdown', col='c2')], [Fc('sk_vapour', 'band', 26.0, 0.25, scroll=8.0)]),
    L('Mourning Blade', 'A deep red glow in the steel that swells and fades slowly, grief breathing.',
      [E('molten_strip', 1.8, 1.0, 5.0, 1.0, 0.8)], [Fc('sk_enchfill', 'all', 28.0, 0.5, scroll=8.0, pulse=(5.0, 0.4, 1.2)), Fc('haze', 'edge', 22.0, 0.3, scroll=8.0, col='c2')]),
    L('Shearpoint Ash', 'Grey-red ash streaming off the blade into the wind.',
      [E('sig_streak', 4.6, 2.4, 1.4, 0.5, 0.7, along='axisdown'), E('shade_strip', 4.0, 2.4, 3.0, 0.9, 0.6, along='axisdown', blend='alpha', dark=True)], [Fc('sk_swirls', 'edge', 26.0, 0.4, scroll=5.0, blend='alpha', dark=True)])])
W['priest_hevnoraak'] = (('green', 'white'), [
    L('Broken Seal', 'Green light bursting out through cracks across the stone, pulsing outward like a seal giving way.',
      [E('molten_strip', 2.4, 1.4, 3.0, 1.0, 1.0)], [Fc('cracks', 'all', 14.0, 1.8, pulse=(2.2, 0.4, 1.3)), Fc('sk_enchfill', 'band', 28.0, 0.3, scroll=5.0, col='c2')]),
    L('Marsh Light', 'A will-o\'-wisp glow seeping from the stone and drifting off it.',
      [E('wisp_strip', 5.5, 3.0, 2.4, 0.8, 0.35, along='axis'), E('stream_strip', 1.2, 0.8, 2.0, 1.0, 0.6)], [Fc('sk_vapour', 'all', 22.0, 0.25, scroll=8.0)]),
    L('Valthume Runes', 'Old runes cut in the stone, lit from within, light running round its outline.',
      [E('sig_runes', 2.4, 1.4, 12.0, 0.6, 1.0), E('molten_strip', 1.2, 0.8, 3.0, 1.0, 0.6, col='c2')], [Fc('haze', 'edge', 20.0, 0.3, scroll=6.0)])])
W['priest_vokun'] = (('purple', 'shadow'), [
    L('Nightmare Glow', 'Purple light deep in the blade, slowly shifting like a dream.',
      [E('molten_strip', 2.0, 1.2, 4.0, 1.0, 0.9)], [Fc('cracks', 'all', 20.0, 1.2, pulse=(4.5, 0.4, 1.1)), Fc('sk_wisps', 'all', 24.0, 0.35, flip=(2, 2), fps=2)]),
    L('High Gate Shadow', 'Dark smoke coiling off the blade, violet only at its thin rim.',
      [E('shade_strip', 6.0, 3.4, 3.2, 0.9, 1.0, along='axis', blend='alpha', dark=True), E('stream_strip', 1.4, 0.9, 1.6, 1.0, 0.6, col='c2')], [Fc('sk_swirls', 'all', 26.0, 0.55, scroll=6.0, blend='alpha', dark=True)]),
    L('Dreamer\'s Veil', 'Slow sleepy wisps curling round the blade and fading.',
      [E('wisp_strip', 6.0, 3.4, 3.4, 0.6, 0.4, along='contour'), E('mist_strip1', 3.6, 2.2, 6.0, 1.0, 0.2, col='c2')], [Fc('haze', 'edge', 26.0, 0.3, scroll=9.0)])])
W['priest_morokei'] = (('drain', 'arcane'), [
    L('Arcane Radiance', 'A halo round the maul\'s head and rays of magicka standing off it.',
      [E('ray_strip', 11.0, 0, 9.0, 0.6, 0.45), E('stream_strip', 1.6, 1.0, 1.2, 1.0, 0.8)], [Fc('halo_shell', 'all', 30.0, 0.35, pulse=(4.0, 0.7, 1.1)), Fc('sk_enchfill', 'all', 30.0, 0.28, scroll=4.0, col='c2')]),
    L('Magnus Echo', 'The crystal humming with drained magicka, its light streaming inward to the heart.',
      [E('stream_strip', 1.8, 1.2, 0.8, 1.4, 1.0), E('wisp_strip', 4.0, 2.4, 1.8, 0.8, 0.35, along='contour', col='c2')], [Fc('sk_wisps', 'all', 20.0, 0.45, flip=(2, 2), fps=4)]),
    L('Labyrinthian Rune', 'Runes cut round the head, lit in blue, light running slowly through them.',
      [E('sig_runes', 2.6, 1.6, 10.0, 0.6, 1.1), E('molten_strip', 1.2, 0.8, 4.0, 1.0, 0.6)], [Fc('halo_shell', 'edge', 26.0, 0.3, pulse=(5.0, 0.6, 1.1), col='c2')])])
W['tab_stormtalons'] = (('blue', 'white'), [
    L('Storm Claws', 'Lightning running along each glass claw and crackling at its point.',
      [E('bolt_edge', 3.2, 1.8, 0, 1, 1.3, **BOLT), E('stream_strip', 1.0, 0.6, 0.7, 2.0, 0.6, col='c2')], [Fc('bolt_shell', 'all', 18.0, 1.0, flip=(8, 4), fps=22)]),
    L('Charged Glass', 'The glass full of a white charge, the shock web glowing inside it.',
      [E('stream_strip', 1.2, 0.8, 0.5, 2.4, 0.9, col='c2')], [Fc('sk_shock', 'all', 12.0, 0.8, scroll=1.5)]),
    L('Static Rake', 'Fine sparks racing along the claws\' edges, blue to white.',
      [E('sig_embers', 2.0, 1.0, 0.6, 0.8, 1.2, along='axis', col='c2'), E('bolt_edge', 1.6, 0.8, 0, 1, 0.9, **BOLT)], [Fc('haze', 'edge', 14.0, 0.3, scroll=2.0)])])
W['tab_gravebell'] = (('white', 'gold'), [
    L('Tolling Bronze', 'Warm light in the bell\'s cracks, glowing steadily, its rim lit gold.',
      [E('molten_strip', 2.2, 1.4, 3.0, 1.0, 1.0, col='c2')], [Fc('cracks', 'all', 12.0, 1.4)]),
    L('Grave Hush', 'A pale mist rising slowly off the bell, cold and quiet.',
      [E('mist_strip1', 3.6, 2.2, 6.0, 1.0, 0.28), E('stream_strip', 1.0, 0.6, 2.4, 1.0, 0.5, col='c2')], [Fc('sk_vapour', 'all', 16.0, 0.25, scroll=9.0)]),
    L('Turning Light', 'Holy gold light glowing out of the bell\'s mouth, steady and bright.',
      [E('ray_strip', 7.0, 0, 8.0, 0.6, 0.4, col='c2'), E('stream_strip', 1.2, 0.8, 2.0, 1.0, 0.7)], [Fc('halo_shell', 'all', 18.0, 0.4, col='c2')])])
W['tab_talons'] = (('red', 'orange'), [
    L('Talons of Fire', 'Red fire rising off each dragonbone claw to its point.',
      [E('fire_rise0', 4.0, 2.2, 0.9, 1.1, 0.75, along='axis'), E('fire_rise1', 3.0, 1.6, 0.55, 1.8, 0.5, along='axis', col='c2')], [Fc('sk_enchfill', 'all', 18.0, 0.3, scroll=3.0)]),
    L('Bleeding Bone', 'Blood running along each claw and off its point.',
      [E('sig_drops', 1.6, 0.9, 2.6, 0.7, 1.0, along='axis'), E('molten_strip', 1.0, 0.6, 3.0, 1.0, 0.6)], [Fc('haze', 'edge', 14.0, 0.3, scroll=5.0)]),
    L('Woe Embers', 'Embers streaming back off the claws as they move.',
      [E('sig_embers', 2.4, 1.2, 0.8, 0.8, 1.2, along='axisdown', col='c2'), E('stream_strip', 1.0, 0.6, 1.4, 1.0, 0.6)], [Fc('sk_enchfill', 'band', 18.0, 0.3, scroll=4.0)])])
W['tab_riverstone'] = (('green', 'white'), [
    L('River Stone', 'Moss-green light in the seams of a stone crust over the blade.',
      [E('molten_strip', 1.6, 1.0, 5.0, 1.0, 0.7)], [Fc('sig_stone', 'all', 14.0, 1.2, pulse=(6.0, 0.6, 1.1))]),
    L('White Water', 'Pale water-light running down the halberd\'s blade like a current.',
      [E('stream_strip', 1.6, 1.0, 1.0, 1.6, 0.9, col='c2'), E('sig_streak', 3.4, 1.8, 1.4, 0.6, 0.6, along='axis')], [Fc('sk_vapour', 'band', 20.0, 0.3, scroll=3.0, col='c2')]),
    L('Spriggan Glow', 'A soft green life-light seeping from the steel.',
      [E('wisp_strip', 4.6, 2.6, 3.0, 0.7, 0.35, along='axis'), E('molten_strip', 1.2, 0.8, 4.0, 1.0, 0.6)], [Fc('sk_wisps', 'all', 20.0, 0.35, flip=(2, 2), fps=2)])])
W['tab_wyrmspine'] = (('fear', 'orange'), [
    L('Dragon\'s Breath', 'Red-black fire roaring off the pike\'s head toward its point.',
      [E('fire_rise0', 6.5, 3.6, 0.6, 1.2, 0.85, along='axis'), E('fire_rise1', 4.5, 2.6, 0.4, 1.8, 0.6, along='axis', col='c2')], [Fc('sk_enchfill', 'all', 26.0, 0.35, scroll=2.0)]),
    L('Wyrm Scale', 'The head glowing dull red between dark scales, embers lifting off.',
      [E('sig_embers', 3.0, 1.6, 1.0, 0.8, 1.0, along='axis', col='c2')], [Fc('cracks', 'all', 14.0, 1.3, pulse=(3.0, 0.5, 1.2)), Fc('sk_swirls', 'band', 20.0, 0.4, scroll=5.0, blend='alpha', dark=True)]),
    L('Fear Smoke', 'Dark smoke streaming off the head, red light burning through it.',
      [E('shade_strip', 5.5, 3.0, 2.0, 0.9, 0.9, along='axis', blend='alpha', dark=True), E('stream_strip', 1.4, 0.9, 1.0, 1.0, 0.8)], [Fc('sk_swirls', 'all', 24.0, 0.5, scroll=4.0, blend='alpha', dark=True)])])
W['tab_lanternwick'] = (('iceblue', 'white'), [
    L('Rimewick Lanterns', 'Each head a cold lantern: ice-blue light glowing steadily through cracked rime.',
      [E('molten_strip', 1.8, 1.0, 5.0, 1.0, 0.8)], [Fc('frost_shell', 'all', 12.0, 1.4), Fc('sk_frost', 'all', 18.0, 0.35, scroll=8.0, col='c2')]),
    L('Snowfall', 'Snow drifting round each head and falling away.',
      [E('sig_snow', 4.0, 2.2, 2.4, 0.8, 0.9, along='axisdown', col='c2'), E('stream_strip', 1.0, 0.6, 2.0, 1.0, 0.6)], [Fc('sk_frost', 'all', 18.0, 0.3, scroll=6.0)]),
    L('Frozen Flame', 'Ice-blue flames standing still on each head, flickering slowly.',
      [E('fire_rise0', 4.0, 2.2, 2.6, 1.0, 0.65, along='axis'), E('rime_strip', 2.0, 1.0, 24.0, 1.4, 0.8, col='c2')], [Fc('haze', 'edge', 16.0, 0.3, scroll=6.0)])])
W['tab_cinderclaws'] = (('orange', 'red'), [
    L('Cinder Core', 'Orange heat glowing in each claw\'s cracks, like iron fresh from the forge.',
      [E('molten_strip', 1.6, 1.0, 3.0, 1.0, 1.0)], [Fc('cracks', 'all', 10.0, 1.5, pulse=(2.4, 0.5, 1.2))]),
    L('Ember Rake', 'Embers streaming off each claw\'s point as it moves.',
      [E('sig_embers', 2.4, 1.2, 0.7, 0.8, 1.3, along='axis'), E('stream_strip', 1.0, 0.6, 1.0, 1.0, 0.6, col='c2')], [Fc('sk_enchfill', 'band', 14.0, 0.35, scroll=3.0)]),
    L('Hearthfire', 'Small flames licking up each claw.',
      [E('fire_rise1', 3.0, 1.6, 0.6, 1.6, 0.6, along='axis'), E('fire_rise0', 2.0, 1.2, 0.9, 1.1, 0.5, along='axis', col='c2')], [Fc('haze', 'edge', 12.0, 0.3, scroll=3.0)])])
W['tab_eclipse'] = (('moon', 'gold'), [
    L('Eclipse Fire', 'Silver fire on one edge and gold on the other, meeting at the point.',
      [E('fire_rise0', 6.0, 3.4, 0.9, 1.1, 0.75, along='axis', col='c1'), E('fire_rise1', 4.4, 2.6, 0.55, 1.8, 0.5, along='axis', col='c2')], [Fc('sk_enchfill', 'all', 26.0, 0.3, scroll=3.0)]),
    L('Corona', 'A dark spearhead ringed in gold light, like the sun behind the moon.',
      [E('ray_strip', 9.0, 0, 7.0, 0.6, 0.5, col='c2'), E('molten_strip', 1.4, 0.8, 4.0, 1.0, 0.8, col='c2')], [Fc('sk_swirls', 'all', 22.0, 0.6, scroll=8.0, blend='alpha', dark=True)]),
    L('Moon and Sun', 'Cold moonlight and warm sunlight chasing each other along the edges.',
      [E('stream_strip', 1.6, 1.0, 1.2, 1.0, 0.9, col='c1'), E('sig_band', 3.0, 1.6, 2.4, 0.3, 0.7, col='c2')], [Fc('halo_shell', 'all', 24.0, 0.3, pulse=(4.0, 0.6, 1.1))])])
W['tab_frostmourn'] = (('iceblue', 'white'), [
    L('Frost Breath', 'Ice-blue mist sinking off the rapier, cold breath on the air.',
      [E('mist_strip0', 3.6, 2.0, 5.0, 0.9, 0.32), E('vapour_strip', 4.6, 2.6, 4.0, 0.9, 0.45, along='axisdown', col='c2')], [Fc('sk_frost', 'all', 18.0, 0.25, scroll=9.0)]),
    L('Needle Rime', 'Fine rime needles all along the thin blade, glinting.',
      [E('rime_strip', 2.4, 1.2, 26.0, 1.6, 1.3), E('stream_strip', 0.8, 0.5, 1.6, 1.0, 0.6, col='c2')], [Fc('frost_shell', 'edge', 12.0, 1.2, pulse=(3.0, 0.75, 1.1))]),
    L('Winter\'s Edge', 'A thin white-blue line of cold running up the edge to the point.',
      [E('molten_strip', 1.0, 0.6, 1.4, 1.6, 1.2, col='c2'), E('sig_streak', 2.6, 1.4, 1.2, 0.6, 0.6, along='axis')], [Fc('sk_frost', 'band', 16.0, 0.3, scroll=6.0)])])
W['tab_silverscythe'] = (('moon', 'white'), [
    L('Silver Moon', 'Moon-silver light in the steel and a glint sweeping along the blade\'s edge.',
      [E('molten_strip', 1.6, 1.0, 4.0, 1.0, 0.8), E('sig_band', 1.6, 0.8, 1.9, 0.25, 1.4, col='c2')], [Fc('cracks', 'all', 18.0, 1.0)]),
    L('Moonlit Haze', 'A pale silver haze hanging round the blade.',
      [E('mist_strip1', 4.0, 2.4, 6.0, 1.0, 0.25), E('stream_strip', 1.0, 0.6, 2.0, 1.0, 0.5, col='c2')], [Fc('sk_vapour', 'all', 22.0, 0.25, scroll=8.0)]),
    L('Reaper\'s Gleam', 'A cold hard shine along the blade\'s curve, bright at its point.',
      [E('stream_strip', 1.6, 1.0, 0.8, 1.4, 1.0, col='c2'), E('ray_strip', 5.0, 0, 6.0, 0.6, 0.3)], [Fc('halo_shell', 'edge', 20.0, 0.3)])])
W['tab_bonereaper'] = (('green', 'white'), [
    L('Soul Harvest', 'Green core in the bone, souls streaming up off it in thin ghostly trails.',
      [E('wisp_strip', 6.0, 3.2, 2.0, 0.6, 0.45, along='axis', col='c2'), E('molten_strip', 1.6, 1.0, 4.0, 1.0, 0.8)], [Fc('cracks', 'all', 18.0, 1.1)]),
    L('Grave Light', 'Necromantic green light glowing from deep inside the bone.',
      [E('stream_strip', 1.6, 1.0, 1.8, 1.0, 0.8)], [Fc('sk_enchfill', 'all', 24.0, 0.5, scroll=6.0, pulse=(4.0, 0.5, 1.1))]),
    L('Restless Dead', 'Pale wisps curling round the blade and fading.',
      [E('wisp_strip', 5.0, 2.8, 3.0, 0.8, 0.35, along='contour', col='c2'), E('mist_strip1', 3.0, 1.8, 6.0, 1.0, 0.2)], [Fc('sk_wisps', 'all', 20.0, 0.35, flip=(2, 2), fps=2)])])
W['tab_wyrmtooth'] = (('green', 'orange'), [
    L('Wyrm Venom', 'Green light glowing out from the dragon teeth along the blade.',
      [E('molten_strip', 2.0, 1.2, 3.0, 1.0, 1.0)], [Fc('cracks', 'all', 16.0, 1.3, pulse=(3.0, 0.5, 1.2))]),
    L('Bleeding Teeth', 'Blood-orange drops running along the teeth and off the blade.',
      [E('sig_drops', 2.0, 1.2, 3.0, 0.6, 1.0, along='axis', col='c2'), E('stream_strip', 1.2, 0.8, 1.6, 1.0, 0.6)], [Fc('haze', 'edge', 18.0, 0.3, scroll=5.0)]),
    L('Dragon Ember', 'Embers lifting off the blade\'s teeth.',
      [E('sig_embers', 3.0, 1.6, 0.9, 0.8, 1.1, along='axis', col='c2'), E('molten_strip', 1.2, 0.8, 3.0, 1.0, 0.6)], [Fc('sk_enchfill', 'band', 22.0, 0.3, scroll=4.0)])])
W['tab_bloodmoon'] = (('red', 'fear'), [
    L('Blood Moon', 'Deep red light in the steel, blood running down the blade and off its point.',
      [E('sig_drops', 2.4, 1.4, 2.8, 0.6, 1.0, along='axis'), E('molten_strip', 1.6, 1.0, 4.0, 1.0, 0.8)], [Fc('cracks', 'all', 18.0, 1.1, col='c2')]),
    L('Vampire\'s Hunger', 'Red mist drawn in toward the blade, as if it were drinking.',
      [E('mist_strip0', 4.6, 2.6, 5.0, 0.9, 0.3, along='axisdown'), E('stream_strip', 1.2, 0.8, 1.2, 1.0, 0.7, col='c2')], [Fc('sk_vapour', 'band', 22.0, 0.3, scroll=4.0)]),
    L('Crimson Night', 'Dark red smoke curling off the blade, blood-red at its rim.',
      [E('shade_strip', 5.6, 3.0, 3.0, 0.9, 0.9, along='axis', blend='alpha', dark=True), E('stream_strip', 1.4, 0.9, 1.6, 1.0, 0.6)], [Fc('sk_swirls', 'all', 24.0, 0.5, scroll=6.0, blend='alpha', dark=True)])])
W['fang'] = (('orange', 'shadow'), [
    L('World-Eater\'s Fire', 'Orange fire under a dark smoke, embers rising through it, Alduin\'s own.',
      [E('fire_rise0', 6.5, 3.6, 0.9, 1.1, 0.7, along='axis'), E('sig_embers4k', 4.0, 2.2, 0.9, 0.8, 1.0, along='axis'), E('shade_strip', 5.0, 2.8, 3.0, 0.9, 0.6, along='axis', blend='alpha', dark=True, col='c2')],
      [Fc('sk_enchfill', 'all', 30.0, 0.35, scroll=3.0)]),
    L('Time-Wound', 'A deep orange light in the bone, rippling slowly, the world\'s end held in it.',
      [E('molten_strip', 2.2, 1.4, 4.0, 1.0, 1.0)], [Fc('cracks', 'all', 20.0, 1.4, pulse=(4.0, 0.5, 1.2)), Fc('sk_swirls', 'band', 26.0, 0.4, scroll=8.0, blend='alpha', dark=True, col='c2')]),
    L('Black Dragon', 'Dark smoke pouring off the blade, orange burning only at its rim.',
      [E('shade_strip', 6.5, 3.6, 3.2, 0.9, 1.0, along='axis', blend='alpha', dark=True), E('stream_strip', 1.6, 1.0, 1.4, 1.0, 0.8)], [Fc('sk_swirls', 'all', 26.0, 0.55, scroll=6.0, blend='alpha', dark=True)])])
W['alch'] = (('shadow', 'purple'), [
    L('Shadowflame', 'Violet-black fire rising up the scythe\'s blade.',
      [E('fire_rise0', 7.0, 3.8, 0.9, 1.1, 0.8, along='axis'), E('fire_rise1', 5.0, 2.8, 0.55, 1.8, 0.55, along='axis', col='c2')], [Fc('sk_enchfill', 'all', 34.0, 0.32, scroll=3.0)]),
    L('Alchemist\'s Smoke', 'Dark violet smoke boiling off the blade like a spilled flask.',
      [E('shade_strip', 6.0, 3.4, 2.6, 0.9, 0.9, along='axis', blend='alpha', dark=True), E('sig_embers', 3.0, 1.6, 1.0, 0.8, 0.8, along='axis', col='c2')], [Fc('sk_swirls', 'all', 26.0, 0.5, scroll=5.0, blend='alpha', dark=True)]),
    L('Transmuted Edge', 'A violet line of heat along the blade, the steel glowing as if being changed.',
      [E('molten_strip', 2.0, 1.2, 2.4, 1.0, 1.1, col='c2')], [Fc('cracks', 'all', 22.0, 1.2, pulse=(3.0, 0.5, 1.2))])])
W['carian'] = (('purple', 'blue'), [
    L('Glintstone Mist', 'Soft purple mist over the whole wand, glinting faintly.',
      [E('mist_strip0', 4.0, 2.2, 6.0, 0.8, 0.3), E('mist_strip1', 3.0, 1.8, 4.2, 1.2, 0.22, col='c2')], [Fc('haze', 'edge', 16.0, 0.3, scroll=6.0)]),
    L('Starlight', 'Blue-violet starlight running along the wand and gathering at its tip.',
      [E('stream_strip', 1.4, 0.8, 1.0, 1.2, 0.9, along='axis', col='c2'), E('ray_strip', 4.0, 0, 6.0, 0.6, 0.3)], [Fc('sk_wisps', 'all', 14.0, 0.3, flip=(2, 2), fps=3)]),
    L('Sorcery', 'Runes glowing along the wand\'s length.',
      [E('sig_runes', 1.8, 1.0, 10.0, 0.6, 1.0), E('stream_strip', 0.8, 0.5, 2.0, 1.0, 0.5, col='c2')], [Fc('haze', 'edge', 14.0, 0.25, scroll=6.0)])])
W['radiant'] = (('gold', 'white'), [
    L('Radiant Heads', 'Gold light glowing in both heads\' cracks, bright at their edges.',
      [E('molten_strip', 2.0, 1.2, 3.0, 1.0, 1.0)], [Fc('cracks', 'all', 14.0, 1.4, pulse=(3.0, 0.6, 1.2))]),
    L('Sunfire', 'Golden flames rising off both heads.',
      [E('fire_rise0', 4.6, 2.6, 0.9, 1.1, 0.7, along='axis'), E('fire_rise1', 3.4, 2.0, 0.55, 1.8, 0.5, along='axis', col='c2')], [Fc('sk_enchfill', 'all', 20.0, 0.3, scroll=3.0)]),
    L('Dawn Halo', 'A halo of sunlight round each head, rays standing off it.',
      [E('ray_strip', 8.0, 0, 7.0, 0.6, 0.45), E('stream_strip', 1.2, 0.8, 1.6, 1.0, 0.8, col='c2')], [Fc('halo_shell', 'all', 18.0, 0.35, pulse=(4.0, 0.7, 1.1))])])
W['ss_none'] = (('white', 'blue'), [
    L('Bare Blade', 'A thin white line of light along the rapier\'s edge, nothing more.',
      [E('molten_strip', 1.2, 0.8, 3.0, 1.0, 0.8)], [Fc('cracks', 'all', 16.0, 0.7)]),
    L('Spellbound', 'A faint blue enchantment glow over the steel, waiting for a spell.',
      [E('stream_strip', 1.0, 0.6, 2.0, 1.0, 0.6, col='c2')], [Fc('sk_enchfill', 'all', 20.0, 0.35, scroll=5.0, col='c2')]),
    L('Duelist\'s Gleam', 'A bright gleam sweeping along the blade now and then.',
      [E('sig_band', 1.6, 0.8, 2.6, 0.3, 1.2), E('molten_strip', 0.8, 0.5, 3.0, 1.0, 0.5)], [Fc('haze', 'edge', 14.0, 0.2, scroll=5.0)])])
W['ss_fire'] = (('ss_none', 'orange'), [
    L('Fire-Bound', 'Fire running up the rapier with sparks lifting off the point.',
      [E('fire_rise0', 4.6, 2.6, 0.7, 1.2, 0.75, along='axis'), E('sig_embers', 2.4, 1.2, 0.8, 0.8, 1.0, along='axis', col='c2')], [Fc('sk_enchfill', 'all', 20.0, 0.3, scroll=3.0)]),
    L('Searing Edge', 'A white-hot line along the edge with heat shimmering off it.',
      [E('molten_strip', 1.4, 0.8, 1.6, 1.6, 1.2, col='c2'), E('fire_rise1', 2.4, 1.4, 0.5, 1.8, 0.4, along='axis')], [Fc('haze', 'edge', 14.0, 0.3, scroll=3.0)]),
    L('Cinder Thrust', 'Embers streaming back off the blade as if from a thrust.',
      [E('sig_embers', 3.0, 1.6, 0.6, 0.8, 1.2, along='axisdown'), E('stream_strip', 1.0, 0.6, 1.0, 1.0, 0.6, col='c2')], [Fc('sk_enchfill', 'band', 18.0, 0.3, scroll=3.0)])])
W['ss_frost'] = (('iceblue', 'white'), [
    L('Frost-Bound', 'Frost along the rapier with fine ice needles on its edge.',
      [E('rime_strip', 2.6, 1.4, 24.0, 1.6, 1.2), E('vapour_strip', 3.6, 2.0, 5.0, 0.9, 0.4, along='axisdown', col='c2')], [Fc('frost_shell', 'edge', 12.0, 1.2, pulse=(3.4, 0.75, 1.1))]),
    L('Cold Steel', 'Skyrim\'s frost cloud drifting slowly over the blade.',
      [E('stream_strip', 1.0, 0.6, 2.0, 1.0, 0.6, col='c2')], [Fc('sk_frost', 'all', 16.0, 0.45, scroll=8.0)]),
    L('Ice Fang', 'A sharp white-blue cold gathered toward the point.',
      [E('sig_streak', 2.6, 1.4, 1.0, 0.6, 0.9, along='axis', col='c2'), E('molten_strip', 1.0, 0.6, 2.0, 1.0, 0.8)], [Fc('sk_frost', 'band', 14.0, 0.3, scroll=6.0)])])
W['ss_shock'] = (('iceblue', 'white'), [
    L('Storm-Bound', 'Lightning crackling along the rapier, sparks at its point.',
      [E('bolt_edge', 3.0, 1.6, 0, 1, 1.2, **BOLT), E('stream_strip', 1.0, 0.6, 0.7, 2.0, 0.5, col='c2')], [Fc('bolt_shell', 'all', 14.0, 0.9, flip=(8, 4), fps=22)]),
    L('Live Wire', 'A blue-white charge running in pulses up the blade.',
      [E('sig_band', 1.4, 0.8, 0.8, 0.5, 1.1, along='axis', col='c2'), E('stream_strip', 1.2, 0.8, 0.5, 2.4, 0.8)], [Fc('sk_shock', 'all', 14.0, 0.6, scroll=1.4)]),
    L('Static Haze', 'A faint crackling shimmer round the steel.',
      [E('sig_embers', 2.0, 1.0, 0.5, 0.8, 1.0, along='axis', col='c2')], [Fc('sk_shock', 'band', 16.0, 0.4, scroll=2.0), Fc('haze', 'edge', 14.0, 0.3, scroll=2.0)])])
W['ss_sun'] = (('orange', 'gold'), [
    L('Sun-Bound', 'Orange radiance round the rapier, rays standing off it.',
      [E('ray_strip', 7.0, 0, 8.0, 0.6, 0.45), E('stream_strip', 1.2, 0.8, 1.2, 1.0, 0.8, col='c2')], [Fc('halo_shell', 'all', 18.0, 0.35, pulse=(4.0, 0.7, 1.1))]),
    L('Dawnguard', 'Golden light running along the blade like sunrise on steel.',
      [E('stream_strip', 1.6, 1.0, 1.4, 1.0, 0.9, along='axis', col='c2'), E('sig_band', 2.0, 1.0, 3.0, 0.3, 0.6)], [Fc('sk_enchfill', 'all', 18.0, 0.35, scroll=4.0, col='c2')]),
    L('Solar Edge', 'Bright sunfire licking along the edge.',
      [E('fire_rise1', 3.0, 1.6, 0.6, 1.6, 0.55, along='axis'), E('molten_strip', 1.0, 0.6, 2.0, 1.0, 0.8, col='c2')], [Fc('halo_shell', 'edge', 14.0, 0.3)])])
W['ss_blood'] = (('blue', 'arcane'), [
    L('Arcane-Bound', 'Blue spirit wisps streaming tipward off the rapier.',
      [E('wisp_strip', 4.6, 2.6, 1.8, 0.7, 0.42, along='axis'), E('wisp_strip', 3.4, 1.8, 1.2, 1.1, 0.28, along='axis', col='c2')], [Fc('sk_wisps', 'all', 16.0, 0.3, flip=(2, 2), fps=3)]),
    L('Mana Edge', 'Arcane light running along the edge in bright beads.',
      [E('molten_strip', 1.2, 0.8, 1.6, 1.8, 1.1, col='c2'), E('stream_strip', 1.0, 0.6, 1.0, 1.0, 0.6)], [Fc('sk_enchfill', 'band', 16.0, 0.3, scroll=4.0)]),
    L('Void Mist', 'A dark arcane mist clinging to the blade, blue only at its rim.',
      [E('shade_strip', 4.0, 2.2, 3.0, 0.9, 0.8, along='axis', blend='alpha', dark=True), E('stream_strip', 1.0, 0.6, 1.4, 1.0, 0.7, col='c2')], [Fc('sk_swirls', 'all', 18.0, 0.45, scroll=6.0, blend='alpha', dark=True)])])
W['slayer'] = (('gold', 'white'), [
    L('Slayer\'s Runes', 'The Dragon Slayer\'s runes burning gold under a soft golden mist.',
      [E('sig_runes4k', 2.4, 1.6, 14.0, 0.6, 1.2), E('mist_strip1', 3.6, 2.2, 5.0, 1.0, 0.22)], [Fc('haze', 'edge', 26.0, 0.3, scroll=6.0)]),
    L('Dragonbane', 'Gold light in the bone and a bright edge, steady and proud.',
      [E('molten_strip', 2.0, 1.2, 3.0, 1.0, 1.0)], [Fc('cracks', 'all', 22.0, 1.2), Fc('sk_enchfill', 'band', 28.0, 0.3, scroll=5.0, col='c2')]),
    L('Golden Mist', 'Gold mist pouring off the blade.',
      [E('mist_strip0', 5.0, 2.8, 6.0, 0.8, 0.34), E('mist_strip1', 4.0, 2.4, 4.2, 1.2, 0.26, col='c2')], [Fc('sk_vapour', 'band', 26.0, 0.22, scroll=7.0)])])

LOOKS3 = {k: v[1] for k, v in W.items()}
COLS2 = {k: v[0] for k, v in W.items()}
