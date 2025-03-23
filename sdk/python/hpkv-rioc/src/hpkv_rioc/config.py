"""
RIOC configuration classes
"""

from dataclasses import dataclass
from typing import Optional

@dataclass
class RiocTlsConfig:
    """TLS configuration for RIOC client."""
    certificate_path: Optional[str] = None
    """Path to the client certificate file."""

    key_path: Optional[str] = None
    """Path to the client private key file."""

    ca_path: Optional[str] = None
    """Path to the CA certificate file for verifying the server and/or client certificates."""

    verify_hostname: Optional[str] = None
    """Hostname to verify in the server's certificate."""

    verify_peer: bool = True
    """Whether to verify peer certificates. If true, ca_path must be provided."""

@dataclass
class RiocConfig:
    """Configuration for RIOC client connection."""
    host: str
    """The host to connect to."""

    port: int
    """The port to connect to."""

    timeout_ms: int = 5000
    """Operation timeout in milliseconds."""

    tls: Optional[RiocTlsConfig] = None
    """TLS configuration. If None, TLS will not be used.
    When using mTLS, both client and CA certificates must be provided.""" 