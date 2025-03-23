#ifndef RIOC_H
#define RIOC_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/uio.h>
#include <stdbool.h>

// Forward declarations
struct rioc_tls_context;

// Error codes
#define RIOC_SUCCESS     0
#define RIOC_ERR_PARAM  -1
#define RIOC_ERR_MEM    -2
#define RIOC_ERR_IO     -3
#define RIOC_ERR_PROTO  -4
#define RIOC_ERR_DEVICE -5
#define RIOC_ERR_NOENT  -6
#define RIOC_ERR_BUSY   -7
#define RIOC_ERR_OVERFLOW -8

// Protocol constants
#define RIOC_VERSION    2
#define RIOC_MAGIC      0x524F4943  // "RIOC"
#define RIOC_MAX_KEY_SIZE    512
#define RIOC_MAX_VALUE_SIZE  102400  // 100KB

// Use optimized settings for all platforms
#define RIOC_MAX_BATCH_SIZE  128   // Larger batches for better performance
#define RIOC_TCP_BUFFER_SIZE (1024 * 1024)  // 1MB socket buffers

// Ring buffer size (must be power of 2)
#define RIOC_RING_SIZE (32 * 1024)  // 32KB ring buffer
#define RIOC_RING_MASK (RIOC_RING_SIZE - 1)

// Maximum number of IOVs per operation
#define RIOC_MAX_IOV 3  // header + key + value

// Cache line size
#define RIOC_CACHE_LINE_SIZE 128
#define RIOC_ALIGNED __attribute__((aligned(RIOC_CACHE_LINE_SIZE)))

// Branch prediction hints
#define RIOC_UNLIKELY(x) __builtin_expect(!!(x), 0)

// Prefetch hints
#define RIOC_PREFETCH(x) __builtin_prefetch(x)

// Commands
#define RIOC_CMD_GET            1
#define RIOC_CMD_INSERT         2
#define RIOC_CMD_DELETE         3
#define RIOC_CMD_PARTIAL_UPDATE 4
#define RIOC_CMD_BATCH          5
#define RIOC_CMD_RANGE_QUERY    6
#define RIOC_CMD_ATOMIC_INC_DEC 7

// Flags
#define RIOC_FLAG_ERROR    0x1
#define RIOC_FLAG_PIPELINE 0x2
#define RIOC_FLAG_MORE     0x4

// TLS configuration
typedef struct rioc_tls_config {
    const char* cert_path;        // Server cert or client CA cert path
    const char* key_path;         // Server private key path (server only)
    const char* ca_path;          // CA certificate path (client only)
    const char* verify_hostname;  // Hostname to verify (client only)
    bool verify_peer;            // Enable certificate verification
} rioc_tls_config;

// Server configuration
typedef struct rioc_server_config {
    const char* mount_path;      // Path to the block device
    uint32_t max_connections;    // Maximum number of concurrent connections
    uint32_t port;              // Port to listen on
    rioc_tls_config* tls;       // Optional TLS config, NULL for no TLS
} rioc_server_config;

// Client configuration
typedef struct rioc_client_config {
    const char* host;           // Server hostname
    uint32_t port;             // Server port
    uint32_t timeout_ms;       // Operation timeout in milliseconds
    rioc_tls_config* tls;      // Optional TLS config, NULL for no TLS
} rioc_client_config;

// Optimized operation header
struct rioc_op_header {
    uint16_t command;    // Operation type
    uint16_t key_len;    // Key length
    uint32_t value_len;  // Value length
    uint64_t timestamp;  // Operation timestamp
};

// Batch header
struct rioc_batch_header {
    uint32_t magic;      // Protocol magic
    uint16_t version;    // Protocol version
    uint16_t count;      // Number of operations in batch
    uint32_t flags;      // Operation flags
};

// Response header
struct rioc_response_header {
    uint32_t status;     // Operation status
    uint32_t value_len;  // Length of value (for GET)
};

// Client context
struct rioc_client {
    int fd;             // Socket file descriptor
    uint64_t sequence;  // Operation sequence number
    struct rioc_tls_context *tls; // TLS context, NULL if not using TLS
};

// Server context
struct rioc_server {
    int device_fd;           // Device file descriptor
    int server_fd;          // Server socket
    int num_workers;        // Number of worker threads
    pthread_t *worker_threads; // Worker thread handles
    volatile int running;   // Server running flag
    struct rioc_tls_context *tls; // TLS context, NULL if not using TLS
};

// Ring buffer structure
struct rioc_ring {
    char *buffer;
    size_t head;
    size_t tail;
} RIOC_ALIGNED;

// Response structure for GET operations
struct rioc_response {
    char *value;         // Value buffer
    size_t value_len;    // Value length
    int status;         // Response status
} RIOC_ALIGNED;

// Optimized batch operation structure
struct rioc_batch_op {
    struct rioc_op_header header;
    char key[RIOC_MAX_KEY_SIZE];
    char *value_ptr;  // Pointer to value data (non-const since we modify it for GET responses)
    size_t value_offset;    // Offset in batch value buffer
    struct rioc_response response;
    struct iovec iov[RIOC_MAX_IOV];  // Pre-allocated IOVs
} __attribute__((aligned(RIOC_CACHE_LINE_SIZE)));

// Optimized batch structure
struct rioc_batch {
    struct rioc_client *client;
    struct rioc_batch_header batch_header;
    struct rioc_batch_op ops[RIOC_MAX_BATCH_SIZE];
    char *value_buffer;     // Single buffer for all values
    size_t value_buffer_size;
    size_t value_buffer_used;
    size_t count;
    size_t iov_count;
    uint32_t flags;
} __attribute__((aligned(RIOC_CACHE_LINE_SIZE)));

// Response tracking structure for non-blocking batch execution
struct rioc_batch_tracker {
    struct rioc_batch *batch;
    pthread_t response_thread;
    atomic_int completed;
    atomic_int error;
    atomic_size_t responses_received;
    char pad[RIOC_CACHE_LINE_SIZE];  // Padding to prevent false sharing
} __attribute__((aligned(RIOC_CACHE_LINE_SIZE)));

// Ring buffer for response tracking
struct rioc_ring_buffer {
    char *buffer;
    atomic_size_t head;
    atomic_size_t tail;
    size_t size;
    size_t mask;
} RIOC_ALIGNED;

// Range query result structure
struct rioc_range_result {
    char *key;
    size_t key_len;
    char *value;
    size_t value_len;
};

// Forward declare TLS context for internal use
struct rioc_tls_context;

// TLS cleanup functions
void rioc_tls_cleanup_ssl(struct rioc_tls_context *tls_ctx);
void rioc_tls_server_ctx_free(struct rioc_tls_context *tls_ctx);
void rioc_tls_client_ctx_free(struct rioc_tls_context *tls_ctx);

// Legacy API (to be deprecated)
int rioc_client_init(struct rioc_client *client, const char *host, int port);
int rioc_client_close(struct rioc_client *client);
int rioc_server_init(struct rioc_server *server, const char *device_path);
int rioc_server_start(struct rioc_server *server, int port, int num_workers);
int rioc_server_stop(struct rioc_server *server);
int rioc_server_close(struct rioc_server *server);
int rioc_server_set_tls(struct rioc_server *server, rioc_tls_config *config);

// API with TLS support
int rioc_server_start_with_config(rioc_server_config* config);
void rioc_server_stop_with_config(void);
int rioc_client_connect_with_config(rioc_client_config* config, struct rioc_client** client);
void rioc_client_disconnect_with_config(struct rioc_client* client);

// Basic operations
int rioc_get(struct rioc_client *client, const char *key, size_t key_len, 
             char **value, size_t *value_len);
int rioc_insert(struct rioc_client *client, const char *key, size_t key_len,
                const char *value, size_t value_len, uint64_t timestamp);
int rioc_delete(struct rioc_client *client, const char *key, size_t key_len,
                uint64_t timestamp);
int rioc_range_query(struct rioc_client *client, const char *start_key, size_t start_key_len,
                    const char *end_key, size_t end_key_len, 
                    struct rioc_range_result **results, size_t *result_count);
void rioc_free_range_results(struct rioc_range_result *results, size_t count);
int rioc_atomic_inc_dec(struct rioc_client *client, const char *key, size_t key_len,
                        int64_t increment, uint64_t timestamp, int64_t *result);

// Batch and pipeline operations
struct rioc_batch;

struct rioc_batch *rioc_batch_create(struct rioc_client *client);
int rioc_batch_add_get(struct rioc_batch *batch, const char *key, size_t key_len);
int rioc_batch_add_insert(struct rioc_batch *batch, const char *key, size_t key_len,
                         const char *value, size_t value_len, uint64_t timestamp);
int rioc_batch_add_delete(struct rioc_batch *batch, const char *key, size_t key_len,
                         uint64_t timestamp);
int rioc_batch_add_atomic_inc_dec(struct rioc_batch *batch, const char *key, size_t key_len,
                                 int64_t increment, uint64_t timestamp);
void rioc_batch_free(struct rioc_batch *batch);

// Non-blocking batch operations
struct rioc_batch_tracker* rioc_batch_execute_async(struct rioc_batch *batch);
int rioc_batch_wait(struct rioc_batch_tracker *tracker, int timeout_ms);
int rioc_batch_get_response_async(struct rioc_batch_tracker *tracker, size_t index, 
                                char **value, size_t *value_len);
void rioc_batch_tracker_free(struct rioc_batch_tracker *tracker);
int rioc_batch_add_range_query(struct rioc_batch *batch, 
                              const char *start_key, size_t start_key_len,
                              const char *end_key, size_t end_key_len);

#endif // RIOC_H 