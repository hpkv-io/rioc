import { RiocClient, RiocConfig, RiocTlsConfig } from '../src';

async function main() {
  // Parse command line arguments
  const useTls = process.argv.includes('--tls');
  const host = process.argv.includes('--host') ? process.argv[process.argv.indexOf('--host') + 1] : 'localhost';
  const port = process.argv.includes('--port') ? parseInt(process.argv[process.argv.indexOf('--port') + 1]) : 8000;

  // Create client configuration
  const config: RiocConfig = {
    host,
    port,
    timeoutMs: 5000
  };

  // Add TLS configuration if enabled
  if (useTls) {
    config.tls = {
      caPath: process.env.RIOC_CA_PATH || '/workspaces/kernel-high-performance-kv-store/api/rioc/certs/ca.crt',
      certificatePath: process.env.RIOC_CERT_PATH || '/workspaces/kernel-high-performance-kv-store/api/rioc/certs/client.crt',
      keyPath: process.env.RIOC_KEY_PATH || '/workspaces/kernel-high-performance-kv-store/api/rioc/certs/client.key',
      verifyHostname: host,
      verifyPeer: true
    };
  }

  console.log('Client Configuration:');
  console.log(`  Host:      ${config.host}`);
  console.log(`  Port:      ${config.port}`);
  console.log(`  TLS:       ${useTls ? 'enabled' : 'disabled'}`);
  if (useTls) {
    console.log(`  CA Path:   ${config.tls?.caPath}`);
    console.log(`  Cert Path: ${config.tls?.certificatePath}`);
    console.log(`  Key Path:  ${config.tls?.keyPath}`);
  }
  console.log('\nConnecting to server...');
  const startTime = process.hrtime.bigint();

  // Create client
  const client = new RiocClient(config);
  const connectTime = Number(process.hrtime.bigint() - startTime) / 1_000_000;
  console.log(`Connected in ${connectTime.toFixed(2)} ms`);

  try {
    // Test data
    const key = Buffer.from('test_key');
    const initialValue = Buffer.from('initial value');
    const newValue = Buffer.from('updated value');

    // Get initial timestamp
    const timestamp = RiocClient.getTimestamp();
    console.log(`\n1. Inserting record with timestamp ${timestamp}`);
    const insertStart = process.hrtime.bigint();
    client.insert(key, initialValue, timestamp);
    const insertTime = Number(process.hrtime.bigint() - insertStart) / 1_000_000;
    console.log(`Insert successful in ${insertTime.toFixed(2)} ms`);

    // Sleep briefly to ensure timestamp increases
    await new Promise(resolve => setTimeout(resolve, 1));

    console.log('\n2. Getting record');
    const getStart = process.hrtime.bigint();
    const value = client.get(key);
    const getTime = Number(process.hrtime.bigint() - getStart) / 1_000_000;
    if (value === null) {
      console.log(`Key not found (took ${getTime.toFixed(2)} ms)`);
    } else {
      console.log(`Get successful in ${getTime.toFixed(2)} ms, value: ${value.toString()}`);
    }

    // Sleep briefly to ensure timestamp increases
    await new Promise(resolve => setTimeout(resolve, 1));

    // Update
    const updateTimestamp = RiocClient.getTimestamp();
    console.log(`\n3. Updating record with timestamp ${updateTimestamp}`);
    const updateStart = process.hrtime.bigint();
    client.insert(key, newValue, updateTimestamp);
    const updateTime = Number(process.hrtime.bigint() - updateStart) / 1_000_000;
    console.log(`Update successful in ${updateTime.toFixed(2)} ms`);

    // Sleep briefly to ensure timestamp increases
    await new Promise(resolve => setTimeout(resolve, 1));

    console.log('\n4. Getting updated record');
    const getUpdatedStart = process.hrtime.bigint();
    const retrievedValue = client.get(key);
    const getUpdatedTime = Number(process.hrtime.bigint() - getUpdatedStart) / 1_000_000;
    if (retrievedValue === null) {
      console.log(`Key not found (took ${getUpdatedTime.toFixed(2)} ms)`);
    } else {
      console.log(`Get successful in ${getUpdatedTime.toFixed(2)} ms, value: ${retrievedValue.toString()}`);
    }

    // Sleep briefly to ensure timestamp increases
    await new Promise(resolve => setTimeout(resolve, 1));

    // Delete
    const deleteTimestamp = RiocClient.getTimestamp();
    console.log('\n5. Deleting record');
    const deleteStart = process.hrtime.bigint();
    client.delete(key, deleteTimestamp);
    const deleteTime = Number(process.hrtime.bigint() - deleteStart) / 1_000_000;
    console.log(`Delete successful in ${deleteTime.toFixed(2)} ms`);

    // Test get after delete
    console.log('\n6. Getting deleted record');
    const getDeletedStart = process.hrtime.bigint();
    const deletedValue = client.get(key);
    const getDeletedTime = Number(process.hrtime.bigint() - getDeletedStart) / 1_000_000;
    if (deletedValue === null) {
      console.log(`Get after delete correctly returned null in ${getDeletedTime.toFixed(2)} ms`);
    } else {
      console.log(`Unexpected: Get after delete returned value in ${getDeletedTime.toFixed(2)} ms`);
    }
  } finally {
    const disposeStart = process.hrtime.bigint();
    client.dispose();
    const disposeTime = Number(process.hrtime.bigint() - disposeStart) / 1_000_000;
    console.log(`\nAll tests completed successfully (cleanup took ${disposeTime.toFixed(2)} ms)`);
  }
}

main().catch(error => {
  console.error('Error:', error);
  process.exit(1);
}); 