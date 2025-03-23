import { RiocClient, RiocConfig, RiocTlsConfig, RiocKeyNotFoundError, RangeQueryResult } from '../src';
import { expect } from 'chai';
import 'mocha';

describe('RiocClient', () => {
    // Test configuration from environment variables with defaults
    const host = process.env.RIOC_TEST_HOST ?? '127.0.0.1';
    const port = parseInt(process.env.RIOC_TEST_PORT ?? '8000');
    const useTls = process.env.RIOC_TEST_TLS !== 'false'; // TLS enabled by default
    const caPath = process.env.RIOC_TEST_CA_PATH ?? '/workspaces/kernel-high-performance-kv-store/api/rioc/certs/ca.crt';
    const clientCertPath = process.env.RIOC_TEST_CLIENT_CERT_PATH ?? '/workspaces/kernel-high-performance-kv-store/api/rioc/certs/client.crt';
    const clientKeyPath = process.env.RIOC_TEST_CLIENT_KEY_PATH ?? '/workspaces/kernel-high-performance-kv-store/api/rioc/certs/client.key';

    let client: RiocClient;

    beforeEach(() => {
        // Create client configuration
        const config: RiocConfig = {
            host,
            port,
            timeoutMs: 5000,
            tls: useTls ? {
                caPath,
                certificatePath: clientCertPath,
                keyPath: clientKeyPath,
                verifyHostname: host,
                verifyPeer: true
            } : undefined
        };

        // Log test configuration
        console.log('Test Configuration:');
        console.log(`  Host: ${host}`);
        console.log(`  Port: ${port}`);
        console.log(`  TLS: ${useTls ? 'enabled' : 'disabled'}`);
        if (useTls) {
            console.log(`  CA Path: ${caPath}`);
            console.log(`  Client Cert Path: ${clientCertPath}`);
            console.log(`  Client Key Path: ${clientKeyPath}`);
        }

        // Create client
        client = new RiocClient(config);
    });

    afterEach(() => {
        if (client) {
            client.dispose();
        }
    });

    it('should insert and get string values', () => {
        // Arrange
        const key = Buffer.from('test_key');
        const value = Buffer.from('test_value');
        const timestamp = RiocClient.getTimestamp();

        // Act
        client.insert(key, value, timestamp);
        const retrievedValue = client.get(key);

        // Assert
        expect(retrievedValue).to.not.be.null;
        expect(retrievedValue!.toString()).to.equal(value.toString());
    });

    it('should insert and get binary values', () => {
        // Arrange
        const key = Buffer.from('test_key_binary');
        const value = Buffer.from([0x01, 0x02, 0x03, 0x04]);
        const timestamp = RiocClient.getTimestamp();

        // Act
        client.insert(key, value, timestamp);
        const retrievedValue = client.get(key);

        // Assert
        expect(retrievedValue).to.not.be.null;
        expect(Buffer.compare(retrievedValue!, value)).to.equal(0);
    });

    it('should throw error with code -6 for nonexistent key', () => {
        // Arrange
        const key = Buffer.from('nonexistent_key');

        // Act & Assert
        try {
            client.get(key);
            expect.fail('Should have thrown an error');
        } catch (err: any) {
            expect(err).to.have.property('code', -6); // RIOC_ERR_NOENT
        }
    });

    it('should delete existing key', () => {
        // Arrange
        const key = Buffer.from('key_to_delete');
        const value = Buffer.from('value_to_delete');
        const timestamp = RiocClient.getTimestamp();
        client.insert(key, value, timestamp);

        // Act
        client.delete(key, RiocClient.getTimestamp());

        // Assert
        try {
            client.get(key);
            expect.fail('Should have thrown an error');
        } catch (err: any) {
            expect(err).to.have.property('code', -6); // RIOC_ERR_NOENT
        }
    });

    it('should execute batch operations successfully', () => {
        // Arrange
        const key1 = Buffer.from('batch_key1');
        const value1 = Buffer.from('batch_value1');
        const key2 = Buffer.from('batch_key2');
        const value2 = Buffer.from('batch_value2');
        const key3 = Buffer.from('batch_key3');
        const value3 = Buffer.from('batch_value3');
        const timestamp = RiocClient.getTimestamp();

        // Insert initial values
        client.insert(key1, value1, timestamp);
        client.insert(key2, value2, timestamp);
        client.insert(key3, value3, timestamp);

        // Act
        const batch = client.createBatch();
        try {
            batch.addGet(key1);
            batch.addDelete(key2, RiocClient.getTimestamp());
            batch.addGet(key3);

            const tracker = batch.executeAsync();
            tracker.wait(1000);

            // Assert
            const result1 = tracker.getResponse(0);
            const result2 = tracker.getResponse(2);

            expect(Buffer.compare(result1!, value1)).to.equal(0);
            expect(Buffer.compare(result2!, value3)).to.equal(0);
            
            try {
                client.get(key2);
                expect.fail('Should have thrown an error');
            } catch (err: any) {
                expect(err).to.have.property('code', -6); // RIOC_ERR_NOENT
            }

            tracker.dispose();
        } finally {
            batch.dispose();
        }
    });

    it('should return increasing timestamps', (done) => {
        // Act
        const timestamp1 = RiocClient.getTimestamp();
        setTimeout(() => {
            const timestamp2 = RiocClient.getTimestamp();

            // Assert
            expect(Number(timestamp2)).to.be.greaterThan(Number(timestamp1));
            done();
        }, 1);
    });

    describe('Range Query', () => {
        const testData = [
            { key: 'range:a', value: 'Value A' },
            { key: 'range:b', value: 'Value B' },
            { key: 'range:c', value: 'Value C' },
            { key: 'range:d', value: 'Value D' },
            { key: 'range:e', value: 'Value E' },
            { key: 'other:x', value: 'Value X' }
        ];

        beforeEach(async () => {
            // Insert test data
            const timestamp = RiocClient.getTimestamp();
            for (let i = 0; i < testData.length; i++) {
                const item = testData[i];
                const key = Buffer.from(item.key);
                const value = Buffer.from(item.value);
                client.insert(key, value, timestamp + BigInt(i));
            }
        });

        it('should perform a range query', () => {
            const startKey = Buffer.from('range:a');
            const endKey = Buffer.from('range:e');
            
            const results = client.rangeQuery(startKey, endKey);
            
            expect(results).to.be.an('array');
            expect(results.length).to.equal(5);
            
            // Verify results
            for (let i = 0; i < results.length; i++) {
                const result = results[i];
                expect(result).to.have.property('key').that.is.instanceOf(Buffer);
                expect(result).to.have.property('value').that.is.instanceOf(Buffer);
                
                const keyStr = result.key.toString();
                const valueStr = result.value.toString();
                
                // Find the matching test data
                const matchingItem = testData.find(item => item.key === keyStr);
                expect(matchingItem).to.not.be.undefined;
                expect(valueStr).to.equal(matchingItem!.value);
            }
        });

        it('should perform a range query with a subset of keys', () => {
            const startKey = Buffer.from('range:b');
            const endKey = Buffer.from('range:d');
            
            const results = client.rangeQuery(startKey, endKey);
            
            expect(results).to.be.an('array');
            expect(results.length).to.equal(3);
            
            // Verify results contain only the expected keys
            const resultKeys = results.map((r: RangeQueryResult) => r.key.toString());
            expect(resultKeys).to.include('range:b');
            expect(resultKeys).to.include('range:c');
            expect(resultKeys).to.include('range:d');
            expect(resultKeys).to.not.include('range:a');
            expect(resultKeys).to.not.include('range:e');
            expect(resultKeys).to.not.include('other:x');
        });

        it('should return an empty array for a range with no keys', () => {
            const startKey = Buffer.from('nonexistent:a');
            const endKey = Buffer.from('nonexistent:z');
            
            const results = client.rangeQuery(startKey, endKey);
            
            expect(results).to.be.an('array');
            expect(results.length).to.equal(0);
        });

        it('should perform a range query in a batch', () => {
            const batch = client.createBatch();
            
            // Add a range query for 'range:' keys
            const startKey = Buffer.from('range:');
            const endKey = Buffer.from('range:\xFF');
            batch.addRangeQuery(startKey, endKey);
            
            // Add a range query for 'other:' keys
            const otherStartKey = Buffer.from('other:');
            const otherEndKey = Buffer.from('other:\xFF');
            batch.addRangeQuery(otherStartKey, otherEndKey);
            
            // Execute batch
            const tracker = batch.executeAsync();
            tracker.wait();
            
            // Get results for the first range query
            const rangeResults = tracker.getRangeQueryResponse(0);
            expect(rangeResults).to.be.an('array');
            expect(rangeResults.length).to.equal(5);
            
            // Get results for the second range query
            const otherResults = tracker.getRangeQueryResponse(1);
            expect(otherResults).to.be.an('array');
            expect(otherResults.length).to.equal(1);
            expect(otherResults[0].key.toString()).to.equal('other:x');
            
            // Clean up
            tracker.dispose();
            batch.dispose();
        });
    });

    describe('Atomic Increment/Decrement', () => {
        it('should atomically increment and decrement a counter', () => {
            // Arrange
            const key = Buffer.from('atomic_test_key');
            const timestamp = RiocClient.getTimestamp();
            
            // Clean up before test
            try {
                client.delete(key, timestamp);
            } catch (err) {
                // Key might not exist, that's ok
            }
            
            // Act - Increment by 10
            const result1 = client.atomicIncDec(key, 10, RiocClient.getTimestamp());
            
            // Assert
            expect(Number(result1)).to.equal(10);
            
            // Act - Decrement by 3
            const result2 = client.atomicIncDec(key, -3, RiocClient.getTimestamp());
            
            // Assert
            expect(Number(result2)).to.equal(7);
            
            // Act - Increment multiple times
            const result3 = client.atomicIncDec(key, 5, RiocClient.getTimestamp());
            const result4 = client.atomicIncDec(key, 8, RiocClient.getTimestamp());
            
            // Assert
            expect(Number(result3)).to.equal(12);
            expect(Number(result4)).to.equal(20);
            
            // Clean up
            client.delete(key, RiocClient.getTimestamp());
        });
        
        it('should read a counter value with increment of 0', () => {
            // Arrange
            const key = Buffer.from('atomic_read_test_key');
            const timestamp = RiocClient.getTimestamp();
            
            // Clean up before test
            try {
                client.delete(key, timestamp);
            } catch (err) {
                // Key might not exist, that's ok
            }
            
            // Initialize with a value
            client.atomicIncDec(key, 42, RiocClient.getTimestamp());
            
            // Act - Read current value with increment of 0
            const result = client.atomicIncDec(key, 0, RiocClient.getTimestamp());
            
            // Assert
            expect(Number(result)).to.equal(42);
            
            // Clean up
            client.delete(key, RiocClient.getTimestamp());
        });
        
        it('should execute atomic operations in a batch', () => {
            // Arrange
            const key1 = Buffer.from('batch_atomic_key1');
            const key2 = Buffer.from('batch_atomic_key2');
            const key3 = Buffer.from('batch_atomic_key3');
            const key4 = Buffer.from('batch_atomic_key4');
            
            // Clean up before test
            try {
                client.delete(key1, RiocClient.getTimestamp());
                client.delete(key2, RiocClient.getTimestamp());
                client.delete(key3, RiocClient.getTimestamp());
                client.delete(key4, RiocClient.getTimestamp());
            } catch (err) {
                // Keys might not exist, that's ok
            }
            
            // Initialize multiple keys with different values
            client.atomicIncDec(key1, 5, RiocClient.getTimestamp()); // Start at 5
            client.atomicIncDec(key2, 10, RiocClient.getTimestamp()); // Start at 10
            
            // Act - Create batch with multiple atomic operations
            const batch = client.createBatch();
            
            try {
                // Add operations to modify existing and create new counters
                batch.addAtomicIncDec(key1, 15, RiocClient.getTimestamp()); // Increment (5 -> 20)
                batch.addAtomicIncDec(key2, -5, RiocClient.getTimestamp()); // Decrement (10 -> 5)
                batch.addAtomicIncDec(key3, 30, RiocClient.getTimestamp()); // New key
                batch.addAtomicIncDec(key4, 40, RiocClient.getTimestamp()); // New key
                
                // Execute batch
                const tracker = batch.executeAsync();
                tracker.wait(1000);
                
                try {
                    // Assert results
                    const result1 = tracker.getAtomicResult(0);
                    const result2 = tracker.getAtomicResult(1);
                    const result3 = tracker.getAtomicResult(2);
                    const result4 = tracker.getAtomicResult(3);
                    
                    expect(Number(result1)).to.equal(20); // 5 + 15
                    expect(Number(result2)).to.equal(5);  // 10 - 5
                    expect(Number(result3)).to.equal(30); // 0 + 30
                    expect(Number(result4)).to.equal(40); // 0 + 40
                    
                    // Verify values individually
                    const verify1 = client.atomicIncDec(key1, 0, RiocClient.getTimestamp());
                    const verify2 = client.atomicIncDec(key2, 0, RiocClient.getTimestamp());
                    const verify3 = client.atomicIncDec(key3, 0, RiocClient.getTimestamp());
                    const verify4 = client.atomicIncDec(key4, 0, RiocClient.getTimestamp());
                    
                    expect(Number(verify1)).to.equal(20);
                    expect(Number(verify2)).to.equal(5);
                    expect(Number(verify3)).to.equal(30);
                    expect(Number(verify4)).to.equal(40);
                    
                    // Create a second batch to modify the same counters
                    const batch2 = client.createBatch();
                    
                    batch2.addAtomicIncDec(key1, -8, RiocClient.getTimestamp());  // Decrement (20 -> 12)
                    batch2.addAtomicIncDec(key2, 15, RiocClient.getTimestamp());  // Increment (5 -> 20)
                    batch2.addAtomicIncDec(key3, -10, RiocClient.getTimestamp()); // Decrement (30 -> 20)
                    batch2.addAtomicIncDec(key4, -20, RiocClient.getTimestamp()); // Decrement (40 -> 20)
                    
                    const tracker2 = batch2.executeAsync();
                    tracker2.wait(1000);
                    
                    // Verify second batch results
                    const result2_1 = tracker2.getAtomicResult(0);
                    const result2_2 = tracker2.getAtomicResult(1);
                    const result2_3 = tracker2.getAtomicResult(2);
                    const result2_4 = tracker2.getAtomicResult(3);
                    
                    expect(Number(result2_1)).to.equal(12);
                    expect(Number(result2_2)).to.equal(20);
                    expect(Number(result2_3)).to.equal(20);
                    expect(Number(result2_4)).to.equal(20);
                    
                    // Verify final values individually
                    expect(Number(client.atomicIncDec(key1, 0, RiocClient.getTimestamp()))).to.equal(12);
                    expect(Number(client.atomicIncDec(key2, 0, RiocClient.getTimestamp()))).to.equal(20);
                    expect(Number(client.atomicIncDec(key3, 0, RiocClient.getTimestamp()))).to.equal(20);
                    expect(Number(client.atomicIncDec(key4, 0, RiocClient.getTimestamp()))).to.equal(20);
                    
                    tracker2.dispose();
                } finally {
                    tracker.dispose();
                }
            } finally {
                batch.dispose();
                
                // Clean up
                try {
                    client.delete(key1, RiocClient.getTimestamp());
                    client.delete(key2, RiocClient.getTimestamp());
                    client.delete(key3, RiocClient.getTimestamp());
                    client.delete(key4, RiocClient.getTimestamp());
                } catch (err) {
                    // Clean up errors are not test failures
                }
            }
        });
    });
}); 