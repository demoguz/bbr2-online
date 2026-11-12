#!/usr/bin/env python3
"""uitree.py <json>: prints a readable summary of a screen tree."""
import json,struct,sys
def vec(b):
    d=bytes.fromhex(b['$bin'])
    return tuple(round(x,2) for x in struct.unpack('<%df'%(len(d)//4),d)) if len(d)%4==0 else b['$bin']
def short(v):
    if isinstance(v,dict) and '$bin' in v: return vec(v)
    return v
def walk(e,depth=0,maxd=99):
    if depth>maxd: return
    d=e.get('data',{}) or {}; pr=d.get('Properties',{}) or {}; comp=d.get('Components',{}) or {}
    pos=((comp.get('VuTransformComponent') or {}).get('Properties') or {}).get('Position')
    plugs=((comp.get('VuScriptComponent') or {}).get('Plugs')) or {}
    s='  '*depth+'%s [%s]'%(e.get('name','?'),e.get('type','?'))
    if pos: s+=' pos=%s'%(vec(pos),)
    if pr: s+=' '+json.dumps({k:short(v) for k,v in pr.items()},ensure_ascii=False)[:170]
    if plugs: s+=' plugs='+json.dumps(plugs)[:230]
    print(s)
    ch=d.get('ChildEntities')
    if isinstance(ch,list):
        for c in ch: walk(c,depth+1,maxd)
    elif isinstance(ch,dict):
        for k,c in ch.items(): c=dict(c); c.setdefault('name',k); c.setdefault('type','(template content)'); walk(c,depth+1,maxd)
if __name__=='__main__':
    j=json.load(open(sys.argv[1])); walk(j['RootEntity'],0,int(sys.argv[2]) if len(sys.argv)>2 else 99)
