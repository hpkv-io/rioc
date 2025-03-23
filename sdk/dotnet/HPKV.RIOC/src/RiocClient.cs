using System.Runtime.InteropServices;
using System.Text;
using HPKV.RIOC.Native;
using Microsoft.Extensions.Logging;

namespace HPKV.RIOC;

/// <summary>
/// A client for interacting with the RIOC key-value store.
/// </summary>
public sealed unsafe class RiocClient : IDisposable
{
    private readonly void* _handle;
    private readonly ILogger? _logger;
    private bool _disposed;

    /// <summary>
    /// Creates a new instance of the RIOC client.
    /// </summary>
    /// <param name="config">The client configuration.</param>
    /// <param name="logger">Optional logger for client operations.</param>
    /// <exception cref="RiocException">Thrown when the client fails to initialize.</exception>
    public RiocClient(RiocConfig config, ILogger? logger = null)
    {
        _logger = logger;

        // Initialize platform
        int result = RiocNative.rioc_platform_init();
        if (result != 0)
        {
            _logger?.LogError("Failed to initialize RIOC platform. Error code: {ErrorCode}", result);
            throw RiocExceptionFactory.Create(result);
        }

        // Convert strings to UTF-8 bytes
        byte[] hostBytes = Encoding.UTF8.GetBytes(config.Host);
        byte[]? certPathBytes = config.Tls?.CertificatePath != null ? Encoding.UTF8.GetBytes(config.Tls.CertificatePath) : null;
        byte[]? keyPathBytes = config.Tls?.KeyPath != null ? Encoding.UTF8.GetBytes(config.Tls.KeyPath) : null;
        byte[]? caPathBytes = config.Tls?.CaPath != null ? Encoding.UTF8.GetBytes(config.Tls.CaPath) : null;
        byte[]? verifyHostnameBytes = config.Tls?.VerifyHostname != null ? Encoding.UTF8.GetBytes(config.Tls.VerifyHostname) : null;

        // Prepare native config
        NativeTlsConfig* tlsConfig = null;
        NativeClientConfig nativeConfig;

        fixed (byte* hostPtr = hostBytes)
        fixed (byte* certPathPtr = certPathBytes)
        fixed (byte* keyPathPtr = keyPathBytes)
        fixed (byte* caPathPtr = caPathBytes)
        fixed (byte* verifyHostnamePtr = verifyHostnameBytes)
        {
            if (config.Tls != null)
            {
                tlsConfig = (NativeTlsConfig*)Marshal.AllocHGlobal(sizeof(NativeTlsConfig));
                tlsConfig->cert_path = certPathPtr;
                tlsConfig->key_path = keyPathPtr;
                tlsConfig->ca_path = caPathPtr;
                tlsConfig->verify_hostname = verifyHostnamePtr;
                tlsConfig->verify_peer = config.Tls.VerifyPeer;
            }

            nativeConfig.host = hostPtr;
            nativeConfig.port = (uint)config.Port;
            nativeConfig.timeout_ms = (uint)config.TimeoutMs;
            nativeConfig.tls = tlsConfig;

            void* handle;
            result = RiocNative.rioc_client_connect_with_config(&nativeConfig, &handle);

            if (tlsConfig != null)
            {
                Marshal.FreeHGlobal((IntPtr)tlsConfig);
            }

            if (result != 0)
            {
                _logger?.LogError("Failed to connect RIOC client. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }

            _handle = handle;
        }

        _logger?.LogInformation("RIOC client connected successfully");
    }

    /// <summary>
    /// Gets the value associated with the specified key.
    /// </summary>
    /// <param name="key">The key to get.</param>
    /// <returns>The value associated with the key.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public byte[] Get(ReadOnlySpan<byte> key)
    {
        ThrowIfDisposed();

        byte* valuePtr;
        nuint valueLen;

        fixed (byte* keyPtr = key)
        {
            int result = RiocNative.rioc_get(_handle, keyPtr, (nuint)key.Length, &valuePtr, &valueLen);
            if (result != 0)
            {
                _logger?.LogError("Failed to get value. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }
        }

        if (valuePtr == null || valueLen == 0)
        {
            return Array.Empty<byte>();
        }

        byte[] value = new byte[valueLen];
        Marshal.Copy((IntPtr)valuePtr, value, 0, (int)valueLen);
        return value;
    }

    /// <summary>
    /// Gets the value associated with the specified key as a UTF-8 string.
    /// </summary>
    /// <param name="key">The key to get.</param>
    /// <returns>The value associated with the key as a string.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public string GetString(string key)
    {
        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        byte[] valueBytes = Get(keyBytes);
        return Encoding.UTF8.GetString(valueBytes);
    }

    /// <summary>
    /// Inserts or updates a key-value pair.
    /// </summary>
    /// <param name="key">The key to insert or update.</param>
    /// <param name="value">The value to store.</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public void Insert(ReadOnlySpan<byte> key, ReadOnlySpan<byte> value, ulong timestamp)
    {
        ThrowIfDisposed();

        fixed (byte* keyPtr = key)
        fixed (byte* valuePtr = value)
        {
            int result = RiocNative.rioc_insert(_handle, keyPtr, (nuint)key.Length, valuePtr, (nuint)value.Length, timestamp);
            if (result != 0)
            {
                _logger?.LogError("Failed to insert value. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }
        }
    }

    /// <summary>
    /// Inserts or updates a key-value pair using UTF-8 strings.
    /// </summary>
    /// <param name="key">The key to insert or update.</param>
    /// <param name="value">The value to store.</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public void InsertString(string key, string value, ulong timestamp)
    {
        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        byte[] valueBytes = Encoding.UTF8.GetBytes(value);
        Insert(keyBytes, valueBytes, timestamp);
    }

    /// <summary>
    /// Deletes the specified key.
    /// </summary>
    /// <param name="key">The key to delete.</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public void Delete(ReadOnlySpan<byte> key, ulong timestamp)
    {
        ThrowIfDisposed();

        fixed (byte* keyPtr = key)
        {
            int result = RiocNative.rioc_delete(_handle, keyPtr, (nuint)key.Length, timestamp);
            if (result != 0)
            {
                _logger?.LogError("Failed to delete key. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }
        }
    }

    /// <summary>
    /// Deletes the specified key using a UTF-8 string.
    /// </summary>
    /// <param name="key">The key to delete.</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public void DeleteString(string key, ulong timestamp)
    {
        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        Delete(keyBytes, timestamp);
    }

    /// <summary>
    /// Performs a range query to retrieve all key-value pairs within the specified range.
    /// </summary>
    /// <param name="startKey">The start key of the range (inclusive).</param>
    /// <param name="endKey">The end key of the range (inclusive).</param>
    /// <returns>A list of key-value pairs within the specified range.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public List<KeyValuePair<byte[], byte[]>> RangeQuery(ReadOnlySpan<byte> startKey, ReadOnlySpan<byte> endKey)
    {
        ThrowIfDisposed();

        NativeRangeResult* resultsPtr = null;
        nuint resultCount = 0;

        fixed (byte* startKeyPtr = startKey)
        fixed (byte* endKeyPtr = endKey)
        {
            int result = RiocNative.rioc_range_query(_handle, startKeyPtr, (nuint)startKey.Length, 
                                                   endKeyPtr, (nuint)endKey.Length, 
                                                   &resultsPtr, &resultCount);
            if (result != 0)
            {
                _logger?.LogError("Failed to perform range query. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }
        }

        List<KeyValuePair<byte[], byte[]>> results = new List<KeyValuePair<byte[], byte[]>>((int)resultCount);

        try
        {
            for (nuint i = 0; i < resultCount; i++)
            {
                NativeRangeResult result = resultsPtr[i];
                
                // Copy key
                byte[] key = new byte[result.key_len];
                Marshal.Copy((IntPtr)result.key, key, 0, (int)result.key_len);
                
                // Copy value
                byte[] value = new byte[result.value_len];
                Marshal.Copy((IntPtr)result.value, value, 0, (int)result.value_len);
                
                results.Add(new KeyValuePair<byte[], byte[]>(key, value));
            }
        }
        finally
        {
            if (resultsPtr != null && resultCount > 0)
            {
                RiocNative.rioc_free_range_results(resultsPtr, resultCount);
            }
        }

        return results;
    }

    /// <summary>
    /// Performs a range query to retrieve all key-value pairs within the specified range using UTF-8 strings.
    /// </summary>
    /// <param name="startKey">The start key of the range (inclusive).</param>
    /// <param name="endKey">The end key of the range (inclusive).</param>
    /// <returns>A list of key-value pairs within the specified range as strings.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public List<KeyValuePair<string, string>> RangeQueryString(string startKey, string endKey)
    {
        byte[] startKeyBytes = Encoding.UTF8.GetBytes(startKey);
        byte[] endKeyBytes = Encoding.UTF8.GetBytes(endKey);
        
        var results = RangeQuery(startKeyBytes, endKeyBytes);
        
        return results.Select(kv => new KeyValuePair<string, string>(
            Encoding.UTF8.GetString(kv.Key),
            Encoding.UTF8.GetString(kv.Value)
        )).ToList();
    }

    /// <summary>
    /// Creates a new batch for executing multiple operations together.
    /// </summary>
    /// <returns>A new batch instance.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public RiocBatch CreateBatch()
    {
        ThrowIfDisposed();

        void* batch = RiocNative.rioc_batch_create(_handle);
        if (batch == null)
        {
            _logger?.LogError("Failed to create batch");
            throw new RiocException(-3, "Failed to create batch");
        }

        return new RiocBatch(batch, _logger);
    }

    /// <summary>
    /// Gets the current timestamp in nanoseconds.
    /// </summary>
    /// <returns>The current timestamp in nanoseconds.</returns>
    public static ulong GetTimestamp()
    {
        return RiocNative.rioc_get_timestamp_ns();
    }

    /// <summary>
    /// Atomically increments or decrements a counter stored at the specified key.
    /// </summary>
    /// <param name="key">The key of the counter.</param>
    /// <param name="increment">The value to add to the counter (can be negative to decrement).</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <returns>The new value of the counter after the operation.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public long AtomicIncDec(ReadOnlySpan<byte> key, long increment, ulong timestamp)
    {
        ThrowIfDisposed();

        long result = 0;
        fixed (byte* keyPtr = key)
        {
            int status = RiocNative.rioc_atomic_inc_dec(_handle, keyPtr, (nuint)key.Length, increment, timestamp, &result);
            if (status != 0)
            {
                _logger?.LogError("Failed to perform atomic increment/decrement. Error code: {ErrorCode}", status);
                throw RiocExceptionFactory.Create(status);
            }
        }
        return result;
    }

    /// <summary>
    /// Atomically increments or decrements a counter stored at the specified key.
    /// </summary>
    /// <param name="key">The key of the counter as a string.</param>
    /// <param name="increment">The value to add to the counter (can be negative to decrement).</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <returns>The new value of the counter after the operation.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the client has been disposed.</exception>
    public long AtomicIncDecString(string key, long increment, ulong timestamp)
    {
        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        return AtomicIncDec(keyBytes, increment, timestamp);
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(RiocClient));
        }
    }

    /// <summary>
    /// Disposes the client.
    /// </summary>
    public void Dispose()
    {
        if (!_disposed)
        {
            RiocNative.rioc_client_disconnect_with_config(_handle);
            RiocNative.rioc_platform_cleanup();
            _disposed = true;
        }
    }
} 