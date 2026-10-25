#include "studio.h"
#include <math.h>
#include <string.h>
#include <glib/gstdio.h>
gboolean project_validate_scene(const Project *p,GError **error);

typedef struct { char *path; guint width, height; int x, y; const Asset *visual; } Texture;
static void texture_free(gpointer value) { Texture *t=value; g_free(t->path); g_free(t); }
static gboolean bad(GError **error, const char *message)
{ g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,message); return FALSE; }
static int texture_compare(gconstpointer a, gconstpointer b)
{
    const Texture *x=*(Texture *const *)a, *y=*(Texture *const *)b;
    return x->width!=y->width ? (x->width>y->width ? -1:1) : strcmp(x->path,y->path);
}
static int find_texture(GPtrArray *textures, const char *path)
{
    for(guint i=0;i<textures->len;i++) if(g_str_equal(((Texture*)g_ptr_array_index(textures,i))->path,path)) return i;
    return -1;
}
static gboolean write_file(const char *dir,const char *name,const char *data,GError **error)
{
    char *path=g_build_filename(dir,name,NULL);
    gboolean ok=g_file_set_contents_full(path,data,-1,G_FILE_SET_CONTENTS_CONSISTENT,0644,error);
    g_free(path); return ok;
}

typedef struct {char *name;GBytes *bytes;guint32 rate;} Sound;
static void sound_free(gpointer data)
{Sound *s=data;g_free(s->name);g_bytes_unref(s->bytes);g_free(s);}
static guint32 sound_be32(const guint8 *p)
{return (guint32)p[0]<<24 | (guint32)p[1]<<16 | (guint32)p[2]<<8 | p[3];}
static gboolean collect_sounds(const Project *p,const char *module,GPtrArray *sounds,GError **error)
{
    GRegex *regex=g_regex_new("@sound[ \t]+([^ \t\r\n*]+)",0,0,NULL);GMatchInfo *matches=NULL;
    g_regex_match(regex,module,0,&matches);gboolean ok=TRUE;
    while(g_match_info_matches(matches) && ok) {
        char *name=g_match_info_fetch(matches,1);gboolean found=FALSE;
        for(guint k=0;k<sounds->len;k++) if(g_str_equal(name,((Sound *)g_ptr_array_index(sounds,k))->name)) found=TRUE;
        if(!g_regex_match_simple("^[A-Za-z0-9_-]{1,8}\\.[vV][aA][gG]$",name,0,0))
            ok=bad(error,"@sound requires a DOS 8.3 VAG filename in the project data folder.");
        if(ok && !found) {
            char *path=asset_path(p,name),*bytes=NULL;gsize size=0;
            ok=path && g_file_get_contents(path,&bytes,&size,error);g_free(path);
            const guint8 *v=(const guint8 *)bytes;
            if(ok && (size<80 || size>0x7eff0+48 || memcmp(v,"VAGp",4) || sound_be32(v+4)!=0x20 ||
                sound_be32(v+12)!=size-48 || ((size-48)&15) || sound_be32(v+16)<1000 || sound_be32(v+16)>44100))
                ok=bad(error,"Invalid @sound VAG: expected mono VAGp v0x20 with complete SPU ADPCM blocks.");
            gboolean loop=ok && v[size-15]==3;
            if(loop && size<96) ok=bad(error,"Looping VAG needs a lead-in, loop-start and end block.");
            for(gsize offset=48;ok && offset<size;offset+=16) {
                guint8 flag=offset+16==size?(loop?3:1):(loop && offset==64?4:0);
                if((v[offset]>>4)>4 || (v[offset]&15)>12 || v[offset+1]!=flag)
                    ok=bad(error,"@sound requires a one-shot or full-sample looping VAG with valid ADPCM blocks.");
            }
            if(ok) {
                Sound *sound=g_new0(Sound,1);sound->name=g_strdup(name);sound->rate=sound_be32(v+16);
                sound->bytes=g_bytes_new_take(bytes,size);bytes=NULL;g_ptr_array_add(sounds,sound);
            }
            g_free(bytes);
        }
        g_free(name);g_match_info_next(matches,NULL);
    }
    g_match_info_free(matches);g_regex_unref(regex);return ok;
}

static gboolean export_one(const Project *p,const char *directory,GError **error)
{
    if(!project_validate_scene(p,error)) return FALSE;
    GPtrArray *visuals=g_ptr_array_new_with_free_func((GDestroyNotify)asset_free);
    GPtrArray *textures=g_ptr_array_new_with_free_func(texture_free);
    GPtrArray *modules=g_ptr_array_new_with_free_func(g_free);
    GPtrArray *sounds=g_ptr_array_new_with_free_func(sound_free);
    gboolean ok=TRUE; guint triangles=0;
    for(guint i=0;i<p->objects->len && ok;i++) {
        Object *o=g_ptr_array_index(p->objects,i);
        Asset *a=NULL;
        for(int k=0;k<3;k++) if(lround(o->scale[k]*4096)<1 || lround(o->bounds[k])<1)
            ok=bad(error,"Scale or collision bounds below PS1 precision (minimum volume 0.5).");
        if(ok && o->kind!=OBJECT_CAMERA && (*o->asset || *o->texture)) {
            a=asset_load(p,o,error); ok=a!=NULL;
            if(ok) {
                triangles+=a->vertices->len/3;
                for(guint v=0;v<a->vertices->len && ok;v++) for(int k=0;k<3;k++)
                    if(fabs(g_array_index(a->vertices,AssetVertex,v).p[k]*o->scale[k])>32767)
                        ok=bad(error,"Scaled vertex outside the PS1 range [-32767,32767].");
                if(ok && *o->texture && find_texture(textures,o->texture)<0) {
                    Texture *t=g_new0(Texture,1); t->path=g_strdup(o->texture); t->width=a->width;t->height=a->height; t->visual=a;
                    g_ptr_array_add(textures,t);
                }
            }
        }
        g_ptr_array_add(visuals,a);
        if(ok && *o->module) {
            gboolean found=FALSE;
            for(guint m=0;m<modules->len;m++) if(g_str_equal(o->module,g_ptr_array_index(modules,m))) found=TRUE;
            if(!found) g_ptr_array_add(modules,g_strdup(o->module));
        }
    }
    if(ok && (triangles>4096 || textures->len>6 || modules->len>64)) ok=bad(error,"Scene limits: 4096 triangles, 6 textures, 64 distinct modules.");
    g_ptr_array_sort(textures,texture_compare);
    gboolean used[6]={FALSE,FALSE,FALSE,FALSE,FALSE,FALSE};
    const int sx[]={320,576,832,320,576,832},sy[]={0,0,0,256,256,256};
    for(guint i=0;i<textures->len && ok;i++) {
        Texture *t=g_ptr_array_index(textures,i); int slot=-1;
        for(int k=0;k<6;k++) if(!used[k] && t->width<=(k%3==2?192U:256U)) {slot=k;break;}
        if(slot<0) ok=bad(error,"VRAM exhausted: only four textures can be wider than 192 pixels.");
        else { used[slot]=TRUE; t->x=sx[slot];t->y=sy[slot]; }
    }
    GPtrArray *module_contents=g_ptr_array_new_with_free_func(g_free);
    for(guint i=0;i<modules->len && ok;i++) {
        char *path=asset_path(p,g_ptr_array_index(modules,i)),*contents=NULL;gsize length;
        ok=path && g_file_get_contents(path,&contents,&length,error);g_free(path);
        if(!ok && (!error || !*error)) bad(error,"Missing C module.");
        if(ok && (length>256*1024 || !g_str_is_ascii(contents) || !strstr(contents,"psx_logic")))
            ok=bad(error,"Invalid module: maximum 256 KB ASCII and a const PsxLogic psx_logic symbol required.");
        if(ok) ok=collect_sounds(p,contents,sounds,error);
        g_ptr_array_add(module_contents,contents);
    }
    gsize sound_size=0;
    for(guint i=0;i<sounds->len;i++) sound_size+=(g_bytes_get_size(((Sound *)g_ptr_array_index(sounds,i))->bytes)-48+63)&~(gsize)63;
    if(ok && sound_size>0x7eff0) ok=bad(error,"Scene sounds exceed available SPU sample RAM (520176 bytes, reverb off).");
    if(!ok) goto done;
    GString *source=g_string_new("/* Generated by PSX Studio. ANSI C89. */\n#include \"scene.h\"\n\n");
    char *ascii=g_str_to_ascii(p->name,"C");
    for(guint i=0;ascii[i];i++) if(!g_ascii_isprint(ascii[i])) ascii[i]=' ';
    if(strlen(ascii)>30) ascii[30]=0;
    char *escaped=g_strescape(ascii,NULL);
    g_string_append_printf(source,"const char *studio_scene_name=\"%s\";\n",escaped);g_free(ascii);g_free(escaped);
    g_string_append_printf(source,"unsigned int studio_texture_count=%u;\nPsxTexture studio_textures[%u]={\n",textures->len,MAX(1,textures->len));
    if(!textures->len) g_string_append(source,"{0,0,0,0,0,0,0,0}\n");
    for(guint i=0;i<textures->len;i++) {
        Texture *t=g_ptr_array_index(textures,i);
        g_string_append_printf(source,"{\"ASSETS\\\\T%03u.TIM\",%d,%d,%u,%u,0,0,0}%s\n",i,t->x,t->y,t->width,t->height,i+1<textures->len?",":"");
    }
    g_string_append(source,"};\n");
    /* Word arrays keep DMA sources aligned on both Psy-Q and host builds. */
    for(guint i=0;i<sounds->len;i++) {
        Sound *sound=g_ptr_array_index(sounds,i);gsize size;const guint8 *v=g_bytes_get_data(sound->bytes,&size);
        g_string_append_printf(source,"static const unsigned int sound_%u_%u[%zu]={\n",p->scene_id,i,((size-48+63)&~(gsize)63)/4);
        for(gsize j=48;j<size;j+=4) {
            guint32 word=(guint32)v[j] | (guint32)v[j+1]<<8 | (guint32)v[j+2]<<16 | (guint32)v[j+3]<<24;
            g_string_append_printf(source,"0x%08xU,%s",word,((j-48)/4)%8==7?"\n":"");
        }
        g_string_append(source,"\n};\n");
    }
    if(sounds->len) {
        g_string_append_printf(source,"static PsxSound sound_bank_%u[%u]={\n",p->scene_id,sounds->len);
        for(guint i=0;i<sounds->len;i++) {
            Sound *sound=g_ptr_array_index(sounds,i);
            g_string_append_printf(source,"{\"%s\",(const unsigned char *)sound_%u_%u,%zuUL,%uUL,0}%s\n",
                sound->name,p->scene_id,i,(g_bytes_get_size(sound->bytes)-48+63)&~(gsize)63,sound->rate,i+1<sounds->len?",":"");
        }
        g_string_append(source,"};\n");
    }
    for(guint i=0;i<modules->len;i++) g_string_append_printf(source,"extern const PsxLogic studio_logic_%u;\n",i);
    for(guint i=0;i<visuals->len;i++) {
        Asset *a=g_ptr_array_index(visuals,i);Object *o=g_ptr_array_index(p->objects,i);
        if(!a) continue;
        g_string_append_printf(source,"static const PsxVertex vertices_%u[%u]={\n",i,a->vertices->len);
        for(guint v=0;v<a->vertices->len;v++) {
            AssetVertex *vertex=&g_array_index(a->vertices,AssetVertex,v);
            g_string_append_printf(source,"{{%ldL,%ldL,%ldL},{%ld,%ld}}%s\n",lround(vertex->p[0]),lround(vertex->p[1]),lround(vertex->p[2]),
                asset_uv_texel(vertex->uv[0],a->width),asset_uv_texel(vertex->uv[1],a->height),v+1<a->vertices->len?",":"");
        }
        g_string_append_printf(source,"};\nstatic const PsxMesh mesh_%u={vertices_%u,%u,%d};\n",i,i,a->vertices->len,*o->texture?find_texture(textures,o->texture):-1);
    }
    g_string_append_printf(source,"static const PsxObject object_defaults[%u]={\n",MAX(1,p->objects->len));
    if(!p->objects->len) g_string_append(source,"{0}\n");
    for(guint i=0;i<p->objects->len;i++) {
        Object *o=g_ptr_array_index(p->objects,i);
        g_string_append_printf(source,"{%uU,%d,1,%d,",o->id,o->kind,o->solid?1:0);
        const double *vectors[]={o->position,o->rotation,o->scale,o->bounds,o->collider_offset};
        for(int group=0;group<5;group++) {
            g_string_append_c(source,'{');
            for(int k=0;k<3;k++) {
                double value=vectors[group][k];if(group==1) value=fmod(value,360)*4096/360;else if(group==2) value*=4096;
                g_string_append_printf(source,"%s%ldL",k?",":"",lround(value));
            }
            g_string_append(source,"},");
        }
        g_string_append_printf(source,"%uU,%uU,{%ldL,%ldL,%ldL},0,",o->follow_target,o->target_scene,lround(o->follow_offset[0]),lround(o->follow_offset[1]),lround(o->follow_offset[2]));
        int module=-1;for(guint m=0;m<modules->len;m++) if(g_str_equal(o->module,g_ptr_array_index(modules,m))) module=m;
        if(module>=0) g_string_append_printf(source,"&studio_logic_%d,0,",module);else g_string_append(source,"0,0,");
        if(g_ptr_array_index(visuals,i)) g_string_append_printf(source,"&mesh_%u,",i);else g_string_append(source,"0,");
        g_string_append_printf(source,"%d,%d,%d,%ldL,%d,0}%s\n",o->input_mode,o->collision_action,o->speed,lround(o->camera_zoom*4096),o->camera_projection,i+1<p->objects->len?",":"");
    }
    g_string_append(source,"};\n");
    g_string_append_printf(source,"static PsxObject objects[%u];\nstatic PsxScene scene;\nPsxScene *studio_scene_load(void)\n{\nscene.objects=objects;scene.count=%u;\n",MAX(1,p->objects->len),p->objects->len);
    if(sounds->len) g_string_append_printf(source,"scene.sounds=sound_bank_%u;scene.sound_count=%u;scene.play_sound=0;scene.play_music=0;\n",p->scene_id,sounds->len);
    else g_string_append(source,"scene.sounds=0;scene.sound_count=0;scene.play_sound=0;scene.play_music=0;\n");
    for(int k=0;k<3;k++) g_string_append_printf(source,"scene.camera[%d]=%ldL;\n",k,lround(p->camera[k]+(k==2?600:0)));
    g_string_append_printf(source,"scene.yaw=%ld;scene.pitch=%ld;scene.zoom=%ldL;\n",lround(p->view[0]*4096/360),lround(p->view[1]*4096/360),lround(p->view[2]*4096));
    g_string_append_printf(source,"scene.projection=%d;scene.active_camera=%uU;scene.camera_eye=0;scene.show_colliders=%d;\n",p->projection,p->active_camera,p->show_colliders);
    g_string_append_printf(source,"memcpy(objects,object_defaults,%u*sizeof(PsxObject));\n",p->objects->len);
    g_string_append(source,"psx_scene_init(&scene);return &scene;\n}\n");
    const char *header="#ifndef STUDIO_SCENE_H\n#define STUDIO_SCENE_H\n#include \"object.h\"\nextern const char *studio_scene_name;\nextern unsigned int studio_texture_count;\nextern PsxTexture studio_textures[];\nPsxScene *studio_scene_load(void);\n#endif\n";
    if(g_mkdir_with_parents(directory,0755)!=0) ok=bad(error,"Cannot create the export directory.");
    if(ok) ok=write_file(directory,"scene.c",source->str,error) && write_file(directory,"scene.h",header,error);
    g_string_free(source,TRUE);
    GString *manifest=g_string_new("{\"textures\":[");
    for(guint i=0;i<textures->len;i++) {
        Texture *t=g_ptr_array_index(textures,i);JsonNode *node=json_node_new(JSON_NODE_VALUE);json_node_set_string(node,t->path);char *path=json_to_string(node,FALSE);json_node_free(node);
        g_string_append_printf(manifest,"%s{\"path\":%s,\"x\":%d,\"y\":%d,\"width\":%u,\"height\":%u,\"file\":\"T%03u.TIM\"}",i?",":"",path,t->x,t->y,t->width,t->height,i);g_free(path);
    }
    for(guint i=0;i<textures->len && ok;i++) {
        Texture *t=g_ptr_array_index(textures,i);
        char *name=g_strdup_printf("T%03u.TIM",i), *path=g_build_filename(directory,name,NULL);
        ok=asset_write_tim(t->visual,path,t->x,t->y,error);
        g_free(path);g_free(name);
    }
    g_string_append(manifest,"],\"modules\":[");
    for(guint i=0;i<modules->len && ok;i++) {
        char *name=g_strdup_printf("L%03u.C",i);
        ok=write_file(directory,name,g_ptr_array_index(module_contents,i),error);g_free(name);
        name=g_strdup_printf("M%03u.C",i);
        char *wrapper=g_strdup_printf("#define psx_logic studio_logic_%u\n#include \"L%03u.C\"\n",i,i);
        if(ok) ok=write_file(directory,name,wrapper,error);
        g_string_append_printf(manifest,"%s\"%s\"",i?",":"",name);g_free(name);g_free(wrapper);
    }
    g_string_append_printf(manifest,"],\"triangles\":%u}\n",triangles);
    if(ok) ok=write_file(directory,"assets.json",manifest->str,error);
    g_string_free(manifest,TRUE);
 done:
    g_ptr_array_unref(sounds);g_ptr_array_unref(visuals);g_ptr_array_unref(textures);g_ptr_array_unref(modules);g_ptr_array_unref(module_contents);
    return ok;
}

static void remove_stage(const char *directory)
{
    GDir *d=g_dir_open(directory,0,NULL);const char *entry;
    if(d) {while((entry=g_dir_read_name(d))) {char *file=g_build_filename(directory,entry,NULL);if(g_file_test(file,G_FILE_TEST_IS_DIR)) remove_stage(file);else g_unlink(file);g_free(file);}g_dir_close(d);}
    g_rmdir(directory);
}
static char *read_stage(const char *directory,const char *name,GError **error)
{ char *path=g_build_filename(directory,name,NULL),*text=NULL;g_file_get_contents(path,&text,NULL,error);g_free(path);return text; }

/* Keep immutable geometry/sample bytes once in the combined translation unit.
 * Mesh texture IDs, object state and writable SPU bank addresses stay per Scene.
 * Exact initializer equality matters: matching filenames alone is insufficient. */
static void share_arrays(GString *code,GHashTable *shared,GHashTable *aliases,
                         guint level,guint *resident_triangles)
{
    GRegex *regex=g_regex_new("static const (PsxVertex|unsigned int) ([a-zA-Z0-9_]+)\\[([0-9]+)\\]=\\{\n(.*?)\\};\n",G_REGEX_DOTALL,0,NULL);
    GMatchInfo *match=NULL;g_regex_match(regex,code->str,0,&match);
    GString *result=g_string_new("");gint previous=0;
    while(g_match_info_matches(match)) {
        gint start,end;g_match_info_fetch_pos(match,0,&start,&end);
        char *type=g_match_info_fetch(match,1),*name=g_match_info_fetch(match,2);
        char *count=g_match_info_fetch(match,3),*body=g_match_info_fetch(match,4);
        char *key=g_strconcat(type,"/",count,"/",body,NULL);
        const char *canonical=g_hash_table_lookup(shared,key);
        gboolean geometry=g_str_equal(type,"PsxVertex");
        g_string_append_len(result,code->str+previous,start-previous);
        if(!canonical) {
            char *identifier=geometry?g_strdup_printf("level_%u_%s",level,name):g_strdup(name);
            g_hash_table_insert(shared,key,identifier);key=NULL;canonical=identifier;
            g_string_append_len(result,code->str+start,end-start);
            if(geometry) *resident_triangles+=(guint)g_ascii_strtoull(count,NULL,10)/3;
        } else if(!geometry) g_string_append_printf(result,"#define %s %s\n",name,canonical);
        if(geometry) g_hash_table_insert(aliases,g_strdup(name),g_strdup(canonical));
        previous=end;g_free(key);g_free(type);g_free(name);g_free(count);g_free(body);
        g_match_info_next(match,NULL);
    }
    g_string_append(result,code->str+previous);g_string_assign(code,result->str);
    g_string_free(result,TRUE);g_match_info_free(match);g_regex_unref(regex);
}

gboolean project_export(const Project *p,const char *directory,GError **error)
{
    if(!project_validate(p,error)) return FALSE;
    char *stage=g_dir_make_tmp("psx-scenes-XXXXXX",error);if(!stage) return FALSE;
    GString *includes=g_string_new(""),*dispatch=g_string_new(""),*decl=g_string_new("");
    JsonArray *textures=json_array_new(),*modules=json_array_new();
    guint texture_id=0,module_id=0,max_objects=1,total_triangles=0;
    gboolean ok=TRUE;
    GHashTable *shared=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    for(guint n=0;n<p->scenes->len && ok;n++) {
        Scene *scene=g_ptr_array_index(p->scenes,n);Project view;
        ok=project_scene_view(p,n,&view,error);
        char *sub=g_strdup_printf("%s/scene-%u",stage,n);
        if(ok) ok=export_one(&view,sub,error);
        if(!ok) {project_clear(&view);g_free(sub);break;}
        max_objects=MAX(max_objects,view.objects->len);
        char *manifest_text=read_stage(sub,"assets.json",error),*code=read_stage(sub,"scene.c",error);
        JsonParser *parser=json_parser_new();
        if(!manifest_text || !code || !json_parser_load_from_data(parser,manifest_text,-1,error)) ok=FALSE;
        if(ok) {
            JsonObject *manifest=json_node_get_object(json_parser_get_root(parser));
            JsonArray *ts=json_object_get_array_member(manifest,"textures"),*ms=json_object_get_array_member(manifest,"modules");
            GString *rewritten=g_string_new(code);
            GHashTable *aliases=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
            share_arrays(rewritten,shared,aliases,n,&total_triangles);
            g_string_replace(rewritten,"#include \"scene.h\"","#include \"object.h\"",0);
            char *allocation=g_strdup_printf("static PsxObject objects[%u];\n",MAX(1,view.objects->len));
            g_string_replace(rewritten,allocation,"",0);g_free(allocation);
            g_string_replace(rewritten,"static PsxScene scene;\n","",0);
            g_string_replace(rewritten,"psx_scene_init(&scene);return &scene;","return &scene;",0);
            g_string_replace(rewritten,"PsxScene *studio_scene_load(void)","static PsxScene *studio_scene_load(void)",0);
            g_string_replace(rewritten,"const char *studio_scene_name=","static const char *studio_scene_name=",0);
            g_string_replace(rewritten,"unsigned int studio_texture_count=","static unsigned int studio_texture_count=",0);
            g_string_replace(rewritten,"PsxTexture studio_textures[","static PsxTexture studio_textures[",0);
            for(guint k=0;k<json_array_get_length(ts) && ok;k++) {
                JsonObject *t=json_array_get_object_element(ts,k);
                char *old=g_strdup(json_object_get_string_member(t,"file")),*new=g_strdup_printf("T%03u.TIM",texture_id),*placeholder=g_strdup_printf("Q%03u.TIM",texture_id);
                g_string_replace(rewritten,old,placeholder,0);
                char *bytes=NULL;gsize length=0;char *from=g_build_filename(sub,old,NULL),*to=g_build_filename(stage,new,NULL);
                ok=g_file_get_contents(from,&bytes,&length,error) && g_file_set_contents(to,bytes,length,error);
                g_free(bytes);g_free(from);g_free(to);
                JsonNode *copy=json_node_copy(json_array_get_element(ts,k));json_object_set_string_member(json_node_get_object(copy),"file",new);
                json_array_add_element(textures,copy);texture_id++;
                g_free(old);g_free(new);g_free(placeholder);
            }
            for(guint k=0;k<texture_id;k++) {char *q=g_strdup_printf("Q%03u.TIM",k),*t=g_strdup_printf("T%03u.TIM",k);g_string_replace(rewritten,q,t,0);g_free(q);g_free(t);}
            g_string_append_printf(includes,"#define scene studio_state\n#define object_defaults level_%u_defaults\n#define studio_scene_name level_%u_name\n#define studio_textures level_%u_textures\n#define studio_texture_count level_%u_texture_count\n#define studio_scene_load level_%u_load\n",n,n,n,n,n);
            for(guint k=0;k<view.objects->len;k++) {
                char *raw=g_strdup_printf("vertices_%u",k),*fallback=g_strdup_printf("level_%u_vertices_%u",n,k);
                const char *canonical=g_hash_table_lookup(aliases,raw);
                g_string_append_printf(includes,"#define vertices_%u %s\n#define mesh_%u level_%u_mesh_%u\n",k,canonical?canonical:fallback,k,n,k);
                g_free(raw);g_free(fallback);
            }
            for(guint k=0;k<json_array_get_length(ms) && ok;k++) {
                g_string_append_printf(includes,"#define studio_logic_%u level_%u_logic_%u\n",k,n,k);
                char *old=g_strdup_printf("L%03u.C",k),*contents=read_stage(sub,old,error),*name=g_strdup_printf("L%03u.C",module_id);
                if(contents) ok=write_file(stage,name,contents,error);else ok=FALSE;
                g_free(old);g_free(contents);g_free(name);
                name=g_strdup_printf("M%03u.C",module_id);
                char *wrapper=g_strdup_printf("#define psx_logic level_%u_logic_%u\n#include \"L%03u.C\"\n",n,k,module_id);
                if(ok) ok=write_file(stage,name,wrapper,error);
                json_array_add_string_element(modules,name);g_free(name);g_free(wrapper);module_id++;
            }
            char *name=g_strdup_printf("S%03u.C",n);
            if(ok) ok=write_file(stage,name,rewritten->str,error);
            g_string_append_printf(includes,"#include \"%s\"\n#undef scene\n#undef object_defaults\n#undef studio_scene_name\n#undef studio_textures\n#undef studio_texture_count\n#undef studio_scene_load\n",name);g_free(name);
            for(guint k=0;k<view.objects->len;k++) g_string_append_printf(includes,"#undef vertices_%u\n#undef mesh_%u\n",k,k);
            for(guint k=0;k<json_array_get_length(ms);k++) g_string_append_printf(includes,"#undef studio_logic_%u\n",k);
            g_string_append_printf(dispatch,"case %uU: level_%u_load();studio_scene_name=level_%u_name;studio_textures=level_%u_textures;studio_texture_count=level_%u_texture_count;break;\n",scene->id,n,n,n,n);
            g_string_append_printf(decl,"case %uU:return 1;\n",scene->id);
            g_string_free(rewritten,TRUE);g_hash_table_unref(aliases);
        }
        g_object_unref(parser);g_free(code);g_free(manifest_text);project_clear(&view);g_free(sub);
    }
    g_hash_table_unref(shared);
    if(ok && (total_triangles>4096 || module_id>64)) ok=bad(error,"Project limits: 4096 resident triangles and 64 logic modules across all Scenes.");
    if(ok) {
        GString *source=g_string_new("/* Generated multi-Scene C89 game. */\n#include \"scene.h\"\n#include <string.h>\n");
        g_string_append_printf(source,"static PsxObject objects[%u];\nstatic PsxScene studio_state;\nconst char *studio_scene_name;\nPsxTexture *studio_textures;\nunsigned int studio_texture_count;\n",max_objects);
        g_string_append(source,includes->str);
        g_string_append_printf(source,"static int scene_exists(unsigned int id) {switch(id) {\n%s default:return 0;}}\n",decl->str);
        g_string_append_printf(source,"PsxScene *studio_scene_load_id(unsigned int id)\n{\nif(!scene_exists(id)) return 0;\npsx_scene_clear(&studio_state);\nmemset(objects,0,sizeof(objects));\nmemset(&studio_state,0,sizeof(studio_state));\nswitch(id) {\n%s}\nstudio_state.scene_id=id;studio_state.scene_exists=scene_exists;\npsx_scene_init(&studio_state);return &studio_state;\n}\n",dispatch->str);
        g_string_append_printf(source,"PsxScene *studio_scene_load(void) {return studio_scene_load_id(%uU);}\n",p->startup_scene);
        g_string_append(source,"int studio_scene_apply_change(void) {unsigned int id;id=studio_state.pending_scene;if(!id) return 0;return studio_scene_load_id(id)!=0;}\n");
        const char *header="#ifndef STUDIO_SCENE_H\n#define STUDIO_SCENE_H\n#include \"object.h\"\nextern const char *studio_scene_name;\nextern unsigned int studio_texture_count;\nextern PsxTexture *studio_textures;\nPsxScene *studio_scene_load(void);\nPsxScene *studio_scene_load_id(unsigned int id);\nint studio_scene_apply_change(void);\n#endif\n";
        ok=write_file(stage,"scene.c",source->str,error) && write_file(stage,"scene.h",header,error);g_string_free(source,TRUE);
        JsonObject *manifest=json_object_new();json_object_set_array_member(manifest,"textures",json_array_ref(textures));json_object_set_array_member(manifest,"modules",json_array_ref(modules));
        json_object_set_int_member(manifest,"triangles",total_triangles);
        JsonNode *node=json_node_new(JSON_NODE_OBJECT);json_node_take_object(node,manifest);char *text=json_to_string(node,TRUE);
        if(ok) ok=write_file(stage,"assets.json",text,error);
        g_free(text);json_node_free(node);
        if(ok && g_mkdir_with_parents(directory,0755)!=0) ok=bad(error,"Cannot create export directory.");
        GDir *d=g_dir_open(stage,0,error);const char *entry;
        if(!d) ok=FALSE;
        while(d && ok && (entry=g_dir_read_name(d))) {
            char *from=g_build_filename(stage,entry,NULL);
            if(g_file_test(from,G_FILE_TEST_IS_REGULAR)) {
                char *bytes=NULL,*to=g_build_filename(directory,entry,NULL);gsize length=0;
                ok=g_file_get_contents(from,&bytes,&length,error) && g_file_set_contents(to,bytes,length,error);g_free(bytes);g_free(to);
            }
            g_free(from);
        }
        if(d) g_dir_close(d);
    }
    g_string_free(includes,TRUE);g_string_free(dispatch,TRUE);g_string_free(decl,TRUE);
    json_array_unref(textures);json_array_unref(modules);remove_stage(stage);g_free(stage);return ok;
}
