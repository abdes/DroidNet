// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

/// <summary>One cooked member file; only texture descriptors have a virtual path, and the source that produced them.</summary>
/// <param name="Kind">The file's role.</param>
/// <param name="RelativePath">The file's path below the cooked root.</param>
/// <param name="Size">The file's size in bytes.</param>
/// <param name="Sha256">The file's SHA-256 digest.</param>
/// <param name="VirtualPath">The virtual path of a texture descriptor.</param>
/// <param name="SourcePath">The virtual path of the source that produced a texture descriptor.</param>
public sealed record FileRecord(
    FileKind Kind,
    string RelativePath,
    ulong Size,
    System.ReadOnlyMemory<byte> Sha256,
    string? VirtualPath = null,
    string? SourcePath = null);
