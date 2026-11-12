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
    static unsigned char data[8192];
    long file,count,bytes;
    char name[26];
    unsigned int level;
    if(!psx_checkpoint_name_valid(request->file)) {request->status=-1;request->operation=0;return;}
    memcpy(name,"bu00:",5);strcpy(name+5,request->file);
    if(!initialized) {InitCARD(1);StartCARD();ChangeClearPAD(0);_bu_init();initialized=1;}
    request->status=-1;bytes=request->data?8192:512;
    if(request->operation==PSX_CHECKPOINT_LOAD) {
        file=open(name,O_RDONLY);
        if(file>=0) {
            count=read(file,data,bytes);close(file);
            level=count==bytes?psx_checkpoint_decode(data):0;
            if(level && request->data && !psx_checkpoint_payload_decode(data,request->data,request->capacity,&request->size)) level=0;
            if(level) {request->scene_id=level;request->status=1;}
        }
    } else if(request->operation==PSX_CHECKPOINT_SAVE && request->scene_id!=0 &&
              (!request->data || (request->size && request->size<=PSX_SAVE_PAYLOAD_MAX && request->size<=request->capacity))) {
        file=open(name,O_WRONLY);
        if(file<0) {
            file=open(name,O_CREAT|0x10000L);
            if(file>=0) {close(file);file=open(name,O_WRONLY);}
        }
        if(file>=0) {
            if(request->data) psx_checkpoint_payload_encode(data,request->scene_id,request->title,request->data,request->size);
            else psx_checkpoint_encode(data,request->scene_id,request->title);
            count=write(file,data,bytes);close(file);
            if(count==bytes) request->status=1;
        }
    }
    request->operation=0;
}
