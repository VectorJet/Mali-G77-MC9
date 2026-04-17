#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
    printf("=== Mali-G77 EGL Triangle Dumper ===\n");
    
    /* Load Mali EGL library */
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) { printf("eglGetDisplay failed\n"); return 1; }
    if (!eglInitialize(display, NULL, NULL)) { printf("eglInitialize failed\n"); return 1; }

    EGLConfig config;
    EGLint num_config;
    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    if (!eglChooseConfig(display, attribs, &config, 1, &num_config) || num_config == 0) {
        printf("eglChooseConfig failed\n"); return 1;
    }

    EGLint pbuffer_attribs[] = { EGL_WIDTH, 256, EGL_HEIGHT, 256, EGL_NONE };
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer_attribs);
    if (surface == EGL_NO_SURFACE) { printf("eglCreatePbufferSurface failed\n"); return 1; }

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (context == EGL_NO_CONTEXT) { printf("eglCreateContext failed\n"); return 1; }

    if (!eglMakeCurrent(display, surface, surface, context)) {
        printf("eglMakeCurrent failed\n"); return 1;
    }

    printf("EGL Initialized. Renderer: %s\n", glGetString(GL_RENDERER));

    const char *vs_src =
        "attribute vec4 vPosition;\n"
        "void main() {\n"
        "  gl_Position = vPosition;\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n" /* Red Triangle */
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "vPosition");
    glLinkProgram(prog);
    glUseProgram(prog);

    /* A simple centered triangle */
    GLfloat vVertices[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f
    };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices);
    glEnableVertexAttribArray(0);

    /* Flush GL pipeline before we capture */
    glFinish();

    printf("Drawing triangle...\n");
    /* We will use our ioctl_spy to capture the job submitted by glDrawArrays + glFinish */
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f); // Green background
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();

    printf("Reading pixels...\n");
    unsigned char pixels[4];
    glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    printf("Center pixel: %02x %02x %02x %02x\n", pixels[0], pixels[1], pixels[2], pixels[3]);

    return 0;
}
