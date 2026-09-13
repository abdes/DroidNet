// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Opens ownership markers without throwing for expected sharing conflicts or released readers.</summary>
internal static partial class WindowsCookFile
{
    /// <summary>Attempts an exclusive open while preserving the existing cross-process file-lock protocol.</summary>
    /// <param name="path">The ownership marker path.</param>
    /// <param name="mode">Open for an existing reader, or OpenOrCreate for the publication gate.</param>
    /// <param name="busy">Whether another handle prevented the open.</param>
    /// <returns>The owned stream, or null when busy or when an existing reader has disappeared.</returns>
    internal static FileStream? TryOpenExclusive(string path, FileMode mode, out bool busy)
    {
        var disposition = mode switch
        {
            FileMode.Open => 3u,
            FileMode.OpenOrCreate => 4u,
            _ => throw new ArgumentOutOfRangeException(nameof(mode)),
        };
        var handle = CreateFile(ToNativePath(path), 0xC0000000, 0, 0, disposition, 0x80, 0);
        var error = Marshal.GetLastPInvokeError();
        busy = false;
        try
        {
            if (handle.IsInvalid)
            {
                busy = error is 32 or 33;
                return busy || (mode == FileMode.Open && error is 2 or 3)
                    ? null : throw FileFailure(path, error);
            }

            var stream = new FileStream(handle, FileAccess.ReadWrite);
            handle = null;
            return stream;
        }
        finally
        {
            handle?.Dispose();
        }
    }

    /// <summary>Removes a released marker without throwing if a writer claimed it or already removed it.</summary>
    /// <param name="path">The released marker path.</param>
    internal static void DeleteReleased(string path)
    {
        if (DeleteFile(ToNativePath(path)))
        {
            return;
        }

        var error = Marshal.GetLastPInvokeError();
        if (error is not (2 or 3 or 32 or 33))
        {
            throw FileFailure(path, error);
        }
    }

    private static IOException FileFailure(string path, int error)
        => new($"Could not access cook ownership file '{path}': {new Win32Exception(error).Message}", unchecked((int)0x80070000) | error);

    private static string ToNativePath(string path)
    {
        var absolute = Path.GetFullPath(path);
        return absolute.StartsWith(@"\\?\", StringComparison.Ordinal) ? absolute
            : absolute.StartsWith(@"\\", StringComparison.Ordinal) ? @"\\?\UNC\" + absolute[2..]
            : @"\\?\" + absolute;
    }

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [LibraryImport("kernel32.dll", EntryPoint = "CreateFileW", StringMarshalling = StringMarshalling.Utf16, SetLastError = true)]
    private static partial SafeFileHandle CreateFile(string path, uint access, uint sharing, nint security, uint disposition, uint attributes, nint template);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [LibraryImport("kernel32.dll", EntryPoint = "DeleteFileW", StringMarshalling = StringMarshalling.Utf16, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool DeleteFile(string path);
}
