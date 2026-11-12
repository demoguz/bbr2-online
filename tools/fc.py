#!/usr/bin/env python3
"""Lossless decoder/encoder for the fast container (VuFastContainer).
JSON form: int=type1, float=type2, bool=type3, str=type4, list=type5, dict=type6,
{"$i64": n}=type7, {"$bin": "hex"}=type8 (inline binary; position/colour/rect), None=type0.
Usage: fc.py dump <VuProjectAsset/name> <out.json> | build <in.json> <out.fc> | test <name>"""
import sys,os,struct,json,glob
HERE=os.path.dirname(os.path.abspath(__file__))
for w in glob.glob(os.path.join(HERE,'..','toolchain','wheels','lz4-*.whl')):
    d='/tmp/bbr2lz4'
    if not os.path.exists(d): import zipfile; zipfile.ZipFile(w).extractall(d)
    sys.path.insert(0,d)
import lz4.block
sys.path.insert(0,HERE); import apf
def decode(d,o):
    t=struct.unpack_from('<I',d,o)[0]
    if t==0: return None
    if t==1: return struct.unpack_from('<i',d,o+4)[0]
    if t==2: return float(struct.unpack_from('<f',d,o+4)[0])
    if t==3: return bool(d[o+4])
    if t==4:
        r=struct.unpack_from('<I',d,o+4)[0]; s=o+r; e=d.index(b'\0',s); return d[s:e].decode('utf-8')
    if t==5:
        n=struct.unpack_from('<I',d,o+4)[0]; return [decode(d,o+struct.unpack_from('<I',d,o+8+4*i)[0]) for i in range(n)]
    if t==6:
        n=struct.unpack_from('<I',d,o+4)[0]; out={}
        for i in range(n):
            h,ko,vo=struct.unpack_from('<QII',d,o+8+16*i)
            ks=o+ko; ke=d.index(b'\0',ks); out[d[ks:ke].decode('utf-8')]=decode(d,o+vo)
        return out
    if t==7: return {'$i64':struct.unpack_from('<q',d,o+4)[0]}
    if t==8:
        n=struct.unpack_from('<I',d,o+4)[0]; return {'$bin':d[o+8:o+8+n].hex()}
    raise ValueError('unknown type %d @%x'%(t,o))
def fnv64(s):
    h=0xcbf29ce484222325
    for c in s.encode('utf-8'): h=((h^c)*0x100000001b3)&0xffffffffffffffff
    return h
def al(n,a=4): return (n+a-1)&~(a-1)
class B:
    def __init__(s): s.b=bytearray()
    def emit(s,v):
        o=len(s.b); b=s.b
        def w32(at,x): struct.pack_into('<I',b,at,x&0xffffffff)
        if v is None: b+=struct.pack('<II',0,0)
        elif isinstance(v,bool): b+=struct.pack('<II',3,1 if v else 0)
        elif isinstance(v,int): b+=struct.pack('<Ii',1,v)
        elif isinstance(v,float): b+=struct.pack('<If',2,v)
        elif isinstance(v,str):
            e=v.encode('utf-8')+b'\0'; b+=struct.pack('<II',4,8); b+=e; b+=b'\0'*(al(len(b))-len(b))
        elif isinstance(v,list):
            hdr=8+4*len(v); b+=struct.pack('<II',5,len(v)); b+=b'\0'*(4*len(v))
            for i,x in enumerate(v): p=s.emit(x); w32(o+8+4*i,p-o)
        elif isinstance(v,dict) and '$i64' in v and len(v)==1: b+=struct.pack('<Iq',7,v['$i64'])
        elif isinstance(v,dict) and '$bin' in v and len(v)==1:
            data=bytes.fromhex(v['$bin']); b+=struct.pack('<II',8,len(data)); b+=data; b+=b'\0'*(al(len(b))-len(b))
        elif isinstance(v,dict):
            items=sorted(v.items(),key=lambda kv: fnv64(kv[0]))
            b+=struct.pack('<II',6,len(items)); ent=len(b); b+=b'\0'*(16*len(items))
            for i,(k,x) in enumerate(items):
                ko=len(b); e=k.encode('utf-8')+b'\0'; b+=e; b+=b'\0'*(al(len(b))-len(b))
                vo=s.emit(x)
                struct.pack_into('<QII',b,ent+16*i,fnv64(k),ko-o,vo-o)
        else: raise TypeError(type(v))
        return o
def build(tree): bb=B(); bb.emit(tree); return bytes(bb.b)
def load_project(name):
    raw=apf.get(name)
    d=lz4.block.decompress(raw,uncompressed_size=1<<26)
    return decode(d,8), d
if __name__=='__main__':
    a=sys.argv[1:]
    if a[0]=='dump':
        t,_=load_project(a[1]); open(a[2],'w').write(json.dumps(t,indent=1,ensure_ascii=False))
    elif a[0]=='build':
        t=json.load(open(a[1])); open(a[2],'wb').write(build(t))
    elif a[0]=='test':
        t,_=load_project(a[1]); b=build(t); t2=decode(b,0)
        print('lossless' if t==t2 else 'DIFFERENT', len(b))
