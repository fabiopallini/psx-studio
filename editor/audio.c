#include "audio.h"
#include <string.h>

static gboolean invalid(GError **error, const char *message)
{
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
    return FALSE;
}
static guint u16(const guint8 *p) { return p[0] | (guint)p[1] << 8; }
static guint32 u32(const guint8 *p) { return u16(p) | (guint32)u16(p+2) << 16; }
static void be32(guint8 *p, guint32 n)
{
    p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n;
}
static int clamp(int n) { return CLAMP(n,-32768,32767); }
/* Arithmetic right shift, including negative values, without implementation-
 * defined signed shifts. Predictor coefficients are Q6. */
static int floor64(int n) { return n>=0 ? n/64 : -((-n+63)/64); }
static const int coefficients[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};

/* Choose filter and quantization by decoded squared error, carrying reconstructed
 * history between blocks. SPU: 28 signed samples / 16 bytes, low nibble first.
 * https://psx-spx.consoledev.net/soundprocessingunitspu/#spu-adpcm-samples */
static void block_encode(const int samples[28], guint8 *out, int history[4])
{
    guint64 best=G_MAXUINT64;
    for(int filter=0;filter<5;filter++) for(int shift=0;shift<=12;shift++) {
        int h1=history[0],h2=history[1],step=1<<(12-shift);
        guint64 error=0;
        guint8 block[16]={0};block[0]=(filter<<4)|shift;
        for(int i=0;i<28;i++) {
            int prediction=floor64(h1*coefficients[filter][0]+h2*coefficients[filter][1]+32);
            int residual=samples[i]-prediction;
            int q=residual>=0 ? (residual+step/2)/step : -((-residual+step/2)/step);
            q=CLAMP(q,-8,7);
            int decoded=clamp(prediction+q*step),difference=samples[i]-decoded;
            error+=(guint64)((gint64)difference*difference);
            block[2+i/2]|=(q&15)<<((i&1)*4);
            h2=h1;h1=decoded;
        }
        if(error<best) {best=error;memcpy(out,block,16);history[2]=h1;history[3]=h2;}
    }
    history[0]=history[2];history[1]=history[3];
}

GBytes *audio_encode_wav(GBytes *wav, const char *name, GError **error)
{
    gsize size;const guint8 *p=g_bytes_get_data(wav,&size);
    const guint8 *fmt=NULL,*pcm=NULL;gsize fmt_size=0,pcm_size=0;
    if(size<12 || size>64*1024*1024 || memcmp(p,"RIFF",4) || memcmp(p+8,"WAVE",4) ||
       u32(p+4)<4 || (guint64)u32(p+4)+8!=size) {
        invalid(error,"Invalid WAV: expected a complete RIFF/WAVE file (maximum 64 MB).");return NULL;
    }
    for(gsize offset=12;offset<size;) {
        if(size-offset<8) {invalid(error,"Truncated WAV chunk header.");return NULL;}
        guint32 length=u32(p+offset+4);
        guint64 next=(guint64)offset+8+length+(length&1);
        if(next>size) {invalid(error,"Truncated WAV chunk data.");return NULL;}
        if(!memcmp(p+offset,"fmt ",4)) {
            if(fmt) {invalid(error,"Duplicate WAV format chunk.");return NULL;}
            fmt=p+offset+8;fmt_size=length;
        } else if(!memcmp(p+offset,"data",4)) {
            if(pcm) {invalid(error,"Multiple WAV data chunks are unsupported.");return NULL;}
            pcm=p+offset+8;pcm_size=length;
        }
        offset=(gsize)next;
    }
    if(!fmt || fmt_size<16 || !pcm || !pcm_size) {
        invalid(error,"WAV must contain a PCM format and nonempty audio data.");return NULL;
    }
    guint channels=u16(fmt+2),bits=u16(fmt+14),align=u16(fmt+12);
    guint32 rate=u32(fmt+4);
    if(u16(fmt)!=1 || (channels!=1 && channels!=2) || (bits!=8 && bits!=16)) {
        invalid(error,"Export WAV as uncompressed PCM, 8 or 16 bit, mono or stereo.");return NULL;
    }
    if(rate<1000 || rate>44100 || align!=channels*(bits/8) ||
       u32(fmt+8)!=rate*align || pcm_size%align) {
        invalid(error,"Invalid PCM layout or sample rate: use 1000–44100 Hz and complete sample frames.");return NULL;
    }
    gsize frames=pcm_size/align,blocks=(frames+27)/28;
    /* A silent leading block initializes interpolation history. A separate end
     * block stops a one-shot voice without cutting off the final audible block. */
    gsize output_size=48+(blocks+2)*16;
    guint8 *out=g_malloc0(output_size);
    memcpy(out,"VAGp",4);be32(out+4,0x20);be32(out+12,output_size-48);be32(out+16,rate);
    for(gsize i=0;name && name[i] && i<16;i++) out[32+i]=g_ascii_isprint(name[i]) ? name[i] : '_';
    int history[4]={0};
    for(gsize block=0;block<blocks;block++) {
        int samples[28]={0};
        for(gsize j=0;j<28 && block*28+j<frames;j++) {
            const guint8 *frame=pcm+(block*28+j)*align;
            int sum=0;
            for(guint c=0;c<channels;c++) {
                int sample=bits==8 ? ((int)frame[c]-128)*256 : (int)u16(frame+c*2);
                if(bits==16 && sample>=32768) sample-=65536;
                sum+=sample;
            }
            samples[j]=sum/(int)channels;
        }
        block_encode(samples,out+64+block*16,history);
    }
    out[output_size-15]=1; /* End, no repeat. No looping is implied by WAV metadata. */
    return g_bytes_new_take(out,output_size);
}

char *audio_import_wav(const char *source, const char *directory, GError **error)
{
    GFile *file=g_file_new_for_path(source);
    GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_SIZE,0,NULL,error);
    g_object_unref(file);
    if(!info) return NULL;
    goffset length=g_file_info_get_size(info);g_object_unref(info);
    if(length>64*1024*1024) {invalid(error,"WAV too large (maximum 64 MB).");return NULL;}
    char *contents;gsize size;
    if(!g_file_get_contents(source,&contents,&size,error)) return NULL;
    GBytes *wav=g_bytes_new_take(contents,size);
    char *base=g_path_get_basename(source),*dot=strrchr(base,'.');if(dot) *dot=0;
    GBytes *vag=audio_encode_wav(wav,base,error);g_free(base);g_bytes_unref(wav);
    if(!vag) return NULL;
    const guint8 *encoded=g_bytes_get_data(vag,&size);
    char *sha=g_compute_checksum_for_data(G_CHECKSUM_SHA256,encoded,size);
    /* Stable DOS 8.3 name; never overwrite unrelated named assets. */
    char *name=g_strdup_printf("%.8s.vag",sha),*target=g_build_filename(directory,name,NULL);
    gboolean ok=g_mkdir_with_parents(directory,0755)==0;
    if(!ok) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot create the project data folder.");
    if(ok && g_file_test(target,G_FILE_TEST_EXISTS)) {
        char *old=NULL;gsize old_size=0;
        ok=g_file_get_contents(target,&old,&old_size,error);
        if(ok && (old_size!=size || memcmp(old,encoded,size)))
            ok=invalid(error,"An unrelated asset already uses this VAG filename.");
        g_free(old);
    } else if(ok) ok=g_file_set_contents_full(target,(const char *)encoded,size,G_FILE_SET_CONTENTS_CONSISTENT,0644,error);
    g_free(name);g_free(sha);g_bytes_unref(vag);
    if(!ok) {g_free(target);return NULL;}
    return target;
}
