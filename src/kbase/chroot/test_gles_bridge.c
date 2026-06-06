#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*bridge_scissor_proc)(GLint, GLint, GLsizei, GLsizei);

static void check_shader(GLuint shader, const char *name) {
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        printf("%s compile failed: %s\n", name, log);
        exit(1);
    }
}

static void check_program(GLuint program) {
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        printf("program link failed: %s\n", log);
        exit(1);
    }
}

int main(void) {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        printf("eglInitialize failed: 0x%x\n", eglGetError());
        return 1;
    }

    EGLConfig cfg;
    EGLint ncfg = 0;
    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &ncfg) || ncfg < 1) {
        printf("eglChooseConfig failed: 0x%x\n", eglGetError());
        return 1;
    }
    EGLint config_id = 0;
    EGLint renderable_type = 0;
    if (!eglGetConfigAttrib(dpy, cfg, EGL_CONFIG_ID, &config_id) ||
        !eglGetConfigAttrib(dpy, cfg, EGL_RENDERABLE_TYPE, &renderable_type) ||
        config_id != 1 || !(renderable_type & EGL_OPENGL_ES2_BIT)) {
        printf("EGL config query failed\n");
        return 1;
    }

    EGLint pbuf_attrs[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbuf_attrs);
    if (surf == EGL_NO_SURFACE) {
        printf("eglCreatePbufferSurface failed: 0x%x\n", eglGetError());
        return 1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API) || eglQueryAPI() != EGL_OPENGL_ES_API) {
        printf("EGL client API binding failed\n");
        return 1;
    }
    EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        printf("eglMakeCurrent failed: 0x%x\n", eglGetError());
        return 1;
    }
    EGLint surface_width = 0;
    EGLint surface_height = 0;
    EGLint context_version = 0;
    if (!eglQuerySurface(dpy, surf, EGL_WIDTH, &surface_width) ||
        !eglQuerySurface(dpy, surf, EGL_HEIGHT, &surface_height) ||
        !eglQueryContext(dpy, ctx, EGL_CONTEXT_CLIENT_VERSION, &context_version) ||
        surface_width != 64 || surface_height != 64 || context_version != 2 ||
        eglGetCurrentDisplay() != dpy || eglGetCurrentContext() != ctx ||
        eglGetCurrentSurface(EGL_DRAW) != surf ||
        eglGetCurrentSurface(EGL_READ) != surf) {
        printf("EGL current/query state failed\n");
        return 1;
    }
    if (!eglSwapInterval(dpy, 0) || !eglWaitClient() || !eglWaitGL() ||
        !eglWaitNative(EGL_CORE_NATIVE_ENGINE)) {
        printf("EGL synchronization calls failed\n");
        return 1;
    }

    printf("egl: %d.%d vendor=%s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));
    printf("gl vendor: %s\n", glGetString(GL_VENDOR));
    printf("gl renderer: %s\n", glGetString(GL_RENDERER));
    GLint precision_range[2] = {0};
    GLint precision_bits = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT,
                               precision_range, &precision_bits);
    if (precision_range[1] < precision_range[0] || precision_bits < 0) {
        printf("shader precision query failed\n");
        return 1;
    }
    bridge_scissor_proc bridge_scissor =
        (bridge_scissor_proc)eglGetProcAddress("glScissor");
    if (!bridge_scissor) {
        printf("eglGetProcAddress(glScissor) failed\n");
        return 1;
    }

    const char *vs_src =
        "attribute vec2 vPosition;\n"
        "attribute vec2 vTexCoord;\n"
        "uniform mat4 uMvp;\n"
        "uniform mat2 uPositionScale;\n"
        "uniform mat3 uTexMatrix;\n"
        "varying vec2 texCoord;\n"
        "void main() {\n"
        "  gl_Position = uMvp * vec4(uPositionScale * vPosition, 0.0, 1.0);\n"
        "  texCoord = (uTexMatrix * vec3(vTexCoord, 1.0)).xy;\n"
        "}\n";
    const char *fs_src =
        "precision mediump float;\n"
        "varying vec2 texCoord;\n"
        "uniform sampler2D uTexture;\n"
        "uniform vec4 uTint[2];\n"
        "uniform ivec2 uFlags;\n"
        "void main() {\n"
        "  vec4 color = texture2D(uTexture, texCoord);\n"
        "  gl_FragColor = uFlags.x == 1 ? color * uTint[1] : uTint[0];\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, NULL);
    glCompileShader(vs);
    check_shader(vs, "vertex shader");

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(fs);
    check_shader(fs, "fragment shader");

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "vPosition");
    glBindAttribLocation(program, 1, "vTexCoord");
    glLinkProgram(program);
    check_program(program);
    glUseProgram(program);
    glValidateProgram(program);
    GLint validate_status = 0;
    glGetProgramiv(program, GL_VALIDATE_STATUS, &validate_status);
    if (!validate_status) {
        printf("program validation failed\n");
        return 1;
    }
    if (!glIsShader(vs) || !glIsShader(fs) || !glIsProgram(program)) {
        printf("shader/program validity query failed\n");
        return 1;
    }
    char shader_source[512] = {0};
    glGetShaderSource(vs, sizeof(shader_source), NULL, shader_source);
    if (!strstr(shader_source, "uPositionScale")) {
        printf("shader source query failed\n");
        return 1;
    }
    GLuint attached[4] = {0};
    GLsizei attached_count = 0;
    glGetAttachedShaders(program, 4, &attached_count, attached);
    if (attached_count != 2 ||
        !((attached[0] == vs && attached[1] == fs) ||
          (attached[0] == fs && attached[1] == vs))) {
        printf("attached shader query failed\n");
        return 1;
    }
    glDetachShader(program, fs);
    attached_count = 0;
    memset(attached, 0, sizeof(attached));
    glGetAttachedShaders(program, 4, &attached_count, attached);
    if (attached_count != 1 || attached[0] != vs) {
        printf("shader detach query failed\n");
        return 1;
    }
    glAttachShader(program, fs);
    GLint active_uniforms = 0;
    GLint active_attributes = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_uniforms);
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &active_attributes);
    int found_tint = 0;
    int found_position = 0;
    for (GLint i = 0; i < active_uniforms; i++) {
        char name[128] = {0};
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, (GLuint)i, sizeof(name), NULL,
                           &size, &type, name);
        if (strcmp(name, "uTint[0]") == 0 &&
            size == 2 && type == GL_FLOAT_VEC4) {
            found_tint = 1;
        }
    }
    for (GLint i = 0; i < active_attributes; i++) {
        char name[128] = {0};
        GLint size = 0;
        GLenum type = 0;
        glGetActiveAttrib(program, (GLuint)i, sizeof(name), NULL,
                          &size, &type, name);
        if (strcmp(name, "vPosition") == 0 &&
            size == 1 && type == GL_FLOAT_VEC2) {
            found_position = 1;
        }
    }
    if (!found_tint || !found_position) {
        printf("active variable reflection failed\n");
        return 1;
    }

    GLint pos_attr = glGetAttribLocation(program, "vPosition");
    GLint tex_attr = glGetAttribLocation(program, "vTexCoord");
    if (pos_attr != 0 || tex_attr != 1) {
        printf("attrib lookup failed: pos=%d tex=%d\n", pos_attr, tex_attr);
        return 1;
    }

    GLint max_tex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    if (max_tex < 1) {
        printf("glGetIntegerv(GL_MAX_TEXTURE_SIZE) failed: %d\n", max_tex);
        return 1;
    }

    GLint tex_loc = glGetUniformLocation(program, "uTexture");
    if (tex_loc < 0) {
        printf("glGetUniformLocation(uTexture) failed\n");
        return 1;
    }
    glUniform1i(tex_loc, 0);

    GLint mvp_loc = glGetUniformLocation(program, "uMvp");
    if (mvp_loc < 0) {
        printf("glGetUniformLocation(uMvp) failed\n");
        return 1;
    }
    GLfloat mvp[] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp);

    GLint position_scale_loc = glGetUniformLocation(program, "uPositionScale");
    GLint tex_matrix_loc = glGetUniformLocation(program, "uTexMatrix");
    GLint tint_loc = glGetUniformLocation(program, "uTint[0]");
    GLint flags_loc = glGetUniformLocation(program, "uFlags");
    if (position_scale_loc < 0 || tex_matrix_loc < 0 ||
        tint_loc < 0 || flags_loc < 0) {
        printf("extended uniform lookup failed\n");
        return 1;
    }
    GLfloat position_scale[] = {
        1.0f, 0.0f,
        0.0f, 1.0f,
    };
    GLfloat tex_matrix[] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
    GLfloat tints[] = {
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
    };
    GLint flags[] = {1, 0};
    glUniformMatrix2fv(position_scale_loc, 1, GL_FALSE, position_scale);
    glUniformMatrix3fv(tex_matrix_loc, 1, GL_FALSE, tex_matrix);
    glUniform4fv(tint_loc, 2, tints);
    glUniform2iv(flags_loc, 1, flags);
    GLfloat position_scale_readback[4] = {0};
    GLint flags_readback[2] = {0};
    glGetUniformfv(program, position_scale_loc, position_scale_readback);
    glGetUniformiv(program, flags_loc, flags_readback);
    if (memcmp(position_scale, position_scale_readback,
               sizeof(position_scale)) != 0 ||
        memcmp(flags, flags_readback, sizeof(flags)) != 0) {
        printf("uniform value query failed\n");
        return 1;
    }

    GLfloat verts[] = {
         0.0f,  0.8f, 0.5f, 0.5f,
        -0.8f, -0.8f, 0.5f, 0.5f,
         0.8f, -0.8f, 0.5f, 0.5f,
    };
    GLushort indices[] = {0, 1, 2};

    GLuint vbo = 0;
    unsigned char large_vertex_upload[5000] = {0};
    memcpy(large_vertex_upload, verts, sizeof(verts));
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(large_vertex_upload),
                 large_vertex_upload, GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    GLint buffer_size = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &buffer_size);
    if (!glIsBuffer(vbo) || buffer_size != (GLint)sizeof(large_vertex_upload)) {
        printf("buffer query failed: valid=%u size=%d\n",
               glIsBuffer(vbo), buffer_size);
        return 1;
    }
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void *)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    GLint attrib_enabled = 0;
    glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib_enabled);
    if (!attrib_enabled) {
        printf("vertex attrib query failed\n");
        return 1;
    }
    void *attrib_pointer = NULL;
    glGetVertexAttribPointerv(1, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attrib_pointer);
    if ((uintptr_t)attrib_pointer != 2 * sizeof(GLfloat)) {
        printf("vertex attrib pointer query failed: %p\n", attrib_pointer);
        return 1;
    }
    GLfloat current_attrib[] = {0.25f, 0.5f, 0.75f, 1.0f};
    GLfloat current_attrib_readback[4] = {0};
    glVertexAttrib4fv(7, current_attrib);
    glGetVertexAttribfv(7, GL_CURRENT_VERTEX_ATTRIB, current_attrib_readback);
    if (memcmp(current_attrib, current_attrib_readback,
               sizeof(current_attrib)) != 0) {
        printf("current vertex attrib query failed\n");
        return 1;
    }

    GLuint ebo = 0;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    unsigned char texel[] = {255, 0, 255, 255};
    GLuint texture = 0;
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)GL_NEAREST);
    GLfloat wrap_s = (GLfloat)GL_CLAMP_TO_EDGE;
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap_s);
    GLint wrap_t = GL_CLAMP_TO_EDGE;
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrap_t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, texel);
    glGenerateMipmap(GL_TEXTURE_2D);
    GLint min_filter = 0;
    GLfloat mag_filter = 0.0f;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min_filter);
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag_filter);
    if (!glIsTexture(texture) || min_filter != GL_NEAREST ||
        (GLint)mag_filter != GL_NEAREST) {
        printf("texture query failed: valid=%u min=0x%x\n",
               glIsTexture(texture), min_filter);
        return 1;
    }

    unsigned char px[4] = {0};
    GLuint color_target = 0;
    glGenTextures(1, &color_target);
    glBindTexture(GL_TEXTURE_2D, color_target);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    GLuint depth_target = 0;
    glGenRenderbuffers(1, &depth_target);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_target);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 8, 8);
    GLint renderbuffer_width = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH,
                                 &renderbuffer_width);
    if (!glIsRenderbuffer(depth_target) || renderbuffer_width != 8) {
        printf("renderbuffer query failed: valid=%u width=%d\n",
               glIsRenderbuffer(depth_target), renderbuffer_width);
        return 1;
    }

    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_target, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_target);
    GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (!glIsFramebuffer(framebuffer) ||
        framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
        printf("framebuffer incomplete: 0x%x\n", framebuffer_status);
        return 1;
    }
    GLint color_attachment_type = 0;
    GLint color_attachment_name = 0;
    GLint depth_attachment_type = 0;
    GLint depth_attachment_name = 0;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &color_attachment_type);
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &color_attachment_name);
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depth_attachment_type);
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depth_attachment_name);
    if (color_attachment_type != GL_TEXTURE ||
        color_attachment_name != (GLint)color_target ||
        depth_attachment_type != GL_RENDERBUFFER ||
        depth_attachment_name != (GLint)depth_target) {
        printf("framebuffer attachment query failed\n");
        return 1;
    }

    glViewport(0, 0, 8, 8);
    GLint viewport[4] = {0};
    GLfloat line_width_range[2] = {0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, line_width_range);
    if (viewport[0] != 0 || viewport[1] != 0 ||
        viewport[2] != 8 || viewport[3] != 8 ||
        line_width_range[0] <= 0.0f ||
        line_width_range[1] < line_width_range[0]) {
        printf("multi-value state query failed\n");
        return 1;
    }
    glClearColor(0.0f, 0.5f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("offscreen pixel: %u %u %u %u\n", px[0], px[1], px[2], px[3]);
    if (px[0] != 0 || px[1] < 126 || px[1] > 129 || px[2] != 255 || px[3] != 255) {
        printf("offscreen framebuffer readback failed\n");
        return 1;
    }

    unsigned char pattern[] = {
        255,   0,   0, 255,
          0, 255,   0, 255,
          0,   0, 255, 255,
        255, 255, 255, 255,
    };
    unsigned char pattern_readback[sizeof(pattern)] = {0};
    glBindTexture(GL_TEXTURE_2D, color_target);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2,
                    GL_RGBA, GL_UNSIGNED_BYTE, pattern);
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pattern_readback);
    if (memcmp(pattern, pattern_readback, sizeof(pattern)) != 0) {
        printf("rectangular readback failed\n");
        return 1;
    }
    printf("rectangular readback: red green blue white\n");

    GLuint copied_texture = 0;
    glGenTextures(1, &copied_texture);
    glBindTexture(GL_TEXTURE_2D, copied_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 2, 2, 0);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 2, 2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, copied_texture, 0);
    memset(pattern_readback, 0, sizeof(pattern_readback));
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pattern_readback);
    if (memcmp(pattern, pattern_readback, sizeof(pattern)) != 0) {
        printf("copy texture image failed\n");
        return 1;
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_target, 0);

    unsigned char masked_readback[8] = {0};
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    if (!glIsEnabled(GL_SCISSOR_TEST)) {
        printf("glIsEnabled(GL_SCISSOR_TEST) failed\n");
        return 1;
    }
    bridge_scissor(0, 0, 4, 8);
    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
    GLboolean color_mask[4] = {0};
    glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
    if (!color_mask[0] || color_mask[1] || color_mask[2] || color_mask[3]) {
        printf("color mask query failed\n");
        return 1;
    }
    glClearColor(1.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    if (glIsEnabled(GL_SCISSOR_TEST)) {
        printf("glDisable/glIsEnabled failed\n");
        return 1;
    }
    glReadPixels(2, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, masked_readback);
    glReadPixels(6, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, masked_readback + 4);
    if (masked_readback[0] != 255 || masked_readback[1] != 255 ||
        masked_readback[2] != 0 || masked_readback[3] != 255 ||
        masked_readback[4] != 0 || masked_readback[5] != 255 ||
        masked_readback[6] != 0 || masked_readback[7] != 255) {
        printf("scissor/color-mask test failed\n");
        return 1;
    }
    printf("scissor/color-mask: yellow green\n");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, texture);
    px[0] = px[1] = px[2] = px[3] = 0;
    glViewport(0, 0, 64, 64);
    glEnable(GL_BLEND);
    glBlendColor(0.25f, 0.5f, 0.75f, 1.0f);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ZERO);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glStencilFunc(GL_ALWAYS, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xff);
    glStencilFuncSeparate(GL_FRONT, GL_ALWAYS, 1, 0xff);
    glStencilMaskSeparate(GL_FRONT, 0xff);
    glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_REPLACE);
    glClearStencil(0);
    glLineWidth(1.0f);
    glDepthRangef(0.0f, 1.0f);
    glPolygonOffset(0.0f, 0.0f);
    glSampleCoverage(1.0f, GL_FALSE);
    glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearDepthf(1.0f);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0);
    glFlush();
    glFinish();
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("center pixel: %u %u %u %u\n", px[0], px[1], px[2], px[3]);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteRenderbuffers(1, &depth_target);
    glDeleteTextures(1, &color_target);
    glDeleteTextures(1, &copied_texture);
    glDeleteTextures(1, &texture);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    glDeleteShader(fs);
    glDeleteShader(vs);
    glReleaseShaderCompiler();
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) ||
        !eglDestroyContext(dpy, ctx) || !eglDestroySurface(dpy, surf) ||
        !eglTerminate(dpy)) {
        printf("EGL cleanup failed: 0x%x\n", eglGetError());
        return 1;
    }
    return 0;
}
