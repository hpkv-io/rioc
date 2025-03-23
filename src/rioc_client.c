#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <stdatomic.h>
#include <limits.h>
#include <inttypes.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <sys/uio.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <netinet/ip.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <poll.h>
#endif

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#include "rioc.h"
#include "rioc_platform.h"

// Define IPTOS_LOWDELAY if not available
#ifndef IPTOS_LOWDELAY
#define IPTOS_LOWDELAY 0x10
#endif

// Define IOV_MAX if not available
#ifndef IOV_MAX
#define IOV_MAX 1024  // Conservative default
#endif

// Define MSG_MORE if not available (Linux-specific)
#ifndef MSG_MORE
#define MSG_MORE 0
#endif


// Forward declarations
static void* response_thread_func(void *arg);

// Helper function to send vectored I/O
static ssize_t writev_all(rioc_socket_t fd, struct iovec *iov, int iovcnt) {
    size_t total_size = 0;
    for (int i = 0; i < iovcnt; i++) {
        total_size += iov[i].iov_len;
    }
    
    // For small transfers, use regular send
    if (total_size <= 4096) {
        char stack_buffer[4096];
        char *p = stack_buffer;
        
        // Coalesce small buffers
        for (int i = 0; i < iovcnt; i++) {
            memcpy(p, iov[i].iov_base, iov[i].iov_len);
            p += iov[i].iov_len;
        }
        
        size_t sent = 0;
        while (sent < total_size) {
            ssize_t n = rioc_send(fd, stack_buffer + sent, total_size - sent, 0);
            if (n <= 0) {
                if (rioc_socket_error() == RIOC_EINTR) continue;
                if (rioc_socket_error() == RIOC_EAGAIN || rioc_socket_error() == RIOC_EWOULDBLOCK) continue;
                return -1;
            }
            sent += n;
        }
        return sent;
    }

    // For larger transfers, use writev with optimized chunking
    size_t total = 0;
    struct iovec *curr_iov = iov;
    int curr_iovcnt = iovcnt;
    
    // Enable TCP_CORK for large transfers
    rioc_enable_tcp_cork(fd);
    
    while (curr_iovcnt > 0) {
        // Prefetch next IOV if available
        if (curr_iovcnt > 1) {
            __builtin_prefetch(curr_iov + 1, 0, 3);
        }
        
        ssize_t n = writev(fd, curr_iov, curr_iovcnt);
        if (n <= 0) {
            if (rioc_socket_error() == RIOC_EINTR) continue;
            if (rioc_socket_error() == RIOC_EAGAIN || rioc_socket_error() == RIOC_EWOULDBLOCK) continue;
            rioc_disable_tcp_cork(fd);
            return -1;
        }
        total += n;
        
        // Update IOVs
        size_t processed = n;
        while (processed > 0 && curr_iovcnt > 0) {
            if (processed >= curr_iov->iov_len) {
                processed -= curr_iov->iov_len;
                curr_iov++;
                curr_iovcnt--;
            } else {
                curr_iov->iov_base = (char *)curr_iov->iov_base + processed;
                curr_iov->iov_len -= processed;
                break;
            }
        }
    }
    
    // Disable TCP_CORK and flush
    rioc_disable_tcp_cork(fd);
    
    return total;
}

static ssize_t recv_all(rioc_socket_t fd, void *buf, size_t len) {
    // For small transfers (≤4KB), use pre-allocated aligned buffer
    static __thread char recv_buffer[4096] __attribute__((aligned(RIOC_CACHE_LINE_SIZE)));
    
    if (len <= 4096) {
        ssize_t n = rioc_recv(fd, recv_buffer, len, 0);
        if (n > 0) {
            __builtin_prefetch(buf, 1, 3);  // Prefetch destination
            memcpy(buf, recv_buffer, n);
        }
        return n;
    }
    
    // For larger transfers, use readv with optimized chunking
    struct iovec iov[2];  // Use smaller IOV array for better cache usage
    size_t remaining = len;
    char *p = buf;
    
    while (remaining > 0) {
        size_t chunk_size = (remaining > 65536) ? 65536 : remaining;
        iov[0].iov_base = p;
        iov[0].iov_len = chunk_size;
        
        // Prefetch next chunk
        if (remaining > chunk_size) {
            __builtin_prefetch(p + chunk_size, 1, 3);
        }
        
        ssize_t n = readv(fd, iov, 1);
        if (n <= 0) {
            if (rioc_socket_error() == RIOC_EINTR) continue;
            if (rioc_socket_error() == RIOC_EAGAIN || rioc_socket_error() == RIOC_EWOULDBLOCK) continue;
            return -1;
        }
        
        p += n;
        remaining -= n;
    }
    
    return len;
}

// Send a single operation header and data
static int send_op(struct rioc_client *client, uint16_t command, const char *key, size_t key_len,
                  const char *value, size_t value_len, uint64_t timestamp) {
    // Use vectored I/O for better performance
    struct iovec iovs[4];  // batch_header + op_header + key + value
    int iov_count = 0;
    
    // Setup batch header
    static __thread struct rioc_batch_header batch_header;
    batch_header.magic = RIOC_MAGIC;
    batch_header.version = RIOC_VERSION;
    batch_header.count = 1;
    batch_header.flags = RIOC_FLAG_PIPELINE | RIOC_FLAG_MORE;
    iovs[iov_count].iov_base = &batch_header;
    iovs[iov_count].iov_len = sizeof(batch_header);
    iov_count++;
    
    // Setup op header
    static __thread struct rioc_op_header op_header;
    op_header.command = command;
    op_header.key_len = key_len;
    op_header.value_len = value_len;
    op_header.timestamp = timestamp;
    iovs[iov_count].iov_base = &op_header;
    iovs[iov_count].iov_len = sizeof(op_header);
    iov_count++;
    
    // Setup key
    iovs[iov_count].iov_base = (void*)key;
    iovs[iov_count].iov_len = key_len;
    iov_count++;
    
    // Setup value if present
    if (value && value_len > 0) {
        iovs[iov_count].iov_base = (void*)value;
        iovs[iov_count].iov_len = value_len;
        iov_count++;
    }
     
    // Calculate total size
    size_t total_size = 0;
    for (int i = 0; i < iov_count; i++) {
        total_size += iovs[i].iov_len;
    }
    
    // For small operations (≤4KB), use regular send
    if (total_size <= 4096) {
        static __thread char send_buffer[4096] __attribute__((aligned(RIOC_CACHE_LINE_SIZE)));
        char *p = send_buffer;
        
        for (int i = 0; i < iov_count; i++) {
            memcpy(p, iovs[i].iov_base, iovs[i].iov_len);
            p += iovs[i].iov_len;
        }
        
        if (client->tls) {
            size_t sent = 0;
            while (sent < total_size) {
                int n = rioc_tls_write(client->tls, send_buffer + sent, total_size - sent);
                if (n <= 0) {
                    if (n == -EAGAIN) continue;
                    return RIOC_ERR_IO;
                }
                sent += n;
            }
        } else {
            size_t sent = 0;
            while (sent < total_size) {
                ssize_t n = rioc_send(client->fd, send_buffer + sent, total_size - sent, 0);
                if (n <= 0) {
                    if (rioc_socket_error() == RIOC_EINTR) continue;
                    return RIOC_ERR_IO;
                }
                sent += n;
            }
        }
        return RIOC_SUCCESS;
    }
    
    // For larger operations, use writev or TLS write
    if (client->tls) {
        // TLS doesn't support writev, so we need to write each buffer separately
        for (int i = 0; i < iov_count; i++) {
            size_t sent = 0;
            while (sent < iovs[i].iov_len) {
                int n = rioc_tls_write(client->tls, 
                                     (char*)iovs[i].iov_base + sent, 
                                     iovs[i].iov_len - sent);
                if (n <= 0) {
                    if (n == -EAGAIN) continue;
                    return RIOC_ERR_IO;
                }
                sent += n;
            }
        }
        return RIOC_SUCCESS;
    } else {
        ssize_t n = writev_all(client->fd, iovs, iov_count);
        return (n >= 0 && (size_t)n == total_size) ? RIOC_SUCCESS : RIOC_ERR_IO;
    }
}

static int recv_response(rioc_socket_t fd, struct rioc_response_header *response, 
                        char **value, size_t *value_len, struct rioc_tls_context *tls) {
    
    // Use pre-allocated thread-local buffer for response header
    static __thread struct rioc_response_header header_buf __attribute__((aligned(RIOC_CACHE_LINE_SIZE)));
    
    // Receive response header with optimized recv
    ssize_t n;
    if (tls) {
        n = rioc_tls_read(tls, &header_buf, sizeof(header_buf));
    } else {
        n = rioc_recv(fd, &header_buf, sizeof(header_buf), 0);
    }
    if (n != sizeof(header_buf)) {
        return RIOC_ERR_IO;
    }
    
    *response = header_buf;
    
    // Check status early to avoid unnecessary work
    if ((int32_t)response->status != RIOC_SUCCESS) {
        return (int32_t)response->status;
    }
    
    // Read value if present
    if (response->value_len > 0 && value && value_len) {
        // Use pre-allocated buffer for small values
        static __thread char value_buf[4096] __attribute__((aligned(RIOC_CACHE_LINE_SIZE)));
        
        if (response->value_len <= sizeof(value_buf)) {
            // Fast path for small values
            if (tls) {
                n = rioc_tls_read(tls, value_buf, response->value_len);
            } else {
                n = rioc_recv(fd, value_buf, response->value_len, 0);
            }
            if (n != (ssize_t)response->value_len) {
                return RIOC_ERR_IO;
            }
            
            *value = malloc(response->value_len + 1);
            if (!*value) {
                return RIOC_ERR_MEM;
            }
            
            memcpy(*value, value_buf, response->value_len);
            (*value)[response->value_len] = '\0';
        } else {
            // Slow path for large values
            *value = malloc(response->value_len + 1);
            if (!*value) {
                return RIOC_ERR_MEM;
            }
            
            if (tls) {
                n = rioc_tls_read(tls, *value, response->value_len);
            } else {
                n = rioc_recv(fd, *value, response->value_len, 0);
            }
            if (n != (ssize_t)response->value_len) {
                free(*value);
                return RIOC_ERR_IO;
            }
            
            (*value)[response->value_len] = '\0';
        }
        
        *value_len = response->value_len;
    } else if (value && value_len) {
        *value = NULL;
        *value_len = 0;
    }
    
    return RIOC_SUCCESS;
}

// Client API implementation
int rioc_client_init(struct rioc_client *client, const char *host, int port) {
    if (!client || !host || port <= 0) {
        return RIOC_ERR_PARAM;
    }
    
    // Initialize platform
    int ret = rioc_platform_init();
    if (ret != 0) {
        return RIOC_ERR_IO;
    }
    
    // Create socket
    client->fd = rioc_socket_create();
    if (client->fd == RIOC_INVALID_SOCKET) {
        return RIOC_ERR_IO;
    }
    
    // Set socket options
    if (rioc_set_socket_options(client->fd) != 0) {
        rioc_socket_close(client->fd);
        return RIOC_ERR_IO;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        rioc_socket_close(client->fd);
        return RIOC_ERR_IO;
    }

    // Connect to server
    if (connect(client->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        rioc_socket_close(client->fd);
        return RIOC_ERR_IO;
    }

    return RIOC_SUCCESS;
}

int rioc_client_close(struct rioc_client *client) {
    if (!client) {
        return RIOC_ERR_PARAM;
    }
    
    if (client->fd != RIOC_INVALID_SOCKET) {
        rioc_socket_close(client->fd);
        client->fd = RIOC_INVALID_SOCKET;
    }
    
    rioc_platform_cleanup();
    return RIOC_SUCCESS;
}

// Create a new batch
struct rioc_batch *rioc_batch_create(struct rioc_client *client) {
    struct rioc_batch *batch;
    // Align to cache line for better performance
    if (posix_memalign((void**)&batch, RIOC_CACHE_LINE_SIZE, sizeof(*batch)) != 0) {
        return NULL;
    }
    memset(batch, 0, sizeof(*batch));
    
    batch->client = client;
    
    // Pre-allocate value buffer with full batch size plus padding for alignment
    batch->value_buffer_size = RIOC_MAX_VALUE_SIZE * RIOC_MAX_BATCH_SIZE + RIOC_CACHE_LINE_SIZE;
    if (posix_memalign((void**)&batch->value_buffer, RIOC_CACHE_LINE_SIZE, batch->value_buffer_size) != 0) {
        free(batch);
        return NULL;
    }
    
    // Initialize batch header
    batch->batch_header.magic = RIOC_MAGIC;
    batch->batch_header.version = RIOC_VERSION;
    batch->batch_header.count = 0;
    batch->batch_header.flags = RIOC_FLAG_PIPELINE | RIOC_FLAG_MORE;  // Set flags at creation
    
    return batch;
}

// Add operation to batch
static int batch_add_op(struct rioc_batch *batch, 
                       uint16_t command,
                       const char *key, size_t key_len,
                       const char *value, size_t value_len,
                       uint64_t timestamp) {
    if (!batch || !key || key_len > RIOC_MAX_KEY_SIZE ||
        (value && value_len > RIOC_MAX_VALUE_SIZE) ||
        batch->count >= RIOC_MAX_BATCH_SIZE) {
        return RIOC_ERR_PARAM;
    }
    
    struct rioc_batch_op *op = &batch->ops[batch->count];
    
    // Set up header
    op->header.command = command;
    op->header.key_len = key_len;
    op->header.value_len = value_len;
    op->header.timestamp = timestamp;
    
    // Copy key with prefetch
    __builtin_prefetch(key, 0, 3);  // Prefetch key for read
    __builtin_prefetch(op->key, 1, 3);  // Prefetch destination for write
    memcpy(op->key, key, key_len);
    
    // Value handling
    if (value && value_len > 0) {
        // Align value buffer to cache line
        size_t aligned_offset = (batch->count * RIOC_MAX_VALUE_SIZE + RIOC_CACHE_LINE_SIZE - 1) 
                               & ~(RIOC_CACHE_LINE_SIZE - 1);
        char *value_dest = batch->value_buffer + aligned_offset;
        
        // Prefetch for value copy
        __builtin_prefetch(value, 0, 3);  // Prefetch value for read
        __builtin_prefetch(value_dest, 1, 3);  // Prefetch destination for write
        
        // Copy value with unrolled loop for large values
        if (value_len >= 64) {
            size_t i;
            for (i = 0; i + 63 < value_len; i += 64) {
                __builtin_prefetch(value + i + 64, 0, 3);
                __builtin_prefetch(value_dest + i + 64, 1, 3);
                memcpy(value_dest + i, value + i, 64);
            }
            if (i < value_len) {
                memcpy(value_dest + i, value + i, value_len - i);
            }
        } else {
            memcpy(value_dest, value, value_len);
        }
        
        op->value_ptr = value_dest;
        op->value_offset = aligned_offset;
    } else {
        op->value_ptr = NULL;
        op->value_offset = 0;
    }
    
    batch->count++;
    return RIOC_SUCCESS;
}

int rioc_batch_add_get(struct rioc_batch *batch, const char *key, size_t key_len) {
    return batch_add_op(batch, RIOC_CMD_GET, key, key_len, NULL, 0, 0);
}

int rioc_batch_add_insert(struct rioc_batch *batch, const char *key, size_t key_len,
                         const char *value, size_t value_len, uint64_t timestamp) {
    return batch_add_op(batch, RIOC_CMD_INSERT, key, key_len, value, value_len, timestamp);
}

int rioc_batch_add_delete(struct rioc_batch *batch, const char *key, size_t key_len,
                         uint64_t timestamp) {
    return batch_add_op(batch, RIOC_CMD_DELETE, key, key_len, NULL, 0, timestamp);
}

int rioc_batch_add_range_query(struct rioc_batch *batch, 
                              const char *start_key, size_t start_key_len,
                              const char *end_key, size_t end_key_len) {
    if (!batch || !start_key || !end_key || 
        start_key_len > RIOC_MAX_KEY_SIZE || end_key_len > RIOC_MAX_KEY_SIZE ||
        batch->count >= RIOC_MAX_BATCH_SIZE) {
        return RIOC_ERR_PARAM;
    }
    
    struct rioc_batch_op *op = &batch->ops[batch->count];
    
    // Set up header
    op->header.command = RIOC_CMD_RANGE_QUERY;
    op->header.key_len = start_key_len;
    op->header.value_len = end_key_len;
    op->header.timestamp = 0;  // Not used for range query
    
    // Copy start key with prefetch
    __builtin_prefetch(start_key, 0, 3);  // Prefetch start key for read
    __builtin_prefetch(op->key, 1, 3);    // Prefetch destination for write
    memcpy(op->key, start_key, start_key_len);
    
    // For end key, we need to allocate space in the value buffer
    if (end_key_len > 0) {
        // Align value buffer to cache line
        size_t aligned_offset = (batch->count * RIOC_MAX_VALUE_SIZE + RIOC_CACHE_LINE_SIZE - 1) 
                               & ~(RIOC_CACHE_LINE_SIZE - 1);
        char *value_dest = batch->value_buffer + aligned_offset;
        
        // Prefetch for value copy
        __builtin_prefetch(end_key, 0, 3);     // Prefetch end key for read
        __builtin_prefetch(value_dest, 1, 3);  // Prefetch destination for write
        
        // Copy end key
        memcpy(value_dest, end_key, end_key_len);
        
        op->value_ptr = value_dest;
        op->value_offset = aligned_offset;
    } else {
        op->value_ptr = NULL;
        op->value_offset = 0;
    }
    
    batch->count++;
    return RIOC_SUCCESS;
}

int rioc_batch_add_atomic_inc_dec(struct rioc_batch *batch, const char *key, size_t key_len,
                                 int64_t increment, uint64_t timestamp) {
    return batch_add_op(batch, RIOC_CMD_ATOMIC_INC_DEC, key, key_len, 
                       (const char*)&increment, sizeof(increment), timestamp);
}

void rioc_batch_free(struct rioc_batch *batch) {
    if (batch) {
        free(batch->value_buffer);
        free(batch);
    }
}

// Execute batch asynchronously
struct rioc_batch_tracker* rioc_batch_execute_async(struct rioc_batch *batch) {
    if (!batch || batch->count == 0) {
        return NULL;
    }
    
    // Allocate and initialize tracker
    struct rioc_batch_tracker *tracker;
    if (posix_memalign((void**)&tracker, RIOC_CACHE_LINE_SIZE, sizeof(*tracker)) != 0) {
        return NULL;
    }
    memset(tracker, 0, sizeof(*tracker));
    
    tracker->batch = batch;
    atomic_init(&tracker->completed, 0);
    atomic_init(&tracker->error, 0);
    atomic_init(&tracker->responses_received, 0);
    
    // Update batch header count
    batch->batch_header.count = batch->count;
    
    // Pre-calculate total IOVs and allocate array
    size_t total_iovs = 1;  // Batch header
    for (size_t i = 0; i < batch->count; i++) {
        total_iovs += 2;  // Header + key
        if (batch->ops[i].value_ptr) {
            total_iovs++;  // Value
        }
    }
    
    // Allocate IOV array with cache alignment
    struct iovec *iovs;
    if (posix_memalign((void**)&iovs, RIOC_CACHE_LINE_SIZE, total_iovs * sizeof(struct iovec)) != 0) {
        free(tracker);
        return NULL;
    }
    
    // Set up IOVs with prefetching
    iovs[0].iov_base = &batch->batch_header;
    iovs[0].iov_len = sizeof(struct rioc_batch_header);
    
    size_t iov_index = 1;
    for (size_t i = 0; i < batch->count; i++) {
        struct rioc_batch_op *op = &batch->ops[i];
        
        // Prefetch next operation
        if (i + 1 < batch->count) {
            __builtin_prefetch(&batch->ops[i + 1], 0, 3);
        }
        
        // Header IOV
        iovs[iov_index].iov_base = &op->header;
        iovs[iov_index].iov_len = sizeof(op->header);
        iov_index++;
        
        // Key IOV
        iovs[iov_index].iov_base = op->key;
        iovs[iov_index].iov_len = op->header.key_len;
        iov_index++;
        
        // Value IOV
        if (op->value_ptr) {
            iovs[iov_index].iov_base = (void *)op->value_ptr;
            iovs[iov_index].iov_len = op->header.value_len;
            iov_index++;
        }
    }
    
    // Enable TCP_CORK for batch send (only for non-TLS)
    if (!batch->client->tls) {
#ifdef __linux__
        int cork = 1;
        setsockopt(batch->client->fd, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork));
#endif
    }
    
    // Send all data
    if (batch->client->tls) {
        // Use TLS vectored I/O for TLS connections
        if (rioc_tls_writev(batch->client->tls, iovs, total_iovs) < 0) {
            free(iovs);
            free(tracker);
            return NULL;
        }
    } else {
        // Use regular vectored I/O for non-TLS connections
        if (writev_all(batch->client->fd, iovs, total_iovs) < 0) {
            free(iovs);
#ifdef __linux__
            int cork = 0;
            setsockopt(batch->client->fd, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork));
#endif
            free(tracker);
            return NULL;
        }
    }

    // Disable TCP_CORK and flush (only for non-TLS)
    if (!batch->client->tls) {
#ifdef __linux__
        int cork = 0;
        setsockopt(batch->client->fd, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork));
#endif
    }
    
    free(iovs);
    
    // Start response thread
    if (pthread_create(&tracker->response_thread, NULL, response_thread_func, tracker) != 0) {
        free(tracker);
        return NULL;
    }
    
    return tracker;
}

// Response thread function for async batch execution
static void* response_thread_func(void *arg) {
    struct rioc_batch_tracker *tracker = (struct rioc_batch_tracker *)arg;
    struct rioc_batch *batch = tracker->batch;
    struct rioc_response_header response;
    
    // Allocate aligned response buffer
    char *response_buffer;
    if (posix_memalign((void**)&response_buffer, RIOC_CACHE_LINE_SIZE, 
                       RIOC_MAX_VALUE_SIZE + RIOC_CACHE_LINE_SIZE) != 0) {
        atomic_store_explicit(&tracker->error, RIOC_ERR_MEM, memory_order_release);
        atomic_store_explicit(&tracker->completed, 1, memory_order_release);
        return NULL;
    }
    
    // Process all operations
    for (size_t i = 0; i < batch->count; i++) {
        struct rioc_batch_op *op = &batch->ops[i];
        
        // Prefetch next operation
        if (i + 1 < batch->count) {
            __builtin_prefetch(&batch->ops[i + 1], 0, 3);
        }
        
        // Receive response header
        ssize_t ret;
        if (batch->client->tls) {
            ret = rioc_tls_read(batch->client->tls, &response, sizeof(response));
        } else {
            ret = recv_all(batch->client->fd, &response, sizeof(response));
        }
        if (ret != sizeof(response)) {
            free(response_buffer);
            atomic_store_explicit(&tracker->error, RIOC_ERR_IO, memory_order_release);
            atomic_store_explicit(&tracker->completed, 1, memory_order_release);
            return NULL;
        }
        
        // Store response
        op->response.status = response.status;
        op->response.value_len = response.value_len;
        
        // Handle GET responses
        if ((op->header.command == RIOC_CMD_GET || op->header.command == RIOC_CMD_ATOMIC_INC_DEC) && 
            response.value_len > 0) {
            // Receive value data directly into response buffer
            if (batch->client->tls) {
                ret = rioc_tls_read(batch->client->tls, response_buffer, response.value_len);
            } else {
                ret = recv_all(batch->client->fd, response_buffer, response.value_len);
            }
            if (ret != (ssize_t)response.value_len) {
                free(response_buffer);
                atomic_store_explicit(&tracker->error, RIOC_ERR_IO, memory_order_release);
                atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                return NULL;
            }
            
            // Allocate and copy value
            char *value = malloc(response.value_len + 1);
            if (!value) {
                free(response_buffer);
                atomic_store_explicit(&tracker->error, RIOC_ERR_MEM, memory_order_release);
                atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                return NULL;
            }
            
            memcpy(value, response_buffer, response.value_len);
            // Only add null terminator for GET, not for atomic operations
            if (op->header.command == RIOC_CMD_GET) {
                value[response.value_len] = '\0';
            }
            op->value_ptr = value;
        }
        // Handle RANGE_QUERY responses
        else if (op->header.command == RIOC_CMD_RANGE_QUERY && response.value_len > 0) {
            // Allocate array for range results
            size_t count = response.value_len;
            struct rioc_range_result *results = malloc(count * sizeof(struct rioc_range_result));
            if (!results) {
                free(response_buffer);
                atomic_store_explicit(&tracker->error, RIOC_ERR_MEM, memory_order_release);
                atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                return NULL;
            }
            memset(results, 0, count * sizeof(struct rioc_range_result));
            
            // Allocate buffer for results
            size_t buffer_size = RIOC_MAX_VALUE_SIZE * 10;  // 10MB max
            char *buffer = malloc(buffer_size);
            if (!buffer) {
                free(results);
                free(response_buffer);
                atomic_store_explicit(&tracker->error, RIOC_ERR_MEM, memory_order_release);
                atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                return NULL;
            }
            
            // Receive results
            size_t offset = 0;
            
            // Process each result
            for (size_t j = 0; j < count; j++) {
                uint16_t key_len;
                size_t value_len;
                
                // Receive key length
                if (batch->client->tls) {
                    ret = rioc_tls_read(batch->client->tls, buffer + offset, sizeof(uint16_t));
                } else {
                    ret = recv_all(batch->client->fd, buffer + offset, sizeof(uint16_t));
                }
                if (ret != sizeof(uint16_t)) {
                    free(buffer);
                    for (size_t k = 0; k < j; k++) {
                        if (results[k].key) free(results[k].key);
                        if (results[k].value) free(results[k].value);
                    }
                    free(results);
                    free(response_buffer);
                    atomic_store_explicit(&tracker->error, RIOC_ERR_IO, memory_order_release);
                    atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                    return NULL;
                }
                
                memcpy(&key_len, buffer + offset, sizeof(uint16_t));
                offset += sizeof(uint16_t);
                
                // Receive key
                if (batch->client->tls) {
                    ret = rioc_tls_read(batch->client->tls, buffer + offset, key_len);
                } else {
                    ret = recv_all(batch->client->fd, buffer + offset, key_len);
                }
                if (ret != key_len) {
                    free(buffer);
                    for (size_t k = 0; k < j; k++) {
                        if (results[k].key) free(results[k].key);
                        if (results[k].value) free(results[k].value);
                    }
                    free(results);
                    free(response_buffer);
                    atomic_store_explicit(&tracker->error, RIOC_ERR_IO, memory_order_release);
                    atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                    return NULL;
                }
                
                // Allocate and copy key
                results[j].key = malloc(key_len + 1);
                if (!results[j].key) {
                    free(buffer);
                    for (size_t k = 0; k < j; k++) {
                        if (results[k].key) free(results[k].key);
                        if (results[k].value) free(results[k].value);
                    }
                    free(results);
                    free(response_buffer);
                    atomic_store_explicit(&tracker->error, RIOC_ERR_MEM, memory_order_release);
                    atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                    return NULL;
                }
                
                memcpy(results[j].key, buffer + offset, key_len);
                results[j].key[key_len] = '\0';
                results[j].key_len = key_len;
                offset += key_len;
                
                // Receive value length
                if (batch->client->tls) {
                    ret = rioc_tls_read(batch->client->tls, buffer + offset, sizeof(size_t));
                } else {
                    ret = recv_all(batch->client->fd, buffer + offset, sizeof(size_t));
                }
                if (ret != sizeof(size_t)) {
                    free(buffer);
                    for (size_t k = 0; k <= j; k++) {
                        if (results[k].key) free(results[k].key);
                        if (results[k].value) free(results[k].value);
                    }
                    free(results);
                    free(response_buffer);
                    atomic_store_explicit(&tracker->error, RIOC_ERR_IO, memory_order_release);
                    atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                    return NULL;
                }
                
                memcpy(&value_len, buffer + offset, sizeof(size_t));
                offset += sizeof(size_t);
                
                // Receive value
                if (batch->client->tls) {
                    ret = rioc_tls_read(batch->client->tls, buffer + offset, value_len);
                } else {
                    ret = recv_all(batch->client->fd, buffer + offset, value_len);
                }
                if (ret != value_len) {
                    free(buffer);
                    for (size_t k = 0; k <= j; k++) {
                        if (results[k].key) free(results[k].key);
                        if (results[k].value) free(results[k].value);
                    }
                    free(results);
                    free(response_buffer);
                    atomic_store_explicit(&tracker->error, RIOC_ERR_IO, memory_order_release);
                    atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                    return NULL;
                }
                
                // Allocate and copy value
                results[j].value = malloc(value_len + 1);
                if (!results[j].value) {
                    free(buffer);
                    for (size_t k = 0; k <= j; k++) {
                        if (results[k].key) free(results[k].key);
                        if (results[k].value) free(results[k].value);
                    }
                    free(results);
                    free(response_buffer);
                    atomic_store_explicit(&tracker->error, RIOC_ERR_MEM, memory_order_release);
                    atomic_store_explicit(&tracker->completed, 1, memory_order_release);
                    return NULL;
                }
                
                memcpy(results[j].value, buffer + offset, value_len);
                results[j].value[value_len] = '\0';
                results[j].value_len = value_len;
                offset += value_len;
            }
            
            // Store results in the operation
            op->value_ptr = (char*)results;
            free(buffer);
        }
        
        atomic_store_explicit(&tracker->responses_received, i + 1, memory_order_release);
    }
    
    free(response_buffer);
    atomic_store_explicit(&tracker->error, RIOC_SUCCESS, memory_order_release);
    atomic_store_explicit(&tracker->completed, 1, memory_order_release);
    return NULL;
}

// Wait for batch responses with optional timeout
int rioc_batch_wait(struct rioc_batch_tracker *tracker, int timeout_ms) {
    if (!tracker) {
        return RIOC_ERR_PARAM;
    }
    
    struct timeval start, now;
    gettimeofday(&start, NULL);
    
    while (!atomic_load_explicit(&tracker->completed, memory_order_acquire)) {
        if (timeout_ms > 0) {
            gettimeofday(&now, NULL);
            int64_t elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                               (now.tv_usec - start.tv_usec) / 1000;
            if (elapsed_ms >= timeout_ms) {
                return RIOC_ERR_IO;  // Timeout
            }
            // Short sleep to avoid busy waiting
            struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000};  // 100 microseconds
            nanosleep(&ts, NULL);
        } else {
            // Short sleep to avoid busy waiting
            struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000};  // 100 microseconds
            nanosleep(&ts, NULL);
        }
    }
    
    return atomic_load_explicit(&tracker->error, memory_order_acquire);
}

// Get response for a specific operation in the batch
int rioc_batch_get_response_async(struct rioc_batch_tracker *tracker, size_t index, 
                                char **value, size_t *value_len) {
    if (!tracker || !value || !value_len || index >= tracker->batch->count) {
        return RIOC_ERR_PARAM;
    }
    
    size_t responses_received = atomic_load_explicit(&tracker->responses_received, memory_order_acquire);
    if (index >= responses_received) {
        return RIOC_ERR_IO;  // Response not yet available
    }
    
    struct rioc_batch_op *op = &tracker->batch->ops[index];
    *value = (char *)op->value_ptr;
    *value_len = op->response.value_len;
    
    return op->response.status;
}

// Free the tracker and associated resources
void rioc_batch_tracker_free(struct rioc_batch_tracker *tracker) {
    if (!tracker) {
        return;
    }
    
    if (tracker->response_thread) {
        pthread_join(tracker->response_thread, NULL);
    }
    
    // Free GET operation values and RANGE_QUERY results
    struct rioc_batch *batch = tracker->batch;
    for (size_t i = 0; i < batch->count; i++) {
        struct rioc_batch_op *op = &batch->ops[i];
        if ((op->header.command == RIOC_CMD_GET || op->header.command == RIOC_CMD_ATOMIC_INC_DEC) && op->value_ptr) {
            free((void*)op->value_ptr);
        } else if (op->header.command == RIOC_CMD_RANGE_QUERY && op->value_ptr) {
            // Free range query results
            struct rioc_range_result *results = (struct rioc_range_result *)op->value_ptr;
            size_t count = op->response.value_len;
            
            for (size_t j = 0; j < count; j++) {
                if (results[j].key) {
                    free(results[j].key);
                }
                if (results[j].value) {
                    free(results[j].value);
                }
            }
            
            free(results);
        }
    }
    
    free(tracker);
}

// Single operation functions
int rioc_get(struct rioc_client *client, const char *key, size_t key_len,
             char **value, size_t *value_len) {
    if (!client || !key || !value || !value_len || key_len > RIOC_MAX_KEY_SIZE) {
        return RIOC_ERR_PARAM;
    }
    
    // Send GET operation
    int ret = send_op(client, RIOC_CMD_GET, key, key_len, NULL, 0, 0);
    if (ret != RIOC_SUCCESS) {
        return ret;
    }
    
    // Receive response
    struct rioc_response_header response;
    ret = recv_response(client->fd, &response, value, value_len, client->tls);

    return ret;
}

int rioc_insert(struct rioc_client *client, const char *key, size_t key_len,
                const char *value, size_t value_len, uint64_t timestamp) {
    if (!client || !key || !value || key_len > RIOC_MAX_KEY_SIZE || 
        value_len > RIOC_MAX_VALUE_SIZE) {
        return RIOC_ERR_PARAM;
    }
    
    // Send INSERT operation
    int ret = send_op(client, RIOC_CMD_INSERT, key, key_len, value, value_len, timestamp);
    if (ret != RIOC_SUCCESS) {
        return ret;
    }
    
    // Receive response
    struct rioc_response_header response;
    ret = recv_response(client->fd, &response, NULL, NULL, client->tls);
    
    return ret;
}

int rioc_delete(struct rioc_client *client, const char *key, size_t key_len, uint64_t timestamp) {
    if (!client || !key || key_len > RIOC_MAX_KEY_SIZE) {
        return RIOC_ERR_PARAM;
    }
    
    // Send DELETE operation
    int ret = send_op(client, RIOC_CMD_DELETE, key, key_len, NULL, 0, timestamp);
    if (ret != RIOC_SUCCESS) {
        return ret;
    }
    
    // Receive response
    struct rioc_response_header response;
    ret = recv_response(client->fd, &response, NULL, NULL, client->tls);
    
    return ret;
}

// Range query implementation
int rioc_range_query(struct rioc_client *client, const char *start_key, size_t start_key_len,
                    const char *end_key, size_t end_key_len, 
                    struct rioc_range_result **results, size_t *result_count) {
    if (!client || !start_key || !end_key || !results || !result_count || 
        start_key_len > RIOC_MAX_KEY_SIZE || end_key_len > RIOC_MAX_KEY_SIZE) {
        return RIOC_ERR_PARAM;
    }
    
    // Initialize result count
    *result_count = 0;
    *results = NULL;
    
    // Use vectored I/O for better performance
    struct iovec iovs[4];  // batch_header + op_header + start_key + end_key
    int iov_count = 0;
    
    // Setup batch header
    static __thread struct rioc_batch_header batch_header;
    batch_header.magic = RIOC_MAGIC;
    batch_header.version = RIOC_VERSION;
    batch_header.count = 1;
    batch_header.flags = RIOC_FLAG_PIPELINE | RIOC_FLAG_MORE;
    iovs[iov_count].iov_base = &batch_header;
    iovs[iov_count].iov_len = sizeof(batch_header);
    iov_count++;
    
    // Setup op header
    static __thread struct rioc_op_header op_header;
    op_header.command = RIOC_CMD_RANGE_QUERY;
    op_header.key_len = start_key_len;
    op_header.value_len = end_key_len;
    op_header.timestamp = 0;  // Not used for range query
    iovs[iov_count].iov_base = &op_header;
    iovs[iov_count].iov_len = sizeof(op_header);
    iov_count++;
    
    // Setup start key
    iovs[iov_count].iov_base = (void*)start_key;
    iovs[iov_count].iov_len = start_key_len;
    iov_count++;
    
    // Setup end key
    iovs[iov_count].iov_base = (void*)end_key;
    iovs[iov_count].iov_len = end_key_len;
    iov_count++;
    
    // Calculate total size
    size_t total_size = 0;
    for (int i = 0; i < iov_count; i++) {
        total_size += iovs[i].iov_len;
    }
    
    // Send request
    if (client->tls) {
        // TLS doesn't support writev, so we need to write each buffer separately
        for (int i = 0; i < iov_count; i++) {
            size_t sent = 0;
            while (sent < iovs[i].iov_len) {
                int n = rioc_tls_write(client->tls, 
                                     (char*)iovs[i].iov_base + sent, 
                                     iovs[i].iov_len - sent);
                if (n <= 0) {
                    if (n == -EAGAIN) continue;
                    return RIOC_ERR_IO;
                }
                sent += n;
            }
        }
    } else {
        ssize_t n = writev_all(client->fd, iovs, iov_count);
        if (n < 0 || (size_t)n != total_size) {
            return RIOC_ERR_IO;
        }
    }
    
    // Receive response header
    struct rioc_response_header response;
    ssize_t n;
    if (client->tls) {
        n = rioc_tls_read(client->tls, &response, sizeof(response));
    } else {
        n = rioc_recv(client->fd, &response, sizeof(response), 0);
    }
    if (n != sizeof(response)) {
        return RIOC_ERR_IO;
    }
    
    // Check status
    if ((int32_t)response.status != RIOC_SUCCESS) {
        return (int32_t)response.status;
    }
    
    // Get result count
    size_t count = response.value_len;
    *result_count = count;
    
    if (count == 0) {
        // No results
        return RIOC_SUCCESS;
    }
    
    // Allocate result array
    *results = malloc(count * sizeof(struct rioc_range_result));
    if (!*results) {
        return RIOC_ERR_MEM;
    }
    memset(*results, 0, count * sizeof(struct rioc_range_result));
    
    // Allocate buffer for results
    size_t buffer_size = RIOC_MAX_VALUE_SIZE * 10;  // 10MB max
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        free(*results);
        *results = NULL;
        *result_count = 0;
        return RIOC_ERR_MEM;
    }
    
    // Receive results
    size_t total_received = 0;
    size_t offset = 0;
    
    // Calculate total size to receive
    for (size_t i = 0; i < count; i++) {
        uint16_t key_len;
        size_t value_len;
        
        // Receive key length
        if (client->tls) {
            n = rioc_tls_read(client->tls, buffer + offset, sizeof(uint16_t));
        } else {
            n = rioc_recv(client->fd, buffer + offset, sizeof(uint16_t), 0);
        }
        if (n != sizeof(uint16_t)) {
            free(buffer);
            rioc_free_range_results(*results, i);
            *results = NULL;
            *result_count = 0;
            return RIOC_ERR_IO;
        }
        
        memcpy(&key_len, buffer + offset, sizeof(uint16_t));
        offset += sizeof(uint16_t);
        
        // Receive key
        if (client->tls) {
            n = rioc_tls_read(client->tls, buffer + offset, key_len);
        } else {
            n = rioc_recv(client->fd, buffer + offset, key_len, 0);
        }
        if (n != key_len) {
            free(buffer);
            rioc_free_range_results(*results, i);
            *results = NULL;
            *result_count = 0;
            return RIOC_ERR_IO;
        }
        
        // Allocate and copy key
        (*results)[i].key = malloc(key_len + 1);
        if (!(*results)[i].key) {
            free(buffer);
            rioc_free_range_results(*results, i);
            *results = NULL;
            *result_count = 0;
            return RIOC_ERR_MEM;
        }
        
        memcpy((*results)[i].key, buffer + offset, key_len);
        (*results)[i].key[key_len] = '\0';
        (*results)[i].key_len = key_len;
        offset += key_len;
        
        // Receive value length
        if (client->tls) {
            n = rioc_tls_read(client->tls, buffer + offset, sizeof(size_t));
        } else {
            n = rioc_recv(client->fd, buffer + offset, sizeof(size_t), 0);
        }
        if (n != sizeof(size_t)) {
            free(buffer);
            rioc_free_range_results(*results, i + 1);
            *results = NULL;
            *result_count = 0;
            return RIOC_ERR_IO;
        }
        
        memcpy(&value_len, buffer + offset, sizeof(size_t));
        offset += sizeof(size_t);
        
        // Receive value
        if (client->tls) {
            n = rioc_tls_read(client->tls, buffer + offset, value_len);
        } else {
            n = rioc_recv(client->fd, buffer + offset, value_len, 0);
        }
        if (n != value_len) {
            free(buffer);
            rioc_free_range_results(*results, i + 1);
            *results = NULL;
            *result_count = 0;
            return RIOC_ERR_IO;
        }
        
        // Allocate and copy value
        (*results)[i].value = malloc(value_len + 1);
        if (!(*results)[i].value) {
            free(buffer);
            rioc_free_range_results(*results, i + 1);
            *results = NULL;
            *result_count = 0;
            return RIOC_ERR_MEM;
        }
        
        memcpy((*results)[i].value, buffer + offset, value_len);
        (*results)[i].value[value_len] = '\0';
        (*results)[i].value_len = value_len;
        offset += value_len;
    }
    
    free(buffer);
    return RIOC_SUCCESS;
}

// Free range query results
void rioc_free_range_results(struct rioc_range_result *results, size_t count) {
    if (!results) {
        return;
    }
    
    for (size_t i = 0; i < count; i++) {
        if (results[i].key) {
            free(results[i].key);
        }
        if (results[i].value) {
            free(results[i].value);
        }
    }
    
    free(results);
}

int rioc_atomic_inc_dec(struct rioc_client *client, const char *key, size_t key_len,
                        int64_t increment, uint64_t timestamp, int64_t *result) {
    if (!client || !key || !key_len || !result || key_len > RIOC_MAX_KEY_SIZE) {
        return RIOC_ERR_PARAM;
    }

    // Send the atomic increment/decrement operation
    int ret = send_op(client, RIOC_CMD_ATOMIC_INC_DEC, key, key_len, 
                      (const char*)&increment, sizeof(increment), timestamp);
    if (ret != RIOC_SUCCESS) {
        return ret;
    }

    // Receive response
    struct rioc_response_header response;
    char *value = NULL;
    size_t value_len = 0;

    ret = recv_response(client->fd, &response, &value, &value_len, client->tls);
    if (ret != RIOC_SUCCESS) {
        return ret;
    }

    // Check response status
    if (response.status != RIOC_SUCCESS) {
        if (value) free(value);
        return response.status;
    }

    // Extract result from response
    if (value_len != sizeof(int64_t)) {
        if (value) free(value);
        return RIOC_ERR_PROTO;
    }

    memcpy(result, value, sizeof(int64_t));
    free(value);

    return RIOC_SUCCESS;
}

int rioc_client_connect_with_config(rioc_client_config* config, struct rioc_client** client) {
    if (!config || !config->host || config->port <= 0 || !client) {
        return RIOC_ERR_PARAM;
    }

    // Allocate client structure
    *client = malloc(sizeof(struct rioc_client));
    if (!*client) {
        return RIOC_ERR_MEM;
    }
    memset(*client, 0, sizeof(struct rioc_client));

    // Initialize platform
    int ret = rioc_platform_init();
    if (ret != 0) {
        free(*client);
        return RIOC_ERR_IO;
    }

    // Create socket
    (*client)->fd = rioc_socket_create();
    if ((*client)->fd == RIOC_INVALID_SOCKET) {
        free(*client);
        return RIOC_ERR_IO;
    }

    // Set socket options
    if (rioc_set_socket_options((*client)->fd) != 0) {
        rioc_socket_close((*client)->fd);
        free(*client);
        return RIOC_ERR_IO;
    }

    // Set up address resolution hints
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // IPv4
    hints.ai_socktype = SOCK_STREAM;

    // Convert port to string for getaddrinfo
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", config->port);

    // Resolve address
    ret = getaddrinfo(config->host, port_str, &hints, &result);
    if (ret != 0) {
        rioc_socket_close((*client)->fd);
        free(*client);
        return RIOC_ERR_IO;
    }

    // Connect to server
    if (connect((*client)->fd, result->ai_addr, result->ai_addrlen) < 0) {
        freeaddrinfo(result);
        rioc_socket_close((*client)->fd);
        free(*client);
        return RIOC_ERR_IO;
    }

    freeaddrinfo(result);

    // Initialize TLS if configured
    if (config->tls) {
        (*client)->tls = malloc(sizeof(struct rioc_tls_context));
        if (!(*client)->tls) {
            rioc_socket_close((*client)->fd);
            free(*client);
            return RIOC_ERR_MEM;
        }

        ret = rioc_tls_client_ctx_create((*client)->tls, config->tls);
        if (ret != RIOC_SUCCESS) {
            free((*client)->tls);
            rioc_socket_close((*client)->fd);
            free(*client);
            return ret;
        }

        ret = rioc_tls_client_connect((*client)->tls, (*client)->fd, config->host);
        if (ret != RIOC_SUCCESS) {
            rioc_tls_client_ctx_free((*client)->tls);
            free((*client)->tls);
            rioc_socket_close((*client)->fd);
            free(*client);
            return ret;
        }
    }

    return RIOC_SUCCESS;
}

void rioc_client_disconnect_with_config(struct rioc_client* client) {
    if (client) {
        if (client->tls) {
            rioc_tls_client_ctx_free(client->tls);
            free(client->tls);
        }
        if (client->fd != RIOC_INVALID_SOCKET) {
            rioc_socket_close(client->fd);
        }
        free(client);
    }
}