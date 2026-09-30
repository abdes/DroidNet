// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Decision-relevant source metadata, excluding observation timestamps.</summary>
/// <param name="Size">File length in bytes.</param>
/// <param name="IsDirectory">Whether the source is a directory.</param>
/// <param name="IsSymlink">Whether the source is a symbolic link.</param>
[StructLayout(LayoutKind.Auto)]
internal readonly record struct NativeSourceShape(ulong Size, bool IsDirectory, bool IsSymlink);

/// <summary>Original file metadata in the native report's UTC time representation.</summary>
/// <param name="Size">File length in bytes.</param>
/// <param name="IsDirectory">Whether the source is a directory.</param>
/// <param name="IsSymlink">Whether the source is a symbolic link.</param>
/// <param name="LastModifiedSeconds">Whole UTC seconds since the Unix epoch.</param>
/// <param name="LastModifiedNanoseconds">Nonnegative fractional nanoseconds.</param>
public sealed record NativeSourceFileMetadata(ulong Size, bool IsDirectory, bool IsSymlink, long LastModifiedSeconds, int LastModifiedNanoseconds)
{
    /// <summary>Gets decision-relevant metadata, excluding observation timestamps.</summary>
    /// <remarks>Timestamps prove in-attempt read coherence, not reproducible input semantics.</remarks>
    internal NativeSourceShape Shape => new(this.Size, this.IsDirectory, this.IsSymlink);
}
