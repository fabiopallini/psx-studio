#ifndef STUDIO_VIEWPORT_SHADERS_H
#define STUDIO_VIEWPORT_SHADERS_H

#define STUDIO_FRAGMENT_BODY \
    "uniform vec4 tint; uniform sampler2D image; uniform int textured; out vec4 pixel;\n" \
    "void main(){pixel=tint; if(textured!=0){vec4 texel=texture(image,SAMPLE_UV);" \
    "if(texel.a<0.5)discard;texel.rgb=floor(texel.rgb*255.0/8.0)/31.0;pixel*=texel;}}"

#define STUDIO_VERTEX_GL \
    "#version 150\nin vec4 point; in vec2 uv; noperspective out vec2 texuv;" \
    "void main(){gl_Position=point;texuv=uv;}"
#define STUDIO_FRAGMENT_GL \
    "#version 150\nnoperspective in vec2 texuv;\n#define SAMPLE_UV texuv\n" STUDIO_FRAGMENT_BODY

/* GLES has no core noperspective qualifier. Interpolate (uv*w,w) smoothly and
 * divide in the fragment shader to recover the same screen-linear affine UVs. */
#define STUDIO_VERTEX_ES \
    "#version 300 es\nprecision highp float;\nin vec4 point; in vec2 uv; out vec3 affine_uv;" \
    "void main(){gl_Position=point;affine_uv=vec3(uv*point.w,point.w);}"
#define STUDIO_FRAGMENT_ES \
    "#version 300 es\nprecision highp float;\nin vec3 affine_uv;\n" \
    "#define SAMPLE_UV (affine_uv.xy/affine_uv.z)\n" STUDIO_FRAGMENT_BODY

#endif
