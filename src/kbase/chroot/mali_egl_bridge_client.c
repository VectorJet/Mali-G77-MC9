#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BRIDGE_SOCKET "/data/local/tmp/mali_egl_bridge.sock"
#define BRIDGE_MAGIC 0x31474c4dU
#define BRIDGE_CMD_CLEAR_READ 1
#define BRIDGE_MAX_TEXT 4096
#define BRIDGE_MAX_FLOATS 1024
#define BRIDGE_MAX_BYTES 4096
#define BRIDGE_MAX_RESPONSE_TEXT 1024

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

static float parse_channel(const char *s) {
    char *end = NULL;
    float v = strtof(s, &end);
    if (!end || *end != '\0' || v < 0.0f || v > 1.0f) {
        fprintf(stderr, "invalid color channel '%s' (expected 0.0..1.0)\n", s);
        exit(2);
    }
    return v;
}

int main(int argc, char **argv) {
    struct bridge_request req = {
        .magic = BRIDGE_MAGIC,
        .cmd = BRIDGE_CMD_CLEAR_READ,
        .rgba = {1.0f, 0.0f, 0.0f, 1.0f},
    };

    if (argc == 4 || argc == 5) {
        req.rgba[0] = parse_channel(argv[1]);
        req.rgba[1] = parse_channel(argv[2]);
        req.rgba[2] = parse_channel(argv[3]);
        req.rgba[3] = argc == 5 ? parse_channel(argv[4]) : 1.0f;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [r g b [a]]\n", argv[0]);
        return 2;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", BRIDGE_SOCKET);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "connect %s failed: %s\n", BRIDGE_SOCKET, strerror(errno));
        return 1;
    }

    if (write_full(fd, &req, sizeof(req)) != (ssize_t)sizeof(req)) {
        perror("write");
        return 1;
    }

    struct bridge_response resp;
    ssize_t got = read_full(fd, &resp, sizeof(resp));
    if (got != (ssize_t)sizeof(resp)) {
        fprintf(stderr, "short read: %zd\n", got);
        return 1;
    }

    if (resp.magic != BRIDGE_MAGIC || resp.status != 0) {
        fprintf(stderr, "bridge error: magic=0x%08x status=%u\n", resp.magic, resp.status);
        return 1;
    }

    printf("requested rgba: %.3f %.3f %.3f %.3f\n",
           req.rgba[0], req.rgba[1], req.rgba[2], req.rgba[3]);
    printf("gpu pixel rgba: %u %u %u %u\n",
           resp.pixel[0], resp.pixel[1], resp.pixel[2], resp.pixel[3]);
    return 0;
}
