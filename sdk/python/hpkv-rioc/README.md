# HPKV RIOC Python SDK

Python SDK for HPKV RIOC (Remote I/O Client) - a high-performance key-value store client.

## Features

- High-performance key-value operations
- Support for TLS/mTLS
- Batch operations
- Range queries
- Atomic increment/decrement operations
- Thread-safe client
- Cross-platform support (Windows, Linux, macOS)
- Python 3.8+ support

## Installation

```bash
pip install hpkv-rioc
```

## Quick Start

```python
from hpkv_rioc import RiocClient, RiocConfig

# Create client configuration
config = RiocConfig(
    host="localhost",
    port=8080,
    timeout_ms=5000
)

# Create client
client = RiocClient(config)

# Insert a key-value pair
client.insert_string("hello", "world")

# Get the value
value = client.get_string("hello")
print(f"hello -> {value}")  # hello -> world

# Delete the key
client.delete_string("hello")
```

## Using TLS/mTLS

```python
from hpkv_rioc import RiocClient, RiocConfig, RiocTlsConfig

# Create TLS configuration
tls_config = RiocTlsConfig(
    certificate_path="certs/client.crt",
    key_path="certs/client.key",
    ca_path="certs/ca.crt",
    verify_hostname="localhost",
    verify_peer=True
)

# Create client configuration with TLS
config = RiocConfig(
    host="localhost",
    port=8443,  # TLS port
    timeout_ms=5000,
    tls=tls_config
)

# Create client
client = RiocClient(config)
```

## Batch Operations

```python
from hpkv_rioc import RiocClient, RiocConfig

config = RiocConfig(host="localhost", port=8080)
client = RiocClient(config)

# Using context manager
with client.batch() as batch:
    batch.add_insert(b"key1", b"value1", client.get_timestamp())
    batch.add_insert(b"key2", b"value2", client.get_timestamp())
    batch.add_get(b"key1")
    batch.add_get(b"key2")

# Manual batch handling
batch = client.create_batch()
batch.add_get(b"key1")
batch.add_get(b"key2")

tracker = batch.execute()
tracker.wait()

value1 = tracker.get_response(0)
value2 = tracker.get_response(1)
```

## Range Queries

```python
from hpkv_rioc import RiocClient, RiocConfig

config = RiocConfig(host="localhost", port=8080)
client = RiocClient(config)

# Perform range query
results = client.range_query(b"prefix:", b"prefix:\xff")

# Process results
for result in results:
    print(f"Key: {result.key}, Value: {result.value}")

# Using strings
results = client.range_query_string("user:", "user:\xff")
for key, value in results:
    print(f"Key: {key}, Value: {value}")
```

## Atomic Increment/Decrement Operations

```python
from hpkv_rioc import RiocClient, RiocConfig

config = RiocConfig(host="localhost", port=8080)
client = RiocClient(config)

# Initialize a counter with value 10
result = client.atomic_inc_dec_string("counter", 10)
print(f"Counter value: {result}")  # Output: Counter value: 10

# Increment by 5
result = client.atomic_inc_dec_string("counter", 5)
print(f"Counter value: {result}")  # Output: Counter value: 15

# Decrement by 3
result = client.atomic_inc_dec_string("counter", -3)
print(f"Counter value: {result}")  # Output: Counter value: 12

# Read current value without changing it
result = client.atomic_inc_dec_string("counter", 0)
print(f"Current value: {result}")  # Output: Current value: 12
```

### Atomic Operations in Batch

```python
from hpkv_rioc import RiocClient, RiocConfig

config = RiocConfig(host="localhost", port=8080)
client = RiocClient(config)

# Initialize counters
client.atomic_inc_dec_string("counter1", 5)
client.atomic_inc_dec_string("counter2", 10)

# Create a batch with atomic operations
batch = client.create_batch()
timestamp = client.get_timestamp()

# Add operations to batch
batch.add_atomic_inc_dec("counter1".encode(), 15, timestamp)  # Increment (5 -> 20)
batch.add_atomic_inc_dec("counter2".encode(), -5, timestamp)  # Decrement (10 -> 5)

# Execute batch
tracker = batch.execute()
tracker.wait()

# Get results
result1 = tracker.get_atomic_result(0)
result2 = tracker.get_atomic_result(1)

print(f"Counter1: {result1}")  # Output: Counter1: 20
print(f"Counter2: {result2}")  # Output: Counter2: 5
```

> **Note about Initialization**: 
> - Always initialize counters with positive values
> - After initialization, you can increment or decrement the counter to any value, including negative values
> - The kernel module does not support direct initialization with negative values

## API Reference

### RiocConfig

Configuration for RIOC client connection.

```python
class RiocConfig:
    host: str            # The host to connect to
    port: int           # The port to connect to
    timeout_ms: int     # Operation timeout in milliseconds (default: 5000)
    tls: RiocTlsConfig  # Optional TLS configuration
```

### RiocTlsConfig

TLS configuration for RIOC client.

```python
class RiocTlsConfig:
    certificate_path: str   # Path to client certificate file
    key_path: str          # Path to client private key file
    ca_path: str           # Path to CA certificate file
    verify_hostname: str   # Hostname to verify in server's certificate
    verify_peer: bool      # Whether to verify peer certificates
```

### RiocClient

Main client class for interacting with RIOC server.

```python
class RiocClient:
    def __init__(self, config: RiocConfig): ...
    
    # Basic operations
    def get(self, key: bytes) -> bytes: ...
    def get_string(self, key: str) -> str: ...
    def insert(self, key: bytes, value: bytes, timestamp: Optional[int] = None): ...
    def insert_string(self, key: str, value: str, timestamp: Optional[int] = None): ...
    def delete(self, key: bytes, timestamp: Optional[int] = None): ...
    def delete_string(self, key: str, timestamp: Optional[int] = None): ...
    
    # Range query operations
    def range_query(self, start_key: bytes, end_key: bytes) -> List[RangeQueryResult]: ...
    def range_query_string(self, start_key: str, end_key: str) -> List[Tuple[str, str]]: ...
    
    # Batch operations
    def create_batch(self) -> RiocBatch: ...
    @contextmanager
    def batch(self) -> Generator[RiocBatch, None, None]: ...
    
    # Utilities
    @staticmethod
    def get_timestamp() -> int: ...
```

### RangeQueryResult

Represents a key-value pair returned from a range query.

```python
class RangeQueryResult:
    key: bytes    # The key as bytes
    value: bytes  # The value as bytes
```

### RiocBatch

Batch operation class.

```python
class RiocBatch:
    def add_get(self, key: bytes): ...
    def add_insert(self, key: bytes, value: bytes, timestamp: int): ...
    def add_delete(self, key: bytes, timestamp: int): ...
    def add_range_query(self, start_key: bytes, end_key: bytes): ...
    def execute(self) -> RiocBatchTracker: ...
```

### RiocBatchTracker

Tracks batch operation execution.

```python
class RiocBatchTracker:
    def wait(self, timeout_ms: int = -1): ...
    def get_response(self, index: int) -> bytes: ...
    def get_range_query_response(self, index: int) -> List[RangeQueryResult]: ...
```

## Error Handling

The SDK defines several exception types:

- `RiocError`: Base exception for all RIOC errors
- `RiocTimeoutError`: Raised when an operation times out
- `RiocConnectionError`: Raised when there is a connection error

Example:

```python
from hpkv_rioc import RiocError, RiocTimeoutError, RiocConnectionError

try:
    value = client.get_string("non-existent-key")
except RiocTimeoutError:
    print("Operation timed out")
except RiocConnectionError:
    print("Connection error")
except RiocError as e:
    print(f"RIOC error: {e}")
```

## Thread Safety

The `RiocClient` class is thread-safe. All operations are protected by a lock to ensure thread safety.

## Platform Support

The SDK supports the following platforms:

- Windows (x64)
- Linux (x64)
- macOS (x64, arm64)