"""
Pytest configuration for RIOC tests.
"""

import os
import pytest
from hpkv_rioc import RiocTlsConfig

def get_default_certs_dir():
    """Get the default certificates directory."""
    return "/workspaces/kernel-high-performance-kv-store/api/rioc/certs"

def pytest_addoption(parser):
    """Add custom command line options."""
    parser.addoption(
        "--host",
        action="store",
        default="localhost",
        help="RIOC server host"
    )
    parser.addoption(
        "--port",
        action="store",
        type=int,
        default=8000,
        help="RIOC server port"
    )
    parser.addoption(
        "--timeout",
        action="store",
        type=int,
        default=5000,
        help="Operation timeout in milliseconds"
    )
    parser.addoption(
        "--certs-dir",
        action="store",
        default=get_default_certs_dir(),
        help="Directory containing certificates"
    )

@pytest.fixture(scope="session")
def certs_dir(request):
    """Get the certificates directory."""
    certs_dir = request.config.getoption("--certs-dir")
    if not os.path.exists(certs_dir):
        pytest.skip(f"Certificates directory not found: {certs_dir}")
    return certs_dir

@pytest.fixture(scope="session")
def tls_config(request, certs_dir):
    """Create TLS configuration."""
    cert_path = os.path.join(certs_dir, "client.crt")
    key_path = os.path.join(certs_dir, "client.key")
    ca_path = os.path.join(certs_dir, "ca.crt")
    
    # Verify certificate files exist
    if not os.path.exists(cert_path):
        pytest.skip(f"Client certificate not found: {cert_path}")
    if not os.path.exists(key_path):
        pytest.skip(f"Client key not found: {key_path}")
    if not os.path.exists(ca_path):
        pytest.skip(f"CA certificate not found: {ca_path}")
    
    return RiocTlsConfig(
        certificate_path=cert_path,
        key_path=key_path,
        ca_path=ca_path,
        verify_hostname=request.config.getoption("--host"),
        verify_peer=True
    )

@pytest.fixture
def client(tls_config):
    """Create a RIOC client for testing."""
    from hpkv_rioc import RiocClient, RiocConfig
    config = RiocConfig(
        host=os.getenv("RIOC_TEST_HOST", "localhost"),
        port=int(os.getenv("RIOC_TEST_PORT", "8000")),
        timeout_ms=int(os.getenv("RIOC_TEST_TIMEOUT", "5000")),
        tls=tls_config
    )
    client = RiocClient(config)
    yield client
    client.close()

@pytest.fixture(scope="session", autouse=True)
def set_test_env(request):
    """Set environment variables for tests."""
    os.environ["RIOC_TEST_HOST"] = request.config.getoption("--host")
    os.environ["RIOC_TEST_PORT"] = str(request.config.getoption("--port"))
    os.environ["RIOC_TEST_TIMEOUT"] = str(request.config.getoption("--timeout"))
    os.environ["RIOC_TEST_CERTS_DIR"] = request.config.getoption("--certs-dir") 