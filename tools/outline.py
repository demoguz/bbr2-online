#!/usr/bin/env python3
"""outline.py <rva> [maxins]: summary view of a function - only string/global references and calls."""
import sys,re,json,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); import bbre
import capstone
fmap=bbre.fmap()
def outline(rva,maxins=4000,full=False):
    f=bbre.fn(rva); start,end=(f if f else (rva,rva+0x400))
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.skipdata=True
    o=bbre.r2o(start); D=bbre.D; B=bbre.BASE; k=0
    print(f'fn {start:x}-{end:x}',fmap.get(start,''))
    for ins in md.disasm(D[o:o+(end-start)],B+start):
        k+=1
        if k>maxins: break
        a=ins.address-B; s=None
        m=re.search(r'rip ([+-]) (0x[0-9a-f]+)',ins.op_str)
        if m:
            t=ins.address+ins.size+(int(m.group(2),16) if m.group(1)=='+' else -int(m.group(2),16))-B
            oo=bbre.r2o(t)
            if oo is not None and not bbre.intext(t):
                b=D[oo:oo+60].split(b'\0')[0]
                if len(b)>=3 and all(32<=c<127 for c in b): s=f'"{b.decode()}"'
                elif ins.mnemonic in('mov','cmp','lea') : s=f'g_{t:x}' + (' (lea)' if ins.mnemonic=='lea' else '')
            elif ins.mnemonic=='lea' and bbre.intext(t): s=f'&fn_{t:x} {fmap.get(t,"")}'
            if s and ins.mnemonic=='call': s='call ['+s+']'
        elif ins.mnemonic=='call':
            if ins.op_str.startswith('0x'):
                t=int(ins.op_str,16)-B; s=f'call {t:x} {fmap.get(t,"")}'
            else: s=f'call {ins.op_str}'
        elif ins.mnemonic=='jmp' and ins.op_str.startswith('0x'):
            t=int(ins.op_str,16)-B
            if not (start<=t<end): s=f'tailjmp {t:x} {fmap.get(t,"")}'
        if full and not s: s=f'{ins.mnemonic} {ins.op_str}'
        if s: print(f' {a:7x} {s}')
if __name__=='__main__':
    outline(int(sys.argv[1],16),int(sys.argv[2]) if len(sys.argv)>2 else 4000,len(sys.argv)>3)
