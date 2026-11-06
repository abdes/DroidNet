// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>What a relocation will move and which authored files it will rewrite.</summary>
public sealed class AssetRelocationPlan
{
    internal AssetRelocationPlan(
        AssetRelocationRequest request,
        RelocationMap map,
        IReadOnlyList<RelocationEdit> edits,
        IReadOnlyList<AssetPath> referrers)
    {
        this.Request = request;
        this.Map = map;
        this.Edits = edits;
        this.Referrers = referrers;
    }

    /// <summary>Gets the request this plan carries out.</summary>
    public AssetRelocationRequest Request { get; }

    /// <summary>Gets the physical moves, including companion files.</summary>
    public IReadOnlyList<RelocationFileMove> Moves => this.Map.Moves;

    /// <summary>Gets the virtual paths of the authored files that reference a moved asset and stay in place.</summary>
    public IReadOnlyList<AssetPath> Referrers { get; }

    /// <summary>Gets the absolute locations of every file the relocation rewrites, after the moves.</summary>
    public IReadOnlyList<FileSystemPath> RewrittenFiles => [.. this.Edits.Select(static edit => edit.FinalPath)];

    /// <summary>Gets what a move into a shared mount also moves from the project's own mounts, since shared content never
    /// references them.</summary>
    public IReadOnlyList<AssetRelocationMove> ClosureMoves { get; init; } = [];

    /// <summary>Gets the shared mounts the relocation changes, whose other projects it does not update.</summary>
    public IReadOnlyList<string> SharedMounts { get; init; } = [];

    /// <summary>Gets the sources in other mounts that models moved into a shared mount keep referencing.</summary>
    public IReadOnlyList<AssetPath> ReferencedSources { get; init; } = [];

    /// <summary>Gets the source files copied in for the models that moved: each move's source is the copy, its target the
    /// original.</summary>
    public IReadOnlyList<AssetRelocationMove> SourceCopies { get; init; } = [];

    /// <summary>Gets the request that undoes this relocation, what its closure moved included.</summary>
    public AssetRelocationRequest Reverse
        => new AssetRelocationRequest { Moves = [.. this.Request.Moves, .. this.ClosureMoves], ProductRenames = this.Request.ProductRenames }.Reverse() with
        {
            CopiesSourcesIn = this.Request.RemovedSourceCopies.Count != 0,
            RemovedSourceCopies = this.SourceCopies,
        };

    /// <summary>Gets what the user reviews before this relocation.</summary>
    public AssetRelocationReview Review => new(this.ClosureMoves, this.Referrers, this.SharedMounts, this.ReferencedSources);

    /// <summary>Gets a value indicating whether the request changes nothing: no move or rewrite.</summary>
    internal bool IsEmpty => this.Moves.Count == 0 && this.Edits.Count == 0;

    /// <summary>Gets the identity mapping.</summary>
    internal RelocationMap Map { get; }

    /// <summary>Gets the rewritten files.</summary>
    internal IReadOnlyList<RelocationEdit> Edits { get; }

    /// <summary>Maps the virtual path of a file or folder through the relocation.</summary>
    /// <param name="path">The virtual path.</param>
    /// <returns>The new virtual path, or null when it does not move.</returns>
    public AssetPath? MapVirtualPath(AssetPath path) => this.Map.MapPath(path);
}
