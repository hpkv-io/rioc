#define _GNU_SOURCE
#include "rioc_platform.h"

#ifndef RIOC_PLATFORM_WINDOWS

#include <time.h>
#include <signal.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>

#ifdef RIOC_PLATFORM_MACOS
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread/pthread.h>

// macOS-specific socket options
#ifndef TCP_NOPUSH
#define TCP_NOPUSH 4
#endif

#ifndef TCP_KEEPALIVE
#define TCP_KEEPALIVE 0x10
#endif

#ifndef TCP_CONNECTIONTIMEOUT
#define TCP_CONNECTIONTIMEOUT 0x20
#endif
#endif

int rioc_platform_init(void) {
    // Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);

    // Initialize TLS
    int ret = rioc_tls_init();
    if (ret != RIOC_SUCCESS) {
        return ret;
    }

    return RIOC_SUCCESS;
}

void rioc_platform_cleanup(void) {
    // Cleanup TLS
    rioc_tls_cleanup();
}

rioc_socket_t rioc_socket_create(void) {
    return socket(AF_INET, SOCK_STREAM, 0);
}

int rioc_socket_close(rioc_socket_t socket) {
    return close(socket);
}

int rioc_set_socket_options(rioc_socket_t socket) {
    int flag = 1;
    int ret;

    // Basic TCP options that work everywhere
    ret = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    if (ret < 0) return ret;

    // Set buffer sizes
    int buf_size = 1024 * 1024;  // 1MB buffer
    ret = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    if (ret < 0) return ret;
    
    ret = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    if (ret < 0) return ret;

    // Set keep-alive
    ret = setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
    if (ret < 0) return ret;

    // Set low latency options
    int prio = IPTOS_LOWDELAY;
    setsockopt(socket, IPPROTO_IP, IP_TOS, &prio, sizeof(prio));

#ifdef RIOC_PLATFORM_LINUX
    // Linux-specific optimizations
    setsockopt(socket, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));
    
    int keepalive_time = 10;
    int keepalive_intvl = 1;
    int keepalive_probes = 3;
    
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepalive_time, sizeof(keepalive_time));
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_intvl, sizeof(keepalive_intvl));
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_probes, sizeof(keepalive_probes));
#endif

#ifdef RIOC_PLATFORM_MACOS
    // macOS-specific optimizations

    // Enable TCP keepalive with aggressive settings
    int keepalive = 10;  // 10 seconds
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPALIVE, &keepalive, sizeof(keepalive));

    // Set socket to reuse address
    flag = 1;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
#endif

    return 0;
}

ssize_t rioc_send(rioc_socket_t socket, const void* buf, size_t len, int flags) {
    return send(socket, buf, len, flags | MSG_NOSIGNAL);
}

ssize_t rioc_recv(rioc_socket_t socket, void* buf, size_t len, int flags) {
    return recv(socket, buf, len, flags | MSG_WAITALL);
}

int rioc_socket_error(void) {
    return errno;
}

uint64_t rioc_get_timestamp_ns(void) {
    struct timespec ts;
#ifdef RIOC_PLATFORM_MACOS
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void rioc_sleep_us(unsigned int usec) {
#ifdef RIOC_PLATFORM_MACOS
    struct timespec ts = {
        .tv_sec = usec / 1000000,
        .tv_nsec = (usec % 1000000) * 1000
    };
    nanosleep(&ts, NULL);
#else
    usleep(usec);
#endif
}

void rioc_enable_tcp_cork(rioc_socket_t socket) {
#ifdef RIOC_PLATFORM_LINUX
    int flag = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_CORK, &flag, sizeof(flag));
#elif defined(RIOC_PLATFORM_MACOS)
    // On macOS, use TCP_NOPUSH for better coalescing
    int flag = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NOPUSH, &flag, sizeof(flag));
#else
    (void)socket; // Suppress unused parameter warning
#endif
}

void rioc_disable_tcp_cork(rioc_socket_t socket) {
#ifdef RIOC_PLATFORM_LINUX
    int flag = 0;
    setsockopt(socket, IPPROTO_TCP, TCP_CORK, &flag, sizeof(flag));
#elif defined(RIOC_PLATFORM_MACOS)
    // On macOS, disable TCP_NOPUSH and force a flush
    int flag = 0;
    setsockopt(socket, IPPROTO_TCP, TCP_NOPUSH, &flag, sizeof(flag));
    // Force a flush
    char dummy = 0;
    send(socket, &dummy, 0, 0);
#else
    (void)socket; // Suppress unused parameter warning
#endif
}

int rioc_pin_thread_to_cpu(int cpu) {
#ifdef RIOC_PLATFORM_LINUX
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif defined(RIOC_PLATFORM_MACOS)
    // On macOS, use thread policy to suggest CPU affinity
    thread_affinity_policy_data_t policy = { cpu };
    thread_port_t mach_thread = pthread_mach_thread_np(pthread_self());
    return thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                           (thread_policy_t)&policy, THREAD_AFFINITY_POLICY_COUNT);
#else
    (void)cpu; // Suppress unused parameter warning
    return 0;
#endif
}

#endif // !RIOC_PLATFORM_WINDOWS 
