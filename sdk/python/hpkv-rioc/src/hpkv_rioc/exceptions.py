"""
RIOC SDK exceptions
"""

class RiocError(Exception):
    """Base exception for all RIOC errors."""
    def __init__(self, code: int, message: str):
        self.code = code
        super().__init__(f"{message} (code: {code})")

class RiocTimeoutError(RiocError):
    """Raised when an operation times out."""
    def __init__(self, message: str = "Operation timed out"):
        super().__init__(-1, message)

class RiocConnectionError(RiocError):
    """Raised when there is a connection error."""
    def __init__(self, message: str = "Connection error"):
        super().__init__(-3, message)

def create_rioc_error(code: int) -> RiocError:
    """Create a RiocError from an error code."""
    error_messages = {
        -1: "Invalid parameters",
        -2: "Out of memory",
        -3: "I/O error",
        -4: "Protocol error",
        -5: "Device error",
        -6: "Not found",
        -7: "Resource busy",
    }
    message = error_messages.get(code, f"Unknown error ({code})")
    return RiocError(code, message) 