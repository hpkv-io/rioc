#!/usr/bin/env node
import { RiocClient, RiocConfig } from '../src';
import { parseArgs } from './utils/args';

// Parse command line arguments
const { useTls, host, port, caPath, certPath, keyPath } = parseArgs();

// Create client configuration
const config: RiocConfig = {
  host,
  port,
  timeoutMs: 5000
};

// Add TLS configuration if enabled
if (useTls) {
  config.tls = {
    caPath,
    certificatePath: certPath,
    keyPath,
    verifyHostname: host,
    verifyPeer: true
  };
}

// Log configuration
console.log('Configuration:');
console.log(`  Host: ${host}`);
console.log(`  Port: ${port}`);
console.log(`  TLS: ${useTls ? 'enabled' : 'disabled'}`);
if (useTls) {
  console.log(`  CA Path: ${caPath}`);
  console.log(`  Certificate Path: ${certPath}`);
  console.log(`  Key Path: ${keyPath}`);
}

// Create client
const client = new RiocClient(config);

// Function to pause execution
const sleep = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));

async function runExample() {
  try {
    console.log('\n--- Atomic Operations Example ---\n');
    
    // Create counters
    const counter1 = Buffer.from('atomic_example_counter1');
    const counter2 = Buffer.from('atomic_example_counter2');
    
    // Clean up any existing counters from previous runs
    try {
      client.delete(counter1, RiocClient.getTimestamp());
      client.delete(counter2, RiocClient.getTimestamp());
      console.log('Cleaned up existing counters\n');
    } catch (err) {
      // Counters might not exist, that's ok
    }
    
    // Initialize counter1 with value 10
    console.log('Initializing counter1 with value 10...');
    const value1 = client.atomicIncDec(counter1, 10, RiocClient.getTimestamp());
    console.log(`Counter1 value: ${value1}\n`);
    
    // Increment counter1 by 5
    console.log('Incrementing counter1 by 5...');
    const value2 = client.atomicIncDec(counter1, 5, RiocClient.getTimestamp());
    console.log(`Counter1 value: ${value2}\n`);
    
    // Decrement counter1 by 3
    console.log('Decrementing counter1 by 3...');
    const value3 = client.atomicIncDec(counter1, -3, RiocClient.getTimestamp());
    console.log(`Counter1 value: ${value3}\n`);
    
    // Initialize counter2 with value 20
    console.log('Initializing counter2 with value 20...');
    const value4 = client.atomicIncDec(counter2, 20, RiocClient.getTimestamp());
    console.log(`Counter2 value: ${value4}\n`);
    
    // Batch operations
    console.log('Creating batch for atomic operations...');
    const batch = client.createBatch();
    
    try {
      // Add atomic operations to batch
      batch.addAtomicIncDec(counter1, 8, RiocClient.getTimestamp());   // Increment counter1 by 8
      batch.addAtomicIncDec(counter2, -7, RiocClient.getTimestamp());  // Decrement counter2 by 7
      
      console.log('Executing batch...');
      const tracker = batch.executeAsync();
      tracker.wait(1000);
      
      try {
        // Get results
        const result1 = tracker.getAtomicResult(0);
        const result2 = tracker.getAtomicResult(1);
        
        console.log(`Counter1 value (after batch): ${result1}`);
        console.log(`Counter2 value (after batch): ${result2}\n`);
        
        // Verify values with direct reads
        console.log('Verifying values with direct reads...');
        const verify1 = client.atomicIncDec(counter1, 0, RiocClient.getTimestamp());
        const verify2 = client.atomicIncDec(counter2, 0, RiocClient.getTimestamp());
        
        console.log(`Counter1 direct read: ${verify1}`);
        console.log(`Counter2 direct read: ${verify2}\n`);
        
        // Clean up
        console.log('Cleaning up...');
        client.delete(counter1, RiocClient.getTimestamp());
        client.delete(counter2, RiocClient.getTimestamp());
        console.log('Counters deleted\n');
        
        console.log('Example completed successfully!');
      } finally {
        tracker.dispose();
      }
    } finally {
      batch.dispose();
    }
  } catch (err) {
    console.error('Error:', err);
  } finally {
    client.dispose();
  }
}

// Run the example
runExample().catch(err => {
  console.error('Unhandled error:', err);
  process.exit(1);
}); 