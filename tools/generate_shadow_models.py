#!/usr/bin/env python3
"""Original low-poly tactical characters and immutable animation poses.

Desktop geometry generation only. Uses the dedicated tactical actors atlas;
no synthesis, floating point or mutable vertices run on the console.
"""
import copy
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'examples/shadow-ops_psx_data'

class Mesh:
    def __init__(self):
        self.vertices = []
        self.uvs = []
        self.faces = []

    def face(self, points, uv, inside=None):
        if len(uv) == 2 and isinstance(uv[0], (int, float)):
            uv = [uv] * len(points)
        # Sloped shoulders and tapered jaws need explicit planar triangles.
        if len(points)>3:
            for i in range(1,len(points)-1):
                self.face([points[j] for j in (0,i,i+1)],
                          [uv[j] for j in (0,i,i+1)],inside)
            return
        if inside is not None:
            a=[points[1][i]-points[0][i] for i in range(3)]
            b=[points[2][i]-points[0][i] for i in range(3)]
            normal=[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
            if sum(normal[i]*(points[0][i]-inside[i]) for i in range(3))>0:
                points=list(reversed(points));uv=list(reversed(uv))
        start = len(self.vertices) + 1
        self.vertices.extend(points)
        self.uvs.extend(uv)
        self.faces.append(list(range(start, start + len(points))))

    def write(self, name):
        text = ['# Original Shadow Ops tactical art; generated immutable pose.']
        text += ['v ' + ' '.join(f'{v:.4f}' for v in p) for p in self.vertices]
        text += [f'vt {(u + .5) / 128:.7f} {1 - (v + .5) / 128:.7f}' for u, v in self.uvs]
        text += ['f ' + ' '.join(f'{i}/{i}' for i in f) for f in self.faces]
        (DATA / name).write_text('\n'.join(text) + '\n')
        return sum(len(f) - 2 for f in self.faces)


def tile(column, row):
    x=column*32; y=row*32
    return [(x+1,y+30),(x+30,y+30),(x+30,y+1),(x+1,y+1)]


def box(mesh, lower, upper, uv, cap=True, front=None, transform=lambda p:p, top_uv=None, back_uv=None):
    x,y,z = lower;X,Y,Z = upper
    faces = [[(x,y,z),(X,y,z),(X,Y,z),(x,Y,z)],
             [(X,y,Z),(x,y,Z),(x,Y,Z),(X,Y,Z)],
             [(x,y,Z),(x,y,z),(x,Y,z),(x,Y,Z)],
             [(X,y,z),(X,y,Z),(X,Y,Z),(X,Y,z)]]
    if cap:faces += [[(x,Y,z),(X,Y,z),(X,Y,Z),(x,Y,Z)]]
    for i, face in enumerate(faces):
        face_uv=top_uv if i==4 and top_uv else back_uv if i==1 and back_uv else front if i==0 and front else uv
        mesh.face([transform(p) for p in face],face_uv,inside=transform(((x+X)/2,(y+Y)/2,(z+Z)/2)))


def bone(mesh, a, b, radius, uv, end=False, transform=lambda p:p, sides=4, taper=1):
    # Low-sided sleeve/trouser section; no hidden internal caps.
    direction = [b[i]-a[i] for i in range(3)]
    length = math.sqrt(sum(v*v for v in direction))
    direction = [v / length for v in direction]
    side = [direction[1],-direction[0],0]
    length = math.sqrt(sum(v*v for v in side))
    side = [v / length for v in side]
    other = [direction[1]*side[2]-direction[2]*side[1],
             direction[2]*side[0]-direction[0]*side[2],
             direction[0]*side[1]-direction[1]*side[0]]
    rings = [[tuple(p[i]+radius*(taper if ring else 1)*(s*side[i]+t*other[i]) for i in range(3))
              for s,t in ([(-1,-1),(1,-1),(1,1),(-1,1)] if sides==4 else
                         [(math.cos(i*2*math.pi/sides),math.sin(i*2*math.pi/sides)) for i in range(sides)])] for ring,p in enumerate((a,b))]
    for i in range(sides):
        j=(i+1)%sides;mesh.face([transform(p) for p in [rings[0][i],rings[0][j],rings[1][j],rings[1][i]]],[uv[3],uv[2],uv[1],uv[0]] if isinstance(uv[0],tuple) else uv,inside=transform(tuple((a[k]+b[k])/2 for k in range(3))))
    if end:mesh.face([transform(p) for p in rings[1]],(80,118),inside=transform(tuple((a[k]+b[k])/2 for k in range(3))))


def character(name, guard=False, pose='rest'):
    m=Mesh()
    side_x=64 if guard else 0
    shade=[(side_x+2,30),(side_x+10,30),(side_x+10,1),(side_x+2,1)]
    chest=tile(2 if guard else 0,0)
    back=tile(3 if guard else 1,0)
    run=pose in ('run1','run2','run3')
    stride=1 if pose=='run1' else -1 if pose=='run2' else 0
    twist=lean=0
    if pose=='kick':lean=5
    if pose=='recover':lean=2
    if run:lean=-5 if stride else -3
    def upper(p):
        x,y,z=p
        a=math.radians(twist);x,z=x*math.cos(a)+z*math.sin(a),z*math.cos(a)-x*math.sin(a)
        a=math.radians(lean);y,z=y*math.cos(a)-z*math.sin(a),y*math.sin(a)+z*math.cos(a)
        return (x,y+(1 if run and not stride else 0),z)
    # Bevelled vest and sloping shoulders, with a narrower waist. The same
    # connected silhouette is retained in every immutable animation pose.
    profile=[(-3.8,-1),(3.8,-1),(5,-.55),(5,.55),(3.8,1),(-3.8,1),(-5,.55),(-5,-.55)]
    lower=[(x*.8,-2,z*3) for x,z in profile]
    top=[(x*1.22,11.5 if abs(x)>4 else 13,z*3.8) for x,z in profile]
    for i in range(len(top)):
        j=(i+1)%len(top)
        m.face([upper(p) for p in [lower[i],lower[j],top[j],top[i]]],
               chest if i==0 else back if i==4 else shade,inside=upper((0,5,0)))
    m.face([upper(p) for p in top],(76,70) if guard else (12,70),inside=upper((0,5,0)))
    bone(m,(0,12,0),(0,16,0),1.9,(110,53) if guard else (46,53),transform=upper,sides=5)
    for side in [-1,1]:
        x=side*3;step=side*stride*6 if run else 0
        lifted=run and side*stride>0
        foot_y=-10 if lifted else -15 if run and not stride else -16
        knee_z=-1 if lifted else step*0.7-1
        centres=[(x,-2,0),(x,-6 if lifted else -9,knee_z),
                 (x,foot_y+3,step),(x,foot_y,step)]
        # Six-sided trousers taper into a separate boot collar. The sole keeps
        # the original ground contact; its forward toe reads from all angles.
        profile=[(-.65,-1),(.65,-1),(1,0),(.65,1),(-.65,1),(-1,0)]
        profiles=[[(dx*r,dz*r) for dx,dz in profile] for r in (2.7,2.35,1.85,2.1)]
        profiles[-1]=[(dx*2.25,dz*2.5-1.25) for dx,dz in profile]
        rings=[[(cx+dx,cy,cz+dz) for dx,dz in outline]
               for (cx,cy,cz),outline in zip(centres,profiles)]
        tx=96 if guard else 32
        for segment in range(3):
            vtop,vbottom=((65,79),(79,88),(88,94))[segment]
            uv=[(tx+10,vbottom),(tx+20,vbottom),(tx+20,vtop),(tx+10,vtop)]
            for i in range(6):
                j=(i+1)%6
                points=[rings[segment+1][i],rings[segment+1][j],rings[segment][j],rings[segment][i]]
                inside=tuple((centres[segment][k]+centres[segment+1][k])/2 for k in range(3))
                m.face(points,uv,inside=inside)
    # A six-sided head with a tapered jaw replaces the rectangular head box.
    outline=[(-2.6,-3.3),(2.6,-3.3),(3.8,-.5),(3.1,2.8),(-3.1,2.8),(-3.8,-.5)]
    jaw=[(x*.72,14.5,z*.85) for x,z in outline]
    crown=[(x,21,z) for x,z in outline]
    head_x=96 if guard else 32
    hair=[(head_x+2,39),(head_x+29,39),(head_x+29,33),(head_x+2,33)]
    for i in range(6):
        j=(i+1)%6
        # Helmet geometry supplies its own silhouette: do not stretch the
        # painted helmet across the face a second time. Mirror the profile on
        # the left cheek; the rear head contains hair rather than another eye.
        fx=64 if guard else 0
        front=[(fx+3,62),(fx+28,62),(fx+28,45 if guard else 35),(fx+3,45 if guard else 35)]
        profile=[(head_x+4,62),(head_x+27,62),(head_x+27,45 if guard else 35),(head_x+4,45 if guard else 35)]
        uv=front if i==0 else profile if i==1 else [profile[j] for j in (1,0,3,2)] if i==5 else hair
        m.face([upper(p) for p in [jaw[i],jaw[j],crown[j],crown[i]]],uv,inside=upper((0,17,0)))
    for i in range(1,5):
        m.face([upper(crown[0]),upper(crown[i]),upper(crown[i+1])],
               [hair[0],hair[1],hair[2]],inside=upper((0,17,0)))
    # Low-ready, raised two-hand aim, and recoil.
    hand=(1,7,-9);support=(-1,7,-8)
    if pose in ('aim','kick','recover') or guard:hand=(1,10,-12);support=(-1,10,-11)
    if pose=='kick':hand=(1,12,-9);support=(-1,11,-8)
    if pose=='recover':hand=(1,10.5,-11);support=(-1,10,-10)
    if run:hand=(6,4,-3-stride*5);support=(-6,4,-3+stride*5)
    if pose=='throw':hand=(5,21,-1);support=(-3,9,-8)
    for side,wrist in [(1,hand),(-1,support)]:
        elbow=(side*7,6,-3+(side*stride*-3 if run else 0))
        if pose=='throw' and side==1:elbow=(8,17,2)
        # Shoulder/elbow fabric and forearm/glove use separate atlas ranges.
        sx=64 if guard else 0
        upper_uv=[(sx+11,81),(sx+21,81),(sx+21,66),(sx+11,66)]
        lower_uv=[(sx+11,93),(sx+20,93),(sx+20,79),(sx+11,79)]
        bone(m,(side*5.5,11,0),elbow,2.2,upper_uv,transform=upper,sides=5,taper=.82)
        bone(m,elbow,wrist,1.8,lower_uv,end=True,transform=upper,sides=5,taper=.72)
    x,y,z=support if pose=='throw' else hand
    # Eight pistol triangles preserve the immutable pose cache capacity.
    box(m,(x-1,y-1,z-7),(x+1,y+1,z+1),tile(2,3),cap=False,transform=upper)
    if guard:
        # Six-sided sloped helmet rim; a compact rear radio pack.
        rim=[(math.cos(i*math.pi/3)*4.3,20,math.sin(i*math.pi/3)*4) for i in range(6)]
        dome=[(x*.72,23,z*.72) for x,y,z in rim]
        for i in range(6):
            j=(i+1)%6
            m.face([upper(p) for p in [rim[i],rim[j],dome[j],dome[i]]],(76,70),inside=upper((0,21,0)))
        m.face([upper(p) for p in dome],(76,70),inside=upper((0,21,0)))
        box(m,(-3,2,3),(3,10,5),tile(3,0),transform=upper)
    triangles=m.write(name);print(f'{name}: {triangles} triangles')


def effects():
    m=Mesh()
    # Two crossed tapered ribbons, with a small white-hot crossed diamond tip.
    # At zoom 0.8, one-unit half-widths collapse to zero pixels after integer
    # projection on vertical shots. Round to three world units on both ribbons.
    for p in [[(-2.5,0,0),(2.5,0,0),(0,0,16)],[(0,-2.5,0),(0,2.5,0),(0,0,16)]]:m.face(p,(36,38))
    m.face([(-3,0,0),(0,0,-2),(3,0,0),(0,0,2)],(71,74))
    m.face([(0,-3,0),(0,0,-2),(0,3,0),(0,0,2)],(71,74))
    m.write('tracer.obj')
    m=Mesh()
    m.face([(-2.5,0,0),(0,0,-5),(2.5,0,0),(0,0,2)],(36,38))
    m.face([(0,-2,0),(0,0,-4),(0,2,0),(0,0,1)],(71,74));m.write('muzzle.obj')


def main():
    variants=[('agent.obj',False,'rest'),('guard.obj',True,'aim'),('gthrow.obj',True,'throw')]
    variants += [(n,g,pose) for n,g,pose in [('aim.obj',False,'aim'),('kick.obj',False,'kick'),('recover.obj',False,'recover'),('gkick.obj',True,'kick'),('grecover.obj',True,'recover')]]
    variants += [(prefix+str(i)+'.obj',guard,'run'+str(i))
                 for prefix,guard in [('prun',False),('grun',True)] for i in (1,2,3)]
    for args in variants:character(*args)
    effects()
    p=ROOT/'examples/shadow-ops.psx';project=json.loads(p.read_text())
    references=[(65,'Pistol aim','aim.obj'),(66,'Pistol recoil','kick.obj'),(67,'Pistol recovery','recover.obj'),(68,'Guard recoil','gkick.obj'),(69,'Guard recovery','grecover.obj')]
    references += [(first+i-1,prefix+' run '+str(i),prefix+str(i)+'.obj')
                   for first,prefix in ((70,'prun'),(74,'grun')) for i in (1,2)]
    references += [(id,label+' passing pose',prefix+'3.obj')
                   for id,prefix,label in ((76,'prun','Pistol'),(78,'grun','Guard'))]
    for scene in project['scenes']:
        if scene['id'] not in (2,3):continue
        objects=scene['data']['objects'];template=next(o for o in objects if o['id']==65)
        for id,name,asset in references+[(80+i,'Muzzle flash '+str(i),'muzzle.obj') for i in range(3)]:
            existing=next((o for o in objects if o['id']==id),None)
            if existing is None:existing=copy.deepcopy(template);objects.append(existing)
            existing.update(id=id,name=name,asset=asset,texture='atlas.png',scale=[1,1,1] if id>=80 else [1.25]*3)
        for o in objects:
            if o['id'] in (2,20,21) or 65<=o['id']<=78:o['texture']='actors.png'
            if 100<=o['id']<112:o.update(kind='mesh',asset='tracer.obj',texture='atlas.png',scale=[1,1,1])
            if o['id']==2:o['name']='Agent - tactical operative'
    project['objects']=copy.deepcopy(next(s for s in project['scenes'] if s['id']==project['active_scene'])['data']['objects'])
    p.write_text(json.dumps(project,indent=2)+'\n')
if __name__=='__main__':main()
