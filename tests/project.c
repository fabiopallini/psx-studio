#include "studio.h"
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
        const char *text=g_str_has_suffix(name,".obj")?
            "v -28 -28 -28\nv 28 -28 -28\nv 28 -28 28\nv -28 -28 28\nv 0 28 0\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt 0.5 0.5\n"
            "f 1/1 4/4 3/3 2/2\nf 1/1 2/2 5/5\nf 2/2 3/3 5/5\nf 3/3 4/4 5/5\nf 4/4 1/1 5/5\n":
            "#include \"object.h\"\nstatic void update(PsxObject *o,PsxScene *s,unsigned long input)"
            "{(void)s;(void)input;o->rotation[1]=(o->rotation[1]+16)&4095;}\n"
            "const PsxLogic psx_logic={0,update,0};\n";
        g_assert_true(g_file_set_contents(file,text,-1,&error));g_assert_no_error(error);
    }
    return file;
}

static void roundtrip(void)
{
    Project source, loaded;
    project_init(&source); project_init(&loaded);
    Object *o = project_add(&source, OBJECT_BILLBOARD);
    g_free(o->name); o->name = g_strdup("Player \"uno\" / è");
    o->position[0] = -123.5; o->position[2] = 88; o->rotation[1] = 45;
    o->scale[2] = 2; o->bounds[0] = 19; o->solid = TRUE;
    source.camera[0] = 40;source.projection=1;
    g_free(source.emulator_command);source.emulator_command=g_strdup("other-emulator --image {iso}");
    g_free(source.output_path);source.output_path=g_strdup("exports");
    char *file = path("roundtrip.psx");
    GError *error = NULL;
    g_assert_true(project_save(&source, file, &error)); g_assert_no_error(error);
    g_assert_false(source.dirty);
    char *data = project_data_path(file); g_assert_true(g_file_test(data, G_FILE_TEST_IS_DIR));
    g_assert_true(project_load(&loaded, file, &error)); g_assert_no_error(error);
    Object *read = g_ptr_array_index(loaded.objects, 0);
    g_assert_cmpstr(read->name, ==, o->name); g_assert_cmpint(read->kind, ==, OBJECT_BILLBOARD);
    g_assert_cmpfloat(read->position[0], ==, -123.5); g_assert_cmpfloat(read->rotation[1], ==, 45);
    g_assert_cmpfloat(read->scale[2], ==, 2); g_assert_cmpfloat(read->bounds[0], ==, 19);
    g_assert_true(read->solid); g_assert_cmpfloat(loaded.camera[0], ==, 40);
    g_assert_cmpstr(loaded.output_path, ==, "exports");
    g_assert_cmpint(loaded.projection,==,1);
    g_assert_cmpstr(loaded.emulator_command,==,source.emulator_command);
    g_assert_cmpuint(project_add(&loaded, OBJECT_SPRITE)->id, >, read->id);
    g_free(data); g_free(file); project_clear(&source); project_clear(&loaded);
}

static void invalid_open_preserves_state(void)
{
    const char *bad[] = {
        "[]", "{", "{\"version\":2}",
        "{\"version\":1,\"name\":7,\"psyq_path\":\"\",\"camera\":[0,0,0],\"objects\":[]}",
        "{\"version\":1,\"name\":\"p\",\"psyq_path\":\"\",\"camera\":[0,0],\"objects\":[]}",
        "{\"version\":1,\"name\":\"p\",\"psyq_path\":\"\",\"camera\":[0,0,0],\"objects\":[{}]}",
        "{\"version\":1,\"name\":\"p\",\"psyq_path\":\"\",\"camera\":[{},0,0],\"objects\":[]}"
    };
    Project p; project_init(&p); project_add(&p, OBJECT_MESH);
    char *file = path("invalid.psx");
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        GError *error = NULL;
        g_assert_true(g_file_set_contents(file, bad[i], -1, &error)); g_assert_no_error(error);
        g_assert_false(project_load(&p, file, &error)); g_assert_nonnull(error); g_clear_error(&error);
        g_assert_cmpuint(p.objects->len, ==, 1); g_assert_true(p.dirty);
        g_assert_cmpstr(p.name, ==, "New project");
    }
    g_free(file); project_clear(&p);
}

static void save_failure_preserves_disk(void)
{
    Project p; project_init(&p); Object *o = project_add(&p, OBJECT_MESH);
    char *file = path("preserve.psx");
    GError *error = NULL;
    g_assert_true(project_save(&p, file, &error));
    char *before, *after;
    g_assert_true(g_file_get_contents(file, &before, NULL, &error));
    o->scale[0] = 0; p.dirty = TRUE;
    g_assert_false(project_save(&p, file, &error)); g_clear_error(&error);
    g_assert_true(p.dirty);
    g_assert_true(g_file_get_contents(file, &after, NULL, &error));
    g_assert_cmpstr(before, ==, after);
    o->scale[0] = 1;
    char *impossible = path("missing/not-a-directory.psx");
    char *blocker = path("missing"); g_assert_true(g_file_set_contents(blocker, "x", 1, &error));
    g_assert_false(project_save(&p, impossible, &error)); g_clear_error(&error);
    g_assert_cmpstr(p.path, ==, file); g_assert_true(p.dirty);
    g_free(blocker); g_free(impossible); g_free(before); g_free(after); g_free(file); project_clear(&p);
}

static void duplicate_ids_and_paths(void)
{
    Project p; project_init(&p);
    Object *a = project_add(&p, OBJECT_MESH); a->solid = TRUE;
    Object *b = project_duplicate(&p, 0);
    g_assert_cmpuint(a->id, !=, b->id); g_assert_true(b->solid);
    g_free(b->name); b->name = g_strdup("independent");
    g_assert_cmpstr(a->name, !=, b->name);
    GError *error = NULL;
    b->id = a->id;
    g_assert_false(project_validate(&p, &error)); g_clear_error(&error); b->id = 2;
    const char *invalid[] = {"../outside.obj", "nested/../../outside.obj", "/tmp/object.obj", "C:\\asset.obj"};
    for (guint i = 0; i < G_N_ELEMENTS(invalid); i++) {
        g_free(b->asset); b->asset = g_strdup(invalid[i]);
        g_assert_false(project_validate(&p, &error)); g_clear_error(&error);
    }
    g_free(b->asset); b->asset = g_strdup("models/hero.obj");
    g_assert_true(project_validate(&p, &error)); g_assert_no_error(error);
    project_clear(&p);
}

static void relocation(void)
{
    Project p, loaded; project_init(&p); project_init(&loaded);
    Object *o = project_add(&p, OBJECT_SPRITE); g_free(o->asset); o->asset = g_strdup("texture.png");
    char *file = path("move.psx"), *dir = project_data_path(file);
    GError *error = NULL;
    g_assert_true(project_save(&p, file, &error));
    char *asset = g_build_filename(dir, "texture.png", NULL);
    g_assert_true(g_file_set_contents(asset, "asset-content", -1, &error));
    char *other = path("moved.psx"), *other_dir = project_data_path(other);
    g_assert_cmpint(g_rename(file, other), ==, 0); g_assert_cmpint(g_rename(dir, other_dir), ==, 0);
    g_assert_true(project_load(&loaded, other, &error)); g_assert_no_error(error);
    Object *read = g_ptr_array_index(loaded.objects, 0);
    char *resolved = g_build_filename(other_dir, read->asset, NULL);
    g_assert_true(g_file_test(resolved, G_FILE_TEST_IS_REGULAR));
    g_free(file); g_free(dir); g_free(asset); g_free(other); g_free(other_dir); g_free(resolved);
    project_clear(&p); project_clear(&loaded);
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

static void compile_export(Project *p, const char *harness_source, const char *name)
{
    char *directory = path(name);
    GError *error = NULL;
    g_assert_true(project_export(p, directory, &error)); g_assert_no_error(error);
    char *scene = g_build_filename(directory, "scene.c", NULL);
    char *harness = g_build_filename(directory, "check.c", NULL);
    char *binary = g_build_filename(directory, "check", NULL);
    char *runtime = g_canonicalize_filename("runtime", NULL);
    char *object = g_build_filename(runtime, "object.c", NULL);
    char *include = g_strconcat("-I", directory, NULL);
    char *runtime_include = g_strconcat("-I", runtime, NULL);
    g_assert_true(g_file_set_contents(harness, harness_source, -1, &error)); g_assert_no_error(error);
    char *compiler[] = { "cc", "-std=c89", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-x", "c",
        include, runtime_include, scene, object, harness, "-o", binary, NULL };
    GPtrArray *args=g_ptr_array_new(), *extra=g_ptr_array_new_with_free_func(g_free);
    for(int k=0;compiler[k];k++) g_ptr_array_add(args,compiler[k]);
    GDir *generated=g_dir_open(directory,0,NULL);const char *entry;
    while((entry=g_dir_read_name(generated))) if(entry[0]=='M' && g_str_has_suffix(entry,".C")) {
        char *module=g_build_filename(directory,entry,NULL);g_ptr_array_add(extra,module);g_ptr_array_add(args,module);
    }
    g_dir_close(generated);g_ptr_array_add(args,NULL);spawn_checked((char**)args->pdata,directory);
    g_ptr_array_unref(args);g_ptr_array_unref(extra);
    char *run[] = { binary, NULL }; spawn_checked(run, directory);
    g_free(directory); g_free(scene); g_free(harness); g_free(binary); g_free(runtime); g_free(object);
    g_free(include); g_free(runtime_include);
}

static void export_c89_scene(void)
{
    Project p; project_init(&p);
    g_free(p.name); p.name = g_strdup("Scene \"quoted\" %s");
    Scene *named=g_ptr_array_index(p.scenes,0);g_free(named->name);named->name=g_strdup(p.name);
    Object *a = project_add(&p, OBJECT_MESH), *b = project_add(&p, OBJECT_BILLBOARD);
    a->position[0] = -120.5; a->position[1] = 25.4; a->rotation[1] = 90;
    p.camera[0]=10; p.view[0]=90; p.projection=1; a->input_mode=1; a->speed=3; a->collision_action=2;
    a->scale[0] = 2.5; a->bounds[2] = 37.5; a->solid = TRUE;
    b->position[2] = 70;
    compile_export(&p,
        "#include \"scene.h\"\n#include <assert.h>\n#include <string.h>\n"
        "int main(void) { PsxScene *s; s=studio_scene_load();\n"
        "assert(strcmp(studio_scene_name,\"Scene \\\"quoted\\\" %s\")==0);\n"
        "assert(s->count==2); assert(s->objects[0].id==1);\n"
        "assert(s->projection==1); assert(s->camera[0]==10); assert(s->yaw==1024); assert(s->objects[0].input_mode==1); assert(s->objects[0].speed==3); assert(s->objects[0].collision_action==2);\n"
        "assert(s->objects[0].position[0]==-121); assert(s->objects[0].position[1]==25);\n"
        "assert(s->objects[0].rotation[1]==1024); assert(s->objects[0].scale[0]==10240);\n"
        "assert(s->objects[0].half_size[2]==38); assert(s->objects[0].solid);\n"
        "assert(s->objects[1].kind==PSX_OBJECT_BILLBOARD); assert(s->objects[1].position[2]==70);\n"
        "psx_object_kill(&s->objects[0]); assert(studio_scene_load()->objects[0].alive);\n"
        "return 0; }\n", "export-scene");
    project_clear(&p);
}

static void export_empty_and_capacity(void)
{
    Project p; project_init(&p);
    compile_export(&p,
        "#include \"scene.h\"\n#include <assert.h>\nint main(void) {\n"
        "PsxScene *s; s=studio_scene_load(); assert(s->count==0); psx_scene_update(s,0); return 0; }\n", "export-empty");
    for (int i = 0; i < STUDIO_MAX_OBJECTS; i++) project_add(&p, OBJECT_SPRITE);
    compile_export(&p,
        "#include \"scene.h\"\n#include <assert.h>\nint main(void) {\n"
        "PsxScene *s; s=studio_scene_load(); assert(s->count==512); assert(s->objects[511].id==512); return 0; }\n", "export-capacity");
    project_clear(&p);
}

static void export_rejects_unsupported(void)
{
    Project p; project_init(&p); Object *o = project_add(&p, OBJECT_MESH);
    char *directory = path("unsupported-export");
    GError *error = NULL;
    g_free(o->asset); o->asset = g_strdup("asset.obj");
    g_assert_false(project_export(&p, directory, &error)); g_clear_error(&error);
    g_assert_false(g_file_test(directory, G_FILE_TEST_EXISTS));
    g_free(o->asset); o->asset = g_strdup("");
    o->bounds[0] = .1;
    g_assert_false(project_export(&p, directory, &error)); g_clear_error(&error); o->bounds[0] = 32;
    p.view[1] = 91;
    g_assert_false(project_export(&p, directory, &error)); g_clear_error(&error); p.view[1] = 34;
    g_free(o->module); o->module = g_strdup("custom.c");
    g_assert_false(project_export(&p, directory, &error)); g_clear_error(&error);
    g_assert_false(g_file_test(directory, G_FILE_TEST_EXISTS));
    g_free(directory); project_clear(&p);
}

static void assets_save_as_module(void)
{
    Project p,loaded;project_init(&p);project_init(&loaded);GError *error=NULL;
    char *file=path("assets.psx");g_assert_true(project_save(&p,file,&error));
    Object *o=project_add(&p,OBJECT_MESH);
    char *obj=fixture_asset("pyramid.obj"),*png=fixture_asset("checker.png"),*module=fixture_asset("spin.c");
    g_assert_true(asset_import(&p,o,obj,0,&error));g_assert_no_error(error);
    g_assert_true(asset_import(&p,o,png,1,&error));g_assert_no_error(error);
    g_assert_true(asset_import(&p,o,module,2,&error));g_assert_no_error(error);
    o->input_mode=2;o->collision_action=1;o->speed=4;p.view[0]=55;p.camera[1]=17;
    char *copy=path("assets-copy.psx");g_assert_true(project_save(&p,copy,&error));g_assert_no_error(error);
    g_assert_true(project_load(&loaded,copy,&error));g_assert_no_error(error);
    Object *read=g_ptr_array_index(loaded.objects,0);g_assert_cmpint(read->input_mode,==,2);g_assert_cmpint(read->speed,==,4);
    g_assert_cmpint(read->collision_action,==,1);g_assert_cmpfloat(loaded.view[0],==,55);g_assert_cmpfloat(loaded.camera[1],==,17);
    Asset *visual=asset_load(&loaded,read,&error);g_assert_no_error(error);g_assert_nonnull(visual);g_assert_cmpuint(visual->width,==,64);asset_free(visual);
    read->input_mode=0;read->collision_action=0;
    compile_export(&loaded,
        "#include \"scene.h\"\n#include <assert.h>\nint main(void) {\n"
        "PsxScene *s; s=studio_scene_load(); assert(s->objects[0].mesh); assert(s->objects[0].mesh->count==18);\n"
        "assert(studio_texture_count==1); assert(studio_textures[0].w==64);\n"
        "assert(s->objects[0].rotation[1]==0); psx_scene_update(s,0); assert(s->objects[0].rotation[1]==16); return 0;}\n", "export-assets-module");
    project_clear(&loaded);project_clear(&p);g_free(file);g_free(copy);g_free(obj);g_free(png);g_free(module);
}

static void projection_validation(void)
{
    Project p;project_init(&p);GError *error=NULL;
    char *file=path("projection.psx");
    const char *legacy="{\"version\":1,\"name\":\"legacy\",\"psyq_path\":\"\",\"camera\":[0,0,-600],\"objects\":[]}";
    g_assert_true(g_file_set_contents(file,legacy,-1,&error));
    g_assert_true(project_load(&p,file,&error));g_assert_cmpint(p.projection,==,0);
    const char *invalid="{\"version\":2,\"name\":\"invalid\",\"psyq_path\":\"\",\"camera\":[0,0,-600],\"objects\":[],\"projection\":\"unknown\"}";
    g_assert_true(g_file_set_contents(file,invalid,-1,&error));
    g_assert_false(project_load(&p,file,&error));g_clear_error(&error);
    g_assert_cmpstr(p.name,==,"legacy");
    p.projection=7;g_assert_false(project_validate(&p,&error));g_clear_error(&error);
    g_free(file);project_clear(&p);
}

static void output_discovery(void)
{
    Project p;project_init(&p);GError *error=NULL;
    char *root=path("output-root"),*project=g_build_filename(root,"game.psx",NULL);
    g_assert_cmpint(g_mkdir_with_parents(root,0755),==,0);
    g_assert_true(project_save(&p,project,&error));
    char *base=g_build_filename(root,"exports","game-hash",NULL),*cd=g_build_filename(base,"cdrom",NULL);
    g_assert_cmpint(g_mkdir_with_parents(cd,0755),==,0);
    const char *names[]={".psx-studio-build","SUCCESS.OK","cdrom/game.cue","cdrom/GAME.ISO"};
    for(guint i=0;i<G_N_ELEMENTS(names);i++) {
        char *file=g_build_filename(base,names[i],NULL);
        g_assert_true(g_file_set_contents(file,i==0?project:"test",-1,&error));g_free(file);
    }
    g_free(p.output_path);p.output_path=g_strdup("exports");
    char *found=project_build_output(&p,root);g_assert_cmpstr(found,==,base);g_free(found);
    g_assert_null(project_build_sdk(&p,root));
    char *config=g_build_filename(base,"dos.conf",NULL);
    g_assert_true(g_file_set_contents(config,"[autoexec]\n    mount d \"/sdk with spaces/psyq\" -ro\n",-1,&error));
    char *sdk=project_build_sdk(&p,root);g_assert_cmpstr(sdk,==,"/sdk with spaces/psyq");g_free(sdk);
    char *info=g_build_filename(base,"build-info.json",NULL);
    g_assert_true(g_file_set_contents(info,"{\"version\":1,\"psyq_path\":\"/selected/sdk\"}",-1,&error));
    sdk=project_build_sdk(&p,root);g_assert_cmpstr(sdk,==,"/selected/sdk");g_free(sdk);
    g_assert_cmpstr(p.psyq_path,==,""); /* Recovery is explicit; it never silently changes the project. */
    g_assert_true(g_file_set_contents(info,"{\"psyq_path\":12}",-1,&error));
    sdk=project_build_sdk(&p,root);g_assert_cmpstr(sdk,==,"/sdk with spaces/psyq");g_free(sdk);
    g_free(config);g_free(info);
    char *iso=g_build_filename(base,"cdrom","GAME.ISO",NULL);g_assert_cmpint(g_remove(iso),==,0);
    g_assert_null(project_build_output(&p,root));
    g_assert_true(g_file_set_contents(iso,"test",-1,&error));
    char *marker=g_build_filename(base,".psx-studio-build",NULL);
    g_assert_true(g_file_set_contents(marker,"/another/project.psx",-1,&error));
    g_assert_null(project_build_output(&p,root));
    /* The fixed default wins over an older hashed folder for the same project. */
    char *fixed=g_build_filename(root,"build","ps1","game",NULL);
    char *legacy=g_build_filename(root,"build","ps1","game-oldhash",NULL);
    const char *folders[]={legacy,fixed};
    for(guint j=0;j<G_N_ELEMENTS(folders);j++) {
        char *images=g_build_filename(folders[j],"cdrom",NULL);g_assert_cmpint(g_mkdir_with_parents(images,0755),==,0);g_free(images);
        for(guint i=0;i<G_N_ELEMENTS(names);i++) {
            char *file=g_build_filename(folders[j],names[i],NULL);
            g_assert_true(g_file_set_contents(file,i==0?project:"test",-1,&error));g_free(file);
        }
    }
    g_free(p.output_path);p.output_path=g_strdup("");
    found=project_build_output(&p,root);g_assert_cmpstr(found,==,fixed);g_free(found);
    g_free(fixed);g_free(legacy);
    g_assert_no_error(error);g_free(marker);g_free(iso);g_free(cd);g_free(base);g_free(project);g_free(root);project_clear(&p);
}

static void native_tim_pixels(void)
{
    Project p; project_init(&p); GError *error = NULL;
    char *file=path("tim.psx"); g_assert_true(project_save(&p,file,&error));
    char *data=project_data_path(file), *png=g_build_filename(data,"pixels.png",NULL);
    /* Odd width also checks row order without implicit padding between rows. */
    const unsigned char pixels[]={
        255,0,0,255, 0,255,0,255, 0,0,255,255,
        0,0,0,255, 255,255,255,0, 255,255,255,127,
        255,255,255,128, 7,7,7,255, 8,16,24,255
    };
    png_image image; memset(&image,0,sizeof(image)); image.version=PNG_IMAGE_VERSION;
    image.width=3; image.height=3; image.format=PNG_FORMAT_RGBA;
    g_assert_true(png_image_write_to_file(&image,png,FALSE,pixels,0,NULL));
    Object *o=project_add(&p,OBJECT_SPRITE); g_free(o->texture); o->texture=g_strdup("pixels.png");
    char *directory=path("native-tim"); g_assert_true(project_export(&p,directory,&error));g_assert_no_error(error);
    char *tim=g_build_filename(directory,"T000.TIM",NULL), *bytes;gsize length;
    g_assert_true(g_file_get_contents(tim,&bytes,&length,&error));g_assert_no_error(error);
    const unsigned char expected[]={
        0x10,0,0,0, 2,0,0,0, 30,0,0,0, 0x40,1,0,0, 3,0,3,0,
        0x1f,0, 0xe0,3, 0,0x7c, 0,0x80, 0,0, 0,0,
        0xff,0x7f, 0,0x80, 0x41,0x0c
    };
    g_assert_cmpmem(bytes,length,expected,sizeof(expected));
    Asset invalid={0};
    g_assert_false(asset_write_tim(&invalid,tim,0,0,&error));g_clear_error(&error);
    Asset *visual=asset_load(&p,o,&error);g_assert_nonnull(visual);g_assert_no_error(error);
    g_assert_false(asset_write_tim(visual,tim,1023,0,&error));g_clear_error(&error);
    asset_free(visual);g_free(bytes);g_free(tim);g_free(directory);g_free(png);g_free(data);g_free(file);project_clear(&p);
}

static void uv_texel_mapping(void)
{
    Project p;project_init(&p);GError *error=NULL;
    char *file=path("uv.psx");g_assert_true(project_save(&p,file,&error));
    char *data=project_data_path(file),*png=g_build_filename(data,"uv.png",NULL),*obj=g_build_filename(data,"uv.obj",NULL);
    unsigned char pixels[3*3*4];memset(pixels,255,sizeof(pixels));
    png_image image;memset(&image,0,sizeof(image));image.version=PNG_IMAGE_VERSION;
    image.width=3;image.height=3;image.format=PNG_FORMAT_RGBA;
    g_assert_true(png_image_write_to_file(&image,png,FALSE,pixels,0,NULL));
    g_assert_true(g_file_set_contents(obj,"v 0 0 0\nv 10 0 0\nv 0 10 0\nvt 0.25 0.75\nvt 0 0\nvt 1 1\nf 1/1 2/2 3/3\n",-1,&error));
    Object *o=project_add(&p,OBJECT_MESH);g_free(o->asset);o->asset=g_strdup("uv.obj");g_free(o->texture);o->texture=g_strdup("uv.png");
    Asset *a=asset_load(&p,o,&error);g_assert_nonnull(a);g_assert_no_error(error);
    AssetVertex *v=&g_array_index(a->vertices,AssetVertex,0);
    /* GL previously chose texel 0 at u=.25; PS1 export selects texel 1.
     * The preview now samples the centre of this same exported texel. */
    g_assert_cmpint((int)(v->uv[0]*a->width),==,0);
    long texel=asset_uv_texel(v->uv[0],a->width);
    g_assert_cmpint(texel,==,1);
    g_assert_cmpint((int)(((texel+.5)/a->width)*a->width),==,1);
    compile_export(&p,"#include \"scene.h\"\n#include <assert.h>\nint main(void){const PsxVertex *v;v=studio_scene_load()->objects[0].mesh->vertices;assert(v[0].uv[0]==1 && v[0].uv[1]==1);assert(v[1].uv[0]==0 && v[1].uv[1]==2);assert(v[2].uv[0]==2 && v[2].uv[1]==0);return 0;}\n","uv-mapping-export");
    asset_free(a);g_free(obj);g_free(png);g_free(data);g_free(file);project_clear(&p);
}

static void gameplay_behaviors(void)
{
    Project p;project_init(&p);GError *error=NULL;
    char *file=path("behaviors.psx"),*module=fixture_asset("spin.c");
    g_assert_true(project_save(&p,file,&error));g_assert_no_error(error);
    Object *block=project_add(&p,OBJECT_MESH);block->solid=TRUE;
    for(int k=0;k<3;k++) block->bounds[k]=16;
    g_assert_true(asset_import(&p,block,module,2,&error));g_assert_no_error(error);
    Object *player=project_add(&p,OBJECT_MESH);player->solid=TRUE;player->position[0]=-80;player->input_mode=1;player->speed=4;
    for(int k=0;k<3;k++) player->bounds[k]=8;
    Object *hazard=project_add(&p,OBJECT_SPRITE);hazard->position[0]=80;hazard->position[2]=40;hazard->collision_action=2;
    for(int k=0;k<3;k++) hazard->bounds[k]=8;
    Object *camera=project_add(&p,OBJECT_CAMERA);camera->follow_target=player->id;
    camera->follow_offset[1]=100;camera->follow_offset[2]=200;p.active_camera=camera->id;
    compile_export(&p,
        "#include \"scene.h\"\n#include <assert.h>\nint main(void) {\n"
        "PsxScene *s; PsxObject *player; int i; s=studio_scene_load();\n"
        "assert(s->count==4 && s->active_camera==4 && s->camera_eye);player=&s->objects[1];\n"
        "assert(s->objects[0].rotation[1]==0);psx_scene_update(s,0);assert(s->objects[0].rotation[1]==16);\n"
        "for(i=0;i<100;i++) psx_scene_update(s,8192UL);\n"
        "assert(player->alive && player->position[0]==-24);assert(s->camera[0]==-24);\n"
        "for(i=0;i<10;i++) {psx_scene_update(s,16384UL);}assert(player->position[2]==40);\n"
        "for(i=0;i<100 && player->alive;i++) psx_scene_update(s,8192UL);\n"
        "assert(!player->alive && s->objects[2].alive);return 0;}\n", "export-gameplay-behaviors");
    project_clear(&p);g_free(file);g_free(module);
}

static void vram_limits(void)
{
    Project p;project_init(&p);GError *error=NULL;char *file=path("vram.psx");
    g_assert_true(project_save(&p,file,&error));char *dir=project_data_path(file);
    for(int i=0;i<6;i++) {
        char *name=g_strdup_printf("texture%d.png",i),*png=g_build_filename(dir,name,NULL);
        png_image image;memset(&image,0,sizeof(image));image.version=PNG_IMAGE_VERSION;image.width=i<4?256:192;image.height=8;image.format=PNG_FORMAT_RGBA;
        unsigned char *pixels=g_malloc0(PNG_IMAGE_SIZE(image));g_assert_true(png_image_write_to_file(&image,png,FALSE,pixels,0,NULL));g_free(pixels);
        Object *o=project_add(&p,OBJECT_SPRITE);g_free(o->texture);o->texture=name;g_free(png);
    }
    char *output=path("six-textures");g_assert_true(project_export(&p,output,&error));g_assert_no_error(error);
    JsonParser *parser=json_parser_new();char *manifest=g_build_filename(output,"assets.json",NULL);g_assert_true(json_parser_load_from_file(parser,manifest,&error));g_assert_no_error(error);
    JsonArray *textures=json_object_get_array_member(json_node_get_object(json_parser_get_root(parser)),"textures");
    g_assert_cmpuint(json_array_get_length(textures),==,6);
    for(guint a=0;a<6;a++) for(guint b=0;b<a;b++) {
        JsonObject *x=json_array_get_object_element(textures,a),*y=json_array_get_object_element(textures,b);
        int xx=json_object_get_int_member(x,"x"),xy=json_object_get_int_member(x,"y"),xw=json_object_get_int_member(x,"width");
        int yx=json_object_get_int_member(y,"x"),yy=json_object_get_int_member(y,"y"),yw=json_object_get_int_member(y,"width");
        g_assert_true(xy!=yy || xx+xw<=yx || yx+yw<=xx);
    }
    Object *extra=project_add(&p,OBJECT_SPRITE);g_free(extra->texture);extra->texture=g_strdup("texture0.png");
    /* A seventh object sharing a texture does not consume another slot. */
    g_assert_true(project_export(&p,output,&error));g_assert_no_error(error);
    Object *fifth=g_ptr_array_index(p.objects,4);g_free(fifth->texture);fifth->texture=g_strdup("wide.png");
    char *wide=g_build_filename(dir,"wide.png",NULL),*first=g_build_filename(dir,"texture0.png",NULL);
    char *bytes;gsize length;g_assert_true(g_file_get_contents(first,&bytes,&length,&error));g_assert_true(g_file_set_contents(wide,bytes,length,&error));g_free(bytes);
    g_assert_false(project_export(&p,output,&error));g_assert_nonnull(error);g_clear_error(&error);
    g_object_unref(parser);g_free(manifest);g_free(wide);g_free(first);g_free(output);g_free(dir);g_free(file);project_clear(&p);
}

static void camera_objects(void)
{
    Project p,q;project_init(&p);project_init(&q);
    Object *c=project_add(&p,OBJECT_CAMERA);
    g_assert_cmpuint(p.active_camera,==,c->id);
    g_assert_cmpfloat(c->rotation[0],==,p.view[1]);
    c->position[0]=123;c->position[1]=456;c->position[2]=789;
    c->rotation[0]=0;c->rotation[1]=90;c->camera_zoom=1.5;c->camera_projection=1;
    Object *copy=project_duplicate(&p,0);
    g_assert_cmpuint(p.active_camera,==,c->id);
    g_assert_cmpfloat(copy->camera_zoom,==,1.5);
    g_assert_cmpint(copy->camera_projection,==,1);
    p.show_game_frame=FALSE;p.drag_axis=2;p.view[1]=0;
    char *file=path("cameras.psx");GError *error=NULL;
    g_assert_true(project_save(&p,file,&error));g_assert_no_error(error);
    g_assert_true(project_load(&q,file,&error));g_assert_no_error(error);
    g_assert_cmpuint(q.active_camera,==,c->id);g_assert_false(q.show_game_frame);g_assert_cmpint(q.drag_axis,==,2);
    Object *loaded=g_ptr_array_index(q.objects,0);
    g_assert_cmpint(loaded->kind,==,OBJECT_CAMERA);g_assert_cmpfloat(loaded->camera_zoom,==,1.5);
    char *data=project_data_path(file),*module=g_build_filename(data,"camera.c",NULL);
    g_assert_true(g_file_set_contents(module,
        "#include \"object.h\"\n"
        "static void update(PsxObject *o, PsxScene *s, unsigned long input) { (void)s;(void)input;o->position[2]++; }\n"
        "const PsxLogic psx_logic={0,update,0};\n",-1,&error));g_assert_no_error(error);
    g_free(loaded->module);loaded->module=g_strdup("camera.c");g_free(module);g_free(data);
    compile_export(&q,
        "#include \"scene.h\"\n#include <assert.h>\n"
        "int main(void) { PsxScene *s; PsxObject *c; s=studio_scene_load(); c=&s->objects[0];\n"
        "assert(s->active_camera==1 && s->camera_eye); assert(c->kind==PSX_OBJECT_CAMERA && !c->mesh);\n"
        "assert(s->camera[0]==123 && s->camera[1]==456 && s->camera[2]==789);\n"
        "assert(s->yaw==1024 && s->pitch==0 && s->projection==1 && s->zoom==6144);\n"
        "c->input_mode=2;c->speed=3;psx_scene_update(s,4096UL);assert(s->camera[1]==459 && s->camera[2]==790);\n"
        "assert(!psx_objects_overlap(c,&s->objects[1]));\n"
        "s->active_camera=2;psx_scene_update(s,0);assert(s->camera[0]==163);\n"
        "return 0; }\n", "export-camera");
    q.active_camera=0;project_duplicate(&q,0);g_assert_cmpuint(q.active_camera,==,0);
    q.active_camera=999;g_assert_false(project_validate(&q,&error));g_clear_error(&error);
    q.active_camera=0;loaded->solid=TRUE;g_assert_false(project_validate(&q,&error));g_clear_error(&error);
    loaded->solid=FALSE;loaded->rotation[0]=91;g_assert_false(project_validate(&q,&error));g_clear_error(&error);
    g_free(file);project_clear(&p);project_clear(&q);
}

static void emulator_command(void)
{
    GError *error=NULL;
    const char *cue="/a path/it's {iso}/game.cue",*iso="/a path/GAME.ISO";
    char **argv=project_emulator_argv("\"emulator tool\" --disc={cue} --iso \"{iso}\" --literal '$HOME; touch file'",cue,iso,&error);
    g_assert_no_error(error);g_assert_nonnull(argv);
    g_assert_cmpstr(argv[0],==,"emulator tool");
    char *disc=g_strconcat("--disc=",cue,NULL);g_assert_cmpstr(argv[1],==,disc);g_free(disc);
    g_assert_cmpstr(argv[2],==,"--iso");g_assert_cmpstr(argv[3],==,iso);
    g_assert_cmpstr(argv[5],==,"$HOME; touch file");g_assert_null(argv[6]);g_strfreev(argv);
    const char *invalid[]={"","pcsxr -cdfile","pcsxr 'unterminated {cue}"};
    for(guint i=0;i<G_N_ELEMENTS(invalid);i++) {
        argv=project_emulator_argv(invalid[i],cue,iso,&error);
        g_assert_null(argv);g_assert_nonnull(error);g_clear_error(&error);
    }
    Project p;project_init(&p);g_free(p.emulator_command);p.emulator_command=g_strdup("pcsxr");
    g_assert_false(project_validate(&p,&error));g_clear_error(&error);project_clear(&p);
}

static void multiple_scenes(void)
{
    Project p,q;project_init(&p);project_init(&q);GError *error=NULL;
    Object *player=project_add(&p,OBJECT_MESH),*trigger=project_add(&p,OBJECT_SPRITE);
    player->input_mode=1;player->speed=2;
    trigger->collision_action=3;trigger->target_scene=2;
    player->collider_offset[0]=7;player->bounds[0]=4;trigger->bounds[0]=4;
    Scene *second=project_scene_add(&p,"Second level");g_assert_nonnull(second);g_assert_cmpuint(second->id,==,2);
    Object *target=project_add(&p,OBJECT_SPRITE);target->position[0]=500;target->input_mode=2;target->speed=4;
    Object *camera=project_add(&p,OBJECT_CAMERA);camera->follow_target=target->id;
    camera->follow_offset[0]=5;camera->follow_offset[1]=100;camera->follow_offset[2]=200;p.show_colliders=TRUE;
    char *file=path("levels.psx");
    g_assert_true(project_save(&p,file,&error));g_assert_no_error(error);
    g_assert_true(project_load(&q,file,&error));g_assert_no_error(error);
    g_assert_cmpuint(q.scenes->len,==,2);g_assert_cmpuint(q.scene_id,==,2);g_assert_cmpuint(q.startup_scene,==,1);
    g_assert_true(project_scene_switch(&q,1,&error));g_assert_no_error(error);
    g_assert_cmpuint(q.objects->len,==,2);g_assert_cmpfloat(((Object*)g_ptr_array_index(q.objects,0))->collider_offset[0],==,7);
    compile_export(&q,
        "#include \"scene.h\"\n#include <assert.h>\n"
        "static int cleaned;\nstatic void cleanup(PsxObject *o,PsxScene *s) { (void)s; assert(o->user_data);cleaned++; }\n"
        "int main(void) {PsxScene *s;int i;s=studio_scene_load();assert(s->scene_id==1 && !s->show_colliders);\n"
        "s->objects[0].cleanup=cleanup;s->objects[0].user_data=&cleaned;\n"
        "assert(!psx_scene_request_change(s,999));assert(psx_scene_request_change(s,2));assert(!psx_scene_request_change(s,1));\n"
        "assert(studio_scene_apply_change());assert(cleaned==1 && s->scene_id==2 && s->count==2 && s->show_colliders);\n"
        "assert(s->camera[0]==505 && s->camera[1]==100 && s->camera[2]==200);\n"
        "psx_scene_update(s,8192UL);assert(s->objects[0].position[0]==504 && s->camera[0]==509);\n"
        "for(i=0;i<20;i++) {assert(psx_scene_request_change(s,1));assert(studio_scene_apply_change());\n"
        "assert(s->objects[0].alive && !s->objects[0].user_data && !s->objects[0].cleanup && s->objects[0].position[0]==0);\n"
        "psx_scene_update(s,0);assert(s->pending_scene==2 && s->scene_id==1);assert(studio_scene_apply_change());assert(s->scene_id==2);}\n"
        "assert(!studio_scene_load_id(999));assert(s->scene_id==2);return 0;}\n", "export-levels");
    JsonNode *snapshot=project_to_json(&q);g_assert_true(project_scene_delete(&q,2,&error));g_assert_no_error(error);
    g_assert_cmpint(((Object*)g_ptr_array_index(q.objects,1))->collision_action,==,0);
    Project restored;project_init(&restored);g_assert_true(project_from_json(&restored,snapshot,&error));g_assert_no_error(error);
    g_assert_cmpuint(restored.scenes->len,==,2);project_clear(&restored);json_node_free(snapshot);
    g_assert_false(project_scene_delete(&q,1,&error));g_clear_error(&error);
    g_free(file);project_clear(&p);project_clear(&q);
}

static void scene_module_transition(void)
{
    if(!g_file_test("examples/first-scene.psx",G_FILE_TEST_IS_REGULAR)) {
        g_test_skip("Install the examples archive to run this integration test.");return;
    }
    Project p;project_init(&p);GError *error=NULL;
    g_assert_true(project_load(&p,"examples/first-scene.psx",&error));g_assert_no_error(error);
    char *file=path("module-scenes.psx");
    g_assert_true(project_save(&p,file,&error));g_assert_no_error(error);
    g_assert_true(project_scene_switch(&p,2,&error));g_assert_no_error(error);
    char *data=project_data_path(file),*module=g_build_filename(data,"return.c",NULL);
    g_assert_true(g_file_set_contents(module,
        "#include \"object.h\"\nstatic void update(PsxObject *o,PsxScene *s,unsigned long input)"
        "{(void)o;if(input&2048UL)psx_scene_request_change(s,1);}\nconst PsxLogic psx_logic={0,update,0};\n",-1,&error));
    g_assert_no_error(error);
    Object *player=g_ptr_array_index(p.objects,1);g_free(player->module);player->module=g_strdup("return.c");
    g_assert_true(project_scene_switch(&p,1,&error));g_assert_no_error(error);
    compile_export(&p,
        "#include \"scene.h\"\n#include <assert.h>\n"
        "int main(void) {PsxScene *s;s=studio_scene_load();assert(s->scene_id==1 && studio_texture_count==3);\n"
        "assert(psx_scene_request_change(s,2));assert(studio_scene_apply_change());assert(s->scene_id==2 && studio_texture_count==3);\n"
        "psx_scene_update(s,2048UL);assert(s->pending_scene==1);assert(studio_scene_apply_change());\n"
        "assert(s->scene_id==1 && s->objects[1].position[0]==-90);return 0;}\n","export-module-transition");
    project_clear(&p);g_free(file);g_free(data);g_free(module);
}

static void first_scene_example(void)
{
    if(!g_file_test("examples/first-scene.psx",G_FILE_TEST_IS_REGULAR)) {
        g_test_skip("Install the examples archive to run this integration test.");return;
    }
    Project p;project_init(&p);GError *error=NULL;
    g_assert_true(project_load(&p,"examples/first-scene.psx",&error));g_assert_no_error(error);
    g_assert_cmpuint(p.scenes->len,==,2);
    compile_export(&p,
        "#include \"scene.h\"\n#include <assert.h>\n"
        "int main(void) {PsxScene *s;int i;s=studio_scene_load();assert(s->scene_id==1);\n"
        "assert(s->objects[1].kind==PSX_OBJECT_MESH && s->objects[2].kind==PSX_OBJECT_BILLBOARD);\n"
        "assert(s->objects[2].collision_action==3 && s->objects[2].target_scene==2);\n"
        "assert(s->objects[3].follow_target==2 && s->camera[0]==-90);\n"
        "psx_scene_update(s,8192UL);assert(s->objects[1].position[0]==-85 && s->camera[0]==-85);\n"
        "for(i=0;i<100 && !s->pending_scene;i++) psx_scene_update(s,8192UL);\n"
        "assert(s->pending_scene==2);assert(studio_scene_apply_change());assert(s->scene_id==2);\n"
        "assert(s->objects[1].position[0]==-90 && s->camera[0]==-90);\n"
        "assert(s->objects[2].target_scene==1 && studio_texture_count==3);\n"
        "for(i=0;i<100 && !s->pending_scene;i++) psx_scene_update(s,8192UL);\n"
        "assert(s->pending_scene==1);assert(studio_scene_apply_change());assert(s->scene_id==1);\n"
        "assert(s->objects[1].position[0]==-90);return 0;}\n","export-first-scene");
    project_clear(&p);
}

static void sound_assets(void)
{
    Project p;project_init(&p);GError *error=NULL;
    Object *o=project_add(&p,OBJECT_MESH);char *file=path("sound.psx");
    g_assert_true(project_save(&p,file,&error));g_assert_no_error(error);
    char *directory=project_data_path(file),*module=g_build_filename(directory,"logic.c",NULL);
    char *sample=g_build_filename(directory,"shot.vag",NULL),*destination=path("sound-export");
    const char *code="/* @sound shot.vag */\n#include \"object.h\"\nconst PsxLogic psx_logic={0,0,0};\n";
    g_assert_true(g_file_set_contents(module,code,-1,&error));g_assert_no_error(error);
    /* Three silent ADPCM blocks: independent of optional example assets. */
    gsize size=96;char *vag=g_malloc0(size);
    memcpy(vag,"VAGp",4);vag[7]=0x20;vag[15]=48;
    vag[18]=0x2b;vag[19]=0x11; /* 11025 Hz, big endian. */
    vag[size-15]=1;
    g_assert_true(g_file_set_contents(sample,vag,size,&error));g_assert_no_error(error);
    g_free(o->module);o->module=g_strdup("logic.c");
    g_assert_true(project_export(&p,destination,&error));g_assert_no_error(error);
    /* Exercise external sample linkage and sharing without example assets. */
    g_assert_nonnull(project_scene_add(&p,"Shared sample"));
    Object *second=project_add(&p,OBJECT_MESH);g_free(second->module);second->module=g_strdup("logic.c");
    compile_export(&p,
        "#include \"scene.h\"\n#include <assert.h>\n"
        "int main(void){PsxScene *s;const unsigned char *bytes;PsxSound *bank;"
        "s=studio_scene_load_id(1);assert(s && s->sound_count==1);"
        "bytes=s->sounds[0].data;bank=s->sounds;assert(bytes[33]==1);"
        "s=studio_scene_load_id(2);assert(s && s->sound_count==1);"
        "assert(s->sounds!=bank && s->sounds[0].data==bytes);return 0;}\n",
        "shared-sample-linkage");
    g_assert_true(project_scene_switch(&p,1,&error));g_assert_no_error(error);
    vag[65]=4;vag[size-15]=3; /* Full-sample music loop. */
    g_assert_true(g_file_set_contents(sample,vag,size,&error));g_assert_no_error(error);
    g_assert_true(project_export(&p,destination,&error));g_assert_no_error(error);
    vag[65]=0; /* Repeat without a loop-start marker is invalid. */
    g_assert_true(g_file_set_contents(sample,vag,size,&error));g_assert_no_error(error);
    g_assert_false(project_export(&p,destination,&error));g_assert_nonnull(error);g_clear_error(&error);
    vag[65]=0;vag[size-15]=1;
    vag[49]=7; /* Arbitrary loop flags are rejected. */
    g_assert_true(g_file_set_contents(sample,vag,size,&error));g_assert_no_error(error);
    g_assert_false(project_export(&p,destination,&error));g_assert_nonnull(error);g_clear_error(&error);
    g_assert_true(g_file_set_contents(module,"/* @sound ../shot.vag */\nconst PsxLogic psx_logic={0,0,0};",-1,&error));g_assert_no_error(error);
    g_assert_false(project_export(&p,destination,&error));g_assert_nonnull(error);g_clear_error(&error);
    g_free(vag);g_free(file);g_free(directory);g_free(module);g_free(sample);g_free(destination);project_clear(&p);
}

static void shadow_ops_example(void)
{
    if(!g_file_test("examples/shadow-ops.psx",G_FILE_TEST_IS_REGULAR)) {
        g_test_skip("Install the examples archive to run this integration test.");return;
    }
    Project p;project_init(&p);GError *error=NULL;char *harness=NULL;
    g_assert_true(project_load(&p,"examples/shadow-ops.psx",&error));g_assert_no_error(error);
    g_assert_cmpuint(p.scenes->len,>=,5);g_assert_cmpuint(p.scenes->len,<=,6);
    g_assert_nonnull(project_scene_find(&p,p.scene_id));g_assert_cmpuint(p.startup_scene,==,1);
    g_assert_cmpuint(((Scene*)g_ptr_array_index(p.scenes,0))->id,==,1);
    /* Industrial scenery must not replace the bright shot-effect atlas. */
    for(guint i=0;i<p.scenes->len;i++) {
        Project view;
        g_assert_true(project_scene_view(&p,i,&view,&error));g_assert_no_error(error);
        for(guint j=0;j<view.objects->len;j++) {
            Object *o=g_ptr_array_index(view.objects,j);
            if(!g_strcmp0(o->asset,"tracer.obj") || !g_strcmp0(o->asset,"muzzle.obj"))
                g_assert_cmpstr(o->texture,==,"atlas.png");
        }
        project_clear(&view);
    }
    g_assert_true(g_file_get_contents("tests/shadow_game.c",&harness,NULL,&error));g_assert_no_error(error);
    compile_export(&p,harness,"export-shadow-ops");
    g_free(harness);project_clear(&p);
}

static void scene_snapshot_isolation(void)
{
    Project p,q;project_init(&p);project_init(&q);GError *error=NULL;
    Object *trigger=project_add(&p,OBJECT_SPRITE);trigger->collision_action=3;trigger->target_scene=2;
    g_assert_nonnull(project_scene_add(&p,NULL));g_assert_nonnull(project_scene_add(&p,NULL));
    JsonNode *before=project_to_json(&p);
    g_assert_true(project_scene_delete(&p,2,&error));g_assert_no_error(error);
    g_assert_true(project_from_json(&q,before,&error));g_assert_no_error(error);
    g_assert_true(project_scene_switch(&q,1,&error));g_assert_no_error(error);
    trigger=g_ptr_array_index(q.objects,0);g_assert_cmpint(trigger->collision_action,==,3);g_assert_cmpuint(trigger->target_scene,==,2);
    json_node_free(before);project_clear(&p);project_clear(&q);
    project_init(&p);
    for(guint i=1;i<STUDIO_MAX_SCENES;i++) g_assert_nonnull(project_scene_add(&p,NULL));
    g_assert_null(project_scene_add(&p,NULL));g_assert_true(project_validate(&p,&error));g_assert_no_error(error);project_clear(&p);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    char *build = g_canonicalize_filename("build", NULL);
    char *template = g_build_filename(build, "project-tests-XXXXXX", NULL);
    test_root = g_mkdtemp(template); g_assert_nonnull(test_root);
    g_test_add_func("/project/scene-snapshot-isolation",scene_snapshot_isolation);
    g_test_add_func("/export/scene-module-transition",scene_module_transition);
    g_test_add_func("/export/first-scene-example",first_scene_example);
    g_test_add_func("/export/shadow-ops-example",shadow_ops_example);
    g_test_add_func("/project/multiple-scenes",multiple_scenes);
    g_test_add_func("/project/emulator-command",emulator_command);
    g_test_add_func("/project/camera-objects",camera_objects);
    g_test_add_func("/project/roundtrip", roundtrip);
    g_test_add_func("/project/invalid-open-preserves-state", invalid_open_preserves_state);
    g_test_add_func("/project/save-failure-preserves-disk", save_failure_preserves_disk);
    g_test_add_func("/project/duplicate-ids-relative-paths", duplicate_ids_and_paths);
    g_test_add_func("/project/relocation", relocation);
    g_test_add_func("/export/sound-assets",sound_assets);
    g_test_add_func("/export/c89-runtime-scene", export_c89_scene);
    g_test_add_func("/export/empty-and-capacity", export_empty_and_capacity);
    g_test_add_func("/export/rejects-unsupported", export_rejects_unsupported);
    g_test_add_func("/assets/save-as-and-module",assets_save_as_module);
    g_test_add_func("/assets/vram-limits",vram_limits);
    g_test_add_func("/assets/native-tim-pixels", native_tim_pixels);
    g_test_add_func("/assets/uv-texel-mapping",uv_texel_mapping);
    g_test_add_func("/project/output-discovery", output_discovery);
    g_test_add_func("/project/projection-validation",projection_validation);
    g_test_add_func("/export/gameplay-behaviors", gameplay_behaviors);
    int result = g_test_run(); remove_tree(test_root); g_free(template); g_free(build); return result;
}
