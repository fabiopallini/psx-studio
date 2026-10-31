#!/usr/bin/env python3
"""Optional desktop asset generator; requires the separate Shadow Ops example.

Regenerates the integrated mountain village in campaign Scene 6.
Preserve manual Scene 6 scenery edits before regenerating.
Python/Pillow are generation tools, not editor or game build dependencies.
"""
import copy
import json
import math
import random
import shutil
from pathlib import Path
from PIL import Image, ImageDraw
import generate_shadow_models as art

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'examples/shadow-ops.psx'
DATA = SOURCE.with_name(SOURCE.stem + '_psx_data')


def uv(n):
    return art.tile(n % 4, n // 4)


def face(m, points, n, inside=None):
    coords = uv(n)
    m.face(points, coords[:len(points)], inside=inside)


def atlas():
    rng = random.Random(810)
    image = Image.new('RGB', (128, 128))
    # Snow, exposed schist, ruined plaster, aged beams; roof, glass, rust,
    # rubber; packed snow track, rock shadows, pale snow, evergreen needles.
    colors = [(193, 207, 215), (85, 95, 105), (150, 142, 126), (83, 65, 51),
              (81, 89, 99), (46, 68, 80), (107, 72, 57), (33, 38, 43),
              (152, 167, 173), (63, 74, 85), (222, 229, 228), (47, 73, 67),
              (111, 116, 114), (113, 52, 40), (178, 183, 179), (100, 114, 124)]
    for y in range(128):
        for x in range(128):
            n = (y // 32) * 4 + x // 32
            grain = rng.randrange(-12, 13)
            image.putpixel((x, y), tuple(max(8, min(248, c + grain)) for c in colors[n]))
    d = ImageDraw.Draw(image)
    for n in (2,):
        x, y = n % 4 * 32, n // 4 * 32
        for row in range(4, 32, 8):
            d.line((x, y+row, x+31, y+row-2), fill=(66, 71, 75))
            for col in range((row % 3)*4, 32, 13):
                d.line((x+col, y+row-2, x+col-2, y+row-8), fill=(73, 77, 78))
    for n in (1,9):
        x,y=n%4*32,n//4*32
        for _ in range(20):
            a,b=rng.randrange(32),rng.randrange(32)
            d.line((x+a,y+b,x+min(31,a+8),y+min(31,b+3)),fill=(61,73,85))
    for n in (3, 4):
        x, y = n % 4 * 32, n // 4 * 32
        for col in range(3, 32, 7):
            d.line((x+col, y, x+col-2, y+31), fill=(47, 49, 50))
    d.rectangle((33, 33, 62, 62), outline=(102, 115, 120), width=2)
    d.line((37, 57, 56, 38), fill=(130, 152, 163), width=2)
    d.line((46, 34, 45, 61), fill=(26, 37, 43))
    for _ in range(28):
        x, y = rng.randrange(65, 95), rng.randrange(33, 63)
        d.line((x, y, min(x+3, 94), min(y+2, 62)), fill=(158, 103, 65))
    # Tire ruts, with snow left along both sides.
    for x in (5, 23):
        d.line((x, 65, x, 94), fill=(103, 119, 127), width=3)
        for y in range(66, 94, 4):
            d.line((x-2, y, x+2, y+1), fill=(126, 140, 145))
    image.save(DATA / 'alpine.png')


def rock(name, hx, hy, hz, seed=1):
    rng = random.Random(seed)
    m = art.Mesh()
    # Rectangular collision footprint, irregular bevelled outline and snow cap.
    ring = [(-1,-.65),(-.7,-1),(.6,-1),(1,-.6),(1,.7),(.55,1),(-.65,1),(-1,.5)]
    lower = [(x*hx, -hy, z*hz) for x,z in ring]
    upper = [(x*hx*.75, hy*rng.uniform(.3, .8), z*hz*.76) for x,z in ring]
    centre = (-hx*.15, hy, hz*.1)
    for i in range(8):
        j = (i+1) % 8
        face(m, [lower[i], lower[j], upper[j], upper[i]], 1, (0,0,0))
        face(m, [upper[i], upper[j], centre], 10 if i%3 else 0, (0,-hy,0))
    m.write(name)


def ridge(name, hx, hy, hz, seed):
    rng = random.Random(seed)
    m = art.Mesh()
    # The inward edge meets the original solid boundary. Relief expands outward.
    count = max(2, int(max(hx,hz)/40))
    along_z = hz > hx
    length = hz if along_z else hx
    width = hx if along_z else hz
    for i in range(count):
        a = -length + 2*length*i/count
        b = -length + 2*length*(i+1)/count
        h = rng.uniform(hy*1.3, hy*3.5)
        pts = [(a,-hy,-width),(b,-hy,-width),(b,h,width*9),(a,h*.8,width*7)]
        if along_z: pts = [(z,y,x) for x,y,z in pts]
        face(m, pts, 1, (0,-hy-10,0))
        pts = [(a,h*.8,width*7),(b,h,width*9),(b,-hy,width*25),(a,-hy,width*25)]
        if along_z: pts = [(z,y,x) for x,y,z in pts]
        face(m, pts, 0, (0,-hy-10,0))
    m.write(name)


def car():
    m = art.Mesh()
    # A tapered, dented station wagon with sloping bonnet and broken glazing.
    rings = [(-44,15,3),(-36,21,11),(21,21,12),(43,17,7)]
    for (a,w,h),(b,v,k) in zip(rings,rings[1:]):
        face(m,[(-w,-8,a),(w,-8,a),(v,-8,b),(-v,-8,b)],7,(0,0,0))
        for side in (-1,1):
            face(m,[(side*w,-8,a),(side*v,-8,b),(side*v,k,b),(side*w,h,a)],6,(0,0,0))
        face(m,[(-w,h,a),(w,h,a),(v,k,b),(-v,k,b)],14)
    face(m,[(-15,-8,-44),(15,-8,-44),(15,3,-44),(-15,3,-44)],6,(0,0,0))
    face(m,[(-17,-8,43),(17,-8,43),(17,7,43),(-17,7,43)],6,(0,0,0))
    face(m,[(-19,12,-19),(19,12,-19),(16,29,-7),(-16,29,-7)],5)
    face(m,[(-16,29,-7),(16,29,-7),(15,27,23),(-15,27,23)],10)
    face(m,[(-15,27,23),(15,27,23),(19,12,32),(-19,12,32)],5)
    for side in (-1,1):
        face(m,[(side*19,12,-19),(side*16,29,-7),(side*15,27,23),(side*19,12,32)],5)
        art.box(m,(side*18-2,10,4),(side*18+2,27,7),uv(6))
        for z in (-27,26):
            # Six-sided tires have an eight-unit tread, rather than a flat disc.
            inner=[(side*18,-6+9*math.sin(i*math.pi/3),z+9*math.cos(i*math.pi/3)) for i in range(6)]
            outer=[(side*26,p[1],p[2]) for p in inner]
            m.face(outer,[uv(7)[i%4] for i in range(6)],inside=(side*22,-6,z))
            for i in range(6):
                j=(i+1)%6
                face(m,[inner[i],inner[j],outer[j],outer[i]],7,(side*22,-6,z))
    m.write('wreck.obj')


def mountain():
    m=art.Mesh()
    ring=[(-140,0,-80),(-65,14,-130),(80,0,-100),(150,0,20),(70,0,100),(-90,0,100)]
    summit=(-20,230,-15)
    for i in range(6):
        j=(i+1)%6
        mid=tuple(ring[i][k]*.45+summit[k]*.55 for k in range(3))
        nxt=tuple(ring[j][k]*.45+summit[k]*.55 for k in range(3))
        face(m,[ring[i],ring[j],nxt,mid],1 if i%2 else 9)
        face(m,[mid,nxt,summit],10 if i%3 else 0)
    m.write('peak.obj')


def village_cover_module(code):
    """Keep the generated extended-cover contract reproducible for old archives."""
    code=code.replace('static PsxObject *walls[12];',
        '/* Cover IDs 400..499 extend scenery without changing combat rules. */\nstatic PsxObject *walls[112];')
    return code.replace(
        'if((scene->objects[i].id>=10 && scene->objects[i].id<=19) || scene->objects[i].id==42 || scene->objects[i].id==44)',
        'if((scene->objects[i].id>=10 && scene->objects[i].id<=19) ||\n'
        '        (scene->objects[i].id>=400 && scene->objects[i].id<=499) ||\n'
        '        scene->objects[i].id==42 || scene->objects[i].id==44)')


def place_exit_checkpoints(project):
    """One explicit-confirmation checkpoint beside each authored campaign exit."""
    template=copy.deepcopy(next(o for scene in project['scenes'] for o in scene['data']['objects']
                                if o['id']==50 and o.get('texture')=='save.png'))
    for scene in project['scenes']:
        if scene['id']==1:continue
        objects=scene['data']['objects']
        objects[:]=[o for o in objects if o['id'] not in (50,51)]
        exits=sorted((o for o in objects if o['id'] in (40,41)),
                     key=lambda o:0 if o['position'][2]<0 else 1)
        for i,exit in enumerate(exits):
            x,_,z=exit['position'];north=z<0
            point=copy.deepcopy(template)
            point.update(id=50+i,name='Save checkpoint - '+('north exit' if north else 'south exit'),
                position=[x+(30 if north else -30),25,z+(67 if north else -67)],
                target_scene=exit['target_scene'] or scene['id'])
            objects.append(point)
    active=next(scene for scene in project['scenes'] if scene['id']==project['active_scene'])
    project['objects']=copy.deepcopy(active['data']['objects'])


def extend_village():
    """Expand campaign Scene 6 while keeping all other authored Scenes intact."""
    project=json.loads(SOURCE.read_text())
    scene=next((s for s in project['scenes'] if s['id']==6),None)
    if scene is None:raise SystemExit('Install the Shadow Ops example with mountain Scene 6 first.')
    destination=SOURCE.with_name('shadow-ops_psx_data')
    art.DATA=destination
    atlas();car();rock('boulder.obj',50,30,20,3);mountain()
    # Single exterior shells avoid hidden wall-box faces. Preserve UV materials,
    # damaged gables, chimney and surviving roof strips at gameplay distance.
    m=art.Mesh();hx,hz=48,37
    art.box(m,(-hx,-30,-hz),(hx,-18,hz),uv(1),top_uv=uv(12))
    for z in (-hz,hz):
        for xa,xb,top in ((-hx,-19,20),(-19,0,4),(15,hx,13)):
            face(m,[(xa,-18,z),(xb,-18,z),(xb,top,z),(xa,top-4,z)],2,(0,0,0))
    for x in (-hx,hx):
        face(m,[(x,-18,-hz),(x,-18,hz),(x,24,hz),(x,28,-hz)],2,(0,0,0))
        face(m,[(x,28,-hz),(x,24,hz),(x,49,0)],2,(0,0,0))
    for xa,xb in ((-52,-16),(24,52)):
        face(m,[(xa,50,0),(xb,50,0),(xb,23,43),(xa,23,43)],4)
        face(m,[(xa,51,0),(xb,51,0),(xb,26,-43),(xa,26,-43)],10,(0,-30,0))
    face(m,[(-50,24,-3),(50,24,-3),(50,24,3),(-50,24,3)],3)
    art.box(m,(26,21,-24),(38,64,-15),uv(1),top_uv=uv(10))
    face(m,[(-49,-7,-13),(-49,12,-13),(-49,12,8),(-49,-7,8)],5)
    for x,z in ((-12,9),(15,-12)):
        face(m,[(x,-15,z),(x+6,-12,z),(x+6,-12,z+24),(x,-15,z+24)],3)
    m.write('lodge.obj')
    m=art.Mesh();art.box(m,(-3,-2,-3),(3,48,3),uv(3))
    for y,r,h in ((18,23,42),(45,15,38)):
        for i in range(3):
            a,b=i*math.tau/3,(i+1)*math.tau/3
            face(m,[(r*math.cos(a),y,r*math.sin(a)),(r*math.cos(b),y,r*math.sin(b)),(0,y+h,0)],11)
            face(m,[(r*.88*math.cos(a),y+6,r*.88*math.sin(a)),(r*.88*math.cos(b),y+6,r*.88*math.sin(b)),(0,y+h+1,0)],10)
    m.write('fir-low.obj')
    # Large tiled valley; only its outer edge rises, outside the playable area.
    m=art.Mesh();rng=random.Random(611);nodes={}
    for iz in range(13):
        for ix in range(11):
            x,z=-700+140*ix,-960+160*iz
            edge=abs(x)>650 or abs(z)>880
            nodes[ix,iz]=(x+rng.randrange(-12,13),rng.randrange(15,50) if edge else 0,z+rng.randrange(-12,13))
    for iz in range(12):
        for ix in range(10):
            centre=160*math.sin(iz*.6)
            n=8 if abs(-630+140*ix-centre)<65 else 10 if (ix+iz)%4==0 else 0
            face(m,[nodes[ix,iz],nodes[ix+1,iz],nodes[ix+1,iz+1],nodes[ix,iz+1]],n)
    m.write('village.obj')
    # Fewer broad terrain facets, with irregular peaks; no repeated wall cubes.
    def bank(name,hx,hz,seed):
        rng=random.Random(seed);m=art.Mesh();along=hz>hx
        length=hz if along else hx;count=max(2,int(length/100))
        for i in range(count):
            a,b=-length+2*length*i/count,-length+2*length*(i+1)/count
            h=rng.randrange(80,180)
            for pts,n in (([(a,-50,-20),(b,-50,-20),(b,h,95),(a,h*.8,75)],1),
                          ([(a,h*.8,75),(b,h,95),(b,-50,260),(a,-50,260)],0)):
                if along:pts=[(z,y,x) for x,y,z in pts]
                face(m,pts,n,(0,-70,0))
        m.write(name)
    bank('bank-side.obj',20,870,1);bank('bank-long.obj',545,20,2);bank('bank-end.obj',75,20,3)
    objects=scene['data']['objects']
    objects[:]=[o for o in objects if o['id']<300]
    byid={o['id']:o for o in objects}
    byid[1].update(asset='village.obj',name='Snowbound mountain village')
    byid[2]['position']=[-470,20,730]
    for oid,x,rot in ((10,-650,180),(11,650,0)):
        byid[oid].update(asset='bank-side.obj',position=[x,50,0],rotation=[0,rot,0],bounds=[20,50,870])
    for oid,x,z,rot in ((12,-115,-850,180),(13,115,850,0)):
        byid[oid].update(asset='bank-long.obj',position=[x,50,z],rotation=[0,rot,0],bounds=[545,50,20])
    for oid,x,z,rot in ((18,-585,850,0),(19,585,-850,180)):
        byid[oid].update(asset='bank-end.obj',position=[x,50,z],rotation=[0,rot,0],bounds=[75,50,20])
    for oid,x,z in ((40,-470,882),(41,470,-882),(42,-470,850),(44,470,-850),(43,-470,850),(45,470,-850),(46,-470,872),(47,470,-872)):
        byid[oid]['position'][0]=x;byid[oid]['position'][2]=z
    byid[14].update(position=[-135,14,190],bounds=[26,22,44])
    byid[15].update(asset='lodge.obj',position=[95,30,-100],bounds=[48,30,40])
    byid[16].update(position=[-385,50,520],scale=[4.9,1.67,3.5],bounds=[245,50,70])
    byid[17].update(asset='lodge.obj',position=[-165,24,65],scale=[.8,.8,.8],bounds=[39,24,32])
    byid[20]['position']=[-310,20,80]
    byid[21]['position']=[300,20,-250]
    for oid,pos in ((30,[-310,18,430]),(31,[220,18,-520]),(32,[-455,18,685]),(33,[240,18,-30])):
        byid[oid]['position']=pos
    template=byid[16]
    def add(oid,name,asset,pos,scale=(1,1,1),bounds=(1,1,1),solid=False):
        o=copy.deepcopy(template)
        o.update(id=oid,name=name,asset=asset,position=list(pos),scale=list(scale),bounds=list(bounds),
                 rotation=[0,0,0],solid=solid,module='',texture='alpine.png')
        objects.append(o)
    # Five buildings form staggered streets around a broad central square.
    for oid,pos in ((400,[-320,39,-170]),(401,[320,39,175]),(402,[85,39,-530]),(403,[-235,39,-580])):
        add(oid,'Ruined village lodge','lodge.obj',pos,scale=(1.7,1.3,1.5),bounds=(82,39,60),solid=True)
    # Rock pairs leave deliberate 80-120 unit necks between their collision edges.
    for oid,x,z,hx,hz in ((410,305,520,325,70),(411,-430,-410,220,65),(412,280,-410,370,65),
                         (413,-360,315,80,80),(414,-70,315,90,80)):
        add(oid,'Snow-capped rock passage','boulder.obj',[x,40,z],(hx/50,1.33,hz/20),(hx,40,hz),True)
    add(420,'Abandoned village car','wreck.obj',[210,14,-190],bounds=(26,22,44),solid=True)
    for i,(x,z) in enumerate(((-535,530),(520,405),(-520,-610),(530,-650))):
        add(300+i,'Snow-covered mountain fir','fir-low.obj',[x,0,z],(1.4,1.8,1.4))
    for i,(x,z) in enumerate(((-850,-650),(0,-1180),(860,-850))):
        add(340+i,'Distant alpine peak','peak.obj',[x,0,z],(2,2,2))
    mission=destination/'mission.c'
    mission.write_text(village_cover_module(mission.read_text()))
    project['active_scene']=6
    scene['data']['camera']=[0,40,-600]
    scene['data']['view']=[0,60,.35]
    for key,value in scene['data'].items():
        if key not in ('name','psyq_path','dosbox','emulator_command','output_path'):
            project[key]=copy.deepcopy(value)
    place_exit_checkpoints(project)
    temporary=SOURCE.with_suffix('.psx.tmp');temporary.write_text(json.dumps(project,indent=2)+'\n');temporary.replace(SOURCE)
    print('Expanded Scene 6 into a mountain village in '+str(SOURCE.relative_to(ROOT)))


if __name__=='__main__':
    extend_village()
