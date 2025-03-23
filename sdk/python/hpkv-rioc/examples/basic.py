"""
Basic example of using the HPKV RIOC Python SDK.
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
    parser = argparse.ArgumentParser(description="RIOC Basic Example")
    cert_paths = get_default_cert_paths()
    
    parser.add_argument("--host", default="localhost",
                      help="Server host")
    parser.add_argument("--port", type=int, default=8000,
                      help="Server port")
    parser.add_argument("--timeout", type=int, default=5000,
                      help="Operation timeout in milliseconds")
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

        # Test data
        key = "test_key"
        initial_value = "initial value"
        updated_value = "updated value"

        # Get initial timestamp
        timestamp = client.get_timestamp()
        print(f"\n1. Inserting record with timestamp {timestamp}")
        start_time = datetime.now(UTC)
        try:
            client.insert_string(key, initial_value, timestamp)
            insert_time = datetime.now(UTC) - start_time
            print(f"Insert successful in {insert_time.total_seconds() * 1000:.2f} ms")
        except RiocError as e:
            print(f"Insert failed: {e}")
            if e.code == -3:  # I/O error
                print("This usually means the RIOC server is not running or is not accessible.")
                print("Please make sure the server is running and accessible at the specified host and port.")
            return

        # Sleep briefly to ensure timestamp increases
        time.sleep(0.001)

        print("\n2. Getting record")
        start_time = datetime.now(UTC)
        try:
            value = client.get_string(key)
            get_time = datetime.now(UTC) - start_time
            print(f"Get successful in {get_time.total_seconds() * 1000:.2f} ms, value: {value}")
        except RiocError as e:
            get_time = datetime.now(UTC) - start_time
            print(f"Get failed: {e} (took {get_time.total_seconds() * 1000:.2f} ms)")
            return

        # Sleep briefly to ensure timestamp increases
        time.sleep(0.001)

        # Full update
        timestamp = client.get_timestamp()
        print(f"\n3. Updating record with timestamp {timestamp}")
        start_time = datetime.now(UTC)
        try:
            client.insert_string(key, updated_value, timestamp)
            update_time = datetime.now(UTC) - start_time
            print(f"Update successful in {update_time.total_seconds() * 1000:.2f} ms")
        except RiocError as e:
            print(f"Update failed: {e}")
            return

        # Sleep briefly to ensure timestamp increases
        time.sleep(0.001)

        print("\n4. Getting updated record")
        start_time = datetime.now(UTC)
        try:
            value = client.get_string(key)
            get_time = datetime.now(UTC) - start_time
            print(f"Get successful in {get_time.total_seconds() * 1000:.2f} ms, value: {value}")
        except RiocError as e:
            get_time = datetime.now(UTC) - start_time
            print(f"Get failed: {e} (took {get_time.total_seconds() * 1000:.2f} ms)")
            return

        # Sleep briefly to ensure timestamp increases
        time.sleep(0.001)

        # Test delete
        print("\n5. Deleting record")
        timestamp = client.get_timestamp()
        start_time = datetime.now(UTC)
        try:
            client.delete_string(key, timestamp)
            delete_time = datetime.now(UTC) - start_time
            print(f"Delete successful in {delete_time.total_seconds() * 1000:.2f} ms")
        except RiocError as e:
            print(f"Delete failed: {e}")
            return

        # Test get after delete
        print("\n6. Getting deleted record")
        start_time = datetime.now(UTC)
        try:
            value = client.get_string(key)
            get_time = datetime.now(UTC) - start_time
            print(f"Get unexpectedly succeeded in {get_time.total_seconds() * 1000:.2f} ms")
        except RiocError as e:
            get_time = datetime.now(UTC) - start_time
            print(f"Get after delete correctly returned error in {get_time.total_seconds() * 1000:.2f} ms")

        print("\nAll tests completed successfully")

    except RiocError as e:
        print(f"Error: {e}")
        if e.code == -3:  # I/O error
            print("This usually means the RIOC server is not running or is not accessible.")
            print("Please make sure the server is running and accessible at the specified host and port.")
        return

if __name__ == "__main__":
    main() 