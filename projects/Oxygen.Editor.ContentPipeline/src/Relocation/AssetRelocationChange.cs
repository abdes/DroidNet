// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// A committed relocation, as open documents see it: which files moved, which were rewritten and with what
/// version, and how any asset reference maps to its new identity.
/// </summary>
public sealed class AssetRelocationChange
{
    private readonly RelocationMap map;

    internal AssetRelocationChange(RelocationMap map, IReadOnlyList<RelocatedFile> rewrittenFiles, Task<string?> contentPublished)
    {
        this.map = map;
        this.RewrittenFiles = rewrittenFiles;
        this.ContentPublished = contentPublished;
    }

    /// <summary>Gets the physical moves, including companion files and folders.</summary>
    public IReadOnlyList<RelocationFileMove> Moves => this.map.Moves;

    /// <summary>Gets each rewritten file with its locations and the version the transaction wrote.</summary>
    public IReadOnlyList<RelocatedFile> RewrittenFiles { get; }

    /// <summary>
    /// Gets the follow-up cook that publishes the moved assets under their new identities. The runtime resolves the
    /// new paths only after it succeeds. It never faults; its result is null when the content published, or why it
    /// did not.
    /// </summary>
    public Task<string?> ContentPublished { get; }

    /// <summary>Maps an asset reference to its new identity, keeping its form.</summary>
    /// <param name="reference">A full <c>asset:///</c> URI or a canonical virtual path.</param>
    /// <returns>The new reference, or null when the referenced asset did not move.</returns>
    public string? MapReference(string reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        return AuthoredReferenceFormat.MapReference(reference, this.map.MapIdentity);
    }

    /// <summary>Maps an asset URI to its new identity.</summary>
    /// <param name="reference">The asset URI.</param>
    /// <returns>The new URI, or null when the referenced asset did not move.</returns>
    public Uri? MapReference(Uri reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        return this.MapReference(reference.ToString()) is { } mapped ? new Uri(mapped) : null;
    }

    /// <summary>Maps a physical location through the moves.</summary>
    /// <param name="path">An absolute path.</param>
    /// <returns>The new location, or null when the path did not move.</returns>
    public string? MapPath(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return this.map.IsMoved(path) ? this.map.MapPath(path) : null;
    }

    /// <summary>Finds the rewrite of a file, by its location before or after the moves.</summary>
    /// <param name="path">An absolute path.</param>
    /// <returns>The rewrite, or null when the relocation did not rewrite the file.</returns>
    public RelocatedFile? FindRewrite(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        var full = Path.GetFullPath(path);
        return this.RewrittenFiles.FirstOrDefault(file => string.Equals(Path.GetFullPath(file.OriginalPath), full, StringComparison.OrdinalIgnoreCase)
            || string.Equals(Path.GetFullPath(file.Path), full, StringComparison.OrdinalIgnoreCase));
    }
}
