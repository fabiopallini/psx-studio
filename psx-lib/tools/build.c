/* Desktop build orchestration. No Python or shell is used for game builds. */
#include "core.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

static gboolean safe_path(const char *path,GError **error);
static GPtrArray *directory_names(const char *path,GError **error);
static gboolean failure(GError **error,const char *message)
{ g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,message);return FALSE; }
static char *file_at(const char *base,const char *name)
{ return g_build_filename(base,name,NULL); }
static gboolean exists(const char *base,const char *name)
{ char *p=file_at(base,name);gboolean ok=g_file_test(p,G_FILE_TEST_EXISTS);g_free(p);return ok; }
static gboolean read_at(const char *base,const char *name,char **text,gsize *length,GError **error)
{ char *p=file_at(base,name);gboolean ok=g_file_get_contents(p,text,length,error);g_free(p);return ok; }
static gboolean write_at(const char *base,const char *name,const char *text,gssize length,GError **error)
{ char *p=file_at(base,name);gboolean ok=safe_path(p,error) && g_file_set_contents(p,text,length,error);g_free(p);return ok; }
static gboolean no_link(const char *path,GError **error)
{
#ifdef G_OS_WIN32
    gunichar2 *wide=g_utf8_to_utf16(path,-1,NULL,NULL,error);if(!wide) return FALSE;
    DWORD attributes=GetFileAttributesW((LPCWSTR)wide);g_free(wide);
    if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
        return failure(error,"Build paths cannot contain symbolic links or junctions.");
#else
    if(g_file_test(path,G_FILE_TEST_IS_SYMLINK)) return failure(error,"Build paths cannot contain symbolic links.");
#endif
    return TRUE;
}
static gboolean safe_path(const char *path,GError **error)
{
    char *parent=g_path_get_dirname(path);
    gboolean ok=!strcmp(path,parent) || (safe_path(parent,error) && no_link(path,error));
    g_free(parent);return ok;
}
static gboolean make_directory(const char *path,GError **error)
{
    if(!safe_path(path,error)) return FALSE;
    if(g_mkdir_with_parents(path,0755)==0) return TRUE;
    g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(errno),"Cannot create %s: %s",path,g_strerror(errno));return FALSE;
}
static gboolean copy_file(const char *source,const char *destination,GError **error)
{
    if(!safe_path(source,error) || !safe_path(destination,error)) return FALSE;
    GFile *a=g_file_new_for_path(source),*b=g_file_new_for_path(destination);
    gboolean ok=g_file_copy(a,b,G_FILE_COPY_OVERWRITE,NULL,NULL,NULL,error);
    g_object_unref(a);g_object_unref(b);return ok;
}
static gboolean copy_tree(const char *source,const char *destination,GError **error)
{
    if(!safe_path(source,error) || !make_directory(destination,error)) return FALSE;
    GDir *directory=g_dir_open(source,0,error);if(!directory) return FALSE;
    const char *name;gboolean ok=TRUE;
    while(ok && (name=g_dir_read_name(directory))) {
        char *a=file_at(source,name),*b=file_at(destination,name);
        ok=no_link(a,error) && (g_file_test(a,G_FILE_TEST_IS_DIR)?copy_tree(a,b,error):copy_file(a,b,error));
        g_free(a);g_free(b);
    }
    g_dir_close(directory);return ok;
}
static gboolean remove_tree(const char *path,GError **error)
{
    if(!safe_path(path,error)) return FALSE;
    if(g_file_test(path,G_FILE_TEST_IS_DIR)) {
        GDir *directory=g_dir_open(path,0,error);if(!directory) return FALSE;
        const char *name;gboolean ok=TRUE;
        while(ok && (name=g_dir_read_name(directory))) {char *child=file_at(path,name);ok=remove_tree(child,error);g_free(child);}
        g_dir_close(directory);if(!ok) return FALSE;
        if(g_rmdir(path)==0) return TRUE;
    } else if(!g_file_test(path,G_FILE_TEST_EXISTS) || g_unlink(path)==0) return TRUE;
    g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(errno),"Cannot remove %s: %s",path,g_strerror(errno));return FALSE;
}
static gboolean copy_relative(const char *source,const char *destination,const char *name,GError **error)
{ char *a=file_at(source,name),*b=file_at(destination,name);gboolean ok=copy_file(a,b,error);g_free(a);g_free(b);return ok; }
static gboolean copy_subtree(const char *source,const char *sub,const char *destination,GError **error)
{ char *a=file_at(source,sub);gboolean ok=copy_tree(a,destination,error);g_free(a);return ok; }
static gboolean rewrite(const char *base,const char *name,const char *before,const char *after,GError **error)
{
    char *text=NULL;if(!read_at(base,name,&text,NULL,error)) return FALSE;
    GString *out=g_string_new(text);g_free(text);g_string_replace(out,before,after,0);
    gboolean ok=write_at(base,name,out->str,out->len,error);g_string_free(out,TRUE);return ok;
}
static guint32 le32(const guchar *p)
{ return (guint32)p[0]|((guint32)p[1]<<8)|((guint32)p[2]<<16)|((guint32)p[3]<<24); }
static guint le16(const guchar *p) {return p[0]|((guint)p[1]<<8);}

typedef struct {const guchar *data;gsize length;guint stride,offset;} Disc;
typedef struct {guint32 sector,length;} Extent;
static GByteArray *disc_extent(const Disc *disc,guint32 sector,guint32 count,GError **error)
{
    if(count>disc->length) {failure(error,"Invalid ISO extent length.");return NULL;}
    GByteArray *bytes=g_byte_array_sized_new(count);
    while(bytes->len<count) {
        guint64 position=(guint64)sector*disc->stride+disc->offset;
        guint amount=MIN(2048U,count-bytes->len);
        if(position+2048>disc->length) {g_byte_array_unref(bytes);failure(error,"Truncated ISO file.");return NULL;}
        g_byte_array_append(bytes,disc->data+(gsize)position,amount);sector++;
    }
    return bytes;
}
static gboolean disc_directory(const Disc *disc,guint32 sector,guint32 length,const char *prefix,
                               guint depth,GHashTable *files,GError **error)
{
    if(depth>16) return failure(error,"ISO directory nesting limit exceeded.");
    GByteArray *bytes=disc_extent(disc,sector,length,error);if(!bytes) return FALSE;
    guint cursor=0;gboolean ok=TRUE;
    while(ok && cursor<bytes->len) {
        guint size=bytes->data[cursor];
        if(!size) {cursor=(cursor/2048+1)*2048;continue;}
        if(size<34 || cursor+size>bytes->len) {ok=failure(error,"Invalid ISO9660 directory.");break;}
        const guchar *record=bytes->data+cursor;guint namesize=record[32];
        if(namesize+33>size) {ok=failure(error,"Invalid ISO filename.");break;}
        if(!(namesize==1 && record[33]<=1)) {
            char *name=g_strndup((const char*)record+33,namesize),*full=g_strconcat(prefix,name,NULL);g_free(name);
            if(record[25]&2) {
                char *next=g_strconcat(full,"/",NULL);
                ok=disc_directory(disc,le32(record+2),le32(record+10),next,depth+1,files,error);g_free(next);g_free(full);
            } else {
                Extent *extent=g_new(Extent,1);extent->sector=le32(record+2);extent->length=le32(record+10);
                g_hash_table_replace(files,full,extent);
            }
        }
        cursor+=size;
    }
    g_byte_array_unref(bytes);return ok;
}
static gboolean disc_file(const Disc *disc,GHashTable *files,const char *name,const char *host,GError **error)
{
    Extent *extent=g_hash_table_lookup(files,name);char *text=NULL;gsize length;
    if(!extent) {g_set_error(error,G_IO_ERROR,G_IO_ERROR_FAILED,"%s is missing from the CD image.",name);return FALSE;}
    if(!g_file_get_contents(host,&text,&length,error)) return FALSE;
    GByteArray *bytes=disc_extent(disc,extent->sector,extent->length,error);
    gboolean ok=bytes && length==bytes->len && !memcmp(text,bytes->data,length);
    g_free(text);if(bytes) g_byte_array_unref(bytes);
    if(!ok && !*error) g_set_error(error,G_IO_ERROR,G_IO_ERROR_FAILED,"%s differs from its source file.",name);
    return ok;
}
static JsonParser *read_manifest(const char *output,GError **error)
{
    char *path=file_at(output,"src/assets.json");JsonParser *parser=json_parser_new();
    gboolean ok=json_parser_load_from_file(parser,path,error);g_free(path);
    if(ok) {
        JsonNode *document=json_parser_get_root(parser);
        JsonObject *manifest=JSON_NODE_HOLDS_OBJECT(document)?json_node_get_object(document):NULL;
        JsonNode *textures=manifest?json_object_get_member(manifest,"textures"):NULL;
        if(!textures || !JSON_NODE_HOLDS_ARRAY(textures)) ok=failure(error,"Invalid texture manifest.");
        if(ok) {
            JsonArray *array=json_node_get_array(textures);
            for(guint i=0;ok && i<json_array_get_length(array);i++) {
                JsonNode *node=json_array_get_element(array,i);
                JsonNode *file=JSON_NODE_HOLDS_OBJECT(node)?json_object_get_member(json_node_get_object(node),"file"):NULL;
                if(!file || json_node_get_value_type(file)!=G_TYPE_STRING ||
                   !g_regex_match_simple("^[A-Z0-9_]{1,8}\\.TIM$",json_node_get_string(file),0,0))
                    ok=failure(error,"Invalid texture filename in manifest.");
                if(ok && json_object_has_member(json_node_get_object(node),"directory")) {
                    JsonNode *dir=json_object_get_member(json_node_get_object(node),"directory");
                    if(json_node_get_value_type(dir)!=G_TYPE_STRING || !g_regex_match_simple("^B[0-9]{3,5}$",json_node_get_string(dir),0,0))
                        ok=failure(error,"Invalid texture directory in manifest.");
                }
            }
        }
    }
    if(!ok) {g_object_unref(parser);return NULL;}return parser;
}
static gboolean verify_build(const char *output,GError **error)
{
    if(!safe_path(output,error)) return FALSE;
    char *log=NULL;if(!read_at(output,"src/BUILD.LOG",&log,NULL,error)) return FALSE;
    char *decoded=g_convert(log,-1,"UTF-8","CP437",NULL,NULL,NULL);
    if(!decoded) decoded=g_utf8_make_valid(log,-1);
    g_free(log);log=decoded;
    char **lines=g_strsplit(log,"\n",-1);gboolean ok=TRUE;
    for(guint i=0;lines[i];i++) if(g_regex_match_simple("\\b(error|fatal)\\b",lines[i],G_REGEX_CASELESS,0) &&
        !g_regex_match_simple("\\b0 errors?\\b",lines[i],G_REGEX_CASELESS,0)) {ok=failure(error,"Errors in BUILD.LOG.");break;}
    g_print("%s\n",log);
    g_free(log);g_strfreev(lines);if(!ok) return FALSE;
    if(!exists(output,"SUCCESS.OK")) return failure(error,"Build success marker is missing.");
    char *exe=NULL;gsize length;
    if(!read_at(output,"src/MAIN.EXE",&exe,&length,error)) return FALSE;
    ok=length>=8 && !memcmp(exe,"PS-X EXE",8);g_free(exe);
    if(!ok) return failure(error,"MAIN.EXE does not have a valid PS-X EXE header.");
    char *path=file_at(output,"cdrom/GAME.ISO");GMappedFile *mapped=g_mapped_file_new(path,FALSE,error);g_free(path);
    if(!mapped) return FALSE;
    Disc disc={(const guchar*)g_mapped_file_get_contents(mapped),g_mapped_file_get_length(mapped),0,0};
    const guchar *descriptor=NULL;guint strides[]={2352,2048},offsets[]={24,0};
    for(guint i=0;i<2;i++) {
        gsize start=16*strides[i]+offsets[i];
        if(disc.length>=start+2048 && !memcmp(disc.data+start,"\001CD001\001",7)) {
            disc.stride=strides[i];disc.offset=offsets[i];descriptor=disc.data+start;break;
        }
    }
    if(!descriptor || disc.length%disc.stride) ok=failure(error,"Invalid sector structure or ISO9660 descriptor.");
    GHashTable *files=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    if(ok) ok=disc_directory(&disc,le32(descriptor+158),le32(descriptor+166),"",0,files,error);
    const char *names[]={"MAIN.EXE;1","SYSTEM.CNF;1"},*hosts[]={"src/MAIN.EXE","cdrom/SYSTEM.CNF"};
    for(guint i=0;ok && i<2;i++) {path=file_at(output,hosts[i]);ok=disc_file(&disc,files,names[i],path,error);g_free(path);}
    if(ok && exists(output,"src/assets.json")) {
        JsonParser *parser=read_manifest(output,error);ok=parser!=NULL;
        if(parser) {
            JsonArray *textures=json_object_get_array_member(json_node_get_object(json_parser_get_root(parser)),"textures");
            for(guint i=0;ok && i<json_array_get_length(textures);i++) {
                JsonObject *texture=json_array_get_object_element(textures,i);
                const char *name=json_object_get_string_member(texture,"file");
                const char *dir=json_object_has_member(texture,"directory")?json_object_get_string_member(texture,"directory"):NULL;
                char *discname=dir?g_strdup_printf("ASSETS/%s/%s;1",dir,name):g_strconcat("ASSETS/",name,";1",NULL);
                char *host=g_build_filename(output,"data",name,NULL);
                ok=disc_file(&disc,files,discname,host,error);g_free(discname);g_free(host);
            }
            g_object_unref(parser);
        }
    }
    if(ok) {
        char *cue=g_strdup_printf("FILE \"GAME.ISO\" BINARY\n  TRACK 01 %s\n    INDEX 01 00:00:00\n",disc.stride==2352?"MODE2/2352":"MODE1/2048");
        ok=write_at(output,"cdrom/game.cue",cue,-1,error);g_free(cue);
        if(ok) g_print("Verified PS-X EXE, CD sectors and contents: %s\n",output);
    }
    g_hash_table_unref(files);g_mapped_file_unref(mapped);return ok;
}

#ifdef G_OS_WIN32
typedef HANDLE BuildLock;
static gboolean acquire_lock(const char *output,BuildLock *lock,GError **error)
{
    char *path=file_at(output,".build.lock");gunichar2 *wide=g_utf8_to_utf16(path,-1,NULL,NULL,error);g_free(path);
    if(!wide) return FALSE;
    *lock=CreateFileW((LPCWSTR)wide,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);g_free(wide);
    if(*lock==INVALID_HANDLE_VALUE) return failure(error,"Cannot open the build lock.");
    OVERLAPPED overlap;memset(&overlap,0,sizeof(overlap));
    if(!LockFileEx(*lock,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap)) {
        CloseHandle(*lock);*lock=INVALID_HANDLE_VALUE;return failure(error,"A build of this project is already running.");
    }
    return TRUE;
}
static void release_lock(BuildLock lock) {if(lock!=INVALID_HANDLE_VALUE) CloseHandle(lock);}
#define NO_LOCK INVALID_HANDLE_VALUE
#else
typedef int BuildLock;
static gboolean acquire_lock(const char *output,BuildLock *lock,GError **error)
{
    char *path=file_at(output,".build.lock");*lock=g_open(path,O_CREAT|O_RDWR,0600);g_free(path);
    if(*lock<0) return failure(error,"Cannot open the build lock.");
    if(flock(*lock,LOCK_EX|LOCK_NB)!=0) {close(*lock);*lock=-1;return failure(error,"A build of this project is already running.");}
    return TRUE;
}
static void release_lock(BuildLock lock) {if(lock>=0) close(lock);}
#define NO_LOCK (-1)
#endif

static gboolean dos_text(const char *path,GError **error)
{
    char *text=NULL;gsize length;if(!g_file_get_contents(path,&text,&length,error)) return FALSE;
    GString *out=g_string_new(NULL);
    for(gsize i=0;i<length;i++) {
        if((guchar)text[i]>127 || !text[i]) {g_free(text);g_string_free(out,TRUE);return failure(error,"Console source must contain ASCII text only.");}
        if(text[i]!='\r') {if(text[i]=='\n') g_string_append_c(out,'\r');g_string_append_c(out,text[i]);}
    }
    gboolean ok=g_file_set_contents(path,out->str,out->len,error);g_free(text);g_string_free(out,TRUE);return ok;
}
static gboolean normalize_sources(const char *output,GError **error)
{
    char *source=file_at(output,"src");GDir *dir=g_dir_open(source,0,error);gboolean ok=dir!=NULL;const char *name;
    while(ok && (name=g_dir_read_name(dir))) {
        char *lower=g_ascii_strdown(name,-1);
        if(g_str_has_suffix(lower,".c") || g_str_has_suffix(lower,".h") || g_str_has_suffix(lower,".mak") || g_str_has_suffix(lower,".rsp")) {
            char *path=file_at(source,name);ok=dos_text(path,error);g_free(path);
        }
        g_free(lower);
    }
    if(dir) g_dir_close(dir);
    g_free(source);
    const char *files[]={"cdrom/CONF.CTI","cdrom/SYSTEM.CNF"};
    for(guint i=0;ok && i<2;i++) {char *path=file_at(output,files[i]);ok=dos_text(path,error);g_free(path);}
    return ok;
}
static gboolean prepare_custom_sources(const char *source,const char *src,int max_objects,
    int render_triangles,const char *resources,const char *asset_root,GError **error)
{
    GPtrArray *names=directory_names(source,error);if(!names) return FALSE;
    GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
    JsonObject *manifest=json_object_new();JsonArray *modules=json_array_new(),*textures=json_array_new();
    json_object_set_int_member(manifest,"version",1);
    json_object_set_int_member(manifest,"max_objects",max_objects);
    json_object_set_int_member(manifest,"render_triangles",render_triangles);
    json_object_set_array_member(manifest,"modules",modules);json_object_set_array_member(manifest,"textures",textures);
    gboolean ok=TRUE,has_main=FALSE;
    for(guint i=0;ok && i<names->len;i++) {
        const char *name=g_ptr_array_index(names,i);char *lower=g_ascii_strdown(name,-1);
        gboolean code=g_str_has_suffix(lower,".c"),header=g_str_has_suffix(lower,".h"),tim=g_str_has_suffix(lower,".tim");
        if(!code && !header && !tim) {g_free(lower);continue;}
        if(!g_regex_match_simple("^[a-z0-9_]{1,8}\\.(c|h|tim)$",lower,0,0) || g_hash_table_contains(seen,lower))
            ok=failure(error,"C source projects require unique, flat DOS 8.3 C/header/TIM filenames.");
        if(ok && strcmp(lower,"main.c") && exists(src,lower))
            ok=failure(error,"A custom source attempts to replace an engine runtime file.");
        if(ok) g_hash_table_add(seen,g_strdup(lower));
        char *target=tim?g_ascii_strup(name,-1):g_strdup(lower);
        if(ok) {
            char *from=file_at(source,name),*to=file_at(src,target);
            ok=copy_file(from,to,error);g_free(from);g_free(to);
        }
        if(ok && code) {
            if(!strcmp(lower,"main.c")) has_main=TRUE;
            else json_array_add_string_element(modules,target);
        }
        if(ok && tim) {
            char *bytes=NULL;gsize length=0;ok=read_at(src,target,&bytes,&length,error);
            if(ok && (length<20 || le32((guchar*)bytes)!=16 || le32((guchar*)bytes+4)!=2))
                ok=failure(error,"C source resources must be direct-color 16-bit TIM files.");
            if(ok) {
                const guchar *b=(const guchar*)bytes;
                guint x=le16(b+12),y=le16(b+14),w=le16(b+16),h=le16(b+18);
                if(x<320 || x+w>1024 || y+h>512 || (x+w>960 && y+h>256) || (x%64)+w>256 || (y%256)+h>256)
                    ok=failure(error,"Custom TIM overlaps framebuffer/font memory or VRAM bounds.");
                if(ok) {
                    JsonObject *image=json_object_new();json_object_set_string_member(image,"file",target);
                    json_object_set_int_member(image,"x",x);json_object_set_int_member(image,"y",y);
                    json_object_set_int_member(image,"width",w);json_object_set_int_member(image,"height",h);
                    json_array_add_object_element(textures,image);
                }
            }
            g_free(bytes);
        }
        g_free(target);g_free(lower);
    }
    if(ok && !has_main) ok=failure(error,"A C source project requires main.c.");
    if(ok && resources) {
        ok=resources_export(resources,asset_root,src,error);
        char *path=file_at(src,"assets.json");JsonParser *parser=json_parser_new();
        if(ok) ok=json_parser_load_from_file(parser,path,error);
        if(ok) {
            JsonObject *bank=json_node_get_object(json_parser_get_root(parser));
            JsonArray *rs=json_object_get_array_member(bank,"modules"),*ts=json_object_get_array_member(bank,"textures");
            for(guint i=0;i<json_array_get_length(rs);i++) json_array_add_string_element(modules,json_array_get_string_element(rs,i));
            for(guint i=0;i<json_array_get_length(ts);i++) json_array_add_element(textures,json_node_copy(json_array_get_element(ts,i)));
            json_object_set_int_member(manifest,"triangles",json_object_get_int_member(bank,"triangles"));
        }
        g_object_unref(parser);g_free(path);
    }

    if(ok) {
        JsonNode *node=json_node_new(JSON_NODE_OBJECT);json_node_take_object(node,manifest);manifest=NULL;
        char *text=json_to_string(node,TRUE);ok=write_at(src,"assets.json",text,-1,error);
        g_free(text);json_node_free(node);
    }
    if(manifest) json_object_unref(manifest);
    g_hash_table_unref(seen);g_ptr_array_unref(names);return ok;
}

static gboolean prepare_application(const char *output,const char *source_root,int max_objects,
    int render_triangles,const char *resources,const char *asset_root,gboolean diagnostics,GError **error)
{
    char *src=file_at(output,"src"),*data=file_at(output,"data");
    gboolean ok=remove_tree(data,error) && make_directory(data,error) &&
        prepare_custom_sources(source_root,src,max_objects,render_triangles,resources,asset_root,error);
    JsonParser *parser=ok?read_manifest(output,error):NULL;if(ok && !parser) ok=FALSE;
    if(parser) {
        JsonObject *manifest=json_node_get_object(json_parser_get_root(parser));
        JsonArray *modules=json_object_get_array_member(manifest,"modules"),*textures=json_object_get_array_member(manifest,"textures");
        gint64 max_objects=json_object_has_member(manifest,"max_objects")?json_object_get_int_member(manifest,"max_objects"):0;
        if(max_objects<1 || max_objects>512) ok=failure(error,"Invalid exported Scene object capacity.");
        if(ok) {
            char *capacity=g_strdup_printf("#define STUDIO_OBJECT_LIMIT %u",(guint)max_objects);
            ok=rewrite(output,"src/psx.h","#define STUDIO_OBJECT_LIMIT 512",capacity,error);g_free(capacity);
        }
        gint64 polygon_limit=json_object_has_member(manifest,"render_triangles")?json_object_get_int_member(manifest,"render_triangles"):8192;
        if(polygon_limit<256 || polygon_limit>8192) ok=failure(error,"Invalid exported render triangle capacity.");
        if(ok) {
            char *capacity=g_strdup_printf("#define STUDIO_POLYGON_LIMIT %u",(guint)polygon_limit);
            ok=rewrite(output,"src/psx.h","#define STUDIO_POLYGON_LIMIT 8192",capacity,error);g_free(capacity);
        }
        GString *sources=g_string_new("main.c object.c");
        for(guint i=0;i<json_array_get_length(modules);i++) {g_string_append_c(sources,' ');g_string_append(sources,json_array_get_string_element(modules,i));}
        /* DOS command tails are limited to 126 bytes. CCPSX control files
         * keep multi-Scene module lists out of that command tail. */
        g_string_append(sources," camera.c psx.c card.c\n");
        char *control=file_at(src,"sources.rsp");
        if(ok) ok=g_file_set_contents(control,sources->str,sources->len,error);
        g_free(control);
        if(ok) ok=rewrite(output,"src/makefile.mak","main.c object.c camera.c psx.c card.c","@sources.rsp",error);
        g_string_free(sources,TRUE);
        if(ok) ok=rewrite(output,"src/makefile.mak","-pedantic",diagnostics?"-pedantic-errors -DSTUDIO_DIAGNOSTICS":"-pedantic-errors",error);
        GString *entries=g_string_new("                    Directory ASSETS\n"),*log=g_string_new(NULL);
        const char *last_directory=NULL;
        for(guint i=0;ok && i<json_array_get_length(textures);i++) {
            JsonObject *texture=json_array_get_object_element(textures,i);const char *name=json_object_get_string_member(texture,"file");
            guint width=json_object_get_int_member(texture,"width"),height=json_object_get_int_member(texture,"height");
            if(!width || width>256 || !height || height>256) {ok=failure(error,"Invalid texture dimensions in manifest.");break;}
            char *bytes=NULL;gsize length;ok=read_at(src,name,&bytes,&length,error);
            if(ok) {
                const guchar *header=(const guchar*)bytes;guint size=20+2*width*height;
                if(length!=size || le32(header)!=16 || le32(header+4)!=2 || le32(header+8)!=size-8 ||
                   le16(header+12)!=(guint)json_object_get_int_member(texture,"x") ||
                   le16(header+14)!=(guint)json_object_get_int_member(texture,"y") ||
                   le16(header+16)!=width || le16(header+18)!=height) ok=failure(error,"Invalid native TIM or VRAM rectangle.");
            }
            g_free(bytes);if(ok) ok=copy_relative(src,data,name,error);
            const char *disc_directory=json_object_has_member(texture,"directory")?json_object_get_string_member(texture,"directory"):NULL;
            if(g_strcmp0(last_directory,disc_directory)) {
                if(last_directory) g_string_append(entries,"                        EndDirectory\n");
                if(disc_directory) g_string_append_printf(entries,"                        Directory %s\n",disc_directory);
                last_directory=disc_directory;
            }
            g_string_append_printf(entries,"                        File %s\n                            XAFileAttributes Form1 Data\n                            Source [root]\\data\\%s\n                        EndFile\n",name,name);
            g_string_append_printf(log,"Native TIM: %s %ux%u VRAM (%u,%u)\n",name,width,height,
                (guint)json_object_get_int_member(texture,"x"),(guint)json_object_get_int_member(texture,"y"));
        }
        if(last_directory) g_string_append(entries,"                        EndDirectory\n");
        g_string_append(entries,"                    EndDirectory\n                EndHierarchy");
        if(ok && json_array_get_length(textures)) ok=rewrite(output,"cdrom/CONF.CTI","                EndHierarchy",entries->str,error);
        if(ok) ok=write_at(output,"texture.log",log->str,log->len,error);
        g_string_free(entries,TRUE);g_string_free(log,TRUE);g_object_unref(parser);
    }
    g_free(src);g_free(data);return ok;
}

/* Incremental SDK objects live outside src/, which is regenerated every time.
 * Reuse is content-based, never based on DOS timestamps (two-second granularity).
 * Only a verified build can publish new object receipts. */
#define OBJECT_CACHE_VERSION 1
static const char *compile_flags(gboolean diagnostics)
{ return diagnostics?"-O3 -Wall -pedantic-errors -DSTUDIO_DIAGNOSTICS":"-O3 -Wall -pedantic-errors"; }
static void checksum_part(GChecksum *sum,const char *name,const char *bytes,gsize length)
{
    char size[32];g_snprintf(size,sizeof(size),"%" G_GSIZE_FORMAT,length);
    g_checksum_update(sum,(const guchar*)name,strlen(name)+1);
    g_checksum_update(sum,(const guchar*)size,strlen(size)+1);
    if(length) g_checksum_update(sum,(const guchar*)bytes,length);
}
static gint compare_names(gconstpointer a,gconstpointer b)
{ return strcmp(*(char *const*)a,*(char *const*)b); }
static GPtrArray *directory_names(const char *path,GError **error)
{
    GDir *dir=g_dir_open(path,0,error);if(!dir) return NULL;
    GPtrArray *names=g_ptr_array_new_with_free_func(g_free);const char *name;
    while((name=g_dir_read_name(dir))) g_ptr_array_add(names,g_strdup(name));
    g_dir_close(dir);g_ptr_array_sort(names,compare_names);return names;
}
static gboolean checksum_sdk_tree(const char *root,const char *relative,GChecksum *sum,
                                  gboolean *reusable,guint depth,GError **error)
{
    if(depth>64) return failure(error,"SDK directory nesting limit exceeded.");
    char *path=file_at(root,relative);gboolean ok=TRUE;
    if(g_file_test(path,G_FILE_TEST_IS_SYMLINK)) {
        /* Do not follow directory cycles or trust untracked symlink targets. */
        *reusable=FALSE;checksum_part(sum,relative,"symlink",7);
    } else if(g_file_test(path,G_FILE_TEST_IS_DIR)) {
        GPtrArray *names=directory_names(path,error);ok=names!=NULL;
        for(guint i=0;ok && i<names->len;i++) {
            char *child=g_build_filename(relative,g_ptr_array_index(names,i),NULL);
            ok=checksum_sdk_tree(root,child,sum,reusable,depth+1,error);g_free(child);
        }
        if(names) g_ptr_array_unref(names);
    } else {
        char *bytes=NULL;gsize length;
        ok=g_file_get_contents(path,&bytes,&length,error);
        if(ok) checksum_part(sum,relative,bytes,length);
        g_free(bytes);
    }
    g_free(path);return ok;
}
static char *object_context(const char *sdk,gboolean diagnostics,gboolean *reusable,GError **error)
{
    GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);*reusable=TRUE;
    checksum_part(sum,"schema","psyq-objects-1",strlen("psyq-objects-1"));
    checksum_part(sum,"sdk",sdk,strlen(sdk));
    const char *flags=compile_flags(diagnostics);checksum_part(sum,"flags",flags,strlen(flags));
    const char *dirs[]={"psyq","include","lib"};gboolean ok=TRUE;
    for(guint i=0;ok && i<G_N_ELEMENTS(dirs);i++) ok=checksum_sdk_tree(sdk,dirs[i],sum,reusable,0,error);
    char *result=ok?g_strdup(g_checksum_get_string(sum)):NULL;g_checksum_free(sum);return result;
}
static GHashTable *source_files(const char *src,GError **error)
{
    GPtrArray *names=directory_names(src,error);if(!names) return NULL;
    GHashTable *files=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    for(guint i=0;i<names->len;i++) {
        const char *name=g_ptr_array_index(names,i);char *lower=g_ascii_strdown(name,-1);
        if(g_str_has_suffix(lower,".c") || g_str_has_suffix(lower,".h")) g_hash_table_insert(files,lower,g_strdup(name));
        else g_free(lower);
    }
    g_ptr_array_unref(names);return files;
}
static gboolean checksum_source(const char *src,const char *name,GHashTable *files,GHashTable *visited,
                                 GChecksum *sum,gboolean *reusable,GError **error)
{
    char *lower=g_ascii_strdown(name,-1);
    if(g_hash_table_contains(visited,lower)) {g_free(lower);return TRUE;}
    g_hash_table_add(visited,lower);
    char *path=file_at(src,name),*text=NULL;gsize length;
    gboolean ok=safe_path(path,error) && g_file_get_contents(path,&text,&length,error);g_free(path);
    if(!ok) return FALSE;
    checksum_part(sum,lower,text,length);
    if(strstr(text,"__DATE__") || strstr(text,"__TIME__") || strstr(text,"__TIMESTAMP__")) *reusable=FALSE;
    GRegex *directive=g_regex_new("^[ \\t]*#[ \\t]*include[ \\t]+([^\\r\\n]+)",G_REGEX_MULTILINE,0,NULL);
    GRegex *literal=g_regex_new("^[\"<]([^\">]+)[\">]",0,0,NULL);
    GMatchInfo *matches=NULL;g_regex_match(directive,text,0,&matches);
    while(ok && g_match_info_matches(matches)) {
        char *argument=g_match_info_fetch(matches,1);GMatchInfo *include=NULL;
        if(g_regex_match(literal,argument,0,&include)) {
            char *header=g_match_info_fetch(include,1),*key=g_ascii_strdown(header,-1);
            const char *local=g_hash_table_lookup(files,key);
            if(local) ok=checksum_source(src,local,files,visited,sum,reusable,error);
            else if(strstr(header,"..") || strchr(header,':') || g_path_is_absolute(header)) *reusable=FALSE;
            /* All SDK headers are covered by the context hash. Macro and external
             * includes cannot be tracked reliably and force this unit to compile. */
            g_free(header);g_free(key);
        } else *reusable=FALSE;
        g_match_info_free(include);g_free(argument);g_match_info_next(matches,NULL);
    }
    g_match_info_free(matches);g_regex_unref(directive);g_regex_unref(literal);g_free(text);return ok;
}
static char *unit_fingerprint(const char *src,const char *name,GHashTable *files,const char *context,
                              gboolean *reusable,GError **error)
{
    GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);
    checksum_part(sum,"context",context,strlen(context));
    GHashTable *visited=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
    gboolean ok=checksum_source(src,name,files,visited,sum,reusable,error);
    char *result=ok?g_strdup(g_checksum_get_string(sum)):NULL;
    g_hash_table_unref(visited);g_checksum_free(sum);return result;
}
static gboolean unit_name(const char *name)
{ return g_regex_match_simple("^[A-Za-z0-9_]{1,8}\\.[cC]$",name,0,0); }
static char *object_name(const char *source)
{
    char *name=g_ascii_strup(source,-1);char *dot=strrchr(name,'.');*dot=0;
    char *result=g_strconcat(name,".OBJ",NULL);g_free(name);return result;
}
static const char *json_string(JsonObject *object,const char *name)
{
    JsonNode *node=object?json_object_get_member(object,name):NULL;
    return node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node)==G_TYPE_STRING?json_node_get_string(node):NULL;
}
static JsonObject *json_child(JsonObject *object,const char *name)
{
    JsonNode *node=object?json_object_get_member(object,name):NULL;
    return node && JSON_NODE_HOLDS_OBJECT(node)?json_node_get_object(node):NULL;
}
static JsonObject *cache_document(JsonParser *parser)
{
    JsonNode *node=json_parser_get_root(parser);
    JsonObject *root=node && JSON_NODE_HOLDS_OBJECT(node)?json_node_get_object(node):NULL;
    JsonNode *version=root?json_object_get_member(root,"version"):NULL;
    return version && JSON_NODE_HOLDS_VALUE(version) && json_node_get_value_type(version)==G_TYPE_INT64 &&
           json_node_get_int(version)==OBJECT_CACHE_VERSION?root:NULL;
}
static char *object_digest(const char *directory,const char *name,GError **error)
{
    char *path=file_at(directory,name),*bytes=NULL;gsize length=0;
    gboolean ok=safe_path(path,error) && g_file_get_contents(path,&bytes,&length,error);
    char *hash=ok && length?g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar*)bytes,length):NULL;
    if(ok && !length) failure(error,"Empty SDK object file.");
    g_free(path);g_free(bytes);return hash;
}
static gboolean write_cache(const char *directory,const char *name,JsonObject *root,GError **error)
{
    JsonNode *node=json_node_new(JSON_NODE_OBJECT);json_node_set_object(node,root);
    char *text=json_to_string(node,TRUE);gboolean ok=write_at(directory,name,text,-1,error);
    g_free(text);json_node_free(node);return ok;
}
static gboolean prepare_objects(const char *output,const char *sdk,gboolean diagnostics,GError **error)
{
    char *src=file_at(output,"src"),*objects=file_at(output,"obj"),*context=NULL,*sources=NULL;
    gboolean reusable=TRUE,ok=make_directory(objects,error);
    if(ok) context=object_context(sdk,diagnostics,&reusable,error);
    if(!context) ok=FALSE;
    GHashTable *files=ok?source_files(src,error):NULL;if(!files) ok=FALSE;
    if(ok && exists(src,"sources.rsp")) ok=read_at(src,"sources.rsp",&sources,NULL,error);
    else if(ok) {sources=g_strdup("main.c object.c camera.c psx.c card.c\n");ok=write_at(src,"sources.rsp",sources,-1,error);}
    JsonParser *previous=json_parser_new();char *receipt=file_at(objects,"cache.json");
    if(ok) ok=safe_path(receipt,error);
    JsonObject *old=ok && json_parser_load_from_file(previous,receipt,NULL)?cache_document(previous):NULL;
    JsonObject *old_units=json_child(old,"units"),*plan=json_object_new(),*units=json_object_new();
    json_object_set_int_member(plan,"version",OBJECT_CACHE_VERSION);json_object_set_object_member(plan,"units",units);
    GString *make=g_string_new(NULL),*link=g_string_new("");
    g_string_append_printf(make,"CFLAGS = %s\nLDFLAGS = -Xo$80010000\n\n16bit:\n",compile_flags(diagnostics));
    GHashTable *used=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
    char **names=g_strsplit_set(sources?sources:""," \t\r\n",-1);guint dirty=0,count=0;
    for(guint i=0;ok && names[i];i++) if(*names[i]) {
        const char *name=names[i];
        if(!unit_name(name) || json_object_has_member(units,name)) {ok=failure(error,"Invalid or duplicate compiler source in manifest.");break;}
        gboolean cacheable=reusable;char *hash=unit_fingerprint(src,name,files,context,&cacheable,error);
        if(!hash) {ok=FALSE;break;}
        char *obj=object_name(name);
        if(g_hash_table_contains(used,obj)) {g_free(hash);g_free(obj);ok=failure(error,"Compiler source names collide under DOS case folding.");break;}
        g_hash_table_add(used,g_strdup(obj));JsonObject *old_unit=json_child(old_units,name);
        const char *old_hash=json_string(old_unit,"fingerprint"),*old_object=json_string(old_unit,"object_hash");
        JsonNode *old_flag=old_unit?json_object_get_member(old_unit,"cacheable"):NULL;
        gboolean previously_cacheable=old_flag && JSON_NODE_HOLDS_VALUE(old_flag) &&
            json_node_get_value_type(old_flag)==G_TYPE_BOOLEAN && json_node_get_boolean(old_flag);
        char *actual=cacheable && previously_cacheable && old_hash && g_str_equal(old_hash,hash) && old_object?object_digest(objects,obj,NULL):NULL;
        gboolean hit=actual && g_str_equal(actual,old_object);g_free(actual);
        if(!hit) {
            char *path=file_at(objects,obj);ok=remove_tree(path,error);g_free(path);
            g_string_append_printf(make,"\tccpsx $(CFLAGS) -c %s -o..\\obj\\%s\n",name,obj);dirty++;
        }
        g_string_append_printf(link,"..\\obj\\%s\n",obj);
        JsonObject *unit=json_object_new();json_object_set_string_member(unit,"fingerprint",hash);
        json_object_set_boolean_member(unit,"cacheable",cacheable);json_object_set_object_member(units,name,unit);
        count++;g_free(hash);g_free(obj);
    }
    /* Old unit names must neither be linked nor accumulate after Scene removal. */
    GPtrArray *cached=ok?directory_names(objects,error):NULL;
    if(ok && !cached) ok=FALSE;
    for(guint i=0;ok && i<cached->len;i++) {
        const char *name=g_ptr_array_index(cached,i);
        if(g_regex_match_simple("^[A-Z0-9_]{1,8}\\.OBJ$",name,0,0) && !g_hash_table_contains(used,name)) {
            char *path=file_at(objects,name);ok=remove_tree(path,error);g_free(path);
        }
    }
    if(cached) g_ptr_array_unref(cached);
    g_hash_table_unref(used);
    g_string_append(make,"\tccpsx $(LDFLAGS) @objects.rsp -omain.cpe\n\tcpe2x /ce main.cpe\n\t..\\cdrom\\buildcd.exe -l -i..\\cdrom\\temp.img ..\\cdrom\\CONF.CTI\n\t..\\cdrom\\stripiso.exe s 2352 ..\\cdrom\\temp.img ..\\cdrom\\GAME.ISO\n");
    if(ok) ok=write_at(src,"objects.rsp",link->str,link->len,error) && write_at(src,"makefile.mak",make->str,make->len,error) &&
              write_cache(objects,"pending.json",plan,error);
    if(ok) g_print("Incremental compilation: %u of %u units require compilation (%u reused).\n",dirty,count,count-dirty);
    g_strfreev(names);g_free(src);g_free(objects);g_free(context);g_free(sources);g_free(receipt);
    if(files) g_hash_table_unref(files);
    g_string_free(make,TRUE);g_string_free(link,TRUE);json_object_unref(plan);g_object_unref(previous);return ok;
}
static gboolean commit_objects(const char *output,const char *sdk,gboolean diagnostics,GError **error)
{
    char *src=file_at(output,"src"),*objects=file_at(output,"obj"),*pending=file_at(objects,"pending.json");
    JsonParser *parser=json_parser_new();gboolean ok=safe_path(pending,error) && json_parser_load_from_file(parser,pending,error);
    JsonObject *root=ok?cache_document(parser):NULL,*units=json_child(root,"units");
    if(ok && !units) ok=failure(error,"Invalid pending object cache.");
    gboolean reusable=TRUE;char *context=ok?object_context(sdk,diagnostics,&reusable,error):NULL;
    if(!context) ok=FALSE;
    GHashTable *files=ok?source_files(src,error):NULL;if(!files) ok=FALSE;
    GList *names=units?json_object_get_members(units):NULL;
    for(GList *at=names;ok && at;at=at->next) {
        const char *name=at->data;JsonObject *unit=json_child(units,name);const char *expected=json_string(unit,"fingerprint");
        if(!unit_name(name) || !expected) {ok=failure(error,"Invalid pending compilation unit.");break;}
        gboolean cacheable=reusable;char *hash=unit_fingerprint(src,name,files,context,&cacheable,error);
        if(!hash) {ok=FALSE;break;}
        if(!g_str_equal(hash,expected)) ok=failure(error,"Compilation inputs changed during the SDK build; rebuild required.");
        g_free(hash);
        char *obj=object_name(name),*digest=ok?object_digest(objects,obj,error):NULL;g_free(obj);
        if(!digest) ok=FALSE;
        if(ok) {json_object_set_string_member(unit,"object_hash",digest);json_object_set_boolean_member(unit,"cacheable",cacheable);}
        g_free(digest);
    }
    if(ok) ok=write_cache(objects,"cache.json",root,error);
    g_list_free(names);if(files) g_hash_table_unref(files);
    g_free(context);g_free(src);g_free(objects);g_free(pending);g_object_unref(parser);return ok;
}

/* Publish bounded, UTF-8 diagnostics even when DOSBox exits before verification.
 * Compiler errors live in BUILD.LOG, not in DOSBox's host console output. */
static void print_failed_log(const char *output,const char *relative,const char *label,gboolean dos_encoding)
{
    const gsize limit=128*1024;
    char *path=file_at(output,relative);GFile *file=g_file_new_for_path(path);
    GFileInputStream *stream=g_file_read(file,NULL,NULL);g_object_unref(file);
    if(!stream) {g_free(path);return;}
    GFileInfo *info=g_file_input_stream_query_info(stream,G_FILE_ATTRIBUTE_STANDARD_SIZE,NULL,NULL);
    goffset size=info?g_file_info_get_size(info):0;if(info) g_object_unref(info);
    goffset offset=size>(goffset)limit?size-(goffset)limit:0;
    gboolean ok=!offset || g_seekable_seek(G_SEEKABLE(stream),offset,G_SEEK_SET,NULL,NULL);
    char *bytes=g_malloc(limit+1);gsize length=0;
    if(ok) ok=g_input_stream_read_all(G_INPUT_STREAM(stream),bytes,limit,&length,NULL,NULL);
    g_object_unref(stream);
    if(ok && length) {
        char *decoded=dos_encoding?g_convert(bytes,length,"UTF-8","CP437",NULL,NULL,NULL):NULL;
        if(!decoded) decoded=g_utf8_make_valid(bytes,length);
        GString *text=g_string_new(decoded);g_free(decoded);
        g_string_replace(text,"\r\n","\n",0);g_string_replace(text,"\r","\n",0);
        g_printerr("\n%s (%s):\n%s%s\n",label,path,offset?"[Showing the last 128 KiB]\n":"",text->str);
        g_string_free(text,TRUE);
    }
    g_free(bytes);g_free(path);
}

typedef struct {GMainLoop *loop;GSubprocess *process;GError *error;const char *log;gboolean timed_out,ok;} DosWait;
static void dos_finished(GObject *source,GAsyncResult *result,gpointer data)
{
    DosWait *wait=data;GBytes *bytes=NULL;
    wait->ok=g_subprocess_communicate_finish(G_SUBPROCESS(source),result,&bytes,NULL,&wait->error);
    if(bytes) {
        gsize length;const char *data_bytes=g_bytes_get_data(bytes,&length);
        if(wait->ok) wait->ok=g_file_set_contents(wait->log,data_bytes,length,&wait->error);
        else g_file_set_contents(wait->log,data_bytes,length,NULL);
        g_bytes_unref(bytes);
    }
    if(wait->ok) wait->ok=g_subprocess_wait_check(wait->process,NULL,&wait->error);
    g_main_loop_quit(wait->loop);
}
static gboolean dos_timeout(gpointer data)
{DosWait *wait=data;wait->timed_out=TRUE;g_subprocess_force_exit(wait->process);return G_SOURCE_REMOVE;}
static gboolean run_dosbox(const char *output,GError **error)
{
    char *config=file_at(output,"dos.conf"),*log=file_at(output,"dosbox.log");
    char *program=g_find_program_in_path("dosbox-x");
    if(!program) {
        g_free(config);g_free(log);
        return failure(error,"DOSBox-X was not found. Install dosbox-x and make it available in PATH.");
    }
    const char *args[]={program,"-defaultconf","-conf",config,NULL};
    g_print("DOSBox-X: %s (single build session)\n",program);fflush(stdout);
    GSubprocessLauncher *launcher=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher,output);
    GSubprocess *process=g_subprocess_launcher_spawnv(launcher,args,error);
    g_object_unref(launcher);g_free(program);g_free(config);
    if(!process) {g_free(log);return FALSE;}
    DosWait wait={g_main_loop_new(NULL,FALSE),process,NULL,log,FALSE,FALSE};
    guint timer=g_timeout_add_seconds(600,dos_timeout,&wait);
    g_subprocess_communicate_async(process,NULL,NULL,dos_finished,&wait);g_main_loop_run(wait.loop);
    if(!wait.timed_out) g_source_remove(timer);
    g_main_loop_unref(wait.loop);g_object_unref(process);
    if(wait.timed_out) {g_clear_error(&wait.error);g_free(log);return failure(error,"DOSBox-X exceeded the 600 second build timeout.");}
    if(wait.error) {g_prefix_error(&wait.error,"DOSBox-X failed (see %s): ",log);g_propagate_error(error,wait.error);}
    g_free(log);return wait.ok;
}

char *engine_install_root(const char *executable)
{
    char *directory=g_path_get_dirname(executable),*marker=file_at(directory,"pipeline/template/CONF.CTI");
    gboolean local=g_file_test(marker,G_FILE_TEST_IS_REGULAR) || exists(directory,"psx-lib/pipeline/template/CONF.CTI");
    char *root=local?g_strdup(directory):g_path_get_dirname(directory);
    g_free(marker);g_free(directory);return root;
}

int engine_build_main(int argc,char **argv)
{
    char *psyq_arg=NULL,*output_arg=NULL,*verify_arg=NULL,*root_arg=NULL,*build_arg=NULL,*define_arg=NULL,*name_arg=NULL,*resources_arg=NULL,*assets_arg=NULL,*source_arg=NULL;
    int source_objects=64,source_triangles=1024;
    gboolean prepare=FALSE,diagnostics=FALSE;
    GOptionEntry options[]={
        {"define",0,0,G_OPTION_ARG_STRING,&define_arg,"Set NAME=integer in the generated psxdefs.h for C projects","NAME=VALUE"},
        {"name",0,0,G_OPTION_ARG_STRING,&name_arg,"Application/output name for C source builds","NAME"},
        {"resources",0,0,G_OPTION_ARG_FILENAME,&resources_arg,"Optional C asset list for --source-root","HEADER"},
        {"asset-root",0,0,G_OPTION_ARG_FILENAME,&assets_arg,"Conversion input folder for --resources","DIR"},
        {"source-root",0,0,G_OPTION_ARG_FILENAME,&source_arg,"Build a C application directory containing main.c","DIR"},
        {"max-objects",0,0,G_OPTION_ARG_INT,&source_objects,"Object capacity for C source projects (1..512)","N"},
        {"render-triangles",0,0,G_OPTION_ARG_INT,&source_triangles,"Triangle packet capacity for C source projects (256..8192)","N"},
        {"psyq",0,0,G_OPTION_ARG_FILENAME,&psyq_arg,"Psy-Q SDK containing psyq/, include/, lib/","PATH"},
        {"output-root",0,0,G_OPTION_ARG_FILENAME,&output_arg,"Override the build output root","PATH"},
        {"verify",0,0,G_OPTION_ARG_FILENAME,&verify_arg,"Verify an existing PS1 build","PATH"},
        {"prepare-only",0,0,G_OPTION_ARG_NONE,&prepare,"Prepare sources without starting DOSBox",NULL},
        {"diagnostics",0,0,G_OPTION_ARG_NONE,&diagnostics,"Enable toolchain diagnostics",NULL},
        {"build-directory",0,0,G_OPTION_ARG_FILENAME,&build_arg,"Generated build directory (independent of engine resources)","PATH"},
        {"root",0,0,G_OPTION_ARG_FILENAME,&root_arg,"Override the installed resource root","PATH"},
        {NULL,0,0,0,NULL,NULL,NULL}
    };
    GError *error=NULL;GOptionContext *context=g_option_context_new("-- native PS1 build");
    g_option_context_add_main_entries(context,options,NULL);
    /* Remove the build dispatcher flag before parsing its arguments. */
    for(int i=1;i<argc;i++) if(!strcmp(argv[i],"--build-ps1")) {for(int j=i;j<argc;j++) argv[j]=argv[j+1];argc--;break;}
    gboolean ok=g_option_context_parse(context,&argc,&argv,&error);g_option_context_free(context);
    BuildLock lock=NO_LOCK;
    char *root=NULL,*output=NULL,*sdk=NULL,*build=NULL,*latest=NULL,*source_path=NULL;
    if(!ok) goto done;
    if(argc!=1) {ok=failure(&error,"Unexpected build argument.");goto done;}
    if((resources_arg || assets_arg) && (!source_arg || !resources_arg || !assets_arg)) {
        ok=failure(&error,"--resources and --asset-root require --source-root and each other.");goto done;
    }
    if(define_arg && (!source_arg || !g_regex_match_simple("^[A-Za-z_][A-Za-z0-9_]{0,47}=[0-9]{1,9}$",define_arg,0,0))) {
        ok=failure(&error,"--define requires --source-root and NAME=unsigned_decimal_integer.");goto done;
    }
    if(name_arg && (!source_arg || !g_regex_match_simple("^[A-Za-z0-9_-]{1,32}$",name_arg,0,0))) {ok=failure(&error,"--name requires --source-root and 1..32 ASCII letters/digits/_/-.");goto done;}
    if(source_arg && (source_objects<1 || source_objects>512 || source_triangles<256 || source_triangles>8192)) {
        ok=failure(&error,"Invalid C source runtime capacities.");goto done;
    }
    if(verify_arg) {ok=verify_build(verify_arg,&error);goto done;}
    if(!source_arg) {ok=failure(&error,"--source-root is required to build a C application.");goto done;}
    if(source_arg) {
        source_path=g_canonicalize_filename(source_arg,NULL);
        if(!(ok=safe_path(source_path,&error)) || !g_file_test(source_path,G_FILE_TEST_IS_DIR)) {
            if(ok) ok=failure(&error,"--source-root must name a C source directory.");
            goto done;
        }
    }
    char *executable=g_file_read_link("/proc/self/exe",NULL);
    if(!executable) executable=g_find_program_in_path(argv[0]);
    if(!executable) {ok=failure(&error,"Cannot locate the engine executable.");goto done;}
    root=root_arg?g_canonicalize_filename(root_arg,NULL):engine_install_root(executable);g_free(executable);
    const char *requested=psyq_arg;
    if(!requested || !*requested) {ok=failure(&error,"--psyq is required to build.");goto done;}
    sdk=g_canonicalize_filename(requested,NULL);g_print("Psy-Q SDK: %s\n",sdk);fflush(stdout);
    const char *sdk_files[]={"psyq/CCPSX.EXE","psyq/PSYMAKE.EXE","include","lib"};
    for(guint i=0;i<G_N_ELEMENTS(sdk_files);i++) {
        char *lower=g_ascii_strdown(sdk_files[i],-1);gboolean found=exists(sdk,sdk_files[i]) || exists(sdk,lower);g_free(lower);
        if(!found) {g_set_error(&error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"Invalid Psy-Q SDK: missing %s",sdk_files[i]);ok=FALSE;goto done;}
    }
    build=build_arg?g_canonicalize_filename(build_arg,NULL):file_at(root,"build");if(!(ok=make_directory(build,&error))) goto done;
    latest=file_at(build,"last-ps1-build.txt");
    if(!(ok=safe_path(latest,&error))) goto done;
    char *stem=name_arg?g_strdup(name_arg):g_path_get_basename(source_path);
    GRegex *pattern=g_regex_new("[^a-zA-Z0-9_-]",0,0,NULL);char *safe=g_regex_replace_literal(pattern,stem,-1,0,"_",0,NULL),*slug=g_strndup(safe,32);
    g_regex_unref(pattern);g_free(safe);g_free(stem);
    char *base;
    if(output_arg) base=g_canonicalize_filename(output_arg,NULL);
    else base=file_at(build,"ps1");
    if(output_arg) {
        const char *identity=source_path;
        char *hash=identity?g_compute_checksum_for_string(G_CHECKSUM_SHA256,identity,-1):g_strdup("probe");
        char *name=g_strdup_printf("%s-%.10s",slug,hash);output=file_at(base,name);g_free(hash);g_free(name);
    } else output=file_at(base,slug);
    g_free(base);g_free(slug);
    if(source_path) {
        char *prefix=g_strconcat(output,G_DIR_SEPARATOR_S,NULL);
        gboolean overlap=!strcmp(source_path,output) || g_str_has_prefix(source_path,prefix);
        g_free(prefix);if(overlap) {ok=failure(&error,"C sources cannot be inside their managed output directory.");goto done;}
    }
    if(!(ok=make_directory(output,&error) && acquire_lock(output,&lock,&error))) goto done;
    const char *owner=source_path;char *marker=NULL;
    if(exists(output,".psx-studio-build")) {
        ok=read_at(output,".psx-studio-build",&marker,NULL,&error);
        if(ok && strcmp(marker,owner)) ok=failure(&error,"This build folder belongs to another project; use --output-root to keep builds separate.");
        g_free(marker);marker=NULL;
    } else {
        GDir *directory=g_dir_open(output,0,&error);ok=directory!=NULL;const char *name;
        while(ok && (name=g_dir_read_name(directory))) if(strcmp(name,".build.lock")) ok=failure(&error,"The output directory is not a build managed by PSX Lib.");
        if(directory) g_dir_close(directory);
    }
    if(!ok || !(ok=write_at(output,".psx-studio-build",owner,-1,&error))) goto done;
    const char *dirs[]={"src","cdrom","data","TEMP","obj"};
    for(guint i=0;ok && i<G_N_ELEMENTS(dirs);i++) {char *path=file_at(output,dirs[i]);ok=safe_path(path,&error);g_free(path);}
    if(!ok) goto done;
    const char *artifacts[]={"SUCCESS.OK","FAILED.TXT","src/MAIN.EXE","src/MAIN.CPE","src/BUILD.LOG","dosbox.log","cdrom/GAME.ISO","cdrom/game.cue","cdrom/TEMP.IMG","build-info.json"};
    for(guint i=0;ok && i<G_N_ELEMENTS(artifacts);i++) {char *path=file_at(output,artifacts[i]);ok=remove_tree(path,&error);g_free(path);}
    if(g_file_get_contents(latest,&marker,NULL,NULL)) {if(!strcmp(g_strstrip(marker),output)) g_unlink(latest);g_free(marker);marker=NULL;}
    char *src=file_at(output,"src"),*cdrom=file_at(output,"cdrom");
    char *resources=exists(root,"psx-lib/pipeline/template/CONF.CTI")?file_at(root,"psx-lib"):g_strdup(root);
    /* The marker owns generated sources; clear obsolete application units. */
    if(ok) ok=remove_tree(src,&error) && copy_subtree(resources,"pipeline/template/src",src,&error) && copy_subtree(resources,"pipeline/cdrom",cdrom,&error);
    char *a=file_at(resources,"pipeline/template/CONF.CTI"),*b=file_at(cdrom,"CONF.CTI");if(ok) ok=copy_file(a,b,&error);g_free(a);g_free(b);
    const char *runtime[]={"object.c","object.h","camera.c","camera.h","fps.h","render.h","timing.h","frame.h","save.h"};
    char *runtime_dir=file_at(resources,"runtime");for(guint i=0;ok && i<G_N_ELEMENTS(runtime);i++) ok=copy_relative(runtime_dir,src,runtime[i],&error);g_free(runtime_dir);g_free(resources);
    if(ok) {char *text=NULL;ok=read_at(src,"camera.h",&text,NULL,&error);if(ok) {char *fixed=g_strconcat("#define PSX_RUNTIME_FIXED_ONLY 1\n",text,NULL);ok=write_at(src,"camera.h",fixed,-1,&error);g_free(fixed);}g_free(text);}
    char *temporary=file_at(output,"TEMP"),*data=file_at(output,"data");if(ok) ok=make_directory(temporary,&error) && make_directory(data,&error);g_free(temporary);g_free(data);g_free(src);g_free(cdrom);
    if(ok) ok=write_at(output,"texture.log","",0,&error);
    g_print("Build output: %s\n",output);fflush(stdout);
    if(ok) ok=prepare_application(output,source_path,source_objects,source_triangles,resources_arg,assets_arg,diagnostics,&error);
    if(ok && source_path) {
        GString *header=g_string_new("#ifndef PSX_BUILD_DEFS_H\n#define PSX_BUILD_DEFS_H\n");
        if(define_arg) {
            char **parts=g_strsplit(define_arg,"=",2);
            g_string_append_printf(header,"#define %s %" G_GUINT64_FORMAT "\n",parts[0],g_ascii_strtoull(parts[1],NULL,10));
            g_strfreev(parts);
        }
        g_string_append(header,"#endif\n");ok=write_at(output,"src/psxdefs.h",header->str,-1,&error);g_string_free(header,TRUE);
    }
    if(ok) ok=normalize_sources(output,&error);
    if(ok) ok=prepare_objects(output,sdk,diagnostics,&error);
    if(ok) ok=normalize_sources(output,&error);
    const char *batch="@echo off\nset PATH=D:\\psyq\nset PSX_PATH=D:\\psyq\nset PSYQ_PATH=D:\\psyq\nset COMPILER_PATH=D:\\psyq\nset LIBRARY_PATH=D:\\lib\nset C_INCLUDE_PATH=D:\\include\nset C_PLUS_INCLUDE_PATH=D:\\include\nset GO32=DPMISTACK 1000000\nset GO32TMP=C:\\TEMP\nset TMPDIR=C:\\TEMP\nset TEMP=C:\\TEMP\nset TMP=C:\\TEMP\ncd \\src\npsymake 16bit > BUILD.LOG\nif errorlevel 1 goto failed\nif not exist MAIN.EXE goto failed\nif not exist ..\\cdrom\\GAME.ISO goto failed\necho success > C:\\SUCCESS.OK\ngoto done\n:failed\necho failed > C:\\FAILED.TXT\n:done\ncd \\\n";
    if(ok) ok=write_at(output,"BUILD.BAT",batch,-1,&error);
    if(ok) {char *path=file_at(output,"BUILD.BAT");ok=dos_text(path,&error);g_free(path);}
    if(ok && (strpbrk(output,"\"\r\n") || strpbrk(sdk,"\"\r\n"))) ok=failure(&error,"DOSBox paths cannot contain quotes or newlines.");
    if(ok) {
        char *config=g_strdup_printf("[sdl]\noutput=surface\n[dosbox]\nmemsize=64\nprivate area size=131072\n[dos]\nems=false\n[cpu]\ncore=dynamic\ncputype=386\ncycles=fixed 200000\n[autoexec]\nmount c \"%s\"\nmount d \"%s\" -ro\nc:\ncall BUILD.BAT\nexit\n",output,sdk);
        ok=write_at(output,"dos.conf",config,-1,&error);g_free(config);
    }
    if(ok && !prepare) {
        ok=run_dosbox(output,&error);
        if(!ok) {
            print_failed_log(output,"dosbox.log","DOSBox-X log",FALSE);
            print_failed_log(output,"src/BUILD.LOG","Psy-Q compiler log",TRUE);
        } else ok=verify_build(output,&error);
    }
    if(ok && !prepare) {
        ok=commit_objects(output,sdk,diagnostics,&error);
        if(!ok) {char *success=file_at(output,"SUCCESS.OK");g_unlink(success);g_free(success);}
    }
    if(ok && !prepare) {
        JsonObject *info=json_object_new();json_object_set_int_member(info,"version",1);
        if(source_path) json_object_set_string_member(info,"source_root",source_path);
        json_object_set_string_member(info,"psyq_path",sdk);JsonNode *node=json_node_new(JSON_NODE_OBJECT);json_node_take_object(node,info);
        char *text=json_to_string(node,TRUE);ok=write_at(output,"build-info.json",text,-1,&error);g_free(text);json_node_free(node);
        if(ok) {text=g_strconcat(output,"\n",NULL);ok=g_file_set_contents(latest,text,-1,&error);g_free(text);}
    }
done:
    if(!ok) g_printerr("PS1 build failed: %s\n",error?error->message:"Unknown error");
    g_clear_error(&error);release_lock(lock);
    g_free(root);g_free(output);g_free(sdk);g_free(build);g_free(latest);g_free(source_path);
    g_free(psyq_arg);g_free(output_arg);g_free(verify_arg);g_free(root_arg);g_free(build_arg);g_free(source_arg);g_free(resources_arg);g_free(name_arg);g_free(define_arg);g_free(assets_arg);
    return ok?0:1;
}
