# HPKV.RIOC - High Performance Key-Value Store .NET Client SDK

HPKV.RIOC is a high-performance .NET client SDK for interacting with the High Performance Key-Value Store (HPKV). It provides a simple, efficient, and type-safe way to interact with HPKV from .NET applications.

## Features

- Full support for HPKV operations (Get, Insert, Delete, Range Query)
- Atomic counter operations (Increment/Decrement)
- Batch operations for improved performance
- Secure communication with TLS and mTLS support
- Cross-platform support (Windows, Linux, macOS)
- Support for both x64 and ARM64 architectures
- Async-capable batch operations
- Built-in logging support
- Native performance through direct interop

## Installation

Install the package from NuGet:

```bash
dotnet add package HPKV.RIOC
```

## Quick Start

Here's a simple example of using the SDK:

```csharp
using HPKV.RIOC;
using Microsoft.Extensions.Logging;

// Create logger (optional)
ILoggerFactory loggerFactory = LoggerFactory.Create(builder => 
    builder.AddConsole().SetMinimumLevel(LogLevel.Information));
var logger = loggerFactory.CreateLogger<RiocClient>();

// Create client configuration
var config = new RiocConfig
{
    Host = "localhost",
    Port = 8000,
    TimeoutMs = 5000,
    // TLS configuration (recommended)
    Tls = new RiocTlsConfig
    {
        // For one-way TLS (server authentication only)
        CaPath = "path/to/ca.crt",
        VerifyHostname = "localhost",
        VerifyPeer = true,

        // For mutual TLS (mTLS)
        CertificatePath = "path/to/client.crt",  // Client certificate
        KeyPath = "path/to/client.key"           // Client private key
    }
};

// Create client
using var client = new RiocClient(config, logger);

// Basic operations
string key = "mykey";
string value = "myvalue";
ulong timestamp = RiocClient.GetTimestamp();

// Insert
client.InsertString(key, value, timestamp);

// Get
string retrievedValue = client.GetString(key);
Console.WriteLine($"Retrieved value: {retrievedValue}");

// Delete
client.DeleteString(key, RiocClient.GetTimestamp());

// Range Query
var results = client.RangeQueryString("start_key", "end_key");
foreach (var kv in results)
{
    Console.WriteLine($"Key: {kv.Key}, Value: {kv.Value}");
}
```

## Security

The SDK supports both one-way TLS (server authentication) and mutual TLS (mTLS):

### One-way TLS (Server Authentication)
```csharp
var config = new RiocConfig
{
    Host = "localhost",
    Port = 8000,
    Tls = new RiocTlsConfig
    {
        CaPath = "path/to/ca.crt",          // CA certificate to verify server
        VerifyHostname = "localhost",        // Hostname to verify in server cert
        VerifyPeer = true                    // Enable certificate verification
    }
};
```

### Mutual TLS (mTLS)
```csharp
var config = new RiocConfig
{
    Host = "localhost",
    Port = 8000,
    Tls = new RiocTlsConfig
    {
        CaPath = "path/to/ca.crt",          // CA certificate to verify server
        CertificatePath = "path/to/client.crt", // Client certificate
        KeyPath = "path/to/client.key",      // Client private key
        VerifyHostname = "localhost",        // Hostname to verify in server cert
        VerifyPeer = true                    // Enable certificate verification
    }
};
```

## Batch Operations

For better performance when executing multiple operations:

```csharp
using var batch = client.CreateBatch();

// Add operations to batch
batch.AddGet(Encoding.UTF8.GetBytes("key1"));
batch.AddInsert(
    Encoding.UTF8.GetBytes("key2"),
    Encoding.UTF8.GetBytes("value2"),
    RiocClient.GetTimestamp()
);
batch.AddDelete(Encoding.UTF8.GetBytes("key3"), RiocClient.GetTimestamp());
batch.AddRangeQuery(
    Encoding.UTF8.GetBytes("start_key"),
    Encoding.UTF8.GetBytes("end_key")
);

// Execute batch asynchronously
using var tracker = batch.ExecuteAsync();

// Wait for completion (optional timeout)
tracker.Wait(timeoutMs: 1000);

// Get results
byte[] value = tracker.GetResponse(0); // Get result for first operation
var rangeResults = tracker.GetRangeQueryResponse(3); // Get range query results
```

## Atomic Counter Operations

The SDK supports atomic increment and decrement operations on counter values:

```csharp
// Initialize or update a counter
string counterKey = "visitor_count";
ulong timestamp = RiocClient.GetTimestamp();

// Increment the counter by 1
long newCount = client.AtomicIncDecString(counterKey, 1, timestamp++);
Console.WriteLine($"Visitor count: {newCount}");

// Increment by larger values
long newValue = client.AtomicIncDecString(counterKey, 10, timestamp++);
Console.WriteLine($"Incremented by 10, new value: {newValue}");

// Decrement a counter (use negative values)
long decrementedValue = client.AtomicIncDecString(counterKey, -5, timestamp++);
Console.WriteLine($"Decremented by 5, new value: {decrementedValue}");

// Read the current value without changing it
long currentValue = client.AtomicIncDecString(counterKey, 0, timestamp++);
Console.WriteLine($"Current value: {currentValue}");

// Atomic operations with raw bytes
byte[] rawKey = Encoding.UTF8.GetBytes("byte_counter");
long rawValue = client.AtomicIncDec(rawKey, 1, timestamp++);
```

### Batch Atomic Operations

For better performance with multiple counters:

```csharp
// Create a batch with multiple atomic operations
using var batch = client.CreateBatch();

// Add multiple counters to update atomically
batch.AddAtomicIncDecString("page_views", 1, timestamp++);
batch.AddAtomicIncDecString("api_calls", 5, timestamp++);
batch.AddAtomicIncDecString("errors", -2, timestamp++);  // Decrement

// Execute batch
using var tracker = batch.ExecuteAsync();
tracker.Wait();

// Get the new counter values
long newPageViews = tracker.GetAtomicResult(0);
long newApiCalls = tracker.GetAtomicResult(1);
long newErrors = tracker.GetAtomicResult(2);

Console.WriteLine($"Updated counters - Views: {newPageViews}, API Calls: {newApiCalls}, Errors: {newErrors}");
```

## Range Query

The SDK supports range queries to retrieve all key-value pairs within a specified range:

```csharp
// Direct range query
var results = client.RangeQueryString("start_key", "end_key");
foreach (var kv in results)
{
    Console.WriteLine($"Key: {kv.Key}, Value: {kv.Value}");
}

// Range query with raw bytes
byte[] startKey = Encoding.UTF8.GetBytes("start_key");
byte[] endKey = Encoding.UTF8.GetBytes("end_key");
var byteResults = client.RangeQuery(startKey, endKey);
foreach (var kv in byteResults)
{
    string key = Encoding.UTF8.GetString(kv.Key);
    string value = Encoding.UTF8.GetString(kv.Value);
    Console.WriteLine($"Key: {key}, Value: {value}");
}

// Batch range query
using var batch = client.CreateBatch();
batch.AddRangeQuery(startKey, endKey);
using var tracker = batch.ExecuteAsync();
tracker.Wait();

var batchResults = tracker.GetRangeQueryResponse(0);
// Or as strings:
var batchStringResults = tracker.GetRangeQueryResponseString(0);
```

## Error Handling

The SDK uses strongly-typed exceptions for error handling:

```csharp
try
{
    string value = client.GetString("nonexistent-key");
}
catch (RiocKeyNotFoundException)
{
    Console.WriteLine("Key not found");
}
catch (RiocException ex)
{
    Console.WriteLine($"Operation failed: {ex.Message} (Error code: {ex.ErrorCode})");
}
```

## Logging

The SDK supports the standard .NET logging abstractions:

```csharp
using Microsoft.Extensions.Logging;

ILogger logger = LoggerFactory.Create(builder =>
{
    builder.AddConsole();
    builder.SetMinimumLevel(LogLevel.Information);
}).CreateLogger<RiocClient>();

var client = new RiocClient(config, logger);
```

## Thread Safety

- `RiocClient` instances are thread-safe for concurrent operations
- `RiocBatch` instances are not thread-safe and should be used by a single thread
- Each operation (Get, Insert, Delete, Range Query, Atomic Inc/Dec) is atomic

## Performance Considerations

- Use batch operations when performing multiple operations
- Reuse client instances when possible
- Use byte arrays directly instead of strings for better performance
- Consider using connection pooling for high-concurrency scenarios
- For range queries, limit the range size to avoid excessive memory usage

## Platform Support

The SDK supports the following platforms:
- Windows x64
- Linux x64
- Linux ARM64
- macOS x64

## Building from Source

To build the SDK from source:

1. Clone the repository:
```bash
git clone https://github.com/mehrandvd/kernel-high-performance-kv-store.git
cd kernel-high-performance-kv-store/api/sdk/csharp
```

2. Build the solution:
```bash
dotnet build
```

3. Run tests:
```bash
# Run tests with default configuration (mTLS enabled)
dotnet test

# Run tests without TLS
RIOC_TEST_TLS=false dotnet test

# Run tests with custom certificate paths
RIOC_TEST_CA_PATH=/path/to/ca.crt \
RIOC_TEST_CLIENT_CERT_PATH=/path/to/client.crt \
RIOC_TEST_CLIENT_KEY_PATH=/path/to/client.key \
dotnet test
```

## Tools

The SDK comes with two command-line tools:

1. `rioc-test`: Basic functionality test tool
```bash
# With mTLS (default)
dotnet run --project HPKV.RIOC.BasicTest/src/HPKV.RIOC.BasicTest.csproj -- \
    -h localhost -p 8000 -t \
    --ca /path/to/ca.crt \
    --cert /path/to/client.crt \
    --key /path/to/client.key

# Without TLS
dotnet run --project HPKV.RIOC.BasicTest/src/HPKV.RIOC.BasicTest.csproj -- -h localhost -p 8000
```

2. `rioc-bench`: Performance benchmark tool
```bash
# With mTLS (default)
dotnet run --project HPKV.RIOC.Benchmark/src/HPKV.RIOC.Benchmark.csproj -- \
    -h localhost -p 8000 -n 4 -o 10000 -s 1024 -v -t \
    --ca /path/to/ca.crt \
    --cert /path/to/client.crt \
    --key /path/to/client.key

# Without TLS
dotnet run --project HPKV.RIOC.Benchmark/src/HPKV.RIOC.Benchmark.csproj -- \
    -h localhost -p 8000 -n 4 -o 10000 -s 1024 -v
```