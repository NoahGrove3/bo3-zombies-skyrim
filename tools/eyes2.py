"""eyes2.py: (Noah: red eyes in the actual sockets) writes meshes/CurseOfAlduin/eyes2/<row>_glow.nif for the red-eyed rows: each
model's head mesh is drawn front-on as a depth map (eyes2_depth.py), its eye sockets found as the two wells either side of
the nose at eye height (eyes2_find.py), and two red glows (a soft halo round a hot core, crossed cards) set in them, in the
head bone's own space (src/Undead.cpp hangs them on the head). Inputs: /tmp/m_*.txt dumps of each head mesh (tools meshdump)."""
import numpy as np, sys, subprocess, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).parent)); from eyes2_find import find, shapes
from eyes2_depth import depth
OUT=pathlib.Path('/tmp/eyes2out/Meshes/CurseOfAlduin/eyes2'); OUT.mkdir(parents=True,exist_ok=True)
RED=(1.0,0.10,0.06)
def at_point(f,pick,x,z):
    S=[s for s in shapes(f) if s['s2b'] is not None and (pick is None or s['name'] in pick)]
    V=np.concatenate([np.array(s['V']) for s in S]); off=0;T=[]
    for s in S: T+=[[a+off,b+off,c+off] for a,b,c in s['T']]; off+=len(s['V'])
    top=V[:,2].max(); D=depth(V,np.array(T),-7,7,top-16,top,0.1)
    zz=np.linspace(top-16,top,D.shape[0]); xx=np.linspace(-7,7,D.shape[1])
    box=(np.abs(zz[:,None]-z)<1.2)&(np.abs(xx[None,:]-abs(x))<1.2)
    rim=np.nanpercentile(np.where(box,D,np.nan),90); bot=np.nanmin(np.where(box,D,np.nan))
    y=max(bot+0.6,rim-1.2); s2b=S[0]['s2b']
    t=np.array(s2b[:3]); R=np.array(s2b[3:12]).reshape(3,3); sc=s2b[12]
    return [tuple(R@np.array(p)*sc+t) for p in [(-abs(x),y,z),(abs(x),y,z)]]
models={
 'zombie':  lambda: find('/tmp/m_zombie.txt')[1],
 'skull':   lambda: find('/tmp/m_skull.txt')[1],
 'lich':    lambda: find('/tmp/m_lich.txt')[1],
 'bloody':  lambda: at_point('/tmp/mall_arons_skeletonbloody.txt',['NPC R Toe0 [RToe]_9'],2.3,138.3),
 'human':   lambda: [(-2.55,8.9,3.55),(2.55,8.9,3.55)],
}
size={'zombie':0.55,'skull':0.6,'lich':0.6,'bloody':0.6,'human':0.5}
rows={'zombie':['zombie','z_dread','runner','z_deranged2','tb_mangled','tb_foul','tb_putrid','tb_malignant','tb_relentless','tb_dread_putrid'],
 'skull':['sk_rotten','skeleton','necro','hexer_rime','hexer_storm','sk_necromancer','boneman','marksman','boneman_warrior','sk_raider','sk_reaver','mb_nether_lich'],
 'bloody':['sk_bloody'],'lich':['lich'],'human':['nightwalker','volatile']}
for m,ids in rows.items():
    eyes=models[m](); sz=size[m]
    print(m,'head-space eyes',[tuple(round(float(c),2) for c in e) for e in eyes])
    for rid in ids:
        out=OUT/f'{rid}_glow.nif'
        L=[f'out {out}']
        for name,tex,scale,half in (('halo','CurseOfAlduin\\fx\\soft.dds',1.8,1.9*sz),('core','CurseOfAlduin\\fx\\core.dds',3.2,0.75*sz)):
            L.append(f"shape {name} tex={tex} color={RED[0]:.3f},{RED[1]:.3f},{RED[2]:.3f},1 scale={scale} blend=add falloff=0")
            vi=0
            for e in eyes:
                for ax in range(3):
                    u,v=[(0,2),(1,2),(0,1)][ax]
                    for (a,b,uu,vv) in ((-half,-half,0,1),(half,-half,1,1),(half,half,1,0),(-half,half,0,0)):
                        q=list(e); q[u]+=a; q[v]+=b
                        L.append(f'v {q[0]:.4f} {q[1]:.4f} {q[2]:.4f} {uu} {vv}')
                    L+= [f't {vi} {vi+1} {vi+2}', f't {vi} {vi+2} {vi+3}']; vi+=4
        job=pathlib.Path('/tmp/claude-0')/f'eyes2_{rid}.txt'; job.write_text('\n'.join(L)+'\n')
        r=subprocess.run(['/home/claude/tools/fxwrite',str(job)],capture_output=True,text=True)
        if r.returncode: print(rid,'FAIL',r.stdout[-200:],r.stderr[-200:])
print(sorted(p.name for p in OUT.iterdir()))
