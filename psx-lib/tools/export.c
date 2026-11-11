#include "core.h"
#include <math.h>
#include <string.h>
#include <glib/gstdio.h>

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

/* Bundle resource-list quoted C/header includes before export. Engine headers
 * remain normal includes; no game-specific filename is special-cased. Expanded
 * sources are also the build snapshot/cache input, so nested edits invalidate
 * compilation without copying undeclared files into the SDK workspace. */
static gboolean source_path_safe(const char *path,GError **error)
{
    if(g_file_test(path,G_FILE_TEST_IS_SYMLINK))
        return bad(error,"ResourceContext source includes cannot traverse symbolic links.");
    char *parent=g_path_get_dirname(path);
    gboolean ok=!strcmp(parent,path) || source_path_safe(parent,error);
    g_free(parent);return ok;
}
static char *read_source_recursive(const ResourceContext *p,const char *relative,
    GHashTable *active,guint depth,gsize *budget,GError **error)
{
    if(depth>16 || g_path_is_absolute(relative) || strstr(relative,"..") || !g_regex_match_simple("^[A-Za-z0-9_./-]+\\.[cChH]$",relative,0,0)) {
        bad(error,"Custom includes require relative .c/.h paths without parent traversal and at most 16 nesting levels.");return NULL;
    }
    if(g_hash_table_contains(active,relative)) {bad(error,"Cyclic resource list include.");return NULL;}
    char *path=asset_path(p,relative),*source=NULL;gsize length=0;
    gboolean ok=path && source_path_safe(path,error) && g_file_get_contents(path,&source,&length,error);g_free(path);
    if(ok && (length>256*1024 || *budget+length>1024*1024 || strlen(source)!=length || !g_str_is_ascii(source)))
        ok=bad(error,"ResourceContext source includes must be ASCII and fit the 1 MiB expansion budget.");
    if(!ok) {g_free(source);return NULL;}
    *budget+=length;g_hash_table_add(active,g_strdup(relative));
    GRegex *include=g_regex_new("(?m)^[ \\t]*#[ \\t]*include[ \\t]+\"([^\"\\r\\n]+)\"[ \\t]*(?:/\\*[^\\r\\n]*\\*/)?[ \\t]*\\r?$",0,0,NULL);
    GMatchInfo *matches=NULL;g_regex_match(include,source,0,&matches);
    GString *out=g_string_new("");gint end=0;
    while(ok && g_match_info_matches(matches)) {
        gint start,stop;g_match_info_fetch_pos(matches,0,&start,&stop);
        char *name=g_match_info_fetch(matches,1);
        gboolean runtime=!strcmp(name,"object.h") || !strcmp(name,"camera.h") || !strcmp(name,"psx.h") ||
            !strcmp(name,"frame.h") || !strcmp(name,"render.h") || !strcmp(name,"timing.h") ||
            !strcmp(name,"fps.h") || !strcmp(name,"save.h");
        g_string_append_len(out,source+end,start-end);
        if(runtime) g_string_append_len(out,source+start,stop-start);
        else {
            char *parent=g_path_get_dirname(relative),*joined=g_build_filename(parent,name,NULL);
            g_strdelimit(joined,"\\",'/');
            char *nested=NULL;
            if(g_path_is_absolute(name) || strstr(name,"..")) bad(error,"Resource list includes cannot escape their asset root.");
            else nested=read_source_recursive(p,joined,active,depth+1,budget,error);
            g_free(parent);g_free(joined);
            if(!nested) ok=FALSE;
            else {g_string_append(out,nested);g_free(nested);}
        }
        end=stop;g_free(name);g_match_info_next(matches,NULL);
    }
    if(ok) g_string_append(out,source+end);
    g_match_info_free(matches);g_regex_unref(include);g_hash_table_remove(active,relative);g_free(source);
    if(!ok) {g_string_free(out,TRUE);return NULL;}
    return g_string_free(out,FALSE);
}
char *resource_read_source(const ResourceContext *p,const char *relative,GError **error)
{
    GHashTable *active=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
    gsize budget=0;char *source=read_source_recursive(p,relative,active,0,&budget,error);
    g_hash_table_unref(active);return source;
}

typedef struct {char *name;GBytes *bytes;guint32 rate;} Sound;
static void sound_free(gpointer data)
{Sound *s=data;g_free(s->name);g_bytes_unref(s->bytes);g_free(s);}
static guint32 sound_be32(const guint8 *p)
{return (guint32)p[0]<<24 | (guint32)p[1]<<16 | (guint32)p[2]<<8 | p[3];}
static gboolean collect_sounds(const ResourceContext *p,const char *module,GPtrArray *sounds,GError **error)
{
    GRegex *regex=g_regex_new("@sound[ \t]+([^ \t\r\n*]+)",0,0,NULL);GMatchInfo *matches=NULL;
    g_regex_match(regex,module,0,&matches);gboolean ok=TRUE;
    while(g_match_info_matches(matches) && ok) {
        char *name=g_match_info_fetch(matches,1);gboolean found=FALSE;
        for(guint k=0;k<sounds->len;k++) if(g_str_equal(name,((Sound *)g_ptr_array_index(sounds,k))->name)) found=TRUE;
        if(!g_regex_match_simple("^[A-Za-z0-9_-]{1,8}\\.[vV][aA][gG]$",name,0,0))
            ok=bad(error,"@sound requires a DOS 8.3 VAG filename in the resource asset folder.");
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

/* Pack within 16-bit texture pages, keeping UV endpoints inside one page.
 * Framebuffers occupy X<320; the final page excludes the font at X>=960. */
static gboolean pack_plates(GPtrArray *textures,gboolean base,GError **error)
{
    int skyline[6][256]={{0}};
    const int px[6]={320,576,832,320,576,832},py[6]={0,0,0,256,256,256};
    const int widths[6]={256,256,192,256,256,128};
    if(base) {for(int x=0;x<256;x++) skyline[0][x]=256;for(int x=0;x<64;x++) skyline[1][x]=256;}
    g_ptr_array_sort(textures,texture_compare);
    for(guint i=0;i<textures->len;i++) {
        Texture *t=g_ptr_array_index(textures,i);gboolean placed=FALSE;
        for(int page=0;page<6 && !placed;page++) {
            int best_x=-1,best_y=257;
            for(int x=0;x+(int)t->width<=widths[page];x+=64) {
                int y=0;for(guint k=0;k<t->width;k++) y=MAX(y,skyline[page][x+k]);
                if(y+(int)t->height<=256 && y<best_y) {best_x=x;best_y=y;}
            }
            if(best_x>=0) {
                t->x=px[page]+best_x;t->y=py[page]+best_y;
                for(guint k=0;k<t->width;k++) skyline[page][best_x+k]=best_y+t->height;
                placed=TRUE;
            }
        }
        if(!placed) return bad(error,"VRAM exhausted by object textures and depth layers. Reduce image sizes or the opaque area of a layer.");
    }
    return TRUE;
}
static gboolean export_bank(const ResourceContext *p,const char *directory,const char *resource_sounds,GError **error)
{
    if(!resource_context_validate(p,error)) return FALSE;
    GPtrArray *visuals=g_ptr_array_new_with_free_func((GDestroyNotify)asset_free);
    GPtrArray *textures=g_ptr_array_new_with_free_func(texture_free);
    GPtrArray *sounds=g_ptr_array_new_with_free_func(sound_free);
    gboolean ok=TRUE; guint triangles=0;
    Asset *background=NULL,*background_tiles[2]={NULL,NULL};
    guint background_texture=0,layer_count=0;
    GPtrArray *layer_images=g_ptr_array_new_with_free_func((GDestroyNotify)asset_free);
    struct {Texture *texture;int x,y,depth;} layers[8];
    for(guint i=0;i<p->models->len && ok;i++) {
        ResourceModel *o=g_ptr_array_index(p->models,i);
        Asset *a=NULL;
        if(ok) {
            a=asset_load(p,o,error); ok=a!=NULL;
            if(ok) {
                triangles+=a->vertices->len/3;
                if(ok && *o->texture && find_texture(textures,o->texture)<0) {
                    Texture *t=g_new0(Texture,1); t->path=g_strdup(o->texture); t->width=a->width;t->height=a->height; t->visual=a;
                    g_ptr_array_add(textures,t);
                }
            }
        }
        g_ptr_array_add(visuals,a);
    }
    if(ok && (triangles>4096 || textures->len>6)) ok=bad(error,"Resource bank limits: 4096 triangles and 6 model textures.");
    if(ok && *p->background) {
        background=asset_load_background(p,error);if(!background) {ok=FALSE;goto done;}
    }
    for(guint i=0;ok && i<p->background_layers->len;i++) {
        ResourceLayer *entry=g_ptr_array_index(p->background_layers,i);
        Asset *full=asset_load_background_layer(p,entry->path,error);
        if(!full) {ok=FALSE;break;}
        int left=320,top=256,right=-1,bottom=-1;
        for(int y=0;y<256;y++) for(int x=0;x<320;x++) if(full->pixels[(y*320+x)*4+3]>=128) {
            left=MIN(left,x);right=MAX(right,x);top=MIN(top,y);bottom=MAX(bottom,y);
        }
        if(right<left) {asset_free(full);ok=bad(error,"Depth layer is fully transparent.");break;}
        for(int x=left;x<=right;x+=256) {
            Asset *tile=g_new0(Asset,1);tile->width=MIN(256,right-x+1);tile->height=bottom-top+1;
            tile->pixels=g_malloc(tile->width*tile->height*4);g_ptr_array_add(layer_images,tile);
            for(guint y=0;y<tile->height;y++) memcpy(tile->pixels+y*tile->width*4,full->pixels+((top+y)*320+x)*4,tile->width*4);
            Texture *t=g_new0(Texture,1);t->path=g_strdup_printf("%s#layer%u-%d",entry->path,i,x);
            t->width=tile->width;t->height=tile->height;t->visual=tile;g_ptr_array_add(textures,t);
            layers[layer_count].texture=t;layers[layer_count].x=x;layers[layer_count].y=top;layers[layer_count].depth=entry->depth;layer_count++;
        }
        asset_free(full);
    }
    if(ok && (background || layer_count)) ok=pack_plates(textures,background!=NULL,error);
    else if(ok) {
        g_ptr_array_sort(textures,texture_compare);
        gboolean used[6]={FALSE,FALSE,FALSE,FALSE,FALSE,FALSE};
        const int sx[]={320,576,832,320,576,832},sy[]={0,0,0,256,256,256};
        for(guint i=0;i<textures->len && ok;i++) {
            Texture *t=g_ptr_array_index(textures,i);int slot=-1;
            for(int k=0;k<6;k++) if(!used[k] && t->width<=(k%3==2?192U:256U)) {slot=k;break;}
            if(slot<0) ok=bad(error,"VRAM exhausted: only four textures can be wider than 192 pixels.");
            else {used[slot]=TRUE;t->x=sx[slot];t->y=sy[slot];}
        }
    }

    if(background && ok) {
        background_texture=textures->len+1;
        for(guint part=0;part<2;part++) {
            Asset *tile=g_new0(Asset,1);tile->width=part?64:256;tile->height=256;
            tile->pixels=g_malloc(tile->width*256*4);background_tiles[part]=tile;
            for(guint y=0;y<256;y++) memcpy(tile->pixels+y*tile->width*4,
                background->pixels+(y*320+(part?256:0))*4,tile->width*4);
            Texture *t=g_new0(Texture,1);t->path=g_strdup(p->background);
            t->width=tile->width;t->height=256;t->x=part?576:320;t->y=0;t->visual=tile;
            g_ptr_array_add(textures,t);
        }
    }
    if(ok && resource_sounds) ok=collect_sounds(p,resource_sounds,sounds,error);
    gsize sound_size=0;
    for(guint i=0;i<sounds->len;i++) sound_size+=(g_bytes_get_size(((Sound *)g_ptr_array_index(sounds,i))->bytes)-48+63)&~(gsize)63;
    if(ok && sound_size>0x7eff0) ok=bad(error,"Resource bank sounds exceed available SPU sample RAM (520176 bytes, reverb off).");
    if(!ok) goto done;
    GString *source=g_string_new("/* Generated by PSX Lib. ANSI C89. */\n#include \"assets.h\"\n\n");
    g_string_append_printf(source,"PsxTexture bank_textures[%u]={\n",MAX(1,textures->len));
    if(!textures->len) g_string_append(source,"{0,0,0,0,0,0,0,0}\n");
    for(guint i=0;i<textures->len;i++) {
        Texture *t=g_ptr_array_index(textures,i);
        g_string_append_printf(source,"{\"ASSETS\\\\T%03u.TIM\",%d,%d,%u,%u,0,0,0}%s\n",i,t->x,t->y,t->width,t->height,i+1<textures->len?",":"");
    }
    g_string_append(source,"};\n");
    /* Word arrays keep DMA sources aligned on both Psy-Q and host builds. */
    for(guint i=0;i<sounds->len;i++) {
        Sound *sound=g_ptr_array_index(sounds,i);gsize size;const guint8 *v=g_bytes_get_data(sound->bytes,&size);
        g_string_append_printf(source,"static const unsigned int sound_%u_%u[%zu]={\n",p->bank_id,i,((size-48+63)&~(gsize)63)/4);
        for(gsize j=48;j<size;j+=4) {
            guint32 word=(guint32)v[j] | (guint32)v[j+1]<<8 | (guint32)v[j+2]<<16 | (guint32)v[j+3]<<24;
            g_string_append_printf(source,"0x%08xU,%s",word,((j-48)/4)%8==7?"\n":"");
        }
        g_string_append(source,"\n};\n");
    }
    if(sounds->len) {
        g_string_append_printf(source,"static PsxSound sound_bank_%u[%u]={\n",p->bank_id,sounds->len);
        for(guint i=0;i<sounds->len;i++) {
            Sound *sound=g_ptr_array_index(sounds,i);
            g_string_append_printf(source,"{\"%s\",(const unsigned char *)sound_%u_%u,%zuUL,%uUL,0}%s\n",
                sound->name,p->bank_id,i,(g_bytes_get_size(sound->bytes)-48+63)&~(gsize)63,sound->rate,i+1<sounds->len?",":"");
        }
        g_string_append(source,"};\n");
    }
    for(guint i=0;i<visuals->len;i++) {
        Asset *a=g_ptr_array_index(visuals,i);ResourceModel *o=g_ptr_array_index(p->models,i);
        if(!a) continue;
        g_string_append_printf(source,"static const PsxVertex vertices_%u[%u]={\n",i,a->vertices->len);
        for(guint v=0;v<a->vertices->len;v++) {
            AssetVertex *vertex=&g_array_index(a->vertices,AssetVertex,v);
            g_string_append_printf(source,"{{%ldL,%ldL,%ldL},{%ld,%ld}}%s\n",lround(vertex->p[0]),lround(vertex->p[1]),lround(vertex->p[2]),
                asset_uv_texel(vertex->uv[0],a->width),asset_uv_texel(vertex->uv[1],a->height),v+1<a->vertices->len?",":"");
        }
        g_string_append_printf(source,"};\nstatic const PsxMesh mesh_%u={vertices_%u,%u,%d};\n",i,i,a->vertices->len,*o->texture?find_texture(textures,o->texture):-1);
    }
    if(g_mkdir_with_parents(directory,0755)!=0) ok=bad(error,"Cannot create the resource export directory.");
    if(ok) ok=write_file(directory,"bank.c",source->str,error);
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
    g_string_append_printf(manifest,"],\"background_texture\":%u,\"sound_count\":%u,\"backdrops\":[",background_texture,sounds->len);
    for(guint i=0;i<layer_count;i++) {
        guint index=0;while(index<textures->len && g_ptr_array_index(textures,index)!=layers[i].texture) index++;
        g_string_append_printf(manifest,"%s[%u,%d,%d,%u,%u,%d]",i?",":"",index,layers[i].x,layers[i].y,layers[i].texture->width,layers[i].texture->height,layers[i].depth);
    }
    g_string_append_printf(manifest,"],\"triangles\":%u}\n",triangles);
    if(ok) ok=write_file(directory,"assets.json",manifest->str,error);
    g_string_free(manifest,TRUE);
 done:
    asset_free(background);asset_free(background_tiles[0]);asset_free(background_tiles[1]);
    g_ptr_array_unref(layer_images);g_ptr_array_unref(sounds);g_ptr_array_unref(visuals);g_ptr_array_unref(textures);
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

/* Deduplicate immutable geometry/sample bytes across resource banks.
 * Mesh texture IDs, writable SPU bank addresses stay per bank.
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

/* Keep each shared ADPCM sample in its own translation unit. Psy-Q's legacy
 * assembler can crash on the combined resource tables plus large sample arrays.
 * The extern declarations retain word alignment and one canonical byte copy. */
static void separate_samples(GString *code,GPtrArray *samples)
{
    GRegex *regex=g_regex_new("static const unsigned int (sound_[0-9_]+)\\[([0-9]+)\\]=\\{\n.*?\\};\n",G_REGEX_DOTALL,0,NULL);
    GMatchInfo *match=NULL;g_regex_match(regex,code->str,0,&match);
    GString *result=g_string_new("");gint previous=0;
    while(g_match_info_matches(match)) {
        gint start,end;g_match_info_fetch_pos(match,0,&start,&end);
        char *name=g_match_info_fetch(match,1),*count=g_match_info_fetch(match,2);
        g_string_append_len(result,code->str+previous,start-previous);
        g_string_append_printf(result,"extern const unsigned int %s[%s];\n",name,count);
        /* Strip only the storage class; the array must have external linkage. */
        g_ptr_array_add(samples,g_strndup(code->str+start+7,end-start-7));
        previous=end;g_free(name);g_free(count);g_match_info_next(match,NULL);
    }
    g_string_append(result,code->str+previous);g_string_assign(code,result->str);
    g_string_free(result,TRUE);g_match_info_free(match);g_regex_unref(regex);
}

/* Keep immutable vertex arrays out of the bank translation unit.
 * The legacy Psy-Q assembler has a much lower practical input limit than the
 * runtime's geometry capacity. Each unique mesh is compiled exactly once. */
static void separate_geometry(GString *code,GPtrArray *samples,GHashTable *aliases)
{
    GRegex *regex=g_regex_new("static const PsxVertex ([a-zA-Z0-9_]+)\\[([0-9]+)\\]=\\{\n.*?\\};\n",G_REGEX_DOTALL,0,NULL);
    GMatchInfo *match=NULL;g_regex_match(regex,code->str,0,&match);
    GString *result=g_string_new("");gint previous=0;
    while(g_match_info_matches(match)) {
        gint start,end;g_match_info_fetch_pos(match,0,&start,&end);
        char *name=g_match_info_fetch(match,1),*count=g_match_info_fetch(match,2);
        g_string_append_len(result,code->str+previous,start-previous);
        g_string_append_printf(result,"extern const PsxVertex %s[%s];\n",name,count);
        /* Strip only the storage class; the array must have external linkage. */
        char *data=g_strndup(code->str+start+7,end-start-7);
        GString *unit=g_string_new("#include \"object.h\"\n");
        g_string_append(unit,data);g_free(data);
        const char *canonical=g_hash_table_lookup(aliases,name);
        if(canonical) g_string_replace(unit,name,canonical,0);
        g_ptr_array_add(samples,g_string_free(unit,FALSE));
        previous=end;g_free(name);g_free(count);g_match_info_next(match,NULL);
    }
    g_string_append(result,code->str+previous);g_string_assign(code,result->str);
    g_string_free(result,TRUE);g_match_info_free(match);g_regex_unref(regex);
}

static char *resource_field(GMatchInfo *match,int index)
{return g_match_info_fetch(match,index);}

gboolean resources_export(const char *list,const char *asset_root,const char *directory,GError **error)
{
    ResourceContext includes;resource_context_init(&includes);
    includes.asset_root=g_path_get_dirname(list);char *filename=g_path_get_basename(list);
    char *text=resource_read_source(&includes,filename,error);g_free(filename);resource_context_clear(&includes);
    if(!text) return FALSE;
    if(strlen(text)>1024*1024) {g_free(text);return bad(error,"Asset list must be ASCII, at most 1 MiB expanded.");}
    char *stage=g_dir_make_tmp("psx-resources-XXXXXX",error);if(!stage) {g_free(text);return FALSE;}
    GHashTable *shared=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    GHashTable *ids=g_hash_table_new(g_direct_hash,g_direct_equal);
    GPtrArray *samples=g_ptr_array_new_with_free_func(g_free),*geometry=g_ptr_array_new_with_free_func(g_free);
    JsonArray *textures=json_array_new(),*modules=json_array_new();
    GString *header=g_string_new("#ifndef PSX_RESOURCES_H\n#define PSX_RESOURCES_H\n#include \"object.h\"\nconst PsxResourceBank *psx_resources(unsigned int id);\n");
    GString *dispatch=g_string_new("#include \"assets.h\"\n"),*cases=g_string_new("");
    /* Deliberately restricted X-macro syntax, no C preprocessor/evaluation. */
    GRegex *line=g_regex_new("^PSX_(BANK|MODEL|SOUND|LAYER|END)\\((.*)\\)[ \\t]*$",0,0,NULL);
    GRegex *bank_args=g_regex_new("^([0-9]+),[ \\t]*\"([A-Za-z0-9_./-]*)\"$",0,0,NULL);
    GRegex *model_args=g_regex_new("^([0-9]+),[ \\t]*(mesh|sprite|billboard),[ \\t]*\"([A-Za-z0-9_./-]*)\",[ \\t]*\"([A-Za-z0-9_./-]*)\"$",0,0,NULL);
    GRegex *sound_args=g_regex_new("^\"([A-Za-z0-9_-]+[.][vV][aA][gG])\"$",0,0,NULL);
    GRegex *layer_args=g_regex_new("^\"([A-Za-z0-9_./-]+)\",[ \\t]*([0-9]+)$",0,0,NULL);
    char **lines=g_strsplit(text,"\n",-1);gboolean ok=TRUE,open=FALSE;guint id=0,bank_count=0,resident=0,texture_id=0;
    ResourceContext p;resource_context_init(&p);GString *sounds=g_string_new("");
    for(guint n=0;ok && lines[n];n++) {
        char *s=g_strstrip(lines[n]);if(!*s || g_str_has_prefix(s,"/*") || g_str_has_prefix(s,"//")) continue;
        GMatchInfo *m=NULL,*a=NULL;
        if(!g_regex_match(line,s,0,&m)) {ok=bad(error,"Invalid asset list line; expected PSX_BANK/MODEL/SOUND/LAYER/END.");g_match_info_free(m);break;}
        char *kind=resource_field(m,1),*args=resource_field(m,2);
        if(!strcmp(kind,"BANK") && !open && g_regex_match(bank_args,args,0,&a)) {
            char *number=resource_field(a,1),*background=resource_field(a,2);
            guint64 wide=g_ascii_strtoull(number,NULL,10);id=(guint)wide;
            if(!id || wide>65535 || bank_count>=24 || g_hash_table_contains(ids,GUINT_TO_POINTER(id))) ok=bad(error,"Asset banks require unique IDs 1..65535, maximum 24 banks.");
            else {
                g_hash_table_add(ids,GUINT_TO_POINTER(id));resource_context_clear(&p);resource_context_init(&p);
                p.asset_root=g_canonicalize_filename(asset_root,NULL);p.bank_id=id;
                g_free(p.background);p.background=g_strdup(background);g_string_truncate(sounds,0);open=TRUE;
            }
            g_free(number);g_free(background);
        } else if(!strcmp(kind,"MODEL") && open && g_regex_match(model_args,args,0,&a)) {
            char *number=resource_field(a,1),*type=resource_field(a,2),*model=resource_field(a,3),*texture=resource_field(a,4);
            guint64 wide=g_ascii_strtoull(number,NULL,10);
            if(!wide || wide>=G_MAXINT || p.models->len>=512) ok=bad(error,"Invalid resource model ID/count.");
            for(guint j=0;ok && j<p.models->len;j++) if(((ResourceModel*)g_ptr_array_index(p.models,j))->id==wide) ok=bad(error,"Duplicate model ID in asset bank.");
            if(ok && (!*model && !*texture)) ok=bad(error,"A resource model needs an OBJ or texture.");
            if(ok) {
                ResourceModel *o=resource_model_add(&p,!strcmp(type,"mesh")?RESOURCE_MESH:!strcmp(type,"sprite")?RESOURCE_SPRITE:RESOURCE_BILLBOARD);
                o->id=(guint)wide;g_free(o->asset);o->asset=g_strdup(model);g_free(o->texture);o->texture=g_strdup(texture);
            }
            g_free(number);g_free(type);g_free(model);g_free(texture);
        } else if(!strcmp(kind,"SOUND") && open && g_regex_match(sound_args,args,0,&a)) {
            char *name=resource_field(a,1);g_string_append_printf(sounds,"@sound %s\n",name);g_free(name);
        } else if(!strcmp(kind,"LAYER") && open && g_regex_match(layer_args,args,0,&a)) {
            char *path=resource_field(a,1),*depth=resource_field(a,2);
            if(p.background_layers->len>=4 || g_ascii_strtoull(depth,NULL,10)>32767) {ok=bad(error,"Invalid resource depth layer.");g_free(path);}
            else {ResourceLayer *layer=g_new0(ResourceLayer,1);layer->path=path;layer->depth=(int)g_ascii_strtoull(depth,NULL,10);g_ptr_array_add(p.background_layers,layer);}
            g_free(depth);
        } else if(!strcmp(kind,"END") && open && !*args) {
            char *sub=g_strdup_printf("%s/bank-%u",stage,id);ok=export_bank(&p,sub,sounds->str,error);
            char *raw=ok?read_stage(sub,"bank.c",error):NULL,*manifest_text=ok?read_stage(sub,"assets.json",error):NULL;
            JsonParser *parser=json_parser_new();
            if(ok) ok=raw && manifest_text && json_parser_load_from_data(parser,manifest_text,-1,error);
            if(ok) {
                JsonObject *metadata=json_node_get_object(json_parser_get_root(parser));
                guint background=json_object_get_int_member(metadata,"background_texture");
                JsonArray *layers=json_object_get_array_member(metadata,"backdrops");
                guint backdrops=json_array_get_length(layers);
                GString *code=g_string_new(raw);GHashTable *aliases=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
                share_arrays(code,shared,aliases,id,&resident);separate_samples(code,samples);separate_geometry(code,geometry,aliases);
                g_string_replace(code,"#include \"assets.h\"","#include \"assets.h\"",0);
                GString *prefix=g_string_new("");g_string_append_printf(prefix,"#define bank_textures rb_%u_textures\n",id);
                for(guint j=0;j<p.models->len;j++) {
                    char *key=g_strdup_printf("vertices_%u",j);const char *canonical=g_hash_table_lookup(aliases,key);
                    if(canonical) g_string_append_printf(prefix,"#define vertices_%u %s\n",j,canonical);
                    g_string_append_printf(prefix,"#define mesh_%u rb_%u_mesh_%u\n",j,id,j);g_free(key);
                }
                JsonArray *ts=json_object_get_array_member(json_node_get_object(json_parser_get_root(parser)),"textures");
                for(guint j=0;ok && j<json_array_get_length(ts);j++) {
                    JsonObject *t=json_array_get_object_element(ts,j);const char *old=json_object_get_string_member(t,"file");
                    char *file=g_strdup_printf("T%03u.TIM",texture_id),*placeholder=g_strdup_printf("Q%03u.TIM",texture_id);
                    g_string_replace(code,old,placeholder,0);
                    char *from=g_build_filename(sub,old,NULL),*to=g_build_filename(stage,file,NULL),*bytes=NULL;gsize length=0;
                    ok=g_file_get_contents(from,&bytes,&length,error) && g_file_set_contents(to,bytes,length,error);
                    g_free(bytes);g_free(from);g_free(to);
                    JsonNode *copy=json_node_copy(json_array_get_element(ts,j));JsonObject *out=json_node_get_object(copy);
                    json_object_set_string_member(out,"file",file);char *dir=g_strdup_printf("B%03u",id);json_object_set_string_member(out,"directory",dir);g_free(dir);
                    json_array_add_element(textures,copy);texture_id++;g_free(file);g_free(placeholder);
                }
                for(guint j=0;j<texture_id;j++) {char *q=g_strdup_printf("Q%03u.TIM",j),*t=g_strdup_printf("T%03u.TIM",j);g_string_replace(code,q,t,0);g_free(q);g_free(t);}
                char *disc=g_strdup_printf("ASSETS\\\\B%03u\\\\",id);g_string_replace(code,"ASSETS\\\\",disc,0);g_free(disc);
                g_string_append(prefix,code->str);
                g_string_append_printf(prefix,"static const PsxResourceMesh rb_meshes[%u]={\n",MAX(1,p.models->len));
                if(!p.models->len) g_string_append(prefix,"{0,0}\n");
                for(guint j=0;j<p.models->len;j++) g_string_append_printf(prefix,"{%u,&mesh_%u},\n",((ResourceModel*)g_ptr_array_index(p.models,j))->id,j);
                g_string_append(prefix,"};\n");
                guint sound_count=json_object_get_int_member(metadata,"sound_count");
                char *sound_name=sound_count?g_strdup_printf("sound_bank_%u",id):g_strdup("0");
                g_string_append_printf(prefix,"const PsxResourceBank psx_bank_%u={bank_textures,%u,rb_meshes,%u,%s,%u,%u,%u,{\n",id,json_array_get_length(ts),p.models->len,sound_name,sound_count,background,backdrops);g_free(sound_name);
                if(!backdrops) g_string_append(prefix,"{0,0,0,0,0,0}\n");
                for(guint j=0;j<backdrops;j++) {
                    JsonArray *layer=json_array_get_array_element(layers,j);
                    g_string_append_c(prefix,'{');
                    for(guint k=0;k<6;k++) g_string_append_printf(prefix,"%s%" G_GINT64_FORMAT,k?",":"",json_array_get_int_element(layer,k));
                    g_string_append(prefix,"},\n");
                }
                g_string_append(prefix,"}};\n");
                char *name=g_strdup_printf("rb%03u.c",id);if(ok) ok=write_file(stage,name,prefix->str,error);json_array_add_string_element(modules,name);g_free(name);
                g_string_append_printf(header,"extern const PsxResourceBank psx_bank_%u;\n",id);
                g_string_append_printf(cases,"case %u:return &psx_bank_%u;\n",id,id);
                g_string_free(prefix,TRUE);g_string_free(code,TRUE);g_hash_table_unref(aliases);
            }
            g_object_unref(parser);g_free(raw);g_free(manifest_text);g_free(sub);open=FALSE;bank_count++;
        } else ok=bad(error,"Invalid asset list arguments or bank nesting.");
        if(a) {g_match_info_free(a);}
        g_match_info_free(m);g_free(kind);g_free(args);
    }
    if(ok && (open || !bank_count || resident>4096)) ok=bad(error,"Unclosed/empty asset list or resident geometry exceeds 4096 triangles.");
    /* Shared arrays have external linkage and one translation unit each. */
    GPtrArray *groups[]={geometry,samples};
    for(guint group=0;group<2;group++) for(guint i=0;ok && i<groups[group]->len;i++) {
        const char *unit=g_ptr_array_index(groups[group],i);GRegex *declaration=g_regex_new("const (PsxVertex|unsigned int) ([A-Za-z0-9_]+)\\[([0-9]+)\\]",0,0,NULL);GMatchInfo *m=NULL;
        if(g_regex_match(declaration,unit,0,&m)) {char *type=resource_field(m,1),*name=resource_field(m,2),*count=resource_field(m,3);g_string_append_printf(header,"extern const %s %s[%s];\n",type,name,count);g_free(type);g_free(name);g_free(count);}
        g_match_info_free(m);g_regex_unref(declaration);
        char *file=g_strdup_printf("r%c%03u.c",group?'a':'g',i);ok=write_file(stage,file,unit,error);json_array_add_string_element(modules,file);g_free(file);
    }
    g_string_append(header,"#endif\n");g_string_append_printf(dispatch,"const PsxResourceBank *psx_resources(unsigned int id)\n{switch(id) {\n%s default:return 0;}}\n",cases->str);
    if(ok) ok=write_file(stage,"assets.h",header->str,error) && write_file(stage,"resbank.c",dispatch->str,error);
    json_array_add_string_element(modules,"resbank.c");
    JsonObject *manifest=json_object_new();json_object_set_array_member(manifest,"textures",textures);json_object_set_array_member(manifest,"modules",modules);json_object_set_int_member(manifest,"triangles",resident);
    JsonNode *node=json_node_new(JSON_NODE_OBJECT);json_node_take_object(node,manifest);char *json=json_to_string(node,TRUE);
    if(ok) {ok=write_file(stage,"assets.json",json,error);}
    g_free(json);json_node_free(node);
    if(ok && g_mkdir_with_parents(directory,0755)!=0) ok=bad(error,"Cannot create resource export directory.");
    GDir *dir=ok?g_dir_open(stage,0,error):NULL;const char *entry;
    if(ok && !dir) ok=FALSE;
    while(dir && ok && (entry=g_dir_read_name(dir))) {
        char *from=g_build_filename(stage,entry,NULL),*to=g_build_filename(directory,entry,NULL);
        if(g_file_test(from,G_FILE_TEST_IS_REGULAR)) {
            if(strcmp(entry,"assets.json") && g_file_test(to,G_FILE_TEST_EXISTS)) ok=bad(error,"Resource output collides with an existing file; export into a fresh directory.");
            char *bytes=NULL;gsize length=0;if(ok) ok=g_file_get_contents(from,&bytes,&length,error) && g_file_set_contents(to,bytes,length,error);g_free(bytes);
        }
        g_free(from);g_free(to);
    }
    if(dir) g_dir_close(dir);
    resource_context_clear(&p);g_string_free(sounds,TRUE);g_strfreev(lines);g_free(text);remove_stage(stage);g_free(stage);
    g_string_free(header,TRUE);g_string_free(dispatch,TRUE);g_string_free(cases,TRUE);g_hash_table_unref(shared);g_hash_table_unref(ids);
    g_ptr_array_unref(samples);g_ptr_array_unref(geometry);g_regex_unref(line);g_regex_unref(bank_args);g_regex_unref(model_args);g_regex_unref(sound_args);g_regex_unref(layer_args);
    return ok;
}
