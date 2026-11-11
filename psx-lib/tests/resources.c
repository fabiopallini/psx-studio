#include "../tools/core.h"
#include <glib/gstdio.h>
#include <string.h>
#include <png.h>

static char *test_root;

static char *path(const char *name) { return g_build_filename(test_root, name, NULL); }

/* Small generated fixtures keep import tests independent of demo projects. */
static char *fixture_asset(const char *name)
{
    char *file=path(name);GError *error=NULL;
    if(g_str_has_suffix(name,".png")) {
        png_image image;memset(&image,0,sizeof(image));image.version=PNG_IMAGE_VERSION;
        image.width=64;image.height=64;image.format=PNG_FORMAT_RGBA;
        unsigned char pixels[64*64*4];memset(pixels,255,sizeof(pixels));
        g_assert_true(png_image_write_to_file(&image,file,FALSE,pixels,0,NULL));
    } else {
        const char *text=
            "v -28 -28 -28\nv 28 -28 -28\nv 28 -28 28\nv -28 -28 28\nv 0 28 0\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt 0.5 0.5\n"
            "f 1/1 4/4 3/3 2/2\nf 1/1 2/2 5/5\nf 2/2 3/3 5/5\nf 3/3 4/4 5/5\nf 4/4 1/1 5/5\n";
        g_assert_true(g_file_set_contents(file,text,-1,&error));g_assert_no_error(error);
    }
    return file;
}

static void remove_tree(const char *path_name)
{
    GDir *directory = g_dir_open(path_name, 0, NULL);
    if (directory) {
        const char *name;
        while ((name = g_dir_read_name(directory))) {
            char *child = g_build_filename(path_name, name, NULL); remove_tree(child); g_free(child);
        }
        g_dir_close(directory); g_rmdir(path_name);
    } else g_unlink(path_name);
}

static void spawn_checked(char **arguments, const char *directory)
{
    GError *error = NULL;
    int status;
    char *out = NULL, *err = NULL;
    char **environment = g_get_environ();
    environment = g_environ_setenv(environment, "TMPDIR", directory, TRUE);
    g_assert_true(g_spawn_sync(NULL, arguments, environment, G_SPAWN_SEARCH_PATH, NULL, NULL, &out, &err, &status, &error));
    g_assert_no_error(error);
    if (status) g_test_message("%s\n%s", out, err);
    g_assert_true(g_spawn_check_wait_status(status, &error)); g_assert_no_error(error);
    g_strfreev(environment); g_free(out); g_free(err);
}

static void run_bank_program(const char *directory,const char *code)
{
    GError *error=NULL;
    char *harness=g_build_filename(directory,"check.c",NULL),*binary=g_build_filename(directory,"check",NULL);
    g_assert_true(g_file_set_contents(harness,code,-1,&error));g_assert_no_error(error);
    char *runtime=g_canonicalize_filename("runtime",NULL),*object=g_build_filename(runtime,"object.c",NULL);
    char *inc=g_strconcat("-I",directory,NULL),*rtinc=g_strconcat("-I",runtime,NULL);
    GPtrArray *args=g_ptr_array_new_with_free_func(g_free);
    const char *flags[]={"cc","-std=c89","-pedantic-errors","-Wall","-Wextra","-Werror",inc,rtinc,object};
    for(guint i=0;i<G_N_ELEMENTS(flags);i++) g_ptr_array_add(args,g_strdup(flags[i]));
    GDir *dir=g_dir_open(directory,0,&error);const char *entry;g_assert_nonnull(dir);
    while((entry=g_dir_read_name(dir))) if(g_str_has_suffix(entry,".c")) g_ptr_array_add(args,g_build_filename(directory,entry,NULL));
    g_dir_close(dir);g_ptr_array_add(args,g_strdup("-o"));g_ptr_array_add(args,g_strdup(binary));g_ptr_array_add(args,NULL);
    spawn_checked((char**)args->pdata,directory);char *run[]={binary,NULL};spawn_checked(run,directory);
    g_ptr_array_unref(args);g_free(harness);g_free(binary);g_free(runtime);g_free(object);g_free(inc);g_free(rtinc);
}

static void plates_and_sound(void)
{
    GError *error=NULL;png_image image;memset(&image,0,sizeof(image));
    image.version=PNG_IMAGE_VERSION;image.width=320;image.height=256;image.format=PNG_FORMAT_RGBA;
    guchar *pixels=g_malloc0(320*256*4);
    for(guint i=0;i<320*256;i++) {pixels[i*4]=200;pixels[i*4+3]=255;}
    char *base=path("plate.png"),*layer=path("layer.png"),*sample=path("sample.vag");
    g_assert_true(png_image_write_to_file(&image,base,FALSE,pixels,0,NULL));
    memset(pixels,0,320*256*4);
    for(guint y=30;y<40;y++) for(guint x=270;x<290;x++) {pixels[(y*320+x)*4+1]=200;pixels[(y*320+x)*4+3]=255;}
    g_assert_true(png_image_write_to_file(&image,layer,FALSE,pixels,0,NULL));g_free(pixels);
    char vag[96]={0};memcpy(vag,"VAGp",4);vag[7]=0x20;vag[15]=48;vag[18]=43;vag[19]=17;vag[81]=1;
    g_assert_true(g_file_set_contents(sample,vag,sizeof(vag),&error));g_assert_no_error(error);
    char *list=path("plates.h"),*directory=path("plates-export");
    const char *text="PSX_BANK(65535, \"plate.png\")\nPSX_LAYER(\"layer.png\", 550)\nPSX_SOUND(\"sample.vag\")\nPSX_END()\n";
    g_assert_true(g_file_set_contents(list,text,-1,&error));g_assert_no_error(error);
    g_assert_true(resources_export(list,test_root,directory,&error));g_assert_no_error(error);
    run_bank_program(directory,
        "#include \"assets.h\"\n#include <assert.h>\n#include <string.h>\n"
        "int main(void){const PsxResourceBank *b;PsxScene s;PsxBackdrop d;"
        "b=psx_resources(65535);assert(b && b->mesh_count==0 && b->texture_count==3);"
        "assert(b->background_texture>0 && b->backdrop_count==1);"
        "assert(b->textures[b->background_texture-1].w==256 && b->textures[b->background_texture].w==64);"
        "d=b->backdrops[0];assert(d.x==270 && d.y==30 && d.w==20 && d.h==10 && d.depth==550);"
        "assert(b->sound_count==1 && b->sounds[0].rate==11025 && b->sounds[0].size==64);"
        "assert(strstr(b->textures[0].path,\"B65535\"));"
        "memset(&s,0,sizeof(s));s.camera[0]=90;psx_resource_apply(&s,b);"
        "assert(s.camera[0]==90 && s.count==0 && s.sound_count==1 && s.backdrop_count==1);return 0;}\n");
    char *scene=g_build_filename(directory,"scene.c",NULL);g_assert_false(g_file_test(scene,G_FILE_TEST_EXISTS));g_free(scene);
    g_free(base);g_free(layer);g_free(sample);g_free(list);g_free(directory);
}

static void invalid_bank_inputs(void)
{
    const char *invalid[]={
        "PSX_BANK(1, \"\")\nPSX_MODEL(1, mesh, \"res.obj\", \"\")\nPSX_MODEL(1, mesh, \"res.obj\", \"\")\nPSX_END()\n",
        "PSX_BANK(1, \"\")\nPSX_LAYER(\"layer.png\", 0)\nPSX_END()\n",
        "PSX_BANK(1, \"../escape.png\")\nPSX_END()\n",
        "{\"version\":3,\"objects\":[]}",
        "PSX_BANK(1, \"\")\n#include \"cycle.h\"\nPSX_END()\n"
    };
    char *list=path("invalid.h"),*directory=path("invalid-bank"),*cycle=path("cycle.h");GError *error=NULL;
    g_assert_true(g_file_set_contents(cycle,"#include \"cycle.h\"\n",-1,&error));g_assert_no_error(error);
    for(guint i=0;i<G_N_ELEMENTS(invalid);i++) {
        g_assert_true(g_file_set_contents(list,invalid[i],-1,&error));g_assert_no_error(error);
        g_assert_false(resources_export(list,test_root,directory,&error));g_assert_nonnull(error);g_clear_error(&error);
    }
    g_free(list);g_free(directory);g_free(cycle);
}

static void c_resource_banks(void)
{
    GError *error=NULL;
    char *model=fixture_asset("res.obj"),*image=fixture_asset("res.png");
    char *list=path("reslist.h"),*directory=path("c-resources");
    char *shared=path("resdefs.h");
    g_assert_true(g_file_set_contents(shared,"PSX_MODEL(2, mesh, \"res.obj\", \"res.png\")\n",-1,&error));
    const char *text="PSX_BANK(1, \"\")\n#include \"resdefs.h\"\nPSX_END()\n"
                     "PSX_BANK(2, \"\")\n#include \"resdefs.h\"\nPSX_END()\n";
    g_assert_true(g_file_set_contents(list,text,-1,&error));
    g_assert_true(resources_export(list,test_root,directory,&error));g_assert_no_error(error);
    char *harness=g_build_filename(directory,"check.c",NULL),*binary=g_build_filename(directory,"check",NULL);
    const char *code="#include \"assets.h\"\n#include <assert.h>\n#include <string.h>\n"
        "int main(void){const PsxResourceBank *a,*b;PsxScene s;memset(&s,0,sizeof(s));"
        "a=psx_resources(1);b=psx_resources(2);assert(a && b && !psx_resources(3));"
        "assert(a->texture_count==1 && b->texture_count==1);"
        "assert(psx_resource_mesh(a,2)->vertices==psx_resource_mesh(b,2)->vertices);"
        "assert(!psx_resource_mesh(a,999));psx_resource_apply(&s,a);"
        "assert(s.sound_count==0 && s.backdrop_count==0);"
        "assert(strstr(a->textures[0].path,\"B001\"));return 0;}\n";
    g_assert_true(g_file_set_contents(harness,code,-1,&error));
    char *runtime=g_canonicalize_filename("runtime",NULL),*object=g_build_filename(runtime,"object.c",NULL);
    char *inc=g_strconcat("-I",directory,NULL),*rtinc=g_strconcat("-I",runtime,NULL);
    GPtrArray *args=g_ptr_array_new_with_free_func(g_free);
    const char *flags[]={"cc","-std=c89","-pedantic-errors","-Wall","-Wextra","-Werror",inc,rtinc,object};
    for(guint i=0;i<G_N_ELEMENTS(flags);i++) g_ptr_array_add(args,g_strdup(flags[i]));
    GDir *dir=g_dir_open(directory,0,&error);const char *entry;g_assert_nonnull(dir);
    while((entry=g_dir_read_name(dir))) if(g_str_has_suffix(entry,".c")) g_ptr_array_add(args,g_build_filename(directory,entry,NULL));
    g_dir_close(dir);g_ptr_array_add(args,g_strdup("-o"));g_ptr_array_add(args,g_strdup(binary));g_ptr_array_add(args,NULL);
    spawn_checked((char**)args->pdata,directory);char *run[]={binary,NULL};spawn_checked(run,directory);
    g_ptr_array_unref(args);
    char *invalid=path("invalid-resources");
    g_assert_true(g_file_set_contents(list,"PSX_BANK(1, \"\")\nPSX_END()\nPSX_BANK(1, \"\")\nPSX_END()\n",-1,&error));
    g_assert_false(resources_export(list,test_root,invalid,&error));g_clear_error(&error);
    g_assert_true(g_file_set_contents(list,"PSX_BANK(1, \"\")\nPSX_MODEL(1, mesh, \"../res.obj\", \"\")\nPSX_END()\n",-1,&error));
    g_assert_false(resources_export(list,test_root,invalid,&error));g_clear_error(&error);
    g_free(model);g_free(image);g_free(list);g_free(directory);g_free(harness);g_free(binary);
    g_free(runtime);g_free(object);g_free(inc);g_free(rtinc);g_free(invalid);g_free(shared);
}

int main(int argc,char **argv)
{
    g_test_init(&argc,&argv,NULL);
    char *build=g_canonicalize_filename("build",NULL);
    char *template=g_build_filename(build,"resource-tests-XXXXXX",NULL);
    test_root=g_mkdtemp(template);g_assert_nonnull(test_root);
    g_test_add_func("/resources/c89-banks",c_resource_banks);
    g_test_add_func("/resources/plates-and-sound",plates_and_sound);
    g_test_add_func("/resources/rejects-invalid-input",invalid_bank_inputs);
    int result=g_test_run();remove_tree(test_root);g_free(template);g_free(build);return result;
}
