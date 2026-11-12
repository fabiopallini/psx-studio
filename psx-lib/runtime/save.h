#ifndef PSX_CHECKPOINT_H
#define PSX_CHECKPOINT_H
/* Single-block PS1 card file: 128-byte header, 128-byte 4-bit icon, payload.
 * The payload is byte-encoded, independent of host long width/endianness. */
#include <string.h>
/* Applications supply an isolated card filename and decide checkpoint meaning. */
static int psx_checkpoint_name_valid(const char *name)
{
    unsigned int i;
    if(!name || !*name) return 0;
    for(i=0;name[i];i++) {
        if(i>=20 || !((name[i]>='A' && name[i]<='Z') || (name[i]>='a' && name[i]<='z') ||
           (name[i]>='0' && name[i]<='9') || name[i]=='-' || name[i]=='_')) return 0;
    }
    return 1;
}
static unsigned int psx_checkpoint_crc(const unsigned char *p,unsigned int count)
{
    unsigned int crc,i;
    crc=65535U;
    while(count--) {
        crc^=(unsigned int)*p++<<8;
        for(i=0;i<8;i++) crc=(crc&32768U)?((crc<<1)^4129U)&65535U:(crc<<1)&65535U;
    }
    return crc;
}
static void psx_checkpoint_encode(unsigned char *data,unsigned int scene,const char *title)
{
    unsigned int i,crc;
    if(!title) title="PSX GAME";
    memset(data,0,512);data[0]='S';data[1]='C';data[2]=17;data[3]=1;
    for(i=0;title[i] && i<31;i++) {
        unsigned char c;
        c=(unsigned char)title[i];if(c>='a' && c<='z') c=(unsigned char)(c-'a'+'A');
        if(c>='A' && c<='Z') {data[4+i*2]=130;data[5+i*2]=(unsigned char)(96+c-'A');}
        else if(c>='0' && c<='9') {data[4+i*2]=130;data[5+i*2]=(unsigned char)(79+c-'0');}
        else {data[4+i*2]=129;data[5+i*2]=64;}
    }
    data[98]=159;data[99]=126; /* CLUT entry 1, opaque turquoise. */
    for(i=128;i<256;i++) data[i]=17;
    memcpy(data+256,"PSXCP002",8);
    for(i=0;i<4;i++) data[264+i]=(unsigned char)(((unsigned long)scene)>>(8*i));
    crc=psx_checkpoint_crc(data+256,12);data[268]=(unsigned char)crc;data[269]=(unsigned char)(crc>>8);
}
static unsigned int psx_checkpoint_decode(const unsigned char *data)
{
    unsigned int crc,i,scene;
    if(data[0]!='S' || data[1]!='C' || memcmp(data+256,"PSXCP002",8)) return 0;
    crc=psx_checkpoint_crc(data+256,12);
    if(data[268]!=(unsigned char)crc || data[269]!=(unsigned char)(crc>>8)) return 0;
    scene=0;for(i=0;i<4;i++) scene|=(unsigned int)((unsigned long)data[264+i]<<(8*i));
    return scene;
}
#define PSX_SAVE_PAYLOAD_MAX 7916U
static int psx_checkpoint_payload_encode(unsigned char *data,unsigned int scene,const char *title,
    const unsigned char *payload,unsigned int size)
{
    unsigned int crc;
    if(!payload || !size || size>PSX_SAVE_PAYLOAD_MAX || !scene) return 0;
    memset(data,0,8192);psx_checkpoint_encode(data,scene,title);
    data[270]='P';data[271]='1';data[272]=(unsigned char)size;data[273]=(unsigned char)(size>>8);
    memcpy(data+276,payload,size);crc=psx_checkpoint_crc(data+272,size+4);
    data[274]=(unsigned char)crc;data[275]=(unsigned char)(crc>>8);return 1;
}
static int psx_checkpoint_payload_decode(const unsigned char *data,unsigned char *payload,
    unsigned int capacity,unsigned int *size)
{
    unsigned int count,crc;
    /* CRC covers the length, zeroed CRC field and payload. */
    if(!psx_checkpoint_decode(data) || data[270]!='P' || data[271]!='1' || !payload) return 0;
    count=(unsigned int)data[272]|((unsigned int)data[273]<<8);
    if(!count || count>capacity || count>PSX_SAVE_PAYLOAD_MAX) return 0;
    /* Compute without mutating the source or allocating another card block. */
    crc=65535U;
    {
        unsigned int i,j,value;
        for(i=0;i<count+4;i++) {
            value=(i==2 || i==3)?0:data[272+i];crc^=value<<8;
            for(j=0;j<8;j++) crc=(crc&32768U)?((crc<<1)^4129U)&65535U:(crc<<1)&65535U;
        }
    }
    if(data[274]!=(unsigned char)crc || data[275]!=(unsigned char)(crc>>8)) return 0;
    memcpy(payload,data+276,count);*size=count;return 1;
}
#endif
