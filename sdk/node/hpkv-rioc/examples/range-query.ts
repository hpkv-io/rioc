import { RiocClient, RiocConfig, RiocTlsConfig, RangeQueryResult } from '../src';

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
    // Insert some test data
    console.log('\nInserting test data...');
    const timestamp = RiocClient.getTimestamp();
    
    const fruits = [
      { key: 'fruit:apple', value: 'A red fruit' },
      { key: 'fruit:banana', value: 'A yellow fruit' },
      { key: 'fruit:cherry', value: 'A small red fruit' },
      { key: 'fruit:date', value: 'A sweet brown fruit' },
      { key: 'fruit:elderberry', value: 'A purple berry' },
      { key: 'fruit:fig', value: 'A sweet Mediterranean fruit' },
      { key: 'fruit:grape', value: 'A small juicy fruit' },
      { key: 'vegetable:carrot', value: 'An orange root vegetable' },
      { key: 'vegetable:potato', value: 'A starchy tuber' }
    ];
    
    // Insert all items
    for (const item of fruits) {
      const key = Buffer.from(item.key);
      const value = Buffer.from(item.value);
      client.insert(key, value, timestamp + BigInt(fruits.indexOf(item)));
      console.log(`  Inserted: ${item.key}`);
    }
    
    // Perform a range query for all fruits
    console.log('\nPerforming range query for all fruits...');
    const startKey = Buffer.from('fruit:');
    const endKey = Buffer.from('fruit:\xFF'); // \xFF is the highest possible byte value
    
    console.time('Range query');
    const results = client.rangeQuery(startKey, endKey);
    console.timeEnd('Range query');
    
    console.log(`Found ${results.length} results:`);
    for (const result of results) {
      const key = result.key.toString();
      const value = result.value.toString();
      console.log(`  ${key} => ${value}`);
    }
    
    // Perform a range query for a subset of fruits
    console.log('\nPerforming range query for fruits from banana to elderberry...');
    const subsetStartKey = Buffer.from('fruit:banana');
    const subsetEndKey = Buffer.from('fruit:elderberry');
    
    console.time('Subset range query');
    const subsetResults = client.rangeQuery(subsetStartKey, subsetEndKey);
    console.timeEnd('Subset range query');
    
    console.log(`Found ${subsetResults.length} results:`);
    for (const result of subsetResults) {
      const key = result.key.toString();
      const value = result.value.toString();
      console.log(`  ${key} => ${value}`);
    }
    
    // Demonstrate batch range query
    console.log('\nPerforming batch range query...');
    const batch = client.createBatch();
    
    // Add a range query for fruits
    batch.addRangeQuery(startKey, endKey);
    
    // Add a range query for vegetables
    const vegStartKey = Buffer.from('vegetable:');
    const vegEndKey = Buffer.from('vegetable:\xFF');
    batch.addRangeQuery(vegStartKey, vegEndKey);
    
    console.time('Batch execution');
    const tracker = batch.executeAsync();
    tracker.wait();
    console.timeEnd('Batch execution');
    
    // Get results for the first range query (fruits)
    const fruitResults = tracker.getRangeQueryResponse(0);
    console.log(`\nFruit results (${fruitResults.length}):`);
    for (const result of fruitResults) {
      const key = result.key.toString();
      const value = result.value.toString();
      console.log(`  ${key} => ${value}`);
    }
    
    // Get results for the second range query (vegetables)
    const vegResults = tracker.getRangeQueryResponse(1);
    console.log(`\nVegetable results (${vegResults.length}):`);
    for (const result of vegResults) {
      const key = result.key.toString();
      const value = result.value.toString();
      console.log(`  ${key} => ${value}`);
    }
    
    // Clean up resources
    tracker.dispose();
    batch.dispose();
    
  } finally {
    // Clean up
    client.dispose();
    console.log('\nClient disposed');
  }
}

main().catch(err => {
  console.error('Error:', err);
  process.exit(1);
}); 