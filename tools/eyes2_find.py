import numpy as np, sys
import pathlib; sys.path.insert(0, str(pathlib.Path(__file__).parent)); from eyes2_depth import depth
from scipy.ndimage import uniform_filter
def shapes(f):
    S=[];cur=None
    for l in open(f):
        if l.startswith('s2b'): pending=[float(x) for x in l.split()[1:]]; continue
        if l.startswith('shape'): cur={'name':l.split(None,1)[1].strip(),'V':[],'T':[],'s2b':pending if 'pending' in dir() else None}; S.append(cur); pending=None; continue
        if l.startswith('v '): cur['V'].append([float(x) for x in l.split()[1:]])
        elif l.startswith('t '): cur['T'].append([int(x) for x in l.split()[1:]])
    return S
def find(f, pick=None):
    S=[s for s in shapes(f) if s['s2b'] is not None and (pick is None or s['name'] in pick)]
    # the head: the shapes skinned to the head bone, merged (the skull, or the body's head)
    V=np.concatenate([np.array(s['V']) for s in S]); off=0; T=[]
    for s in S: T+= [[a+off,b+off,c+off] for a,b,c in s['T']]; off+=len(s['V'])
    T=np.array(T); s2b=S[0]['s2b']
    # only the top of the model (the head)
    top=V[:,2].max(); z0,z1=top-16,top
    D=depth(V,T,-7,7,z0,z1,0.1)
    Df=np.nan_to_num(D,nan=np.nanmin(D))
    rec=uniform_filter(Df,31)-Df          # how far a point sits back from its surroundings (a socket)
    zz=np.linspace(z0,z1,D.shape[0]); xx=np.linspace(-7,7,D.shape[1])
    out=[]
    for side in (-1,1):
        m=np.zeros_like(rec,bool)
        for i,z in enumerate(zz):
            if not (z1-10<z<z1-4): continue
            for j,x in enumerate(xx):
                if 1.0<x*side<4.5 and not np.isnan(D[i,j]): m[i,j]=True
        r=np.where(m,rec,-1e9); i,j=np.unravel_index(np.argmax(r),r.shape)
        sel=m&(rec>0.6*rec[i,j])&(np.abs(zz[:,None]-zz[i])<1.6)&(np.abs(xx[None,:]-xx[j])<1.6)
        ii,jj=np.nonzero(sel); w=rec[ii,jj]
        x=np.average(xx[jj],weights=w); z=np.average(zz[ii],weights=w)
        box=(np.abs(zz[:,None]-z)<1.6)&(np.abs(xx[None,:]-x)<1.6)
        rim=np.nanpercentile(np.where(box,D,np.nan),90); bot=np.nanmin(D[ii,jj])
        y=max(bot+0.6, rim-1.2)
        out.append((x,y,z))
    # the two as a mirrored pair (a face is symmetric; the noisier side follows the other's depth)
    ax_=(abs(out[0][0])+abs(out[1][0]))/2; az=(out[0][2]+out[1][2])/2; ay=(out[0][1]+out[1][1])/2
    out=[(-ax_,ay,az),(ax_,ay,az)]
    t=np.array(s2b[:3]); R=np.array(s2b[3:12]).reshape(3,3); sc=s2b[12]
    head=[tuple((R@np.array(p)*sc+t).round(2)) for p in out]
    return [tuple(np.round(p,2)) for p in out], head
if __name__=='__main__':
    for a in sys.argv[1:]:
        f,_,pick=a.partition(':')
        print(f, *find(f, pick.split(',') if pick else None))
