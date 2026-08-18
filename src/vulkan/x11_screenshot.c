#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#pragma pack(push, 1)
struct BMPHeader {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)

static int ignore_x_errors(Display *d, XErrorEvent *e) {
    (void)d; (void)e;
    return 0;
}

int main(int argc, char **argv) {
    XSetErrorHandler(ignore_x_errors);
    Display *dpy = XOpenDisplay(getenv("DISPLAY") ? getenv("DISPLAY") : ":0");
    if (!dpy) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }
    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);

    XImage *img = XGetImage(dpy, root, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!img) {
        fprintf(stderr, "XGetImage failed\n");
        XCloseDisplay(dpy);
        return 1;
    }

    /* Composite all visible children onto root image */
    Window root_ret, parent_ret, *children = NULL;
    unsigned int nchildren = 0;
    if (XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren) && children) {
        for (unsigned int i = 0; i < nchildren; i++) {
            Window child = children[i];
            XWindowAttributes attr;
            if (XGetWindowAttributes(dpy, child, &attr) &&
                attr.class == InputOutput &&
                attr.map_state == IsViewable &&
                attr.width > 0 && attr.height > 0) {
                int dest_x = attr.x, dest_y = attr.y;
                Window child_dummy;
                XTranslateCoordinates(dpy, child, root, 0, 0, &dest_x, &dest_y, &child_dummy);
                XImage *cimg = XGetImage(dpy, child, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
                if (cimg) {
                    for (int cy = 0; cy < attr.height; cy++) {
                        int ry = dest_y + cy;
                        if (ry < 0 || ry >= height) continue;
                        for (int cx = 0; cx < attr.width; cx++) {
                            int rx = dest_x + cx;
                            if (rx < 0 || rx >= width) continue;
                            unsigned long p = XGetPixel(cimg, cx, cy);
                            XPutPixel(img, rx, ry, p);
                        }
                    }
                    XDestroyImage(cimg);
                }
            }
        }
        XFree(children);
    }

    const char *outfile = argc > 1 ? argv[1] : "x11_screen.bmp";
    FILE *f = fopen(outfile, "wb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    uint32_t row_stride = (width * 3 + 3) & ~3;
    uint32_t image_size = row_stride * height;

    struct BMPHeader hdr = {
        .bfType = 0x4D42,
        .bfSize = sizeof(struct BMPHeader) + image_size,
        .bfOffBits = sizeof(struct BMPHeader),
        .biSize = 40,
        .biWidth = width,
        .biHeight = height,
        .biPlanes = 1,
        .biBitCount = 24,
        .biCompression = 0,
        .biSizeImage = image_size,
    };
    fwrite(&hdr, sizeof(hdr), 1, f);

    uint8_t *row_buf = malloc(row_stride);
    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            unsigned long pixel = XGetPixel(img, x, y);
            row_buf[x * 3 + 0] = pixel & 0xFF;
            row_buf[x * 3 + 1] = (pixel >> 8) & 0xFF;
            row_buf[x * 3 + 2] = (pixel >> 16) & 0xFF;
        }
        fwrite(row_buf, row_stride, 1, f);
    }
    free(row_buf);
    fclose(f);
    XDestroyImage(img);
    XCloseDisplay(dpy);
    printf("Saved composite X11 screenshot (%dx%d) to %s\n", width, height, outfile);
    return 0;
}
