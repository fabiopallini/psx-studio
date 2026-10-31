#include <sys/types.h>
#include <libapi.h>
#include <sys/file.h>
#include "object.h"
#include "save.h"

/* Explicit checkpoint requests only; never format a card or touch other files.
 * BIOS I/O may block. The caller pauses at GPU idle and resets the step clock. */
void psCheckpoint(PsxCheckpoint *request)
{
    static int initialized;
    static unsigned char data[512];
    long file,count;
    char *name="bu00:PSXSTUDIO-SHADOW";
    unsigned int level;
    if(!initialized) {InitCARD(1);StartCARD();ChangeClearPAD(0);_bu_init();initialized=1;}
    request->status=-1;
    if(request->operation==PSX_CHECKPOINT_LOAD) {
        file=open(name,O_RDONLY);
        if(file>=0) {
            count=read(file,data,512);close(file);
            level=count==512?psx_checkpoint_decode(data):0;
            if(level) {request->scene_id=level;request->status=1;}
        }
    } else if(request->operation==PSX_CHECKPOINT_SAVE &&
              (request->scene_id>=1 && request->scene_id<=PSX_CHECKPOINT_SECTORS)) {
        file=open(name,O_WRONLY);
        if(file<0) {
            file=open(name,O_CREAT|0x10000L);
            if(file>=0) {close(file);file=open(name,O_WRONLY);}
        }
        if(file>=0) {
            psx_checkpoint_encode(data,request->scene_id);
            count=write(file,data,512);close(file);
            if(count==512) request->status=1;
        }
    }
    request->operation=0;
}
