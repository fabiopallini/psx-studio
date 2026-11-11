#include "audio.h"
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

static void le16(guint8 *p,guint n) {p[0]=n;p[1]=n>>8;}
static void le32(guint8 *p,guint n) {le16(p,n);le16(p+2,n>>16);}
static guint32 be32(const guint8 *p) {return (guint32)p[0]<<24 | (guint32)p[1]<<16 | (guint32)p[2]<<8 | p[3];}
static GBytes *wave(int channels,int bits,int frames,gboolean tone)
{
    int align=channels*bits/8,length=frames*align;
    guint8 *p=g_malloc0(44+length+(length&1));
    memcpy(p,"RIFF",4);le32(p+4,36+length+(length&1));memcpy(p+8,"WAVEfmt ",8);
    le32(p+16,16);le16(p+20,1);le16(p+22,channels);le32(p+24,22050);
    le32(p+28,22050*align);le16(p+32,align);le16(p+34,bits);memcpy(p+36,"data",4);le32(p+40,length);
    for(int i=0;i<frames;i++) for(int c=0;c<channels;c++) {
        int sample=tone ? (int)(16000*sin(i*2*G_PI*440/22050)) : 0;
        if(channels==2 && c==1) sample=-sample;
        if(bits==8) p[44+i*align+c]=(sample/256)+128;
        else le16(p+44+i*align+c*2,(guint)sample);
    }
    return g_bytes_new_take(p,44+length+(length&1));
}
static void silence(void)
{
    for(int bits=8;bits<=16;bits+=8) for(int channels=1;channels<=2;channels++) {
        GBytes *wav=wave(channels,bits,29,FALSE);GError *error=NULL;
        GBytes *vag=audio_encode_wav(wav,"silence",&error);g_assert_no_error(error);g_assert_nonnull(vag);
        gsize size;const guint8 *p=g_bytes_get_data(vag,&size);
        g_assert_cmpuint(size,==,112);g_assert_cmpmem(p,4,"VAGp",4);
        g_assert_cmpuint(be32(p+4),==,32);g_assert_cmpuint(be32(p+12),==,64);
        g_assert_cmpuint(be32(p+16),==,22050);g_assert_cmpstr((const char *)p+32,==,"silence");
        for(gsize i=48;i<size;i++) g_assert_cmpuint(p[i],==,i==size-15?1:0);
        g_bytes_unref(vag);g_bytes_unref(wav);
    }
}
static void quality(void)
{
    GBytes *wav=wave(1,16,22050,TRUE);GError *error=NULL;
    GBytes *vag=audio_encode_wav(wav,"tone",&error);g_assert_no_error(error);
    gsize size;const guint8 *p=g_bytes_get_data(vag,&size);
    g_assert_cmpuint(p[size-15],==,1);
    /* Independent reference decoder: reconstructed 16-bit PCM, filters in Q6. */
    static const int a[]={0,60,115,98,122},b[]={0,0,-52,-55,-60};
    int h1=0,h2=0,n=0;double squared=0;
    for(gsize offset=64;offset<size-16;offset+=16) {
        int filter=p[offset]>>4,shift=p[offset]&15;
        g_assert_cmpint(filter,<,5);g_assert_cmpint(shift,<=,12);g_assert_cmpuint(p[offset+1],==,0);
        for(int j=0;j<28;j++) {
            int q=(p[offset+2+j/2]>>((j%2)*4))&15;if(q>=8) q-=16;
            int sample=CLAMP(q*(1<<(12-shift))+(int)floor((h1*a[filter]+h2*b[filter]+32)/64.0),-32768,32767);
            h2=h1;h1=sample;
            if(n<22050) {double expected=(int)(16000*sin(n*2*G_PI*440/22050));squared+=(sample-expected)*(sample-expected);}
            n++;
        }
    }
    g_assert_cmpfloat(sqrt(squared/22050),<,120);
    /* Write a fixture for optional independent ffmpeg decoding. */
    if(g_getenv("PSX_AUDIO_FIXTURE")) g_assert_true(g_file_set_contents(g_getenv("PSX_AUDIO_FIXTURE"),(const char *)p,size,NULL));
    g_bytes_unref(vag);g_bytes_unref(wav);
    wav=wave(2,16,29,TRUE);vag=audio_encode_wav(wav,"downmix",&error);g_assert_no_error(error);
    p=g_bytes_get_data(vag,&size);
    for(gsize i=48;i<size;i++) g_assert_cmpuint(p[i],==,i==size-15?1:0);
    g_bytes_unref(vag);g_bytes_unref(wav);
}
static void malformed(void)
{
    GBytes *original=wave(1,16,28,FALSE);gsize size;const guint8 *src=g_bytes_get_data(original,&size);
    for(int test=0;test<9;test++) {
        guint8 *p=g_memdup2(src,size);gsize length=size;
        switch(test) {
        case 0:p[0]='x';break;
        case 1:length--;break;
        case 2:le32(p+40,0xffffffffU);break;
        case 3:le16(p+20,3);break; /* float WAV */
        case 4:le16(p+22,6);break;
        case 5:le16(p+34,24);break;
        case 6:le32(p+24,96000);break;
        case 7:le16(p+32,1);break;
        case 8:le32(p+28,1);break;
        }
        GBytes *wav=g_bytes_new_take(p,length);GError *error=NULL;
        GBytes *vag=audio_encode_wav(wav,"bad",&error);
        g_assert_null(vag);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&error);g_bytes_unref(wav);
    }
    g_bytes_unref(original);
}
static void chunks_and_import(void)
{
    GBytes *original=wave(1,8,29,FALSE);gsize size;const guint8 *src=g_bytes_get_data(original,&size);
    guint8 *p=g_malloc(size+10);memcpy(p,src,12);memcpy(p+12,"JUNK",4);le32(p+16,1);p[20]=42;p[21]=0;
    memcpy(p+22,src+12,size-12);le32(p+4,size+2);
    GBytes *wav=g_bytes_new_take(p,size+10);GError *error=NULL;
    GBytes *vag=audio_encode_wav(wav,"odd",&error);g_assert_no_error(error);g_assert_nonnull(vag);g_bytes_unref(vag);
    char *dir=g_dir_make_tmp("psx-audio-XXXXXX",&error);g_assert_no_error(error);
    char *source=g_build_filename(dir,"odd.wav",NULL);
    g_assert_true(g_file_set_contents(source,(const char *)p,size+10,&error));g_assert_no_error(error);
    char *target=audio_import_wav(source,dir,&error);g_assert_no_error(error);g_assert_nonnull(target);
    char *again=audio_import_wav(source,dir,&error);g_assert_no_error(error);g_assert_cmpstr(target,==,again);g_free(again);
    char *base=g_path_get_basename(target);g_assert_cmpuint(strlen(base),==,12);g_free(base);
    g_assert_true(g_file_set_contents(target,"keep",4,&error));g_assert_no_error(error);
    again=audio_import_wav(source,dir,&error);g_assert_null(again);g_assert_nonnull(error);g_clear_error(&error);
    char *kept;g_assert_true(g_file_get_contents(target,&kept,NULL,NULL));g_assert_cmpstr(kept,==,"keep");g_free(kept);
    g_unlink(source);g_unlink(target);g_rmdir(dir);g_free(source);g_free(target);g_free(dir);
    g_bytes_unref(wav);g_bytes_unref(original);
}
int main(int argc,char **argv)
{
    g_test_init(&argc,&argv,NULL);
    g_test_add_func("/audio/silence",silence);g_test_add_func("/audio/quality",quality);
    g_test_add_func("/audio/malformed",malformed);g_test_add_func("/audio/chunks-and-import",chunks_and_import);
    return g_test_run();
}
