#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <errno.h>
#include "rioc.h"
#include "rioc_platform.h"

// Platform-specific sleep function
static void platform_usleep(unsigned int usec) {
    rioc_sleep_us(usec);
}

// Platform-specific high-resolution timer
static uint64_t get_current_timestamp_ns(void) {
    return rioc_get_timestamp_ns();
}

// Helper function to get time difference in microseconds
static uint64_t time_diff_us(struct timespec start, struct timespec end) {
    uint64_t start_us = (uint64_t)start.tv_sec * 1000000 + start.tv_nsec / 1000;
    uint64_t end_us = (uint64_t)end.tv_sec * 1000000 + end.tv_nsec / 1000;
    return end_us - start_us;
}

// Warm up function
static void warmup_connection(struct rioc_client *client) {
    const char *key = "warmup_key";
    const char *value = "warmup_value";
    char *retrieved_value = NULL;
    size_t retrieved_len = 0;
    
    // Do 10 quick operations to warm up the connection
    for (int i = 0; i < 10; i++) {
        rioc_insert(client, key, strlen(key), value, strlen(value), get_current_timestamp_ns());
        rioc_get(client, key, strlen(key), &retrieved_value, &retrieved_len);
        if (retrieved_value) {
            free(retrieved_value);
            retrieved_value = NULL;
        }
        rioc_delete(client, key, strlen(key), get_current_timestamp_ns());
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    struct rioc_client *client = NULL;
    int ret;
    char *retrieved_value = NULL;
    size_t retrieved_len = 0;
    struct timespec start_time, end_time;

    // Initialize TLS config
    rioc_tls_config tls_config = {
        .ca_path = "../certs/ca.crt",  // CA certificate for verification
        .cert_path = "../certs/client.crt",  // Client certificate
        .key_path = "../certs/client.key",  // Client private key
        .verify_peer = true,          // Verify server certificate
        .verify_hostname = host       // Verify hostname
    };

    // Initialize client config with TLS
    rioc_client_config config = {
        .host = host,
        .port = port,
        .timeout_ms = 5000,
        .tls = &tls_config
    };

    // Initialize client
    printf("Connecting to %s:%d with TLS...\n", host, port);
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_client_connect_with_config(&config, &client);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Failed to initialize client (error code: %d)\n", ret);
        return 1;
    }
    printf("Connected successfully with TLS in %"PRIu64" us\n", time_diff_us(start_time, end_time));

    // Warm up connection
    printf("\nWarming up connection...\n");
    warmup_connection(client);
    printf("Warmup complete\n\n");

    // Test data
    const char *key = "test_key";
    const char *initial_value = "initial value";
    const char *updated_value = "updated value";

    // Get initial timestamp
    uint64_t timestamp = get_current_timestamp_ns();
    printf("1. Inserting record with timestamp %"PRIu64"\n", timestamp);
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_insert(client, key, strlen(key), initial_value, strlen(initial_value), timestamp);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    if (ret != RIOC_SUCCESS && ret != -EEXIST) {
        fprintf(stderr, "Insert failed with error code %d\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    printf("Insert successful in %"PRIu64" us\n", time_diff_us(start_time, end_time));

    // Sleep briefly to ensure timestamp increases
    platform_usleep(1000);

    printf("\n2. Getting record\n");
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_get(client, key, strlen(key), &retrieved_value, &retrieved_len);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    if (ret == RIOC_ERR_NOENT) {
        printf("Key not found (took %"PRIu64" us)\n", time_diff_us(start_time, end_time));
    } else if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Get failed with error code %d\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    } else {
        printf("Get successful in %"PRIu64" us, value length: %zu, value: ", time_diff_us(start_time, end_time), retrieved_len);
        fwrite(retrieved_value, 1, retrieved_len, stdout);
        printf("\n");
    }

    // Sleep briefly to ensure timestamp increases
    platform_usleep(1000);

    // Full update
    timestamp = get_current_timestamp_ns();
    printf("\n3. Updating record with timestamp %"PRIu64"\n", timestamp);
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_insert(client, key, strlen(key), updated_value, strlen(updated_value), timestamp);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    if (ret != RIOC_SUCCESS && ret != -EEXIST) {
        fprintf(stderr, "Update failed with error code %d\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    printf("Update successful in %"PRIu64" us\n", time_diff_us(start_time, end_time));

    // Sleep briefly to ensure timestamp increases
    platform_usleep(1000);

    printf("\n4. Getting updated record\n");
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_get(client, key, strlen(key), &retrieved_value, &retrieved_len);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    if (ret == RIOC_ERR_NOENT) {
        printf("Key not found (took %"PRIu64" us)\n", time_diff_us(start_time, end_time));
    } else if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Get failed with error code %d\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    } else {
        printf("Get successful in %"PRIu64" us, value length: %zu, value: ", time_diff_us(start_time, end_time), retrieved_len);
        fwrite(retrieved_value, 1, retrieved_len, stdout);
        printf("\n");
    }

    // Sleep briefly to ensure timestamp increases
    platform_usleep(1000);

    // Test delete
    printf("\n5. Deleting record\n");
    timestamp = get_current_timestamp_ns();
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_delete(client, key, strlen(key), timestamp);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Delete failed with error code %d\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    printf("Delete successful in %"PRIu64" us\n", time_diff_us(start_time, end_time));

    // Test get after delete
    printf("\n6. Getting deleted record\n");
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_get(client, key, strlen(key), &retrieved_value, &retrieved_len);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    if (ret == RIOC_ERR_NOENT) {
        printf("Get after delete correctly returned RIOC_ERR_NOENT in %"PRIu64" us\n", time_diff_us(start_time, end_time));
    } else if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Get after delete failed with unexpected error code: %d\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    } else {
        // This shouldn't happen - key should be deleted
        fprintf(stderr, "Get after delete unexpectedly succeeded in %"PRIu64" us\n", time_diff_us(start_time, end_time));
        rioc_client_disconnect_with_config(client);
        return 1;
    }

    // Test range query
    printf("\n7. Testing range query\n");
    
    // Insert multiple records for range query
    const char *keys[] = {"range_a", "range_b", "range_c", "range_d", "range_e"};
    const char *values[] = {"value_a", "value_b", "value_c", "value_d", "value_e"};
    int num_records = sizeof(keys) / sizeof(keys[0]);
    
    printf("Inserting %d records for range query test\n", num_records);
    for (int i = 0; i < num_records; i++) {
        timestamp = get_current_timestamp_ns() + i;
        ret = rioc_insert(client, keys[i], strlen(keys[i]), values[i], strlen(values[i]), timestamp);
        if (ret != RIOC_SUCCESS) {
            fprintf(stderr, "Failed to insert record %s for range query test (error code: %d)\n", keys[i], ret);
            rioc_client_disconnect_with_config(client);
            return 1;
        }
        platform_usleep(1000);  // Small delay between inserts
    }
    
    // Perform range query
    struct rioc_range_result *results = NULL;
    size_t result_count = 0;
    
    printf("Performing range query from 'range_b' to 'range_d'\n");
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_range_query(client, "range_b", strlen("range_b"), "range_d", strlen("range_d"), &results, &result_count);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Range query failed with error code %d\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    printf("Range query successful in %"PRIu64" us, found %zu results:\n", time_diff_us(start_time, end_time), result_count);
    
    // Verify results
    for (size_t i = 0; i < result_count; i++) {
        printf("  Result %zu: key='%s', value='%s'\n", i, results[i].key, results[i].value);
    }
    
    // Free results
    rioc_free_range_results(results, result_count);
    
    // Test batch range query
    printf("\n8. Testing batch range query\n");
    
    // Create batch
    struct rioc_batch *batch = rioc_batch_create(client);
    if (!batch) {
        fprintf(stderr, "Failed to create batch\n");
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    // Add range query to batch
    ret = rioc_batch_add_range_query(batch, "range_a", strlen("range_a"), "range_e", strlen("range_e"));
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Failed to add range query to batch (error code: %d)\n", ret);
        rioc_batch_free(batch);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    // Execute batch
    printf("Executing batch with range query\n");
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    struct rioc_batch_tracker *tracker = rioc_batch_execute_async(batch);
    if (!tracker) {
        fprintf(stderr, "Failed to execute batch\n");
        rioc_batch_free(batch);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    // Wait for batch to complete
    ret = rioc_batch_wait(tracker, 0);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Batch execution failed (error code: %d)\n", ret);
        rioc_batch_tracker_free(tracker);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    printf("Batch range query completed in %"PRIu64" us\n", time_diff_us(start_time, end_time));
    
    // Get range query results from batch
    struct rioc_batch_op *op = &batch->ops[0];
    if (op->response.status == RIOC_SUCCESS && op->response.value_len > 0) {
        struct rioc_range_result *batch_results = (struct rioc_range_result *)op->value_ptr;
        size_t batch_result_count = op->response.value_len;
        
        printf("Batch range query found %zu results:\n", batch_result_count);
        for (size_t i = 0; i < batch_result_count; i++) {
            printf("  Result %zu: key='%s', value='%s'\n", i, batch_results[i].key, batch_results[i].value);
        }
    } else {
        printf("Batch range query returned no results or failed (status: %d)\n", op->response.status);
    }
    
    // Free batch resources
    rioc_batch_tracker_free(tracker);

    // Test atomic increment/decrement
    printf("\n9. Testing atomic increment/decrement\n");
    
    // Test initial increment (creates counter)
    const char *counter_key = "test_counter";
    int64_t increment = 5;
    int64_t result;
    
    printf("Creating counter with initial value 5\n");
    timestamp = get_current_timestamp_ns();
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_atomic_inc_dec(client, counter_key, strlen(counter_key), increment, timestamp, &result);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Failed to create counter (error code: %d)\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    printf("Counter created in %"PRIu64" us, value: %"PRId64"\n", time_diff_us(start_time, end_time), result);
    
    // Sleep briefly to ensure timestamp increases
    platform_usleep(1000);
    
    // Test increment
    increment = 3;
    printf("\nIncrementing counter by 3\n");
    timestamp = get_current_timestamp_ns();
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_atomic_inc_dec(client, counter_key, strlen(counter_key), increment, timestamp, &result);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Failed to increment counter (error code: %d)\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    printf("Counter incremented in %"PRIu64" us, new value: %"PRId64"\n", time_diff_us(start_time, end_time), result);
    
    // Sleep briefly to ensure timestamp increases
    platform_usleep(1000);
    
    // Test decrement
    increment = -2;
    printf("\nDecrementing counter by 2\n");
    timestamp = get_current_timestamp_ns();
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ret = rioc_atomic_inc_dec(client, counter_key, strlen(counter_key), increment, timestamp, &result);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Failed to decrement counter (error code: %d)\n", ret);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    printf("Counter decremented in %"PRIu64" us, new value: %"PRId64"\n", time_diff_us(start_time, end_time), result);
    
    // Test batch atomic operations
    printf("\n10. Testing batch atomic operations\n");
    
    // Create new batch
    batch = rioc_batch_create(client);
    if (!batch) {
        fprintf(stderr, "Failed to create batch for atomic operations\n");
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    // Add multiple atomic operations to batch
    timestamp = get_current_timestamp_ns();
    ret = rioc_batch_add_atomic_inc_dec(batch, counter_key, strlen(counter_key), 10, timestamp);
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Failed to add first atomic operation to batch (error code: %d)\n", ret);
        rioc_batch_free(batch);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    timestamp = get_current_timestamp_ns() + 1;
    ret = rioc_batch_add_atomic_inc_dec(batch, counter_key, strlen(counter_key), -5, timestamp);
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Failed to add second atomic operation to batch (error code: %d)\n", ret);
        rioc_batch_free(batch);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    // Execute batch
    printf("Executing batch with atomic operations\n");
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    tracker = rioc_batch_execute_async(batch);
    if (!tracker) {
        fprintf(stderr, "Failed to execute batch\n");
        rioc_batch_free(batch);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    // Wait for batch to complete
    ret = rioc_batch_wait(tracker, 0);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    if (ret != RIOC_SUCCESS) {
        fprintf(stderr, "Batch execution failed (error code: %d)\n", ret);
        rioc_batch_tracker_free(tracker);
        rioc_client_disconnect_with_config(client);
        return 1;
    }
    
    printf("Batch atomic operations completed in %"PRIu64" us\n", time_diff_us(start_time, end_time));
    
    // Get results from batch operations
    int64_t batch_results[2];
    for (int i = 0; i < 2; i++) {
        char *value;
        size_t value_len;
        ret = rioc_batch_get_response_async(tracker, i, &value, &value_len);
        if (ret != RIOC_SUCCESS) {
            fprintf(stderr, "Failed to get batch result %d (error code: %d)\n", i, ret);
            rioc_batch_tracker_free(tracker);
            rioc_client_disconnect_with_config(client);
            return 1;
        }
        memcpy(&batch_results[i], value, sizeof(int64_t));
    }
    printf("Batch results - First increment (+10): %"PRId64", Second increment (-5): %"PRId64"\n", 
           batch_results[0], batch_results[1]);
    
    // Free batch resources
    rioc_batch_tracker_free(tracker);

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    rioc_client_disconnect_with_config(client);
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    printf("\nAll tests completed successfully (cleanup took %"PRIu64" us)\n", time_diff_us(start_time, end_time));
    return 0;
} 