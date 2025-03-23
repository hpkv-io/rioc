"""
RIOC client implementation
"""

import ctypes
import threading
from contextlib import contextmanager
from typing import Optional, List, Dict, Any, Generator, Tuple

from .config import RiocConfig, RiocTlsConfig
from .exceptions import RiocError, create_rioc_error
from .native import (
    rioc_native,
    NativeClientConfig,
    NativeTlsConfig,
    NativeRangeResult,
)

class RangeQueryResult:
    """Represents a key-value pair returned from a range query."""
    def __init__(self, key: bytes, value: bytes):
        self.key = key
        self.value = value

class RiocBatchTracker:
    """Tracks the execution of a batch operation."""
    def __init__(self, handle: ctypes.c_void_p):
        self._handle = handle
        self._completed = False
        self._closed = False

    def wait(self, timeout_ms: int = -1) -> None:
        """Wait for the batch operation to complete."""
        if self._closed:
            return
        if self._completed:
            return

        result = rioc_native.lib.rioc_batch_wait(self._handle, timeout_ms)
        if result != 0:
            raise create_rioc_error(result)
        self._completed = True

    def get_response(self, index: int) -> bytes:
        """Get the response for a GET operation at the specified index."""
        if self._closed:
            raise RiocError(-1, "Batch tracker is closed")
        if not self._completed:
            raise RiocError(-1, "Batch operation not completed")

        value_ptr = ctypes.POINTER(ctypes.c_char)()
        value_len = ctypes.c_size_t()

        result = rioc_native.lib.rioc_batch_get_response_async(
            self._handle, index, ctypes.byref(value_ptr), ctypes.byref(value_len)
        )
        if result != 0:
            raise create_rioc_error(result)

        if not value_ptr or value_len.value == 0:
            return b""

        return ctypes.string_at(value_ptr, value_len.value)

    def get_range_query_response(self, index: int) -> List[RangeQueryResult]:
        """Get the response for a RANGE QUERY operation at the specified index."""
        if self._closed:
            raise RiocError(-1, "Batch tracker is closed")
        if not self._completed:
            raise RiocError(-1, "Batch operation not completed")

        value_ptr = ctypes.POINTER(ctypes.c_char)()
        value_len = ctypes.c_size_t()

        result = rioc_native.lib.rioc_batch_get_response_async(
            self._handle, index, ctypes.byref(value_ptr), ctypes.byref(value_len)
        )
        if result != 0:
            raise create_rioc_error(result)

        if not value_ptr or value_len.value == 0:
            return []

        # For range query, value_ptr points to an array of NativeRangeResult structs
        # and value_len is the count of results
        results_ptr = ctypes.cast(value_ptr, ctypes.POINTER(NativeRangeResult))
        results_count = value_len.value

        # Convert native results to Python objects
        results = []
        for i in range(results_count):
            result = results_ptr[i]
            key = ctypes.string_at(result.key, result.key_len)
            value = ctypes.string_at(result.value, result.value_len)
            results.append(RangeQueryResult(key, value))

        return results

    def get_atomic_result(self, index: int) -> int:
        """Get the response for an ATOMIC_INC_DEC operation at the specified index."""
        if self._closed:
            raise RiocError(-1, "Batch tracker is closed")
        if not self._completed:
            raise RiocError(-1, "Batch operation not completed")

        value_ptr = ctypes.POINTER(ctypes.c_char)()
        value_len = ctypes.c_size_t()

        result = rioc_native.lib.rioc_batch_get_response_async(
            self._handle, index, ctypes.byref(value_ptr), ctypes.byref(value_len)
        )
        if result != 0:
            raise create_rioc_error(result)

        if not value_ptr or value_len.value < ctypes.sizeof(ctypes.c_int64):
            return 0

        # For atomic operations, value_ptr points to an int64_t
        result_value = ctypes.cast(value_ptr, ctypes.POINTER(ctypes.c_int64))[0]
        return result_value

    def close(self) -> None:
        """Clean up the native resources."""
        if not self._closed and hasattr(self, "_handle") and self._handle:
            try:
                if not self._completed:
                    # Try to wait with a short timeout to ensure completion
                    try:
                        self.wait(timeout_ms=100)
                    except:  # pylint: disable=bare-except
                        pass
                rioc_native.lib.rioc_batch_tracker_free(self._handle)
            finally:
                self._handle = None
                self._closed = True

    def __del__(self):
        """Clean up the native resources."""
        try:
            self.close()
        except:  # pylint: disable=bare-except
            pass

class RiocBatch:
    """A batch of RIOC operations."""
    def __init__(self, handle: ctypes.c_void_p):
        self._handle = handle
        self._operations: List[Dict[str, Any]] = []
        self._closed = False

    def add_get(self, key: bytes) -> None:
        """Add a GET operation to the batch."""
        if self._closed:
            raise RiocError(-1, "Batch is closed")
        result = rioc_native.lib.rioc_batch_add_get(
            self._handle,
            key,
            len(key)
        )
        if result != 0:
            raise create_rioc_error(result)
        self._operations.append({"type": "get", "key": key})

    def add_insert(self, key: bytes, value: bytes, timestamp: int) -> None:
        """Add an INSERT operation to the batch."""
        if self._closed:
            raise RiocError(-1, "Batch is closed")
        result = rioc_native.lib.rioc_batch_add_insert(
            self._handle,
            key,
            len(key),
            value,
            len(value),
            timestamp
        )
        if result != 0:
            raise create_rioc_error(result)
        self._operations.append({
            "type": "insert",
            "key": key,
            "value": value,
            "timestamp": timestamp
        })

    def add_delete(self, key: bytes, timestamp: int) -> None:
        """Add a DELETE operation to the batch."""
        if self._closed:
            raise RiocError(-1, "Batch is closed")
        result = rioc_native.lib.rioc_batch_add_delete(
            self._handle,
            key,
            len(key),
            timestamp
        )
        if result != 0:
            raise create_rioc_error(result)
        self._operations.append({
            "type": "delete",
            "key": key,
            "timestamp": timestamp
        })

    def add_range_query(self, start_key: bytes, end_key: bytes) -> None:
        """Add a range query operation to the batch."""
        if self._closed:
            raise RiocError(-1, "Batch is closed")

        # Input validation
        if not isinstance(start_key, bytes):
            raise TypeError("start_key must be bytes")
        if not isinstance(end_key, bytes):
            raise TypeError("end_key must be bytes")
        
        # Call native method
        result = rioc_native.lib.rioc_batch_add_range_query(
            self._handle,
            start_key, len(start_key),
            end_key, len(end_key)
        )
        
        if result != 0:
            raise create_rioc_error(result)
        self._operations.append({
            "type": "range_query",
            "start_key": start_key,
            "end_key": end_key
        })

    def add_atomic_inc_dec(self, key: bytes, value: int, timestamp: int) -> None:
        """Add an atomic increment/decrement operation to the batch.

        Args:
            key: The key of the counter.
            value: The amount to increment (positive) or decrement (negative).
            timestamp: The timestamp for this operation.
        """
        if self._closed:
            raise RiocError(-1, "Batch is closed")

        # Input validation
        if not isinstance(key, bytes):
            raise TypeError("key must be bytes")
        if not isinstance(value, int):
            raise TypeError("value must be int")
        if not isinstance(timestamp, int):
            raise TypeError("timestamp must be int")

        # Call native method
        result = rioc_native.lib.rioc_batch_add_atomic_inc_dec(
            self._handle,
            key, len(key),
            value,
            timestamp
        )

        if result != 0:
            raise create_rioc_error(result)
        self._operations.append({
            "type": "atomic_inc_dec",
            "key": key,
            "value": value,
            "timestamp": timestamp
        })

    def execute(self) -> RiocBatchTracker:
        """Execute the batch operations."""
        if self._closed:
            raise RiocError(-1, "Batch is closed")
        tracker_handle = rioc_native.lib.rioc_batch_execute_async(self._handle)
        if not tracker_handle:
            raise RiocError(-1, "Failed to execute batch")
        return RiocBatchTracker(tracker_handle)

    def close(self) -> None:
        """Clean up the native resources."""
        if not self._closed and hasattr(self, "_handle") and self._handle:
            try:
                rioc_native.lib.rioc_batch_free(self._handle)
            finally:
                self._handle = None
                self._closed = True

    def __del__(self):
        """Clean up the native resources."""
        try:
            self.close()
        except:  # pylint: disable=bare-except
            pass

class RiocClient:
    """RIOC client for interacting with the HPKV store."""
    def __init__(self, config: RiocConfig):
        """Initialize the RIOC client."""
        # Initialize platform
        result = rioc_native.lib.rioc_platform_init()
        if result != 0:
            raise RiocError(result, "Failed to initialize platform")

        # Convert config to native config
        native_config = NativeClientConfig()
        native_config.host = config.host.encode("utf-8")
        native_config.port = config.port
        native_config.timeout_ms = config.timeout_ms

        # Handle TLS config
        native_tls_config = None
        if config.tls:
            native_tls_config = NativeTlsConfig()
            if config.tls.certificate_path:
                native_tls_config.cert_path = config.tls.certificate_path.encode("utf-8")
            if config.tls.key_path:
                native_tls_config.key_path = config.tls.key_path.encode("utf-8")
            if config.tls.ca_path:
                native_tls_config.ca_path = config.tls.ca_path.encode("utf-8")
            if config.tls.verify_hostname:
                native_tls_config.verify_hostname = config.tls.verify_hostname.encode("utf-8")
            native_tls_config.verify_peer = config.tls.verify_peer
            self._tls_config = native_tls_config  # Keep reference to prevent GC
            native_config.tls = ctypes.pointer(native_tls_config)
        else:
            native_config.tls = None
            self._tls_config = None

        # Connect to server
        client_handle = ctypes.c_void_p()
        result = rioc_native.lib.rioc_client_connect_with_config(
            ctypes.byref(native_config),
            ctypes.byref(client_handle)
        )
        if result != 0:
            raise create_rioc_error(result)

        self._handle = client_handle
        self._closed = False
        self._lock = threading.RLock()

    def get(self, key: bytes) -> bytes:
        """Get a value by key."""
        if self._closed:
            raise RiocError(-1, "Client is closed")

        with self._lock:
            value_ptr = ctypes.POINTER(ctypes.c_char)()
            value_len = ctypes.c_size_t()

            result = rioc_native.lib.rioc_get(
                self._handle,
                key,
                len(key),
                ctypes.byref(value_ptr),
                ctypes.byref(value_len)
            )
            if result != 0:
                raise create_rioc_error(result)

            if not value_ptr or value_len.value == 0:
                return b""

            return ctypes.string_at(value_ptr, value_len.value)

    def get_string(self, key: str) -> str:
        """Get a string value by string key."""
        value = self.get(key.encode("utf-8"))
        return value.decode("utf-8")

    def insert(self, key: bytes, value: bytes, timestamp: Optional[int] = None) -> None:
        """Insert or update a key-value pair."""
        if self._closed:
            raise RiocError(-1, "Client is closed")

        # Use current timestamp if not provided
        if timestamp is None:
            timestamp = self.get_timestamp()

        with self._lock:
            result = rioc_native.lib.rioc_insert(
                self._handle,
                key,
                len(key),
                value,
                len(value),
                timestamp
            )
            if result != 0:
                raise create_rioc_error(result)

    def insert_string(self, key: str, value: str, timestamp: Optional[int] = None) -> None:
        """Insert or update a string key-value pair."""
        self.insert(key.encode("utf-8"), value.encode("utf-8"), timestamp)

    def delete(self, key: bytes, timestamp: Optional[int] = None) -> None:
        """Delete a key-value pair."""
        if self._closed:
            raise RiocError(-1, "Client is closed")

        # Use current timestamp if not provided
        if timestamp is None:
            timestamp = self.get_timestamp()

        with self._lock:
            result = rioc_native.lib.rioc_delete(
                self._handle,
                key,
                len(key),
                timestamp
            )
            if result != 0:
                raise create_rioc_error(result)

    def delete_string(self, key: str, timestamp: Optional[int] = None) -> None:
        """Delete a key-value pair using string key."""
        return self.delete(key.encode("utf-8"), timestamp)

    def range_query(self, start_key: bytes, end_key: bytes) -> List[RangeQueryResult]:
        """Perform a range query to retrieve all key-value pairs within the specified range."""
        if self._closed:
            raise RiocError(-1, "Client is closed")

        with self._lock:
            results_ptr = ctypes.POINTER(NativeRangeResult)()
            results_count = ctypes.c_size_t()

            result = rioc_native.lib.rioc_range_query(
                self._handle,
                start_key,
                len(start_key),
                end_key,
                len(end_key),
                ctypes.byref(results_ptr),
                ctypes.byref(results_count)
            )
            if result != 0:
                raise create_rioc_error(result)

            # Convert native results to Python objects
            results = []
            if results_ptr and results_count.value > 0:
                for i in range(results_count.value):
                    native_result = results_ptr[i]
                    key = ctypes.string_at(native_result.key, native_result.key_len)
                    value = ctypes.string_at(native_result.value, native_result.value_len)
                    results.append(RangeQueryResult(key, value))

                # Free native results
                rioc_native.lib.rioc_free_range_results(results_ptr, results_count.value)

            return results

    def range_query_string(self, start_key: str, end_key: str) -> List[Tuple[str, str]]:
        """Perform a range query with string keys and return string results."""
        results = self.range_query(start_key.encode("utf-8"), end_key.encode("utf-8"))
        return [(result.key.decode("utf-8"), result.value.decode("utf-8")) for result in results]

    def create_batch(self) -> RiocBatch:
        """Create a new batch operation."""
        if self._closed:
            raise RiocError(-1, "Client is closed")

        with self._lock:
            batch_handle = rioc_native.lib.rioc_batch_create(self._handle)
            if not batch_handle:
                raise RiocError(-1, "Failed to create batch")
            return RiocBatch(batch_handle)

    @contextmanager
    def batch(self) -> Generator[RiocBatch, None, None]:
        """Context manager for batch operations."""
        batch = self.create_batch()
        try:
            yield batch
            tracker = batch.execute()
            tracker.wait()
            tracker.close()
        finally:
            batch.close()

    @staticmethod
    def get_timestamp() -> int:
        """Get the current timestamp in nanoseconds."""
        return rioc_native.lib.rioc_get_timestamp_ns()

    def close(self) -> None:
        """Close the client and release resources."""
        if not self._closed:
            with self._lock:
                if not self._closed and hasattr(self, "_handle") and self._handle:
                    try:
                        rioc_native.lib.rioc_client_disconnect_with_config(self._handle)
                    finally:
                        self._handle = None
                        self._closed = True
                        # Clean up platform
                        try:
                            rioc_native.lib.rioc_platform_cleanup()
                        except:  # pylint: disable=bare-except
                            pass

    def __del__(self):
        """Clean up the native resources."""
        try:
            self.close()
        except:  # pylint: disable=bare-except
            pass

    def atomic_inc_dec(self, key: bytes, value: int, timestamp: Optional[int] = None) -> int:
        """Atomically increment or decrement a counter value.

        Args:
            key: The key of the counter.
            value: The amount to increment (positive) or decrement (negative).
            timestamp: The timestamp for this operation (optional).

        Returns:
            The new value of the counter after the operation.
        """
        if not isinstance(key, bytes):
            raise TypeError("key must be bytes")
        if not isinstance(value, int):
            raise TypeError("value must be an integer")

        if timestamp is None:
            timestamp = self.get_timestamp()

        result_value = ctypes.c_int64()
        result = rioc_native.lib.rioc_atomic_inc_dec(
            self._handle,
            key, len(key),
            value,
            timestamp,
            ctypes.byref(result_value)
        )

        if result != 0:
            raise create_rioc_error(result)

        return result_value.value

    def atomic_inc_dec_string(self, key: str, value: int, timestamp: Optional[int] = None) -> int:
        """Atomically increment or decrement a counter value using string key.

        Args:
            key: The key of the counter.
            value: The amount to increment (positive) or decrement (negative).
            timestamp: The timestamp for this operation (optional).

        Returns:
            The new value of the counter after the operation.
        """
        return self.atomic_inc_dec(key.encode("utf-8"), value, timestamp) 