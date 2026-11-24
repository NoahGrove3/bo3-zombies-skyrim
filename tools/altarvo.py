"""(Noah, 2026-11-24: "I chose the voice it sounds so much better than before") Felldir the Old's lines - the altar's talk
(docs/altarvo.json) and its existing whispers - made through the ElevenLabs API on Noah's PC, in his "Feldir Echo Alter" voice.

The API key is read from a file Noah makes himself (one line, the key) and is never printed, logged or written anywhere else.
Every step that spends ElevenLabs credits says how many characters, and runs only with --go.

  python3 altarvo.py <key file> <lines json> <out dir> check                   # free: the key, credits left, the cost of a pass
  python3 altarvo.py <key file> <lines json> <out dir> lines [--go] [ids]      # every line not made yet (or just these ids)
  python3 altarvo.py <key file> <lines json> <out dir> review                  # free: review.html - play each line, Approve or Redo
  python3 altarvo.py <key file> <lines json> <out dir> redo <redo.txt> [--go]  # make again the lines the review marked Redo
<lines json> is docs/altarvo_lines.json (tools/altarvo_script.py). Files: lines/<id>.mp3, model eleven_v3, mp3 44.1 kHz."""
import json, pathlib, sys, urllib.error, urllib.request

API = 'https://api.elevenlabs.io'


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
        sys.exit(f'ElevenLabs said {e.code} on {path.split("?")[0]}: {e.read()[:400].decode("utf-8", "replace")}')
    except urllib.error.URLError as e:
        sys.exit(f'could not reach ElevenLabs ({e.reason}) - is api.elevenlabs.io allowed in the network settings?')


def main(a):
    if len(a) < 4: sys.exit(__doc__)
    key, D, out, cmd, rest = key_from(a[0]), json.loads(pathlib.Path(a[1]).read_text(encoding='utf-8')), pathlib.Path(a[2]), a[3], a[4:]
    go = '--go' in rest
    rest = [r for r in rest if r != '--go']
    out.mkdir(parents=True, exist_ok=True)
    L, voice, model = D['lines'], D['voice'], D['model']

    if cmd == 'check':
        u = call(key, 'GET', '/v1/user/subscription')
        left = u.get('character_limit', 0) - u.get('character_count', 0)
        todo = [l for l in L if not (out / 'lines' / f"{l['id']}.mp3").exists()]
        print(f"API reachable, key good. {left:,} characters left this period (of {u.get('character_limit', 0):,}).")
        return print(f"{len(todo)} of {len(L)} lines still to make: about {sum(len(l['text']) for l in todo):,} characters.")

    if cmd in ('lines', 'redo'):
        want = None
        if cmd == 'redo': want = {w.strip() for w in pathlib.Path(rest[0]).read_text().split() if w.strip()}
        elif rest: want = set(rest)
        jobs = []
        for l in L:
            f = out / 'lines' / f"{l['id']}.mp3"
            if want is not None and l['id'] not in want: continue
            if want is None and f.exists(): continue
            jobs.append((l, f))
        n = sum(len(l['text']) for l, _ in jobs)
        if not go: return print(f'{cmd}: {len(jobs)} files, about {n:,} characters. Run again with --go to spend them.')
        (out / 'lines').mkdir(exist_ok=True)
        for k, (l, f) in enumerate(jobs, 1):
            if cmd == 'redo' and f.exists():  # the old take is kept beside it, never deleted
                f.rename(f.with_suffix(f'.take{sum(1 for _ in f.parent.glob(f.stem + ".take*")) + 1}.mp3'))
            f.write_bytes(call(key, 'POST', f'/v1/text-to-speech/{voice}?output_format=mp3_44100_128', {'text': l['text'], 'model_id': model}, raw=True))
            print(f'{k}/{len(jobs)} {f.name}', flush=True)
        return

    if cmd == 'review':
        rows = [dict(l, src=f"lines/{l['id']}.mp3") for l in L if (out / 'lines' / f"{l['id']}.mp3").exists()]
        (out / 'review.html').write_text(REVIEW.replace('__ROWS__', json.dumps(rows)), encoding='utf-8')
        return print(f'review.html: {len(rows)} files')
    sys.exit(__doc__)


REVIEW = r'''<!doctype html><meta charset=utf-8><title>Echo Altar Voice Review</title>
<style>body{font:15px system-ui;margin:0;background:#171513;color:#ece6df}header{position:sticky;top:0;background:#211e1b;padding:12px 16px;border-bottom:1px solid #34302c;display:flex;gap:12px;flex-wrap:wrap;align-items:center}
main{max-width:900px;margin:auto;padding:8px 16px 60px}h2{font-size:15px;color:#e8874a;margin:22px 0 4px}.r{display:grid;grid-template-columns:1fr auto;gap:6px 12px;padding:10px 0;border-bottom:1px solid #34302c}.r[data-v=ok]{opacity:.55}.r[data-v=redo] .t{color:#ef6b62}
.id{font:12px ui-monospace,monospace;color:#a39a91}.ask{color:#a39a91;font-style:italic}button,select{font:inherit;background:#2b2723;color:inherit;border:1px solid #4a443e;border-radius:6px;padding:4px 10px;cursor:pointer}
button.on{background:#e8874a;color:#171513}audio{width:100%;height:32px}</style>
<header><b>Echo Altar voice review</b><select id=s><option value="">All</option><option value=todo>To review</option><option value=redo>Redo</option></select><span id=c></span><button id=x>Save Redo list</button></header><main id=m></main>
<script>const R=__ROWS__;let V={};try{V=JSON.parse(localStorage.getItem('av')||'{}')}catch(e){}
function save(){try{localStorage.setItem('av',JSON.stringify(V))}catch(e){}}
const esc=t=>String(t).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function draw(){const s=document.getElementById('s').value;let n={ok:0,redo:0};R.forEach(r=>{if(V[r.id])n[V[r.id]]++});
document.getElementById('c').textContent=`${n.ok} approved, ${n.redo} redo, ${R.length-n.ok-n.redo} to review`;let g='',h='';
R.filter(r=>!s||(s=='todo'?!V[r.id]:V[r.id]==s)).forEach(r=>{if(r.group!=g){g=r.group;h+=`<h2>${esc(g)}</h2>`}
h+=`<div class=r data-v="${V[r.id]||''}" data-id="${r.id}"><div><div class=id>${r.id} <span class=ask>${esc(r.ask)}</span></div><div class=t>${esc(r.text)}</div><audio controls preload=none src="${r.src}"></audio></div><div><button data-a=ok class="${V[r.id]=='ok'?'on':''}">Approve</button> <button data-a=redo class="${V[r.id]=='redo'?'on':''}">Redo</button></div></div>`});
document.getElementById('m').innerHTML=h}
document.getElementById('m').onclick=e=>{const b=e.target.closest('button');if(!b)return;const id=b.closest('.r').dataset.id;V[id]=V[id]==b.dataset.a?undefined:b.dataset.a;save();draw()};
document.getElementById('s').onchange=draw;
document.getElementById('x').onclick=()=>{const t=Object.keys(V).filter(k=>V[k]=='redo').join('\n');const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([t]));a.download='redo.txt';a.click()};draw();</script>'''

if __name__ == '__main__':
    main(sys.argv[1:])
