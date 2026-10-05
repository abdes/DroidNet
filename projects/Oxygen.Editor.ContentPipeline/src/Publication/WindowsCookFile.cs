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
    /// <summary>Retains a sealed generation while permitting other managed and native readers.</summary>
    /// <param name="path">The existing generation marker.</param>
    /// <returns>The generation lifetime handle.</returns>
    internal static FileStream OpenGenerationReader(string path)
    {
        var reader = TryOpenGeneration(path, reclaim: false, out var busy);
        if (reader is not null)
        {
            return reader;
        }

        throw busy ? new CookOutputBusyException("The cooked generation is being reclaimed. Refresh the publication.")
            : new InvalidDataException("The selected cooked generation is missing its lifetime marker.");
    }

    /// <summary>Claims an unused generation through index removal and deletion.</summary>
    /// <param name="path">The existing generation marker.</param>
    /// <returns>Exclusive ownership, or null when the marker is absent or has a reader.</returns>
    internal static FileStream? TryClaimGeneration(string path)
        => TryOpenGeneration(path, reclaim: true, out _);

    internal static FileStream? TryClaimPublication(string path)
        => TryOpen(path, FileMode.Open, FileAccess.ReadWrite, FileShare.Delete, deleteOnClose: false, out _);

    /// <summary>Distinguishes an absent generation marker from live reclamation.</summary>
    internal static FileStream? TryOpenGenerationReader(string path, out bool busy)
        => TryOpenGeneration(path, reclaim: false, out busy);

    /// <summary>Attempts an exclusive open while preserving the existing cross-process file-lock protocol.</summary>
    /// <param name="path">The ownership marker path.</param>
    /// <param name="mode">Open for an existing reader, or OpenOrCreate for the publication gate.</param>
    /// <param name="busy">Whether another handle prevented the open.</param>
    /// <returns>The owned stream, or null when busy or when an existing reader has disappeared.</returns>
    internal static FileStream? TryOpenExclusive(string path, FileMode mode, out bool busy)
        => TryOpen(path, mode, FileAccess.ReadWrite, FileShare.None, deleteOnClose: false, out busy);

    internal static FileStream? TryOpenOperation(string path, out bool busy)
        => TryOpen(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None, deleteOnClose: true, out busy);

    private static IOException FileFailure(string path, int error)
        => new($"Could not access cook ownership file '{path}': {new Win32Exception(error).Message}", unchecked((int)0x80070000) | error);

    private static FileStream? TryOpenGeneration(string path, bool reclaim, out bool busy)
        => TryOpen(path, FileMode.Open, reclaim ? FileAccess.ReadWrite : FileAccess.Read,
            reclaim ? FileShare.Delete : FileShare.Read | FileShare.Delete, deleteOnClose: false, out busy);

    private static FileStream? TryOpen(string path, FileMode mode, FileAccess access, FileShare sharing, bool deleteOnClose, out bool busy)
    {
        var disposition = mode switch
        {
            FileMode.Open => 3u,
            FileMode.OpenOrCreate => 4u,
            _ => throw new ArgumentOutOfRangeException(nameof(mode)),
        };
        var desired = access == FileAccess.Read ? 0x80000000u : 0xC0000000u;
        if (deleteOnClose)
        {
            desired |= 0x00010000u;
        }

        var attributes = 0x80u | (deleteOnClose ? 0x04000000u : 0u);
        var handle = CreateFile(ToNativePath(path), desired, (uint)sharing, 0, disposition, attributes, 0);
        var error = Marshal.GetLastPInvokeError();
        busy = false;
        try
        {
            if (handle.IsInvalid)
            {
                busy = error is 32 or 33;
                return busy || (mode == FileMode.Open && error is 2 or 3) ? null : throw FileFailure(path, error);
            }

            var stream = new FileStream(handle, access);
            handle = null;
            return stream;
        }
        finally
        {
            handle?.Dispose();
        }
    }

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
}
