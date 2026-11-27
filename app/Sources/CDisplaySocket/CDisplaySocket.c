#include "CDisplaySocket.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -errno;
    }
    return 0;
}

int cds_listen(const char *path)
{
    struct sockaddr_un addr;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        return -ENAMETOOLONG;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -errno;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    unlink(path);
    mode_t prev_umask = umask(0077);
    int bind_rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    int bind_err = errno;
    umask(prev_umask);
    if (bind_rc < 0) {
        close(fd);
        return -bind_err;
    }
    if (chmod(path, 0600) < 0 || listen(fd, 2) < 0) {
        int err = errno;
        close(fd);
        unlink(path);
        return -err;
    }
    int rc = set_nonblocking(fd);
    if (rc < 0) {
        close(fd);
        return rc;
    }
    return fd;
}

int cds_accept(int listen_fd)
{
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) {
        return -errno;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    int rc = set_nonblocking(fd);
    if (rc < 0) {
        close(fd);
        return rc;
    }
    return fd;
}

ssize_t cds_recv(int fd, void *buf, size_t len, int *fds, int max_fds, int *nfds)
{
    struct iovec iov = { .iov_base = buf, .iov_len = len };
    union {
        struct cmsghdr hdr;
        char bytes[CMSG_SPACE(sizeof(int) * 8)];
    } control;
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.bytes;
    msg.msg_controllen = sizeof(control.bytes);
    *nfds = 0;

    ssize_t n = recvmsg(fd, &msg, 0);
    if (n < 0) {
        return -errno;
    }
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        int count = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
        const int *passed = (const int *)CMSG_DATA(c);
        for (int i = 0; i < count; i++) {
            if (*nfds < max_fds) {
                fds[(*nfds)++] = passed[i];
            } else {
                close(passed[i]);
            }
        }
    }
    return n;
}

ssize_t cds_send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = send(fd, p, left, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                struct pollfd pfd = { .fd = fd, .events = POLLOUT };
                if (poll(&pfd, 1, 1000) <= 0) {
                    return -EAGAIN;
                }
                continue;
            }
            return -errno;
        }
        p += n;
        left -= (size_t)n;
    }
    return (ssize_t)len;
}

ssize_t cds_send_nonblocking(int fd, const void *buf, size_t len)
{
    if (len == 0) {
        return 0;
    }
    for (;;) {
        ssize_t n = send(fd, buf, len, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -EAGAIN;
            }
            return -errno;
        }
        return n;
    }
}

const void *cds_map(int fd, size_t size)
{
    void *addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    return addr == MAP_FAILED ? NULL : addr;
}

void cds_unmap(const void *addr, size_t size)
{
    if (addr) {
        munmap((void *)addr, size);
    }
}

void cds_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

void cds_copy_xrgb_opaque(void *dst, size_t dst_stride, const void *src, size_t src_stride, size_t width, size_t height)
{
    if (!dst || !src || width == 0 || height == 0) {
        return;
    }
    uint8_t *restrict d_row = (uint8_t *)dst;
    const uint8_t *restrict s_row = (const uint8_t *)src;
    for (size_t y = 0; y < height; y++) {
        uint8_t *restrict d = d_row;
        const uint8_t *restrict s = s_row;
        for (size_t x = 0; x < width; x++) {
            uint32_t px;
            memcpy(&px, s + x * 4, sizeof(px));
            px |= 0xff000000u;
            memcpy(d + x * 4, &px, sizeof(px));
        }
        d_row += dst_stride;
        s_row += src_stride;
    }
}
