#include "../runtime/object.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void)
{
    PsxInput input;
    unsigned char packet[8]={0,0x41,255,255,128,128,128,128};
    unsigned long buttons;
    long vector[2];
    int angle,found,i;
    double x,z,expected,error;
    packet[2]=255^32;packet[3]=255^64;
    buttons=psx_input_decode(&input,packet);
    assert(buttons==(8192UL|64UL) && !input.analog && !input.x && !input.z);
    packet[1]=0x73;packet[6]=255;packet[7]=80;
    assert(psx_input_decode(&input,packet)==buttons && input.analog && input.x==127 && input.z==-48);
    packet[6]=140;packet[7]=140;psx_input_decode(&input,packet);assert(!input.x && !input.z);
    packet[6]=0;packet[7]=255;packet[1]=0x53;psx_input_decode(&input,packet);
    assert(input.x==-128 && input.z==127 && input.analog);
    packet[0]=255;assert(!psx_input_decode(&input,packet) && !input.x && !input.z && !input.analog);
    packet[0]=0;packet[1]=0x23;assert(!psx_input_decode(&input,packet)); /* Mouse is not a stick. */
    for(angle=0;angle<4096;angle+=3) {
        psx_direction_vector(angle,vector);
        x=-sin(angle*6.283185307179586/4096);z=-cos(angle*6.283185307179586/4096);
        assert(fabs(vector[0]-x*4096)<=2 && fabs(vector[1]-z*4096)<=2);
        found=psx_direction_angle(vector[0],vector[1]);
        error=fabs((double)(found-angle));if(error>2048) error=4096-error;
        assert(error<=2);
    }
    for(i=-128;i<=127;i+=7) {
        if(!i) continue;
        psx_direction_normalize(i,127,vector);
        expected=sqrt((double)i*i+127.0*127.0);
        assert(fabs(vector[0]-i*4096/expected)<=2);
        assert(fabs(vector[1]-127*4096/expected)<=2);
    }
    assert(!psx_direction_normalize(0,0,vector) && !vector[0] && !vector[1]);
    psx_direction_normalize(65534,-65534,vector);assert(vector[0]>=2895 && vector[0]<=2897 && vector[1]==-vector[0]);
    puts("C89 input: packet layout, analog dead zone and Q12 direction reference checks passed.");return 0;
}
