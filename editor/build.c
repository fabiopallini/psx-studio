/* Desktop build orchestration. No Python or shell is used for game builds. */
#include "studio.h"
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
                   !g_regex_match_simple("^T[0-9]{3}\\.TIM$",json_node_get_string(file),0,0))
                    ok=failure(error,"Invalid texture filename in manifest.");
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
                const char *name=json_object_get_string_member(json_array_get_object_element(textures,i),"file");
                char *discname=g_strconcat("ASSETS/",name,";1",NULL),*host=g_build_filename(output,"data",name,NULL);
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
        if(g_str_has_suffix(lower,".c") || g_str_has_suffix(lower,".h") || g_str_has_suffix(lower,".mak")) {
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
static gboolean snapshot_assets(const Project *project,const char *original,const char *data,GError **error)
{
    gboolean ok=TRUE;
    for(guint scene=0;ok && scene<project->scenes->len;scene++) {
        Project view;ok=project_scene_view(project,scene,&view,error);
        for(guint i=0;ok && i<view.objects->len;i++) {
            Object *object=g_ptr_array_index(view.objects,i);
            const char *refs[]={object->asset,object->texture,object->module};
            for(guint j=0;ok && j<G_N_ELEMENTS(refs);j++) if(*refs[j]) {
                char *source=file_at(original,refs[j]),*destination=file_at(data,refs[j]),*parent=g_path_get_dirname(destination);
                ok=make_directory(parent,error) && copy_file(source,destination,error);
                g_free(source);g_free(destination);g_free(parent);
            }
        }
        project_clear(&view);
    }
    if(ok && g_file_test(original,G_FILE_TEST_IS_DIR)) {
        GDir *directory=g_dir_open(original,0,error);ok=directory!=NULL;const char *name;
        while(ok && (name=g_dir_read_name(directory))) {
            char *lower=g_ascii_strdown(name,-1);
            if(g_str_has_suffix(lower,".vag")) ok=copy_relative(original,data,name,error);
            g_free(lower);
        }
        if(directory) g_dir_close(directory);
    }
    return ok;
}

static gboolean prepare_project(Project *project,const char *root,const char *output,const char *exporter,
                                gboolean diagnostics,GError **error)
{
    char *src=file_at(output,"src"),*data=file_at(output,"data"),*original=project_data_path(project->path);
    gboolean ok=remove_tree(data,error) && make_directory(data,error);
    if(ok) ok=snapshot_assets(project,original,data,error);
    if(ok) {
        char *a=file_at(root,"pipeline/project-main.c"),*b=file_at(src,"main.c");ok=copy_file(a,b,error);g_free(a);g_free(b);
    }
    if(ok) {
        g_free(project->asset_root);project->asset_root=g_strdup(data);
        if(exporter) {
            char *snapshot=file_at(output,"project.snapshot.psx");const char *args[]={exporter,"--export",snapshot,"--output",src,"--asset-root",data,NULL};
            GSubprocess *process=g_subprocess_newv(args,G_SUBPROCESS_FLAGS_NONE,error);
            ok=process && g_subprocess_wait_check(process,NULL,error);if(process) g_object_unref(process);g_free(snapshot);
        } else ok=project_export(project,src,error);
    }
    JsonParser *parser=ok?read_manifest(output,error):NULL;if(ok && !parser) ok=FALSE;
    if(parser) {
        JsonObject *manifest=json_node_get_object(json_parser_get_root(parser));
        JsonArray *modules=json_object_get_array_member(manifest,"modules"),*textures=json_object_get_array_member(manifest,"textures");
        GString *sources=g_string_new("main.c scene.c object.c");
        for(guint i=0;i<json_array_get_length(modules);i++) {g_string_append_c(sources,' ');g_string_append(sources,json_array_get_string_element(modules,i));}
        if(ok) ok=rewrite(output,"src/makefile.mak","main.c object.c",sources->str,error);
        g_string_free(sources,TRUE);
        if(ok) ok=rewrite(output,"src/makefile.mak","-pedantic",diagnostics?"-pedantic-errors -DSTUDIO_DIAGNOSTICS":"-pedantic-errors",error);
        GString *entries=g_string_new("                    Directory ASSETS\n"),*log=g_string_new(NULL);
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
            g_string_append_printf(entries,"                        File %s\n                            XAFileAttributes Form1 Data\n                            Source [root]\\data\\%s\n                        EndFile\n",name,name);
            g_string_append_printf(log,"Native TIM: %s %ux%u VRAM (%u,%u)\n",name,width,height,
                (guint)json_object_get_int_member(texture,"x"),(guint)json_object_get_int_member(texture,"y"));
        }
        g_string_append(entries,"                    EndDirectory\n                EndHierarchy");
        if(ok && json_array_get_length(textures)) ok=rewrite(output,"cdrom/CONF.CTI","                EndHierarchy",entries->str,error);
        if(ok) ok=write_at(output,"texture.log",log->str,log->len,error);
        g_string_free(entries,TRUE);g_string_free(log,TRUE);g_object_unref(parser);
    }
    g_free(original);g_free(src);g_free(data);return ok;
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
static gboolean run_dosbox(const Project *project,const char *sdk,const char *output,GError **error)
{
    char *config=file_at(output,"dos.conf"),*log=file_at(output,"dosbox.log"),*program=NULL;
    if(project && *project->dosbox) program=g_strdup(project->dosbox);
    else if(!(program=g_find_program_in_path("dosbox-x"))) program=g_find_program_in_path("dosbox");
    GPtrArray *args=g_ptr_array_new_with_free_func(g_free);
    if(program) {g_ptr_array_add(args,program);g_ptr_array_add(args,g_strdup("-conf"));g_ptr_array_add(args,g_strdup(config));}
    else if((program=g_find_program_in_path("flatpak"))) {
        g_ptr_array_add(args,program);g_ptr_array_add(args,g_strdup("run"));
        g_ptr_array_add(args,g_strdup_printf("--filesystem=%s",output));g_ptr_array_add(args,g_strdup_printf("--filesystem=%s:ro",sdk));
        g_ptr_array_add(args,g_strdup("com.dosbox_x.DOSBox-X"));g_ptr_array_add(args,g_strdup("-conf"));g_ptr_array_add(args,g_strdup(config));
    } else {g_ptr_array_unref(args);g_free(config);g_free(log);return failure(error,"Install DOSBox-X or DOSBox, or configure its executable in Settings.");}
    g_ptr_array_add(args,NULL);GSubprocessLauncher *launcher=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_MERGE);
    GSubprocess *process=g_subprocess_launcher_spawnv(launcher,(const char *const*)args->pdata,error);
    g_object_unref(launcher);g_ptr_array_unref(args);g_free(config);if(!process) {g_free(log);return FALSE;}
    DosWait wait={g_main_loop_new(NULL,FALSE),process,NULL,log,FALSE,FALSE};guint timer=g_timeout_add_seconds(240,dos_timeout,&wait);
    g_subprocess_communicate_async(process,NULL,NULL,dos_finished,&wait);g_main_loop_run(wait.loop);
    if(!wait.timed_out) g_source_remove(timer);
    g_main_loop_unref(wait.loop);g_object_unref(process);g_free(log);
    if(wait.timed_out) {g_clear_error(&wait.error);return failure(error,"DOSBox exceeded the 240 second build timeout.");}
    if(wait.error) g_propagate_error(error,wait.error);
    return wait.ok;
}

char *studio_install_root(const char *executable)
{
    char *directory=g_path_get_dirname(executable),*marker=file_at(directory,"pipeline/template/CONF.CTI");
    char *root= g_file_test(marker,G_FILE_TEST_IS_REGULAR)?g_strdup(directory):g_path_get_dirname(directory);
    g_free(marker);g_free(directory);return root;
}

int studio_build_main(int argc,char **argv)
{
    char *project_arg=NULL,*psyq_arg=NULL,*output_arg=NULL,*verify_arg=NULL,*exporter=NULL,*root_arg=NULL;
    gboolean prepare=FALSE,diagnostics=FALSE;
    GOptionEntry options[]={
        {"project",0,0,G_OPTION_ARG_FILENAME,&project_arg,"Saved project (omit for toolchain probe)","FILE"},
        {"psyq",0,0,G_OPTION_ARG_FILENAME,&psyq_arg,"Psy-Q SDK containing psyq/, include/, lib/","PATH"},
        {"output-root",0,0,G_OPTION_ARG_FILENAME,&output_arg,"Override the build output root","PATH"},
        {"verify",0,0,G_OPTION_ARG_FILENAME,&verify_arg,"Verify an existing PS1 build","PATH"},
        {"prepare-only",0,0,G_OPTION_ARG_NONE,&prepare,"Prepare sources without starting DOSBox",NULL},
        {"diagnostics",0,0,G_OPTION_ARG_NONE,&diagnostics,"Enable toolchain diagnostics",NULL},
        {"exporter",0,0,G_OPTION_ARG_FILENAME,&exporter,"Optional external exporter executable","FILE"},
        {"root",0,0,G_OPTION_ARG_FILENAME,&root_arg,"Override the installed resource root","PATH"},
        {NULL,0,0,0,NULL,NULL,NULL}
    };
    GError *error=NULL;GOptionContext *context=g_option_context_new("-- native PS1 build");
    g_option_context_add_main_entries(context,options,NULL);
    /* argv[0] is the editor executable; remove the dispatcher flag. */
    for(int i=1;i<argc;i++) if(!strcmp(argv[i],"--build-ps1")) {for(int j=i;j<argc;j++) argv[j]=argv[j+1];argc--;break;}
    gboolean ok=g_option_context_parse(context,&argc,&argv,&error);g_option_context_free(context);
    Project project;project_init(&project);BuildLock lock=NO_LOCK;
    char *root=NULL,*output=NULL,*sdk=NULL,*project_path=NULL,*snapshot=NULL,*build=NULL,*latest=NULL;
    if(!ok) goto done;
    if(argc!=1) {ok=failure(&error,"Unexpected build argument.");goto done;}
    if(verify_arg) {ok=verify_build(verify_arg,&error);goto done;}
    char *executable=g_file_read_link("/proc/self/exe",NULL);
    if(!executable) executable=g_find_program_in_path(argv[0]);
    if(!executable) {ok=failure(&error,"Cannot locate the editor executable.");goto done;}
    root=root_arg?g_canonicalize_filename(root_arg,NULL):studio_install_root(executable);g_free(executable);
    if(project_arg) {
        project_path=g_canonicalize_filename(project_arg,NULL);
        if(!(ok=g_file_get_contents(project_path,&snapshot,NULL,&error) && project_load(&project,project_path,&error))) goto done;
    }
    const char *requested=psyq_arg?psyq_arg:project.psyq_path;
    if(!requested || !*requested) {ok=failure(&error,"--psyq is required to build (or configure the SDK in the project).");goto done;}
    sdk=g_canonicalize_filename(requested,NULL);g_print("Psy-Q SDK: %s\n",sdk);fflush(stdout);
    const char *sdk_files[]={"psyq/CCPSX.EXE","psyq/PSYMAKE.EXE","include","lib"};
    for(guint i=0;i<G_N_ELEMENTS(sdk_files);i++) {
        char *lower=g_ascii_strdown(sdk_files[i],-1);gboolean found=exists(sdk,sdk_files[i]) || exists(sdk,lower);g_free(lower);
        if(!found) {g_set_error(&error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"Invalid Psy-Q SDK: missing %s",sdk_files[i]);ok=FALSE;goto done;}
    }
    build=file_at(root,"build");if(!(ok=make_directory(build,&error))) goto done;
    latest=file_at(build,"last-ps1-build.txt");
    if(!(ok=safe_path(latest,&error))) goto done;
    char *stem=project_path?g_path_get_basename(project_path):g_strdup("toolchain");char *extension=strrchr(stem,'.');if(extension && extension!=stem) *extension=0;
    GRegex *pattern=g_regex_new("[^a-zA-Z0-9_-]",0,0,NULL);char *safe=g_regex_replace_literal(pattern,stem,-1,0,"_",0,NULL),*slug=g_strndup(safe,32);
    g_regex_unref(pattern);g_free(safe);g_free(stem);
    char *base;
    if(output_arg) base=g_canonicalize_filename(output_arg,NULL);
    else if(*project.output_path) {char *parent=g_path_get_dirname(project_path);base=g_canonicalize_filename(project.output_path,parent);g_free(parent);}
    else base=file_at(build,"ps1");
    if(output_arg || *project.output_path) {
        char *hash=project_path?g_compute_checksum_for_string(G_CHECKSUM_SHA256,project_path,-1):g_strdup("probe");
        char *name=g_strdup_printf("%s-%.10s",slug,hash);output=file_at(base,name);g_free(hash);g_free(name);
    } else output=file_at(base,slug);
    g_free(base);g_free(slug);
    if(!(ok=make_directory(output,&error) && acquire_lock(output,&lock,&error))) goto done;
    const char *owner=project_path?project_path:"toolchain probe";char *marker=NULL;
    if(exists(output,".psx-studio-build")) {
        ok=read_at(output,".psx-studio-build",&marker,NULL,&error);
        if(ok && strcmp(marker,owner)) ok=failure(&error,"This build folder belongs to another project; use --output-root to keep builds separate.");
        g_free(marker);marker=NULL;
    } else {
        GDir *directory=g_dir_open(output,0,&error);ok=directory!=NULL;const char *name;
        while(ok && (name=g_dir_read_name(directory))) if(strcmp(name,".build.lock")) ok=failure(&error,"The output directory is not a build managed by PSX Studio.");
        if(directory) g_dir_close(directory);
    }
    if(!ok || !(ok=write_at(output,".psx-studio-build",owner,-1,&error))) goto done;
    const char *dirs[]={"src","cdrom","data","TEMP"};
    for(guint i=0;ok && i<G_N_ELEMENTS(dirs);i++) {char *path=file_at(output,dirs[i]);ok=safe_path(path,&error);g_free(path);}
    if(!ok) goto done;
    const char *artifacts[]={"SUCCESS.OK","FAILED.TXT","src/MAIN.EXE","src/MAIN.CPE","src/BUILD.LOG","cdrom/GAME.ISO","cdrom/game.cue","cdrom/TEMP.IMG","build-info.json"};
    for(guint i=0;ok && i<G_N_ELEMENTS(artifacts);i++) {char *path=file_at(output,artifacts[i]);ok=remove_tree(path,&error);g_free(path);}
    if(g_file_get_contents(latest,&marker,NULL,NULL)) {if(!strcmp(g_strstrip(marker),output)) g_unlink(latest);g_free(marker);marker=NULL;}
    char *src=file_at(output,"src"),*cdrom=file_at(output,"cdrom");
    /* The marker owns generated sources; clear them so removed Scenes/modules cannot survive. */
    if(ok) ok=remove_tree(src,&error) && copy_subtree(root,"pipeline/template/src",src,&error) && copy_subtree(root,"pipeline/cdrom",cdrom,&error);
    char *a=file_at(root,"pipeline/template/CONF.CTI"),*b=file_at(cdrom,"CONF.CTI");if(ok) ok=copy_file(a,b,&error);g_free(a);g_free(b);
    const char *runtime[]={"object.c","object.h","camera.c","camera.h","fps.h","render.h","timing.h","frame.h","save.h"};
    char *runtime_dir=file_at(root,"runtime");for(guint i=0;ok && i<G_N_ELEMENTS(runtime);i++) ok=copy_relative(runtime_dir,src,runtime[i],&error);g_free(runtime_dir);
    if(ok) {char *text=NULL;ok=read_at(src,"camera.h",&text,NULL,&error);if(ok) {char *fixed=g_strconcat("#define PSX_RUNTIME_FIXED_ONLY 1\n",text,NULL);ok=write_at(src,"camera.h",fixed,-1,&error);g_free(fixed);}g_free(text);}
    char *temporary=file_at(output,"TEMP"),*data=file_at(output,"data");if(ok) ok=make_directory(temporary,&error) && make_directory(data,&error);g_free(temporary);g_free(data);g_free(src);g_free(cdrom);
    if(ok) ok=write_at(output,"texture.log","",0,&error);
    g_print("Build output: %s\n",output);fflush(stdout);
    if(ok && project_path) ok=write_at(output,"project.snapshot.psx",snapshot,-1,&error) && prepare_project(&project,root,output,exporter,diagnostics,&error);
    if(ok) ok=normalize_sources(output,&error);
    const char *batch="@echo off\nset PATH=D:\\psyq\nset PSX_PATH=D:\\psyq\nset PSYQ_PATH=D:\\psyq\nset COMPILER_PATH=D:\\psyq\nset LIBRARY_PATH=D:\\lib\nset C_INCLUDE_PATH=D:\\include\nset C_PLUS_INCLUDE_PATH=D:\\include\nset GO32=DPMISTACK 1000000\nset GO32TMP=C:\\TEMP\nset TMPDIR=C:\\TEMP\nset TEMP=C:\\TEMP\nset TMP=C:\\TEMP\ncd \\src\npsymake 16bit > BUILD.LOG\nif errorlevel 1 goto failed\nif not exist MAIN.EXE goto failed\nif not exist ..\\cdrom\\GAME.ISO goto failed\necho success > C:\\SUCCESS.OK\ngoto done\n:failed\necho failed > C:\\FAILED.TXT\n:done\ncd \\\n";
    if(ok) ok=write_at(output,"BUILD.BAT",batch,-1,&error);
    if(ok) {char *path=file_at(output,"BUILD.BAT");ok=dos_text(path,&error);g_free(path);}
    if(ok && (strpbrk(output,"\"\r\n") || strpbrk(sdk,"\"\r\n"))) ok=failure(&error,"DOSBox paths cannot contain quotes or newlines.");
    if(ok) {
        char *config=g_strdup_printf("[sdl]\noutput=surface\n[dosbox]\nmemsize=64\n[cpu]\ncore=dynamic\ncputype=386\ncycles=fixed 200000\n[autoexec]\nmount c \"%s\"\nmount d \"%s\" -ro\nc:\ncall BUILD.BAT\nexit\n",output,sdk);
        ok=write_at(output,"dos.conf",config,-1,&error);g_free(config);
    }
    if(ok && !prepare) ok=run_dosbox(project_path?&project:NULL,sdk,output,&error) && verify_build(output,&error);
    if(ok && !prepare) {
        JsonObject *info=json_object_new();json_object_set_int_member(info,"version",1);
        if(project_path) json_object_set_string_member(info,"project_path",project_path);else json_object_set_null_member(info,"project_path");
        json_object_set_string_member(info,"psyq_path",sdk);JsonNode *node=json_node_new(JSON_NODE_OBJECT);json_node_take_object(node,info);
        char *text=json_to_string(node,TRUE);ok=write_at(output,"build-info.json",text,-1,&error);g_free(text);json_node_free(node);
        if(ok) {text=g_strconcat(output,"\n",NULL);ok=g_file_set_contents(latest,text,-1,&error);g_free(text);}
    }
done:
    if(!ok) g_printerr("PS1 build failed: %s\n",error?error->message:"Unknown error");
    g_clear_error(&error);release_lock(lock);project_clear(&project);
    g_free(root);g_free(output);g_free(sdk);g_free(project_path);g_free(snapshot);g_free(build);g_free(latest);
    g_free(project_arg);g_free(psyq_arg);g_free(output_arg);g_free(verify_arg);g_free(exporter);g_free(root_arg);
    return ok?0:1;
}
