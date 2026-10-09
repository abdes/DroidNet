// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>What a relocation will move and which authored files it will rewrite.</summary>
public sealed class AssetRelocationPlan
{
    internal AssetRelocationPlan(
        AssetRelocationRequest request,
        RelocationMap map,
        IReadOnlyList<RelocationEdit> edits,
        IReadOnlyList<string> referrers,
        IReadOnlyList<string> groupFolders,
        IReadOnlyDictionary<string, string> previousGroups)
    {
        this.Request = request;
        this.Map = map;
        this.Edits = edits;
        this.Referrers = referrers;
        this.GroupFolders = groupFolders;
        this.PreviousGroups = previousGroups;
    }

    /// <summary>Gets the request this plan carries out.</summary>
    public AssetRelocationRequest Request { get; }

    /// <summary>Gets the physical moves, including companion files and sibling group folders.</summary>
    public IReadOnlyList<RelocationFileMove> Moves => this.Map.Moves;

    /// <summary>Gets the virtual paths of the authored files that reference a moved asset and stay in place.</summary>
    public IReadOnlyList<string> Referrers { get; }

    /// <summary>Gets each imported output group folder that moves, as <c>old → new</c> virtual paths.</summary>
    public IReadOnlyList<string> GroupFolders { get; }

    /// <summary>Gets the absolute locations of every file the relocation rewrites, after the moves.</summary>
    public IReadOnlyList<string> RewrittenFiles => [.. this.Edits.Select(static edit => edit.FinalPath)];

    /// <summary>
    /// Gets the follow-up cook that publishes the relocated content. It never faults; its result is null when the
    /// content published, or why it did not.
    /// </summary>
    public Task<string?> ContentPublished { get; internal set; } = Task.FromResult<string?>(null);

    /// <summary>Gets the request that undoes this relocation.</summary>
    public AssetRelocationRequest Reverse => this.Request.Reverse(this.PreviousGroups);

    /// <summary>Gets a value indicating whether the request changes nothing: no move, group or rewrite.</summary>
    internal bool IsEmpty => this.Moves.Count == 0 && this.GroupFolders.Count == 0 && this.Edits.Count == 0;

    /// <summary>Gets the identity mapping.</summary>
    internal RelocationMap Map { get; }

    /// <summary>Gets the rewritten files.</summary>
    internal IReadOnlyList<RelocationEdit> Edits { get; }

    /// <summary>Gets each grouped import's group before the relocation, keyed by the request's source path.</summary>
    internal IReadOnlyDictionary<string, string> PreviousGroups { get; }

    /// <summary>Maps an asset identity or virtual path through the relocation.</summary>
    /// <param name="virtualPath">A virtual path or identity.</param>
    /// <returns>The new virtual path, or null when it does not move.</returns>
    public string? MapVirtualPath(string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(virtualPath);
        var identity = RelocationPaths.ToIdentity(virtualPath);
        return this.Map.MapIdentity(identity) is { } mapped ? mapped + virtualPath[identity.Length..] : null;
    }
}
