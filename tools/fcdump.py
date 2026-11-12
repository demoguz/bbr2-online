#!/usr/bin/env python3
"""fcdump.py <VuProjectAsset/name> [out.json]: opens a project asset and converts it to JSON (fast-container format)."""
import sys,os,struct,json,glob
HERE=os.path.dirname(os.path.abspath(__file__))
for w in glob.glob(os.path.join(HERE,'..','toolchain','wheels','lz4-*.whl')):
    d='/tmp/bbr2lz4'
    if not os.path.exists(d): import zipfile; zipfile.ZipFile(w).extractall(d)
    sys.path.insert(0,d)
import lz4.block
sys.path.insert(0,HERE); import apf
def node(d,o):
    t=struct.unpack_from('<I',d,o)[0]
    if t==0: return None
    if t==1: return struct.unpack_from('<i',d,o+4)[0]
    if t==2: return struct.unpack_from('<f',d,o+4)[0]
    if t==3: return bool(d[o+4])
    if t==4:
        r=struct.unpack_from('<I',d,o+4)[0]; s=o+r; e=d.index(b'\0',s); return d[s:e].decode('utf-8','replace')
    if t==5:
        n=struct.unpack_from('<I',d,o+4)[0]; return [node(d,o+struct.unpack_from('<I',d,o+8+4*i)[0]) for i in range(n)]
    if t==6:
        n=struct.unpack_from('<I',d,o+4)[0]; out={}
        for i in range(n):
            h,ko,vo=struct.unpack_from('<QII',d,o+8+16*i)
            ks=o+ko; ke=d.index(b'\0',ks); out[d[ks:ke].decode('utf-8','replace')]=node(d,o+vo)
        return out
    if t==7: return struct.unpack_from('<q',d,o+4)[0]
    return '<type %d>'%t
def load(name):
    raw=apf.get(name)
    if raw is None: raise SystemExit('missing: '+name)
    for usz in (1<<20,1<<23,1<<26):
        try: d=lz4.block.decompress(raw,uncompressed_size=usz); break
        except Exception: d=None
    ver,size=struct.unpack_from('<II',d,0)
    return node(d,8)
if __name__=='__main__':
    j=load(sys.argv[1])
    s=json.dumps(j,indent=1,ensure_ascii=False)
    if len(sys.argv)>2: open(sys.argv[2],'w').write(s)
    else: print(s)
