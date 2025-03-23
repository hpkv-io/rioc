"""
Tests for RIOC TLS functionality.
"""

import os
import pytest
from datetime import datetime, UTC

from hpkv_rioc import RiocClient, RiocConfig, RiocTlsConfig, RiocError

def get_certs_dir():
    """Get the certificates directory."""
    certs_dir = os.getenv("RIOC_TEST_CERTS_DIR", "/workspaces/kernel-high-performance-kv-store/api/rioc/certs")
    if not os.path.exists(certs_dir):
        pytest.skip(f"Certificates directory not found: {certs_dir}")
    return certs_dir

def get_test_host():
    """Get the test host."""
    return os.getenv("RIOC_TEST_HOST", "localhost")

def get_test_port():
    """Get the test port."""
    return int(os.getenv("RIOC_TEST_PORT", "8000"))

def get_test_timeout():
    """Get the test timeout."""
    return int(os.getenv("RIOC_TEST_TIMEOUT", "5000"))

@pytest.fixture(scope="module")
def certs_dir():
    """Get the certificates directory."""
    return get_certs_dir()

@pytest.fixture(scope="module")
def test_host():
    """Get the test host."""
    return get_test_host()

@pytest.fixture(scope="module")
def test_port():
    """Get the test port."""
    return get_test_port()

@pytest.fixture(scope="module")
def test_timeout():
    """Get the test timeout."""
    return get_test_timeout()

@pytest.fixture
def client(certs_dir, test_host, test_port, test_timeout):
    """Create a RIOC client with TLS for testing."""
    tls_config = RiocTlsConfig(
        certificate_path=os.path.join(certs_dir, "client.crt"),
        key_path=os.path.join(certs_dir, "client.key"),
        ca_path=os.path.join(certs_dir, "ca.crt"),
        verify_hostname=test_host,
        verify_peer=True
    )
    config = RiocConfig(
        host=test_host,
        port=test_port,
        timeout_ms=test_timeout,
        tls=tls_config
    )
    client = RiocClient(config)
    yield client
    client.close()

def test_tls_connection(client):
    """Test that client can connect with TLS."""
    # Connection is established in fixture
    assert client is not None

def test_tls_basic(client):
    """Test basic TLS functionality."""
    key = "test_tls_key"
    value = "test_tls_value"

    # Insert
    client.insert_string(key, value)

    # Get
    result = client.get_string(key)
    assert result == value

    # Delete
    client.delete_string(key)

    # Verify deleted
    with pytest.raises(RiocError) as exc_info:
        client.get_string(key)
    assert exc_info.value.code == -6  # Not found

def test_tls_multiple_operations(client):
    """Test multiple operations over TLS."""
    for i in range(10):
        key = f"test_tls_key_{i}"
        value = f"test_tls_value_{i}"

        # Insert
        client.insert_string(key, value)

        # Get
        result = client.get_string(key)
        assert result == value

        # Delete
        client.delete_string(key)

def test_tls_concurrent_operations(client):
    """Test concurrent operations over TLS."""
    import threading
    import queue

    def worker(q, client, start, count):
        try:
            for i in range(start, start + count):
                key = f"test_tls_concurrent_{i}"
                value = f"test_tls_value_{i}"

                # Insert
                client.insert_string(key, value)

                # Get
                result = client.get_string(key)
                assert result == value

                # Delete
                client.delete_string(key)
        except Exception as e:
            q.put(e)
        else:
            q.put(None)

    threads = []
    results = queue.Queue()

    # Start 4 threads, each doing 25 operations
    for i in range(4):
        t = threading.Thread(
            target=worker,
            args=(results, client, i * 25, 25)
        )
        threads.append(t)
        t.start()

    # Wait for all threads
    for t in threads:
        t.join(timeout=30)
        assert not t.is_alive(), "Thread timed out"

    # Check for errors
    while not results.empty():
        result = results.get()
        assert result is None, f"Worker thread failed: {result}"

def test_tls_large_data(client):
    """Test handling large data over TLS."""
    key = "test_tls_large_key"
    # Use 4KB value
    value = "x" * (4 * 1024)  # 4KB string

    # Insert
    client.insert_string(key, value)

    # Get
    result = client.get_string(key)
    assert result == value

    # Delete
    client.delete_string(key)

    # Note: Server accepts larger values, so we don't test size limits here

def test_tls_config_validation(certs_dir, test_host):
    """Test TLS configuration validation."""
    # Test missing certificate
    with pytest.raises(RiocError) as exc_info:
        config = RiocConfig(
            host=test_host,
            port=get_test_port(),
            timeout_ms=get_test_timeout(),
            tls=RiocTlsConfig(
                certificate_path=os.path.join(certs_dir, "nonexistent.crt"),
                key_path=os.path.join(certs_dir, "client.key"),
                ca_path=os.path.join(certs_dir, "ca.crt"),
                verify_hostname=test_host,
                verify_peer=True
            )
        )
        RiocClient(config)
    assert exc_info.value.code == -3  # I/O error

def test_tls_without_client_cert(certs_dir, test_host):
    """Test TLS without client certificate (server auth only)."""
    with pytest.raises(RiocError) as exc_info:
        config = RiocConfig(
            host=test_host,
            port=get_test_port(),
            timeout_ms=get_test_timeout(),
            tls=RiocTlsConfig(
                ca_path=os.path.join(certs_dir, "ca.crt"),
                verify_hostname=test_host,
                verify_peer=True
            )
        )
        RiocClient(config)
    assert exc_info.value.code == -3  # I/O error

def test_tls_without_verification(certs_dir, test_host):
    """Test TLS without certificate verification."""
    config = RiocConfig(
        host=test_host,
        port=get_test_port(),
        timeout_ms=get_test_timeout(),
        tls=RiocTlsConfig(
            certificate_path=os.path.join(certs_dir, "client.crt"),
            key_path=os.path.join(certs_dir, "client.key"),
            ca_path=os.path.join(certs_dir, "ca.crt"),
            verify_hostname=None,  # No hostname verification
            verify_peer=False  # No peer verification
        )
    )
    client = RiocClient(config)
    assert client is not None
    client.close()

def test_tls_wrong_hostname(certs_dir):
    """Test TLS with wrong hostname verification."""
    with pytest.raises(RiocError) as exc_info:
        config = RiocConfig(
            host="wrong.host.name",  # Use wrong hostname for connection
            port=get_test_port(),
            timeout_ms=get_test_timeout(),
            tls=RiocTlsConfig(
                certificate_path=os.path.join(certs_dir, "client.crt"),
                key_path=os.path.join(certs_dir, "client.key"),
                ca_path=os.path.join(certs_dir, "ca.crt"),
                verify_hostname="wrong.host.name",  # Match the wrong hostname
                verify_peer=True
            )
        )
        RiocClient(config)  # Should fail because the server's cert doesn't match wrong.host.name
    assert exc_info.value.code == -3  # I/O error

def test_tls_multiple_clients(certs_dir, test_host, test_port, test_timeout):
    """Test multiple TLS clients."""
    clients = []
    for i in range(10):  # Create 10 concurrent clients
        config = RiocConfig(
            host=test_host,
            port=test_port,
            timeout_ms=test_timeout,
            tls=RiocTlsConfig(
                certificate_path=os.path.join(certs_dir, "client.crt"),
                key_path=os.path.join(certs_dir, "client.key"),
                ca_path=os.path.join(certs_dir, "ca.crt"),
                verify_hostname=test_host,
                verify_peer=True
            )
        )
        client = RiocClient(config)
        clients.append(client)
        
        # Basic test for each client
        key = f"test_multi_tls_{i}"
        value = f"value_{i}"
        client.insert_string(key, value)
        assert client.get_string(key) == value
        client.delete_string(key)
    
    # Cleanup
    for client in clients:
        client.close() 