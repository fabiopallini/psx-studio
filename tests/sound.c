#include "object.h"
#include <libspu.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
void psAudioInit(void);
int psAudioSetScene(PsxScene *scene);
static int writes,waits,on_count,off_count,fail_write;
static unsigned long transfer,keys;
static SpuVoiceAttr last;
void SpuInit(void) {}
long SpuSetMute(long mode) {assert(mode==SPU_OFF);return mode;}
long SpuSetReverb(long mode) {assert(mode==SPU_OFF);return 0;}
void SpuSetCommonAttr(SpuCommonAttr *common) {assert(common->mvol.left==16383 && common->mvol.right==16383);}
void SpuSetKey(long on,unsigned long mask)
{
    if(on) {keys|=mask;on_count++;assert(mask==last.voice);}else {keys&=~mask;off_count++;}
}
void SpuSetVoiceAttr(SpuVoiceAttr *voice) {last=*voice;}
long SpuSetTransferMode(long mode) {assert(mode==SPU_TRANSFER_BY_DMA);return mode;}
unsigned long SpuSetTransferStartAddr(unsigned long address) {transfer=address;return address;}
unsigned long SpuWrite(unsigned char *source,unsigned long size)
{assert(source && !(size&63) && transfer>=0x1010);writes++;return fail_write?0:size;}
long SpuIsTransferCompleted(long mode) {assert(mode==SPU_TRANSFER_WAIT);waits++;return 1;}
int main(void)
{
    static const unsigned int samples[32]={0};
    PsxSound bank[2];PsxScene scene;
    int i,loaded_writes,loaded_waits;
    memset(&scene,0,sizeof(scene));memset(bank,0,sizeof(bank));
    bank[0].name="shot.vag";bank[1].name="other.vag";
    for(i=0;i<2;i++) {bank[i].data=(const unsigned char *)samples;bank[i].size=128;bank[i].rate=22050;}
    scene.sounds=bank;scene.sound_count=2;
    psAudioInit();assert(psAudioSetScene(&scene));
    assert(bank[0].address==0x1010 && bank[1].address==0x1090 && writes==2 && waits==2);
    loaded_writes=writes;loaded_waits=waits;
    assert(!psx_sound_play(&scene,"missing",10000,0));assert(!psx_sound_play(&scene,"shot.vag",0,0));assert(!on_count);
    for(i=0;i<10;i++) {
        assert(psx_sound_play(&scene,"shot.vag",10000,0));
        assert(last.voice==(1UL<<(i%8)) && last.addr==0x1010 && last.pitch==2048);
        assert(last.volume.left==10000 && last.volume.right==10000);
        assert(last.adsr1==0x000f && last.adsr2==0x1fc0);
    }
    assert(psx_sound_play(&scene,"other.vag",99999,99999));
    assert(last.volume.left==0 && last.volume.right==16383 && last.addr==0x1090);
    assert(psx_sound_play(&scene,"shot.vag",10000,-99999));
    assert(last.volume.left==10000 && last.volume.right==0);
    assert(writes==loaded_writes && waits==loaded_waits); /* No I/O on firing. */
    assert(psx_music_play(&scene,"other.vag",4200));assert(last.voice==(1UL<<23));
    for(i=0;i<24;i++) {assert(psx_sound_play(&scene,"shot.vag",10000,0));assert(last.voice!=(1UL<<23));}
    assert(keys&(1UL<<23));assert(writes==loaded_writes && waits==loaded_waits);
    assert(psAudioSetScene(&scene) && !keys);assert(off_count>=15);
    bank[1].size=0x80000;loaded_writes=writes;
    assert(!psAudioSetScene(&scene) && !scene.play_sound && !bank[0].address && writes==loaded_writes);
    bank[1].size=128;fail_write=1;
    assert(!psAudioSetScene(&scene) && !scene.play_sound);
    fail_write=0;scene.sound_count=0;assert(psAudioSetScene(&scene));
    assert(!psx_sound_play(&scene,"shot.vag",10000,0));
    puts("C89 sound: DMA alignment/budget, overlap, pan, Scene reset and nonblocking shots passed.");return 0;
}
