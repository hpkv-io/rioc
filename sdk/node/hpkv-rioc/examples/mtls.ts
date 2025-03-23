import { RiocClient, RiocConfig } from '../src';

async function main() {
  // Create client configuration with mTLS
  const config: RiocConfig = {
    host: '127.0.0.1',
    port: 8000,
    timeoutMs: 5000,
    tls: {
      caPath: '/workspaces/kernel-high-performance-kv-store/api/rioc/certs/ca.crt',
      certificatePath: '/workspaces/kernel-high-performance-kv-store/api/rioc/certs/client.crt',
      keyPath: '/workspaces/kernel-high-performance-kv-store/api/rioc/certs/client.key',
      verifyHostname: '127.0.0.1',
      verifyPeer: true
    }
  };

  console.log('Client Configuration:');
  console.log(`  Host:      ${config.host}`);
  console.log(`  Port:      ${config.port}`);
  console.log(`  CA Path:   ${config.tls?.caPath}`);
  console.log(`  Cert Path: ${config.tls?.certificatePath}`);
  console.log(`  Key Path:  ${config.tls?.keyPath}`);
  console.log('\nConnecting to server...');

  const startTime = process.hrtime.bigint();

  // Create client
  const client = new RiocClient(config);
  const connectTime = Number(process.hrtime.bigint() - startTime) / 1_000_000;
  console.log(`Connected in ${connectTime.toFixed(2)} ms`);

  try {
    // Test data
    const key = Buffer.from('mtls_test_key');
    const value = Buffer.from('mtls test value');

    // Insert
    const timestamp = RiocClient.getTimestamp();
    console.log(`\n1. Inserting record with timestamp ${timestamp}`);
    const insertStart = process.hrtime.bigint();
    client.insert(key, value, timestamp);
    const insertTime = Number(process.hrtime.bigint() - insertStart) / 1_000_000;
    console.log(`Insert successful in ${insertTime.toFixed(2)} ms`);

    // Get
    console.log('\n2. Getting record');
    const getStart = process.hrtime.bigint();
    const retrievedValue = client.get(key);
    const getTime = Number(process.hrtime.bigint() - getStart) / 1_000_000;
    if (retrievedValue === null) {
      console.log(`Key not found (took ${getTime.toFixed(2)} ms)`);
    } else {
      console.log(`Get successful in ${getTime.toFixed(2)} ms, value: ${retrievedValue.toString()}`);
    }

    // Delete
    const deleteTimestamp = RiocClient.getTimestamp();
    console.log('\n3. Deleting record');
    const deleteStart = process.hrtime.bigint();
    client.delete(key, deleteTimestamp);
    const deleteTime = Number(process.hrtime.bigint() - deleteStart) / 1_000_000;
    console.log(`Delete successful in ${deleteTime.toFixed(2)} ms`);

  } finally {
    client.dispose();
    console.log('\nTest completed');
  }
}

main().catch(error => {
  console.error('Error:', error);
  process.exit(1);
}); 