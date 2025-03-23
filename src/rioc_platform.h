#ifndef RIOC_PLATFORM_H
#define RIOC_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/uio.h>
#include "rioc.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

// TLS context structure
typedef struct rioc_tls_context {
    SSL_CTX *ctx;
    SSL *ssl;
    bool is_server;
} rioc_tls_context;

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define RIOC_PLATFORM_WINDOWS
#elif defined(__linux__) && !defined(RIOC_PLATFORM_LINUX)
    #define RIOC_PLATFORM_LINUX
#elif defined(__APPLE__) && !defined(RIOC_PLATFORM_MACOS)
    #define RIOC_PLATFORM_MACOS
#else
    #define RIOC_PLATFORM_POSIX
#endif

// Platform-specific optimizations
#ifdef RIOC_PLATFORM_MACOS
    #define RIOC_USE_DISPATCH_SEMAPHORE 1
#endif

// Socket types and constants
#ifdef RIOC_PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET rioc_socket_t;
    #define RIOC_INVALID_SOCKET INVALID_SOCKET
    #define RIOC_SOCKET_ERROR SOCKET_ERROR
    #define RIOC_EINTR WSAEINTR
    #define RIOC_EAGAIN WSAEWOULDBLOCK
    #define RIOC_EWOULDBLOCK WSAEWOULDBLOCK
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    typedef int rioc_socket_t;
    #define RIOC_INVALID_SOCKET (-1)
    #define RIOC_SOCKET_ERROR (-1)
    #define RIOC_EINTR EINTR
    #define RIOC_EAGAIN EAGAIN
    #define RIOC_EWOULDBLOCK EWOULDBLOCK
#endif

// Platform-specific socket operations
int rioc_platform_init(void);
void rioc_platform_cleanup(void);
rioc_socket_t rioc_socket_create(void);
int rioc_socket_close(rioc_socket_t socket);
int rioc_set_socket_options(rioc_socket_t socket);
ssize_t rioc_send(rioc_socket_t socket, const void* buf, size_t len, int flags);
ssize_t rioc_recv(rioc_socket_t socket, void* buf, size_t len, int flags);
int rioc_socket_error(void);

// Platform-specific time operations
uint64_t rioc_get_timestamp_ns(void);
void rioc_sleep_us(unsigned int usec);

// Platform-specific TCP optimizations
void rioc_enable_tcp_cork(rioc_socket_t socket);
void rioc_disable_tcp_cork(rioc_socket_t socket);

// Platform-specific thread operations
int rioc_pin_thread_to_cpu(int cpu);

// TLS operations
int rioc_tls_init(void);
void rioc_tls_cleanup(void);

// Server TLS operations
int rioc_tls_server_ctx_create(rioc_tls_context *tls_ctx, const rioc_tls_config *config);
int rioc_tls_server_accept(rioc_tls_context *tls_ctx, int client_fd);
void rioc_tls_server_ctx_free(rioc_tls_context *tls_ctx);

// Client TLS operations
int rioc_tls_client_ctx_create(rioc_tls_context *tls_ctx, const rioc_tls_config *config);
int rioc_tls_client_connect(rioc_tls_context *tls_ctx, int fd, const char *hostname);
void rioc_tls_client_ctx_free(rioc_tls_context *tls_ctx);

// TLS I/O operations
int rioc_tls_read(rioc_tls_context *tls_ctx, void *buf, size_t len);
int rioc_tls_write(rioc_tls_context *tls_ctx, const void *buf, size_t len);
int rioc_tls_readv(rioc_tls_context *tls_ctx, struct iovec *iov, int iovcnt);
int rioc_tls_writev(rioc_tls_context *tls_ctx, const struct iovec *iov, int iovcnt);

#endif // RIOC_PLATFORM_H 