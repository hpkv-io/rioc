# RIOC: Remote I/O Client

Client SDKs and API for interacting with the RIOC high-performance key-value store.

## Overview

RIOC provides a client-server protocol for interacting with HPKV, focusing on:

1. **High Performance**: Optimized for low latency and high throughput
2. **Data Consistency**: Atomic operations and proper transaction handling
3. **Transport Security**: TLS 1.3 implementation with robust authentication options

## Available SDKs

- [Node.js](./sdk/node/hpkv-rioc/)
- [Python](./sdk/python/hpkv-rioc/)
- [.NET](./sdk/dotnet/)

## Documentation

Each SDK contains its own detailed documentation and examples in its respective folder.

The core API is documented in the [src/README.md](./src/README.md) file.

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](./LICENSE) file for details. 