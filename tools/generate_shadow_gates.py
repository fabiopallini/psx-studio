#!/usr/bin/env python3
"""Shadow Ops low-poly passage architecture; desktop-only geometry generation."""
import copy
import json
from generate_shadow_models import Mesh, tile, ROOT, DATA


def wall(name,width,height,depth):
    m=Mesh()
    # Preserve 96-world-unit wall texel density on faces and lintels.
    for side in (-1,1):
        left=-width/2
        while left<width/2:
            right=min(left+96,width/2)
            u0=66;u1=66+(right-left)*27/96;v0=6;v1=6+height*27/96
            m.face([(left,-height/2,side*depth/2),(right,-height/2,side*depth/2),
                    (right,height/2,side*depth/2),(left,height/2,side*depth/2)],
                   [(u0,v1),(u1,v1),(u1,v0),(u0,v0)],inside=(0,0,0))
            if side==1:
                m.face([(left,height/2,-depth/2),(right,height/2,-depth/2),
                        (right,height/2,depth/2),(left,height/2,depth/2)],tile(2,0),inside=(0,0,0))
            left=right
    for x in (-width/2,width/2):
        m.face([(x,-height/2,-depth/2),(x,-height/2,depth/2),
                (x,height/2,depth/2),(x,height/2,-depth/2)],tile(2,0),inside=(0,0,0))
    m.write(name)


def main():
    wall('passage-wall-long.obj',480,80,20)
    wall('passage-wall-short.obj',40,80,20)
    wall('passage-lintel.obj',80,16,20)
    gate=Mesh();metal=[(67,112),(94,112),(94,100),(67,100)]
    def rail(left,bottom,right,top):
        # Two-sided front plus top: readable bars without unseen box faces.
        gate.face([(left,bottom,0),(right,bottom,0),(right,top,0),(left,top,0)],metal)
        gate.face([(left,top,-4),(right,top,-4),(right,top,4),(left,top,4)],metal)
    for x in (-30,0,30):rail(x-3,-32,x+3,32)
    for y in (-25,25):rail(-40,y-3,40,y+3)
    gate.write('security-gate.obj')
    floor=Mesh();floor.face([(-40,0,-32),(40,0,-32),(40,0,32),(-40,0,32)],tile(2,0));floor.write('passage-floor.obj')
    path=ROOT/'examples/shadow-ops.psx';project=json.loads(path.read_text())
    for scene in project['scenes']:
        if scene['id'] not in (1,2):continue
        objects=scene['data']['objects'];by_id={o['id']:o for o in objects}
        def item(id,name,asset,position,bounds,solid=False,texture='atlas.png'):
            o=by_id.get(id)
            if o is None:
                o=copy.deepcopy(by_id[12]);o['module']='';objects.append(o);by_id[id]=o
            o.update(id=id,name=name,kind='mesh',asset=asset,texture=texture,position=position,
                     rotation=[0,0,0],scale=[1,1,1],bounds=bounds,collider_offset=[0,0,0],
                     solid=solid,input_mode=0,collision_action=0,speed=2)
            return o
        def opening(north,wall_id,side_id,door_id,header_id,floor_id,trigger_id):
            sign=-1 if north else 1;x=220 if north else -220;z=250*sign
            item(wall_id,('North' if north else 'South')+' wall beside passage','passage-wall-long.obj',[-60 if north else 60,40,z],[240,40,10],True)
            item(side_id,'Passage side pillar','passage-wall-short.obj',[280 if north else -280,40,z],[20,40,10],True)
            item(header_id,'Passage lintel','passage-lintel.obj',[x,72,z],[40,8,10])
            item(door_id,'Security gate - clear guards to open','security-gate.obj',[x,32,z],[40,32,4],True,'actors.png')
            item(floor_id,'Passage threshold','passage-floor.obj',[x,0,272*sign],[40,1,32])
            item(trigger_id,'Passage crossing trigger','',[x,24,282*sign],[30,24,12],texture='')
        if scene['id']==1:opening(True,12,18,42,43,46,40)
        else:
            opening(False,13,18,42,43,46,40)
            opening(True,12,19,44,45,47,41)
        # Keep Scene targets and all user-edited spawns, cameras and other objects.
    project['objects']=copy.deepcopy(next(s['data']['objects'] for s in project['scenes'] if s['id']==project['active_scene']))
    path.write_text(json.dumps(project,indent=2)+'\n')


if __name__=='__main__':main()
