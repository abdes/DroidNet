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
