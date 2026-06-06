#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef intptr_t GLsizeiptr;
typedef intptr_t GLintptr;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef char GLchar;

#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ACTIVE_UNIFORMS 0x8B86
#define GL_ACTIVE_ATTRIBUTES 0x8B89
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_VIEWPORT 0x0BA2
#define GL_SCISSOR_BOX 0x0C10
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_ALIASED_LINE_WIDTH_RANGE 0x846E
#define GL_ALIASED_POINT_SIZE_RANGE 0x846D
#define GL_DEPTH_RANGE 0x0B70
#define GL_BLEND_COLOR 0x8005
#define GL_COLOR_CLEAR_VALUE 0x0C22

#define BRIDGE_MAX_TEXT 4096
#define BRIDGE_MAX_FLOATS 1024
#define BRIDGE_MAX_BYTES 4096
#define BRIDGE_MAX_RESPONSE_TEXT 1024

#define GLX_RENDERER_VENDOR_ID_MESA 0x8183
#define GLX_RENDERER_DEVICE_ID_MESA 0x8184
#define GLX_RENDERER_VERSION_MESA 0x8185
#define GLX_RENDERER_ACCELERATED_MESA 0x8186
#define GLX_RENDERER_VIDEO_MEMORY_MESA 0x8187
#define GLX_RENDERER_UNIFIED_MEMORY_ARCHITECTURE_MESA 0x8188
#define GLX_RENDERER_PREFERRED_PROFILE_MESA 0x8189
#define GLX_RENDERER_OPENGL_CORE_PROFILE_VERSION_MESA 0x818A
#define GLX_RENDERER_OPENGL_COMPATIBILITY_PROFILE_VERSION_MESA 0x818B
#define GLX_RENDERER_OPENGL_ES_PROFILE_VERSION_MESA 0x818C
#define GLX_RENDERER_OPENGL_ES2_PROFILE_VERSION_MESA 0x818D

#define BRIDGE_SOCKET "/data/local/tmp/mali_egl_bridge.sock"
#define BRIDGE_MAGIC 0x31474c4dU

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

static uint32_t bound_array_buffer = 0;
static uint32_t bound_element_array_buffer = 0;

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

static int bridge_call(struct bridge_request *req, struct bridge_response *resp) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[mali-gl-bridge] socket failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", BRIDGE_SOCKET);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[mali-gl-bridge] connect %s failed: %s\n",
                BRIDGE_SOCKET, strerror(errno));
        close(fd);
        return -1;
    }

    req->magic = BRIDGE_MAGIC;
    if (write_full(fd, req, sizeof(*req)) != (ssize_t)sizeof(*req)) {
        fprintf(stderr, "[mali-gl-bridge] write failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    memset(resp, 0, sizeof(*resp));
    ssize_t got = read_full(fd, resp, sizeof(*resp));
    close(fd);

    if (got != (ssize_t)sizeof(*resp) || resp->magic != BRIDGE_MAGIC || resp->status != 0) {
        fprintf(stderr, "[mali-gl-bridge] bad bridge response\n");
        return -1;
    }
    return 0;
}

const GLubyte *glGetString(GLenum name) {
    static const GLubyte vendor[] = "Mali bridge";
    static const GLubyte renderer[] = "Mali-G77 MC9 via Android vendor EGL bridge";
    static const GLubyte version[] = "OpenGL bridge proof 0.1";
    static const GLubyte extensions[] = "";

    switch (name) {
    case GL_VENDOR:
        return vendor;
    case GL_RENDERER:
        return renderer;
    case GL_VERSION:
        return version;
    case GL_EXTENSIONS:
        return extensions;
    default:
        break;
    }

    const GLubyte *(*real_glGetString)(GLenum) = dlsym(RTLD_NEXT, "glGetString");
    return real_glGetString ? real_glGetString(name) : NULL;
}

void glClearColor(float red, float green, float blue, float alpha) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_CLEAR_COLOR;
    req.rgba[0] = red;
    req.rgba[1] = green;
    req.rgba[2] = blue;
    req.rgba[3] = alpha;
    (void)bridge_call(&req, &resp);
}

void glClear(GLbitfield mask) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_CLEAR;
    req.a[0] = mask;
    (void)bridge_call(&req, &resp);
}

void glFinish(void) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_FINISH;
    (void)bridge_call(&req, &resp);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void *pixels) {
    if (pixels && width > 0 && height > 0 && format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
        uint8_t *out = pixels;
        const GLsizei max_tile_width = BRIDGE_MAX_BYTES / 4;
        for (GLsizei col = 0; col < width;) {
            GLsizei tile_width = width - col;
            if (tile_width > max_tile_width) tile_width = max_tile_width;
            GLsizei rows_per_call = BRIDGE_MAX_BYTES / ((size_t)tile_width * 4);

            for (GLsizei row = 0; row < height;) {
                GLsizei rows = height - row;
                if (rows > rows_per_call) rows = rows_per_call;

                struct bridge_request req;
                struct bridge_response resp;
                memset(&req, 0, sizeof(req));
                req.cmd = BRIDGE_CMD_READ_PIXELS;
                req.a[0] = (uint32_t)(x + col);
                req.a[1] = (uint32_t)(y + row);
                req.a[2] = (uint32_t)tile_width;
                req.a[3] = (uint32_t)rows;
                size_t bytes = (size_t)tile_width * rows * 4;
                if (bridge_call(&req, &resp) != 0 || resp.size != bytes) return;

                for (GLsizei tile_row = 0; tile_row < rows; tile_row++) {
                    size_t dst_offset =
                        ((size_t)(row + tile_row) * width + col) * 4;
                    size_t src_offset = (size_t)tile_row * tile_width * 4;
                    memcpy(out + dst_offset, resp.bytes + src_offset,
                           (size_t)tile_width * 4);
                }
                row += rows;
            }
            col += tile_width;
        }
        return;
    }

    void (*real_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) =
        dlsym(RTLD_NEXT, "glReadPixels");
    if (real_glReadPixels) {
        real_glReadPixels(x, y, width, height, format, type, pixels);
    }
}

GLuint glCreateShader(GLenum type) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_CREATE_SHADER;
    req.a[0] = type;
    return bridge_call(&req, &resp) == 0 ? resp.value : 0;
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) {
    struct bridge_request req;
    struct bridge_response resp;
    size_t used = 0;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_SHADER_SOURCE;
    req.a[0] = shader;

    for (GLsizei i = 0; i < count && string && string[i] && used + 1 < BRIDGE_MAX_TEXT; i++) {
        size_t n = length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
        if (n > BRIDGE_MAX_TEXT - used - 1) n = BRIDGE_MAX_TEXT - used - 1;
        memcpy(req.text + used, string[i], n);
        used += n;
    }
    req.text[used] = '\0';
    (void)bridge_call(&req, &resp);
}

void glCompileShader(GLuint shader) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_COMPILE_SHADER;
    req.a[0] = shader;
    (void)bridge_call(&req, &resp);
}

void glDeleteShader(GLuint shader) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DELETE_SHADER;
    req.a[0] = shader;
    (void)bridge_call(&req, &resp);
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_SHADER_IV;
    req.a[0] = shader;
    req.a[1] = pname;
    if (params) *params = bridge_call(&req, &resp) == 0 ? (GLint)resp.value : 0;
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_SHADER_INFO_LOG;
    req.a[0] = shader;
    if (bridge_call(&req, &resp) != 0) {
        if (length) *length = 0;
        if (infoLog && bufSize > 0) infoLog[0] = '\0';
        return;
    }
    GLsizei n = (GLsizei)resp.size;
    if (n >= bufSize) n = bufSize > 0 ? bufSize - 1 : 0;
    if (infoLog && bufSize > 0) {
        memcpy(infoLog, resp.text, (size_t)n);
        infoLog[n] = '\0';
    }
    if (length) *length = n;
}

void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_SHADER_SOURCE;
    req.a[0] = shader;
    if (bridge_call(&req, &resp) != 0) {
        if (length) *length = 0;
        if (source && bufSize > 0) source[0] = '\0';
        return;
    }
    GLsizei n = (GLsizei)resp.size;
    if (n >= bufSize) n = bufSize > 0 ? bufSize - 1 : 0;
    if (source && bufSize > 0) {
        memcpy(source, resp.text, (size_t)n);
        source[n] = '\0';
    }
    if (length) *length = n;
}

GLuint glCreateProgram(void) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_CREATE_PROGRAM;
    return bridge_call(&req, &resp) == 0 ? resp.value : 0;
}

void glAttachShader(GLuint program, GLuint shader) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_ATTACH_SHADER;
    req.a[0] = program;
    req.a[1] = shader;
    (void)bridge_call(&req, &resp);
}

void glDetachShader(GLuint program, GLuint shader) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DETACH_SHADER;
    req.a[0] = program;
    req.a[1] = shader;
    (void)bridge_call(&req, &resp);
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BIND_ATTRIB_LOCATION;
    req.a[0] = program;
    req.a[1] = index;
    if (name) snprintf(req.text, sizeof(req.text), "%s", name);
    (void)bridge_call(&req, &resp);
}

void glLinkProgram(GLuint program) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_LINK_PROGRAM;
    req.a[0] = program;
    (void)bridge_call(&req, &resp);
}

void glValidateProgram(GLuint program) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_VALIDATE_PROGRAM;
    req.a[0] = program;
    (void)bridge_call(&req, &resp);
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_PROGRAM_IV;
    req.a[0] = program;
    req.a[1] = pname;
    if (params) *params = bridge_call(&req, &resp) == 0 ? (GLint)resp.value : 0;
}

void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_PROGRAM_INFO_LOG;
    req.a[0] = program;
    if (bridge_call(&req, &resp) != 0) {
        if (length) *length = 0;
        if (infoLog && bufSize > 0) infoLog[0] = '\0';
        return;
    }
    GLsizei n = (GLsizei)resp.size;
    if (n >= bufSize) n = bufSize > 0 ? bufSize - 1 : 0;
    if (infoLog && bufSize > 0) {
        memcpy(infoLog, resp.text, (size_t)n);
        infoLog[n] = '\0';
    }
    if (length) *length = n;
}

void glGetAttachedShaders(GLuint program, GLsizei maxCount,
                          GLsizei *count, GLuint *shaders) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_ATTACHED_SHADERS;
    req.a[0] = program;
    if (bridge_call(&req, &resp) != 0) {
        if (count) *count = 0;
        return;
    }
    GLsizei n = (GLsizei)resp.value;
    if (n > maxCount) n = maxCount;
    if (shaders && n > 0) memcpy(shaders, resp.bytes, (size_t)n * sizeof(GLuint));
    if (count) *count = n;
}

static void bridge_get_active(uint32_t command, GLuint program, GLuint index,
                              GLsizei bufSize, GLsizei *length, GLint *size,
                              GLenum *type, GLchar *name) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = command;
    req.a[0] = program;
    req.a[1] = index;
    if (bridge_call(&req, &resp) != 0) {
        if (length) *length = 0;
        if (size) *size = 0;
        if (type) *type = 0;
        if (name && bufSize > 0) name[0] = '\0';
        return;
    }
    GLsizei n = (GLsizei)resp.size;
    if (n >= bufSize) n = bufSize > 0 ? bufSize - 1 : 0;
    if (name && bufSize > 0) {
        memcpy(name, resp.text, (size_t)n);
        name[n] = '\0';
    }
    if (length) *length = n;
    if (size) *size = (GLint)resp.value;
    if (type) memcpy(type, resp.bytes, sizeof(*type));
}

void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                        GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
    bridge_get_active(BRIDGE_CMD_GET_ACTIVE_UNIFORM, program, index, bufSize,
                      length, size, type, name);
}

void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                       GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
    bridge_get_active(BRIDGE_CMD_GET_ACTIVE_ATTRIB, program, index, bufSize,
                      length, size, type, name);
}

static void bridge_get_uniform_value(GLuint program, GLint location,
                                     uint32_t type, void *params) {
    if (!params) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_UNIFORM_VALUE;
    req.a[0] = program;
    req.a[1] = (uint32_t)location;
    req.a[2] = type;
    if (bridge_call(&req, &resp) == 0 && resp.size <= BRIDGE_MAX_BYTES)
        memcpy(params, resp.bytes, resp.size);
}

void glGetUniformfv(GLuint program, GLint location, float *params) {
    bridge_get_uniform_value(program, location, 1, params);
}

void glGetUniformiv(GLuint program, GLint location, GLint *params) {
    bridge_get_uniform_value(program, location, 2, params);
}

void glUseProgram(GLuint program) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_USE_PROGRAM;
    req.a[0] = program;
    (void)bridge_call(&req, &resp);
}

GLint glGetAttribLocation(GLuint program, const GLchar *name) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_ATTRIB_LOCATION;
    req.a[0] = program;
    if (name) snprintf(req.text, sizeof(req.text), "%s", name);
    return bridge_call(&req, &resp) == 0 ? (GLint)resp.value : -1;
}

void glDeleteProgram(GLuint program) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DELETE_PROGRAM;
    req.a[0] = program;
    (void)bridge_call(&req, &resp);
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_VERTEX_ATTRIB_POINTER;
    req.a[0] = index;
    req.a[1] = (uint32_t)size;
    req.a[2] = type;
    req.a[3] = normalized;
    req.a[4] = 3; /* current minimal path: triangle vertices */
    req.a[5] = (uint32_t)stride;
    req.a[6] = bound_array_buffer ? 1 : 0;
    req.a[7] = (uint32_t)(uintptr_t)pointer;

    size_t total = (size_t)size * req.a[4];
    if (!bound_array_buffer && type == GL_FLOAT && pointer && total <= BRIDGE_MAX_FLOATS) {
        memcpy(req.floats, pointer, total * sizeof(float));
    }
    (void)bridge_call(&req, &resp);
}

void glEnableVertexAttribArray(GLuint index) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_ENABLE_VERTEX_ATTRIB_ARRAY;
    req.a[0] = index;
    (void)bridge_call(&req, &resp);
}

void glDisableVertexAttribArray(GLuint index) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DISABLE_VERTEX_ATTRIB_ARRAY;
    req.a[0] = index;
    (void)bridge_call(&req, &resp);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DRAW_ARRAYS;
    req.a[0] = mode;
    req.a[1] = (uint32_t)first;
    req.a[2] = (uint32_t)count;
    (void)bridge_call(&req, &resp);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DRAW_ELEMENTS;
    req.a[0] = mode;
    req.a[1] = (uint32_t)count;
    req.a[2] = type;
    req.a[3] = bound_element_array_buffer ? (uint32_t)(uintptr_t)indices : 0;
    (void)bridge_call(&req, &resp);
}

GLenum glGetError(void) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_ERROR;
    return bridge_call(&req, &resp) == 0 ? resp.value : 0;
}

static uint32_t state_component_count(GLenum pname) {
    switch (pname) {
    case GL_VIEWPORT:
    case GL_SCISSOR_BOX:
    case GL_COLOR_WRITEMASK:
    case GL_BLEND_COLOR:
    case GL_COLOR_CLEAR_VALUE:
        return 4;
    case GL_ALIASED_LINE_WIDTH_RANGE:
    case GL_ALIASED_POINT_SIZE_RANGE:
    case GL_DEPTH_RANGE:
        return 2;
    default:
        return 1;
    }
}

static int bridge_get_state(GLenum pname, uint32_t type, uint32_t count,
                            void *data, size_t element_size) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_STATE;
    req.a[0] = pname;
    req.a[1] = type;
    req.a[2] = count;
    size_t size = (size_t)count * element_size;
    if (bridge_call(&req, &resp) != 0 || resp.size != size) return -1;
    memcpy(data, resp.bytes, size);
    return 0;
}

void glGetIntegerv(GLenum pname, GLint *data) {
    if (!data) return;
    uint32_t count = state_component_count(pname);
    if (bridge_get_state(pname, 1, count, data, sizeof(*data)) == 0) return;

    switch (pname) {
    case GL_MAX_TEXTURE_SIZE:
        *data = 4096;
        break;
    case GL_MAX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        *data = 8;
        break;
    case GL_MAX_VERTEX_ATTRIBS:
        *data = 16;
        break;
    default:
        *data = 0;
        break;
    }
}

void glGetFloatv(GLenum pname, float *data) {
    if (!data) return;
    uint32_t count = state_component_count(pname);
    if (bridge_get_state(pname, 2, count, data, sizeof(*data)) != 0) {
        memset(data, 0, count * sizeof(*data));
    }
}

void glGetBooleanv(GLenum pname, GLboolean *data) {
    if (!data) return;
    uint32_t count = state_component_count(pname);
    if (bridge_get_state(pname, 3, count, data, sizeof(*data)) != 0) {
        memset(data, 0, count * sizeof(*data));
    }
}

static GLboolean bridge_is_object(uint32_t kind, GLuint object) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_IS_OBJECT;
    req.a[0] = kind;
    req.a[1] = object;
    return bridge_call(&req, &resp) == 0 ? (GLboolean)resp.value : 0;
}

GLboolean glIsBuffer(GLuint buffer) {
    return bridge_is_object(1, buffer);
}

GLboolean glIsTexture(GLuint texture) {
    return bridge_is_object(2, texture);
}

GLboolean glIsFramebuffer(GLuint framebuffer) {
    return bridge_is_object(3, framebuffer);
}

GLboolean glIsRenderbuffer(GLuint renderbuffer) {
    return bridge_is_object(4, renderbuffer);
}

GLboolean glIsProgram(GLuint program) {
    return bridge_is_object(5, program);
}

GLboolean glIsShader(GLuint shader) {
    return bridge_is_object(6, shader);
}

GLboolean glIsEnabled(GLenum cap) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_IS_ENABLED;
    req.a[0] = cap;
    return bridge_call(&req, &resp) == 0 ? (GLboolean)resp.value : 0;
}

static void bridge_get_parameter(uint32_t command, GLenum target,
                                 GLenum pname, GLint *params) {
    if (!params) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = command;
    req.a[0] = target;
    req.a[1] = pname;
    *params = bridge_call(&req, &resp) == 0 ? (GLint)resp.value : 0;
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    bridge_get_parameter(BRIDGE_CMD_GET_BUFFER_PARAMETER_IV, target, pname, params);
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    bridge_get_parameter(BRIDGE_CMD_GET_TEX_PARAMETER_IV, target, pname, params);
}

void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    bridge_get_parameter(BRIDGE_CMD_GET_RENDERBUFFER_PARAMETER_IV, target, pname, params);
}

void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
    if (!params) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_VERTEX_ATTRIB_IV;
    req.a[0] = index;
    req.a[1] = pname;
    *params = bridge_call(&req, &resp) == 0 ? (GLint)resp.value : 0;
}

void glGetVertexAttribfv(GLuint index, GLenum pname, float *params) {
    if (!params) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_VERTEX_ATTRIB_FV;
    req.a[0] = index;
    req.a[1] = pname;
    if (bridge_call(&req, &resp) == 0 && resp.size == 4 * sizeof(float))
        memcpy(params, resp.bytes, resp.size);
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) {
    if (!pointer) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_VERTEX_ATTRIB_POINTER;
    req.a[0] = index;
    req.a[1] = pname;
    *pointer = bridge_call(&req, &resp) == 0
        ? (void *)(uintptr_t)resp.value : NULL;
}

void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                           GLenum pname, GLint *params) {
    if (!params) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_FRAMEBUFFER_ATTACHMENT_PARAMETER_IV;
    req.a[0] = target;
    req.a[1] = attachment;
    req.a[2] = pname;
    *params = bridge_call(&req, &resp) == 0 ? (GLint)resp.value : 0;
}

void glGenBuffers(GLsizei n, GLuint *buffers) {
    if (!buffers || n <= 0) return;
    for (GLsizei i = 0; i < n; i++) {
        struct bridge_request req;
        struct bridge_response resp;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_GEN_BUFFER;
        buffers[i] = bridge_call(&req, &resp) == 0 ? resp.value : 0;
    }
}

void glBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) bound_array_buffer = buffer;
    if (target == GL_ELEMENT_ARRAY_BUFFER) bound_element_array_buffer = buffer;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BIND_BUFFER;
    req.a[0] = target;
    req.a[1] = buffer;
    (void)bridge_call(&req, &resp);
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    if (!buffers || n <= 0) return;
    for (GLsizei i = 0; i < n; i++) {
        if (bound_array_buffer == buffers[i]) bound_array_buffer = 0;
        if (bound_element_array_buffer == buffers[i]) bound_element_array_buffer = 0;
        struct bridge_request req;
        struct bridge_response resp;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_DELETE_BUFFER;
        req.a[0] = buffers[i];
        (void)bridge_call(&req, &resp);
    }
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BUFFER_DATA;
    req.a[0] = target;
    req.a[1] = size > 0 ? (uint32_t)size : 0;
    req.a[2] = usage;
    if (data && size > 0 && (size_t)size <= BRIDGE_MAX_BYTES) {
        memcpy(req.bytes, data, (size_t)size);
        req.a[3] = (uint32_t)size;
    }
    if (bridge_call(&req, &resp) != 0 || !data || size <= 0 ||
        (size_t)size <= BRIDGE_MAX_BYTES) return;

    const uint8_t *src = data;
    for (GLintptr offset = 0; offset < size;) {
        GLsizeiptr chunk = size - offset;
        if ((size_t)chunk > BRIDGE_MAX_BYTES) chunk = BRIDGE_MAX_BYTES;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_BUFFER_SUB_DATA;
        req.a[0] = target;
        req.a[1] = (uint32_t)offset;
        req.a[2] = (uint32_t)chunk;
        memcpy(req.bytes, src + offset, (size_t)chunk);
        if (bridge_call(&req, &resp) != 0) return;
        offset += chunk;
    }
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    if (!data || offset < 0 || size <= 0) return;

    const uint8_t *src = data;
    for (GLsizeiptr sent = 0; sent < size;) {
        GLsizeiptr chunk = size - sent;
        if ((size_t)chunk > BRIDGE_MAX_BYTES) chunk = BRIDGE_MAX_BYTES;
        struct bridge_request req;
        struct bridge_response resp;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_BUFFER_SUB_DATA;
        req.a[0] = target;
        req.a[1] = (uint32_t)(offset + sent);
        req.a[2] = (uint32_t)chunk;
        memcpy(req.bytes, src + sent, (size_t)chunk);
        if (bridge_call(&req, &resp) != 0) return;
        sent += chunk;
    }
}

GLint glGetUniformLocation(GLuint program, const GLchar *name) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_UNIFORM_LOCATION;
    req.a[0] = program;
    if (name) snprintf(req.text, sizeof(req.text), "%s", name);
    return bridge_call(&req, &resp) == 0 ? (GLint)resp.value : -1;
}

void glUniform4f(GLint location, float v0, float v1, float v2, float v3) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_UNIFORM4F;
    req.a[0] = (uint32_t)location;
    req.rgba[0] = v0;
    req.rgba[1] = v1;
    req.rgba[2] = v2;
    req.rgba[3] = v3;
    (void)bridge_call(&req, &resp);
}

void glUniform1f(GLint location, float v0) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_UNIFORM1F;
    req.a[0] = (uint32_t)location;
    req.rgba[0] = v0;
    (void)bridge_call(&req, &resp);
}

void glUniform2f(GLint location, float v0, float v1) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_UNIFORM2F;
    req.a[0] = (uint32_t)location;
    req.rgba[0] = v0;
    req.rgba[1] = v1;
    (void)bridge_call(&req, &resp);
}

void glUniform3f(GLint location, float v0, float v1, float v2) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_UNIFORM3F;
    req.a[0] = (uint32_t)location;
    req.rgba[0] = v0;
    req.rgba[1] = v1;
    req.rgba[2] = v2;
    (void)bridge_call(&req, &resp);
}

static void bridge_uniform_fv(GLint location, GLsizei count,
                              uint32_t components, const float *value) {
    if (!value || count <= 0) return;
    size_t total = (size_t)count * components;
    if (total > BRIDGE_MAX_FLOATS) total = BRIDGE_MAX_FLOATS;
    count = (GLsizei)(total / components);

    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_UNIFORM_FV;
    req.a[0] = (uint32_t)location;
    req.a[1] = components;
    req.a[2] = (uint32_t)count;
    memcpy(req.floats, value, (size_t)count * components * sizeof(float));
    (void)bridge_call(&req, &resp);
}

void glUniform1fv(GLint location, GLsizei count, const float *value) {
    bridge_uniform_fv(location, count, 1, value);
}

void glUniform2fv(GLint location, GLsizei count, const float *value) {
    bridge_uniform_fv(location, count, 2, value);
}

void glUniform3fv(GLint location, GLsizei count, const float *value) {
    bridge_uniform_fv(location, count, 3, value);
}

void glUniform4fv(GLint location, GLsizei count, const float *value) {
    bridge_uniform_fv(location, count, 4, value);
}

static void bridge_uniform_iv(GLint location, GLsizei count,
                              uint32_t components, const GLint *value) {
    if (!value || count <= 0) return;
    size_t total = (size_t)count * components;
    size_t max_values = BRIDGE_MAX_BYTES / sizeof(GLint);
    if (total > max_values) total = max_values;
    count = (GLsizei)(total / components);

    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_UNIFORM_IV;
    req.a[0] = (uint32_t)location;
    req.a[1] = components;
    req.a[2] = (uint32_t)count;
    memcpy(req.bytes, value, (size_t)count * components * sizeof(GLint));
    (void)bridge_call(&req, &resp);
}

void glUniform1iv(GLint location, GLsizei count, const GLint *value) {
    bridge_uniform_iv(location, count, 1, value);
}

void glUniform2iv(GLint location, GLsizei count, const GLint *value) {
    bridge_uniform_iv(location, count, 2, value);
}

void glUniform3iv(GLint location, GLsizei count, const GLint *value) {
    bridge_uniform_iv(location, count, 3, value);
}

void glUniform4iv(GLint location, GLsizei count, const GLint *value) {
    bridge_uniform_iv(location, count, 4, value);
}

void glUniform2i(GLint location, GLint v0, GLint v1) {
    const GLint value[] = {v0, v1};
    bridge_uniform_iv(location, 1, 2, value);
}

void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    const GLint value[] = {v0, v1, v2};
    bridge_uniform_iv(location, 1, 3, value);
}

void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    const GLint value[] = {v0, v1, v2, v3};
    bridge_uniform_iv(location, 1, 4, value);
}

static void bridge_uniform_matrix(GLint location, GLsizei count,
                                  GLboolean transpose, uint32_t dimension,
                                  uint32_t command, const float *value) {
    if (!value || count <= 0) return;
    size_t elements = (size_t)dimension * dimension;
    size_t total = (size_t)count * elements;
    if (total > BRIDGE_MAX_FLOATS) total = BRIDGE_MAX_FLOATS;
    count = (GLsizei)(total / elements);

    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = command;
    req.a[0] = (uint32_t)location;
    req.a[1] = (uint32_t)count;
    req.a[2] = transpose;
    memcpy(req.floats, value, (size_t)count * elements * sizeof(float));
    (void)bridge_call(&req, &resp);
}

void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                        const float *value) {
    bridge_uniform_matrix(location, count, transpose, 2,
                          BRIDGE_CMD_UNIFORM_MATRIX2FV, value);
}

void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                        const float *value) {
    bridge_uniform_matrix(location, count, transpose, 3,
                          BRIDGE_CMD_UNIFORM_MATRIX3FV, value);
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const float *value) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_UNIFORM_MATRIX4FV;
    req.a[0] = (uint32_t)location;
    req.a[1] = count > 0 ? (uint32_t)count : 0;
    req.a[2] = transpose;
    size_t n = (size_t)req.a[1] * 16;
    if (value && n > 0) {
        if (n > BRIDGE_MAX_FLOATS) n = BRIDGE_MAX_FLOATS;
        memcpy(req.floats, value, n * sizeof(float));
        req.a[1] = (uint32_t)(n / 16);
    }
    (void)bridge_call(&req, &resp);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_VIEWPORT;
    req.a[0] = (uint32_t)x;
    req.a[1] = (uint32_t)y;
    req.a[2] = (uint32_t)width;
    req.a[3] = (uint32_t)height;
    (void)bridge_call(&req, &resp);
}

void glEnable(GLenum cap) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_ENABLE;
    req.a[0] = cap;
    (void)bridge_call(&req, &resp);
}

void glDisable(GLenum cap) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DISABLE;
    req.a[0] = cap;
    (void)bridge_call(&req, &resp);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BLEND_FUNC;
    req.a[0] = sfactor;
    req.a[1] = dfactor;
    (void)bridge_call(&req, &resp);
}

void glBlendColor(float red, float green, float blue, float alpha) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BLEND_COLOR;
    req.rgba[0] = red;
    req.rgba[1] = green;
    req.rgba[2] = blue;
    req.rgba[3] = alpha;
    (void)bridge_call(&req, &resp);
}

void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BLEND_EQUATION_SEPARATE;
    req.a[0] = modeRGB;
    req.a[1] = modeAlpha;
    (void)bridge_call(&req, &resp);
}

void glBlendEquation(GLenum mode) {
    glBlendEquationSeparate(mode, mode);
}

void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB,
                         GLenum srcAlpha, GLenum dstAlpha) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BLEND_FUNC_SEPARATE;
    req.a[0] = srcRGB;
    req.a[1] = dstRGB;
    req.a[2] = srcAlpha;
    req.a[3] = dstAlpha;
    (void)bridge_call(&req, &resp);
}

void glDepthFunc(GLenum func) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DEPTH_FUNC;
    req.a[0] = func;
    (void)bridge_call(&req, &resp);
}

void glClearDepthf(float d) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_CLEAR_DEPTHF;
    req.rgba[0] = d;
    (void)bridge_call(&req, &resp);
}

void glDepthMask(GLboolean flag) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DEPTH_MASK;
    req.a[0] = flag;
    (void)bridge_call(&req, &resp);
}

void glDepthRangef(float near_value, float far_value) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_DEPTH_RANGEF;
    req.rgba[0] = near_value;
    req.rgba[1] = far_value;
    (void)bridge_call(&req, &resp);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_SCISSOR;
    req.a[0] = (uint32_t)x;
    req.a[1] = (uint32_t)y;
    req.a[2] = (uint32_t)width;
    req.a[3] = (uint32_t)height;
    (void)bridge_call(&req, &resp);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_COLOR_MASK;
    req.a[0] = red;
    req.a[1] = green;
    req.a[2] = blue;
    req.a[3] = alpha;
    (void)bridge_call(&req, &resp);
}

void glCullFace(GLenum mode) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_CULL_FACE;
    req.a[0] = mode;
    (void)bridge_call(&req, &resp);
}

void glFrontFace(GLenum mode) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_FRONT_FACE;
    req.a[0] = mode;
    (void)bridge_call(&req, &resp);
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_STENCIL_FUNC;
    req.a[0] = func;
    req.a[1] = (uint32_t)ref;
    req.a[2] = mask;
    (void)bridge_call(&req, &resp);
}

void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_STENCIL_OP;
    req.a[0] = sfail;
    req.a[1] = dpfail;
    req.a[2] = dppass;
    (void)bridge_call(&req, &resp);
}

void glStencilMask(GLuint mask) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_STENCIL_MASK;
    req.a[0] = mask;
    (void)bridge_call(&req, &resp);
}

void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_STENCIL_FUNC_SEPARATE;
    req.a[0] = face;
    req.a[1] = func;
    req.a[2] = (uint32_t)ref;
    req.a[3] = mask;
    (void)bridge_call(&req, &resp);
}

void glStencilMaskSeparate(GLenum face, GLuint mask) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_STENCIL_MASK_SEPARATE;
    req.a[0] = face;
    req.a[1] = mask;
    (void)bridge_call(&req, &resp);
}

void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_STENCIL_OP_SEPARATE;
    req.a[0] = face;
    req.a[1] = sfail;
    req.a[2] = dpfail;
    req.a[3] = dppass;
    (void)bridge_call(&req, &resp);
}

void glClearStencil(GLint s) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_CLEAR_STENCIL;
    req.a[0] = (uint32_t)s;
    (void)bridge_call(&req, &resp);
}

void glLineWidth(float width) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_LINE_WIDTH;
    req.rgba[0] = width;
    (void)bridge_call(&req, &resp);
}

void glPixelStorei(GLenum pname, GLint param) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_PIXEL_STOREI;
    req.a[0] = pname;
    req.a[1] = (uint32_t)param;
    (void)bridge_call(&req, &resp);
}

void glPolygonOffset(float factor, float units) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_POLYGON_OFFSET;
    req.rgba[0] = factor;
    req.rgba[1] = units;
    (void)bridge_call(&req, &resp);
}

void glSampleCoverage(float value, GLboolean invert) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_SAMPLE_COVERAGE;
    req.rgba[0] = value;
    req.a[0] = invert;
    (void)bridge_call(&req, &resp);
}

void glHint(GLenum target, GLenum mode) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_HINT;
    req.a[0] = target;
    req.a[1] = mode;
    (void)bridge_call(&req, &resp);
}

void glFlush(void) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_FLUSH;
    (void)bridge_call(&req, &resp);
}

static void bridge_vertex_attrib(GLuint index, float x, float y, float z, float w) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_VERTEX_ATTRIB_VALUE;
    req.a[0] = index;
    req.rgba[0] = x;
    req.rgba[1] = y;
    req.rgba[2] = z;
    req.rgba[3] = w;
    (void)bridge_call(&req, &resp);
}

void glVertexAttrib1f(GLuint index, float x) {
    bridge_vertex_attrib(index, x, 0.0f, 0.0f, 1.0f);
}

void glVertexAttrib2f(GLuint index, float x, float y) {
    bridge_vertex_attrib(index, x, y, 0.0f, 1.0f);
}

void glVertexAttrib3f(GLuint index, float x, float y, float z) {
    bridge_vertex_attrib(index, x, y, z, 1.0f);
}

void glVertexAttrib4f(GLuint index, float x, float y, float z, float w) {
    bridge_vertex_attrib(index, x, y, z, w);
}

void glVertexAttrib1fv(GLuint index, const float *values) {
    if (values) glVertexAttrib1f(index, values[0]);
}

void glVertexAttrib2fv(GLuint index, const float *values) {
    if (values) glVertexAttrib2f(index, values[0], values[1]);
}

void glVertexAttrib3fv(GLuint index, const float *values) {
    if (values) glVertexAttrib3f(index, values[0], values[1], values[2]);
}

void glVertexAttrib4fv(GLuint index, const float *values) {
    if (values) glVertexAttrib4f(index, values[0], values[1], values[2], values[3]);
}

void glGenTextures(GLsizei n, GLuint *textures) {
    if (!textures || n <= 0) return;
    for (GLsizei i = 0; i < n; i++) {
        struct bridge_request req;
        struct bridge_response resp;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_GEN_TEXTURE;
        textures[i] = bridge_call(&req, &resp) == 0 ? resp.value : 0;
    }
}

void glActiveTexture(GLenum texture) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_ACTIVE_TEXTURE;
    req.a[0] = texture;
    (void)bridge_call(&req, &resp);
}

void glBindTexture(GLenum target, GLuint texture) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BIND_TEXTURE;
    req.a[0] = target;
    req.a[1] = texture;
    (void)bridge_call(&req, &resp);
}

void glDeleteTextures(GLsizei n, const GLuint *textures) {
    if (!textures || n <= 0) return;
    for (GLsizei i = 0; i < n; i++) {
        struct bridge_request req;
        struct bridge_response resp;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_DELETE_TEXTURE;
        req.a[0] = textures[i];
        (void)bridge_call(&req, &resp);
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_TEX_PARAMETERI;
    req.a[0] = target;
    req.a[1] = pname;
    req.a[2] = (uint32_t)param;
    (void)bridge_call(&req, &resp);
}

void glTexParameterf(GLenum target, GLenum pname, float param) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_TEX_PARAMETER_F;
    req.a[0] = target;
    req.a[1] = pname;
    req.rgba[0] = param;
    (void)bridge_call(&req, &resp);
}

void glTexParameterfv(GLenum target, GLenum pname, const float *params) {
    if (params) glTexParameterf(target, pname, params[0]);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
    if (params) glTexParameteri(target, pname, params[0]);
}

void glGetTexParameterfv(GLenum target, GLenum pname, float *params) {
    if (!params) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_TEX_PARAMETER_FV;
    req.a[0] = target;
    req.a[1] = pname;
    if (bridge_call(&req, &resp) == 0 && resp.size == sizeof(*params))
        memcpy(params, resp.bytes, sizeof(*params));
    else
        *params = 0.0f;
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void *pixels) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_TEX_IMAGE_2D;
    req.a[0] = target;
    req.a[1] = (uint32_t)level;
    req.a[2] = (uint32_t)internalformat;
    req.a[3] = (uint32_t)width;
    req.a[4] = (uint32_t)height;
    req.a[5] = (uint32_t)border;
    req.a[6] = format;
    req.a[8] = type;

    size_t bytes_per_pixel = (format == GL_RGBA && type == GL_UNSIGNED_BYTE) ? 4 : 0;
    size_t byte_count = bytes_per_pixel && width > 0 && height > 0
        ? (size_t)width * (size_t)height * bytes_per_pixel
        : 0;
    if (pixels && byte_count > 0) {
        if (byte_count > BRIDGE_MAX_BYTES) byte_count = BRIDGE_MAX_BYTES;
        memcpy(req.bytes, pixels, byte_count);
        req.a[7] = (uint32_t)byte_count;
    }
    (void)bridge_call(&req, &resp);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void *pixels) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_TEX_SUB_IMAGE_2D;
    req.a[0] = target;
    req.a[1] = (uint32_t)level;
    req.a[2] = (uint32_t)xoffset;
    req.a[3] = (uint32_t)yoffset;
    req.a[4] = (uint32_t)width;
    req.a[5] = (uint32_t)height;
    req.a[6] = format;
    req.a[7] = type;

    size_t bytes_per_pixel = (format == GL_RGBA && type == GL_UNSIGNED_BYTE) ? 4 : 0;
    size_t byte_count = bytes_per_pixel && width > 0 && height > 0
        ? (size_t)width * (size_t)height * bytes_per_pixel
        : 0;
    if (pixels && byte_count > 0) {
        if (byte_count > BRIDGE_MAX_BYTES) byte_count = BRIDGE_MAX_BYTES;
        memcpy(req.bytes, pixels, byte_count);
        req.a[8] = (uint32_t)byte_count;
    }
    (void)bridge_call(&req, &resp);
}

void glGenerateMipmap(GLenum target) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GENERATE_MIPMAP;
    req.a[0] = target;
    (void)bridge_call(&req, &resp);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLsizei height,
                      GLint border) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_COPY_TEX_IMAGE_2D;
    req.a[0] = target;
    req.a[1] = (uint32_t)level;
    req.a[2] = internalformat;
    req.a[3] = (uint32_t)x;
    req.a[4] = (uint32_t)y;
    req.a[5] = (uint32_t)width;
    req.a[6] = (uint32_t)height;
    req.a[7] = (uint32_t)border;
    (void)bridge_call(&req, &resp);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_COPY_TEX_SUB_IMAGE_2D;
    req.a[0] = target;
    req.a[1] = (uint32_t)level;
    req.a[2] = (uint32_t)xoffset;
    req.a[3] = (uint32_t)yoffset;
    req.a[4] = (uint32_t)x;
    req.a[5] = (uint32_t)y;
    req.a[6] = (uint32_t)width;
    req.a[7] = (uint32_t)height;
    (void)bridge_call(&req, &resp);
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, const void *data) {
    if (imageSize < 0 || imageSize > BRIDGE_MAX_BYTES) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_COMPRESSED_TEX_IMAGE_2D;
    req.a[0] = target;
    req.a[1] = (uint32_t)level;
    req.a[2] = internalformat;
    req.a[3] = (uint32_t)width;
    req.a[4] = (uint32_t)height;
    req.a[5] = (uint32_t)border;
    req.a[7] = (uint32_t)imageSize;
    if (data && imageSize > 0) memcpy(req.bytes, data, (size_t)imageSize);
    (void)bridge_call(&req, &resp);
}

void glCompressedTexSubImage2D(GLenum target, GLint level,
                               GLint xoffset, GLint yoffset,
                               GLsizei width, GLsizei height, GLenum format,
                               GLsizei imageSize, const void *data) {
    if (imageSize < 0 || imageSize > BRIDGE_MAX_BYTES) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_COMPRESSED_TEX_SUB_IMAGE_2D;
    req.a[0] = target;
    req.a[1] = (uint32_t)level;
    req.a[2] = (uint32_t)xoffset;
    req.a[3] = (uint32_t)yoffset;
    req.a[4] = (uint32_t)width;
    req.a[5] = (uint32_t)height;
    req.a[6] = format;
    req.a[8] = (uint32_t)imageSize;
    if (data && imageSize > 0) memcpy(req.bytes, data, (size_t)imageSize);
    (void)bridge_call(&req, &resp);
}

void glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
    if (!framebuffers || n <= 0) return;
    for (GLsizei i = 0; i < n; i++) {
        struct bridge_request req;
        struct bridge_response resp;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_GEN_FRAMEBUFFER;
        framebuffers[i] = bridge_call(&req, &resp) == 0 ? resp.value : 0;
    }
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BIND_FRAMEBUFFER;
    req.a[0] = target;
    req.a[1] = framebuffer;
    (void)bridge_call(&req, &resp);
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_FRAMEBUFFER_TEXTURE_2D;
    req.a[0] = target;
    req.a[1] = attachment;
    req.a[2] = textarget;
    req.a[3] = texture;
    req.a[4] = (uint32_t)level;
    (void)bridge_call(&req, &resp);
}

GLenum glCheckFramebufferStatus(GLenum target) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_CHECK_FRAMEBUFFER_STATUS;
    req.a[0] = target;
    return bridge_call(&req, &resp) == 0 ? resp.value : 0;
}

void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    if (!framebuffers || n <= 0) return;
    for (GLsizei i = 0; i < n; i++) {
        struct bridge_request req;
        struct bridge_response resp;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_DELETE_FRAMEBUFFER;
        req.a[0] = framebuffers[i];
        (void)bridge_call(&req, &resp);
    }
}

void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
    if (!renderbuffers || n <= 0) return;
    for (GLsizei i = 0; i < n; i++) {
        struct bridge_request req;
        struct bridge_response resp;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_GEN_RENDERBUFFER;
        renderbuffers[i] = bridge_call(&req, &resp) == 0 ? resp.value : 0;
    }
}

void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_BIND_RENDERBUFFER;
    req.a[0] = target;
    req.a[1] = renderbuffer;
    (void)bridge_call(&req, &resp);
}

void glRenderbufferStorage(GLenum target, GLenum internalformat,
                           GLsizei width, GLsizei height) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_RENDERBUFFER_STORAGE;
    req.a[0] = target;
    req.a[1] = internalformat;
    req.a[2] = (uint32_t)width;
    req.a[3] = (uint32_t)height;
    (void)bridge_call(&req, &resp);
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                               GLenum renderbuffertarget, GLuint renderbuffer) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_FRAMEBUFFER_RENDERBUFFER;
    req.a[0] = target;
    req.a[1] = attachment;
    req.a[2] = renderbuffertarget;
    req.a[3] = renderbuffer;
    (void)bridge_call(&req, &resp);
}

void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
    if (!renderbuffers || n <= 0) return;
    for (GLsizei i = 0; i < n; i++) {
        struct bridge_request req;
        struct bridge_response resp;
        memset(&req, 0, sizeof(req));
        req.cmd = BRIDGE_CMD_DELETE_RENDERBUFFER;
        req.a[0] = renderbuffers[i];
        (void)bridge_call(&req, &resp);
    }
}

void glUniform1i(GLint location, GLint v0) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_UNIFORM1I;
    req.a[0] = (uint32_t)location;
    req.a[1] = (uint32_t)v0;
    (void)bridge_call(&req, &resp);
}

void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                GLint *range, GLint *precision) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_GET_SHADER_PRECISION_FORMAT;
    req.a[0] = shadertype;
    req.a[1] = precisiontype;
    if (bridge_call(&req, &resp) == 0 &&
        resp.size == 3 * sizeof(GLint)) {
        if (range) memcpy(range, resp.bytes, 2 * sizeof(GLint));
        if (precision) memcpy(precision, resp.bytes + 2 * sizeof(GLint),
                              sizeof(GLint));
    }
}

void glReleaseShaderCompiler(void) {
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_RELEASE_SHADER_COMPILER;
    (void)bridge_call(&req, &resp);
}

void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryformat,
                    const void *binary, GLsizei length) {
    if (!shaders || count <= 0 || count > 8 || length < 0) return;
    size_t names_size = (size_t)count * sizeof(GLuint);
    if (names_size + (size_t)length > BRIDGE_MAX_BYTES) return;
    struct bridge_request req;
    struct bridge_response resp;
    memset(&req, 0, sizeof(req));
    req.cmd = BRIDGE_CMD_SHADER_BINARY;
    req.a[0] = (uint32_t)count;
    req.a[1] = binaryformat;
    req.a[2] = (uint32_t)length;
    memcpy(req.bytes, shaders, names_size);
    if (binary && length > 0)
        memcpy(req.bytes + names_size, binary, (size_t)length);
    (void)bridge_call(&req, &resp);
}

int glXQueryCurrentRendererIntegerMESA(int attribute, unsigned int *value) {
    if (!value) return 0;

    switch (attribute) {
    case GLX_RENDERER_VENDOR_ID_MESA:
        *value = 0x13b5;
        return 1;
    case GLX_RENDERER_DEVICE_ID_MESA:
        *value = 0x09000800;
        return 1;
    case GLX_RENDERER_VERSION_MESA:
        *value = 1;
        return 1;
    case GLX_RENDERER_ACCELERATED_MESA:
        *value = 1;
        return 1;
    case GLX_RENDERER_VIDEO_MEMORY_MESA:
        *value = 0;
        return 1;
    case GLX_RENDERER_UNIFIED_MEMORY_ARCHITECTURE_MESA:
        *value = 1;
        return 1;
    case GLX_RENDERER_PREFERRED_PROFILE_MESA:
        *value = 0;
        return 1;
    case GLX_RENDERER_OPENGL_CORE_PROFILE_VERSION_MESA:
    case GLX_RENDERER_OPENGL_COMPATIBILITY_PROFILE_VERSION_MESA:
        *value = 0;
        return 1;
    case GLX_RENDERER_OPENGL_ES_PROFILE_VERSION_MESA:
        *value = (3U << 16) | 2U;
        return 1;
    case GLX_RENDERER_OPENGL_ES2_PROFILE_VERSION_MESA:
        *value = (3U << 16) | 2U;
        return 1;
    default:
        break;
    }

    int (*real_fn)(int, unsigned int *) =
        dlsym(RTLD_NEXT, "glXQueryCurrentRendererIntegerMESA");
    return real_fn ? real_fn(attribute, value) : 0;
}

const char *glXQueryCurrentRendererStringMESA(int attribute) {
    switch (attribute) {
    case GLX_RENDERER_VENDOR_ID_MESA:
        return "ARM";
    case GLX_RENDERER_DEVICE_ID_MESA:
        return "Mali-G77 MC9 via Android vendor EGL bridge";
    default:
        break;
    }

    const char *(*real_fn)(int) =
        dlsym(RTLD_NEXT, "glXQueryCurrentRendererStringMESA");
    return real_fn ? real_fn(attribute) : "";
}

int glXQueryRendererIntegerMESA(void *dpy, int screen, int renderer,
                                int attribute, unsigned int *value) {
    (void)dpy;
    (void)screen;
    (void)renderer;
    return glXQueryCurrentRendererIntegerMESA(attribute, value);
}

const char *glXQueryRendererStringMESA(void *dpy, int screen, int renderer,
                                       int attribute) {
    (void)dpy;
    (void)screen;
    (void)renderer;
    return glXQueryCurrentRendererStringMESA(attribute);
}

void (*glXGetProcAddress(const unsigned char *procname))(void) {
    if (procname) {
        if (strcmp((const char *)procname, "glGetString") == 0) return (void (*)(void))glGetString;
        if (strcmp((const char *)procname, "glClearColor") == 0) return (void (*)(void))glClearColor;
        if (strcmp((const char *)procname, "glClear") == 0) return (void (*)(void))glClear;
        if (strcmp((const char *)procname, "glFinish") == 0) return (void (*)(void))glFinish;
        if (strcmp((const char *)procname, "glReadPixels") == 0) return (void (*)(void))glReadPixels;
        if (strcmp((const char *)procname, "glGetIntegerv") == 0) return (void (*)(void))glGetIntegerv;
        if (strcmp((const char *)procname, "glGetAttribLocation") == 0) return (void (*)(void))glGetAttribLocation;
        if (strcmp((const char *)procname, "glXQueryCurrentRendererIntegerMESA") == 0)
            return (void (*)(void))glXQueryCurrentRendererIntegerMESA;
        if (strcmp((const char *)procname, "glXQueryCurrentRendererStringMESA") == 0)
            return (void (*)(void))glXQueryCurrentRendererStringMESA;
        if (strcmp((const char *)procname, "glXQueryRendererIntegerMESA") == 0)
            return (void (*)(void))glXQueryRendererIntegerMESA;
        if (strcmp((const char *)procname, "glXQueryRendererStringMESA") == 0)
            return (void (*)(void))glXQueryRendererStringMESA;
    }

    void (*(*real_fn)(const unsigned char *))(void) = dlsym(RTLD_NEXT, "glXGetProcAddress");
    return real_fn ? real_fn(procname) : NULL;
}

void (*glXGetProcAddressARB(const unsigned char *procname))(void) {
    return glXGetProcAddress(procname);
}
