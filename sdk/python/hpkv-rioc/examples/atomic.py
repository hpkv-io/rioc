#!/usr/bin/env python3
"""
Example demonstrating atomic increment/decrement operations with HPKV RIOC.
"""

import os
import argparse
import time

from hpkv_rioc import RiocClient, RiocConfig, RiocTlsConfig

def parse_args():
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description="HPKV RIOC Atomic Operations Example")
    parser.add_argument("--host", default=os.getenv("RIOC_HOST", "localhost"), help="Server hostname")
    parser.add_argument("--port", type=int, default=int(os.getenv("RIOC_PORT", "8000")), help="Server port")
    parser.add_argument("--timeout", type=int, default=int(os.getenv("RIOC_TIMEOUT", "5000")), help="Timeout in ms")
    parser.add_argument("--tls", action="store_true", help="Enable TLS")
    parser.add_argument("--ca-path", default=os.getenv("RIOC_CA_PATH", ""), help="CA certificate path")
    parser.add_argument("--cert-path", default=os.getenv("RIOC_CERT_PATH", ""), help="Client certificate path")
    parser.add_argument("--key-path", default=os.getenv("RIOC_KEY_PATH", ""), help="Client key path")
    return parser.parse_args()

def main():
    """Run the example."""
    args = parse_args()
    
    # Create TLS config if enabled
    tls_config = None
    if args.tls:
        if not args.ca_path:
            print("Error: --ca-path is required when --tls is enabled")
            return 1
        if not args.cert_path:
            print("Error: --cert-path is required when --tls is enabled")
            return 1
        if not args.key_path:
            print("Error: --key-path is required when --tls is enabled")
            return 1
        
        tls_config = RiocTlsConfig(
            ca_path=args.ca_path,
            certificate_path=args.cert_path,
            key_path=args.key_path,
            verify_hostname=args.host,
            verify_peer=True
        )
    
    # Create client config
    config = RiocConfig(
        host=args.host,
        port=args.port,
        timeout_ms=args.timeout,
        tls=tls_config
    )
    
    print(f"Connecting to {args.host}:{args.port} {'with TLS' if args.tls else 'without TLS'}")
    
    # Create client
    client = RiocClient(config)
    
    try:
        print("\n--- Atomic Operations Example ---\n")
        
        # Create counter keys
        counter1 = "atomic_example_counter1"
        counter2 = "atomic_example_counter2"
        
        # Clean up any existing counters
        try:
            client.delete_string(counter1)
            client.delete_string(counter2)
            print("Cleaned up existing counters\n")
        except:
            # Counters might not exist, that's ok
            pass
        
        # Initialize counter1 with value 10
        print("Initializing counter1 with value 10...")
        value1 = client.atomic_inc_dec_string(counter1, 10)
        print(f"Counter1 value: {value1}\n")
        
        # Increment counter1 by 5
        print("Incrementing counter1 by 5...")
        value2 = client.atomic_inc_dec_string(counter1, 5)
        print(f"Counter1 value: {value2}\n")
        
        # Decrement counter1 by 3
        print("Decrementing counter1 by 3...")
        value3 = client.atomic_inc_dec_string(counter1, -3)
        print(f"Counter1 value: {value3}\n")
        
        # Initialize counter2 with value 20
        print("Initializing counter2 with value 20...")
        value4 = client.atomic_inc_dec_string(counter2, 20)
        print(f"Counter2 value: {value4}\n")
        
        # Use batch operations
        print("Creating batch for atomic operations...")
        batch = client.create_batch()
        
        # Add atomic operations to batch
        timestamp = client.get_timestamp()
        batch.add_atomic_inc_dec(counter1.encode(), 8, timestamp)   # Increment counter1 by 8
        batch.add_atomic_inc_dec(counter2.encode(), -7, timestamp)  # Decrement counter2 by 7
        
        print("Executing batch...")
        tracker = batch.execute()
        tracker.wait()
        
        # Get results
        result1 = tracker.get_atomic_result(0)
        result2 = tracker.get_atomic_result(1)
        
        print(f"Counter1 value (after batch): {result1}")
        print(f"Counter2 value (after batch): {result2}\n")
        
        # Verify values with direct reads
        print("Verifying values with direct reads...")
        verify1 = client.atomic_inc_dec_string(counter1, 0)
        verify2 = client.atomic_inc_dec_string(counter2, 0)
        
        print(f"Counter1 direct read: {verify1}")
        print(f"Counter2 direct read: {verify2}\n")
        
        # Clean up
        print("Cleaning up...")
        client.delete_string(counter1)
        client.delete_string(counter2)
        print("Counters deleted\n")
        
        print("Example completed successfully!")
        return 0
    
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    finally:
        # Always close the client
        client.close()

if __name__ == "__main__":
    import sys
    sys.exit(main()) 