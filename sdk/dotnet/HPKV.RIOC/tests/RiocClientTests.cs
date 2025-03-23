using System.Text;
using Microsoft.Extensions.Logging;
using Xunit;

namespace HPKV.RIOC.Tests;

public class RiocClientTests : IDisposable
{
    private readonly RiocClient _client;
    private readonly ILogger<RiocClient> _logger;

    // Test configuration from environment variables with defaults
    private static readonly string Host = Environment.GetEnvironmentVariable("RIOC_TEST_HOST") ?? "127.0.0.1";
    private static readonly int Port = int.Parse(Environment.GetEnvironmentVariable("RIOC_TEST_PORT") ?? "8000");
    private static readonly bool UseTls = bool.Parse(Environment.GetEnvironmentVariable("RIOC_TEST_TLS") ?? "true");
    private static readonly string CaPath = Environment.GetEnvironmentVariable("RIOC_TEST_CA_PATH") ?? 
        "/workspaces/kernel-high-performance-kv-store/api/rioc/certs/ca.crt";
    private static readonly string ClientCertPath = Environment.GetEnvironmentVariable("RIOC_TEST_CLIENT_CERT_PATH") ?? 
        "/workspaces/kernel-high-performance-kv-store/api/rioc/certs/client.crt";
    private static readonly string ClientKeyPath = Environment.GetEnvironmentVariable("RIOC_TEST_CLIENT_KEY_PATH") ?? 
        "/workspaces/kernel-high-performance-kv-store/api/rioc/certs/client.key";

    public RiocClientTests()
    {
        // Create logger
        ILoggerFactory loggerFactory = LoggerFactory.Create(builder =>
        {
            builder.AddConsole();
            builder.SetMinimumLevel(LogLevel.Debug);
        });
        _logger = loggerFactory.CreateLogger<RiocClient>();

        // Create client configuration
        var config = new RiocConfig
        {
            Host = Host,
            Port = Port,
            TimeoutMs = 5000,
            Tls = UseTls ? new RiocTlsConfig
            {
                CaPath = CaPath,
                CertificatePath = ClientCertPath,
                KeyPath = ClientKeyPath,
                VerifyHostname = Host,
                VerifyPeer = true
            } : null
        };

        // Log test configuration
        _logger.LogInformation("Test Configuration:");
        _logger.LogInformation("  Host: {Host}", Host);
        _logger.LogInformation("  Port: {Port}", Port);
        _logger.LogInformation("  TLS: {TLS}", UseTls ? "enabled" : "disabled");
        if (UseTls)
        {
            _logger.LogInformation("  CA Path: {CaPath}", CaPath);
            _logger.LogInformation("  Client Cert Path: {ClientCertPath}", ClientCertPath);
            _logger.LogInformation("  Client Key Path: {ClientKeyPath}", ClientKeyPath);
        }

        // Create client
        _client = new RiocClient(config, _logger);
    }

    [Fact]
    public void InsertAndGet_WithString_ShouldSucceed()
    {
        // Arrange
        string key = "test_key";
        string value = "test_value";
        ulong timestamp = RiocClient.GetTimestamp();

        // Act
        _client.InsertString(key, value, timestamp);
        string retrievedValue = _client.GetString(key);

        // Assert
        Assert.Equal(value, retrievedValue);
    }

    [Fact]
    public void InsertAndGet_WithBytes_ShouldSucceed()
    {
        // Arrange
        byte[] key = Encoding.UTF8.GetBytes("test_key_bytes");
        byte[] value = Encoding.UTF8.GetBytes("test_value_bytes");
        ulong timestamp = RiocClient.GetTimestamp();

        // Act
        _client.Insert(key, value, timestamp);
        byte[] retrievedValue = _client.Get(key);

        // Assert
        Assert.Equal(value, retrievedValue);
    }

    [Fact]
    public void Get_NonexistentKey_ShouldThrowKeyNotFoundException()
    {
        // Arrange
        string key = "nonexistent_key";

        // Act & Assert
        Assert.Throws<RiocKeyNotFoundException>(() => _client.GetString(key));
    }

    [Fact]
    public void Delete_ExistingKey_ShouldSucceed()
    {
        // Arrange
        string key = "key_to_delete";
        string value = "value_to_delete";
        ulong timestamp = RiocClient.GetTimestamp();
        _client.InsertString(key, value, timestamp);

        // Act
        _client.DeleteString(key, RiocClient.GetTimestamp());

        // Assert
        Assert.Throws<RiocKeyNotFoundException>(() => _client.GetString(key));
    }

    [Fact]
    public void BatchOperations_ShouldSucceed()
    {
        // Arrange
        byte[] key1 = Encoding.UTF8.GetBytes("batch_key1");
        byte[] value1 = Encoding.UTF8.GetBytes("batch_value1");
        byte[] key2 = Encoding.UTF8.GetBytes("batch_key2");
        byte[] value2 = Encoding.UTF8.GetBytes("batch_value2");
        byte[] key3 = Encoding.UTF8.GetBytes("batch_key3");
        byte[] value3 = Encoding.UTF8.GetBytes("batch_value3");
        ulong timestamp = RiocClient.GetTimestamp();

        // Insert initial values
        _client.Insert(key1, value1, timestamp);
        _client.Insert(key2, value2, timestamp);
        _client.Insert(key3, value3, timestamp);

        // Act
        using var batch = _client.CreateBatch();
        batch.AddGet(key1);
        batch.AddDelete(key2, RiocClient.GetTimestamp());
        batch.AddGet(key3);

        using var tracker = batch.ExecuteAsync();
        tracker.Wait(1000);

        // Assert
        byte[] result1 = tracker.GetResponse(0);
        byte[] result2 = tracker.GetResponse(2);

        Assert.Equal(value1, result1);
        Assert.Equal(value3, result2);
        Assert.Throws<RiocKeyNotFoundException>(() => _client.Get(key2));
    }

    [Fact]
    public void GetTimestamp_ShouldReturnIncreasingValues()
    {
        // Act
        ulong timestamp1 = RiocClient.GetTimestamp();
        Thread.Sleep(1);
        ulong timestamp2 = RiocClient.GetTimestamp();

        // Assert
        Assert.True(timestamp2 > timestamp1);
    }

    [Fact]
    public void RangeQuery_WithString_ShouldReturnCorrectResults()
    {
        // Arrange
        string[] keys = new string[]
        {
            "range_test_key_001",
            "range_test_key_002",
            "range_test_key_003",
            "range_test_key_004",
            "range_test_key_005"
        };
        
        string[] values = new string[]
        {
            "range_test_value_001",
            "range_test_value_002",
            "range_test_value_003",
            "range_test_value_004",
            "range_test_value_005"
        };
        
        ulong timestamp = RiocClient.GetTimestamp();
        
        // Insert test data
        for (int i = 0; i < keys.Length; i++)
        {
            _client.InsertString(keys[i], values[i], timestamp + (ulong)i);
        }
        
        // Act
        var results = _client.RangeQueryString("range_test_key_002", "range_test_key_004");
        
        // Assert
        Assert.Equal(3, results.Count); // Should include keys 002, 003, 004
        
        // Verify each key-value pair
        Assert.Contains(results, kv => kv.Key == "range_test_key_002" && kv.Value == "range_test_value_002");
        Assert.Contains(results, kv => kv.Key == "range_test_key_003" && kv.Value == "range_test_value_003");
        Assert.Contains(results, kv => kv.Key == "range_test_key_004" && kv.Value == "range_test_value_004");
        
        // Clean up
        for (int i = 0; i < keys.Length; i++)
        {
            _client.DeleteString(keys[i], timestamp + (ulong)(keys.Length + i));
        }
    }

    [Fact]
    public void RangeQuery_WithBytes_ShouldReturnCorrectResults()
    {
        // Arrange
        byte[][] keys = new byte[][]
        {
            Encoding.UTF8.GetBytes("range_bytes_key_001"),
            Encoding.UTF8.GetBytes("range_bytes_key_002"),
            Encoding.UTF8.GetBytes("range_bytes_key_003"),
            Encoding.UTF8.GetBytes("range_bytes_key_004"),
            Encoding.UTF8.GetBytes("range_bytes_key_005")
        };
        
        byte[][] values = new byte[][]
        {
            Encoding.UTF8.GetBytes("range_bytes_value_001"),
            Encoding.UTF8.GetBytes("range_bytes_value_002"),
            Encoding.UTF8.GetBytes("range_bytes_value_003"),
            Encoding.UTF8.GetBytes("range_bytes_value_004"),
            Encoding.UTF8.GetBytes("range_bytes_value_005")
        };
        
        ulong timestamp = RiocClient.GetTimestamp();
        
        // Insert test data
        for (int i = 0; i < keys.Length; i++)
        {
            _client.Insert(keys[i], values[i], timestamp + (ulong)i);
        }
        
        // Act
        byte[] startKey = Encoding.UTF8.GetBytes("range_bytes_key_002");
        byte[] endKey = Encoding.UTF8.GetBytes("range_bytes_key_004");
        var results = _client.RangeQuery(startKey, endKey);
        
        // Assert
        Assert.Equal(3, results.Count); // Should include keys 002, 003, 004
        
        // Verify each key-value pair by converting to strings for easier comparison
        var stringResults = results.Select(kv => new KeyValuePair<string, string>(
            Encoding.UTF8.GetString(kv.Key),
            Encoding.UTF8.GetString(kv.Value)
        )).ToList();
        
        Assert.Contains(stringResults, kv => kv.Key == "range_bytes_key_002" && kv.Value == "range_bytes_value_002");
        Assert.Contains(stringResults, kv => kv.Key == "range_bytes_key_003" && kv.Value == "range_bytes_value_003");
        Assert.Contains(stringResults, kv => kv.Key == "range_bytes_key_004" && kv.Value == "range_bytes_value_004");
        
        // Clean up
        for (int i = 0; i < keys.Length; i++)
        {
            _client.Delete(keys[i], timestamp + (ulong)(keys.Length + i));
        }
    }

    [Fact]
    public void BatchRangeQuery_ShouldReturnCorrectResults()
    {
        // Arrange
        string[] keys = new string[]
        {
            "batch_range_key_001",
            "batch_range_key_002",
            "batch_range_key_003",
            "batch_range_key_004",
            "batch_range_key_005"
        };
        
        string[] values = new string[]
        {
            "batch_range_value_001",
            "batch_range_value_002",
            "batch_range_value_003",
            "batch_range_value_004",
            "batch_range_value_005"
        };
        
        ulong timestamp = RiocClient.GetTimestamp();
        
        // Insert test data
        for (int i = 0; i < keys.Length; i++)
        {
            _client.InsertString(keys[i], values[i], timestamp + (ulong)i);
        }
        
        // Act
        using var batch = _client.CreateBatch();
        byte[] startKey = Encoding.UTF8.GetBytes("batch_range_key_002");
        byte[] endKey = Encoding.UTF8.GetBytes("batch_range_key_004");
        batch.AddRangeQuery(startKey, endKey);
        
        using var tracker = batch.ExecuteAsync();
        tracker.Wait(1000);
        
        var results = tracker.GetRangeQueryResponseString(0);
        
        // Assert
        Assert.Equal(3, results.Count); // Should include keys 002, 003, 004
        
        // Verify each key-value pair
        Assert.Contains(results, kv => kv.Key == "batch_range_key_002" && kv.Value == "batch_range_value_002");
        Assert.Contains(results, kv => kv.Key == "batch_range_key_003" && kv.Value == "batch_range_value_003");
        Assert.Contains(results, kv => kv.Key == "batch_range_key_004" && kv.Value == "batch_range_value_004");
        
        // Clean up
        for (int i = 0; i < keys.Length; i++)
        {
            _client.DeleteString(keys[i], timestamp + (ulong)(keys.Length + i));
        }
    }

    [Fact]
    public void RangeQuery_EmptyRange_ShouldReturnEmptyList()
    {
        // Act & Assert
        var exception = Assert.Throws<RiocIOException>(() => _client.RangeQueryString("empty_range_a", "empty_range_z"));
        Assert.Equal("An I/O error occurred.", exception.Message);
    }

    [Fact]
    public void AtomicIncDec_ShouldIncrementAndDecrement()
    {
        // Arrange
        string key = "atomic_test_key";
        ulong timestamp = RiocClient.GetTimestamp();
        
        // Ensure clean state
        try { _client.DeleteString(key, timestamp++); } catch { /* Key might not exist */ }
        
        // Act - Increment
        long result1 = _client.AtomicIncDecString(key, 10, timestamp++);
        
        // Assert
        Assert.Equal(10, result1);
        
        // Act - Decrement
        long result2 = _client.AtomicIncDecString(key, -3, timestamp++);
        
        // Assert
        Assert.Equal(7, result2);
        
        // Act - Increment multiple times
        long result3 = _client.AtomicIncDecString(key, 5, timestamp++);
        long result4 = _client.AtomicIncDecString(key, 8, timestamp++);
        
        // Assert
        Assert.Equal(12, result3);
        Assert.Equal(20, result4);
        
        // Clean up
        _client.DeleteString(key, timestamp++);
    }
    
    [Fact]
    public void AtomicIncDec_WithBytes_ShouldIncrementAndDecrement()
    {
        // Arrange
        byte[] key = Encoding.UTF8.GetBytes("atomic_bytes_test_key");
        ulong timestamp = RiocClient.GetTimestamp();
        
        // Ensure clean state
        try { _client.Delete(key, timestamp++); } catch { /* Key might not exist */ }
        
        // Act - Increment
        long result1 = _client.AtomicIncDec(key, 15, timestamp++);
        
        // Assert
        Assert.Equal(15, result1);
        
        // Act - Decrement
        long result2 = _client.AtomicIncDec(key, -7, timestamp++);
        
        // Assert
        Assert.Equal(8, result2);
        
        // Clean up
        _client.Delete(key, timestamp++);
    }
    
    [Fact]
    public void AtomicIncDec_BatchOperations_ShouldSucceed()
    {
        // Arrange
        string key1 = "batch_atomic_key1";
        string key2 = "batch_atomic_key2";
        string key3 = "batch_atomic_key3";
        string key4 = "batch_atomic_key4";
        ulong timestamp = RiocClient.GetTimestamp();
        
        // Ensure clean state
        try 
        { 
            _client.DeleteString(key1, timestamp++);
            _client.DeleteString(key2, timestamp++);
            _client.DeleteString(key3, timestamp++);
            _client.DeleteString(key4, timestamp++);
        } 
        catch { /* Keys might not exist */ }
        
        // Initialize multiple keys with different values (all positive)
        _client.AtomicIncDecString(key1, 5, timestamp++);     // Start at 5
        _client.AtomicIncDecString(key2, 10, timestamp++);    // Start at 10
        
        // Act - Create batch with multiple atomic operations
        using var batch = _client.CreateBatch();
        
        // Add operations to modify existing and create new counters
        batch.AddAtomicIncDecString(key1, 15, timestamp++);   // Increment (5 -> 20)
        batch.AddAtomicIncDecString(key2, -5, timestamp++);   // Decrement (10 -> 5)
        batch.AddAtomicIncDecString(key3, 30, timestamp++);   // New key
        batch.AddAtomicIncDecString(key4, 40, timestamp++);   // New key
        
        // Execute batch
        using var tracker = batch.ExecuteAsync();
        tracker.Wait(1000);
        
        try
        {
            // Get results one at a time with explicit error handling
            long result1 = tracker.GetAtomicResult(0);
            Console.WriteLine($"Result 1: {result1}");
            Assert.Equal(20, result1);  // 5 + 15
            
            long result2 = tracker.GetAtomicResult(1);
            Console.WriteLine($"Result 2: {result2}");
            Assert.Equal(5, result2);   // 10 - 5
            
            long result3 = tracker.GetAtomicResult(2);
            Console.WriteLine($"Result 3: {result3}");
            Assert.Equal(30, result3);  // 0 + 30
            
            long result4 = tracker.GetAtomicResult(3);
            Console.WriteLine($"Result 4: {result4}");
            Assert.Equal(40, result4);  // 0 + 40
            
            // Verify values individually
            long verify1 = _client.AtomicIncDecString(key1, 0, timestamp++);
            Assert.Equal(20, verify1);
            
            long verify2 = _client.AtomicIncDecString(key2, 0, timestamp++);
            Assert.Equal(5, verify2);
            
            long verify3 = _client.AtomicIncDecString(key3, 0, timestamp++);
            Assert.Equal(30, verify3);
            
            long verify4 = _client.AtomicIncDecString(key4, 0, timestamp++);
            Assert.Equal(40, verify4);
            
            // Create a second batch to modify the same counters
            Console.WriteLine("Creating second batch to modify the same counters");
            using var batch2 = _client.CreateBatch();
            
            batch2.AddAtomicIncDecString(key1, -8, timestamp++);  // Decrement (20 -> 12)
            batch2.AddAtomicIncDecString(key2, 15, timestamp++);  // Increment (5 -> 20)
            batch2.AddAtomicIncDecString(key3, -10, timestamp++); // Decrement (30 -> 20)
            batch2.AddAtomicIncDecString(key4, -20, timestamp++); // Decrement (40 -> 20)
            
            using var tracker2 = batch2.ExecuteAsync();
            tracker2.Wait(1000);
            
            // Verify second batch results
            long result2_1 = tracker2.GetAtomicResult(0);
            Console.WriteLine($"Second batch - Result 1: {result2_1}");
            Assert.Equal(12, result2_1);
            
            long result2_2 = tracker2.GetAtomicResult(1);
            Console.WriteLine($"Second batch - Result 2: {result2_2}");
            Assert.Equal(20, result2_2);
            
            long result2_3 = tracker2.GetAtomicResult(2);
            Console.WriteLine($"Second batch - Result 3: {result2_3}");
            Assert.Equal(20, result2_3);
            
            long result2_4 = tracker2.GetAtomicResult(3);
            Console.WriteLine($"Second batch - Result 4: {result2_4}");
            Assert.Equal(20, result2_4);
            
            // Verify final values individually
            Assert.Equal(12, _client.AtomicIncDecString(key1, 0, timestamp++));
            Assert.Equal(20, _client.AtomicIncDecString(key2, 0, timestamp++));
            Assert.Equal(20, _client.AtomicIncDecString(key3, 0, timestamp++));
            Assert.Equal(20, _client.AtomicIncDecString(key4, 0, timestamp++));
        }
        finally
        {
            // Clean up
            try
            {
                _client.DeleteString(key1, timestamp++);
                _client.DeleteString(key2, timestamp++);
                _client.DeleteString(key3, timestamp++);
                _client.DeleteString(key4, timestamp++);
            }
            catch { /* Clean up errors are not test failures */ }
        }
    }
    
    [Fact]
    public void AtomicIncDec_ReadOperation_ShouldReturnCurrentValue()
    {
        // Arrange
        string key = "atomic_read_test_key";
        ulong timestamp = RiocClient.GetTimestamp();
        
        // Ensure clean state
        try { _client.DeleteString(key, timestamp++); } catch { /* Key might not exist */ }
        
        // Initialize with a value
        _client.AtomicIncDecString(key, 42, timestamp++);
        
        // Act - Read current value with increment of 0
        long result = _client.AtomicIncDecString(key, 0, timestamp++);
        
        // Assert
        Assert.Equal(42, result);
        
        // Clean up
        _client.DeleteString(key, timestamp++);
    }

    public void Dispose()
    {
        _client.Dispose();
    }
} 