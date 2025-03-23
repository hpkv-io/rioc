#!/usr/bin/env node
import { resolve } from 'path';

interface Args {
  useTls: boolean;
  host: string;
  port: number;
  caPath: string;
  certPath: string;
  keyPath: string;
}

/**
 * Parse command line arguments for examples.
 * @returns Parsed arguments
 */
export function parseArgs(): Args {
  // Check for TLS flag
  const useTls = process.argv.includes('--tls');
  
  // Parse host and port from environment variables or defaults
  const host = process.env.RIOC_HOST || '127.0.0.1';
  const port = parseInt(process.env.RIOC_PORT || '8000', 10);
  
  // Default cert/key paths
  const defaultCertDir = '/workspaces/kernel-high-performance-kv-store/api/rioc/certs';
  
  // Parse cert/key paths from environment variables or defaults
  const caPath = process.env.RIOC_CA_PATH || resolve(defaultCertDir, 'ca.crt');
  const certPath = process.env.RIOC_CERT_PATH || resolve(defaultCertDir, 'client.crt');
  const keyPath = process.env.RIOC_KEY_PATH || resolve(defaultCertDir, 'client.key');
  
  return {
    useTls,
    host,
    port,
    caPath,
    certPath,
    keyPath
  };
} 