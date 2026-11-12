# Card lobby (SpeedRunners style): track image and buttons on the left, 8 player cards on the right, chat at the bottom.
# Included by build.py (exec) and replaces build_lobby.
# Card states are changed from code: the event "BBR_C<n>_<State>" shows that state's box and hides the others.
CARD_STATES=['Player','Choosing','Open','Bot','Closed']
def text_e(name,sid,x,y,w=300.0,align='center',font=' Soft_Small',z=-4.0):
    e=entity('VuUITextEntity',name,{'Font':font,'String ID':sid,'Rect':vec(-w/2 if align=='center' else (-w if align=='right' else 0.0),0.0,w,0.0),
        'Horizontal Alignment':align,'Vertical Alignment':'center','ShrinkToFit':True})
    set_pos(e,x,y,z); return e
def image_e(name,props,x,y,z):
    e=entity('VuUIImageEntity',name,props); set_pos(e,x,y,z); return e
def box(name,children,hidden=True,x=0.0,y=0.0):
    e={'type':'VuUIContainerEntity','name':name,'data':{'ChildEntities':children}}
    if hidden: e['data']['Components']={'VuUIComponent':{'Properties':{'Hidden':True}}}
    if x or y: set_pos(e,x,y,0.0)
    return e
def ge_e(ev): return entity('VuGenericEventEntity','GE '+ev,{'Event Name':ev})
CARD_X=[690.0,1030.0,1370.0,1710.0]; CARD_Y=[292.0,642.0]; CH=165.0
ICON={'Choosing':'Powerup/Mystery','Open':'UI/Alert_Question','Bot':'DriverIcons/Config_Robot','Closed':'UI/Lock_locked'}
def make_card(i,host,ges):
    n=i+1; P='~/Cards/Card %d'%n
    c=entity('VuViewportContainerEntity','Card %d'%n,{'Viewport':i}); set_pos(c,CARD_X[i%4],CARD_Y[i//4],0.0)
    ch=[]
    if host: ch.append(box('Highlight',[image_e('Image',{'Color':vec(1.0,0.87,0.0,1.0),'Rect':vec(-170.0,-CH-10,340.0,2*CH+20),'Texture Asset':'UI/Buttons_Console/MessageBack_Wonk'},0,0,97.0)]))
    ch.append(image_e('Frame Back',{'Color':vec(0.0,0.22,0.55,1.0),'Rect':vec(-160.0,-CH,320.0,2*CH),'Texture Asset':'UI/Buttons_Console/MessageBack_Wonk'},0,0,95.0))
    ch.append(image_e('Frame',{'Rect':vec(-150.0,-CH+10,300.0,2*CH-20),'Texture Asset':'UI/Buttons_Console/MessageBack_GradientShort','Mask Image':'UI/Buttons_Console/MessageBack_WonkMask'},0,0,90.0))
    v=entity('VuSplitScreenConfigVehicleImageEntity','Vehicle',{'Texture Width':300,'Texture Height':200,'Rect':vec(-138.0,-128.0,276.0,184.0),'Camera Name':'IconLandscape2d'}); set_pos(v,0,0,5.0)
    d=entity('VuSplitScreenConfigDriverImageEntity','Driver',{'Rect':vec(-136.0,-146.0,56.0,64.0),'Prefix':'DriverIcons/Config_'}); set_pos(d,0,0,3.0)
    states={'Player':box('State Player',[v,d])}
    for st,tex in ICON.items():
        col=vec(1.0,1.0,1.0,0.55) if st=='Open' else vec(1.0,1.0,1.0,1.0)
        states[st]=box('State '+st,[image_e('Icon',{'Color':col,'Rect':vec(-62.0,-128.0,124.0,124.0),'Texture Asset':tex},0,0,5.0)])
    ch+= [states[s] for s in CARD_STATES]
    ch.append(text_e('Text Name','[[BBR_S%d_NAME]]'%n,0,70.0,font=' Soft_Small'))
    ch.append(text_e('Text State','[[BBR_S%d_STATE]]'%n,0,105.0,font=' Soft_XSmall'))
    ch.append(text_e('Text Car','[[BBR_S%d_CAR]]'%n,0,133.0,font=' Soft_XSmall'))
    if host:
        b=entity('VuGameButtonEntity','Button',{'Touch Rect':vec(-160.0,-CH,320.0,2*CH)}); ch.append(b)
        link((b,P+'/Button'),'OnFocus',(ch[0],P+'/Highlight'),'Show')
        link((b,P+'/Button'),'OnFocusLost',(ch[0],P+'/Highlight'),'Hide')
    c['data']['ChildEntities']=ch
    for st in CARD_STATES:   # state event: show its own box, hide the others
        g=ge_e('BBR_C%d_%s'%(n,st)); ges.append(g); gp='~/Cards/GE BBR_C%d_%s'%(n,st)
        for other in CARD_STATES:
            link((g,gp),'OnReceived',(states[other],P+'/State '+other),'Show' if other==st else 'Hide')
    return c
def card_button(cards,i): c=cards[i]; return (find(kids(c),'Button'),'~/Cards/Card %d/Button'%(i+1))

def build_lobby(host):
    s=Screen('Online Lobby')
    leave=s.ge('BBR_Leave'); popt=(s.pop,'~/Scripting/PopScreen01')
    kb=s.script(entity('VuDialogEntity','Dialog Keyboard',{'Type':DIALOG_KEYBOARD}))
    if host:
        b=s.button('01 Start','Start Race'); s.pressed(b,s.ge('BBR_StartRace'),'Broadcast')
        b=s.button('02 Track','Choose Track'); s.pressed(b,s.ge('BBR_ChooseTrack'),'Broadcast'); s.pressed(b,s.push(SCREEN_TRACK),'Trigger')
    b=s.button('03 Car','Choose Car & Driver'); s.pressed(b,s.ge('BBR_ChooseCar'),'Broadcast'); s.pressed(b,s.push(SCREEN_CAR),'Trigger')
    if host:
        b=s.button('06 Equal','Same Car for All: [[BBR_EQUAL]]'); s.pressed(b,s.ge('BBR_Equal'),'Broadcast')
    else:
        b=s.button('04 Ready','Ready: [[BBR_READY]]'); s.pressed(b,s.ge('BBR_Ready'),'Broadcast')
    b=s.button('07 Invite','Invite Friends'); s.pressed(b,s.ge('BBR_Invite'),'Broadcast')
    b=s.button('08 Chat','Chat'); s.pressed(b,s.ge('BBR_ChatOpen'),'Broadcast'); s.pressed(b,kb,'Show')
    b=s.button('09 Leave','Leave Lobby'); s.pressed(b,leave,'Broadcast'); s.pressed(b,popt,'Trigger')
    s.back_also(leave,'Broadcast')
    s.on_init('BBR_LobbyShown'); s.on_release('BBR_LobbyHidden')
    s.on_event('BBR_LobbyClosed',popt,'Trigger')
    # left column (Controls box offset -20,-80): track image or the random icon, its name, its info
    TX,TY=255.0,330.0; W=H=222.0; MASK='UI/Buttons_Console/MessageBack_WonkMask'
    s.texts.append(image_e('Track Frame',{'Color':vec(0.0,0.22,0.55,1.0),'Rect':vec(-W/2-12,-H/2-12,W+24,H+24),'Texture Asset':'UI/Buttons_Console/MessageBack_Wonk'},TX,TY,60.0))
    ti=entity('VuCurrentTrackImageEntity','Image',{'Rect':vec(-W/2,-H/2,W,H),'Mask Image':MASK}); set_pos(ti,0,0,50.0)
    real=box('Track Real',[ti],x=TX,y=TY)
    rnd=box('Track Random',[image_e('Image',{'Rect':vec(-W/2,-H/2,W,H),'Texture Asset':'UI/Champs/Banner_ChampRand4','Src Rect':vec(0.1,0.0,0.346,1.0),'Mask Image':MASK},0,0,50.0)],x=TX,y=TY)
    s.texts+= [real,rnd]
    for ev,show,hide in (('BBR_TrackRandom',rnd,real),('BBR_TrackReal',real,rnd)):
        s.on_event(ev,(show,'~/Controls/'+show['name']),'Show'); s.on_event(ev,(hide,'~/Controls/'+hide['name']),'Hide')
    s.texts.append(text_e('Text Track','[[BBR_TRACKNAME]]',TX,TY+158.0,w=440.0,font=' Soft_Medium'))
    s.texts.append(text_e('Text TrackInfo','[[BBR_TRACKINFO]]',TX,TY+196.0,w=440.0,font=' Soft_XSmall'))
    # right side: cards, hint, chat; top strip: address and notices
    ges=[]; cards=[make_card(i,host,ges) for i in range(8)]
    extra=[text_e('Text Hint','[[BBR_LOBBYHINT]]',660.0,835.0,w=1220.0,align='left',font=' Soft_XSmall')]
    for k in range(4): extra.append(text_e('Text Chat%d'%(k+1),'[[BBR_CHAT%d]]'%(k+1),660.0,862.0+25*k,w=1220.0,align='left',font=' Soft_XSmall'))
    extra.append(text_e('Text Addr','[[BBR_ADDR]]',1880.0,42.0,w=900.0,align='right',font=' Soft_XSmall'))
    extra.append(text_e('Text Toast','[[BBR_TOAST]]',1880.0,80.0,w=1100.0,align='right',font=' Soft_Small'))
    root=kids(s.t['RootEntity'])
    root.append({'type':'VuUIContainerEntity','name':'Cards','data':{'ChildEntities':cards+extra+ges}})
    if host:   # card navigation (2x4) and switching the slot on press
        for i in range(8):
            r,c=i//4,i%4; me=card_button(cards,i)
            link(me,'Pressed',s.ge('BBR_Slot%d'%(i+1)),'Broadcast'); link(me,'Pressed',(s.sel,'~/Audio/AudioEventSelect'),'Start')
            if c<3: link(me,'NavRight',card_button(cards,i+1),'SetFocus')
            if c>0: link(me,'NavLeft',card_button(cards,i-1),'SetFocus')
            if r==0: link(me,'NavDown',card_button(cards,i+4),'SetFocus')
            if r==1: link(me,'NavUp',card_button(cards,i-4),'SetFocus')
            if c==0: link(me,'NavLeft',(s.btns[0],'~/Controls/'+s.btns[0]['name']),'SetFocus')
        for b in s.btns: link((b,'~/Controls/'+b['name']),'NavRight',card_button(cards,0),'SetFocus')
    return s.finish(SCREEN_HOSTLOBBY if host else SCREEN_CLIENTLOBBY,y0=600.0,dy=58.0)

# ---------- Quick Event track screen: was "Random" picked? ----------
def build_quickevent():
    t,_=fc.load_project('VuProjectAsset/'+SCREEN_TRACK); t=copy.deepcopy(t)
    tm,_=fc.load_project('VuTemplateAsset/UI_Premium/QuickEvent_ConfigScreen')
    tg=find(kids(find(kids(find(kids(tm['RootEntity']),'Controls Track Grid')),'UIScrollRegion01')),'TrackGrid')
    orig=tg['data']['Components']['VuScriptComponent']['Plugs']
    root=kids(t['RootEntity']); inst=root[0]
    SRC='~/'+inst['name']+'/Controls Track Grid/UIScrollRegion01/TrackGrid'
    ov=inst.setdefault('data',{}).setdefault('ChildEntities',{}).setdefault('Controls Track Grid',{}).setdefault('ChildEntities',{})\
        .setdefault('UIScrollRegion01',{}).setdefault('ChildEntities',{}).setdefault('TrackGrid',{})
    pl=ov.setdefault('Components',{}).setdefault('VuScriptComponent',{}).setdefault('Plugs',{})
    for plug,ev in (('OnRandom','BBR_QRandom'),('OnNotRandom','BBR_QNotRandom'),('NavSelect','BBR_QSelect')):
        g=ge_e(ev); root.append(g)
        pl[plug]=list(orig.get(plug,[]))+['~/GE %s:Broadcast'%ev]
        plugs(g)['Broadcast']=[SRC+':'+plug]
    return save(SCREEN_TRACK,t)

def write_tracks():   # list for the random track: the game's track images (Track_Setting)
    import apf
    out=[]
    for n,*_ in apf.toc():
        if n.startswith('VuTextureAsset/UI/Tracks/') and '/Previews/' not in n:
            nm=n.split('/')[-1]
            if '_' in nm and not nm.startswith('HW'): out.append(nm)
    open(os.path.join(OUT,'tracks.txt'),'w').write('\n'.join(sorted(set(out)))+'\n'); return len(out)
