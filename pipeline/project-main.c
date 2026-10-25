#include "psx.h"
#include "scene.h"
#ifdef STUDIO_DIAGNOSTICS
#include <stdio.h>
#endif

static void draw_status(const PsxScene *view,int loaded)
{
    unsigned int i;
    int fps,controlled,alive;
    if (psFontEnabled()) {
        if(view->hud.enabled && view->hud.style==PSX_HUD_COMBAT) return;
        if(view->hud.enabled && view->hud.style==PSX_HUD_HEALTH_ONLY) return;
        if(view->hud.enabled && view->hud.style==PSX_HUD_MENU) {
            if(view->hud.message) FntPrint("%s",view->hud.message);
        } else if(view->hud.enabled) {
            fps=psFrameRate();
            FntPrint("HP              AMMO %d ",view->hud.ammo);
            if(fps<0) FntPrint("FPS --\n");else FntPrint("FPS %d\n",fps/10);
            if(view->hud.message) FntPrint("%s\n",view->hud.message);
        } else {
            FntPrint("%s\n", studio_scene_name);
            if (!loaded) FntPrint("TEXTURE CD LOAD FAILED\n");
            else FntPrint("OBJECTS: %d\n", (int)view->count);
            fps=psFrameRate();
            if(fps<0) FntPrint("FPS: --\n");
            else FntPrint("FPS: %d.%d\n",fps/10,fps%10);
#ifdef STUDIO_DIAGNOSTICS
            FntPrint("PAD %d\n",(int)studio_pad);
            for(i=0;i<view->count;i++) {
                if(view->objects[i].input_mode) FntPrint("PLAYER %d %d ALIVE %d\n",(int)view->objects[i].position[0],(int)view->objects[i].position[2],view->objects[i].alive);
                if(view->objects[i].logic) FntPrint("MODULE %d\n",(int)view->objects[i].rotation[1]);
            }
#endif
            controlled=0;alive=0;
            for(i=0;i<view->count;i++) if(view->objects[i].input_mode) {
                controlled++;
                if(view->objects[i].alive) alive++;
            }
            if(controlled && !alive) FntPrint("GAME OVER\n");
        }
    }
}

static int load_scene(PsxScene *scene)
{
    int loaded;
    loaded=psSetScene(scene,studio_textures,studio_texture_count);
#ifdef STUDIO_DIAGNOSTICS
    printf("STUDIO SCENE %u OBJECTS %u\n",scene->scene_id,scene->count);
#endif
    return loaded;
}

int main(void)
{
    PsxScene *scene;
    const PsxScene *view;
    int loaded;
    psInit();psShowLoading("LOADING...");scene=studio_scene_load();loaded=load_scene(scene);
    while(1) {
        psUpdate();
        if(scene->pending_scene) {
            if(psGpuIdle()) {
                psShowLoading(scene->hud.loading?scene->hud.loading:"LOADING...");
                if(studio_scene_apply_change()) loaded=load_scene(scene);
            }
            continue;
        }
        if(!psBeginFrame()) continue;
        view=loaded?psDrawScene():scene;
        if(scene->pending_scene) continue;
        psDrawHud();draw_status(view,loaded);psDisplay();
    }
    return 0;
}
