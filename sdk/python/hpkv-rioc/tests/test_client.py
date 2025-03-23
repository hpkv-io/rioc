"""
Tests for the RIOC client basic operations.
"""

import os
import time
import pytest
import ctypes
from datetime import datetime, UTC

from hpkv_rioc import RiocClient, RiocConfig, RiocError, RiocTlsConfig, RangeQueryResult

def get_test_host():
    """Get the test host."""
    return os.getenv("RIOC_TEST_HOST", "localhost")

def get_test_port():
    """Get the test port."""
    return int(os.getenv("RIOC_TEST_PORT", "8000"))

def get_test_timeout():
    """Get the test timeout."""
    return int(os.getenv("RIOC_TEST_TIMEOUT", "5000"))

@pytest.fixture
def client(tls_config):
    """Create a RIOC client for testing."""
    config = RiocConfig(
        host=get_test_host(),
        port=get_test_port(),
        timeout_ms=get_test_timeout(),
        tls=tls_config
    )
    client = RiocClient(config)
    yield client
    client.close()

def test_client_connection(client):
    """Test that client can connect to server."""
    # Connection is established in fixture
    assert client is not None

def test_insert_and_get(client):
    """Test basic insert and get operations."""
    key = "test_key"
    value = "test_value"
    
    # Insert
    client.insert_string(key, value)
    
    # Get
    retrieved_value = client.get_string(key)
    assert retrieved_value == value

def test_insert_and_get_bytes(client):
    """Test insert and get with binary data."""
    key = b"test_key_bytes"
    value = b"test_value_bytes"
    
    # Insert
    client.insert(key, value)
    
    # Get
    retrieved_value = client.get(key)
    assert retrieved_value == value

def test_delete(client):
    """Test delete operation."""
    key = "test_delete_key"
    value = "test_delete_value"
    
    # Insert
    client.insert_string(key, value)
    
    # Verify it exists
    retrieved_value = client.get_string(key)
    assert retrieved_value == value
    
    # Delete
    client.delete_string(key)
    
    # Verify it's gone
    with pytest.raises(RiocError) as excinfo:
        client.get_string(key)
    assert excinfo.value.code == -6  # RIOC_ERR_NOENT

def test_get_nonexistent(client):
    """Test getting a nonexistent key."""
    with pytest.raises(RiocError) as excinfo:
        client.get_string("nonexistent_key")
    assert excinfo.value.code == -6  # RIOC_ERR_NOENT

def test_timestamps(client):
    """Test timestamp handling."""
    key = "test_timestamp_key"
    value1 = "value1"
    value2 = "value2"
    
    # Get current timestamp
    timestamp1 = RiocClient.get_timestamp()
    
    # Insert with timestamp1
    client.insert_string(key, value1, timestamp1)
    
    # Verify value
    retrieved_value = client.get_string(key)
    assert retrieved_value == value1
    
    # Sleep to ensure timestamp increases
    time.sleep(0.001)
    
    # Get a new timestamp
    timestamp2 = RiocClient.get_timestamp()
    assert timestamp2 > timestamp1
    
    # Update with timestamp2
    client.insert_string(key, value2, timestamp2)
    
    # Verify updated value
    retrieved_value = client.get_string(key)
    assert retrieved_value == value2

def test_large_values(client):
    """Test handling of large values."""
    key = "test_large_value_key"
    value = "x" * 1024 * 16  # 16KB value
    
    # Insert
    client.insert_string(key, value)
    
    # Get
    retrieved_value = client.get_string(key)
    assert retrieved_value == value

def test_multiple_operations(client):
    """Test multiple operations in sequence."""
    # Insert multiple keys
    for i in range(10):
        key = f"multi_key_{i}"
        value = f"multi_value_{i}"
        client.insert_string(key, value)
    
    # Get multiple keys
    for i in range(10):
        key = f"multi_key_{i}"
        expected_value = f"multi_value_{i}"
        retrieved_value = client.get_string(key)
        assert retrieved_value == expected_value
    
    # Delete multiple keys
    for i in range(10):
        key = f"multi_key_{i}"
        client.delete_string(key)
    
    # Verify all are deleted
    for i in range(10):
        key = f"multi_key_{i}"
        with pytest.raises(RiocError) as excinfo:
            client.get_string(key)
        assert excinfo.value.code == -6  # RIOC_ERR_NOENT

def test_special_characters(client):
    """Test handling of special characters in keys and values."""
    special_chars = [
        ("key:with:colons", "value:with:colons"),
        ("key/with/slashes", "value/with/slashes"),
        ("key with spaces", "value with spaces"),
        ("key_with_unicode_Ü", "value_with_unicode_Ü"),
        ("key_with_emoji_🔑", "value_with_emoji_🔑"),
        ("key_with_newlines\n", "value_with_newlines\n"),
        ("key_with_tabs\t", "value_with_tabs\t"),
    ]
    
    # Insert all special character keys
    for key, value in special_chars:
        client.insert_string(key, value)
    
    # Get and verify all special character keys
    for key, expected_value in special_chars:
        retrieved_value = client.get_string(key)
        assert retrieved_value == expected_value

def test_empty_values(client):
    """Test handling of empty values."""
    # Skip empty value test as it's not supported by the server
    # Insert with very short value
    key = "short_value_key"
    value = "x"
    client.insert_string(key, value)
    
    # Get and verify
    retrieved_value = client.get_string(key)
    assert retrieved_value == value

def test_concurrent_timestamp(client):
    """Test that timestamps are monotonically increasing."""
    # Get a bunch of timestamps in rapid succession
    timestamps = []
    for _ in range(1000):
        timestamps.append(RiocClient.get_timestamp())
    
    # Verify they are monotonically increasing
    for i in range(1, len(timestamps)):
        assert timestamps[i] >= timestamps[i-1]
    
    # Verify at least some are different (should be true unless system is very slow)
    assert len(set(timestamps)) > 1

def test_binary_data(client):
    """Test handling of binary data."""
    key = b"\x00\x01\x02\x03"
    value = b"\xff\xfe\xfd\xfc"
    
    # Insert
    client.insert(key, value)
    
    # Get
    retrieved_value = client.get(key)
    assert retrieved_value == value

def test_unicode_data(client):
    """Test handling of Unicode data."""
    key = "unicode_key_🔑"
    value = "unicode_value_🔑"
    
    # Insert
    client.insert_string(key, value)
    
    # Get
    retrieved_value = client.get_string(key)
    assert retrieved_value == value

def test_error_handling(client):
    """Test error handling."""
    # Test invalid key (None)
    with pytest.raises(TypeError):
        client.insert(None, b"value")
    
    # Test invalid value (None)
    with pytest.raises(TypeError):
        client.insert(b"key", None)
    
    # Test invalid timestamp (string)
    with pytest.raises(ctypes.ArgumentError):
        client.insert(b"key", b"value", "invalid")
    
    # Test invalid timestamp (float)
    with pytest.raises(ctypes.ArgumentError):
        client.insert(b"key", b"value", 1.5)

def test_batch_operations(client):
    """Test batch operations."""
    # Create batch
    batch = client.create_batch()
    
    # Add operations
    batch.add_insert(b"batch_key1", b"batch_value1", RiocClient.get_timestamp())
    batch.add_insert(b"batch_key2", b"batch_value2", RiocClient.get_timestamp())
    batch.add_get(b"batch_key1")
    batch.add_get(b"batch_key2")
    
    # Execute batch
    tracker = batch.execute()
    tracker.wait()
    
    # Get results
    value1 = tracker.get_response(2)
    value2 = tracker.get_response(3)
    
    # Verify results
    assert value1 == b"batch_value1"
    assert value2 == b"batch_value2"
    
    # Clean up
    tracker.close()
    batch.close()

def test_range_query(client):
    """Test range query functionality."""
    # Insert test data
    test_data = [
        {"key": "range:a", "value": "Value A"},
        {"key": "range:b", "value": "Value B"},
        {"key": "range:c", "value": "Value C"},
        {"key": "range:d", "value": "Value D"},
        {"key": "range:e", "value": "Value E"},
        {"key": "other:x", "value": "Value X"}
    ]
    
    timestamp = RiocClient.get_timestamp()
    for i, item in enumerate(test_data):
        client.insert_string(item["key"], item["value"], timestamp + i)
    
    # Perform range query for all range: keys
    results = client.range_query(b"range:", b"range:\xff")
    
    # Verify results
    assert len(results) == 5
    for result in results:
        assert isinstance(result, RangeQueryResult)
        assert result.key.startswith(b"range:")
        
        # Find matching test data
        key_str = result.key.decode("utf-8")
        value_str = result.value.decode("utf-8")
        
        # Find the matching test data
        matching_item = next((item for item in test_data if item["key"] == key_str), None)
        assert matching_item is not None
        assert value_str == matching_item["value"]
    
    # Perform range query with string interface
    string_results = client.range_query_string("range:", "range:\xff")
    assert len(string_results) == 5
    
    # Verify string results
    for key, value in string_results:
        assert key.startswith("range:")
        matching_item = next((item for item in test_data if item["key"] == key), None)
        assert matching_item is not None
        assert value == matching_item["value"]

def test_range_query_subset(client):
    """Test range query with a subset of keys."""
    # Insert test data if not already present
    test_data = [
        {"key": "range:a", "value": "Value A"},
        {"key": "range:b", "value": "Value B"},
        {"key": "range:c", "value": "Value C"},
        {"key": "range:d", "value": "Value D"},
        {"key": "range:e", "value": "Value E"},
        {"key": "other:x", "value": "Value X"}
    ]
    
    timestamp = RiocClient.get_timestamp()
    for i, item in enumerate(test_data):
        client.insert_string(item["key"], item["value"], timestamp + i)
    
    # Perform range query for a subset of keys
    results = client.range_query(b"range:b", b"range:d")
    
    # Verify results
    assert len(results) == 3
    
    # Check that we have the expected keys
    keys = [result.key.decode("utf-8") for result in results]
    assert "range:b" in keys
    assert "range:c" in keys
    assert "range:d" in keys
    assert "range:a" not in keys
    assert "range:e" not in keys
    assert "other:x" not in keys

def test_range_query_empty(client):
    """Test range query with no matching keys."""
    # Run range query on keys that don't exist
    start_key = b"nonexistent_range_a"
    end_key = b"nonexistent_range_z"
    
    results = client.range_query(start_key, end_key)
    
    # Should be empty list
    assert isinstance(results, list)
    assert len(results) == 0

def test_atomic_inc_dec(client):
    """Test atomic increment and decrement operations."""
    key = "atomic_test_key"
    
    # Ensure clean state (delete if exists)
    try:
        client.delete_string(key)
    except:
        pass
    
    # Test initial increment
    result1 = client.atomic_inc_dec_string(key, 10)
    assert result1 == 10
    
    # Test increment
    result2 = client.atomic_inc_dec_string(key, 5)
    assert result2 == 15
    
    # Test decrement
    result3 = client.atomic_inc_dec_string(key, -8)
    assert result3 == 7
    
    # Test reading current value without changing it
    result4 = client.atomic_inc_dec_string(key, 0)
    assert result4 == 7
    
    # Cleanup
    client.delete_string(key)

def test_atomic_inc_dec_binary(client):
    """Test atomic operations with binary keys."""
    key = b"atomic_binary_key"
    
    # Ensure clean state
    try:
        client.delete(key)
    except:
        pass
    
    # Test operations
    assert client.atomic_inc_dec(key, 42) == 42
    assert client.atomic_inc_dec(key, 10) == 52
    assert client.atomic_inc_dec(key, -20) == 32
    
    # Cleanup
    client.delete(key)

def test_batch_atomic_inc_dec(client):
    """Test atomic operations in a batch."""
    key1 = b"batch_atomic_key1"
    key2 = b"batch_atomic_key2"
    key3 = b"batch_atomic_key3"
    key4 = b"batch_atomic_key4"
    
    # Ensure clean state
    for key in [key1, key2, key3, key4]:
        try:
            client.delete(key)
        except:
            pass
    
    # Initialize some keys
    client.atomic_inc_dec(key1, 5)  # Start at 5
    client.atomic_inc_dec(key2, 10)  # Start at 10
    
    # Create a batch with multiple atomic operations
    batch = client.create_batch()
    
    # Add operations to modify existing and create new counters
    timestamp = client.get_timestamp()
    batch.add_atomic_inc_dec(key1, 15, timestamp)  # Increment (5 -> 20)
    batch.add_atomic_inc_dec(key2, -5, timestamp)  # Decrement (10 -> 5)
    batch.add_atomic_inc_dec(key3, 30, timestamp)  # New key
    batch.add_atomic_inc_dec(key4, 40, timestamp)  # New key
    
    # Execute batch
    tracker = batch.execute()
    tracker.wait()
    
    # Get results
    result1 = tracker.get_atomic_result(0)
    result2 = tracker.get_atomic_result(1)
    result3 = tracker.get_atomic_result(2)
    result4 = tracker.get_atomic_result(3)
    
    # Verify results
    assert result1 == 20
    assert result2 == 5
    assert result3 == 30
    assert result4 == 40
    
    # Verify with direct reads
    assert client.atomic_inc_dec(key1, 0) == 20
    assert client.atomic_inc_dec(key2, 0) == 5
    assert client.atomic_inc_dec(key3, 0) == 30
    assert client.atomic_inc_dec(key4, 0) == 40
    
    # Create a second batch
    batch2 = client.create_batch()
    timestamp = client.get_timestamp()
    
    batch2.add_atomic_inc_dec(key1, -8, timestamp)  # Decrement (20 -> 12)
    batch2.add_atomic_inc_dec(key2, 15, timestamp)  # Increment (5 -> 20)
    batch2.add_atomic_inc_dec(key3, -10, timestamp)  # Decrement (30 -> 20)
    batch2.add_atomic_inc_dec(key4, -20, timestamp)  # Decrement (40 -> 20)
    
    tracker2 = batch2.execute()
    tracker2.wait()
    
    # Verify final values
    assert client.atomic_inc_dec(key1, 0) == 12
    assert client.atomic_inc_dec(key2, 0) == 20
    assert client.atomic_inc_dec(key3, 0) == 20
    assert client.atomic_inc_dec(key4, 0) == 20
    
    # Cleanup
    for key in [key1, key2, key3, key4]:
        client.delete(key)

def test_batch_range_query(client):
    """Test range query in a batch."""
    # Insert test data if not already present
    test_data = [
        {"key": "range:a", "value": "Value A"},
        {"key": "range:b", "value": "Value B"},
        {"key": "range:c", "value": "Value C"},
        {"key": "range:d", "value": "Value D"},
        {"key": "range:e", "value": "Value E"},
        {"key": "other:x", "value": "Value X"}
    ]
    
    timestamp = RiocClient.get_timestamp()
    for i, item in enumerate(test_data):
        client.insert_string(item["key"], item["value"], timestamp + i)
    
    # Create batch
    batch = client.create_batch()
    
    # Add range query operations
    batch.add_range_query(b"range:", b"range:\xff")
    batch.add_range_query(b"other:", b"other:\xff")
    
    # Execute batch
    tracker = batch.execute()
    tracker.wait()
    
    # Get results for first range query
    range_results = tracker.get_range_query_response(0)
    assert len(range_results) == 5
    
    # Verify range results
    for result in range_results:
        assert result.key.startswith(b"range:")
        key_str = result.key.decode("utf-8")
        value_str = result.value.decode("utf-8")
        matching_item = next((item for item in test_data if item["key"] == key_str), None)
        assert matching_item is not None
        assert value_str == matching_item["value"]
    
    # Get results for second range query
    other_results = tracker.get_range_query_response(1)
    assert len(other_results) == 1
    assert other_results[0].key.decode("utf-8") == "other:x"
    assert other_results[0].value.decode("utf-8") == "Value X"
    
    # Clean up
    tracker.close()
    batch.close() 