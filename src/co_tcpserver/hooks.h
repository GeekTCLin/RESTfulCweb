#ifndef CWEB_COROUTINE_HOOKS_H_
#define CWEB_COROUTINE_HOOKS_H_

#include <sys/socket.h>

namespace cweb {
namespace tcpserver {
namespace coroutine {

ssize_t hook_read(int fd, void *buf, size_t nbyte);
ssize_t hook_write(int fd, const void *buf, size_t nbyte);
int hook_accept(int fd, struct sockaddr *addr, socklen_t *len);

}
}
}
#endif
