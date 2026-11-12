#!/usr/bin/env python3
"""peinfo.py <dll>: lists exports/imports."""
import struct,sys
D=open(sys.argv[1],'rb').read(); pe=struct.unpack_from('<I',D,0x3c)[0]
nsec=struct.unpack_from('<H',D,pe+6)[0]; optsz=struct.unpack_from('<H',D,pe+20)[0]
secs=[struct.unpack_from('<IIII',D,pe+24+optsz+40*i+8) for i in range(nsec)]
def r2o(r):
    for vs,va,rs,ro in secs:
        if va<=r<va+max(vs,rs): return ro+r-va
exp,_=struct.unpack_from('<II',D,pe+24+112); o=r2o(exp)
nn=struct.unpack_from('<I',D,o+24)[0]; anm=struct.unpack_from('<I',D,o+32)[0]
print('exports',nn,':',' '.join(D[r2o(struct.unpack_from('<I',D,r2o(anm)+4*i)[0]):].split(b'\0')[0].decode() for i in range(nn)))
imp=struct.unpack_from('<I',D,pe+24+112+8)[0]; o=r2o(imp)
while True:
    ilt,_,_,name,iat=struct.unpack_from('<IIIII',D,o)
    if not name: break
    print('imports:',D[r2o(name):].split(b'\0')[0].decode()); o+=20
