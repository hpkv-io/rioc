/**
 * TLS configuration options for RIOC client.
 */
export interface RiocTlsConfig {
  /**
   * Path to the CA certificate file for verifying the server.
   */
  caPath?: string;

  /**
   * Path to the client certificate file for mTLS.
   */
  certificatePath?: string;

  /**
   * Path to the client private key file for mTLS.
   */
  keyPath?: string;

  /**
   * Hostname to verify in the server's certificate.
   */
  verifyHostname?: string;

  /**
   * Whether to verify the server's certificate.
   * @default true
   */
  verifyPeer?: boolean;
}

/**
 * Configuration options for RIOC client.
 */
export interface RiocConfig {
  /**
   * The host to connect to.
   */
  host: string;

  /**
   * The port to connect to.
   */
  port: number;

  /**
   * Operation timeout in milliseconds.
   * @default 5000
   */
  timeoutMs?: number;

  /**
   * TLS configuration.
   */
  tls?: RiocTlsConfig;
}