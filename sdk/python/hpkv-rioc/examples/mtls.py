"""
Example of using mTLS with the HPKV RIOC Python SDK.
"""

import argparse
import os
from pathlib import Path

from hpkv_rioc import RiocClient, RiocConfig, RiocTlsConfig

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
    parser = argparse.ArgumentParser(description="RIOC mTLS Example")
    cert_paths = get_default_cert_paths()
    
    parser.add_argument("--client-cert", default=cert_paths["client_cert"],
                      help="Path to client certificate file")
    parser.add_argument("--client-key", default=cert_paths["client_key"],
                      help="Path to client private key file")
    parser.add_argument("--ca-cert", default=cert_paths["ca_cert"],
                      help="Path to CA certificate file")
    parser.add_argument("--host", default="localhost",
                      help="Server hostname")
    parser.add_argument("--port", type=int, default=8000,
                      help="Server port")
    
    args = parser.parse_args()

    # Verify certificate files exist
    for cert_path in [args.client_cert, args.client_key, args.ca_cert]:
        if not os.path.exists(cert_path):
            print(f"Error: Certificate file not found: {cert_path}")
            return

    # Create TLS configuration
    tls_config = RiocTlsConfig(
        certificate_path=args.client_cert,
        key_path=args.client_key,
        ca_path=args.ca_cert,
        verify_hostname=args.host,
        verify_peer=True
    )

    # Create client configuration with TLS
    config = RiocConfig(
        host=args.host,
        port=args.port,
        timeout_ms=5000,
        tls=tls_config
    )

    print(f"Connecting to {args.host}:{args.port} with mTLS...")
    print(f"Using certificates:")
    print(f"  Client cert: {args.client_cert}")
    print(f"  Client key:  {args.client_key}")
    print(f"  CA cert:     {args.ca_cert}")

    # Create client
    client = RiocClient(config)

    # Insert some key-value pairs
    print("\nInserting key-value pair...")
    client.insert_string("secure-key", "secure-value")

    # Get value
    print("\nRetrieving value...")
    value = client.get_string("secure-key")
    print(f"secure-key -> {value}")  # secure-key -> secure-value

    # Use batch operations with TLS
    print("\nPerforming batch operations...")
    with client.batch() as batch:
        batch.add_insert(b"key1", b"value1", client.get_timestamp())
        batch.add_get(b"key1")

    print("\nOperations completed successfully!")

if __name__ == "__main__":
    main() 