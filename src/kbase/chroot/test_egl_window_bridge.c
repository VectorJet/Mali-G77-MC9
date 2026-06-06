#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int channel(unsigned long pixel, unsigned long mask) {
    unsigned int shift = 0;
    unsigned long value;
    unsigned long maximum;
    unsigned long original_mask = mask;

    if (!mask) return 0;
    while (!(mask & 1)) {
        mask >>= 1;
        shift++;
    }
    value = (pixel & original_mask) >> shift;
    maximum = mask;
    return (unsigned int)((value * 255 + maximum / 2) / maximum);
}

int main(void) {
    Display *xdisplay = XOpenDisplay(NULL);
    if (!xdisplay) {
        fprintf(stderr, "XOpenDisplay failed\n");
        return 1;
    }

    int screen = DefaultScreen(xdisplay);
    Window root = RootWindow(xdisplay, screen);
    Window window = XCreateSimpleWindow(xdisplay, root, 0, 0, 64, 64, 0,
                                        BlackPixel(xdisplay, screen),
                                        BlackPixel(xdisplay, screen));
    XStoreName(xdisplay, window, "Mali EGL bridge test");
    XMapWindow(xdisplay, window);
    XSync(xdisplay, False);

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed: 0x%x\n", eglGetError());
        return 1;
    }

    const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config = NULL;
    EGLint config_count = 0;
    if (!eglChooseConfig(dpy, config_attrs, &config, 1, &config_count) ||
        config_count != 1) {
        fprintf(stderr, "eglChooseConfig failed: 0x%x\n", eglGetError());
        return 1;
    }

    EGLSurface surface = eglCreateWindowSurface(dpy, config, window, NULL);
    const EGLint context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    EGLContext context = eglCreateContext(dpy, config, EGL_NO_CONTEXT,
                                          context_attrs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(dpy, surface, surface, context)) {
        fprintf(stderr, "EGL window setup failed: 0x%x\n", eglGetError());
        return 1;
    }

    glViewport(0, 0, 64, 64);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!eglSwapBuffers(dpy, surface)) {
        fprintf(stderr, "eglSwapBuffers failed: 0x%x\n", eglGetError());
        return 1;
    }

    XSync(xdisplay, False);
    XImage *image = XGetImage(xdisplay, window, 32, 32, 1, 1, AllPlanes,
                              ZPixmap);
    if (!image) {
        fprintf(stderr, "XGetImage failed\n");
        return 1;
    }
    unsigned long pixel = XGetPixel(image, 0, 0);
    unsigned int red = channel(pixel, image->red_mask);
    unsigned int green = channel(pixel, image->green_mask);
    unsigned int blue = channel(pixel, image->blue_mask);
    XDestroyImage(image);

    printf("egl window: %d.%d\n", major, minor);
    printf("renderer: %s\n", glGetString(GL_RENDERER));
    printf("window pixel: %u %u %u\n", red, green, blue);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, context);
    eglDestroySurface(dpy, surface);
    eglTerminate(dpy);
    XDestroyWindow(xdisplay, window);
    XCloseDisplay(xdisplay);

    return red >= 250 && green <= 5 && blue <= 5 ? 0 : 1;
}
