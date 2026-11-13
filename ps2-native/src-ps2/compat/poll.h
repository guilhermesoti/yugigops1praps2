#ifndef MEMORIES_PS2_POLL_H
#define MEMORIES_PS2_POLL_H
struct pollfd { int fd; short events, revents; };
#define POLLIN 1
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
typedef unsigned nfds_t;
static inline int poll(struct pollfd *fds, nfds_t n, int timeout) { (void)fds; (void)n; (void)timeout; return 0; }
#endif
