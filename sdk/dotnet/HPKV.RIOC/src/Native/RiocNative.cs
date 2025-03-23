using System.Runtime.InteropServices;
using System.Reflection;

namespace HPKV.RIOC.Native;

internal static unsafe class RiocNative
{
    private const string WindowsLibName = "rioc.dll";
    private const string LinuxLibName = "librioc.so";
    private const string OsxLibName = "librioc.dylib";

    private static string LibraryName
    {
        get
        {
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                return WindowsLibName;
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                return LinuxLibName;
            if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                return OsxLibName;
            throw new PlatformNotSupportedException("Unsupported platform");
        }
    }

    static RiocNative()
    {
        NativeLibrary.SetDllImportResolver(typeof(RiocNative).Assembly, ResolveDllImport);
    }

    private static IntPtr ResolveDllImport(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName != WindowsLibName && libraryName != LinuxLibName && libraryName != OsxLibName)
            return IntPtr.Zero;

        string libPath = GetNativeLibraryPath();
        if (!NativeLibrary.TryLoad(libPath, assembly, searchPath, out IntPtr handle))
        {
            throw new DllNotFoundException($"Failed to load native library: {libPath}");
        }
        return handle;
    }

    private static string GetNativeLibraryPath()
    {
        string architecture = RuntimeInformation.ProcessArchitecture switch
        {
            Architecture.X64 => "x64",
            Architecture.Arm64 => "arm64",
            _ => throw new PlatformNotSupportedException($"Unsupported architecture: {RuntimeInformation.ProcessArchitecture}")
        };

        string platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "win" :
                         RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ? "linux" :
                         RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "osx" :
                         throw new PlatformNotSupportedException("Unsupported platform");

        string libraryName = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? WindowsLibName :
                            RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ? LinuxLibName :
                            RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? OsxLibName :
                            throw new PlatformNotSupportedException("Unsupported platform");

        string runtimesPath = Path.Combine(
            Path.GetDirectoryName(typeof(RiocNative).Assembly.Location) ?? "",
            "runtimes",
            $"{platform}-{architecture}",
            "native",
            libraryName);

        return runtimesPath;
    }

    // Core client functions
    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_client_connect_with_config(NativeClientConfig* config, void** client);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void rioc_client_disconnect_with_config(void* client);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_get(void* client, byte* key, nuint key_len, byte** value, nuint* value_len);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_insert(void* client, byte* key, nuint key_len, byte* value, nuint value_len, ulong timestamp);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_delete(void* client, byte* key, nuint key_len, ulong timestamp);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_atomic_inc_dec(void* client, byte* key, nuint key_len, long increment, ulong timestamp, long* result);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_range_query(void* client, byte* start_key, nuint start_key_len, 
                                             byte* end_key, nuint end_key_len, 
                                             NativeRangeResult** results, nuint* result_count);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void rioc_free_range_results(NativeRangeResult* results, nuint count);

    // Batch operations
    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void* rioc_batch_create(void* client);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_batch_add_get(void* batch, byte* key, nuint key_len);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_batch_add_insert(void* batch, byte* key, nuint key_len, byte* value, nuint value_len, ulong timestamp);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_batch_add_delete(void* batch, byte* key, nuint key_len, ulong timestamp);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_batch_add_range_query(void* batch, byte* start_key, nuint start_key_len, 
                                                      byte* end_key, nuint end_key_len);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_batch_add_atomic_inc_dec(void* batch, byte* key, nuint key_len, long increment, ulong timestamp);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void* rioc_batch_execute_async(void* batch);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_batch_wait(void* tracker, int timeout_ms);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_batch_get_response_async(void* tracker, nuint index, byte** value, nuint* value_len);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void rioc_batch_tracker_free(void* tracker);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void rioc_batch_free(void* batch);

    // Platform functions
    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong rioc_get_timestamp_ns();

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void rioc_sleep_us(uint usec);

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rioc_platform_init();

    [DllImport(WindowsLibName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void rioc_platform_cleanup();
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeClientConfig
{
    public byte* host;
    public uint port;
    public uint timeout_ms;
    public NativeTlsConfig* tls;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeTlsConfig
{
    public byte* cert_path;
    public byte* key_path;
    public byte* ca_path;
    public byte* verify_hostname;
    [MarshalAs(UnmanagedType.I1)]
    public bool verify_peer;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeRangeResult
{
    public byte* key;
    public nuint key_len;
    public byte* value;
    public nuint value_len;
} 