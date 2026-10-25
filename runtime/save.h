#ifndef PSX_CHECKPOINT_H
#define PSX_CHECKPOINT_H
/* Single-block PS1 card file: 128-byte header, 128-byte 4-bit icon, payload.
 * The payload is byte-encoded, independent of host long width/endianness. */
#include <string.h>
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
static void psx_checkpoint_encode(unsigned char *data,unsigned int scene)
{
    unsigned int i,crc;
    const char *title="SHADOW OPS";
    memset(data,0,512);data[0]='S';data[1]='C';data[2]=17;data[3]=1;
    for(i=0;title[i];i++) {
        data[4+i*2]=title[i]==' '?129:130;
        data[5+i*2]=title[i]==' '?64:(unsigned char)(96+title[i]-'A');
    }
    data[98]=159;data[99]=126; /* CLUT entry 1, opaque turquoise. */
    for(i=128;i<256;i++) data[i]=17;
    memcpy(data+256,"SHDWCP01",8);data[264]=(unsigned char)scene;
    crc=psx_checkpoint_crc(data+256,9);data[265]=(unsigned char)crc;data[266]=(unsigned char)(crc>>8);
}
static unsigned int psx_checkpoint_decode(const unsigned char *data)
{
    unsigned int crc;
    if(data[0]!='S' || data[1]!='C' || memcmp(data+256,"SHDWCP01",8)) return 0;
    crc=psx_checkpoint_crc(data+256,9);
    if(data[265]!=(unsigned char)crc || data[266]!=(unsigned char)(crc>>8)) return 0;
    return data[264]==1 || data[264]==2?data[264]:0;
}
#endif
