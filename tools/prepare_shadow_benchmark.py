#!/usr/bin/env python3
"""Prepare an isolated, deterministic Shadow Ops renderer benchmark.

Optional developer utility, not part of game builds. Copies current assets and
runtime resources; does not edit the example. Run the printed native build
command with an external SDK, then run its CD in an isolated emulator with
PSX console output enabled. Enter New Game from the title menu. BENCH lines
contain tenths of presented FPS when the emulator forwards game console output;
otherwise inspect the existing PsxFrameRate counter with its debugger. Discard
the initial menu/previous-scene sample and the final stationary period.
"""
import argparse
import copy
import json
from pathlib import Path
import shutil
import shlex

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination',type=Path)
    args=parser.parse_args()
    destination=args.destination.resolve()
    if destination.exists():raise SystemExit('Choose a new benchmark directory to preserve prior results.')
    destination.mkdir(parents=True)
    for folder in ('pipeline','runtime'):
        shutil.copytree(ROOT/folder,destination/'root'/folder)
    project=json.loads((ROOT/'examples/shadow-ops.psx').read_text())
    project['startup_scene']=project['active_scene']=1
    project['output_path']=''
    active=next(s['data'] for s in project['scenes'] if s['id']==1)
    project.update(copy.deepcopy(active))
    target=destination/'bench.psx'
    target.write_text(json.dumps(project,indent=2)+'\n')
    data=destination/'bench_psx_data'
    shutil.copytree(ROOT/'examples/shadow-ops_psx_data',data)
    path=data/'mission.c';code=path.read_text()
    code=code.replace('#include <string.h>', '#include <string.h>\n#include <stdio.h>\nextern int psFrameRate(void);\nstatic unsigned int benchmark_step;')
    code=code.replace('    int i;\n    player->render_flags=', '    int i;\n    benchmark_step=0;\n    player->render_flags=')
    marker='    if(!music_started) music_started='
    injection='''    /* Diagnostic-only input/health overrides. Geometry, collision sizes,
     * patrol spawns, AI, render flags and camera match the source project. */
    benchmark_step++;
    if(!(benchmark_step%50U)) {
        printf("BENCH %u %u %d %ld %ld %d %d\\n",scene->scene_id,
            benchmark_step,psFrameRate(),player->position[0],player->position[2],
            scene->hud.health,scene->hud.ammo);
    }
    if(benchmark_step>=400U) {
        if(scene->scene_id<5) psx_scene_request_change(scene,scene->scene_id+1);
        else if(benchmark_step==400U) printf("BENCH DONE\\n");
        return;
    }
    scene->hud.health=100;scene->hud.ammo=36;
    item=find_any(scene,50);if(item) item->alive=0;
    input=benchmark_step<80U?8192UL:benchmark_step<160U?4096UL:
        benchmark_step<240U?32768UL:benchmark_step<320U?16384UL:
        (benchmark_step%8U<4U?128UL:0UL);
'''
    assert marker in code
    code=code.replace(marker,injection+marker)
    # Retain the actual compact combat HUD; logging has the same cost in both builds.
    path.write_text(code)
    print('Prepared benchmark:',target)
    print(shlex.join([str(ROOT/'build/psx-studio'),'--build-ps1','--project',str(target),'--root',str(destination/'root'),'--psyq','/path/to/SDK']))

if __name__=='__main__':main()
