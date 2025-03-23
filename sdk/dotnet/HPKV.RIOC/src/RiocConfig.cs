namespace HPKV.RIOC;

/// <summary>
/// Configuration for RIOC client TLS settings.
/// </summary>
public class RiocTlsConfig
{
    /// <summary>
    /// Path to the client certificate file.
    /// </summary>
    public string? CertificatePath { get; set; }

    /// <summary>
    /// Path to the client private key file.
    /// </summary>
    public string? KeyPath { get; set; }

    /// <summary>
    /// Path to the CA certificate file for verifying the server and/or client certificates.
    /// </summary>
    public string? CaPath { get; set; }

    /// <summary>
    /// Hostname to verify in the server's certificate.
    /// </summary>
    public string? VerifyHostname { get; set; }

    /// <summary>
    /// Whether to verify peer certificates. If true, CaPath must be provided.
    /// </summary>
    public bool VerifyPeer { get; set; } = true;
}

/// <summary>
/// Configuration for RIOC client connection.
/// </summary>
public class RiocConfig
{
    /// <summary>
    /// The host to connect to.
    /// </summary>
    public required string Host { get; set; }

    /// <summary>
    /// The port to connect to.
    /// </summary>
    public required int Port { get; set; }

    /// <summary>
    /// Operation timeout in milliseconds.
    /// </summary>
    public int TimeoutMs { get; set; } = 5000;

    /// <summary>
    /// TLS configuration. If null, TLS will not be used.
    /// When using mTLS, both client and CA certificates must be provided.
    /// </summary>
    public RiocTlsConfig? Tls { get; set; }
} 