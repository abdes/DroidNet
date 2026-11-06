// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

public sealed record Document(
    ushort ContentVersion,
    IndexFeatures Flags,
    Guid SourceGuid,
    IReadOnlyList<AssetEntry> Assets,
    IReadOnlyList<FileRecord> Files)
{
    /// <summary>Gets the commit this index is: 1 for a root's first, one more for each publication after it.</summary>
    public ulong Commit { get; init; } = 1;

    /// <summary>Gets the files an earlier commit listed and this one does not.</summary>
    public IReadOnlyList<RetiredFile> Retired { get; init; } = [];

    /// <summary>Gets what the assets require of other mounts, in asset order.</summary>
    public IReadOnlyList<AssetRequirement> Requirements { get; init; } = [];
}
