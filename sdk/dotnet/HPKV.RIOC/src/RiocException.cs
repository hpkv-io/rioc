namespace HPKV.RIOC;

/// <summary>
/// Base exception class for RIOC client errors.
/// </summary>
public class RiocException : Exception
{
    /// <summary>
    /// The error code from the native RIOC library.
    /// </summary>
    public int ErrorCode { get; }

    internal RiocException(int errorCode, string message) 
        : base(message)
    {
        ErrorCode = errorCode;
    }

    internal RiocException(int errorCode, string message, Exception? innerException) 
        : base(message, innerException)
    {
        ErrorCode = errorCode;
    }
}

/// <summary>
/// Exception thrown when a key is not found.
/// </summary>
public class RiocKeyNotFoundException : RiocException
{
    internal RiocKeyNotFoundException(int errorCode) 
        : base(errorCode, "The specified key was not found.")
    {
    }
}

/// <summary>
/// Exception thrown when a parameter is invalid.
/// </summary>
public class RiocInvalidParameterException : RiocException
{
    internal RiocInvalidParameterException(int errorCode) 
        : base(errorCode, "One or more parameters are invalid.")
    {
    }
}

/// <summary>
/// Exception thrown when there is insufficient memory.
/// </summary>
public class RiocOutOfMemoryException : RiocException
{
    internal RiocOutOfMemoryException(int errorCode) 
        : base(errorCode, "Insufficient memory to complete the operation.")
    {
    }
}

/// <summary>
/// Exception thrown when there is an I/O error.
/// </summary>
public class RiocIOException : RiocException
{
    internal RiocIOException(int errorCode) 
        : base(errorCode, "An I/O error occurred.")
    {
    }
}

/// <summary>
/// Exception thrown when there is a protocol error.
/// </summary>
public class RiocProtocolException : RiocException
{
    internal RiocProtocolException(int errorCode) 
        : base(errorCode, "A protocol error occurred.")
    {
    }
}

/// <summary>
/// Exception thrown when there is a device error.
/// </summary>
public class RiocDeviceException : RiocException
{
    internal RiocDeviceException(int errorCode) 
        : base(errorCode, "A device error occurred.")
    {
    }
}

/// <summary>
/// Exception thrown when a resource is busy.
/// </summary>
public class RiocBusyException : RiocException
{
    internal RiocBusyException(int errorCode) 
        : base(errorCode, "The resource is busy.")
    {
    }
}

/// <summary>
/// Helper class for creating RIOC exceptions based on error codes.
/// </summary>
internal static class RiocExceptionFactory
{
    public static RiocException Create(int errorCode)
    {
        return errorCode switch
        {
            -1 => new RiocInvalidParameterException(errorCode),
            -2 => new RiocOutOfMemoryException(errorCode),
            -3 => new RiocIOException(errorCode),
            -4 => new RiocProtocolException(errorCode),
            -5 => new RiocDeviceException(errorCode),
            -6 => new RiocKeyNotFoundException(errorCode),
            -7 => new RiocBusyException(errorCode),
            _ => new RiocException(errorCode, $"An unknown error occurred (code: {errorCode}).")
        };
    }
} 