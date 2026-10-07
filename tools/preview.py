"""Quick shaded front/side preview of OBJ meshes (painter's algorithm), for checking re-rigged characters."""
import sys, numpy as np
from PIL import Image, ImageDraw
def load(p):
    V=[];F=[]
    for l in open(p):
        if l.startswith('v '): V.append(list(map(float,l.split()[1:4])))
        elif l.startswith('f '): F.append([int(x.split('/')[0])-1 for x in l.split()[1:4]])
    return np.array(V),np.array(F)
def render(V,F,view,size=360,zmax=200):
    im=Image.new('RGB',(size,size),(30,30,34)); d=ImageDraw.Draw(im)
    if view=='front': P=np.c_[V[:,0],V[:,2],-V[:,1]]   # looking at the face (+Y forward toward viewer)
    else: P=np.c_[V[:,1],V[:,2],V[:,0]]
    s=size/zmax*0.95; tri=P[F]
    n=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]); n/=np.linalg.norm(n,axis=1,keepdims=True)+1e-9
    order=np.argsort(-tri[:,:,2].mean(1)) if view=='front' else np.argsort(tri[:,:,2].mean(1))
    for i in order:
        sh=int(60+170*abs(n[i,2]))
        pts=[(size/2+x*s, size-6-y*s) for x,y,_ in tri[i]]
        d.polygon(pts,fill=(sh,sh,int(sh*0.95)))
    return im
out=Image.new('RGB',(360*2*(len(sys.argv)-2),360))
for k,p in enumerate(sys.argv[2:]):
    V,F=load(p); zmax=max(200,V[:,2].max()*1.05)
    out.paste(render(V,F,'front',zmax=zmax),(720*k,0)); out.paste(render(V,F,'side',zmax=zmax),(720*k+360,0))
out.save(sys.argv[1])
