#include "binding.h"
#include <string>
#include <memory>
#include <atomic>

// Define the range result struct to match the C API
struct rioc_range_result {
  char* key;
  size_t key_len;
  char* value;
  size_t value_len;
};

Napi::FunctionReference RiocClient::constructor;
Napi::FunctionReference RiocBatch::constructor;
Napi::FunctionReference RiocBatchTracker::constructor;

// RiocClient implementation
Napi::Object RiocClient::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "RiocClient", {
    InstanceMethod("get", &RiocClient::Get),
    InstanceMethod("insert", &RiocClient::Insert),
    InstanceMethod("delete", &RiocClient::Delete),
    InstanceMethod("rangeQuery", &RiocClient::RangeQuery),
    InstanceMethod("atomicIncDec", &RiocClient::AtomicIncDec),
    InstanceMethod("dispose", &RiocClient::Dispose),
    InstanceMethod("createBatch", &RiocClient::CreateBatch),
    StaticMethod("getTimestamp", &RiocClient::GetTimestamp)
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  exports.Set("RiocClient", func);
  return exports;
}

RiocClient::RiocClient(const Napi::CallbackInfo& info) : Napi::ObjectWrap<RiocClient>(info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "Config object expected").ThrowAsJavaScriptException();
    return;
  }

  Napi::Object config = info[0].As<Napi::Object>();
  rioc_client_config native_config = {};

  // Extract config values
  std::string host = config.Get("host").As<Napi::String>().Utf8Value();
  native_config.host = const_cast<char*>(host.c_str());
  native_config.port = config.Get("port").As<Napi::Number>().Uint32Value();
  native_config.timeout_ms = config.Has("timeoutMs") ? 
    config.Get("timeoutMs").As<Napi::Number>().Uint32Value() : 5000;

  // Handle TLS config if present
  std::string ca_path, cert_path, key_path, verify_hostname;
  rioc_tls_config native_tls = {};

  if (config.Has("tls") && !config.Get("tls").IsNull()) {
    Napi::Object tls = config.Get("tls").As<Napi::Object>();

    if (tls.Has("caPath")) {
      ca_path = tls.Get("caPath").As<Napi::String>().Utf8Value();
      native_tls.ca_path = ca_path.c_str();
    }

    if (tls.Has("certificatePath")) {
      cert_path = tls.Get("certificatePath").As<Napi::String>().Utf8Value();
      native_tls.cert_path = cert_path.c_str();
    }

    if (tls.Has("keyPath")) {
      key_path = tls.Get("keyPath").As<Napi::String>().Utf8Value();
      native_tls.key_path = key_path.c_str();
    }

    if (tls.Has("verifyHostname")) {
      verify_hostname = tls.Get("verifyHostname").As<Napi::String>().Utf8Value();
      native_tls.verify_hostname = verify_hostname.c_str();
    }

    native_tls.verify_peer = tls.Has("verifyPeer") ? 
      tls.Get("verifyPeer").As<Napi::Boolean>().Value() : true;

    native_config.tls = &native_tls;
  }

  // Connect to server
  struct rioc_client* client = nullptr;
  int result = rioc_client_connect_with_config(&native_config, &client);
  if (result != 0) {
    Napi::Error::New(env, "Failed to connect to server").ThrowAsJavaScriptException();
    return;
  }

  this->client_ptr = client;
}

RiocClient::~RiocClient() {
  if (client_ptr) {
    rioc_client_disconnect_with_config(static_cast<struct rioc_client*>(client_ptr));
    client_ptr = nullptr;
  }
}

Napi::Value RiocClient::Get(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "Buffer expected for key").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Buffer<uint8_t> key = info[0].As<Napi::Buffer<uint8_t>>();
  char* value_ptr = nullptr;
  size_t value_len = 0;

  int result = rioc_get(
    static_cast<struct rioc_client*>(client_ptr),
    reinterpret_cast<const char*>(key.Data()),
    key.Length(),
    &value_ptr,
    &value_len
  );

  if (result != 0) {
    auto error = Napi::Error::New(env, "Get operation failed");
    error.Set("code", Napi::Number::New(env, result));
    error.ThrowAsJavaScriptException();
    return env.Null();
  }

  if (value_len == 0 || value_ptr == nullptr) {
    return env.Null();
  }

  return Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<uint8_t*>(value_ptr), value_len);
}

Napi::Value RiocClient::Insert(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 3 || !info[0].IsBuffer() || !info[1].IsBuffer() || !info[2].IsBigInt()) {
    Napi::TypeError::New(env, "Expected (Buffer, Buffer, BigInt)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Buffer<uint8_t> key = info[0].As<Napi::Buffer<uint8_t>>();
  Napi::Buffer<uint8_t> value = info[1].As<Napi::Buffer<uint8_t>>();
  bool lossless;
  uint64_t timestamp = info[2].As<Napi::BigInt>().Uint64Value(&lossless);

  int result = rioc_insert(
    static_cast<struct rioc_client*>(client_ptr),
    reinterpret_cast<const char*>(key.Data()),
    key.Length(),
    reinterpret_cast<const char*>(value.Data()),
    value.Length(),
    timestamp
  );

  if (result != 0) {
    auto error = Napi::Error::New(env, "Insert operation failed");
    error.Set("code", Napi::Number::New(env, result));
    error.ThrowAsJavaScriptException();
  }

  return env.Undefined();
}

Napi::Value RiocClient::Delete(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 2 || !info[0].IsBuffer() || !info[1].IsBigInt()) {
    Napi::TypeError::New(env, "Expected (Buffer, BigInt)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Buffer<uint8_t> key = info[0].As<Napi::Buffer<uint8_t>>();
  bool lossless;
  uint64_t timestamp = info[1].As<Napi::BigInt>().Uint64Value(&lossless);

  int result = rioc_delete(
    static_cast<struct rioc_client*>(client_ptr),
    reinterpret_cast<const char*>(key.Data()),
    key.Length(),
    timestamp
  );

  if (result != 0) {
    auto error = Napi::Error::New(env, "Delete operation failed");
    error.Set("code", Napi::Number::New(env, result));
    error.ThrowAsJavaScriptException();
  }

  return env.Undefined();
}

void RiocClient::Dispose(const Napi::CallbackInfo& info) {
  if (client_ptr) {
    rioc_client_disconnect_with_config(static_cast<struct rioc_client*>(client_ptr));
    client_ptr = nullptr;
  }
}

Napi::Value RiocClient::CreateBatch(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  auto batch = RiocBatch::constructor.New({Napi::External<void>::New(env, client_ptr)});
  return batch;
}

Napi::Value RiocClient::GetTimestamp(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  uint64_t timestamp = rioc_get_timestamp_ns();
  return Napi::BigInt::New(env, timestamp);
}

Napi::Value RiocClient::RangeQuery(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 2 || !info[0].IsBuffer() || !info[1].IsBuffer()) {
    Napi::TypeError::New(env, "Start key and end key buffers expected").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Buffer<char> startKeyBuffer = info[0].As<Napi::Buffer<char>>();
  Napi::Buffer<char> endKeyBuffer = info[1].As<Napi::Buffer<char>>();

  struct rioc_range_result* results = nullptr;
  size_t result_count = 0;

  int result = rioc_range_query(
    static_cast<struct rioc_client*>(client_ptr),
    startKeyBuffer.Data(), startKeyBuffer.Length(),
    endKeyBuffer.Data(), endKeyBuffer.Length(),
    &results, &result_count
  );

  if (result != 0) {
    Napi::Error::New(env, "Failed to perform range query").ThrowAsJavaScriptException();
    return env.Null();
  }

  // Create array to hold results
  Napi::Array resultArray = Napi::Array::New(env, result_count);

  for (size_t i = 0; i < result_count; i++) {
    Napi::Object pair = Napi::Object::New(env);
    
    // Copy key
    Napi::Buffer<char> keyBuffer = Napi::Buffer<char>::Copy(
      env, results[i].key, results[i].key_len
    );
    
    // Copy value
    Napi::Buffer<char> valueBuffer = Napi::Buffer<char>::Copy(
      env, results[i].value, results[i].value_len
    );
    
    pair.Set("key", keyBuffer);
    pair.Set("value", valueBuffer);
    
    resultArray[i] = pair;
  }

  // Free results
  if (results != nullptr && result_count > 0) {
    rioc_free_range_results(results, result_count);
  }

  return resultArray;
}

Napi::Value RiocClient::AtomicIncDec(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 3 || !info[0].IsBuffer() || !info[1].IsNumber() || !info[2].IsBigInt()) {
    Napi::TypeError::New(env, "Expected key buffer, value number, and timestamp").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Buffer<uint8_t> key = info[0].As<Napi::Buffer<uint8_t>>();
  int64_t value = info[1].As<Napi::Number>().Int64Value();
  bool lossless = false;
  uint64_t timestamp = info[2].As<Napi::BigInt>().Uint64Value(&lossless);

  int64_t result = 0;
  int status = rioc_atomic_inc_dec(
    static_cast<struct rioc_client*>(client_ptr),
    reinterpret_cast<const char*>(key.Data()),
    key.Length(),
    value,
    timestamp,
    &result
  );

  if (status != 0) {
    auto error = Napi::Error::New(env, "Atomic increment/decrement operation failed");
    error.Set("code", Napi::Number::New(env, status));
    error.ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::BigInt::New(env, static_cast<int64_t>(result));
}

// RiocBatch implementation
Napi::Object RiocBatch::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "RiocBatch", {
    InstanceMethod("addGet", &RiocBatch::AddGet),
    InstanceMethod("addInsert", &RiocBatch::AddInsert),
    InstanceMethod("addDelete", &RiocBatch::AddDelete),
    InstanceMethod("addRangeQuery", &RiocBatch::AddRangeQuery),
    InstanceMethod("executeAsync", &RiocBatch::ExecuteAsync),
    InstanceMethod("dispose", &RiocBatch::Dispose),
    InstanceMethod("addAtomicIncDec", &RiocBatch::AddAtomicIncDec)
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  exports.Set("RiocBatch", func);
  return exports;
}

RiocBatch::RiocBatch(const Napi::CallbackInfo& info) : Napi::ObjectWrap<RiocBatch>(info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsExternal()) {
    Napi::TypeError::New(env, "Client pointer expected").ThrowAsJavaScriptException();
    return;
  }

  void* client_ptr = info[0].As<Napi::External<void>>().Data();
  struct rioc_batch* batch = rioc_batch_create(static_cast<struct rioc_client*>(client_ptr));
  if (!batch) {
    Napi::Error::New(env, "Failed to create batch").ThrowAsJavaScriptException();
  }
  batch_ptr = batch;
}

RiocBatch::~RiocBatch() {
  if (batch_ptr) {
    rioc_batch_free(static_cast<struct rioc_batch*>(batch_ptr));
    batch_ptr = nullptr;
  }
}

void RiocBatch::AddGet(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "Buffer expected for key").ThrowAsJavaScriptException();
    return;
  }

  Napi::Buffer<uint8_t> key = info[0].As<Napi::Buffer<uint8_t>>();
  int result = rioc_batch_add_get(
    static_cast<struct rioc_batch*>(batch_ptr),
    reinterpret_cast<const char*>(key.Data()),
    key.Length()
  );

  if (result != 0) {
    Napi::Error::New(env, "Failed to add get operation to batch").ThrowAsJavaScriptException();
  }
}

void RiocBatch::AddInsert(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 3 || !info[0].IsBuffer() || !info[1].IsBuffer() || !info[2].IsBigInt()) {
    Napi::TypeError::New(env, "Expected (Buffer, Buffer, BigInt)").ThrowAsJavaScriptException();
    return;
  }

  Napi::Buffer<uint8_t> key = info[0].As<Napi::Buffer<uint8_t>>();
  Napi::Buffer<uint8_t> value = info[1].As<Napi::Buffer<uint8_t>>();
  bool lossless;
  uint64_t timestamp = info[2].As<Napi::BigInt>().Uint64Value(&lossless);

  int result = rioc_batch_add_insert(
    static_cast<struct rioc_batch*>(batch_ptr),
    reinterpret_cast<const char*>(key.Data()),
    key.Length(),
    reinterpret_cast<const char*>(value.Data()),
    value.Length(),
    timestamp
  );

  if (result != 0) {
    Napi::Error::New(env, "Failed to add insert operation to batch").ThrowAsJavaScriptException();
  }
}

void RiocBatch::AddDelete(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 2 || !info[0].IsBuffer() || !info[1].IsBigInt()) {
    Napi::TypeError::New(env, "Expected (Buffer, BigInt)").ThrowAsJavaScriptException();
    return;
  }

  Napi::Buffer<uint8_t> key = info[0].As<Napi::Buffer<uint8_t>>();
  bool lossless;
  uint64_t timestamp = info[1].As<Napi::BigInt>().Uint64Value(&lossless);

  int result = rioc_batch_add_delete(
    static_cast<struct rioc_batch*>(batch_ptr),
    reinterpret_cast<const char*>(key.Data()),
    key.Length(),
    timestamp
  );

  if (result != 0) {
    Napi::Error::New(env, "Failed to add delete operation to batch").ThrowAsJavaScriptException();
  }
}

void RiocBatch::AddRangeQuery(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 2 || !info[0].IsBuffer() || !info[1].IsBuffer()) {
    Napi::TypeError::New(env, "Start key and end key buffers expected").ThrowAsJavaScriptException();
    return;
  }

  Napi::Buffer<char> startKeyBuffer = info[0].As<Napi::Buffer<char>>();
  Napi::Buffer<char> endKeyBuffer = info[1].As<Napi::Buffer<char>>();

  int result = rioc_batch_add_range_query(
    static_cast<struct rioc_batch*>(batch_ptr),
    startKeyBuffer.Data(), startKeyBuffer.Length(),
    endKeyBuffer.Data(), endKeyBuffer.Length()
  );

  if (result != 0) {
    Napi::Error::New(env, "Failed to add range query to batch").ThrowAsJavaScriptException();
    return;
  }
}

Napi::Value RiocBatch::ExecuteAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  struct rioc_batch_tracker* tracker = rioc_batch_execute_async(static_cast<struct rioc_batch*>(batch_ptr));
  if (!tracker) {
    Napi::Error::New(env, "Failed to execute batch").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto tracker_obj = RiocBatchTracker::constructor.New({Napi::External<void>::New(env, tracker)});
  return tracker_obj;
}

void RiocBatch::Dispose(const Napi::CallbackInfo& info) {
  if (batch_ptr) {
    rioc_batch_free(static_cast<struct rioc_batch*>(batch_ptr));
    batch_ptr = nullptr;
  }
}

void RiocBatch::AddAtomicIncDec(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 3 || !info[0].IsBuffer() || !info[1].IsNumber() || !info[2].IsBigInt()) {
    Napi::TypeError::New(env, "Expected key buffer, value number, and timestamp").ThrowAsJavaScriptException();
    return;
  }

  Napi::Buffer<uint8_t> key = info[0].As<Napi::Buffer<uint8_t>>();
  int64_t value = info[1].As<Napi::Number>().Int64Value();
  bool lossless = false;
  uint64_t timestamp = info[2].As<Napi::BigInt>().Uint64Value(&lossless);

  int result = rioc_batch_add_atomic_inc_dec(
    static_cast<struct rioc_batch*>(batch_ptr),
    reinterpret_cast<const char*>(key.Data()),
    key.Length(),
    value,
    timestamp
  );

  if (result != 0) {
    Napi::Error::New(env, "Failed to add atomic increment/decrement operation to batch").ThrowAsJavaScriptException();
    return;
  }
}

// RiocBatchTracker implementation
Napi::Object RiocBatchTracker::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "RiocBatchTracker", {
    InstanceMethod("wait", &RiocBatchTracker::Wait),
    InstanceMethod("getResponse", &RiocBatchTracker::GetResponse),
    InstanceMethod("getRangeQueryResponse", &RiocBatchTracker::GetRangeQueryResponse),
    InstanceMethod("dispose", &RiocBatchTracker::Dispose),
    InstanceMethod("getAtomicResult", &RiocBatchTracker::GetAtomicResult)
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  exports.Set("RiocBatchTracker", func);
  return exports;
}

RiocBatchTracker::RiocBatchTracker(const Napi::CallbackInfo& info) : Napi::ObjectWrap<RiocBatchTracker>(info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsExternal()) {
    Napi::TypeError::New(env, "Tracker pointer expected").ThrowAsJavaScriptException();
    return;
  }

  tracker_ptr = info[0].As<Napi::External<void>>().Data();
}

RiocBatchTracker::~RiocBatchTracker() {
  if (tracker_ptr) {
    rioc_batch_tracker_free(static_cast<struct rioc_batch_tracker*>(tracker_ptr));
    tracker_ptr = nullptr;
  }
}

void RiocBatchTracker::Wait(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  int timeout_ms = info.Length() > 0 && info[0].IsNumber() ? 
    info[0].As<Napi::Number>().Int32Value() : -1;

  int result = rioc_batch_wait(static_cast<struct rioc_batch_tracker*>(tracker_ptr), timeout_ms);
  if (result != 0) {
    Napi::Error::New(env, "Batch execution failed").ThrowAsJavaScriptException();
  }
}

Napi::Value RiocBatchTracker::GetResponse(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "Number expected for index").ThrowAsJavaScriptException();
    return env.Null();
  }

  size_t index = info[0].As<Napi::Number>().Uint32Value();
  char* value_ptr = nullptr;
  size_t value_len = 0;

  int result = rioc_batch_get_response_async(
    static_cast<struct rioc_batch_tracker*>(tracker_ptr),
    index,
    &value_ptr,
    &value_len
  );

  if (result == -6) { // RIOC_ERR_NOENT
    return env.Null();
  }

  if (result != 0) {
    Napi::Error::New(env, "Failed to get batch response").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (value_len == 0 || value_ptr == nullptr) {
    return env.Null();
  }

  return Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<uint8_t*>(value_ptr), value_len);
}

Napi::Value RiocBatchTracker::GetRangeQueryResponse(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "Index number expected").ThrowAsJavaScriptException();
    return env.Null();
  }

  size_t index = info[0].As<Napi::Number>().Uint32Value();
  char* value_ptr = nullptr;
  size_t value_len = 0;

  int result = rioc_batch_get_response_async(
    static_cast<struct rioc_batch_tracker*>(tracker_ptr),
    index, &value_ptr, &value_len
  );

  if (result != 0) {
    Napi::Error::New(env, "Failed to get batch response").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (value_ptr == nullptr || value_len == 0) {
    return env.Null();
  }

  // For range query, value_len is the count of results
  // and value_ptr points to an array of rioc_range_result structs
  struct rioc_range_result* results = reinterpret_cast<struct rioc_range_result*>(value_ptr);
  
  // Create array to hold results
  Napi::Array resultArray = Napi::Array::New(env, value_len);

  for (size_t i = 0; i < value_len; i++) {
    Napi::Object pair = Napi::Object::New(env);
    
    // Copy key
    Napi::Buffer<char> keyBuffer = Napi::Buffer<char>::Copy(
      env, results[i].key, results[i].key_len
    );
    
    // Copy value
    Napi::Buffer<char> valueBuffer = Napi::Buffer<char>::Copy(
      env, results[i].value, results[i].value_len
    );
    
    pair.Set("key", keyBuffer);
    pair.Set("value", valueBuffer);
    
    resultArray[i] = pair;
  }

  // Note: We don't free the results here as they are managed by the batch tracker
  // and will be freed when the tracker is disposed

  return resultArray;
}

Napi::Value RiocBatchTracker::GetAtomicResult(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "Number expected for index").ThrowAsJavaScriptException();
    return env.Null();
  }

  size_t index = info[0].As<Napi::Number>().Uint32Value();
  char* value_ptr = nullptr;
  size_t value_len = 0;

  int status = rioc_batch_get_response_async(
    static_cast<struct rioc_batch_tracker*>(tracker_ptr),
    index,
    &value_ptr,
    &value_len
  );

  if (status != 0) {
    auto error = Napi::Error::New(env, "Failed to get atomic result from batch");
    error.Set("code", Napi::Number::New(env, status));
    error.ThrowAsJavaScriptException();
    return env.Null();
  }

  // For atomic operations, the result is an int64_t in the value buffer
  if (value_ptr != nullptr && value_len >= sizeof(int64_t)) {
    int64_t result = *reinterpret_cast<int64_t*>(value_ptr);
    return Napi::BigInt::New(env, static_cast<int64_t>(result));
  }

  // If value is not as expected, return 0
  return Napi::BigInt::New(env, static_cast<int64_t>(0));
}

void RiocBatchTracker::Dispose(const Napi::CallbackInfo& info) {
  if (tracker_ptr) {
    rioc_batch_tracker_free(static_cast<struct rioc_batch_tracker*>(tracker_ptr));
    tracker_ptr = nullptr;
  }
}

// Initialize native addon
Napi::Object Init(Napi::Env env, Napi::Object exports) {
  RiocClient::Init(env, exports);
  RiocBatch::Init(env, exports);
  RiocBatchTracker::Init(env, exports);
  return exports;
}

NODE_API_MODULE(rioc, Init)