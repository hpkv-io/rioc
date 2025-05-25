using System.Text;
using CommandLine;
using HPKV.RIOC;
using Microsoft.Extensions.Logging;
using System.Diagnostics;
using System.Threading;

namespace HPKV.RIOC.TestApps.Benchmark;

class Options
{
    [Option('h', "host", Default = "127.0.0.1", HelpText = "Server host")]
    public string Host { get; set; } = "127.0.0.1";

    [Option('p', "port", Default = 8000, HelpText = "Server port")]
    public int Port { get; set; } = 8000;

    [Option('t', "tls", HelpText = "Enable TLS")]
    public bool UseTls { get; set; }

    [Option('c', "cert", Default = "", HelpText = "Path to CA certificate")]
    public string CertPath { get; set; } = "";

    [Option("ca", Default = "", HelpText = "Path to CA certificate")]
    public string CaPath { get; set; } = "";

    [Option("client-cert", Default = "", HelpText = "Path to client certificate")]
    public string ClientCertPath { get; set; } = "";

    [Option("client-key", Default = "", HelpText = "Path to client private key")]
    public string ClientKeyPath { get; set; } = "";

    [Option('n', "num-threads", Default = 1, HelpText = "Number of threads")]
    public int NumThreads { get; set; } = 1;

    [Option('o', "num-ops", Default = 10000, HelpText = "Number of operations per thread")]
    public int NumOps { get; set; } = 10000;

    [Option('s', "value-size", Default = 100, HelpText = "Value size in bytes")]
    public int ValueSize { get; set; } = 100;

    [Option('v', "verify", Default = false, HelpText = "Verify values")]
    public bool VerifyValues { get; set; }
}

class ThreadStats
{
    public List<double> InsertLatencies { get; } = new();
    public List<double> GetLatencies { get; } = new();
    public List<double> DeleteLatencies { get; } = new();
    public List<double> RangeQueryLatencies { get; } = new();
    public int InsertErrors { get; set; }
    public int GetErrors { get; set; }
    public int DeleteErrors { get; set; }
    public int RangeQueryErrors { get; set; }
}

class Program
{
    private static readonly ILoggerFactory LoggerFactory = Microsoft.Extensions.Logging.LoggerFactory.Create(builder =>
    {
        builder.AddConsole().SetMinimumLevel(LogLevel.Information);
    });

    private static readonly ILogger Logger = LoggerFactory.CreateLogger<Program>();

    private const int DefaultNumOperations = 10000;
    private const int DefaultValueSize = 100;
    private const int DefaultBatchSize = 100;
    private const int DefaultNumThreads = 1;
    private const int DefaultWarmupCount = 1000;
    private const int DefaultRangeQueryCount = 100;

    static void Main(string[] args)
    {
        Parser.Default.ParseArguments<Options>(args)
            .WithParsed(RunBenchmark);
    }

    static void RunBenchmark(Options options)
    {
        // Create logger
        using var loggerFactory = LoggerFactory;
        var logger = loggerFactory.CreateLogger<Program>();

        try
        {
            Console.WriteLine("Benchmark Configuration:");
            Console.WriteLine($"  Host:          {options.Host}");
            Console.WriteLine($"  Port:          {options.Port}");
            Console.WriteLine($"  TLS:           {(options.UseTls ? "enabled" : "disabled")}");
            Console.WriteLine($"  Threads:       {options.NumThreads}");
            Console.WriteLine($"  Ops/thread:    {options.NumOps}");
            Console.WriteLine($"  Value size:    {options.ValueSize} bytes");
            Console.WriteLine($"  Verify values: {options.VerifyValues}");

            var tasks = new List<Task>();
            var stats = new ThreadStats[options.NumThreads];

            for (var i = 0; i < options.NumThreads; i++)
            {
                stats[i] = new ThreadStats();
                var threadId = i;
                var threadStats = stats[i];
                tasks.Add(Task.Run(() => RunThread(threadId, options, threadStats, loggerFactory)));
            }

            try
            {
                Task.WaitAll(tasks.ToArray());
            }
            catch (Exception ex)
            {
                logger.LogError(ex, "Error in benchmark threads");
                throw;
            }

            // Aggregate and print results
            PrintResults(options, stats);
        }
        catch (Exception ex)
        {
            logger.LogError(ex, "Error in benchmark");
            throw;
        }
    }

    static void RunThread(int threadId, Options options, ThreadStats stats, ILoggerFactory loggerFactory)
    {
        var logger = loggerFactory.CreateLogger<Program>();

        try
        {
            // Create client configuration
            var config = new RiocConfig
            {
                Host = options.Host,
                Port = options.Port,
                TimeoutMs = 5000,
                Tls = options.UseTls ? new RiocTlsConfig
                {
                    CaPath = options.CaPath,
                    CertificatePath = options.ClientCertPath,
                    KeyPath = options.ClientKeyPath,
                    VerifyHostname = options.Host,
                    VerifyPeer = true
                } : null
            };

            // Create test value
            var value = new byte[options.ValueSize];
            for (var i = 0; i < options.ValueSize; i++)
            {
                value[i] = (byte)('A' + (i % 26));
            }

            using var client = new RiocClient(config, loggerFactory.CreateLogger<RiocClient>());

            // Insert phase
            Console.WriteLine($"Thread {threadId}: Starting insert phase...");
            var batchSize = 128;
            var timestamp = RiocClient.GetTimestamp();

            for (var i = 0; i < options.NumOps; i += batchSize)
            {
                var count = Math.Min(batchSize, options.NumOps - i);

                try
                {
                    using var batch = client.CreateBatch();
                    for (var j = 0; j < count; j++)
                    {
                        var key = $"key_{threadId}_{i + j}";
                        batch.AddInsert(Encoding.UTF8.GetBytes(key), value, timestamp + (ulong)(i + j));
                    }

                    // Measure only the batch execute time
                    var batchStartTime = DateTime.UtcNow;
                    batch.ExecuteAsync().Wait();
                    var latency = (DateTime.UtcNow - batchStartTime).TotalMicroseconds / count;
                    
                    for (var j = 0; j < count; j++)
                    {
                        stats.InsertLatencies.Add(latency);
                    }
                }
                catch (Exception ex)
                {
                    logger.LogError(ex, "Error in insert batch");
                    stats.InsertErrors += count;
                }

                if ((i + count) % 1000 == 0)
                {
                    Console.WriteLine($"Thread {threadId}: Completed {i + count} inserts");
                }
            }

            // Get phase
            Console.WriteLine($"Thread {threadId}: Starting get phase...");
            for (var i = 0; i < options.NumOps; i += batchSize)
            {
                var count = Math.Min(batchSize, options.NumOps - i);

                try
                {
                    using var batch = client.CreateBatch();
                    for (var j = 0; j < count; j++)
                    {
                        var key = $"key_{threadId}_{i + j}";
                        batch.AddGet(Encoding.UTF8.GetBytes(key));
                    }

                    // Measure only the batch execute time
                    var batchStartTime = DateTime.UtcNow;
                    batch.ExecuteAsync().Wait();
                    var latency = (DateTime.UtcNow - batchStartTime).TotalMicroseconds / count;

                    if (options.VerifyValues)
                    {
                        for (var j = 0; j < count; j++)
                        {
                            try
                            {
                                var result = tracker.GetResponse((nuint)j);
                                if (!result.SequenceEqual(value))
                                {
                                    logger.LogError("Value mismatch at index {Index}", j);
                                    stats.GetErrors++;
                                }
                            }
                            catch (Exception ex)
                            {
                                logger.LogError(ex, "Error getting response at index {Index}", j);
                                stats.GetErrors++;
                            }
                        }
                    }

                    for (var j = 0; j < count; j++)
                    {
                        stats.GetLatencies.Add(latency);
                    }
                }
                catch (Exception ex)
                {
                    logger.LogError(ex, "Error in get batch");
                    stats.GetErrors += count;
                }

                if ((i + count) % 1000 == 0)
                {
                    Console.WriteLine($"Thread {threadId}: Completed {i + count} gets");
                }
            }

            // Delete phase
            Console.WriteLine($"Thread {threadId}: Starting delete phase...");
            timestamp = RiocClient.GetTimestamp();

            for (var i = 0; i < options.NumOps; i += batchSize)
            {
                var count = Math.Min(batchSize, options.NumOps - i);

                try
                {
                    using var batch = client.CreateBatch();
                    for (var j = 0; j < count; j++)
                    {
                        var key = $"key_{threadId}_{i + j}";
                        batch.AddDelete(Encoding.UTF8.GetBytes(key), timestamp + (ulong)(i + j));
                    }

                    // Measure only the batch execute time
                    var batchStartTime = DateTime.UtcNow;
                    batch.ExecuteAsync().Wait();
                    var latency = (DateTime.UtcNow - batchStartTime).TotalMicroseconds / count;
                    
                    for (var j = 0; j < count; j++)
                    {
                        stats.DeleteLatencies.Add(latency);
                    }
                }
                catch (Exception ex)
                {
                    logger.LogError(ex, "Error in delete batch");
                    stats.DeleteErrors += count;
                }

                if ((i + count) % 1000 == 0)
                {
                    Console.WriteLine($"Thread {threadId}: Completed {i + count} deletes");
                }
            }

            Console.WriteLine($"Thread {threadId}: Benchmark complete");

            // Range query tests
            Console.WriteLine("\n--- Range Query Operations ---");

            try
            {
                // Use a much smaller number of operations for range queries to avoid memory issues
                int rangeOps = 20;  // Limit to 20 keys per thread for range queries
                
                // First, insert data for range queries with tenant-specific prefixes
                Console.WriteLine($"Inserting {rangeOps} keys for range query benchmark...");
                timestamp = RiocClient.GetTimestamp();
                
                for (int i = 0; i < rangeOps; i++)
                {
                    try
                    {
                        // Use a tenant-specific prefix to ensure isolation between threads
                        string rangeKey = $"tenant{threadId}:range_{i}";
                        string rangeValue = $"value_for_{rangeKey}";
                        
                        client.InsertString(rangeKey, rangeValue, timestamp + (ulong)i);
                        
                        // Add a small delay every 5 inserts to avoid overwhelming the server
                        if (i > 0 && i % 5 == 0)
                        {
                            Thread.Sleep(100);  // 100ms delay
                        }
                    }
                    catch (Exception ex)
                    {
                        logger.LogError(ex, "Failed to insert range key at index {Index}", i);
                    }
                }
                
                // Delay after inserts to ensure processing completes before starting queries
                Thread.Sleep(1000);  // 1 second delay

                // Benchmark range queries
                Console.WriteLine($"Benchmarking range queries with sliding window...");
                var rangeQueryTimer = Stopwatch.StartNew();
                
                // For range query, use a sliding window approach with smaller ranges
                int rangeSize = 5;  // Even smaller range size to avoid memory issues
                int successfulQueries = 0;
                int failedQueries = 0;

                // Only do a few range queries for testing
                int numRangeQueries = 4;
                
                for (int i = 0; i < Math.Min(rangeOps, numRangeQueries * rangeSize); i += rangeSize)
                {
                    // Calculate start and end keys for this range
                    int startIdx = i;
                    int endIdx = Math.Min(i + rangeSize - 1, rangeOps - 1);
                    
                    // Use tenant-specific prefix to ensure isolation between threads
                    string startKey = $"tenant{threadId}:range_{startIdx}";
                    string endKey = $"tenant{threadId}:range_{endIdx}";
                    
                    // Try the range query with retries for transient errors
                    int maxRetries = 3;
                    int retryCount = 0;
                    bool operationSucceeded = false;
                    
                    while (retryCount < maxRetries && !operationSucceeded)
                    {
                        if (retryCount > 0)
                        {
                            // Small delay before retry (exponential backoff)
                            int delayMs = 100 * (1 << retryCount);  // 200ms, 400ms, 800ms
                            Thread.Sleep(delayMs);
                        }
                        
                        try
                        {
                            // Measure only the query execute time
                            var batchStartTime = DateTime.UtcNow;
                            var results = client.RangeQueryString(startKey, endKey);
                            var latency = (DateTime.UtcNow - batchStartTime).TotalMicroseconds;
                            stats.RangeQueryLatencies.Add(latency);
                            
                            // Verification (not timed)
                            if (options.VerifyValues)
                            {
                                foreach (var kv in results)
                                {
                                    string expectedValue = $"value_for_{kv.Key}";
                                    if (kv.Value != expectedValue)
                                    {
                                        logger.LogWarning("Value mismatch for key {Key}", kv.Key);
                                    }
                                }
                            }
                            
                            operationSucceeded = true;
                            successfulQueries++;
                        }
                        catch (RiocKeyNotFoundException)
                        {
                            // This is not really an error, just empty results
                            operationSucceeded = true;
                            successfulQueries++;
                        }
                        catch (RiocException ex)
                        {
                            logger.LogWarning("Range query failed (retry {RetryCount}): {Message}", retryCount, ex.Message);
                            retryCount++;
                        }
                        catch (Exception ex)
                        {
                            logger.LogError(ex, "Unexpected error in range query");
                            retryCount++;
                        }
                    }
                    
                    // If all retries failed, count as error
                    if (!operationSucceeded)
                    {
                        stats.RangeQueryErrors++;
                        failedQueries++;
                    }
                    
                    // Add a larger delay between range queries to avoid overloading the server
                    Thread.Sleep(200);  // 200ms delay
                }

                rangeQueryTimer.Stop();
                if (successfulQueries > 0)
                {
                    double rangeQueryThroughput = successfulQueries / rangeQueryTimer.Elapsed.TotalSeconds;
                    Console.WriteLine($"Range Query: {rangeQueryThroughput:F2} queries/sec ({rangeQueryTimer.Elapsed.TotalMilliseconds:F2} ms for {successfulQueries} successful queries)");
                }
                else
                {
                    Console.WriteLine("Range Query: No successful queries");
                }
                Console.WriteLine($"  Successful queries: {successfulQueries}");
                Console.WriteLine($"  Failed queries: {failedQueries}");

                // Benchmark batch range queries
                Console.WriteLine("Benchmarking batch range queries...");
                var batchRangeQueryTimer = Stopwatch.StartNew();
                
                // Use a very small batch size for range queries to avoid memory issues
                int rangeBatchSize = 2;  // Only 2 range queries per batch
                int numRangeBatches = (Math.Min(rangeOps / rangeSize, numRangeQueries) + rangeBatchSize - 1) / rangeBatchSize;
                int batchRangeSuccessfulQueries = 0;
                int batchRangeFailedQueries = 0;
                
                for (int b = 0; b < numRangeBatches; b++)
                {
                    try
                    {
                        using var batch = client.CreateBatch();
                        int startBatchIdx = b * rangeBatchSize * rangeSize;
                        int endBatchIdx = Math.Min(startBatchIdx + rangeBatchSize * rangeSize, rangeOps);
                        int queriesInBatch = 0;
                        
                        // Add range queries to the batch
                        for (int i = startBatchIdx; i < endBatchIdx; i += rangeSize)
                        {
                            // Calculate start and end indices for this query
                            int startIdx = i;
                            int endIdx = Math.Min(i + rangeSize - 1, rangeOps - 1);
                            
                            // Use tenant-specific prefix to ensure isolation between threads
                            string startKey = $"tenant{threadId}:range_{startIdx}";
                            string endKey = $"tenant{threadId}:range_{endIdx}";
                            
                            // Add range query to batch
                            batch.AddRangeQuery(Encoding.UTF8.GetBytes(startKey), Encoding.UTF8.GetBytes(endKey));
                            queriesInBatch++;
                        }
                        
                        if (queriesInBatch > 0)
                        {
                            // Execute batch with retries
                            int maxRetries = 3;
                            int retryCount = 0;
                            bool batchSucceeded = false;
                            
                            while (retryCount < maxRetries && !batchSucceeded)
                            {
                                if (retryCount > 0)
                                {
                                    // Small delay before retry (exponential backoff)
                                    int delayMs = 200 * (1 << retryCount);  // 400ms, 800ms, 1600ms
                                    Thread.Sleep(delayMs);
                                }
                                
                                try
                                {
                                    // Measure only the batch execute time
                                    var batchStartTime = DateTime.UtcNow;
                                    batch.ExecuteAsync().Wait();
                                    var latency = (DateTime.UtcNow - batchStartTime).TotalMicroseconds / queriesInBatch;
                                    
                                    // Process results (not timed)
                                    for (nuint i = 0; i < (nuint)queriesInBatch; i++)
                                    {
                                        try
                                        {
                                            var results = tracker.GetRangeQueryResponseString(i);
                                            
                                            // Record latency for each successful query
                                            stats.RangeQueryLatencies.Add(latency);
                                            batchRangeSuccessfulQueries++;
                                            
                                            // Verify results if needed
                                            if (options.VerifyValues)
                                            {
                                                foreach (var kv in results)
                                                {
                                                    string expectedValue = $"value_for_{kv.Key}";
                                                    if (kv.Value != expectedValue)
                                                    {
                                                        logger.LogWarning("Value mismatch for key {Key}", kv.Key);
                                                    }
                                                }
                                            }
                                        }
                                        catch (RiocKeyNotFoundException)
                                        {
                                            // Empty results, still count as success
                                            stats.RangeQueryLatencies.Add(latency);
                                            batchRangeSuccessfulQueries++;
                                        }
                                        catch (RiocException ex)
                                        {
                                            logger.LogWarning("Batch range query response {Index} failed: {Message}", i, ex.Message);
                                            batchRangeFailedQueries++;
                                            stats.RangeQueryErrors++;
                                        }
                                    }
                                    
                                    batchSucceeded = true;
                                }
                                catch (Exception ex)
                                {
                                    logger.LogError(ex, "Error executing batch range query");
                                }
                            }
                            
                            if (!batchSucceeded)
                            {
                                batchRangeFailedQueries += queriesInBatch;
                                stats.RangeQueryErrors += queriesInBatch;
                            }
                        }
                    }
                    catch (Exception ex)
                    {
                        logger.LogError(ex, "Error creating or executing batch for range queries");
                    }
                    
                    // Add a larger delay between batches to avoid overloading the server
                    Thread.Sleep(500);  // 500ms delay
                }
                
                batchRangeQueryTimer.Stop();
                if (batchRangeSuccessfulQueries > 0)
                {
                    double batchRangeQueryThroughput = batchRangeSuccessfulQueries / batchRangeQueryTimer.Elapsed.TotalSeconds;
                    Console.WriteLine($"Batch Range Query: {batchRangeQueryThroughput:F2} queries/sec ({batchRangeQueryTimer.Elapsed.TotalMilliseconds:F2} ms for {batchRangeSuccessfulQueries} successful queries)");
                }
                else
                {
                    Console.WriteLine("Batch Range Query: No successful queries");
                }
                Console.WriteLine($"  Successful queries: {batchRangeSuccessfulQueries}");
                Console.WriteLine($"  Failed queries: {batchRangeFailedQueries}");

                // Clean up data
                Console.WriteLine("Cleaning up range query data...");
                for (int i = 0; i < rangeOps; i++)
                {
                    string rangeKey = $"tenant{threadId}:range_{i}";
                    try
                    {
                        client.DeleteString(rangeKey, timestamp + (ulong)(rangeOps + i));
                    }
                    catch (RiocKeyNotFoundException)
                    {
                        // Key not found, ignore
                    }
                    catch (RiocException ex)
                    {
                        logger.LogWarning("Failed to delete key {Key}: {Message}", rangeKey, ex.Message);
                    }
                    catch (Exception ex)
                    {
                        logger.LogError(ex, "Unexpected error deleting key {Key}", rangeKey);
                    }
                    
                    // Add a small delay between deletes
                    Thread.Sleep(50);  // 50ms delay
                }
            }
            catch (Exception ex)
            {
                logger.LogError(ex, "Error in range query benchmark");
            }
        }
        catch (Exception ex)
        {
            logger.LogError(ex, "Error in thread {ThreadId}", threadId);
            throw;
        }
    }

    static void PrintResults(Options options, ThreadStats[] stats)
    {
        try
        {
            var allInsertLatencies = stats.SelectMany(s => s.InsertLatencies).OrderBy(l => l).ToList();
            var allGetLatencies = stats.SelectMany(s => s.GetLatencies).OrderBy(l => l).ToList();
            var allDeleteLatencies = stats.SelectMany(s => s.DeleteLatencies).OrderBy(l => l).ToList();
            var allRangeQueryLatencies = stats.SelectMany(s => s.RangeQueryLatencies).OrderBy(l => l).ToList();

            var totalInsertErrors = stats.Sum(s => s.InsertErrors);
            var totalGetErrors = stats.Sum(s => s.GetErrors);
            var totalDeleteErrors = stats.Sum(s => s.DeleteErrors);
            var totalRangeQueryErrors = stats.Sum(s => s.RangeQueryErrors);

            Console.WriteLine("\nBenchmark Results:");
            Console.WriteLine("=================");

            PrintOperationStats("INSERT", allInsertLatencies, totalInsertErrors, options);
            PrintOperationStats("GET", allGetLatencies, totalGetErrors, options);
            PrintOperationStats("DELETE", allDeleteLatencies, totalDeleteErrors, options);
            PrintOperationStats("RANGE QUERY", allRangeQueryLatencies, totalRangeQueryErrors, options, true);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Error printing results: {ex}");
            throw;
        }
    }

    static void PrintOperationStats(string operation, List<double> latencies, int errors, Options options, bool isRangeQuery = false)
    {
        try
        {
            if (latencies.Count == 0)
            {
                Console.WriteLine($"\n{operation}: No successful operations");
                return;
            }

            var totalOps = isRangeQuery ? 0 : options.NumThreads * options.NumOps; // Range queries have variable count
            var successfulOps = latencies.Count;
            var throughput = successfulOps / (latencies.Sum() / 1_000_000); // ops/sec

            // Calculate percentile indices safely
            var p50Index = (int)Math.Floor(latencies.Count * 0.50);
            var p95Index = (int)Math.Floor(latencies.Count * 0.95);
            var p99Index = (int)Math.Floor(latencies.Count * 0.99);

            // Ensure indices are within bounds
            p50Index = Math.Min(p50Index, latencies.Count - 1);
            p95Index = Math.Min(p95Index, latencies.Count - 1);
            p99Index = Math.Min(p99Index, latencies.Count - 1);

            Console.WriteLine($"\n{operation} Performance:");
            if (!isRangeQuery)
            {
                Console.WriteLine($"  Total operations: {totalOps}");
            }
            Console.WriteLine($"  Successful ops:   {successfulOps}");
            Console.WriteLine($"  Failed ops:       {errors}");
            Console.WriteLine($"  Throughput:       {throughput:F2} ops/sec");
            Console.WriteLine("  Latency (microseconds):");
            Console.WriteLine($"    Min:             {latencies.Min():F3}");
            Console.WriteLine($"    Max:             {latencies.Max():F3}");
            Console.WriteLine($"    Average:         {latencies.Average():F3}");
            Console.WriteLine($"    P50 (median):    {latencies[p50Index]:F3}");
            Console.WriteLine($"    P95:             {latencies[p95Index]:F3}");
            Console.WriteLine($"    P99:             {latencies[p99Index]:F3}");
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Error printing stats for {operation}: {ex}");
            throw;
        }
    }
} 