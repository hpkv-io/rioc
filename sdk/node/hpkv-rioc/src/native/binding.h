#ifndef RIOC_BINDING_H
#define RIOC_BINDING_H

#include <napi.h>
#include <stdint.h>
#include <stddef.h>

// Forward declarations
struct rioc_client;
struct rioc_batch;
struct rioc_batch_tracker;
struct rioc_range_result;

// TLS configuration
struct rioc_tls_config {
  const char* cert_path;        // Server cert or client CA cert path
  const char* key_path;         // Server private key path (server only)
  const char* ca_path;          // CA certificate path (client only)
  const char* verify_hostname;  // Hostname to verify (client only)
  bool verify_peer;            // Enable certificate verification
};

// Client configuration
struct rioc_client_config {
  char* host;
  uint32_t port;
  uint32_t timeout_ms;
  struct rioc_tls_config* tls;
};

class RiocClient : public Napi::ObjectWrap<RiocClient> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  RiocClient(const Napi::CallbackInfo& info);
  ~RiocClient();

private:
  static Napi::FunctionReference constructor;
  void* client_ptr;

  // Core operations
  Napi::Value Get(const Napi::CallbackInfo& info);
  Napi::Value Insert(const Napi::CallbackInfo& info);
  Napi::Value Delete(const Napi::CallbackInfo& info);
  Napi::Value RangeQuery(const Napi::CallbackInfo& info);
  Napi::Value AtomicIncDec(const Napi::CallbackInfo& info);
  void Dispose(const Napi::CallbackInfo& info);

  // Batch operations
  Napi::Value CreateBatch(const Napi::CallbackInfo& info);
  static Napi::Value GetTimestamp(const Napi::CallbackInfo& info);
};

class RiocBatch : public Napi::ObjectWrap<RiocBatch> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  RiocBatch(const Napi::CallbackInfo& info);
  ~RiocBatch();

private:
  static Napi::FunctionReference constructor;
  void* batch_ptr;

  // Batch operations
  void AddGet(const Napi::CallbackInfo& info);
  void AddInsert(const Napi::CallbackInfo& info);
  void AddDelete(const Napi::CallbackInfo& info);
  void AddRangeQuery(const Napi::CallbackInfo& info);
  void AddAtomicIncDec(const Napi::CallbackInfo& info);
  Napi::Value ExecuteAsync(const Napi::CallbackInfo& info);
  void Dispose(const Napi::CallbackInfo& info);

  friend class RiocClient;
};

class RiocBatchTracker : public Napi::ObjectWrap<RiocBatchTracker> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  RiocBatchTracker(const Napi::CallbackInfo& info);
  ~RiocBatchTracker();

private:
  static Napi::FunctionReference constructor;
  void* tracker_ptr;

  // Tracker operations
  void Wait(const Napi::CallbackInfo& info);
  Napi::Value GetResponse(const Napi::CallbackInfo& info);
  Napi::Value GetRangeQueryResponse(const Napi::CallbackInfo& info);
  Napi::Value GetAtomicResult(const Napi::CallbackInfo& info);
  void Dispose(const Napi::CallbackInfo& info);

  friend class RiocBatch;
};

// Function declarations from rioc.h
extern "C" {
  int rioc_client_connect_with_config(struct rioc_client_config* config, struct rioc_client** client);
  void rioc_client_disconnect_with_config(struct rioc_client* client);
  int rioc_get(struct rioc_client* client, const char* key, size_t key_len, char** value, size_t* value_len);
  int rioc_insert(struct rioc_client* client, const char* key, size_t key_len, const char* value, size_t value_len, uint64_t timestamp);
  int rioc_delete(struct rioc_client* client, const char* key, size_t key_len, uint64_t timestamp);
  int rioc_range_query(struct rioc_client* client, const char* start_key, size_t start_key_len, 
                      const char* end_key, size_t end_key_len, 
                      struct rioc_range_result** results, size_t* result_count);
  struct rioc_batch* rioc_batch_create(struct rioc_client* client);
  int rioc_batch_add_get(struct rioc_batch* batch, const char* key, size_t key_len);
  int rioc_batch_add_insert(struct rioc_batch* batch, const char* key, size_t key_len, const char* value, size_t value_len, uint64_t timestamp);
  int rioc_batch_add_delete(struct rioc_batch* batch, const char* key, size_t key_len, uint64_t timestamp);
  int rioc_batch_add_range_query(struct rioc_batch* batch, const char* start_key, size_t start_key_len, 
                               const char* end_key, size_t end_key_len);
  struct rioc_batch_tracker* rioc_batch_execute_async(struct rioc_batch* batch);
  int rioc_batch_wait(struct rioc_batch_tracker* tracker, int timeout_ms);
  int rioc_batch_get_response_async(struct rioc_batch_tracker* tracker, size_t index, char** value, size_t* value_len);
  void rioc_batch_tracker_free(struct rioc_batch_tracker* tracker);
  void rioc_batch_free(struct rioc_batch* batch);
  uint64_t rioc_get_timestamp_ns(void);
  void rioc_free_range_results(struct rioc_range_result* results, size_t count);
  int rioc_atomic_inc_dec(struct rioc_client* client, const char* key, size_t key_len, int64_t value, uint64_t timestamp, int64_t* result);
  int rioc_batch_add_atomic_inc_dec(struct rioc_batch* batch, const char* key, size_t key_len, int64_t value, uint64_t timestamp);
  int rioc_batch_get_atomic_result_async(struct rioc_batch_tracker* tracker, size_t index, int64_t* result);
}

#endif // RIOC_BINDING_H