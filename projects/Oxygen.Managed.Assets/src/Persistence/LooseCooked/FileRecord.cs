// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

/// <summary>One cooked member file; only texture descriptors have a virtual path.</summary>
public sealed record FileRecord(
    FileKind Kind,
    string RelativePath,
    ulong Size,
    System.ReadOnlyMemory<byte> Sha256,
    string? VirtualPath = null);
