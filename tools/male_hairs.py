#!/usr/bin/env python3
"""(Noah: "only 18 available hairstyles for a male, but there's 100 plus for the female") RaceMenu lists a hair flagged for
both sexes to women only, so every playable Xing SMP hair gets a male-only copy in 'CurseOfAlduin - Male Hairs.esp' (light,
masters: Skyrim.esm + the three Xing plugins; the copies reuse the originals' extra parts, textures and race lists).
Run against the game's Data folder (the Xing plugins are other authors' - only this script is kept here):
    python3 tools/male_hairs.py <Data> "<Data>/CurseOfAlduin - Male Hairs.esp"
"""

import sys,struct,zlib,os
G=sys.argv[1]; out=sys.argv[2]
srcs=["Xing SMP Hairs and Wigs - Dint999.esp","Xing SMP Hairs and Wigs - Fuse.esp","Xing SMP Hairs and Wigs - HHairstyles.esp"]
masters=["Skyrim.esm"]+srcs
FORMSUBS={b'HNAM',b'RNAM',b'TNAM',b'CNAM'}
recs=[]; nid=0x800; me=len(masters)
def sub(n,v): return n+struct.pack('<H',len(v))+v
for si,fn in enumerate(srcs):
    d=open(os.path.join(G,fn),'rb').read(); sz=struct.unpack_from('<I',d,4)[0]
    assert struct.unpack_from('<I',d,8)[0]&0x80==0, 'localized'
    # its masters
    hm=[]; i=24
    while i<24+sz:
        t=d[i:i+4]; l=struct.unpack_from('<H',d,i+4)[0]
        if t==b'MAST': hm.append(d[i+6:i+6+l].rstrip(b'\0').decode())
        i+=6+l
    remap={k:masters.index(m) for k,m in enumerate(hm)}; remap[len(hm)]=1+si
    j=24+sz
    while j<len(d):
        t=d[j:j+4]; s=struct.unpack_from('<I',d,j+4)[0]
        if t==b'GRUP': j+=24; continue
        fl=struct.unpack_from('<I',d,j+8)[0]; body=d[j+24:j+24+s]
        if fl&0x40000: body=zlib.decompress(body[4:])
        if t==b'HDPT':
            k=0; subs=[]
            while k<len(body):
                n=body[k:k+4]; l=struct.unpack_from('<H',body,k+4)[0]; subs.append([n,body[k+6:k+6+l]]); k+=6+l
            sd=dict((n,v) for n,v in subs)
            if struct.unpack('<I',sd.get(b'PNAM',b'\xff\xff\xff\xff'))[0]==3 and sd[b'DATA'][0]&1:
                o=b''
                for n,v in subs:
                    if n==b'EDID': v=b'CA_M_'+v
                    elif n==b'DATA': v=bytes([0x03])+v[1:]
                    elif n in FORMSUBS and len(v)==4:
                        x=struct.unpack('<I',v)[0]
                        if x: v=struct.pack('<I',(remap[x>>24]<<24)|(x&0xFFFFFF))
                    o+=sub(n,v)
                recs.append(b'HDPT'+struct.pack('<IIIIHH',len(o),0,(me<<24)|nid,0,44,0)+o); nid+=1
        j+=24+s
grp=b''.join(recs)
grp=b'GRUP'+struct.pack('<I',24+len(grp))+b'HDPT'+struct.pack('<iIHH',0,0,0,0)+grp
h=sub(b'HEDR',struct.pack('<fII',1.71,len(recs),nid))+sub(b'CNAM',b'Claude\0')+sub(b'SNAM',b'Male copies of the Xing SMP hairs (RaceMenu lists a both-sexes hair for women only)\0')
for m in masters: h+=sub(b'MAST',m.encode()+b'\0')+sub(b'DATA',b'\0'*8)
tes4=b'TES4'+struct.pack('<IIIIHH',len(h),0x200,0,0,44,0)+h
open(out,'wb').write(tes4+grp)
print('male hairs',len(recs),'ids',hex(0x800),hex(nid-1))
