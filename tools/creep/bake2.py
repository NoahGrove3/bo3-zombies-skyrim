"""Bake the creep sculpt (vertex colours + its fine surface) onto the 40k unwrapped mesh: albedo + tangent-space normal map
(Skyrim/nifly tangent frame, DirectX green), supersampled at 4096 and filtered down to 2048, charts dilated for mips."""
import numpy as np, re, sys
from scipy.spatial import cKDTree
from PIL import Image
D='/home/claude/work/creep/'; O='/home/claude/work/creep2/'
def fast_obj(p):
    txt=open(p).read()
    v=np.array(re.findall(r'^v ([^\n]+)', txt, re.M)); V=np.array([l.split() for l in v],np.float32)
    f=re.findall(r'^f ([^\n]+)', txt, re.M); F=np.array([[int(t.split('/')[0])-1 for t in l.split()] for l in f],np.int64)
    return V,F,txt
Vh,Fh,_=fast_obj(D+'full.obj'); Ch=Vh[:,3:6]; Vh=Vh[:,:3]
print('high',len(Vh),len(Fh))
fn=np.cross(Vh[Fh[:,1]]-Vh[Fh[:,0]],Vh[Fh[:,2]]-Vh[Fh[:,0]])
Nh=np.zeros_like(Vh)
for k in range(3): np.add.at(Nh,Fh[:,k],fn)
Nh/=np.linalg.norm(Nh,axis=1,keepdims=True)+1e-12
tree=cKDTree(Vh)
# low
txt=open(O+'low40uv.obj').read()
V=np.array([l.split() for l in re.findall(r'^v ([^\n]+)',txt,re.M)],np.float32)
T=np.array([l.split() for l in re.findall(r'^vt ([^\n]+)',txt,re.M)],np.float32)
N=np.array([l.split() for l in re.findall(r'^vn ([^\n]+)',txt,re.M)],np.float32)
F=np.array([[int(t.split('/')[0])-1 for t in l.split()] for l in re.findall(r'^f ([^\n]+)',txt,re.M)],np.int64)
uv=np.stack([T[:,0],1-T[:,1]],1)  # (the NIF's uv: rigwrite flips v)
# nifly's tangent frame (BSTriShape::CalcTangentSpace): tan2 = U direction (sdir), tan1 = V direction (tdir)
t1=np.zeros_like(V); t2=np.zeros_like(V)
v1,v2,v3=V[F[:,0]],V[F[:,1]],V[F[:,2]]; w1,w2,w3=uv[F[:,0]],uv[F[:,1]],uv[F[:,2]]
e1=v2-v1; e2=v3-v1; s1=w2[:,0]-w1[:,0]; s2=w3[:,0]-w1[:,0]; q1=w2[:,1]-w1[:,1]; q2=w3[:,1]-w1[:,1]
r=np.where(s1*q2-s2*q1>=0,1.0,-1.0)[:,None]
sdir=(q2[:,None]*e1-q1[:,None]*e2)*r; tdir=(s1[:,None]*e2-s2[:,None]*e1)*r
sdir/=np.linalg.norm(sdir,axis=1,keepdims=True)+1e-12; tdir/=np.linalg.norm(tdir,axis=1,keepdims=True)+1e-12
for k in range(3): np.add.at(t2,F[:,k],sdir); np.add.at(t1,F[:,k],tdir)
Tu=t2/ (np.linalg.norm(t2,axis=1,keepdims=True)+1e-12); Tu-=N*(N*Tu).sum(1,keepdims=True); Tu/=np.linalg.norm(Tu,axis=1,keepdims=True)+1e-12
Tv=t1/(np.linalg.norm(t1,axis=1,keepdims=True)+1e-12); Tv-=N*(N*Tv).sum(1,keepdims=True); Tv-=Tu*(Tu*Tv).sum(1,keepdims=True); Tv/=np.linalg.norm(Tv,axis=1,keepdims=True)+1e-12
S=4096
alb=np.zeros((S,S,3),np.float32); nrm=np.zeros((S,S,3),np.float32); cov=np.zeros((S,S),bool)
P_all=[];I_all=[];B_all=[];Tri=[]
for t in range(len(F)):
    a,b,c=uv[F[t]]*S
    x0,x1=int(np.floor(min(a[0],b[0],c[0])))-1,int(np.ceil(max(a[0],b[0],c[0])))+1
    y0,y1=int(np.floor(min(a[1],b[1],c[1])))-1,int(np.ceil(max(a[1],b[1],c[1])))+1
    x0=max(x0,0);y0=max(y0,0);x1=min(x1,S);y1=min(y1,S)
    X,Y=np.meshgrid(np.arange(x0,x1)+0.5,np.arange(y0,y1)+0.5)
    den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
    if abs(den)<1e-9: continue
    l0=((b[1]-c[1])*(X-c[0])+(c[0]-b[0])*(Y-c[1]))/den; l1=((c[1]-a[1])*(X-c[0])+(a[0]-c[0])*(Y-c[1]))/den; l2=1-l0-l1
    m=(l0>=-0.02)&(l1>=-0.02)&(l2>=-0.02)
    if not m.any(): continue
    L=np.stack([l0[m],l1[m],l2[m]],1).clip(0,1); L/=L.sum(1,keepdims=True)
    P_all.append(np.stack([Y[m].astype(np.int32),X[m].astype(np.int32)],1)); B_all.append(L.astype(np.float32)); Tri.append(np.full(len(L),t,np.int32))
pix=np.concatenate(P_all); L=np.concatenate(B_all); tri=np.concatenate(Tri)
print('texels',len(pix))
idx=F[tri]
def interp(A): return (A[idx]*L[:,:,None]).sum(1)
P=interp(V); Nn=interp(N); Nn/=np.linalg.norm(Nn,axis=1,keepdims=True); TU=interp(Tu); TV=interp(Tv)
out_c=np.zeros((len(P),3),np.float32); out_n=np.zeros((len(P),3),np.float32)
CH=400000
for s in range(0,len(P),CH):
    p=P[s:s+CH]; n=Nn[s:s+CH]
    d,k=tree.query(p,k=8)
    nk=Nh[k]; agree=(nk*n[:,None,:]).sum(2)
    w=1.0/np.maximum(d,1e-3)**2*np.clip(agree+0.3,0,None)
    w[w.sum(1)==0,0]=1
    w/=w.sum(1,keepdims=True)
    out_c[s:s+CH]=(Ch[k]*w[:,:,None]).sum(1)
    nh=(nk*w[:,:,None]).sum(1); nh/=np.linalg.norm(nh,axis=1,keepdims=True)+1e-12
    out_n[s:s+CH]=nh
# into the tangent frame: R = along U, G = along V (DirectX: v down the image), B = out
ts=np.stack([(out_n*TU).sum(1),(out_n*TV).sum(1),(out_n*Nn).sum(1)],1)
ts[:,2]=np.clip(ts[:,2],0.05,None); ts/=np.linalg.norm(ts,axis=1,keepdims=True)
alb[pix[:,0],pix[:,1]]=out_c; nrm[pix[:,0],pix[:,1]]=ts*0.5+0.5; cov[pix[:,0],pix[:,1]]=True
print('ts mean',ts.mean(0).round(3))
# dilate charts (so mips never pull in the empty gutter)
from scipy.ndimage import distance_transform_edt
_,(iy,ix)=distance_transform_edt(~cov,return_indices=True)
alb=alb[iy,ix]; nrm=nrm[iy,ix]
def down(a): return a.reshape(S//2,2,S//2,2,3).mean((1,3))
A=down(alb); Nm=down(nrm); v=Nm*2-1; v/=np.linalg.norm(v,axis=2,keepdims=True); Nm=v*0.5+0.5
Image.fromarray((A.clip(0,1)*255+0.5).astype(np.uint8)).save(O+'volatile_albedo.png')
Image.fromarray((Nm.clip(0,1)*255+0.5).astype(np.uint8)).save(O+'volatile_normal.png')
print('done')
