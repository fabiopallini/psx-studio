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


char *studio_install_root(const char *executable)
{
    char *directory=g_path_get_dirname(executable);
    char *marker=file_at(directory,"spec.md");
    char *root=g_file_test(marker,G_FILE_TEST_IS_REGULAR)?g_strdup(directory):g_path_get_dirname(directory);
    char *canonical=g_canonicalize_filename(root,NULL);
    g_free(root);g_free(marker);g_free(directory);return canonical;
}

char *studio_engine_program(const char *configured,const char *root,GError **error)
{
    char *program=NULL;
    if(configured && *configured) program=g_find_program_in_path(configured);
    else {
#ifdef G_OS_WIN32
        const char *binary="psx-lib.exe";
#else
        const char *binary="psx-lib";
#endif
        const char *folders[]={"psx-lib/build","psx-lib",""};
        for(guint i=0;root && !program && i<G_N_ELEMENTS(folders);i++) {
            char *candidate=g_build_filename(root,folders[i],binary,NULL);
            if(g_file_test(candidate,G_FILE_TEST_IS_REGULAR) && g_file_test(candidate,G_FILE_TEST_IS_EXECUTABLE))
                program=g_canonicalize_filename(candidate,NULL);
            g_free(candidate);
        }
    }
    if(!program) failure(error,"PSX Lib not found in the Studio folder. Run make lib or reinstall the complete Studio package.");
    return program;
}

static gboolean run_engine(const char *program,const char *const *arguments,GError **error)
{
    GPtrArray *args=g_ptr_array_new();
    g_ptr_array_add(args,(gpointer)program);
    for(guint i=0;arguments[i];i++) g_ptr_array_add(args,(gpointer)arguments[i]);
    g_ptr_array_add(args,NULL);
    GSubprocess *process=g_subprocess_newv((const char *const *)args->pdata,G_SUBPROCESS_FLAGS_NONE,error);
    gboolean ok=process && g_subprocess_wait_check(process,NULL,error);
    if(process) g_object_unref(process);
    g_ptr_array_unref(args);return ok;
}

int studio_build_main(int argc,char **argv)
{
    char *project_arg=NULL,*psyq_arg=NULL,*output_arg=NULL,*verify_arg=NULL,*engine_arg=NULL,*root_arg=NULL;
    gboolean prepare=FALSE,diagnostics=FALSE;
    GOptionEntry options[]={
        {"project",0,0,G_OPTION_ARG_FILENAME,&project_arg,"Saved Studio project","FILE"},
        {"engine",0,0,G_OPTION_ARG_FILENAME,&engine_arg,"PSX Lib executable override for development (default: included framework)","FILE"},
        {"psyq",0,0,G_OPTION_ARG_FILENAME,&psyq_arg,"External Psy-Q SDK passed to PSX Lib","PATH"},
        {"output-root",0,0,G_OPTION_ARG_FILENAME,&output_arg,"Override the engine build output root","PATH"},
        {"verify",0,0,G_OPTION_ARG_FILENAME,&verify_arg,"Ask PSX Lib to verify a build","PATH"},
        {"prepare-only",0,0,G_OPTION_ARG_NONE,&prepare,"Export and prepare without starting DOSBox-X",NULL},
        {"diagnostics",0,0,G_OPTION_ARG_NONE,&diagnostics,"Enable engine diagnostics",NULL},
        {"root",0,0,G_OPTION_ARG_FILENAME,&root_arg,"Override the Studio installation root","PATH"},
        {NULL,0,0,0,NULL,NULL,NULL}
    };
    for(int i=1;i<argc;i++) if(!strcmp(argv[i],"--build-ps1")) {
        for(int j=i;j<argc;j++) argv[j]=argv[j+1];
        argc--;break;
    }
    GError *error=NULL;GOptionContext *context=g_option_context_new("-- export Studio scenes and build with PSX Lib");
    g_option_context_add_main_entries(context,options,NULL);
    gboolean ok=g_option_context_parse(context,&argc,&argv,&error);g_option_context_free(context);
    Project project;project_init(&project);BuildLock lock=NO_LOCK;
    char *root=NULL,*engine=NULL,*workspace=NULL,*source=NULL,*build=NULL,*output=NULL,*name=NULL;
    if(!ok) goto done;
    if(argc!=1 || (!project_arg && !verify_arg)) {ok=failure(&error,"Specify --project or --verify.");goto done;}
    if(project_arg && !(ok=project_load(&project,project_arg,&error))) goto done;
    char *executable=g_find_program_in_path(argv[0]);
    root=root_arg?g_canonicalize_filename(root_arg,NULL):studio_install_root(executable?executable:argv[0]);g_free(executable);
    engine=studio_engine_program(engine_arg,root,&error);
    if(!engine) {ok=FALSE;goto done;}
    if(verify_arg) {
        const char *args[]={"--build-ps1","--verify",verify_arg,NULL};
        ok=run_engine(engine,args,&error);goto done;
    }
    const char *sdk=psyq_arg?psyq_arg:project.psyq_path;
    if(!*sdk) {ok=failure(&error,"Configure Psy-Q in Settings or pass --psyq.");goto done;}
    name=project_build_name(&project);
    build=g_build_filename(root,"build","engine",NULL);
    workspace=g_build_filename(root,"build","studio-projects",name,NULL);
    if(!(ok=make_directory(workspace,&error) && acquire_lock(workspace,&lock,&error))) goto done;
    char *owner=NULL;
    if(exists(workspace,".studio-project")) {
        ok=read_at(workspace,".studio-project",&owner,NULL,&error);
        if(ok && strcmp(owner,project.path)) ok=failure(&error,"Generated source folder belongs to another project.");
        g_free(owner);
    } else {
        GDir *dir=g_dir_open(workspace,0,&error);const char *entry;ok=dir!=NULL;
        while(ok && (entry=g_dir_read_name(dir))) if(strcmp(entry,".build.lock"))
            ok=failure(&error,"Generated source folder is not managed by PSX Studio.");
        if(dir) g_dir_close(dir);
    }
    if(!ok || !(ok=write_at(workspace,".studio-project",project.path,-1,&error))) goto done;
    source=file_at(workspace,"source");
    char *custom=NULL;
    if(output_arg || *project.output_path) {
        char *parent=g_path_get_dirname(project.path);
        custom=g_canonicalize_filename(output_arg?output_arg:project.output_path,output_arg?NULL:parent);g_free(parent);
        char *hash=g_compute_checksum_for_string(G_CHECKSUM_SHA256,source,-1),*leaf=g_strdup_printf("%s-%.10s",name,hash);
        output=file_at(custom,leaf);g_free(hash);g_free(leaf);
    } else output=g_build_filename(build,"ps1",name,NULL);
    /* Invalidate this project's success receipt before export can fail. The
     * engine owns and locks its output separately while compiling/verifying. */
    if(exists(output,".psx-studio-build")) {
        BuildLock output_lock=NO_LOCK;char *owner=NULL;
        ok=acquire_lock(output,&output_lock,&error) && read_at(output,".psx-studio-build",&owner,NULL,&error);
        if(ok && strcmp(owner,source)) ok=failure(&error,"Engine output belongs to another source project.");
        if(ok) {
            char *success=file_at(output,"SUCCESS.OK"),*receipt=file_at(output,".studio-project");
            ok=remove_tree(success,&error) && remove_tree(receipt,&error);g_free(success);g_free(receipt);
        }
        g_free(owner);release_lock(output_lock);
    }
    if(!ok) {g_free(custom);goto done;}
    if(!(ok=remove_tree(source,&error) && project_export_engine(&project,source,&error))) {g_free(custom);goto done;}
    char *manifest_path=file_at(source,"assets.json");JsonParser *parser=json_parser_new();
    ok=json_parser_load_from_file(parser,manifest_path,&error);g_free(manifest_path);
    guint capacity=1;
    if(ok) {
        JsonObject *manifest=json_node_get_object(json_parser_get_root(parser));
        capacity=json_object_get_int_member(manifest,"max_objects");
    }
    g_object_unref(parser);if(!ok) {g_free(custom);goto done;}
    GPtrArray *args=g_ptr_array_new();
    const char *base[]={"--build-ps1","--source-root",source,"--name",name,"--build-directory",build,"--psyq",sdk,"--max-objects",NULL};
    for(guint i=0;base[i];i++) g_ptr_array_add(args,(gpointer)base[i]);
    char *count=g_strdup_printf("%u",capacity);g_ptr_array_add(args,count);
    char *resource_list=file_at(source,"reslist.h"),*resource_root=file_at(source,"resources");
    g_ptr_array_add(args,"--resources");g_ptr_array_add(args,resource_list);
    g_ptr_array_add(args,"--asset-root");g_ptr_array_add(args,resource_root);
    g_ptr_array_add(args,"--render-triangles");g_ptr_array_add(args,"8192");
    if(custom) {g_ptr_array_add(args,"--output-root");g_ptr_array_add(args,custom);}
    if(prepare) g_ptr_array_add(args,"--prepare-only");
    if(diagnostics) g_ptr_array_add(args,"--diagnostics");
    g_ptr_array_add(args,NULL);
    g_print("PSX Lib: %s\nGenerated C89 project: %s\n",engine,source);fflush(stdout);
    ok=run_engine(engine,(const char *const *)args->pdata,&error);
    g_ptr_array_unref(args);g_free(count);g_free(custom);g_free(resource_list);g_free(resource_root);
    if(ok && !prepare) ok=write_at(output,".studio-project",project.path,-1,&error);
done:
    if(!ok) g_printerr("PS1 build failed: %s\n",error?error->message:"Unknown error");
    g_clear_error(&error);release_lock(lock);project_clear(&project);
    g_free(root);g_free(engine);g_free(workspace);g_free(source);g_free(build);g_free(output);g_free(name);
    g_free(project_arg);g_free(psyq_arg);g_free(output_arg);g_free(verify_arg);g_free(engine_arg);g_free(root_arg);
    return ok?0:1;
}
