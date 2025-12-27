/*
 * Hemlock Socket Compatibility Layer
 *
 * Provides cross-platform socket primitives:
 * - BSD sockets API for POSIX
 * - Winsock2 API for Windows
 */

#ifndef HML_COMPAT_SOCKET_H
#define HML_COMPAT_SOCKET_H

#include "platform.h"

#ifdef HML_WINDOWS

/* ========== Windows Winsock2 Implementation ========== */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600  /* Windows Vista+ for WSAPoll */
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

/* Socket type */
typedef SOCKET hml_socket_t;
#define HML_INVALID_SOCKET INVALID_SOCKET
#define HML_SOCKET_ERROR SOCKET_ERROR

/* Socket initialization and cleanup */
HML_INLINE int hml_socket_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

HML_INLINE void hml_socket_cleanup(void) {
    WSACleanup();
}

/* Socket operations */
HML_INLINE hml_socket_t hml_socket(int domain, int type, int protocol) {
    return socket(domain, type, protocol);
}

HML_INLINE int hml_bind(hml_socket_t sock, const struct sockaddr *addr, int addrlen) {
    return bind(sock, addr, addrlen);
}

HML_INLINE int hml_listen(hml_socket_t sock, int backlog) {
    return listen(sock, backlog);
}

HML_INLINE hml_socket_t hml_accept(hml_socket_t sock, struct sockaddr *addr, int *addrlen) {
    return accept(sock, addr, addrlen);
}

HML_INLINE int hml_connect(hml_socket_t sock, const struct sockaddr *addr, int addrlen) {
    return connect(sock, addr, addrlen);
}

HML_INLINE int hml_send(hml_socket_t sock, const void *buf, int len, int flags) {
    return send(sock, (const char *)buf, len, flags);
}

HML_INLINE int hml_recv(hml_socket_t sock, void *buf, int len, int flags) {
    return recv(sock, (char *)buf, len, flags);
}

HML_INLINE int hml_sendto(hml_socket_t sock, const void *buf, int len, int flags,
                           const struct sockaddr *dest_addr, int addrlen) {
    return sendto(sock, (const char *)buf, len, flags, dest_addr, addrlen);
}

HML_INLINE int hml_recvfrom(hml_socket_t sock, void *buf, int len, int flags,
                             struct sockaddr *src_addr, int *addrlen) {
    return recvfrom(sock, (char *)buf, len, flags, src_addr, addrlen);
}

HML_INLINE int hml_setsockopt(hml_socket_t sock, int level, int optname,
                               const void *optval, int optlen) {
    return setsockopt(sock, level, optname, (const char *)optval, optlen);
}

HML_INLINE int hml_getsockopt(hml_socket_t sock, int level, int optname,
                               void *optval, int *optlen) {
    return getsockopt(sock, level, optname, (char *)optval, optlen);
}

HML_INLINE int hml_closesocket(hml_socket_t sock) {
    return closesocket(sock);
}

HML_INLINE int hml_shutdown(hml_socket_t sock, int how) {
    return shutdown(sock, how);
}

/* Error handling */
HML_INLINE int hml_socket_error(void) {
    return WSAGetLastError();
}

HML_INLINE const char *hml_socket_strerror(int err) {
    static HML_THREAD_LOCAL char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   buf, sizeof(buf), NULL);
    /* Remove trailing newline */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
        buf[--len] = '\0';
    }
    return buf;
}

/* Poll - Windows Vista+ has WSAPoll */
typedef WSAPOLLFD hml_pollfd;

#define HML_POLLIN   POLLRDNORM
#define HML_POLLOUT  POLLWRNORM
#define HML_POLLERR  POLLERR
#define HML_POLLHUP  POLLHUP
#define HML_POLLNVAL POLLNVAL

HML_INLINE int hml_poll(hml_pollfd *fds, unsigned int nfds, int timeout) {
    return WSAPoll(fds, nfds, timeout);
}

/* Non-blocking mode */
HML_INLINE int hml_socket_set_nonblocking(hml_socket_t sock, int nonblocking) {
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode);
}

/* Address conversion (these exist in ws2tcpip.h) */
/* inet_pton and inet_ntop are available on Windows Vista+ */

/* Common socket constants */
#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

/* Socket error codes mapping */
#define HML_EAGAIN       WSAEWOULDBLOCK
#define HML_EWOULDBLOCK  WSAEWOULDBLOCK
#define HML_EINPROGRESS  WSAEINPROGRESS
#define HML_ECONNRESET   WSAECONNRESET
#define HML_ENOTCONN     WSAENOTCONN
#define HML_ETIMEDOUT    WSAETIMEDOUT

#else /* POSIX Implementation */

/* ========== POSIX BSD Sockets Implementation ========== */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

/* Socket type */
typedef int hml_socket_t;
#define HML_INVALID_SOCKET (-1)
#define HML_SOCKET_ERROR   (-1)

/* Socket initialization and cleanup (no-op on POSIX) */
HML_INLINE int hml_socket_init(void) {
    return 0;
}

HML_INLINE void hml_socket_cleanup(void) {
    /* No-op */
}

/* Socket operations */
HML_INLINE hml_socket_t hml_socket(int domain, int type, int protocol) {
    return socket(domain, type, protocol);
}

HML_INLINE int hml_bind(hml_socket_t sock, const struct sockaddr *addr, socklen_t addrlen) {
    return bind(sock, addr, addrlen);
}

HML_INLINE int hml_listen(hml_socket_t sock, int backlog) {
    return listen(sock, backlog);
}

HML_INLINE hml_socket_t hml_accept(hml_socket_t sock, struct sockaddr *addr, socklen_t *addrlen) {
    return accept(sock, addr, addrlen);
}

HML_INLINE int hml_connect(hml_socket_t sock, const struct sockaddr *addr, socklen_t addrlen) {
    return connect(sock, addr, addrlen);
}

HML_INLINE ssize_t hml_send(hml_socket_t sock, const void *buf, size_t len, int flags) {
    return send(sock, buf, len, flags);
}

HML_INLINE ssize_t hml_recv(hml_socket_t sock, void *buf, size_t len, int flags) {
    return recv(sock, buf, len, flags);
}

HML_INLINE ssize_t hml_sendto(hml_socket_t sock, const void *buf, size_t len, int flags,
                               const struct sockaddr *dest_addr, socklen_t addrlen) {
    return sendto(sock, buf, len, flags, dest_addr, addrlen);
}

HML_INLINE ssize_t hml_recvfrom(hml_socket_t sock, void *buf, size_t len, int flags,
                                 struct sockaddr *src_addr, socklen_t *addrlen) {
    return recvfrom(sock, buf, len, flags, src_addr, addrlen);
}

HML_INLINE int hml_setsockopt(hml_socket_t sock, int level, int optname,
                               const void *optval, socklen_t optlen) {
    return setsockopt(sock, level, optname, optval, optlen);
}

HML_INLINE int hml_getsockopt(hml_socket_t sock, int level, int optname,
                               void *optval, socklen_t *optlen) {
    return getsockopt(sock, level, optname, optval, optlen);
}

HML_INLINE int hml_closesocket(hml_socket_t sock) {
    return close(sock);
}

HML_INLINE int hml_shutdown(hml_socket_t sock, int how) {
    return shutdown(sock, how);
}

/* Error handling */
HML_INLINE int hml_socket_error(void) {
    return errno;
}

HML_INLINE const char *hml_socket_strerror(int err) {
    return strerror(err);
}

/* Poll */
typedef struct pollfd hml_pollfd;

#define HML_POLLIN   POLLIN
#define HML_POLLOUT  POLLOUT
#define HML_POLLERR  POLLERR
#define HML_POLLHUP  POLLHUP
#define HML_POLLNVAL POLLNVAL

HML_INLINE int hml_poll(hml_pollfd *fds, nfds_t nfds, int timeout) {
    return poll(fds, nfds, timeout);
}

/* Non-blocking mode */
HML_INLINE int hml_socket_set_nonblocking(hml_socket_t sock, int nonblocking) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(sock, F_SETFL, flags);
}

/* Socket error codes */
#define HML_EAGAIN       EAGAIN
#define HML_EWOULDBLOCK  EWOULDBLOCK
#define HML_EINPROGRESS  EINPROGRESS
#define HML_ECONNRESET   ECONNRESET
#define HML_ENOTCONN     ENOTCONN
#define HML_ETIMEDOUT    ETIMEDOUT

#endif /* HML_WINDOWS */

/* ========== Common Definitions ========== */

/* Address family constants (same on both platforms) */
#ifndef AF_INET
#define AF_INET  2
#endif
#ifndef AF_INET6
#define AF_INET6 23  /* Windows uses 23, POSIX varies but we don't redefine if exists */
#endif

/* Socket type constants */
#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM  2
#endif

/* Protocol constants */
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

#endif /* HML_COMPAT_SOCKET_H */
