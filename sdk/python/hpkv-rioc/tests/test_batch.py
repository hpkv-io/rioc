"""
Tests for RIOC batch operations.
"""

import os
import pytest
from datetime import datetime, UTC
import time

from hpkv_rioc import RiocClient, RiocConfig, RiocError

# Test configuration
TEST_HOST = os.getenv("RIOC_TEST_HOST", "localhost")
TEST_PORT = int(os.getenv("RIOC_TEST_PORT", "8000"))
TEST_TIMEOUT = int(os.getenv("RIOC_TEST_TIMEOUT", "5000"))
TEST_USE_TLS = os.getenv("RIOC_TEST_USE_TLS", "true").lower() == "true"

@pytest.fixture
def client(tls_config):
    """Create a RIOC client for testing."""
    config = RiocConfig(
        host=TEST_HOST,
        port=TEST_PORT,
        timeout_ms=TEST_TIMEOUT,
        tls=tls_config
    )
    client = RiocClient(config)
    yield client

def test_batch_context_manager(client):
    """Test batch operations using context manager."""
    keys = [f"test_batch_key_{i}".encode() for i in range(3)]
    values = [f"test_batch_value_{i}".encode() for i in range(3)]
    
    # Insert multiple key-value pairs in a batch
    with client.batch() as batch:
        for key, value in zip(keys, values):
            batch.add_insert(key, value, client.get_timestamp())
    
    # Verify all values were inserted
    for key, expected in zip(keys, values):
        retrieved = client.get(key)
        assert retrieved == expected
    
    # Cleanup
    for key in keys:
        client.delete(key)

def test_batch_manual(client):
    """Test batch operations using manual execution."""
    keys = [f"test_batch_key_{i}".encode() for i in range(3)]
    values = [f"test_batch_value_{i}".encode() for i in range(3)]
    
    # Create and execute batch
    batch = client.create_batch()
    for key, value in zip(keys, values):
        batch.add_insert(key, value, client.get_timestamp())
    tracker = batch.execute()
    tracker.wait()
    
    # Verify all values were inserted
    for key, expected in zip(keys, values):
        retrieved = client.get(key)
        assert retrieved == expected
    
    # Cleanup
    for key in keys:
        client.delete(key)

def test_batch_mixed_operations(client):
    """Test batch with mixed operations (insert, get, delete)."""
    key1 = b"test_batch_mixed_1"
    key2 = b"test_batch_mixed_2"
    value1 = b"value1"
    value2 = b"value2"
    
    # First insert the keys
    client.insert(key1, value1)
    client.insert(key2, value2)
    
    # Create batch with mixed operations
    batch = client.create_batch()
    batch.add_get(key1)  # Get first key
    batch.add_delete(key2, client.get_timestamp())  # Delete second key
    batch.add_insert(key1, b"new_value", client.get_timestamp())  # Update first key
    
    # Execute batch
    tracker = batch.execute()
    tracker.wait()
    
    # Check results
    assert tracker.get_response(0) == value1  # First get should return original value
    assert client.get(key1) == b"new_value"  # Key1 should have new value
    
    # Key2 should be deleted
    with pytest.raises(RiocError) as exc_info:
        client.get(key2)
    assert exc_info.value.code == -6  # RIOC_ERR_NOENT
    
    # Cleanup
    client.delete(key1)

def test_batch_get_only(client):
    """Test batch with only get operations."""
    # First insert some test data
    test_data = {
        b"test_batch_get_1": b"value1",
        b"test_batch_get_2": b"value2",
        b"test_batch_get_3": b"value3"
    }
    for key, value in test_data.items():
        client.insert(key, value)
    
    # Create batch of gets
    batch = client.create_batch()
    for key in test_data.keys():
        batch.add_get(key)
    
    # Execute batch
    tracker = batch.execute()
    tracker.wait()
    
    # Verify results
    for i, (key, expected) in enumerate(test_data.items()):
        assert tracker.get_response(i) == expected
    
    # Cleanup
    for key in test_data.keys():
        client.delete(key)

def test_batch_delete_only(client):
    """Test batch with only delete operations."""
    # First insert some test data
    keys = [b"test_batch_del_1", b"test_batch_del_2", b"test_batch_del_3"]
    value = b"test_value"
    for key in keys:
        client.insert(key, value)
    
    # Create batch of deletes
    timestamp = client.get_timestamp()
    with client.batch() as batch:
        for key in keys:
            batch.add_delete(key, timestamp)
    
    # Verify all keys are deleted
    for key in keys:
        with pytest.raises(RiocError) as exc_info:
            client.get(key)
        assert exc_info.value.code == -6  # RIOC_ERR_NOENT

def test_batch_large(client):
    """Test batch with large number of operations."""
    num_ops = 100
    keys = [f"test_batch_large_{i}".encode() for i in range(num_ops)]
    values = [f"test_batch_large_value_{i}".encode() for i in range(num_ops)]
    
    # Insert in batch
    with client.batch() as batch:
        for key, value in zip(keys, values):
            batch.add_insert(key, value, client.get_timestamp())
    
    # Get in batch
    batch = client.create_batch()
    for key in keys:
        batch.add_get(key)
    tracker = batch.execute()
    tracker.wait()
    
    # Verify results
    for i, expected in enumerate(values):
        assert tracker.get_response(i) == expected
    
    # Cleanup in batch
    with client.batch() as batch:
        for key in keys:
            batch.add_delete(key, client.get_timestamp())

def test_batch_error_handling(client):
    """Test error handling in batch operations."""
    # Try to get non-existent keys in batch
    batch = client.create_batch()
    batch.add_get(b"nonexistent_key_1")
    batch.add_get(b"nonexistent_key_2")
    
    # Execute batch
    tracker = batch.execute()
    tracker.wait()
    
    # Both gets should raise NOENT error
    for i in range(2):
        with pytest.raises(RiocError) as exc_info:
            tracker.get_response(i)
        assert exc_info.value.code == -6  # RIOC_ERR_NOENT

def test_batch_with_empty_values(client):
    """Test batch operations with empty values."""
    keys = [b"test_batch_empty_1", b"test_batch_empty_2"]
    min_value = b" "  # Use single space instead of empty value

    # First insert non-empty values
    with client.batch() as batch:
        for key in keys:
            batch.add_insert(key, b"test", client.get_timestamp())

    # Wait a bit to ensure timestamp increases
    time.sleep(0.001)

    # Then insert minimal values with newer timestamp
    ts = client.get_timestamp()
    with client.batch() as batch:
        for key in keys:
            batch.add_insert(key, min_value, ts)

    # Get in batch
    batch = client.create_batch()
    for key in keys:
        batch.add_get(key)
    tracker = batch.execute()
    tracker.wait()

    # Verify results
    for i in range(len(keys)):
        result = client.get(keys[i])  # Get directly to verify
        assert result == min_value
        assert tracker.get_response(i) == min_value

    # Delete in batch
    with client.batch() as batch:
        for key in keys:
            batch.add_delete(key, client.get_timestamp()) 