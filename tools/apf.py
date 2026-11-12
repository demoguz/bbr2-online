#!/usr/bin/env python3
"""apf.py list [filter] | get <name> <out> : Assets.apf (VUPF v8) reader."""
import struct,os,sys,zlib,re
P=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','Assets.apf')
def toc():
    sz=os.path.getsize(P); f=open(P,'rb'); h=f.read(0x38)
    magic,ver,u1,count,tocsz,crc=struct.unpack_from('<4sIIIII',h)
    f.seek(sz-tocsz); t=f.read(tocsz); o=0; out=[]
    while o<len(t) and len(out)<count:
        e=t.index(b'\0',o); name=t[o:e].decode('latin1'); o=e+1
        off,usize,csize,crc,flags=struct.unpack_from('<IIIII',t,o); o+=20
        out.append((name,off,usize,csize,flags))
    return out
def get(name):
    for n,off,us,cs,fl in toc():
        if n==name:
            f=open(P,'rb'); f.seek(off); d=f.read(cs)
            return zlib.decompress(d) if fl&0x10000 else d
if __name__=='__main__':
    a=sys.argv[1:]
    if a[0]=='list':
        for n,off,us,cs,fl in toc():
            if len(a)<2 or re.search(a[1],n): print(f'{n}  {us}')
    elif a[0]=='types':
        import collections; c=collections.Counter(n.split('/')[0] for n,*_ in toc()); print(c.most_common())
    elif a[0]=='get': open(a[2],'wb').write(get(a[1]))
