/* SPDX-License-Identifier: BSD-2-Clause
 * Real A540 acceptance: require Freedreno, compile shaders, draw three changing
 * frames, sample a texture, and compare pixels read back through Mesa.
 */
#include <OpenGLES/ES2/gl.h>
#include <sys/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "gpu-isolation-probe.h"
#include "smp-probe.h"
extern void *OSMesaCreateContextAttribs(const int *, void *);
extern unsigned char OSMesaMakeCurrent(void *, void *, unsigned, int, int);
extern void OSMesaDestroyContext(void *);
static void report(int ok, const char *detail)
{
    printf("A540 HARDWARE RENDER: %s %s\n",ok ? "PASS" : "FAIL",detail);
    struct statfs fs;
    if (statfs("/private/var/run",&fs) || strcmp(fs.f_fstypename,"ramfs")) return;
    FILE *f = fopen("/private/var/run/lilac-gpu.status","w");
    if (f) { fprintf(f,"A540 HARDWARE RENDER: %s\n%s\n",ok ? "PASS" : "FAIL",detail); fclose(f); }
}
static GLuint shader(GLenum type, const char *source)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s,1,&source,NULL); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s,sizeof(log),NULL,log); puts(log); glDeleteShader(s); return 0; }
    return s;
}
static GLuint program(const char *fragment)
{
    GLuint vs = shader(GL_VERTEX_SHADER,"attribute vec2 position; varying vec2 uv; void main(){gl_Position=vec4(position,0.0,1.0);uv=position*0.5+0.5;}");
    GLuint fs = shader(GL_FRAGMENT_SHADER,fragment);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p,vs); glAttachShader(p,fs); glBindAttribLocation(p,0,"position"); glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p,sizeof(log),NULL,log); puts(log); glDeleteProgram(p); return 0; }
    return p;
}
static int pixel(const uint8_t *p, unsigned x, unsigned y, const uint8_t *expected)
{
    p += (y*64+x)*4;
    for (unsigned c = 0; c < 4; c++) if (abs((int)p[c]-(int)expected[c]) > 1) {
        printf("pixel %u,%u got=%u,%u,%u,%u expected=%u,%u,%u,%u\n",x,y,p[0],p[1],p[2],p[3],expected[0],expected[1],expected[2],expected[3]);
        return 0;
    }
    return 1;
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    unsetenv("OSMESA_SOFTWARE"); unsetenv("GALLIUM_DRIVER");
    if (!smp_probe()) { report(0,"SMP execution and pmap stress"); return 1; }
    if (!isolation_probe()) { report(0,"process address-space isolation"); return 1; }
    if (!isolation_protection()) { report(0,"GPU protection and recovery"); return 1; }
    if (!resume_allocation_probe()) { report(0,"concurrent allocation during GPU resume"); return 1; }
    puts("A540 HARDWARE RENDER: BEGIN shader and texture acceptance");
    const int attrs[] = {0x22,0x1908,0x30,24,0x31,8,0x33,0x38,0x36,2,0x37,0,0};
    uint8_t storage[64*64*4] = {0}, pixels[sizeof(storage)];
    void *ctx = OSMesaCreateContextAttribs(attrs,NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx,storage,GL_UNSIGNED_BYTE,64,64)) { report(0,"context creation"); return 1; }
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    printf("A540 GL_RENDERER=%s GL_VERSION=%s\n",renderer ? renderer : "NULL",glGetString(GL_VERSION));
    if (!renderer || (!strstr(renderer,"FD540") && !strstr(renderer,"Adreno 540"))) { report(0,"A540 renderer required; software rejected"); return 1; }
    GLuint p = program("precision mediump float; uniform vec4 color; void main(){gl_FragColor=color;}");
    if (!p) { report(0,"shader compile/link"); return 1; }
    glViewport(0,0,64,64); glDisable(GL_DITHER); glDisable(GL_BLEND);
    glUseProgram(p);
    const GLfloat triangle[] = {-1,-1,1,-1,0,1};
    GLuint vbo; glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(triangle),triangle,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);
    const uint8_t colors[][4] = {{255,0,0,255},{0,255,0,255},{255,255,0,255}}, blue[] = {0,0,255,255};
    for (unsigned pass = 0; pass < 3; pass++) {
        usleep(800000); /* Same GL objects must survive runtime idle collapse. */
        glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
        glUniform4f(glGetUniformLocation(p,"color"),colors[pass][0]/255.f,colors[pass][1]/255.f,0,1);
        glDrawArrays(GL_TRIANGLES,0,3); glFinish();
        memset(pixels,0x5a,sizeof(pixels)); glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        if (glGetError() || !pixel(pixels,32,24,colors[pass]) || !pixel(pixels,2,60,blue)) {
            report(0,"changing shader frame pixel comparison"); return 1;
        }
        printf("A540 shader frame %u: center and background match\n",pass+1);
    }
    glDeleteProgram(p);
    p = program("precision mediump float; varying vec2 uv; uniform sampler2D image; void main(){gl_FragColor=texture2D(image,uv);}");
    if (!p) { report(0,"texture shader compile/link"); return 1; }
    glUseProgram(p);
    const GLfloat quad[] = {-1,-1,1,-1,-1,1,1,1};
    glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    const uint8_t texels[] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    GLuint texture; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,texels);
    glUniform1i(glGetUniformLocation(p,"image"),0); glDrawArrays(GL_TRIANGLE_STRIP,0,4); glFinish();
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    int ok = glGetError()==GL_NO_ERROR;
    for (unsigned y=0;y<2;y++) for (unsigned x=0;x<2;x++) ok &= pixel(pixels,16+32*x,16+32*y,texels+4*(y*2+x));
    report(ok,ok ? "Freedreno A540: 3 shader frames + 4 texture quadrants matched" : "texture pixel comparison");
    glDeleteTextures(1,&texture); glDeleteBuffers(1,&vbo); glDeleteProgram(p); OSMesaDestroyContext(ctx);
    return ok ? 0 : 1;
}
