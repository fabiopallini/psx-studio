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

def generate():
    if any(not (SOURCE/name).exists() for name in ('actors.png','atlas.png','tracer.obj','muzzle.obj','shot.vag')):
        raise SystemExit('Install the original Shadow Ops example assets first.')
    if any(not (DATA/name).exists() for name in ('background.png','ward-background.png','ward.c','menu.c')):
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
    (ROOT/'examples/last-ward.psx').write_text(json.dumps(project,indent=2)+'\n')
    print('Created examples/last-ward.psx')
if __name__=='__main__':generate()
