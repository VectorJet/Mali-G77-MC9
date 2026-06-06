#include <GL/gl.h>
#include <stdio.h>

int main(void) {
    unsigned char px[4] = {0};

    printf("vendor: %s\n", glGetString(GL_VENDOR));
    printf("renderer: %s\n", glGetString(GL_RENDERER));
    printf("version: %s\n", glGetString(GL_VERSION));

    glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);

    printf("pixel: %u %u %u %u\n", px[0], px[1], px[2], px[3]);
    return 0;
}
