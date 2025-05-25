#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sched.h>
#include <sys/types.h>
#include <float.h>
#include <errno.h>
#include <inttypes.h>
#include "rioc.h"
#include "rioc_platform.h"

#define MAX_THREADS 64
#define MAX_SAMPLES 1000000
#define WARMUP_COUNT 1000
#define BATCH_SIZE 16

// Platform-specific optimizations
#ifdef __APPLE__
#define USE_DISPATCH_SEMAPHORE 1  // Use Grand Central Dispatch on macOS
#include <dispatch/dispatch.h>
#endif

// Aligned key buffer for better performance
static __thread char key_buffer[32] RIOC_ALIGNED;

// Define operation types
enum op_type {
    OP_INSERT = 0,
    OP_GET = 1,
    OP_DELETE = 2,
    OP_RANGE = 3,
    OP_COUNT = 4
};

struct thread_context {
    int thread_id;
    const char *host;
    int port;
    int num_ops;
    int value_size;
    int verify_values;
    double *latencies[OP_COUNT];
    uint64_t op_count[OP_COUNT];
    uint64_t error_count[OP_COUNT];
    uint64_t base_timestamp;
    uint64_t start_time[OP_COUNT];  // Start time for each operation type
    uint64_t end_time[OP_COUNT];    // End time for each operation type
    uint64_t cumulative_batch_time[OP_COUNT];  // Total time spent in batch execution
    rioc_tls_config *tls;           // TLS configuration (NULL if not using TLS)
    const char *tls_ca_cert_path;   // Path to CA certificate for verifying server
    bool tls_verify_peer;           // Whether to verify server certificate
    const char *tls_verify_hostname; // Hostname to verify in server certificate
} RIOC_ALIGNED;  // Align thread context for better cache performance

struct thread_result {
    double min_latency;
    double max_latency;
    double avg_latency;
    double p50_latency;
    double p95_latency;
    double p99_latency;
    uint64_t op_count;
    uint64_t error_count;
};

static inline uint64_t get_timestamp_ns(void) {
    return rioc_get_timestamp_ns();
}

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

static void calculate_stats(double *latencies, int count, struct thread_result *result) {
    qsort(latencies, count, sizeof(double), compare_doubles);
    
    result->min_latency = latencies[0];
    result->max_latency = latencies[count - 1];
    
    double sum = 0;
    for (int i = 0; i < count; i++) {
        sum += latencies[i];
    }
    result->avg_latency = sum / count;
    
    result->p50_latency = latencies[count * 50 / 100];
    result->p95_latency = latencies[count * 95 / 100];
    result->p99_latency = latencies[count * 99 / 100];
}

// Helper function to pin thread to CPU
static void pin_to_cpu(int cpu) {
    rioc_pin_thread_to_cpu(cpu);
}

static void *worker_thread(void *arg) {
    struct thread_context *ctx = (struct thread_context *)arg;
    struct rioc_client *client = NULL;
    char *value;
    
    // Pin thread to CPU
    pin_to_cpu(ctx->thread_id);
    
    // Initialize client config
    rioc_client_config config = {
        .host = ctx->host,
        .port = ctx->port,
        .timeout_ms = 5000,
        .tls = NULL  // We'll initialize TLS separately
    };

    // Initialize TLS if enabled
    rioc_tls_config tls_config;
    if (ctx->tls) {
        memset(&tls_config, 0, sizeof(tls_config));
        tls_config.cert_path = ctx->tls->cert_path;
        tls_config.key_path = ctx->tls->key_path;
        tls_config.ca_path = ctx->tls->ca_path;
        tls_config.verify_hostname = ctx->tls->verify_hostname;
        tls_config.verify_peer = ctx->tls->verify_peer;
        config.tls = &tls_config;
    }
    
    // Initialize client
    printf("Thread %d: Connecting to %s:%d%s...\n", ctx->thread_id, ctx->host, ctx->port, 
           config.tls ? " (TLS)" : "");
    int ret = rioc_client_connect_with_config(&config, &client);
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Thread %d: Failed to initialize client (error code: %d)\n", 
                ctx->thread_id, ret);
        return NULL;
    }
    printf("Thread %d: Connected successfully\n", ctx->thread_id);
    
    // Pre-allocate value buffer with cache line alignment
    if (posix_memalign((void**)&value, RIOC_CACHE_LINE_SIZE, ctx->value_size) != 0) {
        fprintf(stderr, "Thread %d: Failed to allocate value buffer\n", ctx->thread_id);
        rioc_client_disconnect_with_config(client);
        return NULL;
    }
    memset(value, 'A', ctx->value_size - 1);
    value[ctx->value_size - 1] = '\0';
    
    // Get base timestamp in nanoseconds
    ctx->base_timestamp = get_timestamp_ns();
    
    struct rioc_batch *batch = rioc_batch_create(client);
    
    // Main benchmark loop
    printf("Thread %d: Starting benchmark (%d operations)...\n", ctx->thread_id, ctx->num_ops);
    
    // Insert phase
    // Get fresh base timestamp for insert phase
    ctx->base_timestamp = get_timestamp_ns();

    batch = rioc_batch_create(client);
    if (!batch) {
        fprintf(stderr, "Thread %d: Failed to create batch\n", ctx->thread_id);
        free(value);
        rioc_client_disconnect_with_config(client);
        return NULL;
    }
    
    ctx->start_time[OP_INSERT] = get_timestamp_ns();
    for (int i = 0; i < ctx->num_ops; i++) {
        snprintf(key_buffer, sizeof(key_buffer), "key_%d_%d", ctx->thread_id, i);
        uint64_t timestamp = ctx->base_timestamp + i;
        
        ret = rioc_batch_add_insert(batch, key_buffer, strlen(key_buffer), value, ctx->value_size, timestamp);
        if (ret != RIOC_SUCCESS) {
            fprintf(stderr, "Thread %d: Failed to add insert to batch (error code: %d)\n", ctx->thread_id, ret);
            ctx->error_count[OP_INSERT]++;
            continue;
        }
        
        if ((i + 1) % BATCH_SIZE == 0 || i == ctx->num_ops - 1) {
            uint64_t start_ns = get_timestamp_ns();
            struct rioc_batch_tracker *tracker = rioc_batch_execute_async(batch);
            if (!tracker) {
                fprintf(stderr, "Thread %d: Failed to execute batch\n", ctx->thread_id);
                ctx->error_count[OP_INSERT]++;
                continue;
            }
            
            ret = rioc_batch_wait(tracker, 0);  // Wait without timeout
            uint64_t end_ns = get_timestamp_ns();
            
            if (ret == RIOC_SUCCESS || ret == -EEXIST) {
                double batch_latency = (double)(end_ns - start_ns) / 1000.0;  // Convert ns to μs
                int batch_ops = ((i + 1) % BATCH_SIZE == 0) ? BATCH_SIZE : (i % BATCH_SIZE) + 1;
                // Record per-operation latency within the batch
                double per_op_latency = batch_latency / batch_ops;
                for (int j = 0; j < batch_ops; j++) {
                    ctx->latencies[OP_INSERT][ctx->op_count[OP_INSERT]++] = per_op_latency;
                }
                // Add to cumulative batch time
                ctx->cumulative_batch_time[OP_INSERT] += (end_ns - start_ns);
            } else {
                fprintf(stderr, "Thread %d: Insert batch execute failed (error code: %d)\n", ctx->thread_id, ret);
                ctx->error_count[OP_INSERT]++;
            }
            
            rioc_batch_tracker_free(tracker);
            rioc_batch_free(batch);
            batch = NULL;
            batch = rioc_batch_create(client);
            if (!batch) {
                fprintf(stderr, "Thread %d: Failed to create batch\n", ctx->thread_id);
                free(value);
                rioc_client_disconnect_with_config(client);
                return NULL;
            }
        }
        
        if (i > 0 && i % 10000 == 0) {
            printf("Thread %d: Completed %d inserts\n", ctx->thread_id, i);
        }
    }
    
    ctx->end_time[OP_INSERT] = get_timestamp_ns();
    rioc_batch_free(batch);
    batch = NULL;
    
    // Delay after insert phase
    usleep(200000);
    
    // Get phase
    // No need for timestamp in get phase
    batch = rioc_batch_create(client);
    if (!batch) {
        fprintf(stderr, "Thread %d: Failed to create batch\n", ctx->thread_id);
        free(value);
        rioc_client_disconnect_with_config(client);
        return NULL;
    }
    
    ctx->start_time[OP_GET] = get_timestamp_ns();
    for (int i = 0; i < ctx->num_ops; i++) {
        snprintf(key_buffer, sizeof(key_buffer), "key_%d_%d", ctx->thread_id, i);
        ret = rioc_batch_add_get(batch, key_buffer, strlen(key_buffer));
        if (ret != RIOC_SUCCESS) {
            fprintf(stderr, "Thread %d: Failed to add get to batch (error code: %d)\n", ctx->thread_id, ret);
            ctx->error_count[OP_GET]++;
            continue;
        }
        
        if ((i + 1) % BATCH_SIZE == 0 || i == ctx->num_ops - 1) {
            uint64_t start_ns = get_timestamp_ns();
            struct rioc_batch_tracker *tracker = rioc_batch_execute_async(batch);
            if (!tracker) {
                fprintf(stderr, "Thread %d: Failed to execute batch\n", ctx->thread_id);
                ctx->error_count[OP_GET]++;
                continue;
            }
            
            ret = rioc_batch_wait(tracker, 0);  // Wait without timeout
            uint64_t end_ns = get_timestamp_ns();
            
            if (ret == RIOC_SUCCESS) {
                double batch_latency = (double)(end_ns - start_ns) / 1000.0;  // Convert ns to μs
                int batch_ops = ((i + 1) % BATCH_SIZE == 0) ? BATCH_SIZE : (i % BATCH_SIZE) + 1;
                
                if (ctx->verify_values) {
                    // Verify responses
                    for (size_t j = 0; j < batch->count; j++) {
                        char *get_value;
                        size_t get_value_len;
                        int get_ret = rioc_batch_get_response_async(tracker, j, &get_value, &get_value_len);
                        if (get_ret == RIOC_SUCCESS) {
                            // Value matches what we inserted
                            if (get_value_len != (size_t)ctx->value_size || memcmp(get_value, value, get_value_len) != 0) {
                                fprintf(stderr, "Thread %d: Value mismatch for key_%d_%d (len=%zu expected=%d)\n", 
                                        ctx->thread_id, ctx->thread_id, (int)(i - batch_ops + j + 1),
                                        get_value_len, ctx->value_size);
                                ctx->error_count[OP_GET]++;
                            }
                        } else {
                            fprintf(stderr, "Thread %d: Failed to get response for key_%d_%d (error=%d)\n",
                                    ctx->thread_id, ctx->thread_id, (int)(i - batch_ops + j + 1), get_ret);
                            // Only count as error if not "key not found"
                            if (get_ret != RIOC_ERR_NOENT) {
                                ctx->error_count[OP_GET]++;
                            }
                        }
                    }
                }
                
                // Record per-operation latency within the batch
                double per_op_latency = batch_latency / batch_ops;
                for (int j = 0; j < batch_ops; j++) {
                    ctx->latencies[OP_GET][ctx->op_count[OP_GET]++] = per_op_latency;
                }
                // Add to cumulative batch time
                ctx->cumulative_batch_time[OP_GET] += (end_ns - start_ns);
            } else {
                fprintf(stderr, "Thread %d: Get batch execute failed (error code: %d)\n", ctx->thread_id, ret);
                ctx->error_count[OP_GET]++;
            }
            
            rioc_batch_tracker_free(tracker);
            rioc_batch_free(batch);
            batch = NULL;
            batch = rioc_batch_create(client);
            if (!batch) {
                fprintf(stderr, "Thread %d: Failed to create batch\n", ctx->thread_id);
                free(value);
                rioc_client_disconnect_with_config(client);
                return NULL;
            }
        }
        
        if (i > 0 && i % 10000 == 0) {
            printf("Thread %d: Completed %d gets\n", ctx->thread_id, i);
        }
    }
    
    ctx->end_time[OP_GET] = get_timestamp_ns();
    rioc_batch_free(batch);
    batch = NULL;

    // Delay after get phase
    usleep(200000);
    
    // Delete phase
    // Get fresh base timestamp for delete phase
    ctx->base_timestamp = get_timestamp_ns();

    batch = rioc_batch_create(client);
    if (!batch) {
        fprintf(stderr, "Thread %d: Failed to create batch\n", ctx->thread_id);
        free(value);
        rioc_client_disconnect_with_config(client);
        return NULL;
    }
    
    ctx->start_time[OP_DELETE] = get_timestamp_ns();
    for (int i = 0; i < ctx->num_ops; i++) {
        snprintf(key_buffer, sizeof(key_buffer), "key_%d_%d", ctx->thread_id, i);
        uint64_t timestamp = ctx->base_timestamp + i;
        
        ret = rioc_batch_add_delete(batch, key_buffer, strlen(key_buffer), timestamp);
        if (ret != RIOC_SUCCESS) {
            fprintf(stderr, "Thread %d: Failed to add delete to batch (error code: %d)\n", ctx->thread_id, ret);
            ctx->error_count[OP_DELETE]++;
            continue;
        }
        
        if ((i + 1) % BATCH_SIZE == 0 || i == ctx->num_ops - 1) {
            uint64_t start_ns = get_timestamp_ns();
            struct rioc_batch_tracker *tracker = rioc_batch_execute_async(batch);
            if (!tracker) {
                fprintf(stderr, "Thread %d: Failed to execute batch\n", ctx->thread_id);
                ctx->error_count[OP_DELETE]++;
                continue;
            }
            
            ret = rioc_batch_wait(tracker, 0);  // Wait without timeout
            uint64_t end_ns = get_timestamp_ns();
            
            if (ret == RIOC_SUCCESS) {
                double batch_latency = (double)(end_ns - start_ns) / 1000.0;  // Convert ns to μs
                int batch_ops = ((i + 1) % BATCH_SIZE == 0) ? BATCH_SIZE : (i % BATCH_SIZE) + 1;
                // Record per-operation latency within the batch
                double per_op_latency = batch_latency / batch_ops;
                for (int j = 0; j < batch_ops; j++) {
                    ctx->latencies[OP_DELETE][ctx->op_count[OP_DELETE]++] = per_op_latency;
                }
                // Add to cumulative batch time
                ctx->cumulative_batch_time[OP_DELETE] += (end_ns - start_ns);
            } else {
                fprintf(stderr, "Thread %d: Delete batch execute failed (error code: %d)\n", ctx->thread_id, ret);
                ctx->error_count[OP_DELETE]++;
            }
            
            rioc_batch_tracker_free(tracker);
            rioc_batch_free(batch);
            batch = NULL;
            batch = rioc_batch_create(client);
            if (!batch) {
                fprintf(stderr, "Thread %d: Failed to create batch\n", ctx->thread_id);
                free(value);
                rioc_client_disconnect_with_config(client);
                return NULL;
            }
        }
        
        if (i > 0 && i % 10000 == 0) {
            printf("Thread %d: Completed %d deletes\n", ctx->thread_id, i);
        }
    }
    
    ctx->end_time[OP_DELETE] = get_timestamp_ns();
    rioc_batch_free(batch);
    batch = NULL;

    // Delay after delete phase
    usleep(200000);
    
    // Range query phase
    // First, insert new keys for range query
    batch = rioc_batch_create(client);
    if (!batch) {
        fprintf(stderr, "Thread %d: Failed to create batch\n", ctx->thread_id);
        free(value);
        rioc_client_disconnect_with_config(client);
        return NULL;
    }
    
    // Insert keys with a different prefix for range query
    // Use a smaller number of operations for range query to avoid memory issues
    int range_ops = 100;  // Limit to 100 keys per thread for range queries
    
    for (int i = 0; i < range_ops; i++) {
        char range_key[64];
        // Use a tenant-specific prefix to ensure isolation between threads
        snprintf(range_key, sizeof(range_key), "tenant%d:range_%d", ctx->thread_id, i);
        uint64_t timestamp = get_timestamp_ns() + i;
        
        // Create a value with the key as the value for easy verification
        char range_value[128];
        snprintf(range_value, sizeof(range_value), "value_for_%s", range_key);
        
        ret = rioc_batch_add_insert(batch, range_key, strlen(range_key), range_value, strlen(range_value), timestamp);
        if (ret != RIOC_SUCCESS) {
            fprintf(stderr, "Thread %d: Failed to add range key insert to batch (error code: %d)\n", ctx->thread_id, ret);
            continue;
        }
        
        if ((i + 1) % BATCH_SIZE == 0 || i == range_ops - 1) {
            struct rioc_batch_tracker *tracker = rioc_batch_execute_async(batch);
            if (!tracker) {
                fprintf(stderr, "Thread %d: Failed to execute batch for range key inserts\n", ctx->thread_id);
                continue;
            }
            
            ret = rioc_batch_wait(tracker, 0);  // Wait without timeout
            if (ret != RIOC_SUCCESS && ret != -EEXIST) {
                fprintf(stderr, "Thread %d: Range key insert batch execute failed (error code: %d)\n", ctx->thread_id, ret);
            }
            
            rioc_batch_tracker_free(tracker);
            rioc_batch_free(batch);
            batch = NULL;  // Prevent double free
            batch = rioc_batch_create(client);
            if (!batch) {
                fprintf(stderr, "Thread %d: Failed to create batch\n", ctx->thread_id);
                free(value);
                rioc_client_disconnect_with_config(client);
                return NULL;
            }
        }
        
        // Add a small delay every 10 inserts to avoid overwhelming the server
        if (i > 0 && i % 10 == 0) {
            usleep(10000);  // 10ms delay
        }
    }
    
    if (batch) {
        rioc_batch_free(batch);
        batch = NULL;
    }
    
    // Delay after inserts to ensure processing completes before starting queries
    usleep(500000);  // 500ms delay
    
    // Now perform range queries
    
    ctx->start_time[OP_RANGE] = get_timestamp_ns();
    
    // For range query, we'll use a sliding window approach with smaller ranges
    // Each thread will query a range of keys that it inserted
    int range_size = 10;  // Smaller range size to avoid memory issues
    
    for (int i = 0; i < range_ops; i += range_size) {
        // Calculate start and end keys for this range
        char start_key[64], end_key[64];
        int start_idx = i;
        int end_idx = i + range_size - 1;
        if (end_idx >= range_ops) {
            end_idx = range_ops - 1;
        }
        
        // Use tenant-specific prefix to ensure isolation between threads
        snprintf(start_key, sizeof(start_key), "tenant%d:range_%d", ctx->thread_id, start_idx);
        snprintf(end_key, sizeof(end_key), "tenant%d:range_%d", ctx->thread_id, end_idx);
        
        // Create a new batch for each range query
        batch = rioc_batch_create(client);
        if (!batch) {
            fprintf(stderr, "Thread %d: Failed to create batch for range query\n", ctx->thread_id);
            continue;
        }
        
        ret = rioc_batch_add_range_query(batch, start_key, strlen(start_key), end_key, strlen(end_key));
        if (ret != RIOC_SUCCESS) {
            fprintf(stderr, "Thread %d: Failed to add range query to batch (error code: %d)\n", ctx->thread_id, ret);
            ctx->error_count[OP_RANGE]++;
            rioc_batch_free(batch);
            continue;
        }
        
        // Make sure the batch has operations before executing
        if (batch->count == 0) {
            fprintf(stderr, "Thread %d: Empty batch, skipping execution\n", ctx->thread_id);
            rioc_batch_free(batch);
            continue;
        }
        
        // Try the range query with retries for transient errors
        int max_retries = 3;
        int retry_count = 0;
        bool operation_succeeded = false;
        
        while (retry_count < max_retries && !operation_succeeded) {
            if (retry_count > 0) {
                // Small delay before retry (exponential backoff)
                usleep(1000 * (1 << retry_count)); // 2ms, 4ms, 8ms
            }
            
            uint64_t start_ns = get_timestamp_ns();
            struct rioc_batch_tracker *tracker = rioc_batch_execute_async(batch);
            
            if (!tracker) {
                fprintf(stderr, "Thread %d: Failed to execute batch (retry %d)\n", 
                        ctx->thread_id, retry_count);
                retry_count++;
                continue;
            }
            
            ret = rioc_batch_wait(tracker, 0);  // Wait without timeout
            uint64_t end_ns = get_timestamp_ns();
            
            if (ret == RIOC_SUCCESS) {
                double batch_latency = (double)(end_ns - start_ns) / 1000.0;  // Convert ns to μs
                
                // Record per-operation latency for the range query
                ctx->latencies[OP_RANGE][ctx->op_count[OP_RANGE]++] = batch_latency;
                
                // Add to cumulative batch time
                ctx->cumulative_batch_time[OP_RANGE] += (end_ns - start_ns);
                
                // Debug output for range query results
                struct rioc_batch_op *op = &batch->ops[0];
                if (op->response.status == RIOC_SUCCESS) {
                    operation_succeeded = true;
                    
                    if (op->response.value_len > 0) {
                        struct rioc_range_result *results = (struct rioc_range_result *)op->value_ptr;
                        size_t result_count = op->response.value_len;
                        
                        // Verify range query results if verification is enabled
                        if (ctx->verify_values) {
                            // Verify each result without printing
                            for (size_t j = 0; j < result_count; j++) {
                                char expected_value[128];
                                snprintf(expected_value, sizeof(expected_value), "value_for_%s", results[j].key);
                                
                                if (strcmp(results[j].value, expected_value) != 0) {
                                    // Only increment error count, no printing
                                    ctx->error_count[OP_RANGE]++;
                                }
                            }
                        }
                    }
                } else if (op->response.status == RIOC_ERR_NOENT) {
                    // This is not really an error, just empty results
                    operation_succeeded = true;
                } else {
                    // For transient errors, we might retry
                    fprintf(stderr, "Thread %d: Range query operation failed (status: %d, retry %d)\n", 
                           ctx->thread_id, op->response.status, retry_count);
                    retry_count++;
                }
            } else {
                // Batch execution failed
                fprintf(stderr, "Thread %d: Range query batch execute failed (error code: %d, retry %d)\n", 
                       ctx->thread_id, ret, retry_count);
                retry_count++;
            }
            
            rioc_batch_tracker_free(tracker);
            
            // Break the loop if operation succeeded
            if (operation_succeeded) {
                break;
            }
        }
        
        // If all retries failed, count as error
        if (!operation_succeeded) {
            ctx->error_count[OP_RANGE]++;
        }
        
        rioc_batch_free(batch);
        batch = NULL;  // Prevent double free
        
        // Add a small delay between range queries to avoid overloading the server
        usleep(50000);  // 50ms delay
        
        if (i > 0 && i % 50 == 0) {
            printf("Thread %d: Completed %d range queries\n", ctx->thread_id, i / range_size);
        }
    }
    
    ctx->end_time[OP_RANGE] = get_timestamp_ns();

    printf("Thread %d: Benchmark complete.\n", ctx->thread_id);
    printf("  Inserts:  ops=%"PRIu64", errors=%"PRIu64"\n", 
           ctx->op_count[OP_INSERT], ctx->error_count[OP_INSERT]);
    printf("  Gets:     ops=%"PRIu64", errors=%"PRIu64"\n", 
           ctx->op_count[OP_GET], ctx->error_count[OP_GET]);
    printf("  Deletes:  ops=%"PRIu64", errors=%"PRIu64"\n", 
           ctx->op_count[OP_DELETE], ctx->error_count[OP_DELETE]);
    printf("  Ranges:   ops=%"PRIu64", errors=%"PRIu64"\n", 
           ctx->op_count[OP_RANGE], ctx->error_count[OP_RANGE]);
    
    free(value);
    rioc_client_disconnect_with_config(client);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <host> <port> <num_threads> [value_size] [num_ops] [verify] "
                "[tls_cert_path] [tls_key_path] [tls_ca_path]\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    int num_threads = atoi(argv[3]);
    int value_size = (argc > 4) ? atoi(argv[4]) : 100;
    int num_ops = (argc > 5) ? atoi(argv[5]) : 10000;
    int verify = (argc > 6) ? atoi(argv[6]) : 0;
    const char *tls_cert_path = (argc > 7) ? argv[7] : NULL;
    const char *tls_key_path = (argc > 8) ? argv[8] : NULL;
    const char *tls_ca_path = (argc > 9) ? argv[9] : NULL;

    // Validate parameters
    if (num_threads < 1 || num_threads > MAX_THREADS) {
        fprintf(stderr, "Number of threads must be between 1 and %d\n", MAX_THREADS);
        return 1;
    }

    // Validate TLS configuration
    if ((tls_cert_path && !tls_key_path) || (!tls_cert_path && tls_key_path)) {
        fprintf(stderr, "Both TLS certificate and key paths must be provided for TLS mode\n");
        return 1;
    }

    // Initialize TLS config if certificates are provided
    rioc_tls_config *tls_config = NULL;
    if (tls_cert_path) {
        tls_config = malloc(sizeof(rioc_tls_config));
        if (!tls_config) {
            fprintf(stderr, "Failed to allocate TLS config\n");
            return 1;
        }
        memset(tls_config, 0, sizeof(rioc_tls_config));
        tls_config->cert_path = tls_cert_path;
        tls_config->key_path = tls_key_path;
        tls_config->ca_path = tls_ca_path;
        tls_config->verify_hostname = host;  // Use provided hostname for verification
        tls_config->verify_peer = tls_ca_path != NULL;  // Enable peer verification if CA provided
    }

    // Initialize thread contexts
    struct thread_context contexts[MAX_THREADS];
    struct thread_result results[MAX_THREADS][OP_COUNT];  // Separate results for each operation type
    pthread_t threads[MAX_THREADS];
    int threads_started = 0;

    // Initialize contexts
    for (int i = 0; i < num_threads; i++) {
        contexts[i].thread_id = i;
        contexts[i].host = host;
        contexts[i].port = port;
        contexts[i].num_ops = num_ops;
        contexts[i].value_size = value_size;
        contexts[i].verify_values = verify;
        contexts[i].tls = tls_config;  // Use the same TLS config for all threads
        contexts[i].tls_ca_cert_path = tls_ca_path;
        contexts[i].tls_verify_peer = tls_config ? tls_config->verify_peer : false;
        contexts[i].tls_verify_hostname = tls_config ? tls_config->verify_hostname : NULL;
        
        // Initialize cumulative batch times
        for (int j = 0; j < OP_COUNT; j++) {
            contexts[i].cumulative_batch_time[j] = 0;
        }

        // Initialize latency arrays to NULL
        for (int j = 0; j < OP_COUNT; j++) {
            contexts[i].latencies[j] = NULL;
        }

        // Allocate latency arrays
        for (int j = 0; j < OP_COUNT; j++) {
            contexts[i].latencies[j] = malloc(sizeof(double) * num_ops);
            if (!contexts[i].latencies[j]) {
                fprintf(stderr, "Failed to allocate latency array for thread %d\n", i);
                // Cleanup previously allocated arrays
                for (int k = 0; k < j; k++) {
                    if (contexts[i].latencies[k]) {
                        free(contexts[i].latencies[k]);
                        contexts[i].latencies[k] = NULL;
                    }
                }
                for (int k = 0; k < i; k++) {
                    for (int l = 0; l < OP_COUNT; l++) {
                        if (contexts[k].latencies[l]) {
                            free(contexts[k].latencies[l]);
                            contexts[k].latencies[l] = NULL;
                        }
                    }
                }
                if (tls_config) {
                    free(tls_config);
                }
                return 1;
            }
            contexts[i].op_count[j] = 0;
            contexts[i].error_count[j] = 0;
        }
    }

    // Print configuration
    printf("\nBenchmark Configuration:\n");
    printf("  Host:            %s\n", host);
    printf("  Port:            %d\n", port);
    printf("  Threads:         %d\n", num_threads);
    printf("  Value size:      %d bytes\n", value_size);
    printf("  Operations:      %d per thread\n", num_ops);
    printf("  Value verify:    %s\n", verify ? "enabled" : "disabled");
    printf("  TLS:             %s\n", tls_config ? "enabled" : "disabled");
    if (tls_config) {
        printf("  Client cert:     %s\n", tls_cert_path);
        printf("  CA cert:         %s\n", tls_ca_path ? tls_ca_path : "none");
        printf("  Peer verify:     %s\n", tls_config->verify_peer ? "enabled" : "disabled");
    }
    printf("\n");

    // Initialize and start threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, &contexts[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            // Cleanup previously allocated arrays
            for (int j = 0; j < OP_COUNT; j++) {
                if (contexts[i].latencies[j]) {
                    free(contexts[i].latencies[j]);
                    contexts[i].latencies[j] = NULL;
                }
            }
            break;
        }
        threads_started++;
    }
    
    if (threads_started == 0) {
        fprintf(stderr, "Failed to start any threads\n");
        // Cleanup all allocated arrays
        for (int i = 0; i < num_threads; i++) {
            for (int j = 0; j < OP_COUNT; j++) {
                if (contexts[i].latencies[j]) {
                    free(contexts[i].latencies[j]);
                    contexts[i].latencies[j] = NULL;
                }
            }
        }
        if (tls_config) {
            free(tls_config);
        }
        return 1;
    }
    
    // Wait for threads to complete
    for (int i = 0; i < threads_started; i++) {
        pthread_join(threads[i], NULL);
        for (int op = 0; op < OP_COUNT; op++) {
            if (contexts[i].op_count[op] > 0) {
                calculate_stats(contexts[i].latencies[op], contexts[i].op_count[op], &results[i][op]);
            }
            if (contexts[i].latencies[op]) {
                free(contexts[i].latencies[op]);
                contexts[i].latencies[op] = NULL;
            }
        }
    }
    
    // Calculate aggregate statistics for each operation type
    const char *op_names[] = {"INSERT", "GET", "DELETE", "RANGE"};
    
    printf("\nBenchmark Results:\n");
    printf("================\n");
    printf("Configuration:\n");
    printf("  Threads started:   %d\n", threads_started);
    printf("  Ops per thread:    %d\n", num_ops);
    printf("  Value size:        %d bytes\n", value_size);
    printf("  Value verify:      %s\n", verify ? "enabled" : "disabled");
    
    for (int op = 0; op < OP_COUNT; op++) {
        uint64_t total_ops = 0;
        uint64_t total_errors = 0;
        double min_latency = DBL_MAX;
        double max_latency = 0;
        double sum_latency = 0;
        double sum_p50 = 0;
        double sum_p95 = 0;
        double sum_p99 = 0;
        int threads_with_ops = 0;
        uint64_t max_batch_time = 0;  // Track max batch time instead of total
        
        for (int i = 0; i < threads_started; i++) {
            total_ops += contexts[i].op_count[op];
            total_errors += contexts[i].error_count[op];
            // Take max instead of sum since operations are running in parallel
            if (contexts[i].cumulative_batch_time[op] > max_batch_time) {
                max_batch_time = contexts[i].cumulative_batch_time[op];
            }
            if (contexts[i].op_count[op] > 0) {
                if (results[i][op].min_latency < min_latency) min_latency = results[i][op].min_latency;
                if (results[i][op].max_latency > max_latency) max_latency = results[i][op].max_latency;
                sum_latency += results[i][op].avg_latency;
                sum_p50 += results[i][op].p50_latency;
                sum_p95 += results[i][op].p95_latency;
                sum_p99 += results[i][op].p99_latency;
                threads_with_ops++;
            }
        }
        
        if (threads_with_ops == 0) {
            printf("\n%s: No successful operations\n", op_names[op]);
            continue;
        }
        
        double avg_latency = sum_latency / threads_with_ops;
        double avg_p50 = sum_p50 / threads_with_ops;
        double avg_p95 = sum_p95 / threads_with_ops;
        double avg_p99 = sum_p99 / threads_with_ops;
        double elapsed_seconds = (double)max_batch_time / 1000000000.0;  // Convert ns to seconds
        double ops_per_sec = (double)total_ops / elapsed_seconds;
        
        printf("\n%s Performance:\n", op_names[op]);
        printf("  Total operations: %"PRIu64"\n", total_ops);
        printf("  Total errors:     %"PRIu64"\n", total_errors);
        double total_time_ms = (double)max_batch_time / 1000000.0;  // Convert ns to ms
        printf("  Total time:       %.3f ms\n", total_time_ms);
        printf("  Batch size:       %d\n", BATCH_SIZE);
        printf("  Operations/sec:   %.2f\n", ops_per_sec);
        printf("  Latency (microseconds):\n");
        printf("    Min:             %.3f\n", min_latency);
        printf("    Max:             %.3f\n", max_latency);
        printf("    Average:         %.3f\n", avg_latency);
        printf("    P50 (median):    %.3f\n", avg_p50);
        printf("    P95:             %.3f\n", avg_p95);
        printf("    P99:             %.3f\n", avg_p99);
    }
    
    // Cleanup
    for (int i = 0; i < threads_started; i++) {
        for (int j = 0; j < OP_COUNT; j++) {
            if (contexts[i].latencies[j]) {
                free(contexts[i].latencies[j]);
                contexts[i].latencies[j] = NULL;
            }
        }
    }
    if (tls_config) {
        free(tls_config);
    }

    return 0;
} 