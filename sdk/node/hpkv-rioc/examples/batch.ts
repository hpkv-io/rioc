import { RiocClient, RiocConfig } from '../src';

async function main() {
  // Parse command line arguments
  const useTls = process.argv.includes('--tls');
  const host = process.argv.includes('--host') ? process.argv[process.argv.indexOf('--host') + 1] : '127.0.0.1';
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
      caPath: process.env.RIOC_CA_PATH || '',
      certificatePath: process.env.RIOC_CERT_PATH || '',
      keyPath: process.env.RIOC_KEY_PATH || '',
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
    // Create batch
    const batch = client.createBatch();

    // Add operations to batch
    const timestamp = RiocClient.getTimestamp();
    console.log(`\nAdding operations to batch with timestamp ${timestamp}`);

    // Add some inserts
    for (let i = 0; i < 5; i++) {
      const key = Buffer.from(`key_${i}`);
      const value = Buffer.from(`value_${i}`);
      batch.addInsert(key, value, timestamp + BigInt(i));
    }

    // Execute batch
    console.log('\nExecuting batch...');
    const executeStart = process.hrtime.bigint();
    const tracker = batch.executeAsync();
    tracker.wait();
    const executeTime = Number(process.hrtime.bigint() - executeStart) / 1_000_000;
    console.log(`Batch execution completed in ${executeTime.toFixed(2)} ms`);

    // Create a new batch for gets
    const getBatch = client.createBatch();
    console.log('\nReading back values...');

    // Add get operations
    for (let i = 0; i < 5; i++) {
      const key = Buffer.from(`key_${i}`);
      getBatch.addGet(key);
    }

    // Execute get batch
    const getStart = process.hrtime.bigint();
    const getTracker = getBatch.executeAsync();
    getTracker.wait();
    const getTime = Number(process.hrtime.bigint() - getStart) / 1_000_000;
    console.log(`Batch get completed in ${getTime.toFixed(2)} ms`);

    // Print results
    console.log('\nResults:');
    for (let i = 0; i < 5; i++) {
      const value = getTracker.getResponse(i);
      if (value === null) {
        console.log(`key_${i}: not found`);
      } else {
        console.log(`key_${i}: ${value.toString()}`);
      }
    }

    // Create delete batch
    const deleteBatch = client.createBatch();
    const deleteTimestamp = RiocClient.getTimestamp();
    console.log(`\nDeleting keys with timestamp ${deleteTimestamp}`);

    // Add delete operations
    for (let i = 0; i < 5; i++) {
      const key = Buffer.from(`key_${i}`);
      deleteBatch.addDelete(key, deleteTimestamp + BigInt(i));
    }

    // Execute delete batch
    const deleteStart = process.hrtime.bigint();
    const deleteTracker = deleteBatch.executeAsync();
    deleteTracker.wait();
    const deleteTime = Number(process.hrtime.bigint() - deleteStart) / 1_000_000;
    console.log(`Batch delete completed in ${deleteTime.toFixed(2)} ms`);

    // Cleanup
    tracker.dispose();
    getTracker.dispose();
    deleteTracker.dispose();
    batch.dispose();
    getBatch.dispose();
    deleteBatch.dispose();

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