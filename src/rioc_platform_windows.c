#include "rioc_platform.h"

#ifdef RIOC_PLATFORM_WINDOWS

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

static LARGE_INTEGER performance_frequency;
static BOOL frequency_initialized = FALSE;

int rioc_platform_init(void) {
    // Initialize Winsock
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        return result;
    }

    // Initialize high-resolution timer
    if (!frequency_initialized) {
        QueryPerformanceFrequency(&performance_frequency);
        frequency_initialized = TRUE;
    }

    // Initialize multimedia timer for microsecond sleep
    timeBeginPeriod(1);

    // Initialize TLS
    int ret = rioc_tls_init();
    if (ret != RIOC_SUCCESS) {
        // Cleanup previous initializations
        timeEndPeriod(1);
        WSACleanup();
        return ret;
    }

    return RIOC_SUCCESS;
}

void rioc_platform_cleanup(void) {
    // Cleanup TLS first
    rioc_tls_cleanup();

    // Cleanup Windows-specific resources
    timeEndPeriod(1);
    WSACleanup();
}

rioc_socket_t rioc_socket_create(void) {
    return WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
}

int rioc_socket_close(rioc_socket_t socket) {
    return closesocket(socket);
}

int rioc_set_socket_options(rioc_socket_t socket) {
    BOOL flag = TRUE;
    int ret;

    // Basic TCP options
    ret = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
    if (ret == SOCKET_ERROR) return ret;

    // Set buffer sizes
    int buf_size = 1024 * 1024;  // 1MB buffer
    ret = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&buf_size, sizeof(buf_size));
    if (ret == SOCKET_ERROR) return ret;
    
    ret = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&buf_size, sizeof(buf_size));
    if (ret == SOCKET_ERROR) return ret;

    // Set keep-alive
    struct tcp_keepalive keepalive;
    keepalive.onoff = 1;
    keepalive.keepalivetime = 10000;  // 10 seconds
    keepalive.keepaliveinterval = 1000;  // 1 second
    
    DWORD bytes_returned;
    WSAIoctl(socket, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive),
             NULL, 0, &bytes_returned, NULL, NULL);

    // Set QoS
    DWORD qos = 0x10;  // SERVICETYPE_CONTROLLEDLOAD
    ret = setsockopt(socket, SOL_SOCKET, SO_PRIORITY, (char*)&qos, sizeof(qos));

    return 0;
}

ssize_t rioc_send(rioc_socket_t socket, const void* buf, size_t len, int flags) {
    return send(socket, (const char*)buf, (int)len, flags);
}

ssize_t rioc_recv(rioc_socket_t socket, void* buf, size_t len, int flags) {
    return recv(socket, (char*)buf, (int)len, flags);
}

int rioc_socket_error(void) {
    return WSAGetLastError();
}

uint64_t rioc_get_timestamp_ns(void) {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000000000ULL / performance_frequency.QuadPart);
}

void rioc_sleep_us(unsigned int usec) {
    LARGE_INTEGER start, now, counts;
    QueryPerformanceCounter(&start);
    counts.QuadPart = (usec * performance_frequency.QuadPart) / 1000000ULL;
    do {
        QueryPerformanceCounter(&now);
    } while ((now.QuadPart - start.QuadPart) < counts.QuadPart);
}

#endif // RIOC_PLATFORM_WINDOWS 