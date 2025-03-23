# HPKV RIOC - High Performance Key-Value Store Node.js Client SDK

HPKV RIOC is a high-performance Node.js client SDK for interacting with the High Performance Key-Value Store (HPKV). It provides a simple, efficient, and type-safe way to interact with HPKV from Node.js applications.

## Features

- Full support for HPKV operations (Get, Insert, Delete, Range Query)
- Atomic increment/decrement for counter operations
- Batch operations for improved performance
- Secure communication with TLS and mTLS support
- Cross-platform support (Windows, Linux, macOS)
- Support for both x64 and ARM64 architectures
- Async-capable batch operations
- Built-in debugging support
- Native performance through direct interop

## Installation

Install the package from npm:

```bash
npm install hpkv-rioc
```

## Quick Start

Here's a simple example of using the SDK:

```typescript
import { RiocClient, RiocConfig } from 'hpkv-rioc';

// Create client configuration
const config: RiocConfig = {
  host: 'localhost',
  port: 8000,
  timeoutMs: 5000,
  // TLS configuration (recommended)
  tls: {
    // For one-way TLS (server authentication only)
    caPath: 'path/to/ca.crt',          // CA certificate to verify server
    verifyHostname: 'localhost',        // Hostname to verify in server cert
    verifyPeer: true,                  // Enable certificate verification

    // For mutual TLS (mTLS)
    certificatePath: 'path/to/client.crt', // Client certificate
    keyPath: 'path/to/client.key'         // Client private key
  }
};

// Create client
const client = new RiocClient(config);

// Basic operations
const key = Buffer.from('mykey');
const value = Buffer.from('myvalue');
const timestamp = RiocClient.getTimestamp();

// Insert
client.insert(key, value, timestamp);

// Get
const retrievedValue = client.get(key);
if (retrievedValue) {
  console.log('Retrieved value:', retrievedValue.toString());
}

// Range Query
const startKey = Buffer.from('prefix:a');
const endKey = Buffer.from('prefix:z');
const results = client.rangeQuery(startKey, endKey);
console.log(`Found ${results.length} results in range`);
for (const result of results) {
  console.log(`Key: ${result.key.toString()}, Value: ${result.value.toString()}`);
}

// Delete
client.delete(key, RiocClient.getTimestamp());

// Don't forget to dispose when done
client.dispose();
```

## Security

The SDK supports both one-way TLS (server authentication) and mutual TLS (mTLS):

### One-way TLS (Server Authentication)
```typescript
const config: RiocConfig = {
  host: 'localhost',
  port: 8000,
  tls: {
    caPath: 'path/to/ca.crt',          // CA certificate to verify server
    verifyHostname: 'localhost',        // Hostname to verify in server cert
    verifyPeer: true                    // Enable certificate verification
  }
};
```

### Mutual TLS (mTLS)
```typescript
const config: RiocConfig = {
  host: 'localhost',
  port: 8000,
  tls: {
    caPath: 'path/to/ca.crt',          // CA certificate to verify server
    certificatePath: 'path/to/client.crt', // Client certificate
    keyPath: 'path/to/client.key',      // Client private key
    verifyHostname: 'localhost',        // Hostname to verify in server cert
    verifyPeer: true                    // Enable certificate verification
  }
};
```

## Atomic Operations

The SDK supports atomic increment and decrement operations for counter values:

```typescript
import { RiocClient, RiocConfig } from 'hpkv-rioc';

const client = new RiocClient(config);

const key = Buffer.from('counter-key');
const timestamp = RiocClient.getTimestamp();

// Initialize counter with value 10
const value1 = client.atomicIncDec(key, 10, timestamp);
console.log(`Counter value: ${value1}`); // Output: Counter value: 10

// Increment by 5
const value2 = client.atomicIncDec(key, 5, RiocClient.getTimestamp());
console.log(`Counter value: ${value2}`); // Output: Counter value: 15

// Decrement by 3
const value3 = client.atomicIncDec(key, -3, RiocClient.getTimestamp());
console.log(`Counter value: ${value3}`); // Output: Counter value: 12

// Read current value without changing it
const currentValue = client.atomicIncDec(key, 0, RiocClient.getTimestamp());
console.log(`Current value: ${currentValue}`); // Output: Current value: 12

// Don't forget to dispose when done
client.dispose();
```

**Note about initialization**:
- Always initialize counters with a positive value
- After initialization, you can increment or decrement the counter to any value, including negative values
- The kernel module does not support direct initialization with negative values

## Batch Operations

For better performance when executing multiple operations:

```typescript
import { RiocClient, RiocConfig } from 'hpkv-rioc';

const client = new RiocClient(config);

// Create batch
const batch = client.createBatch();

try {
  // Add operations to batch
  batch.addGet(Buffer.from('key1'));
  batch.addInsert(
    Buffer.from('key2'),
    Buffer.from('value2'),
    RiocClient.getTimestamp()
  );
  batch.addDelete(Buffer.from('key3'), RiocClient.getTimestamp());
  
  // Add atomic operations to batch
  batch.addAtomicIncDec(
    Buffer.from('counter1'),
    10,
    RiocClient.getTimestamp()
  );
  
  // Add range query to batch
  batch.addRangeQuery(
    Buffer.from('prefix:a'),
    Buffer.from('prefix:z')
  );

  // Execute batch asynchronously
  const tracker = batch.executeAsync();

  // Wait for completion (optional timeout)
  tracker.wait(1000);

  // Get results
  const value = tracker.getResponse(0); // Get result for first operation
  if (value) {
    console.log('Retrieved value:', value.toString());
  }
  
  // Get atomic result (from the fourth operation)
  const counterValue = tracker.getAtomicResult(3);
  console.log(`Counter value: ${counterValue}`);
  
  // Get range query results (from the fifth operation)
  const rangeResults = tracker.getRangeQueryResponse(4);
  console.log(`Found ${rangeResults.length} results in range`);
  for (const result of rangeResults) {
    console.log(`Key: ${result.key.toString()}, Value: ${result.value.toString()}`);
  }

  // Clean up
  tracker.dispose();
} finally {
  batch.dispose();
  client.dispose();
}
```

## Error Handling

The SDK uses strongly-typed exceptions for error handling:

```typescript
import { RiocClient, RiocKeyNotFoundError } from 'hpkv-rioc';

try {
  const value = client.get(Buffer.from('nonexistent-key'));
} catch (error) {
  if (error instanceof RiocKeyNotFoundError) {
    console.log('Key not found');
  } else {
    console.error('Operation failed:', error);
  }
}
```

## Debugging

The SDK uses the debug package for debugging output. Enable it by setting the DEBUG environment variable:

```bash
DEBUG=hpkv:rioc:* node your-app.js
```

## Examples

The SDK comes with example programs demonstrating various features that you can run using npm:

```bash
# List all available examples
npm run examples
```

1. Basic example:
```bash
# With mTLS (default)
LD_LIBRARY_PATH=/path/to/librioc.so \
RIOC_CA_PATH=/path/to/ca.crt \
RIOC_CERT_PATH=/path/to/client.crt \
RIOC_KEY_PATH=/path/to/client.key \
npm run example:basic -- --tls

# Without TLS
LD_LIBRARY_PATH=/path/to/librioc.so \
npm run example:basic
```

2. Batch operations example:
```bash
# With mTLS (default)
LD_LIBRARY_PATH=/path/to/librioc.so \
RIOC_CA_PATH=/path/to/ca.crt \
RIOC_CERT_PATH=/path/to/client.crt \
RIOC_KEY_PATH=/path/to/client.key \
npm run example:batch -- --tls

# Without TLS
LD_LIBRARY_PATH=/path/to/librioc.so \
npm run example:batch
```

3. Range query example:
```bash
# With mTLS (default)
LD_LIBRARY_PATH=/path/to/librioc.so \
RIOC_CA_PATH=/path/to/ca.crt \
RIOC_CERT_PATH=/path/to/client.crt \
RIOC_KEY_PATH=/path/to/client.key \
npm run example:range-query -- --tls

# Without TLS
LD_LIBRARY_PATH=/path/to/librioc.so \
npm run example:range-query
```

4. Atomic increment/decrement example:
```bash
# With mTLS (default)
LD_LIBRARY_PATH=/path/to/librioc.so \
RIOC_CA_PATH=/path/to/ca.crt \
RIOC_CERT_PATH=/path/to/client.crt \
RIOC_KEY_PATH=/path/to/client.key \
npm run example:atomic -- --tls

# Without TLS
LD_LIBRARY_PATH=/path/to/librioc.so \
npm run example:atomic
```

5. MTLS example (demonstrates secure connections):
```bash
# This example requires TLS
LD_LIBRARY_PATH=/path/to/librioc.so \
RIOC_CA_PATH=/path/to/ca.crt \
RIOC_CERT_PATH=/path/to/client.crt \
RIOC_KEY_PATH=/path/to/client.key \
npm run example:mtls
```

> **Note**: The `LD_LIBRARY_PATH` must point to the directory where `librioc.so` is located. In a development environment, this is typically the build directory of the RIOC project (e.g., `/workspaces/kernel-high-performance-kv-store/api/rioc/build`).

## Testing

The SDK includes a comprehensive test suite. To run the tests:

```bash
# Run tests with default configuration (mTLS enabled)
LD_LIBRARY_PATH=/path/to/librioc.so npm test

# Run tests without TLS
LD_LIBRARY_PATH=/path/to/librioc.so RIOC_TEST_TLS=false npm test

# Run tests with custom certificate paths
LD_LIBRARY_PATH=/path/to/librioc.so \
RIOC_TEST_CA_PATH=/path/to/ca.crt \
RIOC_TEST_CLIENT_CERT_PATH=/path/to/client.crt \
RIOC_TEST_CLIENT_KEY_PATH=/path/to/client.key \
npm test
```

The test suite includes:
- Basic operations (insert, get, delete)
- Range queries (single and batch)
- Binary data handling
- Error handling
- Batch operations
- Timestamp functionality
- TLS/mTLS configuration

## Thread Safety

- `RiocClient` instances are thread-safe for concurrent operations
- `RiocBatch` instances are not thread-safe and should be used by a single thread
- Each operation (Get, Insert, Delete) is atomic

## Performance Considerations

- Use batch operations when performing multiple operations
- Reuse client instances when possible
- Keep client instances for the lifetime of your application
- Consider using connection pooling for high-concurrency scenarios
- Use Buffer.from() for binary data instead of strings when possible

## Requirements

- Node.js 18.0.0 or later
- Windows, Linux, or macOS
- x64 or ARM64 architecture

## License

MIT
