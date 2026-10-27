#!/usr/bin/env python3
"""Optional desktop generator for Shadow Ops' original industrial sectors."""
import copy
import json
import math
from pathlib import Path
from generate_shadow_models import Mesh, box, bone, tile
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/'examples/shadow-ops_psx_data'

def generate():
    m=Mesh();bone(m,(0,-3,0),(0,3,0),3,tile(3,0),end=True,sides=6);box(m,(-1,3,-1),(1,5,1),tile(2,3));m.write('grenade.obj')
    m=Mesh()
    for axis in (0,1):
        points=[(-1,0,0),(0,2,0),(1,0,0),(0,-1,0)]
        if axis:points=[(0,y,x) for x,y,z in points]
        m.face(points,(71,74))
    m.write('blast.obj')
    m=Mesh()
    for i in range(12):
        a=i*math.pi/6;b=(i+1)*math.pi/6
        m.face([(math.cos(a)*r,0,math.sin(a)*r) for r in (48,55)]+[(math.cos(b)*r,0,math.sin(b)*r) for r in (55,48)],(71,74))
    m.write('warning.obj')
    m=Mesh();bone(m,(0,-30,0),(0,30,0),17,tile(0,0),end=True,sides=8);m.write('tank.obj')
    m=Mesh();bone(m,(-65,0,0),(65,0,0),5,tile(1,0),end=True,sides=6);m.write('pipe.obj')
    m=Mesh()
    outline=[(-21,-18),(21,-18),(24,-15),(24,15),(21,18),(-21,18),(-24,15),(-24,-15)]
    for i in range(8):
        j=(i+1)%8
        m.face([(outline[k][0],y,outline[k][1]) for k,y in ((i,-20),(j,-20),(j,20),(i,20))],tile(2,0),inside=(0,0,0))
    m.face([(x,20,z) for x,z in outline],[(65+(x+24)*29/48,33+(z+18)*29/36) for x,z in outline],inside=(0,0,0));m.write('cargo.obj')
    m=Mesh()
    for iz in range(7):
        for ix in range(8):
            x=-300+ix*75;z=-260+iz*75
            # Floor tiles, a warm service lane and darker perimeter drainage.
            column=3 if ix==4 else 1 if ix in (0,7) else 0
            m.face([(x,0,z),(x+75,0,z),(x+75,0,z+75),(x,0,z+75)],tile(column,2),inside=(x+37,-10,z+37))
    m.write('relay.obj')
    m=Mesh();box(m,(-5,-35,-5),(5,35,5),tile(1,0));box(m,(-10,28,-8),(10,35,8),tile(3,0));m.write('lamp.obj')
    # A deliberately restrained hand-drawn atlas: oxidized metal, seams, vents,
    # amber safety stripes and pale luminaire panels. No borrowed artwork.
    image=Image.new('RGB',(128,128));d=ImageDraw.Draw(image)
    colors=[(57,73,72),(75,85,89),(69,78,65),(159,143,86)]
    for y in range(128):
        for x in range(128):
            c=colors[x//32];noise=((x*17+y*29+x*y*3)%13)-6
            image.putpixel((x,y),tuple(max(0,min(255,v+noise)) for v in c))
    for x in range(0,128,32):
        d.rectangle((x,0,x+31,31),outline=(25,34,37),width=2)
        for y in range(4,30,6):d.line((x+4,y,x+27,y),fill=(37,48,50))
    for y in range(33,128,16):
        d.line((0,y,127,y),fill=(32,43,46));d.line((0,y+1,127,y+1),fill=(93,102,100))
    # Floor panels and restrained warning chevrons use their own atlas row.
    for x in (0,32,64):
        d.rectangle((x,64,x+31,95),fill=(48+x//8,61+x//10,65+x//10),outline=(26,37,41),width=2)
        for y in (68,90):
            for xx in (x+4,x+27):d.rectangle((xx,y,xx+1,y+1),fill=(125,133,129))
    d.rectangle((96,64,127,95),fill=(48,58,60))
    for y in range(65,95,8):d.polygon([(98,y),(102,y),(126,y+6),(126,y+10)],fill=(151,126,55))
    d.rectangle((98,2,125,29),fill=(218,199,132),outline=(59,69,67),width=3)
    image.save(DATA/'industry.png')
    p=ROOT/'examples/shadow-ops.psx';project=json.loads(p.read_text())
    base=next(s for s in project['scenes'] if s['id']==3)
    project['scenes']=[s for s in project['scenes'] if s['id'] not in (4,5)]
    for sid,name in ((4,'Grenadier Foundry'),(5,'Night Relay Complex')):
        s=copy.deepcopy(base);s.update(id=sid,name=name)
        objects=s['data']['objects'];template=copy.deepcopy(next(o for o in objects if o['id']==68))
        objects[:]=[o for o in objects if o['id']!=50] # Existing checkpoint format supports sectors 1/2.
        for o in objects:
            if o['id']==40:o['target_scene']=3 if sid==4 else 4
            if o['id']==41:o['target_scene']=5 if sid==4 else 0
            # Shot UVs sample the original bright effect texels, not scenery.
            if o.get('texture')=='atlas.png' and o.get('asset') not in ('tracer.obj','muzzle.obj'):
                o['texture']='industry.png'
            if o['id']==1:o['name']=name+' ground'
            if o['id']==2:o['position']=[-220,20,180]
            if o['id']==20:o['position']=[-160,20,-90];o['rotation']=[0,180,0]
            if o['id']==21:o['position']=[160,20,-150];o['rotation']=[0,180,0]
        def add(id,name,asset,pos,scale=(1,1,1),texture='industry.png'):
            o=copy.deepcopy(template);o.update(id=id,name=name,asset=asset,position=list(pos),scale=list(scale),texture=texture,solid=False,bounds=[1,1,1],module='',target_scene=0);objects.append(o)
        add(79,'Guard grenade throw pose','gthrow.obj',(0,-1000,0),(1.25,)*3,'actors.png')
        for i in range(2):
            add(160+i,'Impact grenade pool','grenade.obj',(0,-1000,0))
            add(162+i,'Impact explosion pool','blast.obj',(0,-1000,0),texture='atlas.png')
            add(164+i,'Grenade landing warning','warning.obj',(0,-1000,0),texture='')
        for i,(x,z) in enumerate(((-260,-180),(260,-180),(-260,40),(260,40))):
            add(300+i,'Pressure vessel','tank.obj',(x,30,z))
        for i,(x,z) in enumerate(((-180,-225),(150,-225))):add(310+i,'Overhead conduit','pipe.obj',(x,70,z))
        if sid==5:
            for o in objects:
                if o['id']==1:o['asset']='relay.obj'
                if o['id'] in (14,16):
                    o['asset']='cargo.obj'
                    o['scale']=[o['bounds'][0]/24,o['bounds'][1]/20,o['bounds'][2]/18]
            for i,(x,z) in enumerate(((-250,120),(250,120),(-250,-120),(250,-120))):add(320+i,'Amber service lamp','lamp.obj',(x,35,z))
            # Recompose cover into a central offset route while preserving half extents.
            for o in objects:
                if o['id']==14:o['position']=[-80,35,40]
                if o['id']==15:o['position']=[90,35,-80]
        project['scenes'].append(s)
    # Warehouse's extraction passage now leads into the added campaign sectors.
    for o in base['data']['objects']:
        if o['id']==41:o['target_scene']=4
    project['objects']=copy.deepcopy(next(s for s in project['scenes'] if s['id']==project['active_scene'])['data']['objects'])
    p.write_text(json.dumps(project,indent=2)+'\n')
if __name__=='__main__':generate()
