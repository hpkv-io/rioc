"""
Example of batch operations with the HPKV RIOC Python SDK.
"""

import argparse
import os
import time
from datetime import datetime, UTC

from hpkv_rioc import RiocClient, RiocConfig, RiocTlsConfig
from hpkv_rioc.exceptions import RiocError

def get_default_cert_paths():
    """Get default certificate paths from the RIOC certs directory."""
    workspace_root = "/workspaces/kernel-high-performance-kv-store"
    certs_dir = os.path.join(workspace_root, "api/rioc/certs")
    return {
        "client_cert": os.path.join(certs_dir, "client.crt"),
        "client_key": os.path.join(certs_dir, "client.key"),
        "ca_cert": os.path.join(certs_dir, "ca.crt")
    }

def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description="RIOC Batch Example")
    cert_paths = get_default_cert_paths()
    
    parser.add_argument("--host", default="localhost",
                      help="Server host")
    parser.add_argument("--port", type=int, default=8000,
                      help="Server port")
    parser.add_argument("--timeout", type=int, default=5000,
                      help="Operation timeout in milliseconds")
    parser.add_argument("--num-ops", type=int, default=5,
                      help="Number of operations per batch")
    parser.add_argument("--value-size", type=int, default=100,
                      help="Size of values in bytes")
    parser.add_argument("--tls", action="store_true",
                      help="Enable TLS")
    parser.add_argument("--client-cert", default=cert_paths["client_cert"],
                      help="Path to client certificate file")
    parser.add_argument("--client-key", default=cert_paths["client_key"],
                      help="Path to client private key file")
    parser.add_argument("--ca-cert", default=cert_paths["ca_cert"],
                      help="Path to CA certificate file")
    
    args = parser.parse_args()

    # Create TLS configuration if enabled
    tls_config = None
    if args.tls:
        # Verify certificate files exist
        for cert_path in [args.client_cert, args.client_key, args.ca_cert]:
            if not os.path.exists(cert_path):
                print(f"Error: Certificate file not found: {cert_path}")
                return

        tls_config = RiocTlsConfig(
            certificate_path=args.client_cert,
            key_path=args.client_key,
            ca_path=args.ca_cert,
            verify_hostname=args.host,
            verify_peer=True
        )

    # Create client configuration
    config = RiocConfig(
        host=args.host,
        port=args.port,
        timeout_ms=args.timeout,
        tls=tls_config
    )

    print(f"Connecting to {args.host}:{args.port}{' with TLS' if args.tls else ''}...")
    if args.tls:
        print(f"Using certificates:")
        print(f"  Client cert: {args.client_cert}")
        print(f"  Client key:  {args.client_key}")
        print(f"  CA cert:     {args.ca_cert}")

    start_time = datetime.now(UTC)

    try:
        # Create client
        client = RiocClient(config)
        connect_time = datetime.now(UTC) - start_time
        print(f"Connected in {connect_time.total_seconds() * 1000:.2f} ms")

        # Create test value
        value = bytes([ord('A') + (i % 26) for i in range(args.value_size)])

        # Insert multiple key-value pairs in a batch
        print("\n1. Inserting multiple key-value pairs in a batch...")
        start_time = datetime.now(UTC)
        try:
            with client.batch() as batch:
                for i in range(args.num_ops):
                    key = f"key_{i}".encode()
                    batch.add_insert(key, value, client.get_timestamp())
            insert_time = datetime.now(UTC) - start_time
            print(f"Batch insert completed in {insert_time.total_seconds() * 1000:.2f} ms")
        except RiocError as e:
            print(f"Batch insert failed: {e}")
            return

        # Read multiple keys in a batch
        print("\n2. Reading multiple keys in a batch...")
        start_time = datetime.now(UTC)
        try:
            batch = client.create_batch()
            for i in range(args.num_ops):
                key = f"key_{i}".encode()
                batch.add_get(key)

            # Execute batch and get results
            tracker = batch.execute()
            tracker.wait()

            # Process results
            for i in range(args.num_ops):
                result = tracker.get_response(i)
                if result == value:
                    print(f"key_{i} -> [value matches, size: {len(result)} bytes]")
                else:
                    print(f"key_{i} -> [value mismatch, expected size: {len(value)}, got: {len(result)}]")

            get_time = datetime.now(UTC) - start_time
            print(f"Batch get completed in {get_time.total_seconds() * 1000:.2f} ms")
        except RiocError as e:
            print(f"Batch get failed: {e}")
            return

        # Mixed operations in a batch
        print("\n3. Mixed operations in a batch...")
        start_time = datetime.now(UTC)
        try:
            with client.batch() as batch:
                # Delete first key
                batch.add_delete(b"key_0", client.get_timestamp())
                
                # Update second key
                batch.add_insert(b"key_1", b"new-value", client.get_timestamp())
                
                # Get third key
                batch.add_get(b"key_2")
                
                # Insert new key
                batch.add_insert(b"key_new", b"new-key-value", client.get_timestamp())
                
                # Get the new key
                batch.add_get(b"key_new")

            mixed_time = datetime.now(UTC) - start_time
            print(f"Mixed batch operations completed in {mixed_time.total_seconds() * 1000:.2f} ms")
        except RiocError as e:
            print(f"Mixed batch operations failed: {e}")
            return

        # Clean up
        print("\n4. Cleaning up...")
        start_time = datetime.now(UTC)
        try:
            with client.batch() as batch:
                for i in range(args.num_ops):
                    key = f"key_{i}".encode()
                    batch.add_delete(key, client.get_timestamp())
                batch.add_delete(b"key_new", client.get_timestamp())

            cleanup_time = datetime.now(UTC) - start_time
            print(f"Cleanup completed in {cleanup_time.total_seconds() * 1000:.2f} ms")
        except RiocError as e:
            print(f"Cleanup failed: {e}")
            return

        print("\nAll batch operations completed successfully")

    except RiocError as e:
        print(f"Error: {e}")
        if e.code == -3:  # I/O error
            print("This usually means the RIOC server is not running or is not accessible.")
            print("Please make sure the server is running and accessible at the specified host and port.")
        return

if __name__ == "__main__":
    main() 