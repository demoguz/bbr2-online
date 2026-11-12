#!/usr/bin/env python3
"""BBR2 static analysis helper. Usage: bbre.py <command> ...
  vt <Class> [n]        the class's vtable(s) and the first n slots
  dis <rva> [n]         disassemble n instructions (does not stop at the function end)
  fn <rva>              bounds of the function containing rva (.pdata)
  xref <rva>            code addresses that reference rva with rel32
  who <rva>             vtable slots that contain this function
  str <text>            search strings + xrefs
  cls <regex>           search class names
  bases <Class>         inheritance chain
"""
import sys,struct,re,json,os,bisect
sys.path.insert(0,'/tmp/bbr2tc')
HERE=os.path.dirname(os.path.abspath(__file__))
EXE=os.path.join(HERE,'..','..','Game_x64.exe')
CDIR=os.path.join(HERE,'.cache'); os.makedirs(CDIR,exist_ok=True)
CACHE=os.path.join(CDIR,'rtti.json')
D=open(EXE,'rb').read()
pe=struct.unpack_from('<I',D,0x3c)[0]
nsec=struct.unpack_from('<H',D,pe+6)[0]; optsz=struct.unpack_from('<H',D,pe+20)[0]
BASE=struct.unpack_from('<Q',D,pe+24+24)[0]
SECS=[]
for i in range(nsec):
    o=pe+24+optsz+40*i
    name=D[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',D,o+8)
    SECS.append((name,va,vs,ro,rs))
def sec(name): return next(s for s in SECS if s[0]==name)
def r2o(rva):
    for n,va,vs,ro,rs in SECS:
        if va<=rva<va+max(vs,rs): return ro+(rva-va) if rva-va<rs else None
def o2r(off):
    for n,va,vs,ro,rs in SECS:
        if ro<=off<ro+rs: return va+(off-ro)
def u32(rva): return struct.unpack_from('<I',D,r2o(rva))[0]
def u64(rva): return struct.unpack_from('<Q',D,r2o(rva))[0]
TEXT=sec('.text'); T0,T1=TEXT[1],TEXT[1]+TEXT[2]
def intext(rva): return T0<=rva<T1
# .pdata
dd=pe+24+ (112 if struct.unpack_from('<H',D,pe+24)[0]==0x20b else 96)
exc_rva,exc_sz=struct.unpack_from('<II',D,dd+8*3)
_po=r2o(exc_rva); PD=[struct.unpack_from('<III',D,_po+12*i) for i in range(exc_sz//12)]
PDB=[p[0] for p in PD]
def fn(rva):
    i=bisect.bisect_right(PDB,rva)-1
    if i>=0 and PD[i][0]<=rva<PD[i][1]: return PD[i][0],PD[i][1]
def rtti():
    if os.path.exists(CACHE): return json.load(open(CACHE))
    tds={}
    for m in re.finditer(rb'\.\?A[VU][\x21-\x7e]+?@@\x00',D):
        r=o2r(m.start())
        if r: tds[r-0x10]=m.group(0)[4:-3].decode()
    out={'vt':{}, 'bases':{}}
    for sn in ('.rdata','.data'):
        n,va,vs,ro,rs=sec(sn)
        cols={}
        for off in range(ro,ro+rs-0x18,4):
            if D[off:off+4]==b'\x01\0\0\0':
                sig,ofs,cd,td,chd,self_=struct.unpack_from('<IIIIII',D,off)
                if self_==va+(off-ro) and td in tds: cols[BASE+self_]=(tds[td],ofs,chd)
        for off in range(ro,ro+rs-8,8):
            q=struct.unpack_from('<Q',D,off)[0]
            if q in cols:
                name,ofs,chd=cols[q]; vt=va+(off-ro)+8
                out['vt'].setdefault(name,[]).append([vt,ofs])
                if name not in out['bases']:
                    try:
                        nb=u32(chd+8); arr=u32(chd+12)
                        out['bases'][name]=[tds.get(u32(u32(arr+4*i)),'?') for i in range(nb)]
                    except Exception: pass
    json.dump(out,open(CACHE,'w')); return out
def strings():
    p=os.path.join(CDIR,'strs.json')
    if os.path.exists(p): return {int(k,16):v for k,v in json.load(open(p)).items()}
    n,va,vs,ro,rs=sec('.rdata'); out={}
    for m in re.finditer(rb'[\x20-\x7e]{4,80}\x00',D[ro:ro+rs]): out[va+m.start()]=m.group(0)[:-1].decode()
    json.dump({hex(k):v for k,v in out.items()},open(p,'w')); return out
def fmap():
    p=os.path.join(CDIR,'fmap.json')
    if os.path.exists(p): return {int(k,16):v for k,v in json.load(open(p)).items()}
    R=rtti(); out={}
    for name,vts in R['vt'].items():
        for vt,ofs in vts:
            for i,s_ in enumerate(vtslots(vt)): out.setdefault(s_,f'{name}[{i}]')
    json.dump({hex(k):v for k,v in out.items()},open(p,'w')); return out
def strrefs(want):
    S=strings(); F=fmap(); res=[]
    for r,v in S.items():
        if v==want:
            for x in xref(r):
                f=fn(x); f0=f[0] if f else 0; res.append((x,f0,F.get(f0,'')))
    return res
def vtslots(vt,n=400):
    res=[]
    for i in range(n):
        try: q=u64(vt+8*i)
        except Exception: break
        if not intext(q-BASE): break
        res.append(q-BASE)
    return res
def dis(rva,n=40,stop_at_ret=False):
    import capstone
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
    o=r2o(rva); k=0
    for ins in md.disasm(D[o:o+n*15],BASE+rva):
        s=f'{ins.address-BASE:7x}  {ins.mnemonic} {ins.op_str}'
        m=re.search(r'rip ([+-]) (0x[0-9a-f]+)',ins.op_str)
        if m:
            t=ins.address+ins.size+(int(m.group(2),16) if m.group(1)=='+' else -int(m.group(2),16))-BASE
            s+=f'   ; ->{t:x}'
            oo=r2o(t)
            if oo and not intext(t):
                b=D[oo:oo+48].split(b'\0')[0]
                if len(b)>=4 and all(32<=c<127 for c in b): s+=f' "{b.decode()}"'
        elif ins.mnemonic in('call','jmp') and ins.op_str.startswith('0x'):
            s=f'{ins.address-BASE:7x}  {ins.mnemonic} {int(ins.op_str,16)-BASE:x}'
        print(s); k+=1
        if k>=n or (stop_at_ret and ins.mnemonic in('ret','int3')): break
def xref(target):
    import numpy as np
    n,va,vs,ro,rs=TEXT; buf=np.frombuffer(D,dtype=np.uint8,count=rs,offset=ro)
    res=[]
    b=buf.astype(np.int64)
    v=(b[:-3]|(b[1:-2]<<8)|(b[2:-1]<<16)|(b[3:]<<24))
    v=np.where(v>=2**31,v-2**32,v)
    idx=np.arange(len(v),dtype=np.int64)
    hit=np.nonzero(va+idx+4+v==target)[0]
    # imm-after-disp cases (e.g. cmp [rip+x],imm8 / mov [rip+x],imm32)
    for extra in (1,4):
        hit=np.concatenate([hit,np.nonzero(va+idx+4+extra+v==target)[0]])
    return sorted(set(int(va+h) for h in hit))
def who(rva,R):
    for name,vts in R['vt'].items():
        for vt,ofs in vts:
            sl=vtslots(vt)
            if rva in sl: print(f'{name} vt={vt:x}(+{ofs}) slot {sl.index(rva)}')
if __name__=='__main__':
    a=sys.argv[1:]; c=a[0] if a else ''
    if c=='vt':
        R=rtti()
        for vt,ofs in R['vt'].get(a[1],[]):
            sl=vtslots(vt); print(f'{a[1]} vtable rva={vt:x} this+{ofs} slots={len(sl)}')
            n=int(a[2]) if len(a)>2 else 12
            print(' '.join(f'{i}:{s:x}' for i,s in enumerate(sl[:n])))
    elif c=='dis': dis(int(a[1],16),int(a[2]) if len(a)>2 else 40)
    elif c=='fn': print(['%x'%x for x in (fn(int(a[1],16)) or [])])
    elif c=='xref':
        for x in xref(int(a[1],16)):
            f=fn(x); print(f'{x:x}  fn={f[0]:x}' if f else f'{x:x}')
    elif c=='who': who(int(a[1],16),rtti())
    elif c=='str':
        for m in re.finditer(re.escape(a[1].encode()),D):
            r=o2r(m.start()); 
            if not r: continue
            s=D[m.start()-0:m.start()+60].split(b'\0')[0]
            # string start
            st=m.start()
            while D[st-1]!=0: st-=1
            rs_=o2r(st); xs=xref(rs_)
            print(f'{rs_:x} "{D[st:st+80].split(bytes([0]))[0].decode("latin1")}" xrefs:',' '.join(f'{x:x}' for x in xs[:8]))
    elif c=='cls':
        R=rtti(); print(' '.join(sorted(n for n in R['vt'] if re.search(a[1],n))))
    elif c=='sref':
        for x,f0,nm in strrefs(a[1]): print('%x fn=%x %s'%(x,f0,nm))
    elif c=='bases': print(rtti()['bases'].get(a[1]))
    else: print(__doc__)
