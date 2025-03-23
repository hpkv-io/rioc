"""
RIOC native library interface
"""

import ctypes
import os
import platform
import sys
from ctypes import (
    c_int, c_uint, c_uint64, c_size_t, c_char_p, c_void_p, c_bool,
    POINTER, Structure, CDLL, c_char
)
from pathlib import Path
from typing import Optional

# Platform-specific library names
_WINDOWS_LIB = "rioc.dll"
_LINUX_LIB = "librioc.so"
_OSX_LIB = "librioc.dylib"

def _get_lib_name() -> str:
    """Get the platform-specific library name."""
    if sys.platform == "win32":
        return _WINDOWS_LIB
    elif sys.platform == "linux":
        return _LINUX_LIB
    elif sys.platform == "darwin":
        return _OSX_LIB
    raise OSError(f"Unsupported platform: {sys.platform}")

def _get_lib_path() -> Path:
    """Get the path to the native library."""
    # Get the package directory
    package_dir = Path(__file__).parent

    # Determine platform and architecture
    if sys.platform == "win32":
        platform_name = "win"
    elif sys.platform == "linux":
        platform_name = "linux"
    elif sys.platform == "darwin":
        platform_name = "osx"
    else:
        raise OSError(f"Unsupported platform: {sys.platform}")

    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        arch = "x64"
    elif machine in ("arm64", "aarch64"):
        arch = "arm64"
    else:
        raise OSError(f"Unsupported architecture: {machine}")

    # Construct library path
    lib_path = package_dir / "runtimes" / f"{platform_name}-{arch}" / "native" / _get_lib_name()
    if not lib_path.exists():
        raise OSError(f"Native library not found: {lib_path}")

    return lib_path

class NativeTlsConfig(Structure):
    """Native TLS configuration structure."""
    _fields_ = [
        ("cert_path", c_char_p),
        ("key_path", c_char_p),
        ("ca_path", c_char_p),
        ("verify_hostname", c_char_p),
        ("verify_peer", c_bool),
    ]

class NativeClientConfig(Structure):
    """Native client configuration structure."""
    _fields_ = [
        ("host", c_char_p),
        ("port", c_uint),
        ("timeout_ms", c_uint),
        ("tls", POINTER(NativeTlsConfig)),
    ]

# Define the range result structure
class NativeRangeResult(Structure):
    """Native range query result structure."""
    _fields_ = [
        ("key", c_char_p),
        ("key_len", c_size_t),
        ("value", c_char_p),
        ("value_len", c_size_t),
    ]

class RiocNative:
    """RIOC native library interface."""
    _instance: Optional["RiocNative"] = None
    _lib: Optional[CDLL] = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance

    def __init__(self):
        if self._lib is None:
            self._init_library()

    def _init_library(self):
        """Initialize the native library."""
        lib_path = _get_lib_path()
        
        # Load the library
        if sys.platform == "win32":
            self._lib = CDLL(str(lib_path))
        else:
            # Add library directory to search path on Unix
            os.environ["LD_LIBRARY_PATH"] = f"{lib_path.parent}:{os.environ.get('LD_LIBRARY_PATH', '')}"
            self._lib = CDLL(str(lib_path))

        # Core client functions
        self._lib.rioc_client_connect_with_config.argtypes = [POINTER(NativeClientConfig), POINTER(c_void_p)]
        self._lib.rioc_client_connect_with_config.restype = c_int

        self._lib.rioc_client_disconnect_with_config.argtypes = [c_void_p]
        self._lib.rioc_client_disconnect_with_config.restype = None

        self._lib.rioc_get.argtypes = [c_void_p, c_char_p, c_size_t, POINTER(POINTER(c_char)), POINTER(c_size_t)]
        self._lib.rioc_get.restype = c_int

        self._lib.rioc_insert.argtypes = [c_void_p, c_char_p, c_size_t, c_char_p, c_size_t, c_uint64]
        self._lib.rioc_insert.restype = c_int

        self._lib.rioc_delete.argtypes = [c_void_p, c_char_p, c_size_t, c_uint64]
        self._lib.rioc_delete.restype = c_int

        # Range query function - Fix the pointer types
        self._lib.rioc_range_query.argtypes = [c_void_p, c_char_p, c_size_t, c_char_p, c_size_t, 
                                              POINTER(POINTER(NativeRangeResult)), POINTER(c_size_t)]
        self._lib.rioc_range_query.restype = c_int

        # Free range results function
        self._lib.rioc_free_range_results.argtypes = [POINTER(NativeRangeResult), c_size_t]
        self._lib.rioc_free_range_results.restype = None

        # Atomic operations
        self._lib.rioc_atomic_inc_dec.argtypes = [c_void_p, c_char_p, c_size_t, ctypes.c_int64, c_uint64, POINTER(ctypes.c_int64)]
        self._lib.rioc_atomic_inc_dec.restype = c_int

        # Batch operations
        self._lib.rioc_batch_create.argtypes = [c_void_p]
        self._lib.rioc_batch_create.restype = c_void_p

        self._lib.rioc_batch_add_get.argtypes = [c_void_p, c_char_p, c_size_t]
        self._lib.rioc_batch_add_get.restype = c_int

        self._lib.rioc_batch_add_insert.argtypes = [c_void_p, c_char_p, c_size_t, c_char_p, c_size_t, c_uint64]
        self._lib.rioc_batch_add_insert.restype = c_int

        self._lib.rioc_batch_add_delete.argtypes = [c_void_p, c_char_p, c_size_t, c_uint64]
        self._lib.rioc_batch_add_delete.restype = c_int

        # Atomic batch operations
        self._lib.rioc_batch_add_atomic_inc_dec.argtypes = [c_void_p, c_char_p, c_size_t, ctypes.c_int64, c_uint64]
        self._lib.rioc_batch_add_atomic_inc_dec.restype = c_int

        # Range query batch function
        self._lib.rioc_batch_add_range_query.argtypes = [c_void_p, c_char_p, c_size_t, c_char_p, c_size_t]
        self._lib.rioc_batch_add_range_query.restype = c_int

        self._lib.rioc_batch_execute_async.argtypes = [c_void_p]
        self._lib.rioc_batch_execute_async.restype = c_void_p

        self._lib.rioc_batch_wait.argtypes = [c_void_p, c_int]
        self._lib.rioc_batch_wait.restype = c_int

        self._lib.rioc_batch_get_response_async.argtypes = [c_void_p, c_size_t, POINTER(POINTER(c_char)), POINTER(c_size_t)]
        self._lib.rioc_batch_get_response_async.restype = c_int

        self._lib.rioc_batch_tracker_free.argtypes = [c_void_p]
        self._lib.rioc_batch_tracker_free.restype = None

        self._lib.rioc_batch_free.argtypes = [c_void_p]
        self._lib.rioc_batch_free.restype = None

        # Platform functions
        self._lib.rioc_get_timestamp_ns.argtypes = []
        self._lib.rioc_get_timestamp_ns.restype = c_uint64

        self._lib.rioc_platform_init.argtypes = []
        self._lib.rioc_platform_init.restype = c_int

        self._lib.rioc_platform_cleanup.argtypes = []
        self._lib.rioc_platform_cleanup.restype = None

    @property
    def lib(self) -> CDLL:
        """Get the native library instance."""
        if self._lib is None:
            raise RuntimeError("Native library not initialized")
        return self._lib

# Global instance
rioc_native = RiocNative() 