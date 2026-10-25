#include "studio.h"
#include <png.h>
#include <math.h>
#include <string.h>

static gboolean bad(GError **error, const char *message)
{ g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message); return FALSE; }

char *asset_path(const Project *p, const char *relative)
{
    char *root = p->asset_root ? g_strdup(p->asset_root) : p->path ? project_data_path(p->path) : NULL;
    if (!root) return NULL;
    char *path = g_build_filename(root, relative, NULL); g_free(root); return path;
}

void asset_free(Asset *a)
{
    if (!a) return;
    if (a->vertices) g_array_unref(a->vertices);
    g_free(a->pixels); g_free(a);
}

static char **words(const char *line)
{
    char **split = g_strsplit_set(line, " \t\r", -1);
    GPtrArray *result = g_ptr_array_new();
    for (int i = 0; split[i]; i++) {
        if (*split[i] == '#') break;
        if (*split[i]) g_ptr_array_add(result, g_strdup(split[i]));
    }
    g_strfreev(split); g_ptr_array_add(result, NULL);
    return (char **)g_ptr_array_free(result, FALSE);
}

static gboolean number(const char *text, double *value)
{
    char *end;
    *value = g_ascii_strtod(text, &end);
    return text != end && !*end && isfinite(*value);
}

static int obj_index(const char *text, guint count)
{
    char *end;
    gint64 value = g_ascii_strtoll(text, &end, 10);
    if (!*text || *end || !value || value < -(gint64)count || value > count) return -1;
    return value > 0 ? value - 1 : count + value;
}

static gboolean convex_face(const AssetVertex *face, guint count)
{
    double normal[3]={0,0,0},length;
    for(guint i=0;i<count;i++) {
        const double *a=face[i].p,*b=face[(i+1)%count].p;
        normal[0]+=(a[1]-b[1])*(a[2]+b[2]);
        normal[1]+=(a[2]-b[2])*(a[0]+b[0]);
        normal[2]+=(a[0]-b[0])*(a[1]+b[1]);
    }
    length=sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
    if(length<1e-8) return FALSE;
    for(int k=0;k<3;k++) normal[k]/=length;
    for(guint i=0;i<count;i++) {
        double a[3],b[3],distance=0;
        for(int k=0;k<3;k++) {
            distance+=(face[i].p[k]-face[0].p[k])*normal[k];
            a[k]=face[(i+1)%count].p[k]-face[i].p[k];b[k]=face[(i+2)%count].p[k]-face[(i+1)%count].p[k];
        }
        if(fabs(distance)>.001 || (a[1]*b[2]-a[2]*b[1])*normal[0]+(a[2]*b[0]-a[0]*b[2])*normal[1]+(a[0]*b[1]-a[1]*b[0])*normal[2]<-1e-6)
            return FALSE;
    }
    return TRUE;
}

static gboolean load_obj(Asset *asset, const char *path, GError **error)
{
    char *contents;
    gsize length;
    if (!g_file_get_contents(path, &contents, &length, error)) return FALSE;
    if (length > 4*1024*1024) { g_free(contents); return bad(error, "OBJ file too large (maximum 4 MB)."); }
    GArray *positions = g_array_new(FALSE, FALSE, sizeof(AssetVertex));
    GArray *uvs = g_array_new(FALSE, FALSE, sizeof(AssetVertex));
    char **lines = g_strsplit(contents, "\n", -1);
    gboolean ok = TRUE;
    asset->has_uv = TRUE;
    for (int line = 0; lines[line] && ok; line++) {
        char **w = words(lines[line]); guint count = g_strv_length(w);
        if (!count) { g_strfreev(w); continue; }
        if (g_str_equal(w[0], "v") || g_str_equal(w[0], "vt")) {
            gboolean uv = g_str_equal(w[0], "vt");
            AssetVertex v = {{0,0,0},{0,0}};
            if (count < (uv ? 3U : 4U)) ok = FALSE;
            for (int k = 0; k < (uv ? 2 : 3) && ok; k++) {
                double n;
                ok = number(w[k+1], &n) && (uv ? (n >= 0 && n <= 1) : fabs(n) <= 32767);
                if (uv) v.uv[k] = k == 1 ? 1-n : n; else v.p[k] = n;
            }
            if (ok) g_array_append_val(uv ? uvs : positions, v);
            if (positions->len > 1024 || uvs->len > 4096) ok = FALSE;
        } else if (g_str_equal(w[0], "f")) {
            AssetVertex face[16];
            if (count < 4 || count > 17) ok = FALSE;
            for (guint k = 1; k < count && ok; k++) {
                char **indices = g_strsplit(w[k], "/", -1);
                int vi = obj_index(indices[0], positions->len);
                if (vi < 0 || g_strv_length(indices) > 3) ok = FALSE;
                if (ok) {
                    face[k-1] = g_array_index(positions, AssetVertex, vi);
                    if (indices[1] && *indices[1]) {
                        int ti = obj_index(indices[1], uvs->len);
                        if (ti < 0) ok = FALSE;
                        else memcpy(face[k-1].uv, g_array_index(uvs, AssetVertex, ti).uv, sizeof(face[k-1].uv));
                    } else asset->has_uv = FALSE;
                }
                g_strfreev(indices);
            }
            if(ok && !convex_face(face,count-1)) ok=FALSE;
            for (guint k = 1; k+1 < count-1 && ok; k++) {
                g_array_append_val(asset->vertices, face[0]);
                g_array_append_val(asset->vertices, face[k]);
                g_array_append_val(asset->vertices, face[k+1]);
                if (asset->vertices->len > 6144) ok = FALSE;
            }
        }
        g_strfreev(w);
    }
    g_array_unref(positions); g_array_unref(uvs); g_strfreev(lines); g_free(contents);
    if (!ok || !asset->vertices->len) return bad(error, "Invalid OBJ: maximum 1024 vertices/2048 triangles, planar convex faces with 3..16 vertices, previously declared indices and UVs in [0,1].");
    return TRUE;
}

static void default_geometry(Asset *a, gboolean cube)
{
    static const int faces[6][4] = {{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
    static const int triangle[] = {0,1,2,0,2,3};
    static const double uv[4][2] = {{0,1},{1,1},{1,0},{0,0}};
    for (int f = 0; f < (cube ? 6 : 1); f++) for (int k = 0; k < 6; k++) {
        int corner = triangle[k], index = faces[f][corner];
        AssetVertex v;
        for (int axis = 0; axis < 3; axis++) v.p[axis] = (index & (1<<axis)) ? 24 : -24;
        if (!cube) v.p[2] = 0;
        memcpy(v.uv, uv[corner], sizeof(v.uv)); g_array_append_val(a->vertices, v);
    }
    a->has_uv = TRUE;
}

Asset *asset_load(const Project *p, const Object *o, GError **error)
{
    Asset *a = g_new0(Asset, 1);
    a->vertices = g_array_new(FALSE, FALSE, sizeof(AssetVertex));
    if (*o->asset) {
        if (o->kind != OBJECT_MESH) { bad(error, "An OBJ model can only be assigned to a 3D object."); goto failed; }
        char *path = asset_path(p, o->asset);
        gboolean ok = path && load_obj(a, path, error); g_free(path);
        if (!ok) { if (!error || !*error) bad(error, "Save the project before loading assets."); goto failed; }
    } else default_geometry(a, o->kind == OBJECT_MESH);
    if (*o->texture) {
        char *path = asset_path(p, o->texture);
        png_image image; memset(&image, 0, sizeof(image)); image.version = PNG_IMAGE_VERSION;
        gboolean ok = path && png_image_begin_read_from_file(&image, path); g_free(path);
        if (!ok) { bad(error, "Cannot read the PNG texture."); png_image_free(&image); goto failed; }
        if (!image.width || !image.height || image.width > 256 || image.height > 256) {
            bad(error, "The PNG texture must be no larger than 256x256."); png_image_free(&image); goto failed;
        }
        image.format = PNG_FORMAT_RGBA;
        a->pixels = g_malloc(PNG_IMAGE_SIZE(image)); a->width = image.width; a->height = image.height;
        ok = png_image_finish_read(&image, NULL, a->pixels, 0, NULL); png_image_free(&image);
        if (!ok || !a->has_uv) { bad(error, "Invalid PNG or model without complete UV coordinates."); goto failed; }
    }
    return a;
failed:
    asset_free(a); return NULL;
}

gboolean asset_import(Project *p, Object *o, const char *source, int type, GError **error)
{
    if (!p->path) return bad(error, "Save the project first to create its data folder.");
    char *contents; gsize size;
    if (!g_file_get_contents(source, &contents, &size, error)) return FALSE;
    if (size > 4*1024*1024) { g_free(contents); return bad(error, "Asset too large (maximum 4 MB)."); }
    char *sha = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)contents, size);
    char *name = g_strdup_printf("%.*s.%s", 16, sha, type == 0 ? "obj" : type == 1 ? "png" : "c");
    char *dir = project_data_path(p->path), *target = g_build_filename(dir, name, NULL);
    gboolean ok = g_mkdir_with_parents(dir, 0755) == 0;
    if (ok) ok = g_file_set_contents_full(target, contents, size, G_FILE_SET_CONTENTS_CONSISTENT, 0644, error);
    else bad(error, "Cannot create the data folder.");
    if (ok && type < 2) {
        Object probe = *o;
        if (type == 0) { probe.asset = name; probe.kind = OBJECT_MESH; }
        else probe.texture = name;
        Asset *check = asset_load(p, &probe, error); ok = check != NULL; asset_free(check);
    }
    if (ok && type == 2 && (!g_utf8_validate(contents, size, NULL) || !strstr(contents, "psx_logic")))
        ok = bad(error, "The module must declare const PsxLogic psx_logic (see the C89 example).");
    if (ok) {
        char **field = type == 0 ? &o->asset : type == 1 ? &o->texture : &o->module;
        g_free(*field); *field = g_strdup(name);
        if (type == 0) o->kind = OBJECT_MESH;
        asset_free(o->visual); o->visual = NULL; o->visual_attempted = FALSE; p->dirty = TRUE;
    }
    g_free(contents); g_free(sha); g_free(name); g_free(dir); g_free(target); return ok;
}

/* TIM direct colour: little-endian header, no CLUT, RGB555 texels. */
static void tim_u16(guchar *out, guint value)
{
    out[0] = value & 255;
    out[1] = (value >> 8) & 255;
}

static void tim_u32(guchar *out, guint32 value)
{
    tim_u16(out, value & 65535);
    tim_u16(out + 2, value >> 16);
}

gboolean asset_write_tim(const Asset *asset, const char *path, int x, int y, GError **error)
{
    if (!asset || !asset->pixels || !asset->width || !asset->height ||
        asset->width > 256 || asset->height > 256 || x < 0 || y < 0 ||
        x + asset->width > 1024 || y + asset->height > 512)
        return bad(error, "Invalid texture or VRAM rectangle for a 16-bit TIM.");
    gsize count = (gsize)asset->width * asset->height;
    gsize length = 20 + count * 2;
    guchar *data = g_malloc0(length);
    tim_u32(data, 0x10);
    tim_u32(data + 4, 2);
    tim_u32(data + 8, 12 + count * 2);
    tim_u16(data + 12, x); tim_u16(data + 14, y);
    tim_u16(data + 16, asset->width); tim_u16(data + 18, asset->height);
    for (gsize i = 0; i < count; i++) {
        const guchar *rgba = asset->pixels + i * 4;
        guint pixel = 0;
        /* Match the viewport alpha cutoff. The renderer uses opaque commands:
         * 0x0000 is transparent; 0x8000 preserves opaque quantized black. */
        if (rgba[3] >= 128) {
            pixel = (rgba[0] >> 3) | ((rgba[1] >> 3) << 5) | ((rgba[2] >> 3) << 10);
            if (!pixel) pixel = 0x8000;
        }
        tim_u16(data + 20 + i * 2, pixel);
    }
    gboolean ok = g_file_set_contents_full(path, (const char *)data, length,
                                           G_FILE_SET_CONTENTS_CONSISTENT, 0644, error);
    g_free(data);
    return ok;
}

/* Shared PS1 endpoint quantization for export and desktop preview. */
long asset_uv_texel(double coordinate,guint size)
{
    return size?lround(CLAMP(coordinate,0,1)*(size-1)):0;
}
