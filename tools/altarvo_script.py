"""(Noah, 2026-11-23: "give the echo alter a lot more voice lines that players can interact with like talking with npcs, having a
list of things to say that changes as the mod's story progresses") the Echo Altar's conversation: a Talk row in the altar's menu
opens four topics - the story so far, this city, your class and wonder, who the voice is - and each topic lists what you can ask,
the list changing as the story moves on. You never speak (the Dragonborn is silent, as in Skyrim); the voice answers.

Every prompt has a gate (when it is in the list) and one or more voiced lines (said in order, one after another). The gates:
    freed<N / freed>=N   altars freed in all Skyrim         here_dark / here_free   this city's altar
    noclass / class=sum|des|alc|ssw                         night / day             the hour
    trial=restless|whisper|echo|seals   this city's next trial (still dark)
    wonder_sleeps / wonder_open / wonder_won   your class's wonder: before six cities, its quest under way, in your hands
    wonders_all          all four wonders won (the dragons wake)          slain_slen / slain_tiid / slain_vo   a word dragon dead
    echo_awake           the three dragons dead, the Echo not yet          echo_slain   the curse broken
    party                more than one player in the party
joined with '&'; 'city' prompts are written once per city ({city} rows) plus the shared trial rows.

    python3 tools/altarvo_script.py  ->  docs/altarvo.json  (+ a count of lines and characters to send to ElevenLabs)"""
import json, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
VOICE = 'aNZSbepNRDH1yUk4Q4Of'   # (2026-11-24) Noah's "Feldir Echo Alter": Felldir the Old, an old man (was the female "Echo Alter")

T = []   # topics


def topic(tid, title, about):
    T.append({'id': tid, 'title': title, 'about': about, 'prompts': []})


def ask(pid, gate, question, *lines):
    T[-1]['prompts'].append({'id': pid, 'gate': gate, 'ask': question, 'lines': list(lines)})


# --------------------------------------------------------------------------------------------------------------------------
topic('greet', 'Greeting and farewell', "Said as the Talk row opens (one picked for the story's stage) and as you leave it.")
ask('greet_faint', 'freed<2', '(opening the Talk row)',
    '[whispers] ...you... can you hear...? The stone is... too far from the other stones...',
    '[whispers] ...closer... wake another... and I will be clearer...')
ask('greet_early', 'freed>=2&freed<6', '(opening the Talk row)',
    '[softly] You came back to the stone. Good. Ask, and I will answer what I can.',
    '[softly] Dragonborn. The night is long... and I have had a long time to think. Ask.')
ask('greet_named', 'freed>=6&echo_slain=0', '(opening the Talk row)',
    '[softly] Dragonborn. Felldir hears you. What would you know?',
    'You have done more than I hoped. Speak, and I will listen.')
ask('greet_end', 'echo_slain', '(opening the Talk row)',
    '[softly] ...still here, Dragonborn. Not for long, I think. Ask, while the stone still holds me.')
ask('greet_night', 'night&here_dark', '(opening the Talk row at night, the city still dark)',
    '[urgent whisper] The dead are walking. Be quick... they do not wait for talk.')
ask('greet_dawn', 'day&here_dark', '(opening the Talk row by day, the city still dark)',
    'The sun holds them for now. Use the light. Rest, ready yourself... the night will not be kind.')
ask('greet_freed', 'here_free&echo_slain=0', '(opening the Talk row at a freed altar)',
    '[softly] This stone is awake, and its city sleeps. It is good to speak from a quiet place.',
    'Look up... no red light falling here. Only stars. Ask, Dragonborn.')
ask('greet_priest', 'here_free&priest_new', '(opening the Talk row just after this city\'s priest fell)',
    '[relieved] He is gone. I felt his oath break from across Skyrim. The stone is singing.')
ask('bye', 'always', '(leaving the Talk row)',
    '[softly] Go. I will be in the stone.',
    'Walk carefully. The dead remember the living.',
    '[sighs] Until the next stone.',
    'The night will come again. Be ready for it.')
ask('nothing_new', 'always', '(every prompt in a topic already heard)',
    '[softly] I have told you all I know of that. For now.',
    'Nothing more... not yet. Free another stone, and I may remember more.')

# --------------------------------------------------------------------------------------------------------------------------
topic('story', 'The story so far', 'What has happened, and what comes next - the list grows with each act.')
ask('s_what', 'always', 'What has happened to Skyrim?',
    '[softly] Helgen burned. The World-Eater flew north over the smoke... and as he flew, he shouted into the earth.',
    'Three words. SLEN... TIID... VO. Flesh. Time. Undo. The shout he raises his dragons with.',
    'But this time it fell on every grave in Skyrim. And every grave heard it.')
ask('s_dead', 'always', 'Why do the dead come back?',
    'His shout hangs like a mist over every road the dead walk. Shor\'s hall, Arkay\'s mercy, whatever waits for them... none of them can pass it.',
    '[sad] They try, every night. The mist turns them back... and his voice throws them down to Skyrim again, as red light out of the sky.')
ask('s_beams', 'always', 'What are the red beams in the sky?',
    'The dead, falling. Turned back from Sovngarde, and thrown down wherever his voice is strongest.',
    '[tense] The red lightning is worse. That is how the strong ones come... the ones his voice holds tightest.')
ask('s_altars', 'always', 'What are these altars?',
    'Echo Altars. The old Tongues raised one in every hold, long before your Empire, to answer a dragon\'s voice with their own.',
    'When the World-Eater shouted, they went dark. Wake one, and its city\'s dead will sleep again.')
ask('s_how', 'freed<2', 'How do I wake an altar?',
    '[whispers] ...four trials... at night... the dead... the stone will show you...',
    '...one city at a time... and then I will be clearer...')
ask('s_me', 'always', 'Why can I do this?',
    'You have a dragon\'s soul in a mortal\'s body. When you kill one of the dead, the stone hears it the way it hears a shout.',
    'The Tongues would have given anything to be what you are. [softly] Do not waste it.')
ask('s_essence', 'always', 'What is the Cursed Essence?',
    'What is left of his shout in the dead. When they fall, it comes out of them... and it comes to you.',
    'The stone can make it into steel, and spells, and brews. Spend it. It does no good in your pocket.')
ask('s_fall', 'always', 'What happens if I fall?',
    'The dead will drag you down. If anyone stands with you, they can lift you again. If no one does... [pause] the night ends without you.',
    'Do not let it. I have lost enough people to the dark.')
ask('s_tablet', 'freed>=1', 'What is the Tablet of Mysteries?',
    'An old thing, and a strange one. The Tongues left weapons with it that they did not want to keep.',
    'Feed it the essence, and it will give you one. It does not tell you which. [softly] Neither will I.')
ask('s_shouts', 'freed>=2', 'Why do the cities teach me shouts?',
    'Every freed city remembers a little of the Voice. They teach it to the one who freed them.',
    'Use them. A shout is a weapon the dead cannot answer.')
ask('s_two', 'freed>=2&freed<4', 'What has changed?',
    'Two stones awake, and their voices carry to each other. That is how you hear me now.',
    'Every stone you wake makes the mist thinner. Not gone... but thinner.')
ask('s_cities', 'freed>=2&freed<6', 'Why one city at a time?',
    'Each altar answers for its own hold. The dead of Whiterun do not care that Riverwood sleeps.',
    'And each city has its keeper. A priest who swore an oath to the World-Eater... and remembers it.')
ask('s_priests', 'freed>=2', 'Who are the Dragon Priests?',
    'Men who served the dragons when the dragons ruled. They wore masks of the dragons\' faces, and took the dragons\' power.',
    '[bitter] When the shout came, every one of them woke with his oath still in his mouth. They keep the altars dark for their master.',
    'Break a city\'s seals, and its priest must come out to face you.')
ask('s_four', 'freed>=4&freed<6', 'Are we close?',
    'Four stones. Five. The mist trembles when the sixth wakes... I can feel it already.',
    'At six... I will be able to tell you who I am. And something old will begin to stir.')
ask('s_wonders', 'freed>=6&wonders_all=0', 'What stirs at six altars?',
    'The wonders. Four weapons, one for each path, hidden when the Tongues still walked. They sleep no longer.',
    'Each waits for the one who walks its path. But none of them will simply be found. Each must be earned.',
    'When yours is won, the Greybeards will call you to the mountain. The wonder waits beneath their hall.')
ask('s_greybeards', 'freed>=6&wonders_all=0', 'What do the Greybeards have to do with this?',
    'High Hrothgar sits on the Throat of the World. Where the Tongues cast the World-Eater out of time.',
    'Beneath it is a room the Greybeards guard and do not speak of. The wonders are kept there, until they are won.')
ask('s_echo_what', 'freed>=10', 'What is the echo on the summit?',
    'When we cast him out of time, his voice did not all go with him. Part of it stayed behind, caught on the summit where we broke him.',
    'It has circled there since, waiting. Now that he is back in the world, it answers him... and it is louder than ever.',
    'He flies where he likes. The echo cannot leave the mountain. That is why you can reach it... and why breaking it breaks his hold on the dead.')
ask('s_ten', 'freed>=10&echo_slain=0', 'Every altar is awake. Is it over?',
    '[softly] Every stone sings again. Listen... all of Skyrim answering at once. I have not heard that in a very long time.',
    'But the World-Eater\'s voice is not gone. It lingers in the wild places... and in the echo he left on the summit.',
    'The cities are safe. The rest of Skyrim is not. Not yet.')
ask('s_dragons', 'wonders_all&echo_awake=0', 'The wonders are won. What now?',
    '[tense] Now the words wake. Each of the three words of his shout was carried by a dragon. They are flying again.',
    'SLEN in the Reach. TIID under Labyrinthian. VO on the peak of Mount Anthor. Silence all three.',
    'Each will come at night, with a siege of the dead before it. And each one gives back what it stole.')
ask('s_slen', 'wonders_all&slain_slen=0', 'Tell me of SLEN.',
    'Slenvahrin. The Flesh-Wyrm. It circles the cult\'s old shrine in the Reach, and the barrow-dead around it grow new flesh.',
    'Kill it, and the strength the World-Eater poured into the dead will run in you instead.')
ask('s_tiid', 'wonders_all&slain_tiid=0', 'Tell me of TIID.',
    'Tiidrah. The Time-Wyrm. It sleeps under Labyrinthian, where the cult laid its dead by the thousand.',
    'It gives the dead back the years the grave took. Take them from it... and the time is yours.')
ask('s_vo', 'wonders_all&slain_vo=0', 'Tell me of VO.',
    '[fearful] Vonahlok. The Undoing. It keeps the last word, on the frozen peak of Mount Anthor.',
    'Where it flies, death itself comes undone. It is the oldest of the three... and the angriest.')
ask('s_slen_done', 'slain_slen', 'Slenvahrin is dead.',
    'I felt it fall. SLEN is silent... the dead in the Reach are only bones again. You are stronger for it.')
ask('s_tiid_done', 'slain_tiid', 'Tiidrah is dead.',
    'TIID is silent. The time the dead stole is yours now. [softly] Spend it better than they did.')
ask('s_vo_done', 'slain_vo', 'Vonahlok is dead.',
    '[relieved] The Undoing has undone its last. I never thought I would hear VO go quiet.')
ask('s_echo', 'echo_awake', 'What is left?',
    'Only their maker. On the summit of the Throat of the World, where we cast him out of time, his echo still circles.',
    'It is not the World-Eater himself. Only the shape his voice left in the sky. [pause] It is enough to kill you.',
    'Break it... and the curse breaks with it. Every grave in Skyrim will be quiet.')
ask('s_echo_how', 'echo_awake', 'How do I beat the Echo?',
    'Bring everything you have earned. The wonder. The words you took from the three. And anyone who will stand with you.',
    'It will call the dead of all Skyrim to the summit. Do not let them pull you away from it.')
ask('s_after', 'echo_slain', 'Is it over?',
    '[softly] The echo is gone from the sky. The mist is lifting... I can see the road to Sovngarde from here.',
    'The dead of Skyrim are walking it. All of them. They are going home.',
    'The nights are only nights again, Dragonborn. You did that.')
ask('s_hall', 'echo_slain', 'What is the Hall of the Awakened?',
    'A room beneath High Hrothgar that opened when the echo broke. The Tongues built it for whoever ended this.',
    'It is yours now. Go and see it.')

# --------------------------------------------------------------------------------------------------------------------------
topic('city', 'This city', "About the city the altar stands in: its dead, its Dragon Priest, its trials - and once it's free, what is left.")
ask('c_trials', 'here_dark', 'How do I wake this altar?',
    'Four trials, in order, and only at night. Lay its restless dead to rest. Follow the whisper. Repeat the echo. Break the seals.',
    'You need not finish them in one night. The stone remembers what you have done.')
ask('c_day', 'here_dark&day', 'Why wait for nightfall?',
    'The dead sleep in the day, and the trials are for the dead. Come back when the sun goes down.')
ask('c_restless', 'here_dark&trial=restless', 'What is Lay the Restless to Rest?',
    'The first of this city\'s dead are already up and wandering. Put them down near the stone... while its light is on you.',
    'Stay close to the altar. The further you stray from its light, the less it counts.')
ask('c_whisper', 'here_dark&trial=whisper', 'Where does the whisper lead?',
    'I will give you a riddle. It names a place in this city where the dead are gathering. Go there, and kill them where they gather.',
    'Listen to the riddle twice if you need it. The stone will say it again.')
ask('c_echo', 'here_dark&trial=echo', 'How do I repeat the echo?',
    'A ring of six stones stands around the altar. They will sing, one after another. Strike them in the order they sang.',
    '[softly] Miss a note, and they go quiet until the next hour. Listen first. Then answer.')
ask('c_seals', 'here_dark&trial=seals', 'Where are the priest\'s seals?',
    'Three seals bind this altar, each guarded by one of his dead. They are marked on your map.',
    'Kill the guard first. Only then will the seal break under your blow. And when the last one breaks... he comes. At once.')
ask('c_coop', 'here_dark&party', 'Do my companions share the trials?',
    'Every blade near the stone counts. What one of you does, the stone remembers for all of you.')

CITY = {
    'Riverwood': (
        ['Helgen is just over the hill, where the dragon first broke the sky over you. This valley\'s dead were the first to hear his shout.',
         'The barrow above the town, Bleak Falls... its dead are restless. They come down the hill every night.'],
        ['Vahlok the Jailor. In the old days he kept the cult\'s prisoners. His oath to the World-Eater was the last he ever swore.',
         'He has crossed the sea to keep this valley dark. A jailor keeps his doors locked. Break them.'],
        ['[softly] The mill wheel turns, and no red light falls over the barrow. Listen... it is a quiet valley again.',
         'This was the first stone you woke. I will remember that.']),
    'Whiterun': (
        ['Whiterun was built around a dragon\'s cage. Its Hall of the Dead lies under the Jarl\'s, and its beds are empty.',
         'The Gildergreen remembers the dead who sat beneath it. They come back to it now, looking for shade they cannot feel.'],
        ['Nahkriin. He guarded Skuldafn for the World-Eater, the gate to Sovngarde itself.',
         '[bitter] The mist that bars Sovngarde\'s door is his to keep. He has come down to the plains so that Whiterun\'s dead never find the way.'],
        ['The Gildergreen is quiet. The plains are only grass again.',
         'Nahkriin\'s gate has one less keeper. The road to Sovngarde is a little clearer for it.']),
    'Solitude': (
        ['The capital. Its crypts are older than its walls, and the Blue Palace stands over them.',
         'The bards here teach the young to sing of the dead. Tonight the dead have come to listen.'],
        ['Volsung of Volskygge. His mask let him breathe where no man could, high on the peaks above Haafingar.',
         'He climbs down to Solitude now, and the red light falls over the palace at his word.'],
        ['The arch over the sea is quiet. The bards can sing of the dead again, instead of to them.',
         '[softly] Solitude will tell this story for a hundred years. They will get it wrong. Let them.']),
    'Windhelm': (
        ['The oldest city of men. Ysgramor\'s stone, and the snow that never leaves the palace of kings.',
         'There is a candle in this city that has never gone out. The dead are drawn to it, like moths.'],
        ['Zahkriisos, the Frost-Priest. He slept on Solstheim until the shout crossed the Sea of Ghosts.',
         'He walked over the ice to the docks. The cold he carries stops a heart. Keep moving, and keep warm.'],
        ['The candle still burns, and nothing comes to it but the living.',
         'Windhelm has buried enough of its own. It can rest now.']),
    'Markarth': (
        ['The deep elves carved this city out of the cliff, and the cliffs bleed silver. The dead here are older than the Nords.',
         'Its Hall of the Dead goes deeper than its priests have ever been. Something down there heard the shout.'],
        ['Otar the Mad. He was so cruel that his own cult sealed him alive in Ragnvald.',
         '[fearful] His oath outlived his reason. He comes down in red lightning, with nothing left of himself but the oath.'],
        ['The stone city is still. Even the Reach\'s oldest dead have gone back to sleep.',
         'Otar is gone. I will not mourn him. Neither did his cult.']),
    'Riften': (
        ['A city of thieves and green water. Red light falls into the canal now, and the drowned come down in it. Nobody sees them coming.',
         'Mara\'s temple stands here. Love is a strong thing... but not stronger than the World-Eater\'s voice.'],
        ['Rahgot of Forelhost. When King Harald\'s army came for him, his followers poisoned themselves rather than give him up.',
         'They rise with him now. The Rift\'s dead follow them down to the city.'],
        ['The canal is only water again. The thieves can go back to stealing from the living.',
         'Rahgot\'s followers died for him twice. [softly] Let them stay dead this time.']),
    'Falkreath': (
        ['More dead than living lie in this town\'s earth. Tonight they are restless in their rows.',
         'The graveyard is the oldest thing here. Older than the town that grew around it.'],
        ['Krosis of Shearpoint. He guarded his word on the mountain, mourning, for a thousand years.',
         '[sad] The shout gave him something to do with his sorrow. He comes to keep this town\'s children out of the ground.'],
        ['The rows are quiet. Falkreath can bury its dead and leave them buried.',
         'Krosis grieved for a thousand years. I hope he has stopped.']),
    'Morthal': (
        ['The marsh hides its dead well. Fog, roots, black water... and red light falling into all of it.',
         'There is a brewer\'s hut at the marsh\'s edge. The swamp\'s dead come to drink, as if they remember thirst.'],
        ['Hevnoraak of Valthume. His own servants sealed him in his tomb to stop him rising.',
         'The seal could not hold against SLEN TIID VO. He walks out of the fog, and the drowned walk behind him.'],
        ['The fog is only fog again. Morthal can sleep with its doors unbarred.',
         'Hevnoraak\'s servants were right to seal him. You finished what they started.']),
    'Dawnstar': (
        ['A cold harbour under a cold sky. The people here have been dreaming badly for a long time.',
         'The miners cut quicksilver out of the hill. Tonight the red light falls on the mouth of the mine.'],
        ['Vokun of High Gate. A priest of shadows. Dawnstar\'s dreamers dreamed his return a thousand years before it came.',
         'Kill him, and the nightmares end. He knows that. He will not make it easy.'],
        ['The harbour is frozen and still. Tonight, Dawnstar will sleep without dreaming.',
         '[softly] The nightmares are over here. For once, the dreamers will wake rested.']),
    'Winterhold': (
        ['What is left of a city the sea took. The College stands across the bridge, and the town does not trust it.',
         'One hearth still welcomes the College\'s mages. Tonight the dead have found it too.'],
        ['Morokei of Labyrinthian. He once held the Staff of Magnus, and the College has feared him ever since.',
         'He waits on the cliffs above the town. The storm he calls has not finished yet.'],
        ['The storm has passed. The bridge to the College is only a bridge again.',
         'Morokei is gone, and the College will claim the credit. Let it.']),
}
LORE = {
    'Riverwood': 'When we raised this stone, there was no town here. Only the river, and a dragon\'s nest on the mountain above it.',
    'Whiterun': 'The dragon in Dragonsreach was not the first caged on that hill. The first was there in our time. We heard it screaming for a year.',
    'Solitude': 'There were Tongues who lived on these cliffs, and taught their students to shout across the water. The sea still remembers the sound.',
    'Windhelm': 'Ysgramor\'s people built over the stone without knowing it was here. It has listened to every king of Windhelm since.',
    'Markarth': 'The deep elves had a stone of their own, under the cliff. Ours answered it once. [softly] Nothing answers from down there now.',
    'Riften': 'The lake here was a holy place to Mara before there was a city. We raised the stone where her pilgrims rested.',
    'Falkreath': 'Even in our time, people came here to bury their dead. Some places are made for grief.',
    'Morthal': 'The marsh swallowed the first stone we raised here. This is the second. We were careful where we put it.',
    'Dawnstar': 'There was a shrine in the hills here to a god of dreams. We built the stone well away from it. We were right to.',
    'Winterhold': 'This stone once stood in a great city. The sea took the city. The stone held on. [softly] So did the College.',
}
for city, (what, priest, free) in CITY.items():
    k = city.lower()
    ask(f'c_{k}_what', f'here={k}&here_dark', f'What happened to {city}?', *what)
    ask(f'c_{k}_priest', f'here={k}&here_dark&freed>=2', f'Who keeps {city}\'s altar dark?', *priest)
    ask(f'c_{k}_free', f'here={k}&here_free', f'Is {city} safe now?', *free)
    ask(f'c_{k}_old', f'here={k}&freed>=4', f'What was {city} like in your time?', LORE[city])

# --------------------------------------------------------------------------------------------------------------------------
topic('class', 'Your class and wonder', 'Before a class is chosen, the four paths; after, what the voice sees in you, your craft, and your wonder.')
ask('k_paths', 'noclass', 'What are the four paths?',
    'The stone offers four ways to fight the dead. The Tongues walked all of them, once.',
    'The Summoner, who calls the dead\'s own kind to fight them. The Destruction mage, who burns them where they stand.',
    'The Alchemist, who bottles every element and pours it out. The Spellsword, steel in one hand and a spell in the other.',
    'Choose at the stone. Choosing again later costs dearly, so choose well.')
ask('k_change', 'freed>=1&class=any', 'Can I change my path?',
    'The stone will let you. It will cost you fifty thousand of the essence the dead leave behind.',
    'Your kit stays your own. But your path\'s powers stay with the path.')
CLS = {
    'sum': ('Summoner', "the Soulcaller's Staff",
            ['You call things to fight for you, and they come. The Tongues had a word for people like you. It was not a kind word.',
             '[softly] But it was a useful one. The dead fear what does not fear them.'],
            ['Let your servants hold the line. You are the one who must not fall. Without you, they go back where they came from.',
             'The more you call, the thinner you are spread. Be wise about it.'],
            ['Listen... the ghost-cats that guarded the Tongues still prowl. Their souls are scattered. Gather them.',
             'Follow them quietly at night, near a freed stone. They fear the hunter. They will come to a patient one.'],
            ['The Soulcaller\'s Staff. Every soul it takes is one the dead will never get back.',
             'At twenty souls the pride answers. [softly] They remember who freed them.']),
    'des': ('Destruction mage', 'the Wyrmspine Javelin',
            ['Fire. Frost. Storm. You do not ask the dead to sleep. You make them.',
             '[softly] The Tongues had mages like you. The dragons learned to fear them... for a while.'],
            ['Your power is in your magicka, and your magicka runs out. Never let the dead catch you empty.',
             'Each element has its use. Fire for the crowd. Frost to slow them. Storm for whatever is still standing.'],
            ['A god\'s eye fell to Nirn, and the curse buried its light. The Eye of Magnus... its pieces are falling again.',
             'When a star crosses the sky above you at night, strike it down. That is where the path begins.'],
            ['The Wyrmspine Javelin. Carved from a dragon\'s spine, and it remembers the sky.',
             'Throw it hard. It wants to fly again.']),
    'alc': ('Alchemist', 'the Mutagen Staff',
            ['You bottle what the rest of us fear. Fire in glass, frost in glass... [softly] even, one day, the curse itself.',
             'The Tongues never trusted alchemists. They trusted their potions well enough.'],
            ['Your flasks are your strength. Know which element the dead fear, and give them that one.',
             'Never stand still to brew. The dead do not wait for anything to cool.'],
            ['Every element can be bottled. Brew the Elixir of Aetherius... and the Mutagen Staff will answer you.',
             'One of the dead burns brighter than the rest. Break it with glass... and catch what falls.'],
            ['The Mutagen Staff. Six vials of the world\'s elements, poured through one orb.',
             'The Elixir of Aetherius was only ever a story. You made it true.']),
    'ssw': ('Spellsword', 'the Dragon Slayer',
            ['Steel in one hand, a spell in the other. You fight the way the first Tongues fought, before they had the Voice.',
             '[softly] You get closer to the dead than anyone should. They feel it.'],
            ['Your blade carries your spell. Choose the spell before you choose the fight.',
             'Get close, and stay close. A Spellsword at the edge of a fight is only half of one.'],
            ['A slayer once swore an oath and broke his blade on the World-Eater\'s scales. The pieces are scattered.',
             'Take up his oath. Where the World-Eater first returned, a broken blade waits for a hand. Begin there.'],
            ['The Dragon Slayer. Every blow carries the spell in your other hand.',
             'The slayer\'s oath is kept. [softly] He would have been glad of you.']),
}
OLD = {
    'sum': 'One of us could call the wolves down from the mountain with a word. They followed her to the summit. None of them came back.',
    'des': 'Our fire-callers burned the dragons\' priests out of their towers. The dragons answered in kind. Fire was cheap in those days.',
    'alc': 'We had a healer who brewed in a skull, because it was the only bowl she had. She kept more of us alive than the Voice did.',
    'ssw': 'Before the Voice, we had steel and a little magic, and we fought dragons with that. Most of us died. The ones who lived taught the rest.',
}
for c, (name, wonder, seen, craft, quest, won) in CLS.items():
    ask(f'k_{c}_seen', f'class={c}', 'What do you see in me?', *seen)
    ask(f'k_{c}_craft', f'class={c}', f'Teach me of the {name}\'s craft.', *craft)
    ask(f'k_{c}_sleep', f'class={c}&wonder_sleeps', 'Is there a wonder for my path?',
        f'There is. {wonder[0].upper() + wonder[1:]}. But it sleeps, and it will not wake until six stones are free.')
    ask(f'k_{c}_quest', f'class={c}&wonder_open', 'Where does my path lead?', *quest)
    ask(f'k_{c}_won', f'class={c}&wonder_won', 'My wonder is won.', *won)
    ask(f'k_{c}_old', f'class={c}&freed>=4', 'Did the Tongues fight like me?', OLD[c])
ask('k_party', 'party&freed>=1', 'What of my companions?',
    'Each of you walks a path of your own. Together, you are what the Tongues were. [softly] Stay together.',
    'If one of you falls, the rest of you lift them. The dead do not take prisoners.')

# --------------------------------------------------------------------------------------------------------------------------
topic('voice', 'Who the voice is', 'The voice tells you more of itself with each stage - its name only once six altars are free.')
ask('v_who0', 'freed<2', 'Who are you?',
    '[whispers] ...someone who has waited... too long... wake another stone...')
ask('v_who2', 'freed>=2&freed<6', 'Who are you?',
    'Someone who was here when these stones were raised. That is all I can give you... for now.',
    '[softly] My name is in the mist with everything else. Wake more stones, and it will come back to me.')
ask('v_where2', 'freed>=2&freed<6', 'Where are you?',
    'Far. Behind the mist he left across the road to Sovngarde. I speak through the stones because I cannot speak any other way.')
ask('v_old', 'freed>=2', 'How old are these stones?',
    'Older than your Empire. Older than the first Nord kings. They were raised while dragons still ruled the sky.',
    'We raised them in secret, in the dark, where the dragons\' priests would not see.')
ask('v_three', 'freed>=4', 'Who raised the stones?',
    'The three of us. The first who learned the Voice... and turned it on our masters.',
    'One fell at the summit, before the end. One I have not seen since. And I... I am still here.')
ask('v_fell', 'freed>=4', 'Who fell at the summit?',
    '[sad] A warrior with a golden hilt. She was the bravest of us. She went at him first, and he took her first.',
    'I watched her fall. I did not stop. [softly] That is what she would have wanted.')
ask('v_lost', 'freed>=4', 'And the other one?',
    'A warrior with one eye, who saw more than most with two. After the summit... I lost him in the mist.',
    'Perhaps he is in Sovngarde, waiting for the road to open. I hope so.')
ask('v_name', 'freed>=6', 'What is your name?',
    'Six stones are free... and I remember. I am Felldir. Felldir the Old.',
    '[softly] It has been a very long time since anyone asked me that.')
ask('v_scroll', 'freed>=6', 'What did you do at the summit?',
    'I read the Elder Scroll. The others held him in the sky, and I read it... and the World-Eater was cast out of time.',
    'We thought it was the end. We were young. We did not know that time has a way of giving things back.')
ask('v_wait', 'freed>=6', 'Why are you still here?',
    'I lived a long life after the summit, and died old, and took the road to Sovngarde. When he shouted at Helgen, the mist fell over it... with me still on it.',
    'I cannot go forward, and I will not go back. So I wait... and I speak through the stones we raised.')
ask('v_alone', 'freed>=6', 'Have you been alone all this time?',
    '[sad] The dead pass me in the mist. They cannot see the way. Neither can I. We are all lost together.',
    'Then you came. [softly] The first living voice I have heard in a very long time.')
ask('v_alduin', 'freed>=6', 'What was Alduin like?',
    'Proud. Patient. He did not hate us. You do not hate the grass you walk on.',
    'That is what frightened me most. Not his anger... his certainty.')
ask('v_dragonborn', 'freed>=6', 'Did you know there would be a Dragonborn?',
    'We hoped. The Scroll told us he would come back. It did not tell us who would be waiting when he did.',
    '[softly] I am glad it was you.')
ask('v_greybeards', 'freed>=6', 'Do you know the Greybeards?',
    'They came after us. They took the Voice and made it quiet, and climbed the mountain to keep it safe.',
    'We used it to fight. They use it to listen. [softly] Perhaps they are wiser than we were.')
ask('v_dragons', 'freed>=6', 'Do you hate the dragons?',
    'I did. For a long time. Then I was dead for longer than I was ever alive, and hate is hard to carry that far.',
    'Now I only want them to stop. That is not the same thing.')
ask('v_deadwant', 'freed>=2', 'What do the dead want?',
    'To go home. Nothing more. They are not cruel... they are lost, and the World-Eater\'s voice is all they can hear.',
    '[sad] When you put them down, you are not killing them. You are showing them the way.')
ask('v_after', 'echo_slain', 'What will happen to you now?',
    '[softly] The road is open. I can see Sovngarde\'s light from here... and someone with a golden hilt, waiting.',
    'I will walk it soon. The stones will stay. Their voice will be quieter without me.',
    '[softly] Thank you, Dragonborn. Go well.')

if __name__ == '__main__':
    n = sum(len(p['lines']) for t in T for p in t['prompts'])
    chars = sum(len(l) for t in T for p in t['prompts'] for l in p['lines'])
    out = {'about': __doc__.split('\n\n')[0], 'voice': VOICE, 'model': 'eleven_v3', 'topics': T}
    (ROOT / 'docs' / 'altarvo.json').write_text(json.dumps(out, indent=1, ensure_ascii=False) + '\n')
    # (2026-11-24: "Re-record them") every line Felldir says, for tools/altarvo.py: the talk lines (av_<prompt>_<n>) and the
    # altar's existing whispers (echo_<key>, the names of assets/sound/whisper's files) - eggs.json, Eggs.cpp's signs and reveals
    import re
    L = [{'id': f"av_{p['id']}_{i + 1}", 'group': t['title'], 'ask': p['ask'], 'text': l}
         for t in T for p in t['prompts'] for i, l in enumerate(p['lines'])]
    E = json.loads((ROOT / 'sheets' / 'eggs.json').read_text())
    for e in E['rows']:
        v = e['voice']
        L.append({'id': f'echo_{v}_open', 'group': 'Whispers: ' + e['title'], 'ask': 'the quest opens', 'text': e['open']})
        for k, st in enumerate(e['steps'], 1):
            L.append({'id': f'echo_{v}_s{k}', 'group': 'Whispers: ' + e['title'], 'ask': f'step {k}', 'text': st['hint']})
            if st.get('done'): L.append({'id': f'echo_{v}_s{k}_done', 'group': 'Whispers: ' + e['title'], 'ask': f'step {k} done', 'text': st['done']})
        L.append({'id': f'echo_{v}_final', 'group': 'Whispers: ' + e['title'], 'ask': 'the quest done', 'text': e['final']})
    W = E['whisper']
    L.append({'id': 'echo_asleep', 'group': 'Whispers', 'ask': 'the wonders sleep', 'text': W['asleep']})
    L.append({'id': 'echo_next', 'group': 'Whispers', 'ask': 'before a city riddle', 'text': W['next']})
    for c, txt in W['cities'].items(): L.append({'id': f'echo_city_{c.lower()}', 'group': 'Whispers: cities', 'ask': c, 'text': txt})
    src = (ROOT / 'src' / 'Eggs.cpp').read_text()
    def arr(name):
        m = re.search(name + r'\[\d\] = \{(.*?)\};', src, re.S)
        return [x.encode().decode('unicode_escape') for x in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))]
    for v, txt in zip(['pride', 'eye', 'stone', 'oath'], arr('kSignLine')):
        L.append({'id': f'echo_{v}_sign', 'group': 'Whispers: signs', 'ask': v, 'text': txt})
    for k6, txt in zip([2, 4, 6], arr('kRevealLine')):
        L.append({'id': f'echo_voice_{k6}', 'group': 'Whispers: reveals', 'ask': f'{k6} altars freed', 'text': txt})
    whs = sorted(f.stem for f in (ROOT / 'assets' / 'sound' / 'whisper').glob('echo_*.wav'))
    missing = [w for w in whs if w not in {l['id'] for l in L}]
    if missing: raise SystemExit(f'whispers with no text found: {missing}')
    (ROOT / 'docs' / 'altarvo_lines.json').write_text(json.dumps({'voice': VOICE, 'model': 'eleven_v3', 'lines': L}, indent=1, ensure_ascii=False) + '\n')
    print(f"altarvo_lines.json: {len(L)} lines, {sum(len(l['text']) for l in L):,} characters")
    print(f'{sum(len(t["prompts"]) for t in T)} prompts, {n} lines, {chars} characters')
    for t in T: print(' ', t['id'], len(t['prompts']), 'prompts', sum(len(p['lines']) for p in t['prompts']), 'lines')
