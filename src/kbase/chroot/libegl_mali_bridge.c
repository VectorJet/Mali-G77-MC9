#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLNativeDisplayType;
typedef void *EGLNativePixmapType;
typedef unsigned long EGLNativeWindowType;
typedef int EGLBoolean;
typedef int EGLint;
typedef unsigned int EGLenum;

#define EGL_TRUE 1
#define EGL_FALSE 0
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)

#define EGL_SUCCESS 0x3000
#define EGL_BAD_ATTRIBUTE 0x3004
#define EGL_BAD_CONFIG 0x3005
#define EGL_BAD_CONTEXT 0x3006
#define EGL_BAD_DISPLAY 0x3008
#define EGL_BAD_MATCH 0x3009
#define EGL_BAD_PARAMETER 0x300C
#define EGL_BAD_SURFACE 0x300D

#define EGL_BUFFER_SIZE 0x3020
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_CONFIG_CAVEAT 0x3027
#define EGL_CONFIG_ID 0x3028
#define EGL_LEVEL 0x3029
#define EGL_MAX_PBUFFER_HEIGHT 0x302A
#define EGL_MAX_PBUFFER_PIXELS 0x302B
#define EGL_MAX_PBUFFER_WIDTH 0x302C
#define EGL_NATIVE_RENDERABLE 0x302D
#define EGL_NATIVE_VISUAL_ID 0x302E
#define EGL_NATIVE_VISUAL_TYPE 0x302F
#define EGL_SAMPLES 0x3031
#define EGL_SAMPLE_BUFFERS 0x3032
#define EGL_SURFACE_TYPE 0x3033
#define EGL_TRANSPARENT_TYPE 0x3034
#define EGL_NONE 0x3038
#define EGL_BIND_TO_TEXTURE_RGB 0x3039
#define EGL_BIND_TO_TEXTURE_RGBA 0x303A
#define EGL_MIN_SWAP_INTERVAL 0x303B
#define EGL_MAX_SWAP_INTERVAL 0x303C
#define EGL_LUMINANCE_SIZE 0x303D
#define EGL_ALPHA_MASK_SIZE 0x303E
#define EGL_COLOR_BUFFER_TYPE 0x303F
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_MATCH_NATIVE_PIXMAP 0x3041
#define EGL_CONFORMANT 0x3042
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_LARGEST_PBUFFER 0x3058
#define EGL_DRAW 0x3059
#define EGL_READ 0x305A
#define EGL_TEXTURE_FORMAT 0x3080
#define EGL_TEXTURE_TARGET 0x3081
#define EGL_MIPMAP_TEXTURE 0x3082
#define EGL_MIPMAP_LEVEL 0x3083
#define EGL_RENDER_BUFFER 0x3086
#define EGL_BACK_BUFFER 0x3084
#define EGL_CONTEXT_CLIENT_TYPE 0x3097
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_PBUFFER_BIT 0x0001
#define EGL_WINDOW_BIT 0x0004
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_RGB_BUFFER 0x308E

#define MAX_SURFACES 16
#define MAX_CONTEXTS 16

struct display_state {
    int initialized;
};

struct config_state {
    int valid;
};

struct surface_state {
    int alive;
    int window_surface;
    EGLint width;
    EGLint height;
    EGLint largest;
    EGLint texture_format;
    EGLint texture_target;
    EGLint mipmap_texture;
    EGLint mipmap_level;
    Display *xdisplay;
    Window xwindow;
    GC gc;
    XShmSegmentInfo shm_info;
    XImage *shm_image;
    int shm_attached;
};

struct context_state {
    int alive;
    EGLint client_version;
};

static struct display_state display_state;
static struct config_state config_state = {1};
static struct surface_state surfaces[MAX_SURFACES];
static struct context_state contexts[MAX_CONTEXTS];
static __thread EGLint last_error = EGL_SUCCESS;
static __thread EGLDisplay current_display;
static __thread EGLSurface current_draw;
static __thread EGLSurface current_read;
static __thread EGLContext current_context;
static __thread EGLenum current_api = EGL_OPENGL_ES_API;
static EGLint swap_interval = 1;

static EGLDisplay display_handle(void) {
    return &display_state;
}

static EGLConfig config_handle(void) {
    return &config_state;
}

static void set_error(EGLint error) {
    last_error = error;
}

static int valid_display(EGLDisplay dpy) {
    return dpy == display_handle();
}

static int valid_config(EGLConfig config) {
    return config == config_handle();
}

static struct surface_state *surface_from_handle(EGLSurface surface) {
    for (int i = 0; i < MAX_SURFACES; i++) {
        if (surface == &surfaces[i] && surfaces[i].alive) return &surfaces[i];
    }
    return NULL;
}

static struct context_state *context_from_handle(EGLContext context) {
    for (int i = 0; i < MAX_CONTEXTS; i++) {
        if (context == &contexts[i] && contexts[i].alive) return &contexts[i];
    }
    return NULL;
}

static EGLint attrib_value(const EGLint *attribs, EGLint name, EGLint fallback) {
    if (!attribs) return fallback;
    for (const EGLint *p = attribs; p[0] != EGL_NONE; p += 2) {
        if (p[0] == name) return p[1];
    }
    return fallback;
}

static void *gles_symbol(const char *name) {
    static void *gles;
    if (!gles) gles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    return gles ? dlsym(gles, name) : NULL;
}


static void shm_free(struct surface_state *s) {
    if (!s->shm_image) return;
    if (s->shm_attached) {
        XShmDetach(s->xdisplay, &s->shm_info);
        s->shm_attached = 0;
    }
    XDestroyImage(s->shm_image);
    s->shm_image = NULL;
    shmdt(s->shm_info.shmaddr);
    shmctl(s->shm_info.shmid, IPC_RMID, 0);
}

static int shm_ensure(struct surface_state *s, int w, int h) {
    if (s->shm_image &&
        s->shm_image->width == w &&
        s->shm_image->height == h)
        return 1;
    shm_free(s);
    XWindowAttributes attrs;
    XGetWindowAttributes(s->xdisplay, s->xwindow, &attrs);
    s->shm_image = XShmCreateImage(s->xdisplay, attrs.visual,
                                    (unsigned)attrs.depth, ZPixmap, NULL,
                                    &s->shm_info, (unsigned)w, (unsigned)h);
    if (!s->shm_image) return 0;
    s->shm_info.shmid = shmget(IPC_PRIVATE,
        (size_t)s->shm_image->bytes_per_line * h, IPC_CREAT | 0600);
    if (s->shm_info.shmid < 0) { XDestroyImage(s->shm_image); s->shm_image = NULL; return 0; }
    s->shm_info.shmaddr = s->shm_image->data = shmat(s->shm_info.shmid, NULL, 0);
    if (s->shm_info.shmaddr == (void *)-1) {
        shmctl(s->shm_info.shmid, IPC_RMID, 0);
        XDestroyImage(s->shm_image); s->shm_image = NULL; return 0;
    }
    s->shm_info.readOnly = 0;
    XShmAttach(s->xdisplay, &s->shm_info);
    s->shm_attached = 1;
    return 1;
}

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
    if (display_id != EGL_DEFAULT_DISPLAY) {
        set_error(EGL_BAD_PARAMETER);
        return EGL_NO_DISPLAY;
    }
    set_error(EGL_SUCCESS);
    return display_handle();
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    display_state.initialized = 1;
    if (major) *major = 1;
    if (minor) *minor = 5;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                         EGLint config_size, EGLint *num_config) {
    if (!valid_display(dpy) || !display_state.initialized) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (!num_config || config_size < 0) {
        set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    *num_config = 1;
    if (configs && config_size > 0) configs[0] = config_handle();
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config) {
    if (!eglGetConfigs(dpy, NULL, 0, num_config)) return EGL_FALSE;
    EGLint renderable = attrib_value(attrib_list, EGL_RENDERABLE_TYPE, 0);
    EGLint surface_type = attrib_value(attrib_list, EGL_SURFACE_TYPE, 0);
    int matches = (!renderable || (renderable & EGL_OPENGL_ES2_BIT)) &&
                  (!surface_type ||
                   (surface_type & (EGL_PBUFFER_BIT | EGL_WINDOW_BIT)));
    *num_config = matches ? 1 : 0;
    if (matches && configs && config_size > 0) configs[0] = config_handle();
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                              EGLint attribute, EGLint *value) {
    if (!valid_display(dpy) || !display_state.initialized) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (!valid_config(config)) {
        set_error(EGL_BAD_CONFIG);
        return EGL_FALSE;
    }
    if (!value) {
        set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    switch (attribute) {
    case EGL_BUFFER_SIZE: *value = 32; break;
    case EGL_RED_SIZE:
    case EGL_GREEN_SIZE:
    case EGL_BLUE_SIZE:
    case EGL_ALPHA_SIZE: *value = 8; break;
    case EGL_DEPTH_SIZE: *value = 24; break;
    case EGL_STENCIL_SIZE: *value = 8; break;
    case EGL_CONFIG_ID: *value = 1; break;
    case EGL_SURFACE_TYPE: *value = EGL_PBUFFER_BIT | EGL_WINDOW_BIT; break;
    case EGL_RENDERABLE_TYPE:
    case EGL_CONFORMANT: *value = EGL_OPENGL_ES2_BIT; break;
    case EGL_MAX_PBUFFER_WIDTH:
    case EGL_MAX_PBUFFER_HEIGHT: *value = 4096; break;
    case EGL_MAX_PBUFFER_PIXELS: *value = 4096 * 4096; break;
    case EGL_MIN_SWAP_INTERVAL: *value = 0; break;
    case EGL_MAX_SWAP_INTERVAL: *value = 1; break;
    case EGL_COLOR_BUFFER_TYPE: *value = EGL_RGB_BUFFER; break;
    case EGL_CONFIG_CAVEAT:
    case EGL_LEVEL:
    case EGL_NATIVE_RENDERABLE:
    case EGL_NATIVE_VISUAL_ID:
    case EGL_NATIVE_VISUAL_TYPE:
    case EGL_SAMPLES:
    case EGL_SAMPLE_BUFFERS:
    case EGL_TRANSPARENT_TYPE:
    case EGL_BIND_TO_TEXTURE_RGB:
    case EGL_BIND_TO_TEXTURE_RGBA:
    case EGL_LUMINANCE_SIZE:
    case EGL_ALPHA_MASK_SIZE: *value = 0; break;
    default:
        set_error(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint *attrib_list) {
    if (!valid_display(dpy) || !display_state.initialized) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_NO_SURFACE;
    }
    if (!valid_config(config)) {
        set_error(EGL_BAD_CONFIG);
        return EGL_NO_SURFACE;
    }
    EGLint width = attrib_value(attrib_list, EGL_WIDTH, 0);
    EGLint height = attrib_value(attrib_list, EGL_HEIGHT, 0);
    if (width < 0 || height < 0 || width > 4096 || height > 4096) {
        set_error(EGL_BAD_ATTRIBUTE);
        return EGL_NO_SURFACE;
    }
    for (int i = 0; i < MAX_SURFACES; i++) {
        if (!surfaces[i].alive) {
            surfaces[i] = (struct surface_state){
                .alive = 1,
                .width = width,
                .height = height,
                .largest = attrib_value(attrib_list, EGL_LARGEST_PBUFFER, EGL_FALSE),
                .texture_format = attrib_value(attrib_list, EGL_TEXTURE_FORMAT, EGL_NONE),
                .texture_target = attrib_value(attrib_list, EGL_TEXTURE_TARGET, EGL_NONE),
                .mipmap_texture = attrib_value(attrib_list, EGL_MIPMAP_TEXTURE, EGL_FALSE),
                .mipmap_level = 0,
            };
            set_error(EGL_SUCCESS);
            return &surfaces[i];
        }
    }
    set_error(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint *attrib_list) {
    (void)attrib_list;
    if (!valid_display(dpy) || !display_state.initialized) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_NO_SURFACE;
    }
    if (!valid_config(config)) {
        set_error(EGL_BAD_CONFIG);
        return EGL_NO_SURFACE;
    }
    Display *xdisplay = XOpenDisplay(NULL);
    if (!xdisplay || !win) {
        if (xdisplay) XCloseDisplay(xdisplay);
        set_error(EGL_BAD_PARAMETER);
        return EGL_NO_SURFACE;
    }
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(xdisplay, (Window)win, &attributes)) {
        XCloseDisplay(xdisplay);
        set_error(EGL_BAD_MATCH);
        return EGL_NO_SURFACE;
    }
    for (int i = 0; i < MAX_SURFACES; i++) {
        if (!surfaces[i].alive) {
            surfaces[i] = (struct surface_state){
                .alive = 1,
                .window_surface = 1,
                .width = attributes.width,
                .height = attributes.height,
                .texture_format = EGL_NONE,
                .texture_target = EGL_NONE,
                .xdisplay = xdisplay,
                .xwindow = (Window)win,
            };
            set_error(EGL_SUCCESS);
            return &surfaces[i];
        }
    }
    XCloseDisplay(xdisplay);
    set_error(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint *attrib_list) {
    if (!valid_display(dpy) || !display_state.initialized) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_NO_CONTEXT;
    }
    if (!valid_config(config)) {
        set_error(EGL_BAD_CONFIG);
        return EGL_NO_CONTEXT;
    }
    if (share_context != EGL_NO_CONTEXT && !context_from_handle(share_context)) {
        set_error(EGL_BAD_CONTEXT);
        return EGL_NO_CONTEXT;
    }
    EGLint version = attrib_value(attrib_list, EGL_CONTEXT_CLIENT_VERSION, 1);
    if (current_api != EGL_OPENGL_ES_API || version != 2) {
        set_error(EGL_BAD_MATCH);
        return EGL_NO_CONTEXT;
    }
    for (int i = 0; i < MAX_CONTEXTS; i++) {
        if (!contexts[i].alive) {
            contexts[i] = (struct context_state){.alive = 1, .client_version = version};
            set_error(EGL_SUCCESS);
            return &contexts[i];
        }
    }
    set_error(EGL_BAD_PARAMETER);
    return EGL_NO_CONTEXT;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                          EGLSurface read, EGLContext ctx) {
    if (!valid_display(dpy) || !display_state.initialized) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (ctx == EGL_NO_CONTEXT && draw == EGL_NO_SURFACE && read == EGL_NO_SURFACE) {
        current_display = EGL_NO_DISPLAY;
        current_draw = EGL_NO_SURFACE;
        current_read = EGL_NO_SURFACE;
        current_context = EGL_NO_CONTEXT;
        set_error(EGL_SUCCESS);
        return EGL_TRUE;
    }
    if (!context_from_handle(ctx)) {
        set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }
    if (!surface_from_handle(draw) || !surface_from_handle(read)) {
        set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    current_display = dpy;
    current_draw = draw;
    current_read = read;
    current_context = ctx;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLContext eglGetCurrentContext(void) {
    return current_context;
}

EGLDisplay eglGetCurrentDisplay(void) {
    return current_display;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw) {
    if (readdraw == EGL_DRAW) return current_draw;
    if (readdraw == EGL_READ) return current_read;
    set_error(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                           EGLint attribute, EGLint *value) {
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    struct surface_state *state = surface_from_handle(surface);
    if (!state) {
        set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (!value) {
        set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    switch (attribute) {
    case EGL_WIDTH: *value = state->width; break;
    case EGL_HEIGHT: *value = state->height; break;
    case EGL_LARGEST_PBUFFER: *value = state->largest; break;
    case EGL_TEXTURE_FORMAT: *value = state->texture_format; break;
    case EGL_TEXTURE_TARGET: *value = state->texture_target; break;
    case EGL_MIPMAP_TEXTURE: *value = state->mipmap_texture; break;
    case EGL_MIPMAP_LEVEL: *value = state->mipmap_level; break;
    case EGL_RENDER_BUFFER: *value = EGL_BACK_BUFFER; break;
    default:
        set_error(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx,
                           EGLint attribute, EGLint *value) {
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    struct context_state *state = context_from_handle(ctx);
    if (!state) {
        set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }
    if (!value) {
        set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    if (attribute == EGL_CONTEXT_CLIENT_TYPE) *value = EGL_OPENGL_ES_API;
    else if (attribute == EGL_CONTEXT_CLIENT_VERSION) *value = state->client_version;
    else {
        set_error(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                            EGLint attribute, EGLint value) {
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    struct surface_state *state = surface_from_handle(surface);
    if (!state) {
        set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (attribute != EGL_MIPMAP_LEVEL) {
        set_error(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }
    state->mipmap_level = value;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglBindAPI(EGLenum api) {
    if (api != EGL_OPENGL_ES_API) {
        set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    current_api = api;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLenum eglQueryAPI(void) {
    return current_api;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    struct surface_state *state = surface_from_handle(surface);
    if (!state) {
        set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (state->window_surface) {
        typedef void (*finish_fn)(void);
        typedef void (*read_pixels_fn)(int, int, int, int, unsigned int,
                                       unsigned int, void *);
        finish_fn bridge_finish = gles_symbol("glFinish");
        read_pixels_fn bridge_read_pixels = gles_symbol("glReadPixels");
        if (!bridge_finish || !bridge_read_pixels) {
            set_error(EGL_BAD_MATCH);
            return EGL_FALSE;
        }
        /* Track window size changes */
        XWindowAttributes win_attrs;
        XGetWindowAttributes(state->xdisplay, state->xwindow, &win_attrs);
        if (win_attrs.width > 0 && win_attrs.height > 0 &&
            (win_attrs.width != state->width ||
             win_attrs.height != state->height)) {
            state->width  = win_attrs.width;
            state->height = win_attrs.height;
        }
        size_t pixels_size = (size_t)state->width * state->height * 4;
        uint8_t *rgba = malloc(pixels_size);
        if (!rgba) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
        bridge_finish();
        bridge_read_pixels(0, 0, state->width, state->height,
                           0x1908, 0x1401, rgba);
        /* Cache GC per surface */
        if (!state->gc)
            state->gc = XCreateGC(state->xdisplay, state->xwindow, 0, NULL);
        /* MIT-SHM path: zero-copy to Xorg */
        if (shm_ensure(state, state->width, state->height)) {
            uint8_t *dst = (uint8_t *)state->shm_image->data;
            int stride   = state->shm_image->bytes_per_line;
            for (int y = 0; y < state->height; y++) {
                int src_y = state->height - 1 - y;
                const uint8_t *src_row = rgba + (size_t)src_y * state->width * 4;
                uint8_t       *dst_row = dst  + (size_t)y    * stride;
                for (int x = 0; x < state->width; x++) {
                    dst_row[x * 4 + 0] = src_row[x * 4 + 2];
                    dst_row[x * 4 + 1] = src_row[x * 4 + 1];
                    dst_row[x * 4 + 2] = src_row[x * 4 + 0];
                    dst_row[x * 4 + 3] = 0;
                }
            }
            free(rgba);
            XShmPutImage(state->xdisplay, state->xwindow, state->gc,
                         state->shm_image, 0, 0, 0, 0,
                         (unsigned)state->width, (unsigned)state->height,
                         False);
            XSync(state->xdisplay, False);
        } else {
            /* Fallback: XPutImage */
            uint8_t *bgra = malloc(pixels_size);
            if (!bgra) { free(rgba); set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
            for (int y = 0; y < state->height; y++) {
                int src_y = state->height - 1 - y;
                for (int x = 0; x < state->width; x++) {
                    size_t s = ((size_t)src_y * state->width + x) * 4;
                    size_t d = ((size_t)y     * state->width + x) * 4;
                    bgra[d+0] = rgba[s+2];
                    bgra[d+1] = rgba[s+1];
                    bgra[d+2] = rgba[s+0];
                    bgra[d+3] = 0;
                }
            }
            free(rgba);
            XImage *image = XCreateImage(state->xdisplay, win_attrs.visual,
                                         (unsigned)win_attrs.depth, ZPixmap, 0,
                                         (char *)bgra, (unsigned)state->width,
                                         (unsigned)state->height, 32, 0);
            if (image) {
                XPutImage(state->xdisplay, state->xwindow, state->gc, image,
                          0, 0, 0, 0,
                          (unsigned)state->width, (unsigned)state->height);
                XSync(state->xdisplay, False);
                XDestroyImage(image); /* frees bgra */
            } else {
                free(bgra);
            }
        }
    }
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (interval < 0 || interval > 1) {
        set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    swap_interval = interval;
    (void)swap_interval;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglWaitClient(void) {
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglWaitGL(void) {
    return eglWaitClient();
}

EGLBoolean eglWaitNative(EGLint engine) {
    (void)engine;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface,
                          EGLNativePixmapType target) {
    (void)target;
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (!surface_from_handle(surface)) {
        set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    set_error(EGL_BAD_MATCH);
    return EGL_FALSE;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    struct surface_state *state = surface_from_handle(surface);
    if (!state) {
        set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (current_draw == surface) current_draw = EGL_NO_SURFACE;
    if (current_read == surface) current_read = EGL_NO_SURFACE;
    shm_free(state);
    if (state->gc) { XFreeGC(state->xdisplay, state->gc); state->gc = NULL; }
    if (state->xdisplay) XCloseDisplay(state->xdisplay);
    memset(state, 0, sizeof(*state));
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    struct context_state *state = context_from_handle(ctx);
    if (!state) {
        set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }
    if (current_context == ctx) current_context = EGL_NO_CONTEXT;
    memset(state, 0, sizeof(*state));
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglReleaseThread(void) {
    current_display = EGL_NO_DISPLAY;
    current_draw = EGL_NO_SURFACE;
    current_read = EGL_NO_SURFACE;
    current_context = EGL_NO_CONTEXT;
    current_api = EGL_OPENGL_ES_API;
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    if (!valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    display_state.initialized = 0;
    for (int i = 0; i < MAX_SURFACES; i++) {
        if (surfaces[i].alive) {
            shm_free(&surfaces[i]);
            if (surfaces[i].gc) XFreeGC(surfaces[i].xdisplay, surfaces[i].gc);
            if (surfaces[i].xdisplay) XCloseDisplay(surfaces[i].xdisplay);
        }
    }
    memset(surfaces, 0, sizeof(surfaces));
    memset(contexts, 0, sizeof(contexts));
    eglReleaseThread();
    set_error(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLint eglGetError(void) {
    EGLint error = last_error;
    last_error = EGL_SUCCESS;
    return error;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name) {
    if (dpy != EGL_NO_DISPLAY && !valid_display(dpy)) {
        set_error(EGL_BAD_DISPLAY);
        return NULL;
    }
    switch (name) {
    case 0x3053: return "Mali bridge EGL";
    case 0x3054: return "1.5";
    case 0x308D: return "OpenGL_ES";
    case 0x3055: return "EGL_KHR_create_context";
    default:
        set_error(EGL_BAD_PARAMETER);
        return NULL;
    }
}

void (*eglGetProcAddress(const char *procname))(void) {
    if (!procname) return NULL;
    return (void (*)(void))gles_symbol(procname);
}
