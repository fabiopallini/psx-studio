/* Headless Windows EGL/ANGLE probe: no GTK windows or game projects. */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include "viewport_shaders.h"

static GLuint compile_shader(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);GLint ok=0;char log[2048];
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) {glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"Viewport shader: %s\n",log);glDeleteShader(shader);return 0;}
    return shader;
}
static int pixel_matches(int red,int green,int blue)
{
    GLubyte pixel[4]={0,0,0,0};glFinish();glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    if(glGetError()!=GL_NO_ERROR || abs((int)pixel[0]-red)>2 || abs((int)pixel[1]-green)>2 ||
       abs((int)pixel[2]-blue)>2 || pixel[3]!=255) {
        fprintf(stderr,"Unexpected EGL pixel: %u,%u,%u,%u (expected %d,%d,%d,255)\n",
            pixel[0],pixel[1],pixel[2],pixel[3],red,green,blue);return 0;
    }
    return 1;
}
int main(void)
{
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLConfig config;EGLint count=0;
    EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;
    GLuint vertex=0,fragment=0,program=0,vao=0,vbo=0,texture=0;int result=1;
    const EGLint config_attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    const EGLint context_attributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    const EGLint surface_attributes[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    const GLfloat triangle[]={-1,-1,0,1,0,0, 6,-2,0,2,1,0, -4,12,0,4,0,1};
    if(display==EGL_NO_DISPLAY || !eglInitialize(display,NULL,NULL) || !eglBindAPI(EGL_OPENGL_ES_API)) goto done;
    if(!eglChooseConfig(display,config_attributes,&config,1,&count) || count!=1) goto done;
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attributes);
    surface=eglCreatePbufferSurface(display,config,surface_attributes);
    if(context==EGL_NO_CONTEXT || surface==EGL_NO_SURFACE || !eglMakeCurrent(display,surface,surface,context)) goto done;
    printf("EGL renderer: %s\n",glGetString(GL_RENDERER));
    vertex=compile_shader(GL_VERTEX_SHADER,STUDIO_VERTEX_ES);fragment=compile_shader(GL_FRAGMENT_SHADER,STUDIO_FRAGMENT_ES);
    if(!vertex || !fragment) goto done;
    program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);
    glBindAttribLocation(program,0,"point");glBindAttribLocation(program,1,"uv");glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked) {char log[2048];glGetProgramInfoLog(program,sizeof(log),NULL,log);fprintf(stderr,"Viewport program: %s\n",log);goto done;}
    glUseProgram(program);glViewport(0,0,16,16);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(triangle),triangle,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,6*sizeof(GLfloat),(void*)0);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,6*sizeof(GLfloat),(void*)(4*sizeof(GLfloat)));
    glUniform4f(glGetUniformLocation(program,"tint"),.25f,.5f,.75f,1);
    glUniform1i(glGetUniformLocation(program,"textured"),0);glDrawArrays(GL_TRIANGLES,0,3);
    if(!pixel_matches(64,128,191)) goto done;
    GLubyte pixels[4*4*4];
    for(int y=0;y<4;y++) for(int x=0;x<4;x++) {
        int i=(y*4+x)*4;pixels[i]=(GLubyte)(x*64);pixels[i+1]=(GLubyte)(y*64);pixels[i+2]=128;pixels[i+3]=255;
    }
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glUniform4f(glGetUniformLocation(program,"tint"),1,1,1,1);
    glUniform1i(glGetUniformLocation(program,"image"),0);glUniform1i(glGetUniformLocation(program,"textured"),1);
    glDrawArrays(GL_TRIANGLES,0,3);
    if(!pixel_matches(66,66,132)) goto done;
    puts("EGL/ANGLE: context, viewport shaders, drawing, affine UVs and RGB555 preview OK.");result=0;
done:
    if(result) fprintf(stderr,"EGL graphics check failed (EGL error 0x%x).\n",eglGetError());
    if(context!=EGL_NO_CONTEXT && surface!=EGL_NO_SURFACE) {
        if(texture) glDeleteTextures(1,&texture);
        if(vbo) glDeleteBuffers(1,&vbo);
        if(vao) glDeleteVertexArrays(1,&vao);
        if(program) glDeleteProgram(program);
        if(vertex) glDeleteShader(vertex);
        if(fragment) glDeleteShader(fragment);
    }
    if(display!=EGL_NO_DISPLAY) {
        eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(surface!=EGL_NO_SURFACE) eglDestroySurface(display,surface);
        if(context!=EGL_NO_CONTEXT) eglDestroyContext(display,context);
        eglTerminate(display);
    }
    return result;
}
