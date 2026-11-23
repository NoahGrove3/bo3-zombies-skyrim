"""(Noah, 2026-11-23: "option 2") the class voice lines made through the ElevenLabs API, on Noah's PC.

The API key is read from a file Noah makes himself (one line, the key) and is never printed, logged or written anywhere else.
Every step that spends ElevenLabs credits says so and how many characters, and runs only with --go.

  python3 elevenvo.py <key file> <out dir> check                 # free: is the API reachable, is the key good, credits left
  python3 elevenvo.py <key file> <out dir> design  [--go]        # 3 previews for each of the 8 voices (docs/classvo_voices.json)
  python3 elevenvo.py <key file> <out dir> keep sum male 2       # make preview 2 the Summoner's male voice (free)
  python3 elevenvo.py <key file> <out dir> lines   [--go] [ids]  # every line not made yet (or just these ids), male and female
  python3 elevenvo.py <key file> <out dir> review                # free: review.html - play each line, Approve or Redo
  python3 elevenvo.py <key file> <out dir> redo <redo.txt> [--go]  # make again the lines the review marked Redo
Lines come from docs/classvo_full.json: "[tag] line", model eleven_v3, mp3 44.1 kHz. Files: <id>_<male|female>.mp3 (the doc's names).
"""
import base64, json, pathlib, sys, urllib.error, urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
API = 'https://api.elevenlabs.io'
MODEL = 'eleven_v3'


def key_from(path):
    k = pathlib.Path(path).read_text(encoding='utf-8-sig').strip()
    if not k or ' ' in k: sys.exit('the key file should hold just the key, on one line')
    return k


def call(key, method, path, body=None, raw=False):
    req = urllib.request.Request(API + path, method=method, headers={'xi-api-key': key, 'Content-Type': 'application/json', 'Accept': '*/*'},
                                 data=json.dumps(body).encode() if body is not None else None)
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            d = r.read()
            return d if raw else json.loads(d or b'{}')
    except urllib.error.HTTPError as e:
        sys.exit(f'ElevenLabs said {e.code} on {path}: {e.read()[:400].decode("utf-8", "replace")}')
    except urllib.error.URLError as e:
        sys.exit(f'could not reach ElevenLabs ({e.reason}) - is api.elevenlabs.io allowed in the network settings?')


def lines():
    return json.loads((ROOT / 'docs/classvo_full.json').read_text(encoding='utf-8'))['lines']


def text_of(l):
    return (f"[{l['tag']}] " if l['tag'] else '') + l['text']


def main(a):
    if len(a) < 3: sys.exit(__doc__)
    key, out, cmd, rest = key_from(a[0]), pathlib.Path(a[1]), a[2], a[3:]
    go = '--go' in rest
    rest = [r for r in rest if r != '--go']
    out.mkdir(parents=True, exist_ok=True)
    vfile = out / 'voices.json'  # class/sex -> voice_id (made by keep)
    voices = json.loads(vfile.read_text()) if vfile.exists() else {}

    if cmd == 'check':
        u = call(key, 'GET', '/v1/user/subscription')
        left = u.get('character_limit', 0) - u.get('character_count', 0)
        print(f"API reachable, key good. Plan {u.get('tier')}: {left:,} characters left this period (of {u.get('character_limit', 0):,}).")
        n = sum(len(text_of(l)) for l in lines()) * 2
        print(f"One full pass of the 320 lines, male and female: about {n:,} characters.")
        return

    if cmd == 'design':
        P = json.loads((ROOT / 'docs/classvo_voices.json').read_text())
        sample = "The dead are waking. Another night, another fight. I hold this place until dawn, whatever comes out of the ground."
        cost = len(sample) * 8
        if not go: return print(f'design: 8 voices x 3 previews, about {cost:,} characters. Run again with --go to spend them.')
        for c, by in P.items():
            for sex, prompt in by.items():
                d = call(key, 'POST', '/v1/text-to-voice/design', {'voice_description': prompt, 'text': sample, 'model_id': 'eleven_ttv_v3'})
                for i, p in enumerate(d.get('previews', []), 1):
                    (out / 'design').mkdir(exist_ok=True)
                    (out / 'design' / f'{c}_{sex}_{i}.mp3').write_bytes(base64.b64decode(p['audio_base_64']))
                    (out / 'design' / f'{c}_{sex}_{i}.id').write_text(p['generated_voice_id'])
                print(f'{c} {sex}: {len(d.get("previews", []))} previews')
        return

    if cmd == 'keep':
        c, sex, i = rest[0], rest[1], int(rest[2])
        gid = (out / 'design' / f'{c}_{sex}_{i}.id').read_text().strip()
        P = json.loads((ROOT / 'docs/classvo_voices.json').read_text())
        names = {'sum': 'Summoner', 'des': 'Destruction', 'alc': 'Alchemist', 'ssw': 'Spellsword'}
        v = call(key, 'POST', '/v1/text-to-voice', {'voice_name': f'CoA {names[c]} {"M" if sex == "male" else "F"}', 'voice_description': P[c][sex], 'generated_voice_id': gid})
        voices[f'{c}_{sex}'] = v['voice_id']
        vfile.write_text(json.dumps(voices, indent=1))
        return print(f'{names[c]} {sex}: preview {i} kept as a voice')

    if cmd in ('lines', 'redo'):
        want = None
        if cmd == 'redo':
            want = {w.strip() for w in pathlib.Path(rest[0]).read_text().split() if w.strip()}
        elif rest:
            want = set(rest)
        jobs = []
        for l in lines():
            for sex in ('male', 'female'):
                f = out / 'lines' / f"{l['id']}_{sex}.mp3"
                tag = f"{l['id']}_{sex}"
                if want is not None and tag not in want and l['id'] not in want: continue
                if want is None and f.exists(): continue
                if f'{l["class"]}_{sex}' not in voices: sys.exit(f'no {l["class"]} {sex} voice kept yet (run design, then keep)')
                jobs.append((l, sex, f))
        n = sum(len(text_of(l)) for l, _, _ in jobs)
        if not go: return print(f'{cmd}: {len(jobs)} files, about {n:,} characters. Run again with --go to spend them.')
        (out / 'lines').mkdir(exist_ok=True)
        for k, (l, sex, f) in enumerate(jobs, 1):
            if cmd == 'redo' and f.exists():  # the old take is kept beside it, never deleted
                f.rename(f.with_suffix(f'.take{sum(1 for _ in f.parent.glob(f.stem + ".take*")) + 1}.mp3'))
            mp3 = call(key, 'POST', f'/v1/text-to-speech/{voices[l["class"] + "_" + sex]}?output_format=mp3_44100_128', {'text': text_of(l), 'model_id': MODEL}, raw=True)
            f.write_bytes(mp3)
            print(f'{k}/{len(jobs)} {f.name}')
        return

    if cmd == 'review':
        rows = []
        for l in lines():
            for sex in ('male', 'female'):
                f = f"lines/{l['id']}_{sex}.mp3"
                if (out / f).exists(): rows.append((l, sex, f))
        html = REVIEW.replace('__ROWS__', json.dumps([{'id': f"{l['id']}_{s}", 'line': l['text'], 'tag': l['tag'], 'trigger': l['trigger'], 'cls': l['class'], 'src': f} for l, s, f in rows]))
        (out / 'review.html').write_text(html, encoding='utf-8')
        return print(f'review.html: {len(rows)} files')
    sys.exit(__doc__)


REVIEW = r'''<!doctype html><meta charset=utf-8><title>Class Voice Review</title>
<style>body{font:15px system-ui;margin:0;background:#171513;color:#ece6df}header{position:sticky;top:0;background:#211e1b;padding:12px 16px;border-bottom:1px solid #34302c;display:flex;gap:12px;flex-wrap:wrap;align-items:center}
main{max-width:900px;margin:auto;padding:8px 16px 60px}.r{display:grid;grid-template-columns:1fr auto;gap:6px 12px;padding:10px 0;border-bottom:1px solid #34302c}.r[data-v=ok]{opacity:.55}.r[data-v=redo] .t{color:#ef6b62}
.id{font:12px ui-monospace,monospace;color:#a39a91}.tag{color:#e8874a}button,select{font:inherit;background:#2b2723;color:inherit;border:1px solid #4a443e;border-radius:6px;padding:4px 10px;cursor:pointer}
button.on{background:#e8874a;color:#171513}audio{width:100%;height:32px}</style>
<header><b>Class voice review</b><select id=f><option value="">All classes</option><option>sum</option><option>des</option><option>alc</option><option>ssw</option></select>
<select id=s><option value="">All</option><option value=todo>To review</option><option value=redo>Redo</option></select><span id=c></span><button id=x>Save Redo list</button></header><main id=m></main>
<script>const R=__ROWS__;let V={};try{V=JSON.parse(localStorage.getItem('v')||'{}')}catch(e){}
function save(){try{localStorage.setItem('v',JSON.stringify(V))}catch(e){}}
function draw(){const f=document.getElementById('f').value,s=document.getElementById('s').value;let n={ok:0,redo:0};R.forEach(r=>{if(V[r.id])n[V[r.id]]++});
document.getElementById('c').textContent=`${n.ok} approved, ${n.redo} redo, ${R.length-n.ok-n.redo} to review`;
document.getElementById('m').innerHTML=R.filter(r=>(!f||r.cls==f)&&(!s||(s=='todo'?!V[r.id]:V[r.id]==s))).map(r=>`<div class=r data-v="${V[r.id]||''}" data-id="${r.id}"><div><div class=id>${r.id}</div><div class=t>${r.tag?`<span class=tag>[${r.tag}]</span> `:''}${r.line}</div><audio controls preload=none src="${r.src}"></audio></div><div><button data-a=ok class="${V[r.id]=='ok'?'on':''}">Approve</button> <button data-a=redo class="${V[r.id]=='redo'?'on':''}">Redo</button></div></div>`).join('')}
document.getElementById('m').onclick=e=>{const b=e.target.closest('button');if(!b)return;const id=b.closest('.r').dataset.id;V[id]=V[id]==b.dataset.a?undefined:b.dataset.a;save();draw()};
document.getElementById('f').onchange=draw;document.getElementById('s').onchange=draw;
document.getElementById('x').onclick=()=>{const t=Object.keys(V).filter(k=>V[k]=='redo').join('\n');const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([t]));a.download='redo.txt';a.click()};draw();</script>'''

if __name__ == '__main__':
    main(sys.argv[1:])
