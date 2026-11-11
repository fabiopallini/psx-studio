#ifndef TEST_LIBSPU_H
#define TEST_LIBSPU_H
#define SPU_OFF 0
#define SPU_ON 1
#define SPU_TRANSFER_BY_DMA 0
#define SPU_TRANSFER_WAIT 1
#define SPU_COMMON_MVOLL 1
#define SPU_COMMON_MVOLR 2
#define SPU_COMMON_MVOLMODEL 16
#define SPU_COMMON_MVOLMODER 32
#define SPU_COMMON_CDMIX 4
#define SPU_COMMON_EXTMIX 8
#define SPU_VOICE_VOLL 1
#define SPU_VOICE_VOLR 2
#define SPU_VOICE_VOLMODEL 4
#define SPU_VOICE_VOLMODER 8
#define SPU_VOICE_PITCH 16
#define SPU_VOICE_WDSA 32
#define SPU_VOICE_LSAX 64
#define SPU_VOICE_ADSR_ADSR1 128
#define SPU_VOICE_ADSR_ADSR2 256
typedef struct {short left,right;} SpuVolume;
typedef struct {unsigned long mask;SpuVolume mvol;} SpuCommonAttr;
typedef struct {
    unsigned long voice,mask;SpuVolume volume;
    unsigned short pitch;unsigned long addr,loop_addr;
    unsigned short adsr1,adsr2;
} SpuVoiceAttr;
void SpuInit(void);
long SpuSetReverb(long mode);
long SpuSetMute(long mode);
void SpuSetCommonAttr(SpuCommonAttr *common);
void SpuSetKey(long on,unsigned long mask);
void SpuSetVoiceAttr(SpuVoiceAttr *voice);
long SpuSetTransferMode(long mode);
unsigned long SpuSetTransferStartAddr(unsigned long address);
unsigned long SpuWrite(unsigned char *source,unsigned long size);
long SpuIsTransferCompleted(long mode);
#endif
