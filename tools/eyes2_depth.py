import numpy as np, sys, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
def load(f):
    V=[];T=[]
    for l in open(f):
        if l.startswith('v '): V.append([float(x) for x in l.split()[1:]])
        elif l.startswith('t '): T.append([int(x) for x in l.split()[1:]])
    return np.array(V),np.array(T)
def depth(V,T,x0,x1,z0,z1,res=0.1):
    W=int((x1-x0)/res); H=int((z1-z0)/res); D=np.full((H,W),-1e9)
    for a,b,c in T:
        P=V[[a,b,c]]
        if P[:,2].max()<z0 or P[:,2].min()>z1: continue
        xmin,xmax=P[:,0].min(),P[:,0].max(); zmin,zmax=P[:,2].min(),P[:,2].max()
        i0=max(0,int((xmin-x0)/res)); i1=min(W-1,int((xmax-x0)/res)+1); j0=max(0,int((zmin-z0)/res)); j1=min(H-1,int((zmax-z0)/res)+1)
        (ax,ay,az),(bx,by,bz),(cx,cy,cz)=P
        den=(bz-cz)*(ax-cx)+(cx-bx)*(az-cz)
        if abs(den)<1e-9: continue
        for j in range(j0,j1+1):
            z=z0+(j+0.5)*res
            for i in range(i0,i1+1):
                x=x0+(i+0.5)*res
                l1=((bz-cz)*(x-cx)+(cx-bx)*(z-cz))/den; l2=((cz-az)*(x-cx)+(ax-cx)*(z-cz))/den; l3=1-l1-l2
                if l1>=0 and l2>=0 and l3>=0:
                    y=l1*ay+l2*by+l3*cy
                    if y>D[j,i]: D[j,i]=y
    D[D<-1e8]=np.nan; return D
if __name__=='__main__':
    out=sys.argv[1]; files=sys.argv[2:]
    fig,ax=plt.subplots(1,len(files),figsize=(7*len(files),8))
    for a,f in zip(np.atleast_1d(ax),files):
        V,T=load(f); top=V[:,2].max(); z0,z1=top-16,top
        D=depth(V,T,-7,7,z0,z1)
        np.save(f+'.npy',D)
        im=a.imshow(D,origin='lower',extent=[-7,7,z0,z1],cmap='turbo'); a.set_title(f); plt.colorbar(im,ax=a); a.grid(True)
    plt.savefig(out,dpi=60)
