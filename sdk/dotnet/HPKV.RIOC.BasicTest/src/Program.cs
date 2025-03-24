using System.Text;
using CommandLine;
using HPKV.RIOC;
using Microsoft.Extensions.Logging;

namespace HPKV.RIOC.TestApps.BasicTest;

class Options
{
    [Option('h', "host", Default = "localhost", HelpText = "Server host")]
    public string Host { get; set; } = "localhost";

    [Option('p', "port", Default = 8000, HelpText = "Server port")]
    public int Port { get; set; } = 8000;

    [Option('t', "tls", HelpText = "Enable TLS")]
    public bool UseTls { get; set; }

    [Option("ca", Default = "", HelpText = "Path to CA certificate")]
    public string CaPath { get; set; } = "";

    [Option("cert", Default = "", HelpText = "Path to client certificate")]
    public string ClientCertPath { get; set; } = "";

    [Option("key", Default = "", HelpText = "Path to client private key")]
    public string ClientKeyPath { get; set; } = "";
}

class Program
{
    static async Task Main(string[] args)
    {
        await Parser.Default.ParseArguments<Options>(args)
            .WithParsedAsync(async options =>
            {
                var config = new RiocConfig
                {
                    Host = options.Host,
                    Port = options.Port
                };

                if (options.UseTls)
                {
                    config.Tls = new RiocTlsConfig
                    {
                        CaPath = options.CaPath,
                        CertificatePath = options.ClientCertPath,
                        KeyPath = options.ClientKeyPath,
                        VerifyHostname = options.Host,
                        VerifyPeer = true
                    };
                }

                // Create logger
                using var loggerFactory = LoggerFactory.Create(builder =>
                {
                    builder.AddConsole();
                    builder.SetMinimumLevel(LogLevel.Information);
                });
                var logger = loggerFactory.CreateLogger<RiocClient>();

                Console.WriteLine($"Connecting to {options.Host}:{options.Port} {(options.UseTls ? "with" : "without")} TLS...");
                var startTime = DateTime.UtcNow;

                // Create client
                using var client = new RiocClient(config, logger);
                var connectTime = DateTime.UtcNow - startTime;
                Console.WriteLine($"Connected in {connectTime.TotalMilliseconds:F2} ms");

                // Test data
                var key = "test_key";
                var initialValue = "initial value";
                var updatedValue = "updated value";

                // Get initial timestamp
                var timestamp = RiocClient.GetTimestamp();
                Console.WriteLine($"\n1. Inserting record with timestamp {timestamp}");
                startTime = DateTime.UtcNow;
                client.InsertString(key, initialValue, timestamp);
                var insertTime = DateTime.UtcNow - startTime;
                Console.WriteLine($"Insert successful in {insertTime.TotalMilliseconds:F2} ms");

                // Sleep briefly to ensure timestamp increases
                await Task.Delay(1);

                Console.WriteLine("\n2. Getting record");
                startTime = DateTime.UtcNow;
                try
                {
                    var value = client.GetString(key);
                    var getTime = DateTime.UtcNow - startTime;
                    Console.WriteLine($"Get successful in {getTime.TotalMilliseconds:F2} ms, value: {value}");
                }
                catch (RiocKeyNotFoundException)
                {
                    var getTime = DateTime.UtcNow - startTime;
                    Console.WriteLine($"Key not found (took {getTime.TotalMilliseconds:F2} ms)");
                    return;
                }

                // Sleep briefly to ensure timestamp increases
                await Task.Delay(1);

                // Full update
                timestamp = RiocClient.GetTimestamp();
                Console.WriteLine($"\n3. Updating record with timestamp {timestamp}");
                startTime = DateTime.UtcNow;
                client.InsertString(key, updatedValue, timestamp);
                var updateTime = DateTime.UtcNow - startTime;
                Console.WriteLine($"Update successful in {updateTime.TotalMilliseconds:F2} ms");

                // Sleep briefly to ensure timestamp increases
                await Task.Delay(1);

                Console.WriteLine("\n4. Getting updated record");
                startTime = DateTime.UtcNow;
                try
                {
                    var value = client.GetString(key);
                    var getTime = DateTime.UtcNow - startTime;
                    Console.WriteLine($"Get successful in {getTime.TotalMilliseconds:F2} ms, value: {value}");
                }
                catch (RiocKeyNotFoundException)
                {
                    var getTime = DateTime.UtcNow - startTime;
                    Console.WriteLine($"Key not found (took {getTime.TotalMilliseconds:F2} ms)");
                    return;
                }

                // Sleep briefly to ensure timestamp increases
                await Task.Delay(1);

                // Test delete
                Console.WriteLine("\n5. Deleting record");
                timestamp = RiocClient.GetTimestamp();
                startTime = DateTime.UtcNow;
                client.DeleteString(key, timestamp);
                var deleteTime = DateTime.UtcNow - startTime;
                Console.WriteLine($"Delete successful in {deleteTime.TotalMilliseconds:F2} ms");

                // Test get after delete
                Console.WriteLine("\n6. Getting deleted record");
                startTime = DateTime.UtcNow;
                try
                {
                    var value = client.GetString(key);
                    var getTime = DateTime.UtcNow - startTime;
                    Console.WriteLine($"Get unexpectedly succeeded in {getTime.TotalMilliseconds:F2} ms");
                }
                catch (RiocKeyNotFoundException)
                {
                    var getTime = DateTime.UtcNow - startTime;
                    Console.WriteLine($"Get after delete correctly returned RIOC_ERR_NOENT in {getTime.TotalMilliseconds:F2} ms");
                }

                // Test batch operations
                Console.WriteLine("\n7. Testing batch operations");
                var keys = new[] { "batch_key1", "batch_key2", "batch_key3" };
                var values = new[] { "batch_value1", "batch_value2", "batch_value3" };

                // Insert initial values
                timestamp = RiocClient.GetTimestamp();
                startTime = DateTime.UtcNow;
                using (var batch = client.CreateBatch())
                {
                    for (var i = 0; i < keys.Length; i++)
                    {
                        batch.AddInsert(Encoding.UTF8.GetBytes(keys[i]), Encoding.UTF8.GetBytes(values[i]), timestamp);
                    }

                    using var tracker = batch.ExecuteAsync();
                    tracker.Wait(1000);
                }
                var batchInsertTime = DateTime.UtcNow - startTime;
                Console.WriteLine($"Batch insert successful in {batchInsertTime.TotalMilliseconds:F2} ms");

                // Get and delete in batch
                startTime = DateTime.UtcNow;
                using (var batch = client.CreateBatch())
                {
                    batch.AddGet(Encoding.UTF8.GetBytes(keys[0]));
                    batch.AddDelete(Encoding.UTF8.GetBytes(keys[1]), RiocClient.GetTimestamp());
                    batch.AddGet(Encoding.UTF8.GetBytes(keys[2]));

                    using var tracker = batch.ExecuteAsync();
                    tracker.Wait(1000);

                    var value1 = Encoding.UTF8.GetString(tracker.GetResponse(0));
                    var value2 = Encoding.UTF8.GetString(tracker.GetResponse(2));

                    Console.WriteLine($"Batch get results: {value1}, {value2}");
                }
                var batchMixedTime = DateTime.UtcNow - startTime;
                Console.WriteLine($"Batch mixed operations successful in {batchMixedTime.TotalMilliseconds:F2} ms");

                // Run range query tests
                RunRangeQueryTests(client);

                // Run atomic increment/decrement tests
                Console.WriteLine("\n==== Running Atomic Increment/Decrement Tests ====");
                RunAtomicIncDecTests(client);

                Console.WriteLine("\nAll tests completed successfully");
            });
    }

    private static void RunRangeQueryTests(RiocClient client)
    {
        Console.WriteLine("\n=== Running Range Query Tests ===");

        // Insert test data for range query
        int numKeys = 10;
        string[] keys = new string[numKeys];
        string[] values = new string[numKeys];
        ulong timestamp = RiocClient.GetTimestamp();

        Console.WriteLine($"Inserting {numKeys} keys for range query test");
        for (int i = 0; i < numKeys; i++)
        {
            // Use a prefix to ensure keys are sorted
            keys[i] = $"range_key_{i:D3}";
            values[i] = $"range_value_{i:D3}";
            client.InsertString(keys[i], values[i], timestamp + (ulong)i);
        }

        // Test direct range query
        Console.WriteLine("Testing direct range query");
        string startKey = "range_key_002";
        string endKey = "range_key_007";
        Console.WriteLine($"Querying range from '{startKey}' to '{endKey}'");

        var results = client.RangeQueryString(startKey, endKey);
        Console.WriteLine($"Range query returned {results.Count} results");

        // Verify results
        int expectedCount = 6; // keys 002 through 007 (inclusive)
        if (results.Count != expectedCount)
        {
            throw new Exception($"Range query result count mismatch: expected {expectedCount}, got {results.Count}");
        }

        foreach (var kv in results)
        {
            Console.WriteLine($"Key: '{kv.Key}', Value: '{kv.Value}'");
            
            // Extract the index from the key
            int index = int.Parse(kv.Key.Substring(10));
            string expectedValue = $"range_value_{index:D3}";
            
            if (kv.Value != expectedValue)
            {
                throw new Exception($"Value mismatch for key '{kv.Key}': expected '{expectedValue}', got '{kv.Value}'");
            }
        }

        // Test batch range query
        Console.WriteLine("\nTesting batch range query");
        using var batch = client.CreateBatch();
        
        // Add a range query to the batch
        string batchStartKey = "range_key_004";
        string batchEndKey = "range_key_008";
        Console.WriteLine($"Adding range query from '{batchStartKey}' to '{batchEndKey}' to batch");
        
        byte[] startKeyBytes = Encoding.UTF8.GetBytes(batchStartKey);
        byte[] endKeyBytes = Encoding.UTF8.GetBytes(batchEndKey);
        batch.AddRangeQuery(startKeyBytes, endKeyBytes);
        
        // Execute batch
        Console.WriteLine("Executing batch");
        using var tracker = batch.ExecuteAsync();
        tracker.Wait();
        Console.WriteLine("Batch executed successfully");
        
        // Get range query results
        var batchResults = tracker.GetRangeQueryResponseString(0);
        Console.WriteLine($"Batch range query returned {batchResults.Count} results");
        
        // Verify batch results
        int batchExpectedCount = 5; // keys 004 through 008 (inclusive)
        if (batchResults.Count != batchExpectedCount)
        {
            throw new Exception($"Batch range query result count mismatch: expected {batchExpectedCount}, got {batchResults.Count}");
        }
        
        foreach (var kv in batchResults)
        {
            Console.WriteLine($"Key: '{kv.Key}', Value: '{kv.Value}'");
            
            // Extract the index from the key
            int index = int.Parse(kv.Key.Substring(10));
            string expectedValue = $"range_value_{index:D3}";
            
            if (kv.Value != expectedValue)
            {
                throw new Exception($"Value mismatch for key '{kv.Key}': expected '{expectedValue}', got '{kv.Value}'");
            }
        }

        // Clean up test data
        Console.WriteLine("\nCleaning up test data");
        timestamp = RiocClient.GetTimestamp();
        for (int i = 0; i < numKeys; i++)
        {
            client.DeleteString(keys[i], timestamp + (ulong)i);
        }

        Console.WriteLine("Range query tests completed successfully");
    }

    private static void RunAtomicIncDecTests(RiocClient client)
    {
        Console.WriteLine("Starting atomic increment/decrement tests...");

        string counterKey = "atomic_counter_test";
        ulong timestamp = RiocClient.GetTimestamp();

        // Initialize counter with a delete first (to ensure a clean state)
        try
        {
            client.DeleteString(counterKey, timestamp++);
            Console.WriteLine("Successfully deleted counter key to start fresh");
        }
        catch (Exception ex)
        {
            // Non-critical, key might not exist
            Console.WriteLine($"Note: Delete failed (likely key doesn't exist): {ex.Message}");
        }

        // 1. Test individual atomic increment
        Console.WriteLine("\n1. Testing individual atomic increment");
        var startTime = DateTime.UtcNow;
        long result1 = client.AtomicIncDecString(counterKey, 5, timestamp++);
        var incTime = DateTime.UtcNow - startTime;
        
        Console.WriteLine($"Increment by 5 successful in {incTime.TotalMilliseconds:F2} ms, new value: {result1}");
        if (result1 != 5)
        {
            throw new Exception($"Atomic increment result mismatch: expected 5, got {result1}");
        }

        // 2. Test individual atomic decrement
        Console.WriteLine("\n2. Testing individual atomic decrement");
        startTime = DateTime.UtcNow;
        long result2 = client.AtomicIncDecString(counterKey, -2, timestamp++);
        var decTime = DateTime.UtcNow - startTime;
        
        Console.WriteLine($"Decrement by 2 successful in {decTime.TotalMilliseconds:F2} ms, new value: {result2}");
        if (result2 != 3)
        {
            throw new Exception($"Atomic decrement result mismatch: expected 3, got {result2}");
        }

        try
        {
            // 3. Test batch atomic operations with just one operation
            Console.WriteLine("\n3. Testing batch atomic operation");

            // Create batch with one atomic operation
            Console.WriteLine("Creating batch with one atomic operation");
            using var batch = client.CreateBatch();
            
            // Add a single counter - increment by 10
            Console.WriteLine("Adding atomic inc operation to batch");
            batch.AddAtomicIncDecString(counterKey, 10, timestamp++);
            
            // Execute batch
            Console.WriteLine("Executing batch");
            startTime = DateTime.UtcNow;
            using var tracker = batch.ExecuteAsync();
            
            Console.WriteLine("Waiting for batch completion");
            tracker.Wait();
            var batchTime = DateTime.UtcNow - startTime;
            
            Console.WriteLine($"Batch atomic operation completed in {batchTime.TotalMilliseconds:F2} ms");

            try
            {
                // Verify batch result
                Console.WriteLine("Getting atomic result from batch");
                long result3 = tracker.GetAtomicResult(0);
                Console.WriteLine($"Batch result: {counterKey}={result3}");
                
                if (result3 != 13)
                {
                    throw new Exception($"Batch atomic result mismatch: expected 13, got {result3}");
                }

                // 4. Verify value individually
                Console.WriteLine("\n4. Verifying value individually after batch operation");
                
                long verify1 = client.AtomicIncDecString(counterKey, 0, timestamp++);
                Console.WriteLine($"Individual verification: {counterKey}={verify1}");
                
                if (verify1 != 13)
                {
                    throw new Exception($"Individual verification mismatch: expected 13, got {verify1}");
                }

                // Clean up test data
                Console.WriteLine("\nCleaning up test data");
                client.DeleteString(counterKey, timestamp++);

                Console.WriteLine("Atomic increment/decrement tests completed successfully");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR: Failed during batch result handling: {ex.GetType().Name} - {ex.Message}");
                throw;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"ERROR: Batch operation exception: {ex.GetType().Name} - {ex.Message}");
            // Continue to cleanup
            try
            {
                // Still try to clean up test data
                Console.WriteLine("\nAttempting to clean up test data despite error");
                client.DeleteString(counterKey, timestamp++);
                Console.WriteLine("Cleanup partially successful");
            }
            catch (Exception cleanupEx)
            {
                Console.WriteLine($"Cleanup failed: {cleanupEx.Message}");
            }
            throw;
        }
    }
} 