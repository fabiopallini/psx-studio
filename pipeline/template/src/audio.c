#include "object.h"
#include <libspu.h>
#include <string.h>

/* SPU sample arena: bytes 0x1010..0x7ffff. Capture area/SDK silence stay
 * reserved; reverb is off. Eight round-robin hardware voices allow overlap. */
#define SOUND_START 0x1010UL
#define SOUND_END 0x80000UL
#define SOUND_VOICES 8
static unsigned int next_voice;

static void stop_voices(void)
{
    SpuVoiceAttr voice;
    SpuSetKey(SPU_OFF,0xffffffUL);
    memset(&voice,0,sizeof(voice));voice.voice=0xffffffUL;
    voice.mask=SPU_VOICE_VOLL|SPU_VOICE_VOLR;
    SpuSetVoiceAttr(&voice); /* Key off alone may retain a long release tail. */
}
void psAudioInit(void)
{
    SpuCommonAttr common;
    SpuInit();SpuSetReverb(SPU_OFF);
    memset(&common,0,sizeof(common));
    common.mask=SPU_COMMON_MVOLL|SPU_COMMON_MVOLR|SPU_COMMON_MVOLMODEL|SPU_COMMON_MVOLMODER|SPU_COMMON_CDMIX|SPU_COMMON_EXTMIX;
    common.mvol.left=common.mvol.right=0x3fff;
    SpuSetCommonAttr(&common);SpuSetMute(SPU_OFF);
    stop_voices();next_voice=0;
}
static int start_sample(PsxScene *scene,const char *name,int volume,int pan,int music)
{
    unsigned int i;
    unsigned long mask;
    PsxSound *sample;
    SpuVoiceAttr voice;
    sample=0;
    for(i=0;i<scene->sound_count;i++) if(!strcmp(scene->sounds[i].name,name)) {sample=&scene->sounds[i];break;}
    if(!sample || !sample->address) return 0;
    mask=music?(1UL<<23):(1UL<<next_voice);
    if(!music) next_voice=(next_voice+1)%SOUND_VOICES;
    SpuSetKey(SPU_OFF,mask);memset(&voice,0,sizeof(voice));
    voice.voice=mask;
    voice.mask=SPU_VOICE_VOLL|SPU_VOICE_VOLR|SPU_VOICE_VOLMODEL|SPU_VOICE_VOLMODER|
        SPU_VOICE_PITCH|SPU_VOICE_WDSA|SPU_VOICE_LSAX|SPU_VOICE_ADSR_ADSR1|SPU_VOICE_ADSR_ADSR2;
    /* Pan Q12: -4096 left, 0 centre, +4096 right; 0x3fff full volume.
     * Products fit signed 32-bit even at the extreme accepted values. */
    voice.volume.left=(short)(pan>0?(long)volume*(4096-pan)/4096:volume);
    voice.volume.right=(short)(pan<0?(long)volume*(4096+pan)/4096:volume);
    voice.pitch=(unsigned short)(sample->rate*4096UL/44100UL);
    voice.addr=sample->address;voice.loop_addr=sample->address;
    voice.adsr1=0x000f;voice.adsr2=0x1fc0; /* Fast attack, full sustain; VAG end stops it. */
    SpuSetVoiceAttr(&voice);SpuSetKey(SPU_ON,mask);return 1;
}
static int play_sound(PsxScene *scene,const char *name,int volume,int pan)
{return start_sample(scene,name,volume,pan,0);}
static int play_music(PsxScene *scene,const char *name,int volume)
{return start_sample(scene,name,volume,0,1);}
int psAudioSetScene(PsxScene *scene)
{
    unsigned int i;
    unsigned long cursor;
    PsxSound *sample;
    stop_voices();next_voice=0;scene->play_sound=0;scene->play_music=0;
    for(i=0;i<scene->sound_count;i++) scene->sounds[i].address=0;
    cursor=SOUND_START;
    for(i=0;i<scene->sound_count;i++) {
        sample=&scene->sounds[i];
        if(!sample->data || !sample->size || (sample->size&63) || sample->size>SOUND_END-cursor ||
           sample->rate<1000 || sample->rate>44100) return 0;
        cursor+=sample->size;
    }
    cursor=SOUND_START;
    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    for(i=0;i<scene->sound_count;i++) {
        sample=&scene->sounds[i];
        if(SpuSetTransferStartAddr(cursor)!=cursor || SpuWrite((unsigned char *)sample->data,sample->size)!=sample->size ||
           !SpuIsTransferCompleted(SPU_TRANSFER_WAIT)) return 0;
        sample->address=cursor;cursor+=sample->size;
    }
    SpuSetMute(SPU_OFF);scene->play_music=play_music;scene->play_sound=play_sound;return 1;
}
