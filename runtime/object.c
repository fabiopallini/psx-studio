#include "object.h"

int psx_objects_overlap(const PsxObject *a, const PsxObject *b)
{
    int axis;
    long delta;
    if (a == b || !a->alive || !b->alive || a->kind==PSX_OBJECT_CAMERA || b->kind==PSX_OBJECT_CAMERA) return 0;
    for (axis = 0; axis < 3; axis++) {
        delta = (a->position[axis]+a->collider_offset[axis]) - (b->position[axis]+b->collider_offset[axis]);
        if (delta < 0) delta = -delta;
        if (delta >= a->half_size[axis] + b->half_size[axis]) return 0;
    }
    return 1;
}

static void collision_event(PsxObject *a, PsxObject *b, PsxScene *scene)
{
    int action_a;
    int action_b;
    action_a = a->collision_action;
    action_b = b->collision_action;
    if (a->logic && a->logic->collision) a->logic->collision(a, b, scene);
    if (scene->pending_scene) return;
    if (a->alive && b->alive && b->logic && b->logic->collision) b->logic->collision(b, a, scene);
    if(scene->pending_scene) return;
    if(action_a==3) psx_scene_request_change(scene,a->target_scene);
    else if(action_b==3) psx_scene_request_change(scene,b->target_scene);
    if (action_a == 1 || action_b == 2) a->alive = 0;
    if (action_b == 1 || action_a == 2) b->alive = 0;
}

int psx_object_move(PsxObject *object, PsxScene *scene, long dx, long dy, long dz)
{
    long origin[3];
    long previous[3];
    long offset[3];
    long steps;
    long magnitude;
    long step;
    long destination;
    unsigned int i;
    int axis;
    if (!object->alive || scene->pending_scene) return 0;
    offset[0] = dx; offset[1] = dy; offset[2] = dz;
    steps = 0;
    for (axis = 0; axis < 3; axis++) {
        if (offset[axis] < -32767L || offset[axis] > 32767L) return 0;
        if (object->position[axis] < -32767L || object->position[axis] > 32767L) return 0;
        origin[axis] = object->position[axis];
        destination = origin[axis] + offset[axis];
        if (destination < -32767L || destination > 32767L) return 0;
        magnitude = offset[axis] < 0 ? -offset[axis] : offset[axis];
        if (magnitude > steps) steps = magnitude;
    }
    for (step = 1; step <= steps; step++) {
        for (axis = 0; axis < 3; axis++) {
            previous[axis] = object->position[axis];
            object->position[axis] = origin[axis] + offset[axis] * step / steps;
        }
        for (i = 0; i < scene->count; i++) {
            PsxObject *other;
            other = &scene->objects[i];
            if (!psx_objects_overlap(object, other)) continue;
            if (object->solid && other->solid) {
                for (axis = 0; axis < 3; axis++) object->position[axis] = previous[axis];
                collision_event(object, other, scene);
                return 0;
            }
            collision_event(object, other, scene);
            if (!object->alive || scene->pending_scene) return 0;
        }
    }
    return 1;
}

void psx_object_kill(PsxObject *object)
{
    object->alive = 0;
}

PsxObject *psx_scene_find(PsxScene *scene,unsigned int id)
{
    unsigned int i;
    for(i=0;i<scene->count;i++) if(scene->objects[i].id==id && scene->objects[i].alive) return &scene->objects[i];
    return 0;
}
int psx_scene_request_change(PsxScene *scene,unsigned int id)
{
    if(!id || scene->pending_scene || !scene->scene_exists || !scene->scene_exists(id)) return 0;
    scene->pending_scene=id;return 1;
}
void psx_scene_clear(PsxScene *scene)
{
    unsigned int i;
    for(i=0;i<scene->count;i++) {
        PsxObject *o;
        o=&scene->objects[i];
        if(o->cleanup) o->cleanup(o,scene);
        o->cleanup=0;o->user_data=0;o->alive=0;
    }
    scene->count=0;scene->pending_scene=0;scene->play_sound=0;scene->play_music=0;
}

static void sync_camera(PsxScene *scene)
{
    unsigned int i;
    int k;
    for(i=0;i<scene->count;i++) {
        PsxObject *o;
        o=&scene->objects[i];
        if(o->id!=scene->active_camera || o->kind!=PSX_OBJECT_CAMERA || !o->alive) continue;
        if(o->follow_target) {
            PsxObject *target;
            target=psx_scene_find(scene,o->follow_target);
            if(target) for(k=0;k<3;k++) {
                long position;position=target->position[k]+o->follow_offset[k];
                o->position[k]=position < -32767L ? -32767L : position > 32767L ? 32767L : position;
            }
        }
        for(k=0;k<3;k++) scene->camera[k]=o->position[k];
        scene->yaw=(int)(o->rotation[1]%4096);
        scene->pitch=(int)(o->rotation[0]%4096);
        scene->zoom=o->camera_zoom;scene->projection=o->camera_projection;
        scene->camera_eye=1;
        return;
    }
}

void psx_scene_init(PsxScene *scene)
{
    unsigned int i;
    for (i = 0; i < scene->count; i++) {
        PsxObject *object;
        object = &scene->objects[i];
        if (object->alive && object->logic && object->logic->init) object->logic->init(object, scene);
    }
    sync_camera(scene);
}

void psx_scene_update(PsxScene *scene, unsigned long input)
{
    unsigned int i;
    unsigned int j;
    int needs_collisions;
    PsxObject *a;
    PsxObject *b;
    long dx;
    long dy;
    long dz;
    if(scene->pending_scene) return;
    for (i = 0; i < scene->count; i++) {
        a = &scene->objects[i];
        if (a->alive && a->input_mode) {
            dx = 0; dy = 0; dz = 0;
            if (input & 32768UL) dx -= a->speed;
            if (input & 8192UL) dx += a->speed;
            if (a->input_mode == 1) {
                if (input & 4096UL) dz -= a->speed;
                if (input & 16384UL) dz += a->speed;
            } else {
                if (input & 4096UL) dy += a->speed;
                if (input & 16384UL) dy -= a->speed;
            }
            psx_object_move(a, scene, dx, dy, dz);
        }
        if(scene->pending_scene) return;
        if (a->alive && a->logic && a->logic->update) a->logic->update(a, scene, input);
        if(scene->pending_scene) return;
    }
    if(scene->pending_scene) return;
    needs_collisions=0;
    for(i=0;i<scene->count;i++) {
        a=&scene->objects[i];
        if(a->alive && a->kind!=PSX_OBJECT_CAMERA && (a->collision_action || (a->logic && a->logic->collision))) {needs_collisions=1;break;}
    }
    if(needs_collisions) for (i = 0; i < scene->count; i++) {
        a = &scene->objects[i];
        for (j = i + 1; j < scene->count; j++) {
            b = &scene->objects[j];
            if(!a->collision_action && !b->collision_action &&
               (!a->logic || !a->logic->collision) && (!b->logic || !b->logic->collision)) continue;
            if (psx_objects_overlap(a, b)) {
                collision_event(a, b, scene);
                if(scene->pending_scene) return;
            }
        }
    }
    sync_camera(scene);
}

int psx_sound_play(PsxScene *scene,const char *name,int volume,int pan)
{
    if(!scene || !scene->play_sound || !name || !*name || volume<=0) return 0;
    if(volume>16383) volume=16383;
    if(pan<-4096) pan=-4096;
    if(pan>4096) pan=4096;
    return scene->play_sound(scene,name,volume,pan);
}

/* Quarter-wave Q12 sine, one sample per eight angle units; integer linear
 * interpolation keeps console math cheap and within two Q12 units of sine. */
static const unsigned short direction_sine[129]={
    0,50,101,151,201,251,301,351,401,451,501,551,
    601,651,700,750,799,848,897,946,995,1044,1092,1141,
    1189,1237,1285,1332,1380,1427,1474,1521,1567,1614,1660,1706,
    1751,1797,1842,1886,1931,1975,2019,2062,2106,2149,2191,2234,
    2276,2317,2359,2399,2440,2480,2520,2559,2598,2637,2675,2713,
    2751,2788,2824,2861,2896,2932,2967,3001,3035,3068,3102,3134,
    3166,3198,3229,3260,3290,3320,3349,3378,3406,3433,3461,3487,
    3513,3539,3564,3588,3612,3636,3659,3681,3703,3724,3745,3765,
    3784,3803,3822,3839,3857,3873,3889,3905,3920,3934,3948,3961,
    3973,3985,3996,4007,4017,4027,4036,4044,4052,4059,4065,4071,
    4076,4081,4085,4088,4091,4093,4095,4096,4096,
};
static long direction_sin(int angle)
{
    int quadrant,offset,index,fraction;
    long value;
    angle%=4096;if(angle<0) angle+=4096;
    quadrant=angle/1024;offset=angle%1024;
    if(quadrant&1) offset=1024-offset;
    index=offset/8;fraction=offset%8;value=direction_sine[index];
    if(fraction) value+=(direction_sine[index+1]-value)*fraction/8;
    return quadrant>=2?-value:value;
}
void psx_direction_vector(int angle,long vector[2])
{
    vector[0]=-direction_sin(angle);vector[1]=-direction_sin(angle+1024);
}
int psx_direction_angle(long x,long z)
{
    long a,b;
    int low,high,middle,angle;
    if(!x && !z) return 0;
    /* Inputs bounded to signed world coordinates; products <= 134213632. */
    a=x<0?-x:x;b=z<0?-z:z;
    low=0;high=1024;
    while(low<high) {
        middle=(low+high)/2;
        if(direction_sin(middle)*b<direction_sin(middle+1024)*a) low=middle+1;
        else high=middle;
    }
    angle=low;
    if(z>0) angle=2048-angle;
    if(x>0) angle=(4096-angle)%4096;
    return angle;
}
int psx_direction_normalize(long x,long z,long vector[2])
{
    unsigned long value,result,bit;
    if(!x && !z) {vector[0]=vector[1]=0;return 0;}
    /* Upscale short input vectors to keep the integer square root precise. */
    while(x<4096 && x>-4096 && z<4096 && z>-4096) {x*=2;z*=2;}
    /* Rescale long world deltas before squaring on a 32-bit PS1 long. */
    while(x>16383 || x<-16383 || z>16383 || z<-16383) {x/=2;z/=2;}
    value=(unsigned long)(x*x+z*z);result=0;bit=1UL<<28;
    while(bit>value) bit>>=2;
    while(bit) {
        if(value>=result+bit) {value-=result+bit;result=(result>>1)+bit;}
        else result>>=1;
        bit>>=2;
    }
    if(!result) {vector[0]=vector[1]=0;return 0;}
    vector[0]=x*4096/(long)result;vector[1]=z*4096/(long)result;return 1;
}
unsigned long psx_input_decode(PsxInput *input,const unsigned char packet[8])
{
    unsigned int type;
    input->x=input->z=input->analog=0;
    if(packet[0]) return 0;
    type=packet[1]>>4;
    if(type!=4 && type!=5 && type!=7) return 0;
    if((packet[1]&15)<1) return 0;
    if((type==5 || type==7) && (packet[1]&15)>=3) {
        input->analog=1;input->x=(int)packet[6]-128;input->z=(int)packet[7]-128;
        if((long)input->x*input->x+(long)input->z*input->z<=24L*24)
            input->x=input->z=0;
    }
    return ((unsigned long)(packet[2]^255)<<8)|(unsigned long)(packet[3]^255);
}

int psx_music_play(PsxScene *scene,const char *name,int volume)
{
    if(!scene || !scene->play_music || !name || !*name || volume<=0) return 0;
    if(volume>16383) volume=16383;
    return scene->play_music(scene,name,volume);
}
