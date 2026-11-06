// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>A rename or move of authored assets, folders, or input actions and contexts.</summary>
public sealed record AssetRelocationRequest
{
    /// <summary>Gets the file and folder moves, by virtual path such as <c>/Content/Materials/Red.omat.json</c>.</summary>
    public IReadOnlyList<AssetRelocationMove> Moves { get; init; } = [];

    /// <summary>Gets the actions and mapping contexts to rename inside their input sources.</summary>
    public IReadOnlyList<InputProductRename> ProductRenames { get; init; } = [];

    /// <summary>Gets the reference fields to rewrite so they name another asset.</summary>
    public IReadOnlyList<ReferenceRetarget> Retargets { get; init; } = [];

    /// <summary>Gets a value indicating whether a model moved into a shared mount gets its own copy of a source it references
    /// in another mount, instead of keeping the reference.</summary>
    public bool CopiesSourcesIn { get; init; }

    /// <summary>Gets the copied source files an undo removes: each move's source is the copy, its target the original that
    /// the model references again.</summary>
    public IReadOnlyList<AssetRelocationMove> RemovedSourceCopies { get; init; } = [];

    /// <summary>Gets the request that undoes this one.</summary>
    /// <returns>The reverse request.</returns>
    public AssetRelocationRequest Reverse() => new()
    {
        Moves = [.. this.Moves.Reverse().Select(static move => new AssetRelocationMove(move.Target, move.Source))],
        ProductRenames = [.. this.ProductRenames.Reverse().Select(static rename => rename.Reverse())],
        Retargets = [.. this.Retargets.Reverse().Select(static retarget => retarget.Reverse())],
    };
}
