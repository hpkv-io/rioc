using System.Runtime.InteropServices;
using HPKV.RIOC.Native;
using Microsoft.Extensions.Logging;
using System.Text;

namespace HPKV.RIOC;

/// <summary>
/// Represents a batch of RIOC operations that can be executed together.
/// </summary>
public sealed unsafe class RiocBatch : IDisposable
{
    private readonly void* _handle;
    private readonly ILogger? _logger;
    private bool _disposed;

    internal RiocBatch(void* handle, ILogger? logger = null)
    {
        _handle = handle;
        _logger = logger;
    }

    /// <summary>
    /// Adds a GET operation to the batch.
    /// </summary>
    /// <param name="key">The key to get.</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the batch has been disposed.</exception>
    public void AddGet(ReadOnlySpan<byte> key)
    {
        ThrowIfDisposed();

        fixed (byte* keyPtr = key)
        {
            int result = RiocNative.rioc_batch_add_get(_handle, keyPtr, (nuint)key.Length);
            if (result != 0)
            {
                _logger?.LogError("Failed to add GET operation to batch. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }
        }
    }

    /// <summary>
    /// Adds an INSERT operation to the batch.
    /// </summary>
    /// <param name="key">The key to insert.</param>
    /// <param name="value">The value to insert.</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the batch has been disposed.</exception>
    public void AddInsert(ReadOnlySpan<byte> key, ReadOnlySpan<byte> value, ulong timestamp)
    {
        ThrowIfDisposed();

        fixed (byte* keyPtr = key)
        fixed (byte* valuePtr = value)
        {
            int result = RiocNative.rioc_batch_add_insert(_handle, keyPtr, (nuint)key.Length, valuePtr, (nuint)value.Length, timestamp);
            if (result != 0)
            {
                _logger?.LogError("Failed to add INSERT operation to batch. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }
        }
    }

    /// <summary>
    /// Adds a DELETE operation to the batch.
    /// </summary>
    /// <param name="key">The key to delete.</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the batch has been disposed.</exception>
    public void AddDelete(ReadOnlySpan<byte> key, ulong timestamp)
    {
        ThrowIfDisposed();

        fixed (byte* keyPtr = key)
        {
            int result = RiocNative.rioc_batch_add_delete(_handle, keyPtr, (nuint)key.Length, timestamp);
            if (result != 0)
            {
                _logger?.LogError("Failed to add DELETE operation to batch. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }
        }
    }

    /// <summary>
    /// Adds a RANGE QUERY operation to the batch.
    /// </summary>
    /// <param name="startKey">The start key of the range (inclusive).</param>
    /// <param name="endKey">The end key of the range (inclusive).</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the batch has been disposed.</exception>
    public void AddRangeQuery(ReadOnlySpan<byte> startKey, ReadOnlySpan<byte> endKey)
    {
        ThrowIfDisposed();

        fixed (byte* startKeyPtr = startKey)
        fixed (byte* endKeyPtr = endKey)
        {
            int result = RiocNative.rioc_batch_add_range_query(_handle, startKeyPtr, (nuint)startKey.Length, 
                                                             endKeyPtr, (nuint)endKey.Length);
            if (result != 0)
            {
                _logger?.LogError("Failed to add RANGE QUERY operation to batch. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }
        }
    }

    /// <summary>
    /// Adds an atomic increment/decrement operation to the batch.
    /// </summary>
    /// <param name="key">The key of the counter.</param>
    /// <param name="increment">The value to add to the counter (can be negative to decrement).</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the batch has been disposed.</exception>
    public void AddAtomicIncDec(ReadOnlySpan<byte> key, long increment, ulong timestamp)
    {
        ThrowIfDisposed();

        fixed (byte* keyPtr = key)
        {
            int result = RiocNative.rioc_batch_add_atomic_inc_dec(_handle, keyPtr, (nuint)key.Length, increment, timestamp);
            if (result != 0)
            {
                _logger?.LogError("Failed to add ATOMIC_INC_DEC operation to batch. Error code: {ErrorCode}", result);
                throw RiocExceptionFactory.Create(result);
            }
        }
    }

    /// <summary>
    /// Adds an atomic increment/decrement operation to the batch.
    /// </summary>
    /// <param name="key">The key of the counter as a string.</param>
    /// <param name="increment">The value to add to the counter (can be negative to decrement).</param>
    /// <param name="timestamp">The timestamp for the operation.</param>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the batch has been disposed.</exception>
    public void AddAtomicIncDecString(string key, long increment, ulong timestamp)
    {
        AddAtomicIncDec(Encoding.UTF8.GetBytes(key), increment, timestamp);
    }

    /// <summary>
    /// Executes the batch operations asynchronously.
    /// </summary>
    /// <returns>A tracker for the batch execution.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the batch has been disposed.</exception>
    public RiocBatchTracker ExecuteAsync()
    {
        ThrowIfDisposed();

        void* tracker = RiocNative.rioc_batch_execute_async(_handle);
        if (tracker == null)
        {
            _logger?.LogError("Failed to execute batch asynchronously");
            throw new RiocException(-3, "Failed to execute batch asynchronously");
        }

        return new RiocBatchTracker(tracker, _logger);
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(RiocBatch));
        }
    }

    /// <summary>
    /// Disposes the batch.
    /// </summary>
    public void Dispose()
    {
        if (!_disposed)
        {
            RiocNative.rioc_batch_free(_handle);
            _disposed = true;
        }
    }
}

/// <summary>
/// Tracks the execution of a batch of RIOC operations.
/// </summary>
public sealed unsafe class RiocBatchTracker : IDisposable
{
    private readonly void* _handle;
    private readonly ILogger? _logger;
    private bool _disposed;

    internal RiocBatchTracker(void* handle, ILogger? logger = null)
    {
        _handle = handle;
        _logger = logger;
    }

    /// <summary>
    /// Waits for the batch operations to complete.
    /// </summary>
    /// <param name="timeoutMs">The timeout in milliseconds. Use 0 for no timeout.</param>
    /// <exception cref="RiocException">Thrown when the operation fails or times out.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the tracker has been disposed.</exception>
    public void Wait(int timeoutMs = 0)
    {
        ThrowIfDisposed();

        int result = RiocNative.rioc_batch_wait(_handle, timeoutMs);
        if (result != 0)
        {
            _logger?.LogError("Failed to wait for batch completion. Error code: {ErrorCode}", result);
            throw RiocExceptionFactory.Create(result);
        }
    }

    /// <summary>
    /// Gets the response for a GET operation in the batch.
    /// </summary>
    /// <param name="index">The index of the operation in the batch.</param>
    /// <returns>The value for the GET operation.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the tracker has been disposed.</exception>
    public byte[] GetResponse(nuint index)
    {
        ThrowIfDisposed();

        byte* valuePtr;
        nuint valueLen;

        int result = RiocNative.rioc_batch_get_response_async(_handle, index, &valuePtr, &valueLen);
        if (result != 0)
        {
            _logger?.LogError("Failed to get batch response at index {Index}. Error code: {ErrorCode}", index, result);
            throw RiocExceptionFactory.Create(result);
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
    /// Gets the response for a RANGE QUERY operation in the batch.
    /// </summary>
    /// <param name="index">The index of the operation in the batch.</param>
    /// <returns>A list of key-value pairs within the specified range.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the tracker has been disposed.</exception>
    public List<KeyValuePair<byte[], byte[]>> GetRangeQueryResponse(nuint index)
    {
        ThrowIfDisposed();

        byte* valuePtr;
        nuint valueLen;

        int result = RiocNative.rioc_batch_get_response_async(_handle, index, &valuePtr, &valueLen);
        if (result != 0)
        {
            _logger?.LogError("Failed to get batch range query response at index {Index}. Error code: {ErrorCode}", index, result);
            throw RiocExceptionFactory.Create(result);
        }

        if (valuePtr == null || valueLen == 0)
        {
            return new List<KeyValuePair<byte[], byte[]>>();
        }

        // For range query, valueLen is the count of results
        // and valuePtr points to an array of NativeRangeResult structs
        NativeRangeResult* resultsPtr = (NativeRangeResult*)valuePtr;
        List<KeyValuePair<byte[], byte[]>> results = new List<KeyValuePair<byte[], byte[]>>((int)valueLen);

        for (nuint i = 0; i < valueLen; i++)
        {
            NativeRangeResult rangeResult = resultsPtr[i];
            
            // Copy key
            byte[] key = new byte[rangeResult.key_len];
            Marshal.Copy((IntPtr)rangeResult.key, key, 0, (int)rangeResult.key_len);
            
            // Copy value
            byte[] value = new byte[rangeResult.value_len];
            Marshal.Copy((IntPtr)rangeResult.value, value, 0, (int)rangeResult.value_len);
            
            results.Add(new KeyValuePair<byte[], byte[]>(key, value));
        }

        return results;
    }

    /// <summary>
    /// Gets the response for a RANGE QUERY operation in the batch as UTF-8 strings.
    /// </summary>
    /// <param name="index">The index of the operation in the batch.</param>
    /// <returns>A list of key-value pairs within the specified range as strings.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the tracker has been disposed.</exception>
    public List<KeyValuePair<string, string>> GetRangeQueryResponseString(nuint index)
    {
        var results = GetRangeQueryResponse(index);
        
        return results.Select(kv => new KeyValuePair<string, string>(
            System.Text.Encoding.UTF8.GetString(kv.Key),
            System.Text.Encoding.UTF8.GetString(kv.Value)
        )).ToList();
    }

    /// <summary>
    /// Gets the result of an atomic increment/decrement operation.
    /// </summary>
    /// <param name="index">The index of the operation in the batch.</param>
    /// <returns>The new value of the counter after the operation.</returns>
    /// <exception cref="RiocException">Thrown when the operation fails.</exception>
    /// <exception cref="ObjectDisposedException">Thrown when the tracker has been disposed.</exception>
    public long GetAtomicResult(nuint index)
    {
        ThrowIfDisposed();

        byte[] responseBytes = GetResponse(index);
        
        if (responseBytes.Length != sizeof(long))
        {
            _logger?.LogError("Invalid atomic response size: {ValueLen}, expected {ExpectedSize}", 
                responseBytes.Length, sizeof(long));
            throw RiocExceptionFactory.Create(-1); // Parameter error
        }
        
        // Convert bytes to long
        return BitConverter.ToInt64(responseBytes, 0);
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(RiocBatchTracker));
        }
    }

    /// <summary>
    /// Disposes the batch tracker.
    /// </summary>
    public void Dispose()
    {
        if (!_disposed)
        {
            RiocNative.rioc_batch_tracker_free(_handle);
            _disposed = true;
        }
    }
} 