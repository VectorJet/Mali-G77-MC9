#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef EGLDisplay (*PFN_eglGetDisplay_t)(EGLNativeDisplayType);
typedef EGLBoolean (*PFN_eglInitialize_t)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*PFN_eglChooseConfig_t)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLSurface (*PFN_eglCreatePbufferSurface_t)(EGLDisplay, EGLConfig, const EGLint*);
typedef EGLContext (*PFN_eglCreateContext_t)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLBoolean (*PFN_eglMakeCurrent_t)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLint (*PFN_eglGetError_t)(void);

typedef const GLubyte *(*PFN_glGetString_t)(GLenum);
typedef void (*PFN_glClearColor_t)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_glClear_t)(GLbitfield);
typedef GLuint (*PFN_glCreateShader_t)(GLenum);
typedef void (*PFN_glShaderSource_t)(GLuint, GLsizei, const GLchar *const*, const GLint *);
typedef void (*PFN_glCompileShader_t)(GLuint);
typedef void (*PFN_glGetShaderiv_t)(GLuint, GLenum, GLint *);
typedef void (*PFN_glGetShaderInfoLog_t)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (*PFN_glCreateProgram_t)(void);
typedef void (*PFN_glAttachShader_t)(GLuint, GLuint);
typedef void (*PFN_glBindAttribLocation_t)(GLuint, GLuint, const GLchar *);
typedef void (*PFN_glLinkProgram_t)(GLuint);
typedef void (*PFN_glGetProgramiv_t)(GLuint, GLenum, GLint *);
typedef void (*PFN_glGetProgramInfoLog_t)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*PFN_glUseProgram_t)(GLuint);
typedef void (*PFN_glVertexAttribPointer_t)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void (*PFN_glEnableVertexAttribArray_t)(GLuint);
typedef void (*PFN_glDrawArrays_t)(GLenum, GLint, GLsizei);
typedef void (*PFN_glFinish_t)(void);
typedef void (*PFN_glReadPixels_t)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);

static void *load_sym(void *lib, const char *name) {
    void *p = dlsym(lib, name);
    if (!p) {
        fprintf(stderr, "Missing symbol %s: %s\n", name, dlerror());
        exit(1);
    }
    return p;
}

static void *load_vendor_gles(const char **out_path) {
    static const char *paths[] = {
        "/vendor/lib64/libGLES_mali.so",
        "/vendor/lib64/egl/mt6893/libGLES_mali.so",
        "/vendor/lib/libGLES_mali.so",
        "/vendor/lib/egl/mt6893/libGLES_mali.so",
    };

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        void *lib = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
        if (lib) {
            *out_path = paths[i];
            return lib;
        }
    }

    return NULL;
}

static void print_shader_log(PFN_glGetShaderiv_t glGetShaderiv,
                             PFN_glGetShaderInfoLog_t glGetShaderInfoLog,
                             GLuint shader, const char *name) {
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return;
    char log[4096];
    GLsizei n = 0;
    memset(log, 0, sizeof(log));
    glGetShaderInfoLog(shader, sizeof(log) - 1, &n, log);
    fprintf(stderr, "%s compile failed: %s\n", name, log);
    exit(1);
}

static void print_program_log(PFN_glGetProgramiv_t glGetProgramiv,
                              PFN_glGetProgramInfoLog_t glGetProgramInfoLog,
                              GLuint prog) {
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok) return;
    char log[4096];
    GLsizei n = 0;
    memset(log, 0, sizeof(log));
    glGetProgramInfoLog(prog, sizeof(log) - 1, &n, log);
    fprintf(stderr, "Program link failed: %s\n", log);
    exit(1);
}

int main(void) {
    const char *libpath = NULL;
    void *lib = load_vendor_gles(&libpath);
    if (!lib) {
        fprintf(stderr, "dlopen vendor mali failed: %s\n", dlerror());
        fprintf(stderr, "Run this binary from /data/local/tmp as root to bypass the Termux app linker namespace.\n");
        return 1;
    }

    PFN_eglGetDisplay_t eglGetDisplay_p = load_sym(lib, "eglGetDisplay");
    PFN_eglInitialize_t eglInitialize_p = load_sym(lib, "eglInitialize");
    PFN_eglChooseConfig_t eglChooseConfig_p = load_sym(lib, "eglChooseConfig");
    PFN_eglCreatePbufferSurface_t eglCreatePbufferSurface_p = load_sym(lib, "eglCreatePbufferSurface");
    PFN_eglCreateContext_t eglCreateContext_p = load_sym(lib, "eglCreateContext");
    PFN_eglMakeCurrent_t eglMakeCurrent_p = load_sym(lib, "eglMakeCurrent");
    PFN_eglGetError_t eglGetError_p = load_sym(lib, "eglGetError");

    PFN_glGetString_t glGetString_p = load_sym(lib, "glGetString");
    PFN_glClearColor_t glClearColor_p = load_sym(lib, "glClearColor");
    PFN_glClear_t glClear_p = load_sym(lib, "glClear");
    PFN_glCreateShader_t glCreateShader_p = load_sym(lib, "glCreateShader");
    PFN_glShaderSource_t glShaderSource_p = load_sym(lib, "glShaderSource");
    PFN_glCompileShader_t glCompileShader_p = load_sym(lib, "glCompileShader");
    PFN_glGetShaderiv_t glGetShaderiv_p = load_sym(lib, "glGetShaderiv");
    PFN_glGetShaderInfoLog_t glGetShaderInfoLog_p = load_sym(lib, "glGetShaderInfoLog");
    PFN_glCreateProgram_t glCreateProgram_p = load_sym(lib, "glCreateProgram");
    PFN_glAttachShader_t glAttachShader_p = load_sym(lib, "glAttachShader");
    PFN_glBindAttribLocation_t glBindAttribLocation_p = load_sym(lib, "glBindAttribLocation");
    PFN_glLinkProgram_t glLinkProgram_p = load_sym(lib, "glLinkProgram");
    PFN_glGetProgramiv_t glGetProgramiv_p = load_sym(lib, "glGetProgramiv");
    PFN_glGetProgramInfoLog_t glGetProgramInfoLog_p = load_sym(lib, "glGetProgramInfoLog");
    PFN_glUseProgram_t glUseProgram_p = load_sym(lib, "glUseProgram");
    PFN_glVertexAttribPointer_t glVertexAttribPointer_p = load_sym(lib, "glVertexAttribPointer");
    PFN_glEnableVertexAttribArray_t glEnableVertexAttribArray_p = load_sym(lib, "glEnableVertexAttribArray");
    PFN_glDrawArrays_t glDrawArrays_p = load_sym(lib, "glDrawArrays");
    PFN_glFinish_t glFinish_p = load_sym(lib, "glFinish");
    PFN_glReadPixels_t glReadPixels_p = load_sym(lib, "glReadPixels");

    printf("=== Vendor Mali EGL Triangle Dumper ===\n");
    printf("uid=%d euid=%d\n", getuid(), geteuid());
    printf("Using %s\n", libpath);

    EGLDisplay display = eglGetDisplay_p(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        fprintf(stderr, "eglGetDisplay failed: 0x%x\n", eglGetError_p());
        return 1;
    }
    if (!eglInitialize_p(display, NULL, NULL)) {
        fprintf(stderr, "eglInitialize failed: 0x%x\n", eglGetError_p());
        return 1;
    }

    EGLConfig config;
    EGLint num_config = 0;
    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    if (!eglChooseConfig_p(display, attribs, &config, 1, &num_config) || num_config == 0) {
        fprintf(stderr, "eglChooseConfig failed: 0x%x\n", eglGetError_p());
        return 1;
    }

    EGLint pbuffer_attribs[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLSurface surface = eglCreatePbufferSurface_p(display, config, pbuffer_attribs);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreatePbufferSurface failed: 0x%x\n", eglGetError_p());
        return 1;
    }

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext_p(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (context == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext failed: 0x%x\n", eglGetError_p());
        return 1;
    }

    if (!eglMakeCurrent_p(display, surface, surface, context)) {
        fprintf(stderr, "eglMakeCurrent failed: 0x%x\n", eglGetError_p());
        return 1;
    }

    printf("Renderer: %s\n", glGetString_p(GL_RENDERER));

    const char *vs_src =
        "attribute vec4 vPosition;\n"
        "void main() {\n"
        "  gl_Position = vPosition;\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "}\n";

    GLuint vs = glCreateShader_p(GL_VERTEX_SHADER);
    glShaderSource_p(vs, 1, &vs_src, NULL);
    glCompileShader_p(vs);
    print_shader_log(glGetShaderiv_p, glGetShaderInfoLog_p, vs, "vertex shader");

    GLuint fs = glCreateShader_p(GL_FRAGMENT_SHADER);
    glShaderSource_p(fs, 1, &fs_src, NULL);
    glCompileShader_p(fs);
    print_shader_log(glGetShaderiv_p, glGetShaderInfoLog_p, fs, "fragment shader");

    GLuint prog = glCreateProgram_p();
    glAttachShader_p(prog, vs);
    glAttachShader_p(prog, fs);
    glBindAttribLocation_p(prog, 0, "vPosition");
    glLinkProgram_p(prog);
    print_program_log(glGetProgramiv_p, glGetProgramInfoLog_p, prog);
    glUseProgram_p(prog);

    GLfloat vVertices[] = {
         0.0f,  0.8f, 0.0f,
        -0.8f, -0.8f, 0.0f,
         0.8f, -0.8f, 0.0f
    };
    glVertexAttribPointer_p(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices);
    glEnableVertexAttribArray_p(0);

    glClearColor_p(0.0f, 1.0f, 0.0f, 1.0f);
    glClear_p(GL_COLOR_BUFFER_BIT);
    glDrawArrays_p(GL_TRIANGLES, 0, 3);
    glFinish_p();

    unsigned char px[4] = {0};
    glReadPixels_p(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("Center pixel: %02x %02x %02x %02x\n", px[0], px[1], px[2], px[3]);
    return 0;
}
