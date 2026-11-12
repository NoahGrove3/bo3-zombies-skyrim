#!/usr/bin/env python3
"""(Noah: "only the men get male hairstyles or unisex hairstyles and the females get girly hairstyles") the Xing SMP hairs
flagged by the mod's own head fitting (EDID HDPT_M__ / HDPT_F__), each to one sex. Replaces the male copies of
tools/male_hairs.py (retired: men got every women's wig). Usage: python3 tools/hair_sexflags.py <Data> [dry]
"""
# the Xing hairs by the mod's own fitting: HDPT_M__ male heads only, HDPT_F__ female heads only (playable)
import sys,struct,zlib,os
G=sys.argv[1]; dry=len(sys.argv)>2
for fn in ["Xing SMP Hairs and Wigs - Dint999.esp","Xing SMP Hairs and Wigs - Fuse.esp","Xing SMP Hairs and Wigs - HHairstyles.esp"]:
    p=os.path.join(G,fn); d=bytearray(open(p,'rb').read()); sz=struct.unpack_from('<I',d,4)[0]; j=24+sz; m=f=0
    while j<len(d):
        t=bytes(d[j:j+4]); s=struct.unpack_from('<I',d,j+4)[0]
        if t==b'GRUP': j+=24; continue
        fl=struct.unpack_from('<I',d,j+8)[0]
        if t==b'HDPT':
            assert not fl&0x40000, 'compressed'
            k=j+24; sub={}; pos={}
            while k<j+24+s:
                n=bytes(d[k:k+4]); l=struct.unpack_from('<H',d,k+4)[0]; sub.setdefault(n,bytes(d[k+6:k+6+l])); pos.setdefault(n,k+6); k+=6+l
            if struct.unpack('<I',sub.get(b'PNAM',b'\xff'*4))[0]==3 and sub[b'DATA'][0]&1:
                ed=sub[b'EDID'].decode('latin1')
                male='_M__' in ed
                d[pos[b'DATA']]=(sub[b'DATA'][0]&~0x6)|(0x2 if male else 0x4)
                m+=male; f+=not male
        j+=24+s
    if not dry: open(p,'wb').write(d)
    print(fn,'male',m,'female',f)
