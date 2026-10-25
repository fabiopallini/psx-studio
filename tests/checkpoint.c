#include "../runtime/save.h"
#include <assert.h>
int main(void)
{
    unsigned char data[512];
    unsigned int i;
    psx_checkpoint_encode(data,1);assert(psx_checkpoint_decode(data)==1);
    psx_checkpoint_encode(data,2);assert(psx_checkpoint_decode(data)==2);
    for(i=256;i<267;i++) {
        data[i]^=1;assert(!psx_checkpoint_decode(data));data[i]^=1;
    }
    psx_checkpoint_encode(data,3);assert(!psx_checkpoint_decode(data));
    memset(data,0,sizeof(data));assert(!psx_checkpoint_decode(data));
    return 0;
}
