// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>A rename or move of authored assets, folders, imported output groups or input actions and contexts.</summary>
public sealed record AssetRelocationRequest
{
    /// <summary>Gets the file and folder moves, by virtual path such as <c>/Content/Materials/Red.omat.json</c>.</summary>
    public IReadOnlyList<AssetRelocationMove> Moves { get; init; } = [];

    /// <summary>Gets the imported output groups to rename or move.</summary>
    public IReadOnlyList<ImportGroupMove> GroupMoves { get; init; } = [];

    /// <summary>Gets the actions and mapping contexts to rename inside their input sources.</summary>
    public IReadOnlyList<InputProductRename> ProductRenames { get; init; } = [];

    /// <summary>Gets the request that undoes this one.</summary>
    /// <param name="previousGroups">The groups each imported model had before this request ran.</param>
    /// <returns>The reverse request.</returns>
    public AssetRelocationRequest Reverse(IReadOnlyDictionary<AssetPath, string> previousGroups)
    {
        ArgumentNullException.ThrowIfNull(previousGroups);
        return new()
        {
            Moves = [.. this.Moves.Reverse().Select(static move => new AssetRelocationMove(move.Target, move.Source))],
            GroupMoves = [.. this.GroupMoves.Select(move => new ImportGroupMove(move.Source, previousGroups[move.Source]))],
            ProductRenames = [.. this.ProductRenames.Reverse().Select(static rename => rename.Reverse())],
        };
    }
}
