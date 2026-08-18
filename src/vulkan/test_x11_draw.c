#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    Display *dpy = XOpenDisplay(":0");
    if (!dpy) {
        fprintf(stderr, "Cannot open display :0\n");
        return 1;
    }
    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);
    printf("Display size: %dx%d\n", width, height);

    Window win = XCreateSimpleWindow(dpy, root, 0, 0, width, height, 0,
                                     BlackPixel(dpy, screen), WhitePixel(dpy, screen));
    XSelectInput(dpy, win, ExposureMask | KeyPressMask);
    XMapWindow(dpy, win);
    XFlush(dpy);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, 0x00FF00); /* Bright Green */
    XFillRectangle(dpy, win, gc, 50, 50, width - 100, height - 100);
    XFlush(dpy);

    printf("Drew green rectangle on X11 window. Sleeping 5 seconds...\n");
    sleep(5);

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
