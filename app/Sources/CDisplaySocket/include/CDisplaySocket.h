#ifndef CDISPLAYSOCKET_H
#define CDISPLAYSOCKET_H

#include <stddef.h>
#include <sys/types.h>

/* Local-socket helpers for the ClassicMac display protocol (display/README.md):
 * the app listens, QEMU connects, and the shared framebuffer's file descriptor
 * arrives with SCM_RIGHTS. All calls return -errno on failure. */

/* Creates a non-blocking AF_UNIX SOCK_STREAM listener at path (replacing any
 * stale socket file). Returns the listening descriptor. */
int cds_listen(const char *path);

/* Accepts one pending connection as a non-blocking, SIGPIPE-free socket. */
int cds_accept(int listen_fd);

/* Reads up to len bytes. Any descriptors passed with the bytes are stored in
 * fds (up to max_fds) and counted in *nfds. Returns bytes read, 0 at end of
 * stream, -EAGAIN when nothing is ready. */
ssize_t cds_recv(int fd, void *buf, size_t len, int *fds, int max_fds, int *nfds);

/* Writes all of buf, waiting while the socket is full (input messages are
 * small; QEMU reads them promptly). */
ssize_t cds_send_all(int fd, const void *buf, size_t len);

/* Non-blocking single write attempt. Returns bytes written (> 0), -EAGAIN when
 * the socket buffer is full, or -errno on error. */
ssize_t cds_send_nonblocking(int fd, const void *buf, size_t len);

/* Maps a shared framebuffer read-only. Returns NULL on failure. */
const void *cds_map(int fd, size_t size);
void cds_unmap(const void *addr, size_t size);

void cds_close(int fd);

/* Copies an XRGB8888 region into a BGRA8888 destination buffer with the alpha
 * byte forced opaque (0xFF), since guest XRGB pixels may leave byte 3 zero. */
void cds_copy_xrgb_opaque(void *dst, size_t dst_stride, const void *src, size_t src_stride, size_t width, size_t height);

#endif
