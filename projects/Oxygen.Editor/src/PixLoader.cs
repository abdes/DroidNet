// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Oxygen.Editor;

/// <summary>
/// Helper for loading the Microsoft PIX GPU capture DLL when running in a debug
/// environment. This class contains a single utility to attempt to load the
/// WinPixGpuCapturer native library so GPU captures can be performed while
/// debugging.
/// </summary>
internal static partial class PixLoader
{
    /// <summary>
    /// Attempts to load the Microsoft PIX native DLL used for GPU captures.
    /// <para>
    /// In DEBUG builds this method will call <see cref="LoadLibrary(string)"/>
    /// with a hard-coded path to the PIX installation and write the result to
    /// the <see cref="Debug"/> output. In non-DEBUG builds this method is a
    /// no-op.</para>
    /// </summary>
    public static void LoadPixDll()
    {
#if DEBUG
        var dllName = "C:\\Program Files\\Microsoft PIX\\2509.25\\WinPixGpuCapturer.dll";
        var handle = LoadLibrary(dllName);
        if (handle == IntPtr.Zero)
        {
            var errorCode = Marshal.GetLastWin32Error();
            Debug.WriteLine($"Failed to load {dllName}. Error code: {errorCode}");
        }
        else
        {
            Debug.WriteLine($"{dllName} loaded successfully.");
        }
#endif
    }

    [LibraryImport("kernel32.dll", SetLastError = true, EntryPoint = "LoadLibraryW", StringMarshalling = StringMarshalling.Utf16)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial IntPtr LoadLibrary(string dllToLoad);
}
