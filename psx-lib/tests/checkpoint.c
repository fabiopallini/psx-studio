#include "../runtime/save.h"
#include <assert.h>
int main(void)
{
    unsigned char data[512];
    unsigned int i;
    psx_checkpoint_encode(data,1,"EXAMPLE 1");assert(psx_checkpoint_decode(data)==1);
    psx_checkpoint_encode(data,2,"EXAMPLE 2");assert(psx_checkpoint_decode(data)==2);
    for(i=256;i<270;i++) {
        data[i]^=1;assert(!psx_checkpoint_decode(data));data[i]^=1;
    }
    for(i=1;i<=5;i++) {psx_checkpoint_encode(data,i,"TEST");assert(psx_checkpoint_decode(data)==i);}
    psx_checkpoint_encode(data,0,0);assert(!psx_checkpoint_decode(data));
    psx_checkpoint_encode(data,70000,0);assert(psx_checkpoint_decode(data)==70000);
    psx_checkpoint_encode(data,255,0);assert(psx_checkpoint_decode(data)==255);
    assert(psx_checkpoint_name_valid("EXAMPLE-0001"));
    assert(!psx_checkpoint_name_valid("../OTHER") && !psx_checkpoint_name_valid(0));
    assert(!psx_checkpoint_name_valid("NAME-IS-LONGER-THAN-TWENTY"));
    memset(data,0,sizeof(data));assert(!psx_checkpoint_decode(data));
    return 0;
}
