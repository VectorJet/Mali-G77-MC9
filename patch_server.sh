sed -i '/int main/i \
static void *shared_pixels = NULL;\
static void init_shm() {\
    int fd = open("/data/local/tmp/mali_bridge_pixels.bin", O_RDWR | O_CREAT, 0666);\
    if (fd < 0) return;\
    ftruncate(fd, 32 * 1024 * 1024);\
    shared_pixels = mmap(NULL, 32 * 1024 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);\
    close(fd);\
}' src/kbase/tools/mali_egl_bridge_server.c

sed -i '/printf("mali-egl-bridge ready\\n");/i \    init_shm();' src/kbase/tools/mali_egl_bridge_server.c

sed -i '/case BRIDGE_CMD_READ_PIXELS:/i \
    case 999: {\
        uint32_t width = req->a[2];\
        uint32_t height = req->a[3];\
        if (shared_pixels) {\
            gl->glReadPixels((GLint)req->a[0], (GLint)req->a[1], (GLsizei)width, (GLsizei)height, 0x1908, 0x1401, shared_pixels);\
        }\
        break;\
    }' src/kbase/tools/mali_egl_bridge_server.c
