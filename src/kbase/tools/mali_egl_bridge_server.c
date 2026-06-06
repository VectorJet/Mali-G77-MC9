#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

static void *shared_pixels = NULL;
static void init_shm() {
    if (shared_pixels) return;
    int fd = open("/data/local/tmp/mali_bridge_pixels.bin", O_RDWR | O_CREAT, 0666);
    if (fd < 0) return;
    fchmod(fd, 0666);
    ftruncate(fd, 32 * 1024 * 1024);
    shared_pixels = mmap(NULL, 32 * 1024 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
}
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLNativeDisplayType;
typedef int EGLBoolean;
typedef int EGLint;

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;

#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_CONTEXT_CLIENT_VERSION 0x3098

#define GL_RENDERER 0x1F01
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_ARRAY_BUFFER 0x8892
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_NO_ERROR 0

#define BRIDGE_MAX_TEXT 4096
#define BRIDGE_MAX_FLOATS 1024
#define BRIDGE_MAX_BYTES 4096
#define BRIDGE_MAX_RESPONSE_TEXT 1024

#define BRIDGE_SOCKET "/data/local/tmp/mali_egl_bridge.sock"
#define BRIDGE_MAGIC 0x31474c4dU /* MLG1 */

enum bridge_cmd {
    BRIDGE_CMD_CLEAR_READ = 1,
    BRIDGE_CMD_CREATE_SHADER = 2,
    BRIDGE_CMD_SHADER_SOURCE = 3,
    BRIDGE_CMD_COMPILE_SHADER = 4,
    BRIDGE_CMD_GET_SHADER_IV = 5,
    BRIDGE_CMD_CREATE_PROGRAM = 6,
    BRIDGE_CMD_ATTACH_SHADER = 7,
    BRIDGE_CMD_BIND_ATTRIB_LOCATION = 8,
    BRIDGE_CMD_LINK_PROGRAM = 9,
    BRIDGE_CMD_GET_PROGRAM_IV = 10,
    BRIDGE_CMD_USE_PROGRAM = 11,
    BRIDGE_CMD_VERTEX_ATTRIB_POINTER = 12,
    BRIDGE_CMD_ENABLE_VERTEX_ATTRIB_ARRAY = 13,
    BRIDGE_CMD_CLEAR_COLOR = 14,
    BRIDGE_CMD_CLEAR = 15,
    BRIDGE_CMD_DRAW_ARRAYS = 16,
    BRIDGE_CMD_FINISH = 17,
    BRIDGE_CMD_READ_PIXELS = 18,
    BRIDGE_CMD_GET_ERROR = 19,
    BRIDGE_CMD_GEN_BUFFER = 20,
    BRIDGE_CMD_BIND_BUFFER = 21,
    BRIDGE_CMD_BUFFER_DATA = 22,
    BRIDGE_CMD_GET_UNIFORM_LOCATION = 23,
    BRIDGE_CMD_UNIFORM4F = 24,
    BRIDGE_CMD_VIEWPORT = 25,
    BRIDGE_CMD_GEN_TEXTURE = 26,
    BRIDGE_CMD_ACTIVE_TEXTURE = 27,
    BRIDGE_CMD_BIND_TEXTURE = 28,
    BRIDGE_CMD_TEX_PARAMETERI = 29,
    BRIDGE_CMD_TEX_IMAGE_2D = 30,
    BRIDGE_CMD_UNIFORM1I = 31,
    BRIDGE_CMD_DRAW_ELEMENTS = 32,
    BRIDGE_CMD_DELETE_SHADER = 33,
    BRIDGE_CMD_DELETE_PROGRAM = 34,
    BRIDGE_CMD_DELETE_BUFFER = 35,
    BRIDGE_CMD_DELETE_TEXTURE = 36,
    BRIDGE_CMD_GET_ATTRIB_LOCATION = 37,
    BRIDGE_CMD_GET_SHADER_INFO_LOG = 38,
    BRIDGE_CMD_GET_PROGRAM_INFO_LOG = 39,
    BRIDGE_CMD_GET_INTEGERV = 40,
    BRIDGE_CMD_UNIFORM1F = 41,
    BRIDGE_CMD_UNIFORM2F = 42,
    BRIDGE_CMD_UNIFORM3F = 43,
    BRIDGE_CMD_UNIFORM_MATRIX4FV = 44,
    BRIDGE_CMD_ENABLE = 45,
    BRIDGE_CMD_DISABLE = 46,
    BRIDGE_CMD_BLEND_FUNC = 47,
    BRIDGE_CMD_DEPTH_FUNC = 48,
    BRIDGE_CMD_CLEAR_DEPTHF = 49,
    BRIDGE_CMD_DEPTH_MASK = 50,
    BRIDGE_CMD_TEX_SUB_IMAGE_2D = 51,
    BRIDGE_CMD_GENERATE_MIPMAP = 52,
    BRIDGE_CMD_GEN_FRAMEBUFFER = 53,
    BRIDGE_CMD_BIND_FRAMEBUFFER = 54,
    BRIDGE_CMD_FRAMEBUFFER_TEXTURE_2D = 55,
    BRIDGE_CMD_CHECK_FRAMEBUFFER_STATUS = 56,
    BRIDGE_CMD_DELETE_FRAMEBUFFER = 57,
    BRIDGE_CMD_GEN_RENDERBUFFER = 58,
    BRIDGE_CMD_BIND_RENDERBUFFER = 59,
    BRIDGE_CMD_RENDERBUFFER_STORAGE = 60,
    BRIDGE_CMD_FRAMEBUFFER_RENDERBUFFER = 61,
    BRIDGE_CMD_DELETE_RENDERBUFFER = 62,
    BRIDGE_CMD_BUFFER_SUB_DATA = 63,
    BRIDGE_CMD_DISABLE_VERTEX_ATTRIB_ARRAY = 64,
    BRIDGE_CMD_SCISSOR = 65,
    BRIDGE_CMD_COLOR_MASK = 66,
    BRIDGE_CMD_CULL_FACE = 67,
    BRIDGE_CMD_FRONT_FACE = 68,
    BRIDGE_CMD_STENCIL_FUNC = 69,
    BRIDGE_CMD_STENCIL_OP = 70,
    BRIDGE_CMD_STENCIL_MASK = 71,
    BRIDGE_CMD_CLEAR_STENCIL = 72,
    BRIDGE_CMD_LINE_WIDTH = 73,
    BRIDGE_CMD_PIXEL_STOREI = 74,
    BRIDGE_CMD_UNIFORM_FV = 75,
    BRIDGE_CMD_UNIFORM_IV = 76,
    BRIDGE_CMD_UNIFORM_MATRIX2FV = 77,
    BRIDGE_CMD_UNIFORM_MATRIX3FV = 78,
    BRIDGE_CMD_IS_OBJECT = 79,
    BRIDGE_CMD_IS_ENABLED = 80,
    BRIDGE_CMD_GET_BUFFER_PARAMETER_IV = 81,
    BRIDGE_CMD_GET_TEX_PARAMETER_IV = 82,
    BRIDGE_CMD_GET_RENDERBUFFER_PARAMETER_IV = 83,
    BRIDGE_CMD_GET_VERTEX_ATTRIB_IV = 84,
    BRIDGE_CMD_GET_STATE = 85,
    BRIDGE_CMD_GET_FRAMEBUFFER_ATTACHMENT_PARAMETER_IV = 86,
    BRIDGE_CMD_GET_SHADER_SOURCE = 87,
    BRIDGE_CMD_GET_ATTACHED_SHADERS = 88,
    BRIDGE_CMD_GET_ACTIVE_UNIFORM = 89,
    BRIDGE_CMD_GET_ACTIVE_ATTRIB = 90,
    BRIDGE_CMD_GET_UNIFORM_VALUE = 91,
    BRIDGE_CMD_DETACH_SHADER = 92,
    BRIDGE_CMD_VALIDATE_PROGRAM = 93,
    BRIDGE_CMD_BLEND_COLOR = 94,
    BRIDGE_CMD_BLEND_EQUATION_SEPARATE = 95,
    BRIDGE_CMD_BLEND_FUNC_SEPARATE = 96,
    BRIDGE_CMD_STENCIL_FUNC_SEPARATE = 97,
    BRIDGE_CMD_STENCIL_MASK_SEPARATE = 98,
    BRIDGE_CMD_STENCIL_OP_SEPARATE = 99,
    BRIDGE_CMD_DEPTH_RANGEF = 100,
    BRIDGE_CMD_POLYGON_OFFSET = 101,
    BRIDGE_CMD_SAMPLE_COVERAGE = 102,
    BRIDGE_CMD_HINT = 103,
    BRIDGE_CMD_FLUSH = 104,
    BRIDGE_CMD_VERTEX_ATTRIB_VALUE = 105,
    BRIDGE_CMD_GET_VERTEX_ATTRIB_FV = 106,
    BRIDGE_CMD_GET_VERTEX_ATTRIB_POINTER = 107,
    BRIDGE_CMD_TEX_PARAMETER_F = 108,
    BRIDGE_CMD_GET_TEX_PARAMETER_FV = 109,
    BRIDGE_CMD_COPY_TEX_IMAGE_2D = 110,
    BRIDGE_CMD_COPY_TEX_SUB_IMAGE_2D = 111,
    BRIDGE_CMD_COMPRESSED_TEX_IMAGE_2D = 112,
    BRIDGE_CMD_COMPRESSED_TEX_SUB_IMAGE_2D = 113,
    BRIDGE_CMD_GET_SHADER_PRECISION_FORMAT = 114,
    BRIDGE_CMD_RELEASE_SHADER_COMPILER = 115,
    BRIDGE_CMD_SHADER_BINARY = 116,
};

typedef EGLDisplay (*PFN_eglGetDisplay_t)(EGLNativeDisplayType);
typedef EGLBoolean (*PFN_eglInitialize_t)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*PFN_eglChooseConfig_t)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
typedef EGLSurface (*PFN_eglCreatePbufferSurface_t)(EGLDisplay, EGLConfig, const EGLint *);
typedef EGLContext (*PFN_eglCreateContext_t)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLBoolean (*PFN_eglMakeCurrent_t)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLint (*PFN_eglGetError_t)(void);

typedef const GLubyte *(*PFN_glGetString_t)(GLenum);
typedef void (*PFN_glClearColor_t)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_glClear_t)(GLbitfield);
typedef GLuint (*PFN_glCreateShader_t)(GLenum);
typedef void (*PFN_glDeleteShader_t)(GLuint);
typedef void (*PFN_glShaderSource_t)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void (*PFN_glCompileShader_t)(GLuint);
typedef void (*PFN_glGetShaderiv_t)(GLuint, GLenum, GLint *);
typedef void (*PFN_glGetShaderInfoLog_t)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*PFN_glGetShaderSource_t)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (*PFN_glCreateProgram_t)(void);
typedef void (*PFN_glDeleteProgram_t)(GLuint);
typedef void (*PFN_glAttachShader_t)(GLuint, GLuint);
typedef void (*PFN_glDetachShader_t)(GLuint, GLuint);
typedef void (*PFN_glBindAttribLocation_t)(GLuint, GLuint, const GLchar *);
typedef GLint (*PFN_glGetAttribLocation_t)(GLuint, const GLchar *);
typedef void (*PFN_glLinkProgram_t)(GLuint);
typedef void (*PFN_glValidateProgram_t)(GLuint);
typedef void (*PFN_glGetProgramiv_t)(GLuint, GLenum, GLint *);
typedef void (*PFN_glGetProgramInfoLog_t)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*PFN_glGetAttachedShaders_t)(GLuint, GLsizei, GLsizei *, GLuint *);
typedef void (*PFN_glGetActiveUniform_t)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
typedef void (*PFN_glGetActiveAttrib_t)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
typedef void (*PFN_glGetUniformfv_t)(GLuint, GLint, GLfloat *);
typedef void (*PFN_glGetUniformiv_t)(GLuint, GLint, GLint *);
typedef void (*PFN_glUseProgram_t)(GLuint);
typedef void (*PFN_glVertexAttribPointer_t)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void (*PFN_glEnableVertexAttribArray_t)(GLuint);
typedef void (*PFN_glDisableVertexAttribArray_t)(GLuint);
typedef void (*PFN_glDrawArrays_t)(GLenum, GLint, GLsizei);
typedef void (*PFN_glDrawElements_t)(GLenum, GLsizei, GLenum, const void *);
typedef void (*PFN_glFinish_t)(void);
typedef void (*PFN_glReadPixels_t)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef GLenum (*PFN_glGetError_t)(void);
typedef void (*PFN_glGetIntegerv_t)(GLenum, GLint *);
typedef void (*PFN_glGetFloatv_t)(GLenum, GLfloat *);
typedef void (*PFN_glGetBooleanv_t)(GLenum, GLboolean *);
typedef GLboolean (*PFN_glIsEnabled_t)(GLenum);
typedef GLboolean (*PFN_glIsBuffer_t)(GLuint);
typedef GLboolean (*PFN_glIsTexture_t)(GLuint);
typedef GLboolean (*PFN_glIsFramebuffer_t)(GLuint);
typedef GLboolean (*PFN_glIsRenderbuffer_t)(GLuint);
typedef GLboolean (*PFN_glIsProgram_t)(GLuint);
typedef GLboolean (*PFN_glIsShader_t)(GLuint);
typedef void (*PFN_glGetBufferParameteriv_t)(GLenum, GLenum, GLint *);
typedef void (*PFN_glGetTexParameteriv_t)(GLenum, GLenum, GLint *);
typedef void (*PFN_glGetRenderbufferParameteriv_t)(GLenum, GLenum, GLint *);
typedef void (*PFN_glGetVertexAttribiv_t)(GLuint, GLenum, GLint *);
typedef void (*PFN_glGetVertexAttribfv_t)(GLuint, GLenum, GLfloat *);
typedef void (*PFN_glGetVertexAttribPointerv_t)(GLuint, GLenum, void **);
typedef void (*PFN_glGetFramebufferAttachmentParameteriv_t)(GLenum, GLenum, GLenum, GLint *);
typedef void (*PFN_glGenBuffers_t)(GLsizei, GLuint *);
typedef void (*PFN_glDeleteBuffers_t)(GLsizei, const GLuint *);
typedef void (*PFN_glBindBuffer_t)(GLenum, GLuint);
typedef void (*PFN_glBufferData_t)(GLenum, intptr_t, const void *, GLenum);
typedef void (*PFN_glBufferSubData_t)(GLenum, intptr_t, intptr_t, const void *);
typedef GLint (*PFN_glGetUniformLocation_t)(GLuint, const GLchar *);
typedef void (*PFN_glUniform1f_t)(GLint, GLfloat);
typedef void (*PFN_glUniform2f_t)(GLint, GLfloat, GLfloat);
typedef void (*PFN_glUniform3f_t)(GLint, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_glUniform4f_t)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_glUniform1fv_t)(GLint, GLsizei, const GLfloat *);
typedef void (*PFN_glUniform2fv_t)(GLint, GLsizei, const GLfloat *);
typedef void (*PFN_glUniform3fv_t)(GLint, GLsizei, const GLfloat *);
typedef void (*PFN_glUniform4fv_t)(GLint, GLsizei, const GLfloat *);
typedef void (*PFN_glUniform1iv_t)(GLint, GLsizei, const GLint *);
typedef void (*PFN_glUniform2iv_t)(GLint, GLsizei, const GLint *);
typedef void (*PFN_glUniform3iv_t)(GLint, GLsizei, const GLint *);
typedef void (*PFN_glUniform4iv_t)(GLint, GLsizei, const GLint *);
typedef void (*PFN_glUniformMatrix2fv_t)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (*PFN_glUniformMatrix3fv_t)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (*PFN_glUniformMatrix4fv_t)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (*PFN_glViewport_t)(GLint, GLint, GLsizei, GLsizei);
typedef void (*PFN_glEnable_t)(GLenum);
typedef void (*PFN_glDisable_t)(GLenum);
typedef void (*PFN_glBlendFunc_t)(GLenum, GLenum);
typedef void (*PFN_glBlendColor_t)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_glBlendEquationSeparate_t)(GLenum, GLenum);
typedef void (*PFN_glBlendFuncSeparate_t)(GLenum, GLenum, GLenum, GLenum);
typedef void (*PFN_glDepthFunc_t)(GLenum);
typedef void (*PFN_glClearDepthf_t)(GLfloat);
typedef void (*PFN_glDepthMask_t)(GLboolean);
typedef void (*PFN_glDepthRangef_t)(GLfloat, GLfloat);
typedef void (*PFN_glScissor_t)(GLint, GLint, GLsizei, GLsizei);
typedef void (*PFN_glColorMask_t)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef void (*PFN_glCullFace_t)(GLenum);
typedef void (*PFN_glFrontFace_t)(GLenum);
typedef void (*PFN_glStencilFunc_t)(GLenum, GLint, GLuint);
typedef void (*PFN_glStencilOp_t)(GLenum, GLenum, GLenum);
typedef void (*PFN_glStencilMask_t)(GLuint);
typedef void (*PFN_glStencilFuncSeparate_t)(GLenum, GLenum, GLint, GLuint);
typedef void (*PFN_glStencilMaskSeparate_t)(GLenum, GLuint);
typedef void (*PFN_glStencilOpSeparate_t)(GLenum, GLenum, GLenum, GLenum);
typedef void (*PFN_glClearStencil_t)(GLint);
typedef void (*PFN_glLineWidth_t)(GLfloat);
typedef void (*PFN_glPixelStorei_t)(GLenum, GLint);
typedef void (*PFN_glPolygonOffset_t)(GLfloat, GLfloat);
typedef void (*PFN_glSampleCoverage_t)(GLfloat, GLboolean);
typedef void (*PFN_glHint_t)(GLenum, GLenum);
typedef void (*PFN_glFlush_t)(void);
typedef void (*PFN_glVertexAttrib4f_t)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_glGenTextures_t)(GLsizei, GLuint *);
typedef void (*PFN_glDeleteTextures_t)(GLsizei, const GLuint *);
typedef void (*PFN_glActiveTexture_t)(GLenum);
typedef void (*PFN_glBindTexture_t)(GLenum, GLuint);
typedef void (*PFN_glTexParameteri_t)(GLenum, GLenum, GLint);
typedef void (*PFN_glTexParameterf_t)(GLenum, GLenum, GLfloat);
typedef void (*PFN_glGetTexParameterfv_t)(GLenum, GLenum, GLfloat *);
typedef void (*PFN_glCopyTexImage2D_t)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
typedef void (*PFN_glCopyTexSubImage2D_t)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
typedef void (*PFN_glCompressedTexImage2D_t)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
typedef void (*PFN_glCompressedTexSubImage2D_t)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *);
typedef void (*PFN_glTexImage2D_t)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (*PFN_glTexSubImage2D_t)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
typedef void (*PFN_glGenerateMipmap_t)(GLenum);
typedef void (*PFN_glUniform1i_t)(GLint, GLint);
typedef void (*PFN_glGetShaderPrecisionFormat_t)(GLenum, GLenum, GLint *, GLint *);
typedef void (*PFN_glReleaseShaderCompiler_t)(void);
typedef void (*PFN_glShaderBinary_t)(GLsizei, const GLuint *, GLenum, const void *, GLsizei);
typedef void (*PFN_glGenFramebuffers_t)(GLsizei, GLuint *);
typedef void (*PFN_glBindFramebuffer_t)(GLenum, GLuint);
typedef void (*PFN_glFramebufferTexture2D_t)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFN_glCheckFramebufferStatus_t)(GLenum);
typedef void (*PFN_glDeleteFramebuffers_t)(GLsizei, const GLuint *);
typedef void (*PFN_glGenRenderbuffers_t)(GLsizei, GLuint *);
typedef void (*PFN_glBindRenderbuffer_t)(GLenum, GLuint);
typedef void (*PFN_glRenderbufferStorage_t)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (*PFN_glFramebufferRenderbuffer_t)(GLenum, GLenum, GLenum, GLuint);
typedef void (*PFN_glDeleteRenderbuffers_t)(GLsizei, const GLuint *);

struct bridge_request {
    uint32_t magic;
    uint32_t cmd;
    uint32_t a[12];
    float rgba[4];
    float floats[BRIDGE_MAX_FLOATS];
    uint8_t bytes[BRIDGE_MAX_BYTES];
    char text[BRIDGE_MAX_TEXT];
};

struct bridge_response {
    uint32_t magic;
    uint32_t status;
    uint32_t value;
    uint32_t size;
    uint8_t pixel[4];
    uint8_t bytes[BRIDGE_MAX_BYTES];
    char text[BRIDGE_MAX_RESPONSE_TEXT];
};

struct gl_api {
    PFN_glClearColor_t glClearColor;
    PFN_glClear_t glClear;
    PFN_glCreateShader_t glCreateShader;
    PFN_glDeleteShader_t glDeleteShader;
    PFN_glShaderSource_t glShaderSource;
    PFN_glCompileShader_t glCompileShader;
    PFN_glGetShaderiv_t glGetShaderiv;
    PFN_glGetShaderInfoLog_t glGetShaderInfoLog;
    PFN_glGetShaderSource_t glGetShaderSource;
    PFN_glCreateProgram_t glCreateProgram;
    PFN_glDeleteProgram_t glDeleteProgram;
    PFN_glAttachShader_t glAttachShader;
    PFN_glDetachShader_t glDetachShader;
    PFN_glBindAttribLocation_t glBindAttribLocation;
    PFN_glGetAttribLocation_t glGetAttribLocation;
    PFN_glLinkProgram_t glLinkProgram;
    PFN_glValidateProgram_t glValidateProgram;
    PFN_glGetProgramiv_t glGetProgramiv;
    PFN_glGetProgramInfoLog_t glGetProgramInfoLog;
    PFN_glGetAttachedShaders_t glGetAttachedShaders;
    PFN_glGetActiveUniform_t glGetActiveUniform;
    PFN_glGetActiveAttrib_t glGetActiveAttrib;
    PFN_glGetUniformfv_t glGetUniformfv;
    PFN_glGetUniformiv_t glGetUniformiv;
    PFN_glUseProgram_t glUseProgram;
    PFN_glVertexAttribPointer_t glVertexAttribPointer;
    PFN_glEnableVertexAttribArray_t glEnableVertexAttribArray;
    PFN_glDisableVertexAttribArray_t glDisableVertexAttribArray;
    PFN_glDrawArrays_t glDrawArrays;
    PFN_glDrawElements_t glDrawElements;
    PFN_glFinish_t glFinish;
    PFN_glReadPixels_t glReadPixels;
    PFN_glGetError_t glGetError;
    PFN_glGetIntegerv_t glGetIntegerv;
    PFN_glGetFloatv_t glGetFloatv;
    PFN_glGetBooleanv_t glGetBooleanv;
    PFN_glIsEnabled_t glIsEnabled;
    PFN_glIsBuffer_t glIsBuffer;
    PFN_glIsTexture_t glIsTexture;
    PFN_glIsFramebuffer_t glIsFramebuffer;
    PFN_glIsRenderbuffer_t glIsRenderbuffer;
    PFN_glIsProgram_t glIsProgram;
    PFN_glIsShader_t glIsShader;
    PFN_glGetBufferParameteriv_t glGetBufferParameteriv;
    PFN_glGetTexParameteriv_t glGetTexParameteriv;
    PFN_glGetRenderbufferParameteriv_t glGetRenderbufferParameteriv;
    PFN_glGetVertexAttribiv_t glGetVertexAttribiv;
    PFN_glGetVertexAttribfv_t glGetVertexAttribfv;
    PFN_glGetVertexAttribPointerv_t glGetVertexAttribPointerv;
    PFN_glGetFramebufferAttachmentParameteriv_t glGetFramebufferAttachmentParameteriv;
    PFN_glGenBuffers_t glGenBuffers;
    PFN_glDeleteBuffers_t glDeleteBuffers;
    PFN_glBindBuffer_t glBindBuffer;
    PFN_glBufferData_t glBufferData;
    PFN_glBufferSubData_t glBufferSubData;
    PFN_glGetUniformLocation_t glGetUniformLocation;
    PFN_glUniform1f_t glUniform1f;
    PFN_glUniform2f_t glUniform2f;
    PFN_glUniform3f_t glUniform3f;
    PFN_glUniform4f_t glUniform4f;
    PFN_glUniform1fv_t glUniform1fv;
    PFN_glUniform2fv_t glUniform2fv;
    PFN_glUniform3fv_t glUniform3fv;
    PFN_glUniform4fv_t glUniform4fv;
    PFN_glUniform1iv_t glUniform1iv;
    PFN_glUniform2iv_t glUniform2iv;
    PFN_glUniform3iv_t glUniform3iv;
    PFN_glUniform4iv_t glUniform4iv;
    PFN_glUniformMatrix2fv_t glUniformMatrix2fv;
    PFN_glUniformMatrix3fv_t glUniformMatrix3fv;
    PFN_glUniformMatrix4fv_t glUniformMatrix4fv;
    PFN_glViewport_t glViewport;
    PFN_glEnable_t glEnable;
    PFN_glDisable_t glDisable;
    PFN_glBlendFunc_t glBlendFunc;
    PFN_glBlendColor_t glBlendColor;
    PFN_glBlendEquationSeparate_t glBlendEquationSeparate;
    PFN_glBlendFuncSeparate_t glBlendFuncSeparate;
    PFN_glDepthFunc_t glDepthFunc;
    PFN_glClearDepthf_t glClearDepthf;
    PFN_glDepthMask_t glDepthMask;
    PFN_glDepthRangef_t glDepthRangef;
    PFN_glScissor_t glScissor;
    PFN_glColorMask_t glColorMask;
    PFN_glCullFace_t glCullFace;
    PFN_glFrontFace_t glFrontFace;
    PFN_glStencilFunc_t glStencilFunc;
    PFN_glStencilOp_t glStencilOp;
    PFN_glStencilMask_t glStencilMask;
    PFN_glStencilFuncSeparate_t glStencilFuncSeparate;
    PFN_glStencilMaskSeparate_t glStencilMaskSeparate;
    PFN_glStencilOpSeparate_t glStencilOpSeparate;
    PFN_glClearStencil_t glClearStencil;
    PFN_glLineWidth_t glLineWidth;
    PFN_glPixelStorei_t glPixelStorei;
    PFN_glPolygonOffset_t glPolygonOffset;
    PFN_glSampleCoverage_t glSampleCoverage;
    PFN_glHint_t glHint;
    PFN_glFlush_t glFlush;
    PFN_glVertexAttrib4f_t glVertexAttrib4f;
    PFN_glGenTextures_t glGenTextures;
    PFN_glDeleteTextures_t glDeleteTextures;
    PFN_glActiveTexture_t glActiveTexture;
    PFN_glBindTexture_t glBindTexture;
    PFN_glTexParameteri_t glTexParameteri;
    PFN_glTexParameterf_t glTexParameterf;
    PFN_glGetTexParameterfv_t glGetTexParameterfv;
    PFN_glCopyTexImage2D_t glCopyTexImage2D;
    PFN_glCopyTexSubImage2D_t glCopyTexSubImage2D;
    PFN_glCompressedTexImage2D_t glCompressedTexImage2D;
    PFN_glCompressedTexSubImage2D_t glCompressedTexSubImage2D;
    PFN_glTexImage2D_t glTexImage2D;
    PFN_glTexSubImage2D_t glTexSubImage2D;
    PFN_glGenerateMipmap_t glGenerateMipmap;
    PFN_glUniform1i_t glUniform1i;
    PFN_glGetShaderPrecisionFormat_t glGetShaderPrecisionFormat;
    PFN_glReleaseShaderCompiler_t glReleaseShaderCompiler;
    PFN_glShaderBinary_t glShaderBinary;
    PFN_glGenFramebuffers_t glGenFramebuffers;
    PFN_glBindFramebuffer_t glBindFramebuffer;
    PFN_glFramebufferTexture2D_t glFramebufferTexture2D;
    PFN_glCheckFramebufferStatus_t glCheckFramebufferStatus;
    PFN_glDeleteFramebuffers_t glDeleteFramebuffers;
    PFN_glGenRenderbuffers_t glGenRenderbuffers;
    PFN_glBindRenderbuffer_t glBindRenderbuffer;
    PFN_glRenderbufferStorage_t glRenderbufferStorage;
    PFN_glFramebufferRenderbuffer_t glFramebufferRenderbuffer;
    PFN_glDeleteRenderbuffers_t glDeleteRenderbuffers;
};

static volatile sig_atomic_t keep_running = 1;
static GLuint shaders[4096];
static GLuint programs[4096];
static GLuint buffers[4096];
static GLuint textures[4096];
static GLuint framebuffers[4096];
static GLuint renderbuffers[4096];
static uint32_t next_shader_id = 1;
static uint32_t next_program_id = 1;
static uint32_t next_buffer_id = 1;
static uint32_t next_texture_id = 1;
static uint32_t next_framebuffer_id = 1;
static uint32_t next_renderbuffer_id = 1;
static float attrib_data[16][BRIDGE_MAX_FLOATS];

static void on_signal(int signo) {
    (void)signo;
    keep_running = 0;
}

static ssize_t read_full(int fd, void *buf, size_t size) {
    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, (uint8_t *)buf + done, size - done);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)n;
    }
    return (ssize_t)done;
}

static ssize_t write_full(int fd, const void *buf, size_t size) {
    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, (const uint8_t *)buf + done, size - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)n;
    }
    return (ssize_t)done;
}

static void *load_sym(void *lib, const char *name) {
    void *p = dlsym(lib, name);
    if (!p) {
        fprintf(stderr, "missing symbol %s: %s\n", name, dlerror());
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

static void check_shader(struct gl_api *gl, GLuint shader, const char *name) {
    GLint ok = 0;
    gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return;
    char log[4096] = {0};
    GLsizei n = 0;
    gl->glGetShaderInfoLog(shader, sizeof(log) - 1, &n, log);
    fprintf(stderr, "%s compile failed: %s\n", name, log);
    exit(1);
}

static void check_program(struct gl_api *gl, GLuint prog) {
    GLint ok = 0;
    gl->glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok) return;
    char log[4096] = {0};
    GLsizei n = 0;
    gl->glGetProgramInfoLog(prog, sizeof(log) - 1, &n, log);
    fprintf(stderr, "program link failed: %s\n", log);
    exit(1);
}

static GLuint make_program(struct gl_api *gl) {
    const char *vs_src =
        "attribute vec4 vPosition;\n"
        "void main() { gl_Position = vPosition; }\n";
    const char *fs_src =
        "precision mediump float;\n"
        "void main() { gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); }\n";

    GLuint vs = gl->glCreateShader(GL_VERTEX_SHADER);
    gl->glShaderSource(vs, 1, &vs_src, NULL);
    gl->glCompileShader(vs);
    check_shader(gl, vs, "vertex shader");

    GLuint fs = gl->glCreateShader(GL_FRAGMENT_SHADER);
    gl->glShaderSource(fs, 1, &fs_src, NULL);
    gl->glCompileShader(fs);
    check_shader(gl, fs, "fragment shader");

    GLuint prog = gl->glCreateProgram();
    gl->glAttachShader(prog, vs);
    gl->glAttachShader(prog, fs);
    gl->glBindAttribLocation(prog, 0, "vPosition");
    gl->glLinkProgram(prog);
    check_program(gl, prog);
    return prog;
}

static void render_color(struct gl_api *gl, const float rgba[4], uint8_t pixel[4]) {
    gl->glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    gl->glFinish();
    gl->glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
}

static GLuint lookup_shader(uint32_t id) {
    return id < (sizeof(shaders) / sizeof(shaders[0])) ? shaders[id] : 0;
}

static GLuint lookup_program(uint32_t id) {
    return id < (sizeof(programs) / sizeof(programs[0])) ? programs[id] : 0;
}

static GLuint lookup_buffer(uint32_t id) {
    return id < (sizeof(buffers) / sizeof(buffers[0])) ? buffers[id] : 0;
}

static GLuint lookup_texture(uint32_t id) {
    return id < (sizeof(textures) / sizeof(textures[0])) ? textures[id] : 0;
}

static GLuint lookup_framebuffer(uint32_t id) {
    return id < (sizeof(framebuffers) / sizeof(framebuffers[0])) ? framebuffers[id] : 0;
}

static GLuint lookup_renderbuffer(uint32_t id) {
    return id < (sizeof(renderbuffers) / sizeof(renderbuffers[0])) ? renderbuffers[id] : 0;
}

static uint32_t reverse_lookup(const GLuint *objects, size_t count, GLuint object) {
    if (!object) return 0;
    for (uint32_t id = 1; id < count; id++) {
        if (objects[id] == object) return id;
    }
    return 0;
}

static uint32_t uniform_component_count(struct gl_api *gl, GLuint program,
                                        GLint location) {
    GLint active = 0;
    gl->glGetProgramiv(program, 0x8B86, &active);
    for (GLint index = 0; index < active; index++) {
        GLchar name[256] = {0};
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        gl->glGetActiveUniform(program, (GLuint)index, sizeof(name) - 1,
                               &length, &size, &type, name);
        if (gl->glGetUniformLocation(program, name) != location) continue;
        switch (type) {
        case 0x1404:
        case 0x1406:
        case 0x8B56:
        case 0x8B5E:
        case 0x8B60: return 1;
        case 0x8B50:
        case 0x8B53:
        case 0x8B57:
        case 0x8B5A: return type == 0x8B5A ? 4 : 2;
        case 0x8B51:
        case 0x8B54:
        case 0x8B58:
        case 0x8B5B: return type == 0x8B5B ? 9 : 3;
        case 0x8B52:
        case 0x8B55:
        case 0x8B59: return 4;
        case 0x8B5C: return 16;
        default: return 1;
        }
    }
    return 1;
}

static void handle_request(struct gl_api *gl, const struct bridge_request *req,
                           struct bridge_response *resp) {
    resp->magic = BRIDGE_MAGIC;
    resp->status = 0;

    switch (req->cmd) {
    case BRIDGE_CMD_CLEAR_READ:
        render_color(gl, req->rgba, resp->pixel);
        break;

    case BRIDGE_CMD_CREATE_SHADER:
        if (next_shader_id >= sizeof(shaders) / sizeof(shaders[0])) {
            resp->status = 1;
            break;
        }
        shaders[next_shader_id] = gl->glCreateShader(req->a[0]);
        resp->value = next_shader_id++;
        break;

    case BRIDGE_CMD_SHADER_SOURCE: {
        GLuint shader = lookup_shader(req->a[0]);
        if (!shader) {
            resp->status = 1;
            break;
        }
        const GLchar *src = req->text;
        GLint len = (GLint)strnlen(req->text, BRIDGE_MAX_TEXT);
        gl->glShaderSource(shader, 1, &src, &len);
        break;
    }

    case BRIDGE_CMD_COMPILE_SHADER: {
        GLuint shader = lookup_shader(req->a[0]);
        if (!shader) resp->status = 1;
        else gl->glCompileShader(shader);
        break;
    }

    case BRIDGE_CMD_GET_SHADER_IV: {
        GLuint shader = lookup_shader(req->a[0]);
        GLint value = 0;
        if (!shader) resp->status = 1;
        else gl->glGetShaderiv(shader, req->a[1], &value);
        resp->value = (uint32_t)value;
        break;
    }

    case BRIDGE_CMD_GET_SHADER_INFO_LOG: {
        GLuint shader = lookup_shader(req->a[0]);
        GLsizei n = 0;
        if (!shader) resp->status = 1;
        else {
            gl->glGetShaderInfoLog(shader, BRIDGE_MAX_RESPONSE_TEXT - 1, &n, resp->text);
            resp->text[BRIDGE_MAX_RESPONSE_TEXT - 1] = '\0';
            resp->size = (uint32_t)n;
        }
        break;
    }

    case BRIDGE_CMD_GET_SHADER_SOURCE: {
        GLuint shader = lookup_shader(req->a[0]);
        GLsizei n = 0;
        if (!shader) resp->status = 1;
        else {
            gl->glGetShaderSource(shader, BRIDGE_MAX_RESPONSE_TEXT - 1, &n, resp->text);
            resp->text[BRIDGE_MAX_RESPONSE_TEXT - 1] = '\0';
            resp->size = (uint32_t)n;
        }
        break;
    }

    case BRIDGE_CMD_CREATE_PROGRAM:
        if (next_program_id >= sizeof(programs) / sizeof(programs[0])) {
            resp->status = 1;
            break;
        }
        programs[next_program_id] = gl->glCreateProgram();
        resp->value = next_program_id++;
        break;

    case BRIDGE_CMD_ATTACH_SHADER: {
        GLuint program = lookup_program(req->a[0]);
        GLuint shader = lookup_shader(req->a[1]);
        if (!program || !shader) resp->status = 1;
        else gl->glAttachShader(program, shader);
        break;
    }

    case BRIDGE_CMD_DETACH_SHADER: {
        GLuint program = lookup_program(req->a[0]);
        GLuint shader = lookup_shader(req->a[1]);
        if (!program || !shader) resp->status = 1;
        else gl->glDetachShader(program, shader);
        break;
    }

    case BRIDGE_CMD_BIND_ATTRIB_LOCATION: {
        GLuint program = lookup_program(req->a[0]);
        if (!program) resp->status = 1;
        else gl->glBindAttribLocation(program, req->a[1], req->text);
        break;
    }

    case BRIDGE_CMD_GET_ATTRIB_LOCATION: {
        GLuint program = lookup_program(req->a[0]);
        if (!program) resp->status = 1;
        else resp->value = (uint32_t)gl->glGetAttribLocation(program, req->text);
        break;
    }

    case BRIDGE_CMD_LINK_PROGRAM: {
        GLuint program = lookup_program(req->a[0]);
        if (!program) resp->status = 1;
        else gl->glLinkProgram(program);
        break;
    }

    case BRIDGE_CMD_VALIDATE_PROGRAM: {
        GLuint program = lookup_program(req->a[0]);
        if (!program) resp->status = 1;
        else gl->glValidateProgram(program);
        break;
    }

    case BRIDGE_CMD_GET_PROGRAM_IV: {
        GLuint program = lookup_program(req->a[0]);
        GLint value = 0;
        if (!program) resp->status = 1;
        else gl->glGetProgramiv(program, req->a[1], &value);
        resp->value = (uint32_t)value;
        break;
    }

    case BRIDGE_CMD_GET_PROGRAM_INFO_LOG: {
        GLuint program = lookup_program(req->a[0]);
        GLsizei n = 0;
        if (!program) resp->status = 1;
        else {
            gl->glGetProgramInfoLog(program, BRIDGE_MAX_RESPONSE_TEXT - 1, &n, resp->text);
            resp->text[BRIDGE_MAX_RESPONSE_TEXT - 1] = '\0';
            resp->size = (uint32_t)n;
        }
        break;
    }

    case BRIDGE_CMD_GET_ATTACHED_SHADERS: {
        GLuint program = lookup_program(req->a[0]);
        GLuint attached[16] = {0};
        GLsizei count = 0;
        if (!program) {
            resp->status = 1;
            break;
        }
        gl->glGetAttachedShaders(program, 16, &count, attached);
        uint32_t *logical = (uint32_t *)resp->bytes;
        for (GLsizei i = 0; i < count; i++) {
            logical[i] = reverse_lookup(
                shaders, sizeof(shaders) / sizeof(shaders[0]), attached[i]);
        }
        resp->size = (uint32_t)count * sizeof(uint32_t);
        resp->value = (uint32_t)count;
        break;
    }

    case BRIDGE_CMD_GET_ACTIVE_UNIFORM:
    case BRIDGE_CMD_GET_ACTIVE_ATTRIB: {
        GLuint program = lookup_program(req->a[0]);
        GLsizei n = 0;
        GLint array_size = 0;
        GLenum type = 0;
        if (!program) {
            resp->status = 1;
            break;
        }
        if (req->cmd == BRIDGE_CMD_GET_ACTIVE_UNIFORM) {
            gl->glGetActiveUniform(program, req->a[1], BRIDGE_MAX_RESPONSE_TEXT - 1,
                                   &n, &array_size, &type, resp->text);
        } else {
            gl->glGetActiveAttrib(program, req->a[1], BRIDGE_MAX_RESPONSE_TEXT - 1,
                                  &n, &array_size, &type, resp->text);
        }
        resp->text[BRIDGE_MAX_RESPONSE_TEXT - 1] = '\0';
        resp->size = (uint32_t)n;
        resp->value = (uint32_t)array_size;
        memcpy(resp->bytes, &type, sizeof(type));
        break;
    }

    case BRIDGE_CMD_GET_UNIFORM_VALUE: {
        GLuint program = lookup_program(req->a[0]);
        if (!program) {
            resp->status = 1;
            break;
        }
        uint32_t count = uniform_component_count(gl, program, (GLint)req->a[1]);
        if (req->a[2] == 1) {
            GLfloat values[16] = {0};
            gl->glGetUniformfv(program, (GLint)req->a[1], values);
            resp->size = count * sizeof(GLfloat);
            memcpy(resp->bytes, values, resp->size);
        } else if (req->a[2] == 2) {
            GLint values[16] = {0};
            gl->glGetUniformiv(program, (GLint)req->a[1], values);
            resp->size = count * sizeof(GLint);
            memcpy(resp->bytes, values, resp->size);
        } else {
            resp->status = 1;
        }
        break;
    }

    case BRIDGE_CMD_USE_PROGRAM: {
        GLuint program = lookup_program(req->a[0]);
        if (!program) resp->status = 1;
        else gl->glUseProgram(program);
        break;
    }

    case BRIDGE_CMD_VERTEX_ATTRIB_POINTER: {
        uint32_t index = req->a[0];
        uint32_t size = req->a[1];
        uint32_t count = req->a[4];
        size_t total = (size_t)size * count;
        if (index >= 16 || size == 0 || total > BRIDGE_MAX_FLOATS) {
            resp->status = 1;
            break;
        }
        memcpy(attrib_data[index], req->floats, total * sizeof(float));
        gl->glVertexAttribPointer(index, (GLint)size, req->a[2], (GLboolean)req->a[3],
                                  (GLsizei)req->a[5],
                                  req->a[6] ? (const void *)(uintptr_t)req->a[7] : attrib_data[index]);
        break;
    }

    case BRIDGE_CMD_ENABLE_VERTEX_ATTRIB_ARRAY:
        gl->glEnableVertexAttribArray(req->a[0]);
        break;

    case BRIDGE_CMD_DISABLE_VERTEX_ATTRIB_ARRAY:
        gl->glDisableVertexAttribArray(req->a[0]);
        break;

    case BRIDGE_CMD_CLEAR_COLOR:
        gl->glClearColor(req->rgba[0], req->rgba[1], req->rgba[2], req->rgba[3]);
        break;

    case BRIDGE_CMD_CLEAR:
        gl->glClear(req->a[0]);
        break;

    case BRIDGE_CMD_DRAW_ARRAYS:
        gl->glDrawArrays(req->a[0], (GLint)req->a[1], (GLsizei)req->a[2]);
        break;

    case BRIDGE_CMD_DRAW_ELEMENTS:
        gl->glDrawElements(req->a[0], (GLsizei)req->a[1], req->a[2],
                           (const void *)(uintptr_t)req->a[3]);
        break;

    case BRIDGE_CMD_FINISH:
        gl->glFinish();
        break;

    case 999: {
        uint32_t width = req->a[2];
        uint32_t height = req->a[3];
        if (shared_pixels) {
            gl->glReadPixels((GLint)req->a[0], (GLint)req->a[1], (GLsizei)width, (GLsizei)height, 0x1908, 0x1401, shared_pixels);
        }
        break;
    }
    case BRIDGE_CMD_READ_PIXELS:
        if ((uint64_t)req->a[2] * req->a[3] * 4 > BRIDGE_MAX_BYTES) {
            resp->status = 1;
            break;
        }
        resp->size = req->a[2] * req->a[3] * 4;
        gl->glReadPixels((GLint)req->a[0], (GLint)req->a[1],
                         (GLsizei)req->a[2], (GLsizei)req->a[3],
                         GL_RGBA, GL_UNSIGNED_BYTE, resp->bytes);
        if (resp->size >= 4) memcpy(resp->pixel, resp->bytes, 4);
        break;

    case BRIDGE_CMD_GET_ERROR:
        resp->value = gl->glGetError();
        break;

    case BRIDGE_CMD_GET_INTEGERV: {
        GLint value = 0;
        gl->glGetIntegerv(req->a[0], &value);
        resp->value = (uint32_t)value;
        break;
    }

    case BRIDGE_CMD_IS_OBJECT: {
        GLuint object = 0;
        switch (req->a[0]) {
        case 1: object = lookup_buffer(req->a[1]); resp->value = gl->glIsBuffer(object); break;
        case 2: object = lookup_texture(req->a[1]); resp->value = gl->glIsTexture(object); break;
        case 3: object = lookup_framebuffer(req->a[1]); resp->value = gl->glIsFramebuffer(object); break;
        case 4: object = lookup_renderbuffer(req->a[1]); resp->value = gl->glIsRenderbuffer(object); break;
        case 5: object = lookup_program(req->a[1]); resp->value = gl->glIsProgram(object); break;
        case 6: object = lookup_shader(req->a[1]); resp->value = gl->glIsShader(object); break;
        default: resp->status = 1; break;
        }
        break;
    }

    case BRIDGE_CMD_IS_ENABLED:
        resp->value = gl->glIsEnabled(req->a[0]);
        break;

    case BRIDGE_CMD_GET_BUFFER_PARAMETER_IV: {
        GLint value = 0;
        gl->glGetBufferParameteriv(req->a[0], req->a[1], &value);
        resp->value = (uint32_t)value;
        break;
    }

    case BRIDGE_CMD_GET_TEX_PARAMETER_IV: {
        GLint value = 0;
        gl->glGetTexParameteriv(req->a[0], req->a[1], &value);
        resp->value = (uint32_t)value;
        break;
    }

    case BRIDGE_CMD_GET_RENDERBUFFER_PARAMETER_IV: {
        GLint value = 0;
        gl->glGetRenderbufferParameteriv(req->a[0], req->a[1], &value);
        resp->value = (uint32_t)value;
        break;
    }

    case BRIDGE_CMD_GET_VERTEX_ATTRIB_IV: {
        GLint value = 0;
        gl->glGetVertexAttribiv(req->a[0], req->a[1], &value);
        resp->value = (uint32_t)value;
        break;
    }

    case BRIDGE_CMD_GET_VERTEX_ATTRIB_FV: {
        GLfloat values[4] = {0};
        gl->glGetVertexAttribfv(req->a[0], req->a[1], values);
        resp->size = sizeof(values);
        memcpy(resp->bytes, values, sizeof(values));
        break;
    }

    case BRIDGE_CMD_GET_VERTEX_ATTRIB_POINTER: {
        void *pointer = NULL;
        gl->glGetVertexAttribPointerv(req->a[0], req->a[1], &pointer);
        resp->value = (uint32_t)(uintptr_t)pointer;
        break;
    }

    case BRIDGE_CMD_GET_STATE: {
        uint32_t count = req->a[2];
        if (count == 0 || count > 16) {
            resp->status = 1;
            break;
        }
        if (req->a[1] == 1) {
            GLint values[16] = {0};
            gl->glGetIntegerv(req->a[0], values);
            resp->size = count * sizeof(GLint);
            memcpy(resp->bytes, values, resp->size);
        } else if (req->a[1] == 2) {
            GLfloat values[16] = {0};
            gl->glGetFloatv(req->a[0], values);
            resp->size = count * sizeof(GLfloat);
            memcpy(resp->bytes, values, resp->size);
        } else if (req->a[1] == 3) {
            GLboolean values[16] = {0};
            gl->glGetBooleanv(req->a[0], values);
            resp->size = count * sizeof(GLboolean);
            memcpy(resp->bytes, values, resp->size);
        } else {
            resp->status = 1;
        }
        break;
    }

    case BRIDGE_CMD_GET_FRAMEBUFFER_ATTACHMENT_PARAMETER_IV: {
        GLint value = 0;
        gl->glGetFramebufferAttachmentParameteriv(req->a[0], req->a[1],
                                                   req->a[2], &value);
        if (req->a[2] == 0x8CD1) {
            GLint object_type = 0;
            gl->glGetFramebufferAttachmentParameteriv(req->a[0], req->a[1],
                                                       0x8CD0, &object_type);
            if (object_type == 0x1702) {
                value = (GLint)reverse_lookup(
                    textures, sizeof(textures) / sizeof(textures[0]), (GLuint)value);
            } else if (object_type == 0x8D41) {
                value = (GLint)reverse_lookup(
                    renderbuffers, sizeof(renderbuffers) / sizeof(renderbuffers[0]),
                    (GLuint)value);
            }
        }
        resp->value = (uint32_t)value;
        break;
    }

    case BRIDGE_CMD_GEN_BUFFER:
        if (next_buffer_id >= sizeof(buffers) / sizeof(buffers[0])) {
            resp->status = 1;
            break;
        }
        gl->glGenBuffers(1, &buffers[next_buffer_id]);
        resp->value = next_buffer_id++;
        break;

    case BRIDGE_CMD_BIND_BUFFER: {
        GLuint buffer = req->a[1] ? lookup_buffer(req->a[1]) : 0;
        if (req->a[1] && !buffer) resp->status = 1;
        else gl->glBindBuffer(req->a[0], buffer);
        break;
    }

    case BRIDGE_CMD_BUFFER_DATA: {
        uint32_t size = req->a[1];
        uint32_t byte_count = req->a[3];
        if (byte_count > BRIDGE_MAX_BYTES || byte_count > size) {
            resp->status = 1;
            break;
        }
        gl->glBufferData(req->a[0], (intptr_t)size,
                         byte_count ? req->bytes : NULL, req->a[2]);
        break;
    }

    case BRIDGE_CMD_BUFFER_SUB_DATA: {
        uint32_t byte_count = req->a[2];
        if (byte_count > BRIDGE_MAX_BYTES) {
            resp->status = 1;
            break;
        }
        gl->glBufferSubData(req->a[0], (intptr_t)req->a[1],
                            (intptr_t)byte_count, req->bytes);
        break;
    }

    case BRIDGE_CMD_GET_UNIFORM_LOCATION: {
        GLuint program = lookup_program(req->a[0]);
        if (!program) resp->status = 1;
        else resp->value = (uint32_t)gl->glGetUniformLocation(program, req->text);
        break;
    }

    case BRIDGE_CMD_UNIFORM4F:
        gl->glUniform4f((GLint)req->a[0], req->rgba[0], req->rgba[1], req->rgba[2], req->rgba[3]);
        break;

    case BRIDGE_CMD_UNIFORM1F:
        gl->glUniform1f((GLint)req->a[0], req->rgba[0]);
        break;

    case BRIDGE_CMD_UNIFORM2F:
        gl->glUniform2f((GLint)req->a[0], req->rgba[0], req->rgba[1]);
        break;

    case BRIDGE_CMD_UNIFORM3F:
        gl->glUniform3f((GLint)req->a[0], req->rgba[0], req->rgba[1], req->rgba[2]);
        break;

    case BRIDGE_CMD_UNIFORM_FV: {
        uint32_t components = req->a[1];
        uint32_t count = req->a[2];
        if (components < 1 || components > 4 ||
            (uint64_t)components * count > BRIDGE_MAX_FLOATS) {
            resp->status = 1;
            break;
        }
        switch (components) {
        case 1: gl->glUniform1fv((GLint)req->a[0], (GLsizei)count, req->floats); break;
        case 2: gl->glUniform2fv((GLint)req->a[0], (GLsizei)count, req->floats); break;
        case 3: gl->glUniform3fv((GLint)req->a[0], (GLsizei)count, req->floats); break;
        case 4: gl->glUniform4fv((GLint)req->a[0], (GLsizei)count, req->floats); break;
        }
        break;
    }

    case BRIDGE_CMD_UNIFORM_IV: {
        uint32_t components = req->a[1];
        uint32_t count = req->a[2];
        if (components < 1 || components > 4 ||
            (uint64_t)components * count * sizeof(GLint) > BRIDGE_MAX_BYTES) {
            resp->status = 1;
            break;
        }
        const GLint *values = (const GLint *)req->bytes;
        switch (components) {
        case 1: gl->glUniform1iv((GLint)req->a[0], (GLsizei)count, values); break;
        case 2: gl->glUniform2iv((GLint)req->a[0], (GLsizei)count, values); break;
        case 3: gl->glUniform3iv((GLint)req->a[0], (GLsizei)count, values); break;
        case 4: gl->glUniform4iv((GLint)req->a[0], (GLsizei)count, values); break;
        }
        break;
    }

    case BRIDGE_CMD_UNIFORM_MATRIX2FV:
        if (req->a[1] > BRIDGE_MAX_FLOATS / 4) {
            resp->status = 1;
            break;
        }
        gl->glUniformMatrix2fv((GLint)req->a[0], (GLsizei)req->a[1],
                               (GLboolean)req->a[2], req->floats);
        break;

    case BRIDGE_CMD_UNIFORM_MATRIX3FV:
        if (req->a[1] > BRIDGE_MAX_FLOATS / 9) {
            resp->status = 1;
            break;
        }
        gl->glUniformMatrix3fv((GLint)req->a[0], (GLsizei)req->a[1],
                               (GLboolean)req->a[2], req->floats);
        break;

    case BRIDGE_CMD_UNIFORM_MATRIX4FV:
        if (req->a[1] > BRIDGE_MAX_FLOATS / 16) {
            resp->status = 1;
            break;
        }
        gl->glUniformMatrix4fv((GLint)req->a[0], (GLsizei)req->a[1],
                               (GLboolean)req->a[2], req->floats);
        break;

    case BRIDGE_CMD_VIEWPORT:
        gl->glViewport((GLint)req->a[0], (GLint)req->a[1], (GLsizei)req->a[2], (GLsizei)req->a[3]);
        break;

    case BRIDGE_CMD_ENABLE:
        gl->glEnable(req->a[0]);
        break;

    case BRIDGE_CMD_DISABLE:
        gl->glDisable(req->a[0]);
        break;

    case BRIDGE_CMD_BLEND_FUNC:
        gl->glBlendFunc(req->a[0], req->a[1]);
        break;

    case BRIDGE_CMD_BLEND_COLOR:
        gl->glBlendColor(req->rgba[0], req->rgba[1], req->rgba[2], req->rgba[3]);
        break;

    case BRIDGE_CMD_BLEND_EQUATION_SEPARATE:
        gl->glBlendEquationSeparate(req->a[0], req->a[1]);
        break;

    case BRIDGE_CMD_BLEND_FUNC_SEPARATE:
        gl->glBlendFuncSeparate(req->a[0], req->a[1], req->a[2], req->a[3]);
        break;

    case BRIDGE_CMD_DEPTH_FUNC:
        gl->glDepthFunc(req->a[0]);
        break;

    case BRIDGE_CMD_CLEAR_DEPTHF:
        gl->glClearDepthf(req->rgba[0]);
        break;

    case BRIDGE_CMD_DEPTH_MASK:
        gl->glDepthMask((GLboolean)req->a[0]);
        break;

    case BRIDGE_CMD_DEPTH_RANGEF:
        gl->glDepthRangef(req->rgba[0], req->rgba[1]);
        break;

    case BRIDGE_CMD_SCISSOR:
        gl->glScissor((GLint)req->a[0], (GLint)req->a[1],
                      (GLsizei)req->a[2], (GLsizei)req->a[3]);
        break;

    case BRIDGE_CMD_COLOR_MASK:
        gl->glColorMask((GLboolean)req->a[0], (GLboolean)req->a[1],
                        (GLboolean)req->a[2], (GLboolean)req->a[3]);
        break;

    case BRIDGE_CMD_CULL_FACE:
        gl->glCullFace(req->a[0]);
        break;

    case BRIDGE_CMD_FRONT_FACE:
        gl->glFrontFace(req->a[0]);
        break;

    case BRIDGE_CMD_STENCIL_FUNC:
        gl->glStencilFunc(req->a[0], (GLint)req->a[1], req->a[2]);
        break;

    case BRIDGE_CMD_STENCIL_OP:
        gl->glStencilOp(req->a[0], req->a[1], req->a[2]);
        break;

    case BRIDGE_CMD_STENCIL_MASK:
        gl->glStencilMask(req->a[0]);
        break;

    case BRIDGE_CMD_STENCIL_FUNC_SEPARATE:
        gl->glStencilFuncSeparate(req->a[0], req->a[1],
                                  (GLint)req->a[2], req->a[3]);
        break;

    case BRIDGE_CMD_STENCIL_MASK_SEPARATE:
        gl->glStencilMaskSeparate(req->a[0], req->a[1]);
        break;

    case BRIDGE_CMD_STENCIL_OP_SEPARATE:
        gl->glStencilOpSeparate(req->a[0], req->a[1], req->a[2], req->a[3]);
        break;

    case BRIDGE_CMD_CLEAR_STENCIL:
        gl->glClearStencil((GLint)req->a[0]);
        break;

    case BRIDGE_CMD_LINE_WIDTH:
        gl->glLineWidth(req->rgba[0]);
        break;

    case BRIDGE_CMD_PIXEL_STOREI:
        gl->glPixelStorei(req->a[0], (GLint)req->a[1]);
        break;

    case BRIDGE_CMD_POLYGON_OFFSET:
        gl->glPolygonOffset(req->rgba[0], req->rgba[1]);
        break;

    case BRIDGE_CMD_SAMPLE_COVERAGE:
        gl->glSampleCoverage(req->rgba[0], (GLboolean)req->a[0]);
        break;

    case BRIDGE_CMD_HINT:
        gl->glHint(req->a[0], req->a[1]);
        break;

    case BRIDGE_CMD_FLUSH:
        gl->glFlush();
        break;

    case BRIDGE_CMD_VERTEX_ATTRIB_VALUE:
        gl->glVertexAttrib4f(req->a[0], req->rgba[0], req->rgba[1],
                             req->rgba[2], req->rgba[3]);
        break;

    case BRIDGE_CMD_GEN_TEXTURE:
        if (next_texture_id >= sizeof(textures) / sizeof(textures[0])) {
            resp->status = 1;
            break;
        }
        gl->glGenTextures(1, &textures[next_texture_id]);
        resp->value = next_texture_id++;
        break;

    case BRIDGE_CMD_ACTIVE_TEXTURE:
        gl->glActiveTexture(req->a[0]);
        break;

    case BRIDGE_CMD_BIND_TEXTURE: {
        GLuint texture = req->a[1] ? lookup_texture(req->a[1]) : 0;
        if (req->a[1] && !texture) resp->status = 1;
        else gl->glBindTexture(req->a[0], texture);
        break;
    }

    case BRIDGE_CMD_TEX_PARAMETERI:
        gl->glTexParameteri(req->a[0], req->a[1], (GLint)req->a[2]);
        break;

    case BRIDGE_CMD_TEX_PARAMETER_F:
        gl->glTexParameterf(req->a[0], req->a[1], req->rgba[0]);
        break;

    case BRIDGE_CMD_GET_TEX_PARAMETER_FV: {
        GLfloat value = 0;
        gl->glGetTexParameterfv(req->a[0], req->a[1], &value);
        resp->size = sizeof(value);
        memcpy(resp->bytes, &value, sizeof(value));
        break;
    }

    case BRIDGE_CMD_TEX_IMAGE_2D: {
        uint32_t byte_count = req->a[7];
        if (byte_count > BRIDGE_MAX_BYTES) {
            resp->status = 1;
            break;
        }
        gl->glTexImage2D(req->a[0], (GLint)req->a[1], (GLint)req->a[2],
                         (GLsizei)req->a[3], (GLsizei)req->a[4], (GLint)req->a[5],
                         req->a[6], req->a[8], byte_count ? req->bytes : NULL);
        break;
    }

    case BRIDGE_CMD_TEX_SUB_IMAGE_2D: {
        uint32_t byte_count = req->a[8];
        if (byte_count > BRIDGE_MAX_BYTES) {
            resp->status = 1;
            break;
        }
        gl->glTexSubImage2D(req->a[0], (GLint)req->a[1], (GLint)req->a[2],
                            (GLint)req->a[3], (GLsizei)req->a[4], (GLsizei)req->a[5],
                            req->a[6], req->a[7], byte_count ? req->bytes : NULL);
        break;
    }

    case BRIDGE_CMD_GENERATE_MIPMAP:
        gl->glGenerateMipmap(req->a[0]);
        break;

    case BRIDGE_CMD_COPY_TEX_IMAGE_2D:
        gl->glCopyTexImage2D(req->a[0], (GLint)req->a[1], req->a[2],
                             (GLint)req->a[3], (GLint)req->a[4],
                             (GLsizei)req->a[5], (GLsizei)req->a[6],
                             (GLint)req->a[7]);
        break;

    case BRIDGE_CMD_COPY_TEX_SUB_IMAGE_2D:
        gl->glCopyTexSubImage2D(req->a[0], (GLint)req->a[1],
                                (GLint)req->a[2], (GLint)req->a[3],
                                (GLint)req->a[4], (GLint)req->a[5],
                                (GLsizei)req->a[6], (GLsizei)req->a[7]);
        break;

    case BRIDGE_CMD_COMPRESSED_TEX_IMAGE_2D:
        if (req->a[7] > BRIDGE_MAX_BYTES) {
            resp->status = 1;
            break;
        }
        gl->glCompressedTexImage2D(req->a[0], (GLint)req->a[1], req->a[2],
                                   (GLsizei)req->a[3], (GLsizei)req->a[4],
                                   (GLint)req->a[5], (GLsizei)req->a[7],
                                   req->a[7] ? req->bytes : NULL);
        break;

    case BRIDGE_CMD_COMPRESSED_TEX_SUB_IMAGE_2D:
        if (req->a[8] > BRIDGE_MAX_BYTES) {
            resp->status = 1;
            break;
        }
        gl->glCompressedTexSubImage2D(req->a[0], (GLint)req->a[1],
                                      (GLint)req->a[2], (GLint)req->a[3],
                                      (GLsizei)req->a[4], (GLsizei)req->a[5],
                                      req->a[6], (GLsizei)req->a[8],
                                      req->a[8] ? req->bytes : NULL);
        break;

    case BRIDGE_CMD_GEN_FRAMEBUFFER:
        if (next_framebuffer_id >= sizeof(framebuffers) / sizeof(framebuffers[0])) {
            resp->status = 1;
            break;
        }
        gl->glGenFramebuffers(1, &framebuffers[next_framebuffer_id]);
        resp->value = next_framebuffer_id++;
        break;

    case BRIDGE_CMD_BIND_FRAMEBUFFER: {
        GLuint framebuffer = req->a[1] ? lookup_framebuffer(req->a[1]) : 0;
        if (req->a[1] && !framebuffer) resp->status = 1;
        else gl->glBindFramebuffer(req->a[0], framebuffer);
        break;
    }

    case BRIDGE_CMD_FRAMEBUFFER_TEXTURE_2D: {
        GLuint texture = req->a[3] ? lookup_texture(req->a[3]) : 0;
        if (req->a[3] && !texture) resp->status = 1;
        else gl->glFramebufferTexture2D(req->a[0], req->a[1], req->a[2],
                                        texture, (GLint)req->a[4]);
        break;
    }

    case BRIDGE_CMD_CHECK_FRAMEBUFFER_STATUS:
        resp->value = gl->glCheckFramebufferStatus(req->a[0]);
        break;

    case BRIDGE_CMD_DELETE_FRAMEBUFFER: {
        GLuint framebuffer = lookup_framebuffer(req->a[0]);
        if (framebuffer) {
            gl->glDeleteFramebuffers(1, &framebuffer);
            if (req->a[0] < sizeof(framebuffers) / sizeof(framebuffers[0]))
                framebuffers[req->a[0]] = 0;
        }
        break;
    }

    case BRIDGE_CMD_GEN_RENDERBUFFER:
        if (next_renderbuffer_id >= sizeof(renderbuffers) / sizeof(renderbuffers[0])) {
            resp->status = 1;
            break;
        }
        gl->glGenRenderbuffers(1, &renderbuffers[next_renderbuffer_id]);
        resp->value = next_renderbuffer_id++;
        break;

    case BRIDGE_CMD_BIND_RENDERBUFFER: {
        GLuint renderbuffer = req->a[1] ? lookup_renderbuffer(req->a[1]) : 0;
        if (req->a[1] && !renderbuffer) resp->status = 1;
        else gl->glBindRenderbuffer(req->a[0], renderbuffer);
        break;
    }

    case BRIDGE_CMD_RENDERBUFFER_STORAGE:
        gl->glRenderbufferStorage(req->a[0], req->a[1],
                                  (GLsizei)req->a[2], (GLsizei)req->a[3]);
        break;

    case BRIDGE_CMD_FRAMEBUFFER_RENDERBUFFER: {
        GLuint renderbuffer = req->a[3] ? lookup_renderbuffer(req->a[3]) : 0;
        if (req->a[3] && !renderbuffer) resp->status = 1;
        else gl->glFramebufferRenderbuffer(req->a[0], req->a[1], req->a[2], renderbuffer);
        break;
    }

    case BRIDGE_CMD_DELETE_RENDERBUFFER: {
        GLuint renderbuffer = lookup_renderbuffer(req->a[0]);
        if (renderbuffer) {
            gl->glDeleteRenderbuffers(1, &renderbuffer);
            if (req->a[0] < sizeof(renderbuffers) / sizeof(renderbuffers[0]))
                renderbuffers[req->a[0]] = 0;
        }
        break;
    }

    case BRIDGE_CMD_UNIFORM1I:
        gl->glUniform1i((GLint)req->a[0], (GLint)req->a[1]);
        break;

    case BRIDGE_CMD_GET_SHADER_PRECISION_FORMAT: {
        GLint range[2] = {0};
        GLint precision = 0;
        gl->glGetShaderPrecisionFormat(req->a[0], req->a[1], range, &precision);
        memcpy(resp->bytes, range, sizeof(range));
        memcpy(resp->bytes + sizeof(range), &precision, sizeof(precision));
        resp->size = sizeof(range) + sizeof(precision);
        break;
    }

    case BRIDGE_CMD_RELEASE_SHADER_COMPILER:
        gl->glReleaseShaderCompiler();
        break;

    case BRIDGE_CMD_SHADER_BINARY: {
        uint32_t count = req->a[0];
        uint32_t length = req->a[2];
        if (count == 0 || count > 8 || length > BRIDGE_MAX_BYTES - count * sizeof(GLuint)) {
            resp->status = 1;
            break;
        }
        GLuint real_shaders[8] = {0};
        const uint32_t *logical = (const uint32_t *)req->bytes;
        for (uint32_t i = 0; i < count; i++) {
            real_shaders[i] = lookup_shader(logical[i]);
            if (!real_shaders[i]) {
                resp->status = 1;
                break;
            }
        }
        if (!resp->status) {
            gl->glShaderBinary((GLsizei)count, real_shaders, req->a[1],
                               req->bytes + count * sizeof(uint32_t),
                               (GLsizei)length);
        }
        break;
    }

    case BRIDGE_CMD_DELETE_SHADER: {
        GLuint shader = lookup_shader(req->a[0]);
        if (shader) {
            gl->glDeleteShader(shader);
            if (req->a[0] < sizeof(shaders) / sizeof(shaders[0])) shaders[req->a[0]] = 0;
        }
        break;
    }

    case BRIDGE_CMD_DELETE_PROGRAM: {
        GLuint program = lookup_program(req->a[0]);
        if (program) {
            gl->glDeleteProgram(program);
            if (req->a[0] < sizeof(programs) / sizeof(programs[0])) programs[req->a[0]] = 0;
        }
        break;
    }

    case BRIDGE_CMD_DELETE_BUFFER: {
        GLuint buffer = lookup_buffer(req->a[0]);
        if (buffer) {
            gl->glDeleteBuffers(1, &buffer);
            if (req->a[0] < sizeof(buffers) / sizeof(buffers[0])) buffers[req->a[0]] = 0;
        }
        break;
    }

    case BRIDGE_CMD_DELETE_TEXTURE: {
        GLuint texture = lookup_texture(req->a[0]);
        if (texture) {
            gl->glDeleteTextures(1, &texture);
            if (req->a[0] < sizeof(textures) / sizeof(textures[0])) textures[req->a[0]] = 0;
        }
        break;
    }

    default:
        resp->status = 1;
        break;
    }
}

static int make_server_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    unlink(BRIDGE_SOCKET);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", BRIDGE_SOCKET);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }
    chmod(BRIDGE_SOCKET, 0666);
    if (listen(fd, 8) < 0) {
        perror("listen");
        exit(1);
    }
    return fd;
}



    
    
    
    
    
int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    const char *libpath = NULL;
    void *lib = load_vendor_gles(&libpath);
    if (!lib) {
        fprintf(stderr, "dlopen vendor Mali failed: %s\n", dlerror());
        return 1;
    }

    PFN_eglGetDisplay_t eglGetDisplay = load_sym(lib, "eglGetDisplay");
    PFN_eglInitialize_t eglInitialize = load_sym(lib, "eglInitialize");
    PFN_eglChooseConfig_t eglChooseConfig = load_sym(lib, "eglChooseConfig");
    PFN_eglCreatePbufferSurface_t eglCreatePbufferSurface = load_sym(lib, "eglCreatePbufferSurface");
    PFN_eglCreateContext_t eglCreateContext = load_sym(lib, "eglCreateContext");
    PFN_eglMakeCurrent_t eglMakeCurrent = load_sym(lib, "eglMakeCurrent");
    PFN_eglGetError_t eglGetError = load_sym(lib, "eglGetError");
    PFN_glGetString_t glGetString = load_sym(lib, "glGetString");

    struct gl_api gl = {
        .glClearColor = load_sym(lib, "glClearColor"),
        .glClear = load_sym(lib, "glClear"),
        .glCreateShader = load_sym(lib, "glCreateShader"),
        .glDeleteShader = load_sym(lib, "glDeleteShader"),
        .glShaderSource = load_sym(lib, "glShaderSource"),
        .glCompileShader = load_sym(lib, "glCompileShader"),
        .glGetShaderiv = load_sym(lib, "glGetShaderiv"),
        .glGetShaderInfoLog = load_sym(lib, "glGetShaderInfoLog"),
        .glGetShaderSource = load_sym(lib, "glGetShaderSource"),
        .glCreateProgram = load_sym(lib, "glCreateProgram"),
        .glDeleteProgram = load_sym(lib, "glDeleteProgram"),
        .glAttachShader = load_sym(lib, "glAttachShader"),
        .glDetachShader = load_sym(lib, "glDetachShader"),
        .glBindAttribLocation = load_sym(lib, "glBindAttribLocation"),
        .glGetAttribLocation = load_sym(lib, "glGetAttribLocation"),
        .glLinkProgram = load_sym(lib, "glLinkProgram"),
        .glValidateProgram = load_sym(lib, "glValidateProgram"),
        .glGetProgramiv = load_sym(lib, "glGetProgramiv"),
        .glGetProgramInfoLog = load_sym(lib, "glGetProgramInfoLog"),
        .glGetAttachedShaders = load_sym(lib, "glGetAttachedShaders"),
        .glGetActiveUniform = load_sym(lib, "glGetActiveUniform"),
        .glGetActiveAttrib = load_sym(lib, "glGetActiveAttrib"),
        .glGetUniformfv = load_sym(lib, "glGetUniformfv"),
        .glGetUniformiv = load_sym(lib, "glGetUniformiv"),
        .glUseProgram = load_sym(lib, "glUseProgram"),
        .glVertexAttribPointer = load_sym(lib, "glVertexAttribPointer"),
        .glEnableVertexAttribArray = load_sym(lib, "glEnableVertexAttribArray"),
        .glDisableVertexAttribArray = load_sym(lib, "glDisableVertexAttribArray"),
        .glDrawArrays = load_sym(lib, "glDrawArrays"),
        .glDrawElements = load_sym(lib, "glDrawElements"),
        .glFinish = load_sym(lib, "glFinish"),
        .glReadPixels = load_sym(lib, "glReadPixels"),
        .glGetError = load_sym(lib, "glGetError"),
        .glGetIntegerv = load_sym(lib, "glGetIntegerv"),
        .glGetFloatv = load_sym(lib, "glGetFloatv"),
        .glGetBooleanv = load_sym(lib, "glGetBooleanv"),
        .glIsEnabled = load_sym(lib, "glIsEnabled"),
        .glIsBuffer = load_sym(lib, "glIsBuffer"),
        .glIsTexture = load_sym(lib, "glIsTexture"),
        .glIsFramebuffer = load_sym(lib, "glIsFramebuffer"),
        .glIsRenderbuffer = load_sym(lib, "glIsRenderbuffer"),
        .glIsProgram = load_sym(lib, "glIsProgram"),
        .glIsShader = load_sym(lib, "glIsShader"),
        .glGetBufferParameteriv = load_sym(lib, "glGetBufferParameteriv"),
        .glGetTexParameteriv = load_sym(lib, "glGetTexParameteriv"),
        .glGetRenderbufferParameteriv = load_sym(lib, "glGetRenderbufferParameteriv"),
        .glGetVertexAttribiv = load_sym(lib, "glGetVertexAttribiv"),
        .glGetVertexAttribfv = load_sym(lib, "glGetVertexAttribfv"),
        .glGetVertexAttribPointerv = load_sym(lib, "glGetVertexAttribPointerv"),
        .glGetFramebufferAttachmentParameteriv =
            load_sym(lib, "glGetFramebufferAttachmentParameteriv"),
        .glGenBuffers = load_sym(lib, "glGenBuffers"),
        .glDeleteBuffers = load_sym(lib, "glDeleteBuffers"),
        .glBindBuffer = load_sym(lib, "glBindBuffer"),
        .glBufferData = load_sym(lib, "glBufferData"),
        .glBufferSubData = load_sym(lib, "glBufferSubData"),
        .glGetUniformLocation = load_sym(lib, "glGetUniformLocation"),
        .glUniform1f = load_sym(lib, "glUniform1f"),
        .glUniform2f = load_sym(lib, "glUniform2f"),
        .glUniform3f = load_sym(lib, "glUniform3f"),
        .glUniform4f = load_sym(lib, "glUniform4f"),
        .glUniform1fv = load_sym(lib, "glUniform1fv"),
        .glUniform2fv = load_sym(lib, "glUniform2fv"),
        .glUniform3fv = load_sym(lib, "glUniform3fv"),
        .glUniform4fv = load_sym(lib, "glUniform4fv"),
        .glUniform1iv = load_sym(lib, "glUniform1iv"),
        .glUniform2iv = load_sym(lib, "glUniform2iv"),
        .glUniform3iv = load_sym(lib, "glUniform3iv"),
        .glUniform4iv = load_sym(lib, "glUniform4iv"),
        .glUniformMatrix2fv = load_sym(lib, "glUniformMatrix2fv"),
        .glUniformMatrix3fv = load_sym(lib, "glUniformMatrix3fv"),
        .glUniformMatrix4fv = load_sym(lib, "glUniformMatrix4fv"),
        .glViewport = load_sym(lib, "glViewport"),
        .glEnable = load_sym(lib, "glEnable"),
        .glDisable = load_sym(lib, "glDisable"),
        .glBlendFunc = load_sym(lib, "glBlendFunc"),
        .glBlendColor = load_sym(lib, "glBlendColor"),
        .glBlendEquationSeparate = load_sym(lib, "glBlendEquationSeparate"),
        .glBlendFuncSeparate = load_sym(lib, "glBlendFuncSeparate"),
        .glDepthFunc = load_sym(lib, "glDepthFunc"),
        .glClearDepthf = load_sym(lib, "glClearDepthf"),
        .glDepthMask = load_sym(lib, "glDepthMask"),
        .glDepthRangef = load_sym(lib, "glDepthRangef"),
        .glScissor = load_sym(lib, "glScissor"),
        .glColorMask = load_sym(lib, "glColorMask"),
        .glCullFace = load_sym(lib, "glCullFace"),
        .glFrontFace = load_sym(lib, "glFrontFace"),
        .glStencilFunc = load_sym(lib, "glStencilFunc"),
        .glStencilOp = load_sym(lib, "glStencilOp"),
        .glStencilMask = load_sym(lib, "glStencilMask"),
        .glStencilFuncSeparate = load_sym(lib, "glStencilFuncSeparate"),
        .glStencilMaskSeparate = load_sym(lib, "glStencilMaskSeparate"),
        .glStencilOpSeparate = load_sym(lib, "glStencilOpSeparate"),
        .glClearStencil = load_sym(lib, "glClearStencil"),
        .glLineWidth = load_sym(lib, "glLineWidth"),
        .glPixelStorei = load_sym(lib, "glPixelStorei"),
        .glPolygonOffset = load_sym(lib, "glPolygonOffset"),
        .glSampleCoverage = load_sym(lib, "glSampleCoverage"),
        .glHint = load_sym(lib, "glHint"),
        .glFlush = load_sym(lib, "glFlush"),
        .glVertexAttrib4f = load_sym(lib, "glVertexAttrib4f"),
        .glGenTextures = load_sym(lib, "glGenTextures"),
        .glDeleteTextures = load_sym(lib, "glDeleteTextures"),
        .glActiveTexture = load_sym(lib, "glActiveTexture"),
        .glBindTexture = load_sym(lib, "glBindTexture"),
        .glTexParameteri = load_sym(lib, "glTexParameteri"),
        .glTexParameterf = load_sym(lib, "glTexParameterf"),
        .glGetTexParameterfv = load_sym(lib, "glGetTexParameterfv"),
        .glCopyTexImage2D = load_sym(lib, "glCopyTexImage2D"),
        .glCopyTexSubImage2D = load_sym(lib, "glCopyTexSubImage2D"),
        .glCompressedTexImage2D = load_sym(lib, "glCompressedTexImage2D"),
        .glCompressedTexSubImage2D = load_sym(lib, "glCompressedTexSubImage2D"),
        .glTexImage2D = load_sym(lib, "glTexImage2D"),
        .glTexSubImage2D = load_sym(lib, "glTexSubImage2D"),
        .glGenerateMipmap = load_sym(lib, "glGenerateMipmap"),
        .glUniform1i = load_sym(lib, "glUniform1i"),
        .glGetShaderPrecisionFormat = load_sym(lib, "glGetShaderPrecisionFormat"),
        .glReleaseShaderCompiler = load_sym(lib, "glReleaseShaderCompiler"),
        .glShaderBinary = load_sym(lib, "glShaderBinary"),
        .glGenFramebuffers = load_sym(lib, "glGenFramebuffers"),
        .glBindFramebuffer = load_sym(lib, "glBindFramebuffer"),
        .glFramebufferTexture2D = load_sym(lib, "glFramebufferTexture2D"),
        .glCheckFramebufferStatus = load_sym(lib, "glCheckFramebufferStatus"),
        .glDeleteFramebuffers = load_sym(lib, "glDeleteFramebuffers"),
        .glGenRenderbuffers = load_sym(lib, "glGenRenderbuffers"),
        .glBindRenderbuffer = load_sym(lib, "glBindRenderbuffer"),
        .glRenderbufferStorage = load_sym(lib, "glRenderbufferStorage"),
        .glFramebufferRenderbuffer = load_sym(lib, "glFramebufferRenderbuffer"),
        .glDeleteRenderbuffers = load_sym(lib, "glDeleteRenderbuffers"),
    };

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        fprintf(stderr, "EGL init failed: 0x%x\n", eglGetError());
        return 1;
    }

    EGLConfig config;
    EGLint nconfig = 0;
    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    if (!eglChooseConfig(display, attribs, &config, 1, &nconfig) || nconfig == 0) {
        fprintf(stderr, "eglChooseConfig failed: 0x%x\n", eglGetError());
        return 1;
    }

    EGLint pbuf[] = {EGL_WIDTH, 3000, EGL_HEIGHT, 3000, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuf);
    EGLint ctxattr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxattr);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "EGL context setup failed: 0x%x\n", eglGetError());
        return 1;
    }

    GLuint program = make_program(&gl);
    gl.glUseProgram(program);

    int server_fd = make_server_socket();
    init_shm();
    init_shm();
    printf("mali-egl-bridge ready\n");
    printf("uid=%d euid=%d renderer=%s lib=%s socket=%s\n",
           getuid(), geteuid(), glGetString(GL_RENDERER), libpath, BRIDGE_SOCKET);
    fflush(stdout);

    while (keep_running) {
        int client = accept(server_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        struct bridge_request req;
        struct bridge_response resp;
        memset(&resp, 0, sizeof(resp));
        resp.magic = BRIDGE_MAGIC;

        ssize_t got = read_full(client, &req, sizeof(req));
        if (got == (ssize_t)sizeof(req) && req.magic == BRIDGE_MAGIC) {
            handle_request(&gl, &req, &resp);
        } else {
            resp.status = 1;
        }
        (void)write_full(client, &resp, sizeof(resp));
        close(client);
    }

    close(server_fd);
    unlink(BRIDGE_SOCKET);
    return 0;
}
