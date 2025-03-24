"""
Example demonstrating range query functionality in the HPKV RIOC Python SDK.
"""

import argparse
import os
import time
from datetime import datetime, UTC

from hpkv_rioc import RiocClient, RiocConfig, RiocTlsConfig, RangeQueryResult
from hpkv_rioc.exceptions import RiocError

def get_default_cert_paths():
    """Get default certificate paths from the RIOC certs directory."""
    workspace_root = ""
    certs_dir = os.path.join(workspace_root, "")
    return {
        "client_cert": os.path.join(certs_dir, "client.crt"),
        "client_key": os.path.join(certs_dir, "client.key"),
        "ca_cert": os.path.join(certs_dir, "ca.crt")
    }

def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description="RIOC Range Query Example")
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
                return 1
        
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

    # Print configuration
    print("Client Configuration:")
    print(f"  Host:      {config.host}")
    print(f"  Port:      {config.port}")
    print(f"  Timeout:   {config.timeout_ms} ms")
    print(f"  TLS:       {'enabled' if config.tls else 'disabled'}")
    if config.tls:
        print(f"  CA Path:   {config.tls.ca_path}")
        print(f"  Cert Path: {config.tls.certificate_path}")
        print(f"  Key Path:  {config.tls.key_path}")

    # Connect to server
    print("\nConnecting to server...")
    start_time = time.time()
    client = RiocClient(config)
    connect_time = (time.time() - start_time) * 1000
    print(f"Connected in {connect_time:.2f} ms")

    try:
        # Insert some test data
        print("\nInserting test data...")
        timestamp = RiocClient.get_timestamp()
        
        fruits = [
            {"key": "fruit:apple", "value": "A red fruit"},
            {"key": "fruit:banana", "value": "A yellow fruit"},
            {"key": "fruit:cherry", "value": "A small red fruit"},
            {"key": "fruit:date", "value": "A sweet brown fruit"},
            {"key": "fruit:elderberry", "value": "A purple berry"},
            {"key": "fruit:fig", "value": "A sweet Mediterranean fruit"},
            {"key": "fruit:grape", "value": "A small juicy fruit"},
            {"key": "vegetable:carrot", "value": "An orange root vegetable"},
            {"key": "vegetable:potato", "value": "A starchy tuber"}
        ]
        
        # Insert all items
        for i, item in enumerate(fruits):
            client.insert_string(item["key"], item["value"], timestamp + i)
            print(f"  Inserted: {item['key']}")
        
        # Perform a range query for all fruits
        print("\nPerforming range query for all fruits...")
        start_time = time.time()
        results = client.range_query_string("fruit:", "fruit:\xff")
        query_time = (time.time() - start_time) * 1000
        print(f"Range query completed in {query_time:.2f} ms")
        
        print(f"Found {len(results)} results:")
        for key, value in results:
            print(f"  {key} => {value}")
        
        # Perform a range query for a subset of fruits
        print("\nPerforming range query for fruits from banana to elderberry...")
        start_time = time.time()
        results = client.range_query_string("fruit:banana", "fruit:elderberry")
        query_time = (time.time() - start_time) * 1000
        print(f"Subset range query completed in {query_time:.2f} ms")
        
        print(f"Found {len(results)} results:")
        for key, value in results:
            print(f"  {key} => {value}")
        
        # Demonstrate batch range query
        print("\nPerforming batch range query...")
        batch = client.create_batch()
        
        # Add a range query for fruits
        batch.add_range_query(b"fruit:", b"fruit:\xff")
        
        # Add a range query for vegetables
        batch.add_range_query(b"vegetable:", b"vegetable:\xff")
        
        start_time = time.time()
        tracker = batch.execute()
        tracker.wait()
        batch_time = (time.time() - start_time) * 1000
        print(f"Batch execution completed in {batch_time:.2f} ms")
        
        # Get results for the first range query (fruits)
        fruit_results = tracker.get_range_query_response(0)
        print(f"\nFruit results ({len(fruit_results)}):")
        for result in fruit_results:
            key = result.key.decode("utf-8")
            value = result.value.decode("utf-8")
            print(f"  {key} => {value}")
        
        # Get results for the second range query (vegetables)
        veg_results = tracker.get_range_query_response(1)
        print(f"\nVegetable results ({len(veg_results)}):")
        for result in veg_results:
            key = result.key.decode("utf-8")
            value = result.value.decode("utf-8")
            print(f"  {key} => {value}")
        
        # Clean up resources
        tracker.close()
        batch.close()
        
    except RiocError as e:
        print(f"Error: {e}")
        return 1
    finally:
        # Clean up
        client.close()
        print("\nClient disposed")
    
    return 0

if __name__ == "__main__":
    exit(main()) 