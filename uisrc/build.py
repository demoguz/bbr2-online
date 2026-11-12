#!/usr/bin/env python3
"""Generates the online UI screens: starts from the game's own screens, changes them and compiles them to ui/*.fc.
The carrier map is written to ui/overrides.txt (asset name = file). All texts are in English."""
import sys,os,json,copy,struct
HERE=os.path.dirname(os.path.abspath(__file__)); ROOT=os.path.join(HERE,'..')
sys.path.insert(0,os.path.join(ROOT,'tools')); import fc
OUT=os.path.join(ROOT,'ui')
# Carriers: assets the Premium build never opens
SCREEN_ONLINE='Screens_Simple/Extra'
SCREEN_HOSTLOBBY='Screens_Simple/Leaderboard'
SCREEN_CLIENTLOBBY='Screens_Simple/Credits'
POPUP_KEYBOARD='Popups_Tutorial/Onboarding_NameA'      # PopupDB Dialogs: Tutorial_Onboarding_NameA
DIALOG_KEYBOARD='Tutorial_Onboarding_NameA'
HUD_1P='HUDs_Premium/1P_CommonHUD'
SCREEN_TRACK='Screens_Premium/QuickEvent_Race'
SCREEN_CAR='Screens_Premium/SplitScreen_CarSelect1'

def vec(*f): return {'$bin':struct.pack('<%df'%len(f),*f).hex()}
def find(lst,name):
    for e in (lst if isinstance(lst,list) else lst.values()):
        if e.get('name')==name: return e
    raise KeyError(name)
def kids(e): return e['data']['ChildEntities']
def plugs(e): return e.setdefault('data',{}).setdefault('Components',{}).setdefault('VuScriptComponent',{}).setdefault('Plugs',{})
def tchild(e,name):   # template override (dict) or a real child entity (list)
    ch=e.setdefault('data',{}).setdefault('ChildEntities',{})
    return find(ch,name) if isinstance(ch,list) else ch.setdefault(name,{})
def link(a,ap,b,bp):   # write the plug at both ends (that is the game's format). a,b = (entity, path)
    ea,pa=a; eb,pb=b
    plugs(ea).setdefault(ap,[]).append('%s:%s'%(pb,bp))
    plugs(eb).setdefault(bp,[]).append('%s:%s'%(pa,ap))
def unlink(a,ap,b,bp):
    ea,pa=a; eb,pb=b
    for e,p,s in ((ea,ap,'%s:%s'%(pb,bp)),(eb,bp,'%s:%s'%(pa,ap))):
        l=plugs(e).get(p,[])
        if s in l: l.remove(s)
        if p in plugs(e) and not plugs(e)[p]: del plugs(e)[p]
def set_pos(e,x,y,z=0.0): e.setdefault('data',{}).setdefault('Components',{}).setdefault('VuTransformComponent',{}).setdefault('Properties',{})['Position']=vec(x,y,z)
def set_texts(btn,head,sub=None):
    ch=btn.setdefault('data',{}).setdefault('ChildEntities',{})
    for k in ('Elements Selected','Elements Unselected'):
        c=ch.setdefault(k,{}).setdefault('ChildEntities',{})
        c.setdefault('Text',{}).setdefault('Properties',{})['String ID']=head
        if sub is not None: c.setdefault('TextSub',{}).setdefault('Properties',{})['String ID']=sub
def entity(t,name,props=None):
    e={'type':t,'name':name,'data':{}}
    if props: e['data']['Properties']=props
    return e
def save(name,tree):
    fn=name.replace('/','_')+'.fc'
    open(os.path.join(OUT,fn),'wb').write(fc.build(tree)); return name,fn

# ---------- 1) Main menu: Online button ----------
def build_main():
    t,_=fc.load_project('VuProjectAsset/Screens_Premium/Main'); t=copy.deepcopy(t)
    root=kids(t['RootEntity'])
    menu=kids(find(root,'Controls Menu')); scr=kids(find(root,'Scripting'))
    P='~/Controls Menu/'
    career=find(menu,'1 Button_Career'); qe=find(menu,'3 Button_QuickEvent'); tour=find(menu,'5 Button_Tournaments')
    online=copy.deepcopy(career); online['name']='4 Button_Online'
    online['data']['Components']['VuScriptComponent']['Plugs']={}
    set_texts(online,'Online','Race your friends over the internet')
    menu.insert(menu.index(tour),online)
    ys={'1 Button_Career':(0,262),'2 Button_Championships':(-12,382),'3 Button_QuickEvent':(-25,502),'4 Button_Online':(-38,622),
        '5 Button_Tournaments':(-51,742),'6 Button_Collection':(-72,852),'7 Button_Info':(-82,932),'9 Button Ach':(-92,1012)}
    for e in menu:
        if e['name'] in ys: set_pos(e,ys[e['name']][0],ys[e['name']][1])
    qch=qe['data']['ChildEntities']
    def rm(lst,s):
        if s in lst: lst.remove(s)
    rm(qch['NavDown']['Components']['VuScriptComponent']['Plugs']['Out'],'~/Controls Menu/5 Button_Tournaments:SetFocus')
    tp=plugs(tour); rm(tp['SetFocus'],'~/Controls Menu/3 Button_QuickEvent/NavDown:Out'); rm(tp['NavUp'],'~/Controls Menu/3 Button_QuickEvent/SetFocus:In')
    rm(qch['SetFocus']['Components']['VuScriptComponent']['Plugs']['In'],'~/Controls Menu/5 Button_Tournaments:NavUp')
    qch['NavDown']['Components']['VuScriptComponent']['Plugs']['Out'].append(P+'4 Button_Online:SetFocus')
    plugs(online)['SetFocus']=[P+'3 Button_QuickEvent/NavDown:Out', P+'5 Button_Tournaments:NavUp']
    plugs(online)['NavUp']=[P+'3 Button_QuickEvent/SetFocus:In']
    qch['SetFocus']['Components']['VuScriptComponent']['Plugs']['In'].append(P+'4 Button_Online:NavUp')
    plugs(online)['NavDown']=[P+'5 Button_Tournaments:SetFocus']
    tp['SetFocus'].append(P+'4 Button_Online:NavDown'); tp['NavUp']=[P+'4 Button_Online:SetFocus']
    push=entity('VuPushScreenUIAction','4 Push_Online',{'Screen':SCREEN_ONLINE}); scr.append(push)
    sfx=find(kids(find(root,'Audio')),'SFX_Select')
    link((online,P+'4 Button_Online'),'Pressed',(push,'~/Scripting/4 Push_Online'),'Trigger')
    link((online,P+'4 Button_Online'),'Pressed',(sfx,'~/Audio/SFX_Select'),'Start')
    # after connecting in the background through a Steam invite the code sends BBR_GoOnline: the Online screen opens and moves on to the lobby
    ge=entity('VuGenericEventEntity','GE BBR_GoOnline',{'Event Name':'BBR_GoOnline'}); scr.append(ge)
    link((ge,'~/Scripting/GE BBR_GoOnline'),'OnReceived',(push,'~/Scripting/4 Push_Online'),'Trigger')
    return save('Screens_Premium/Main',t)

# ---------- 2) Menu-style screens (from the Info screen) ----------
class Screen:
    """Skeleton of the Info screen: title, buttons in the left column, texts on the right, events in Scripting."""
    def __init__(s,header):
        t,_=fc.load_project('VuProjectAsset/Screens_Premium/Info'); t=copy.deepcopy(t); s.t=t
        root=kids(t['RootEntity'])
        keep=[e for e in root if e['name'] in ('Audio','Background_Frame01','Controls','Scripting')]
        t['RootEntity']['data']['ChildEntities']=keep; root=keep
        find(root,'Background_Frame01')['data']['ChildEntities']['Text Header']['Properties']['String ID']=header
        audio=find(root,'Audio'); audio['data']['ChildEntities']=[find(kids(audio),'AudioEventSelect')]
        s.sel=kids(audio)[0]; plugs(s.sel)['Start']=[]
        cl=kids(find(root,'Controls'))
        s.btmpl=find(cl,'01 Stats'); s.back=find(cl,'Button_Back01'); s.img=find(cl,'Image Back Wide'); s.ttmpl=find(cl,'Text Name')
        s.cl=cl; sl=kids(find(root,'Scripting')); s.sl=sl
        s.pop=find(sl,'PopScreen01'); s.init=find(sl,'OnProjectInitialized01'); s.title=find(sl,'Screen_Title'); s.focus=find(sl,'UIFocusSave')
        s.rel=find(sl,'OnProjectRelease01')
        s.focus['data']['Components']['VuScriptComponent']['Plugs']['OnDefaultFocus']=[]
        sl[:]=[s.pop,s.init,s.rel,s.title,s.focus]
        s.btns=[]; s.texts=[]; s.ges={}
    def ge(s,ev):
        if ev not in s.ges:
            e=entity('VuGenericEventEntity','GE '+ev,{'Event Name':ev}); s.sl.append(e); s.ges[ev]=e
        return (s.ges[ev],'~/Scripting/GE '+ev)
    def script(s,e): s.sl.append(e); return (e,'~/Scripting/'+e['name'])
    def push(s,screen):
        nm='Push '+screen.split('/')[-1]
        for e in s.sl:
            if e['name']==nm: return (e,'~/Scripting/'+nm)
        return s.script(entity('VuPushScreenUIAction',nm,{'Screen':screen}))
    def button(s,nm,label):
        b=copy.deepcopy(s.btmpl); b['name']=nm; b['data']['Components']['VuScriptComponent']['Plugs']={}
        set_texts(b,label); s.btns.append(b); return (b,'~/Controls/'+nm)
    def text(s,nm,sid,x,y,w=1050.0,align='left',font=' Soft_Small',wrap=False):
        e=copy.deepcopy(s.ttmpl); e['name']=nm
        p=e['data']['Properties']; p.update({'String ID':sid,'Horizontal Alignment':align,'Rect':vec(0.0,0.0,w,0.0),'Font':font})
        if not wrap: p.pop('Wordbreak',None)
        else: p['Wordbreak']=True
        set_pos(e,x,y,-99.0); e['data'].get('Components',{}).get('VuScriptComponent',{}).pop('Plugs',None)
        s.texts.append(e); return e
    def on_init(s,ev): link((s.init,'~/Scripting/OnProjectInitialized01'),'Trigger',s.ge(ev),'Broadcast')
    def on_release(s,ev): link((s.rel,'~/Scripting/OnProjectRelease01'),'Trigger',s.ge(ev),'Broadcast')
    def on_event(s,ev,target,plug): link(s.ge(ev),'OnReceived',target,plug)
    def pressed(s,b,target,plug): link(b,'Pressed',target,plug)
    def back_also(s,target,plug): link((tchild(s.back,'Pressed'),'~/Controls/Button_Back01/Pressed'),'Out',target,plug)
    def finish(s,carrier,y0=330.0,dy=78.0):
        P='~/Controls/'
        for i,b in enumerate(s.btns):
            set_pos(b,-20-8*i,y0+dy*i)
            if i>0: link((b,P+b['name']),'NavUp',(s.btns[i-1],P+s.btns[i-1]['name']),'SetFocus')
            if i<len(s.btns)-1: link((b,P+b['name']),'NavDown',(s.btns[i+1],P+s.btns[i+1]['name']),'SetFocus')
            link((b,P+b['name']),'Pressed',(s.sel,'~/Audio/AudioEventSelect'),'Start')
        b0=s.btns[0]; link((b0,P+b0['name']),'SetFocus',(s.focus,'~/Scripting/UIFocusSave'),'OnDefaultFocus')
        s.cl[:]=s.btns+[s.back,s.img]+s.texts
        return save(carrier,s.t)

def info_texts(s,lobby):
    X=700.0
    if lobby:
        s.text('Text Info','[[BBR_LOBBYINFO]]',X,250.0,font=' Soft_Medium')
        for i in range(8): s.text('Text P%d'%(i+1),'[[BBR_P%d]]'%(i+1),X,330.0+62*i)
        s.text('Text Addr','[[BBR_ADDR]]',X,850.0,font=' Soft_XSmall')
    else:
        s.text('Text Status','[[BBR_STATUS]]',X,340.0,w=1000.0,wrap=True)
    s.text('Text Toast','[[BBR_TOAST]]',X,930.0,font=' Soft_Small')

def build_online():
    s=Screen('Online')
    kb=s.script(entity('VuDialogEntity','Dialog Keyboard',{'Type':DIALOG_KEYBOARD}))
    b=s.button('01 Create','Create Lobby'); s.pressed(b,s.ge('BBR_CreateLobby'),'Broadcast')
    b=s.button('02 Join','Join Lobby'); s.pressed(b,s.ge('BBR_JoinOpen'),'Broadcast'); s.pressed(b,kb,'Show')
    b=s.button('03 Name','Name: [[BBR_NAME]]'); s.pressed(b,s.ge('BBR_NameOpen'),'Broadcast'); s.pressed(b,kb,'Show')
    b=s.button('04 Back','Back to Menu'); s.pressed(b,(s.pop,'~/Scripting/PopScreen01'),'Trigger')
    s.on_init('BBR_OnlineShown'); s.on_release('BBR_OnlineHidden')
    s.on_event('BBR_GoHostLobby',s.push(SCREEN_HOSTLOBBY),'Trigger')
    s.on_event('BBR_GoClientLobby',s.push(SCREEN_CLIENTLOBBY),'Trigger')
    info_texts(s,False)
    return s.finish(SCREEN_ONLINE,y0=350.0,dy=85.0)

def build_lobby(host):
    s=Screen('Online Lobby')
    leave=s.ge('BBR_Leave'); popt=(s.pop,'~/Scripting/PopScreen01')
    if host:
        b=s.button('01 Start','Start Race'); s.pressed(b,s.ge('BBR_StartRace'),'Broadcast')
        b=s.button('02 Track','Choose Track'); s.pressed(b,s.ge('BBR_ChooseTrack'),'Broadcast'); s.pressed(b,s.push(SCREEN_TRACK),'Trigger')
    b=s.button('03 Car','Choose Car & Driver'); s.pressed(b,s.ge('BBR_ChooseCar'),'Broadcast'); s.pressed(b,s.push(SCREEN_CAR),'Trigger')
    if host:
        for nm,lab,ev in (('04 Size','Lobby Size: [[BBR_SIZE]]','BBR_Size'),('05 Bots','Bots: [[BBR_BOTS]]','BBR_Bots'),
                          ('06 Equal','Same Car for All: [[BBR_EQUAL]]','BBR_Equal'),('07 Fx','Shared Power-Ups: [[BBR_FX]]','BBR_Fx')):
            b=s.button(nm,lab); s.pressed(b,s.ge(ev),'Broadcast')
    else:
        b=s.button('04 Ready','Ready: [[BBR_READY]]'); s.pressed(b,s.ge('BBR_Ready'),'Broadcast')
    b=s.button('09 Leave','Leave Lobby'); s.pressed(b,leave,'Broadcast'); s.pressed(b,popt,'Trigger')
    s.back_also(leave,'Broadcast')
    s.on_init('BBR_LobbyShown'); s.on_release('BBR_LobbyHidden')
    s.on_event('BBR_LobbyClosed',popt,'Trigger')
    info_texts(s,True)
    return s.finish(SCREEN_HOSTLOBBY if host else SCREEN_CLIENTLOBBY,y0=300.0,dy=74.0)

# ---------- 3) Keyboard popup (from the presets keyboard) ----------
def build_keyboard():
    t,_=fc.load_project('VuProjectAsset/Popups_Premium/Dialog_KeyboardPresets'); t=copy.deepcopy(t)
    root=kids(t['RootEntity'])
    el=kids(find(root,'Elements')); ek=find(root,'Elements Keyboard'); ekl=kids(ek)
    sc=find(root,'Scripting'); scl=kids(sc); au=kids(find(root,'Audio'))
    find(el,'Text Heading')['data']['Properties']['String ID']='[[BBR_KB_HEAD]]'
    find(el,'Text Body')['data']['Properties']['String ID']='[[BBR_KB_BODY]]'
    find(el,'Text Remaining')['data']['Properties']['String ID']='([[KEYBOARD_CHARS_REMAINING]])'
    K='~/Elements Keyboard/'
    pk=find(ekl,'PresetKeyboard'); pk['type']='VuGenericKeyboardEntity'; pk['name']='TextKeyboard'
    pp=pk['data']['Properties']; pp['Min Length']=0; pp['Max Length']=24
    pk['data']['Components']['VuScriptComponent']['Plugs']={}
    r5=kids(find(ekl,'Keys Row 05')); acc=find(r5,'GameButton Accept')
    A=(acc,K+'Keys Row 05/GameButton Accept')
    plugs(acc)['Pressed']=[]
    alert=find(au,'AudioEventAlert'); plugs(alert)['Start']=[]
    sel=find(au,'AudioEventSelect'); plugs(sel)['Start']=[]
    ok=find(scl,'CloseDialog_OnAccept'); plugs(ok)['Trigger']=[]
    scl[:]=[e for e in scl if not e['name'].startswith('MessageBox')]
    def se(e): return (e,'~/Scripting/'+e['name'])
    ge_acc=entity('VuGenericEventEntity','GE BBR_KbAccept',{'Event Name':'BBR_KbAccept'}); scl.append(ge_acc)
    ge_ent=entity('VuGenericEventEntity','GE BBR_KbEnter',{'Event Name':'BBR_KbEnter'}); scl.append(ge_ent)
    ge_cls=entity('VuGenericEventEntity','GE BBR_KbClosed',{'Event Name':'BBR_KbClosed'}); scl.append(ge_cls)
    rel=entity('VuOnProjectReleaseEntity','OnProjectRelease01'); scl.append(rel)
    link(A,'Pressed',se(ge_acc),'Broadcast'); link(A,'Pressed',(sel,'~/Audio/AudioEventSelect'),'Start'); link(A,'Pressed',se(ok),'Trigger')
    link(se(ge_ent),'OnReceived',se(ge_acc),'Broadcast'); link(se(ge_ent),'OnReceived',(sel,'~/Audio/AudioEventSelect'),'Start'); link(se(ge_ent),'OnReceived',se(ok),'Trigger')
    link(se(rel),'Trigger',se(ge_cls),'Broadcast')
    # a "." key in the empty 3rd position
    r2=find(ekl,'Keys Row 02'); r3=find(ekl,'Keys Row 03'); r4=find(ekl,'Keys Row 04')
    r3l=kids(r3); k9=find(r3l,'GameButton09'); fr=find(r3l,'Frame')
    dot=copy.deepcopy(k9); dot['name']='GameButton10'; set_pos(dot,1200.0,0.0,0.0)
    dch=dot['data']['ChildEntities']; dch['Key']['ChildEntities']['Text']['Properties']['Key']='.'
    for k in ('SetFocus','NavUp','NavLeft','NavDown'): dch[k]['Components']['VuScriptComponent']['Plugs']={}
    r3l[r3l.index(fr)]=dot
    p2=K+'Keys Row 02/GameButton10'; p3=K+'Keys Row 03/GameButton09'; pd=K+'Keys Row 03/GameButton10'; pb=K+'Keys Row 04/GameButton08_Backspace'
    k10=find(kids(r2),'GameButton10'); bs=find(kids(r4),'GameButton08_Backspace')
    def ch(e,n,path): return (tchild(e,n),path+'/'+n)
    unlink(ch(k10,'NavDown',p2),'Out',ch(bs,'SetFocus',pb),'In')
    link(ch(k10,'NavDown',p2),'Out',ch(dot,'SetFocus',pd),'In')
    link(ch(dot,'NavUp',pd),'Out',ch(k10,'SetFocus',p2),'In')
    link(ch(k9,'NavRight',p3),'Out',ch(dot,'SetFocus',pd),'In')
    link(ch(dot,'NavLeft',pd),'Out',ch(k9,'SetFocus',p3),'In')
    link(ch(dot,'NavDown',pd),'Out',ch(bs,'SetFocus',pb),'In')
    return save(POPUP_KEYBOARD,t)

# ---------- 5) ConstantDB: screen stacks for returning to the lobby ----------
def build_constantdb():
    import lz4.block, apf
    d=lz4.block.decompress(apf.get('VuDBAsset/ConstantDB'),uncompressed_size=1<<24)
    t=fc.decode(d,8); ss=t['ScreenStacks']
    base=['Screens_Premium/Title','Screens_Premium/Main',SCREEN_ONLINE]
    ss['BBR_HostLobby']=base+[SCREEN_HOSTLOBBY]; ss['BBR_ClientLobby']=base+[SCREEN_CLIENTLOBBY]
    body=fc.build(t); out=struct.pack('<II',1,len(body)+8)+body
    open(os.path.join(OUT,'ConstantDB.db'),'wb').write(out); return 'ConstantDB','ConstantDB.db'

# ---------- 4) Race screen: online notice line ----------
def build_hud():
    t,_=fc.load_project('VuProjectAsset/'+HUD_1P); t=copy.deepcopy(t)
    vp=find(kids(t['RootEntity']),'Viewport00')
    txt=entity('VuUITextEntity','BBR Online Text',{'Font':' Outline_Medium','String ID':'[[BBR_HUD]]','Rect':vec(-800.0,0.0,1600.0,0.0),
        'Horizontal Alignment':'center','Vertical Alignment':'center','ShrinkToFit':True})
    set_pos(txt,960.0,215.0,0.0)
    kids(vp).append(txt)
    return save(HUD_1P,t)

exec(open(os.path.join(HERE,'lobby_cards.py'),encoding='utf-8').read())   # the card lobby replaces build_lobby

if __name__=='__main__':
    os.makedirs(OUT,exist_ok=True)
    res=[build_main(),build_online(),build_lobby(True),build_lobby(False),build_keyboard(),build_hud(),build_constantdb(),build_quickevent()]
    print('random track list:',write_tracks())
    with open(os.path.join(OUT,'overrides.txt'),'w') as f:
        for n,fn in res: f.write('%s=%s\n'%(n,fn))
    for n,fn in res: print(n,'->',fn,os.path.getsize(os.path.join(OUT,fn)))
