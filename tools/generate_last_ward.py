#!/usr/bin/env python3
"""Optional desktop art/project generator for the original Last Ward demo.

Requires the Shadow Ops example's original actor atlas and gunshot, Pillow,
and both room backgrounds already present in last-ward_psx_data. Never edits Shadow Ops.
The generated project runs/builds natively without Python.
"""
import copy
import json
import math
import random
import shutil
from pathlib import Path
from PIL import Image, ImageDraw
import generate_shadow_models as art
ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/'examples/last-ward_psx_data'
SOURCE=ROOT/'examples/shadow-ops_psx_data'

def zombie(name,phase=0,dead=False):
    m=art.Mesh()
    def transform(p):
        x,y,z=p
        return (x,6+z*.15,y) if dead else p
    def box(a,b,t):art.box(m,a,b,art.tile(t,0),transform=transform)
    def bone(a,b,r,t,sides=4,end=False):art.bone(m,a,b,r,art.tile(t,0),transform=transform,sides=sides,end=end)
    box((-9,-2,-5),(9,23,5),1)
    bone((0,25,0),(0,38,0),6,0,6,True)
    # Cropped eyes and hair, rather than a second face on every head side.
    box((-4,33,-7),(-1,35,-6),3);box((1,33,-7),(4,35,-6),3)
    for side in (-1,1):
        stride=phase*side*6
        bone((side*5,-2,0),(side*5,-17,stride*.5),4.5,2,3)
        bone((side*5,-17,stride*.5),(side*5,-29,stride),3.5,2,3)
        box((side*5-4,-32,stride-8),(side*5+4,-27,stride+3),3)
        bone((side*9,20,0),(side*13,11,-14+phase*2),3.5,1,3)
        bone((side*13,11,-14+phase*2),(side*8,10,-27),2.6,0,3)
        box((side*8-3,8,-31),(side*8+3,13,-25),0)
    m.write(name)

def blood_art():
    # Established deterministic atlas workflow: crisp pixel clusters, transparent
    # background, nearest-neighbour sampling by the PS1 texture hardware.
    image=Image.new('RGBA',(16,16),(0,0,0,0));draw=ImageDraw.Draw(image)
    for rect,color in [((6,5,9,9),(176,18,24,255)),((4,6,6,7),(224,37,32,255)),
                       ((9,3,10,6),(208,24,30,255)),((9,8,12,9),(130,12,22,255)),
                       ((5,9,6,11),(192,22,28,255)),((2,3,3,4),(207,28,30,255)),
                       ((12,1,13,2),(230,42,36,255)),((13,11,14,12),(168,16,24,255)),
                       ((1,9,2,10),(150,12,22,255)),((8,13,9,14),(196,24,28,255))]:
        draw.rectangle(rect,fill=color)
    image.save(DATA/'blood.png')

def inventory_assets(project):
    """Regenerate UI props/effects and the authored rear doorway boundaries."""
    # The same code-native palette/icon rows draw the PS1 inventory and sprite.
    import re
    header=(ROOT/'runtime/icons.h').read_text()
    palette=[tuple(map(int,c)) for c in re.findall(r'\{(\d+),(\d+),(\d+)\}',header)]
    rows=re.findall(r'"([0-7]{32})"',header.split('psx_icon_key[PSX_ICON_HEIGHT]')[1].split('};')[0])
    image=Image.new('RGBA',(32,20),(0,0,0,0))
    for y,row in enumerate(rows):
        for x,pixel in enumerate(row):
            if pixel!='0':image.putpixel((x,y),(*palette[int(pixel)],255))
    image.save(DATA/'wardkey.png')
    # Effects reuse the actor atlas's existing warm/white pixels, freeing the
    # fourth texture slot for the key without changing blood or projectile size.
    actors=Image.open(DATA/'actors.png').convert('RGB')
    original=Image.open(DATA/'atlas.png').convert('RGB')
    source_points=[(36,38),(71,74)]
    samples=[]
    for point in source_points:
        color=original.getpixel(point)
        samples.append(min(((x,y) for y in range(128) for x in range(128)),
            key=lambda p:sum((actors.getpixel(p)[i]-color[i])**2 for i in range(3))))
    for name in ('tracer.obj','muzzle.obj'):
        lines=(DATA/name).read_text().splitlines()
        for i,line in enumerate(lines):
            if line.startswith('vt '):
                u,v=map(float,line.split()[1:]);index=0 if u<.4 else 1
                # Preserve the already-remapped white sample in the bottom quarter.
                if v<.25:index=min(range(2),key=lambda k:abs(u-(samples[k][0]+.5)/128)+abs(v-(1-(samples[k][1]+.5)/128)))
                x,y=samples[index];lines[i]=f'vt {(x+.5)/128:.7f} {1-(y+.5)/128:.7f}'
        (DATA/name).write_text('\n'.join(lines)+'\n')
    for scene in project['scenes']:
        if scene['id'] not in (2,3):continue
        data=scene['data']
        for o in data['objects']:
            if 100<=o['id']<=107:o['scale']=[.5,.5,1]
        for o in data['objects']:
            if o['id']==80 or 100<=o['id']<=107:o['texture']='actors.png'
            if o['id']==10:
                o['position'][2]=-250 if scene['id']==2 else -310
            if o['id']==30:
                o['position'][2]=-235 if scene['id']==2 else -295
                o['bounds']=[28,32,28];o['scale']=[28/24,32/24,28/24]
        if scene['id']==2:
            prop=next((o for o in data['objects'] if o['id']==31),None)
            if prop is None:
                prop=copy.deepcopy(next(o for o in data['objects'] if o['id']==80))
                prop.update(id=31,name='Ward Key - reception counter',position=[-224,64,30],bounds=[8,4,8],
                            solid=False,module='',collision_action=0,target_scene=0)
                data['objects'].insert(2,prop)
            prop.update(kind='billboard',asset='',texture='wardkey.png',rotation=[0,0,0],scale=[.7,.4375,1])
    active=next(scene['data'] for scene in project['scenes'] if scene['id']==project['active_scene'])
    project['objects']=copy.deepcopy(active['objects'])

def extend_sanitarium(project):
    """Add two connected camera angles without regenerating the existing rooms."""
    import re
    header=(ROOT/'runtime/icons.h').read_text()
    palette=[tuple(map(int,c)) for c in re.findall(r'\{(\d+),(\d+),(\d+)\}',header)]
    rows=re.findall(r'"([0-7]{32})"',header.split('psx_icon_ammo[PSX_ICON_HEIGHT]')[1].split('};')[0])
    image=Image.new('RGBA',(32,20),(0,0,0,0))
    for y,row in enumerate(rows):
        for x,pixel in enumerate(row):
            if pixel!='0':image.putpixel((x,y),(*palette[int(pixel)],255))
    image.save(DATA/'ammo.png')
    ward=next(sc['data'] for sc in project['scenes'] if sc['id']==3)
    def portal(data,id,name,x,z,target,angle=180):
        existing=next((o for o in data['objects'] if o['id']==id),None)
        if existing is None:
            existing=copy.deepcopy(next(o for o in data['objects'] if o['id']==30))
            data['objects'].insert(1,existing)
        existing.update(id=id,name=name,position=[x,32,z],rotation=[0,angle,0],
                        bounds=[28,32,28],scale=[28/24,32/24,28/24],
                        collision_action=3,target_scene=target,solid=False,asset='',texture='',module='')
    atrium=next(sc['data'] for sc in project['scenes'] if sc['id']==2)
    next(o for o in atrium['objects'] if o['id']==30)['rotation']=[0,180,0]
    portal(ward,30,'Door trigger - Atrium',0,190,2,0)
    portal(ward,33,'Door trigger - Observation',0,-295,4)
    for sid,name,bg,eye,zoom,positions in [
        (4,'Observation corridor','observation.png',(380,330,520),.64,[(-95,20),(110,80),(-110,140),(120,0)]),
        (5,'Medical archive','archive.png',(-380,380,520),.66,[(-95,80),(90,-100),(-80,140),(120,70)])]:
        if any(sc['id']==sid for sc in project['scenes']):continue
        data=copy.deepcopy(ward)
        data['objects']=[o for o in data['objects'] if o['id'] not in (31,33,35)]
        data['background']=bg
        camera=next(o for o in data['objects'] if o['id']==200)
        yaw=math.degrees(math.atan2(eye[0],eye[2]));pitch=math.degrees(math.atan2(eye[1],math.hypot(eye[0],eye[2])))
        pitch-=4 if sid==4 else 2
        camera.update(name='Fixed '+name.lower()+' camera',position=list(eye),rotation=[pitch,yaw,0],camera_zoom=zoom)
        # Legacy authoring view matches the fixed game camera, with no follow.
        y=math.radians(yaw);p=math.radians(pitch)
        data['camera']=[eye[0]-600*math.sin(y)*math.cos(p),eye[1]-600*math.sin(p),eye[2]-600*math.cos(y)*math.cos(p)-600]
        data['view']=[yaw,pitch,zoom]
        next(o for o in data['objects'] if o['id']==10)['position'][2]=-250
        next(o for o in data['objects'] if o['id']==14).update(name='Distant wall storage',position=[-360,50,0],bounds=[20,50,200])
        for o,(x,z) in zip([o for o in data['objects'] if 20<=o['id']<=23],positions):o['position']=[x,32,z]
        portal(data,30,'Door trigger - '+('Treatment ward' if sid==4 else 'Observation'),-110 if sid==4 else -70,-210,3 if sid==4 else 4)
        if sid==4:
            portal(data,33,'Door trigger - Archive',150,-210,5)
            ammo=copy.deepcopy(next(o for o in data['objects'] if o['id']==80))
            ammo.update(id=35,name='24 handgun rounds',kind='billboard',asset='',texture='ammo.png',
                        position=[30,12,10],rotation=[0,0,0],scale=[.7,.4375,1],bounds=[8,4,8],module='',solid=False)
            data['objects'].insert(2,ammo)
        project['scenes'].append(dict(id=sid,name=name,data=data))
    active=next(sc['data'] for sc in project['scenes'] if sc['id']==project['active_scene'])
    project['objects']=copy.deepcopy(active['objects'])

def add_depth_demo(project):
    """Add pharmacy and one-shot ammunition pickups to each playable area."""
    archive=next(sc['data'] for sc in project['scenes'] if sc['id']==5)
    if not any(o['id']==33 for o in archive['objects']):
        portal=copy.deepcopy(next(o for o in archive['objects'] if o['id']==30))
        portal.update(id=33,name='Door trigger - Pharmacy',position=[0,32,190],rotation=[0,0,0],target_scene=6)
        archive['objects'].insert(1,portal)
    if not any(sc['id']==6 for sc in project['scenes']):
        data=copy.deepcopy(next(sc['data'] for sc in project['scenes'] if sc['id']==2))
        data['objects']=[o for o in data['objects'] if o['id'] not in (31,33,35)]
        data['background']='pharmacy.png';data['background_layers']=[dict(path='counter.png',depth=550)]
        next(o for o in data['objects'] if o['id']==30).update(name='Door trigger - Archive',target_scene=5)
        next(o for o in data['objects'] if o['id']==200)['name']='Fixed pharmacy camera'
        next(o for o in data['objects'] if o['id']==2)['position']=[0,32,40]
        next(o for o in data['objects'] if o['id']==14).update(name='Pharmacy counter collision',position=[0,40,120],bounds=[130,40,40],scale=[130/24,40/24,40/24])
        for o,(x,z) in zip([o for o in data['objects'] if 20<=o['id']<=23],[(-60,0),(60,0),(-155,-100),(155,-60)]):o['position']=[x,32,z]
        project['scenes'].append(dict(id=6,name='Pharmacy - depth layers',data=data))
    positions={2:(110,12,80),3:(-140,12,70),4:(30,12,10),5:(80,12,80),6:(-145,12,-20)}
    for sc in project['scenes']:
        if sc['id'] not in positions:continue
        data=sc['data'];ammo=next((o for o in data['objects'] if o['id']==35),None)
        if ammo is None:
            ammo=copy.deepcopy(next(o for o in data['objects'] if o['id']==80));data['objects'].insert(2,ammo)
            ammo.update(id=35,name='24 handgun rounds',kind='billboard',asset='',texture='ammo.png',
                        module='',collision_action=0,target_scene=0,solid=False,rotation=[0,0,0],scale=[.7,.4375,1],bounds=[8,4,8])
        ammo['position']=list(positions[sc['id']])
    active=next(sc['data'] for sc in project['scenes'] if sc['id']==project['active_scene'])
    project['objects']=copy.deepcopy(active['objects'])

def expand_rooms(project):
    game=next(scene['data'] for scene in project['scenes'] if scene['id']==2)
    if not any(o['id']==81 for o in game['objects']):
        template=next(o for o in game['objects'] if o['id']==80)
        for i in range(4):
            o=copy.deepcopy(template)
            o.update(id=81+i,name='Pixel blood impact '+str(i+1),kind='billboard',asset='',texture='blood.png',
                     scale=[.6,.6,.6],bounds=[1,1,1])
            game['objects'].append(o)
    next(o for o in game['objects'] if o['id']==10)['position'][2]=-470
    room=copy.deepcopy(game);room['background']='ward-background.png'
    room['objects']=[o for o in room['objects'] if o['id']!=31]
    next(o for o in room['objects'] if o['id']==200)['name']='Fixed treatment ward camera'
    # The second room has beds at the sides rather than an atrium reception desk.
    desk=next(o for o in room['objects'] if o['id']==14)
    desk.update(name='Side bed collision volume',position=[-280,50,-70],bounds=[40,50,175])
    for o,(x,z) in zip([o for o in room['objects'] if 20<=o['id']<=23],
                       [(-120,-60),(125,-80),(-105,110),(120,150)]):
        o['position']=[x,32,z]
    for data,z,target in ((game,-355,3),(room,-415,2)):
        data['objects']=[o for o in data['objects'] if o['id']!=30]
        portal=copy.deepcopy(next(o for o in data['objects'] if o['id']==10))
        portal.update(id=30,name='Door trigger - '+('Treatment ward' if target==3 else 'Atrium'),
                      position=[0,32,z],bounds=[28,32,23],scale=[28/24,32/24,23/24],
                      solid=False,collision_action=3,target_scene=target,module='')
        data['objects'].insert(1,portal)
    project['scenes']=[scene for scene in project['scenes'] if scene['id']!=3]
    project['scenes'].append(dict(id=3,name='Treatment ward',data=room))
    if project['active_scene']==2:
        project['objects']=copy.deepcopy(game['objects'])
    blood_art()
    inventory_assets(project)

def generate():
    if any(not (SOURCE/name).exists() for name in ('actors.png','atlas.png','tracer.obj','muzzle.obj','shot.vag')):
        raise SystemExit('Install the original Shadow Ops example assets first.')
    if any(not (DATA/name).exists() for name in ('background.png','ward-background.png','observation.png','archive.png','pharmacy.png','counter.png','ward.c','menu.c')):
        raise SystemExit('Supply both backgrounds, ward.c and menu.c from the Last Ward example archive first.')
    if (ROOT/'examples/last-ward.psx').exists():
        raise SystemExit('Project exists; preserve/move it before regenerating the demo.')
    DATA.mkdir(parents=True,exist_ok=True);art.DATA=DATA
    for name in ('actors.png','atlas.png','tracer.obj','muzzle.obj','shot.vag'):shutil.copy2(SOURCE/name,DATA/name)
    for name,pose in (('agent.obj','rest'),('aim.obj','aim'),('run1.obj','run1'),('run2.obj','run2'),('kick.obj','kick')):
        art.character(name,pose=pose)
    for name,phase,dead in (('zombie.obj',0,False),('zwalk1.obj',1,False),('zwalk2.obj',-1,False),('zdead.obj',0,True)):
        zombie(name,phase,dead)
    image=Image.new('RGB',(128,128),(20,23,25));rng=random.Random(419)
    colors=[(139,152,129),(99,108,105),(74,67,59),(30,32,31)]
    for y in range(32):
        for x in range(128):
            color=colors[x//32];noise=rng.randrange(-14,15)
            image.putpixel((x,y),tuple(max(8,min(248,c+noise)) for c in color))
    draw=ImageDraw.Draw(image)
    for _ in range(20):
        x,y=rng.randrange(34,60),rng.randrange(2,30)
        draw.line((x,y,min(x+3,62),min(y+3,30)),fill=(78,42,37),width=2)
    draw.rectangle((96,96,127,127),fill=(8,8,8));image.save(DATA/'zombies.png')
    m=art.Mesh()
    for i in range(8):
        a,b=i*math.tau/8,(i+1)*math.tau/8
        m.face([(0,0,0),(math.cos(a)*15,0,math.sin(a)*10),(math.cos(b)*15,0,math.sin(b)*10)],(110,110))
    m.write('shadow.obj')
    def obj(id,name,asset='',texture='',pos=(0,0,0),scale=(1,1,1),bounds=(1,1,1),solid=False,kind='mesh',module=''):
        return dict(id=id,name=name,kind=kind,position=list(pos),rotation=[0,0,0],scale=list(scale),bounds=list(bounds),
            solid=solid,asset=asset,texture=texture,module=module,collider_offset=[0,0,0],follow_offset=[0,0,0],
            follow_target=0,target_scene=0,input_mode=0,collision_action=0,speed=2)
    camera=obj(200,'Fixed atrium camera',kind='camera',pos=(0,300,580),bounds=(16,16,16))
    camera.update(rotation=[27.35,0,0],camera_zoom=.62,camera_projection=1)
    objects=[obj(2,'Armed survivor','agent.obj','actors.png',(0,32,170),(2,2,2),(12,32,12),True,module='ward.c'),camera]
    for id,name,pos,bounds in ((10,'Back wall',(0,100,-270),(320,100,20)),(11,'Left wall',(-300,100,0),(20,100,300)),
                               (12,'Right wall',(300,100,0),(20,100,300)),(13,'Near wall',(0,100,270),(320,100,20)),
                               (14,'Reception desk',(-265,50,-110),(55,50,155))):
        objects.append(obj(id,name,pos=pos,bounds=bounds,solid=True,scale=tuple(v/24 for v in bounds)))
    for i,(x,z) in enumerate(((-130,-145),(120,-155),(-110,-30),(140,30))):
        objects.append(obj(20+i,'Shambling patient '+str(i+1),'zombie.obj','zombies.png',(x,32,z),bounds=(12,32,12),solid=True))
    for id,name in ((50,'aim.obj'),(51,'run1.obj'),(52,'run2.obj'),(53,'kick.obj')):
        objects.append(obj(id,'Survivor pose reference',name,'actors.png',(0,-1000,0),(2,2,2)))
    for id,name in ((60,'zwalk1.obj'),(61,'zwalk2.obj'),(62,'zdead.obj')):
        objects.append(obj(id,'Patient pose reference',name,'zombies.png',(0,-1000,0)))
    for i in range(5):objects.append(obj(70+i,'Ground contact shadow','shadow.obj','zombies.png',(0,1,0)))
    objects.append(obj(80,'Pistol muzzle flash','muzzle.obj','atlas.png',(0,-1000,0),(2,2,2)))
    for i in range(8):objects.append(obj(100+i,'Pistol shot pool','tracer.obj','atlas.png',(0,-1000,0),bounds=(3,3,3)))
    # Editor legacy look-at is derived from the same fixed eye and orientation.
    pitch=math.radians(27.35)
    base=dict(version=3,name='Last Ward',psyq_path='',dosbox='',output_path='',emulator_command='pcsxr -cdfile {cue}',
              camera=[0,300-600*math.sin(pitch),580-600*math.cos(pitch)-600],view=[0,27.35,.62],projection='perspective',
              active_camera=200,show_colliders=False,show_game_frame=True,drag_axis=1,background='background.png')
    game=copy.deepcopy(base);game['objects']=objects
    title=copy.deepcopy(base);titlecamera=copy.deepcopy(camera);titlecamera['module']='menu.c';title['objects']=[titlecamera]
    project=copy.deepcopy(game);project.update(active_scene=2,startup_scene=1,
        scenes=[dict(id=1,name='Last Ward - briefing',data=title),dict(id=2,name='Sanitarium atrium',data=game)])
    expand_rooms(project)
    extend_sanitarium(project)
    add_depth_demo(project)
    (ROOT/'examples/last-ward.psx').write_text(json.dumps(project,indent=2)+'\n')
    print('Created examples/last-ward.psx')
if __name__=='__main__':generate()
