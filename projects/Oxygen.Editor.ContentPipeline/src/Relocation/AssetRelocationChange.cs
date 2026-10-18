// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// A committed relocation, as open documents see it: which files moved, which were rewritten and with what
/// version, and how any asset reference maps to its new identity.
/// </summary>
public sealed class AssetRelocationChange
{
    private readonly RelocationMap map;

    internal AssetRelocationChange(RelocationMap map, IReadOnlyList<RelocatedFile> rewrittenFiles)
    {
        this.map = map;
        this.RewrittenFiles = rewrittenFiles;
    }

    /// <summary>Gets the physical moves, including companion files and folders.</summary>
    public IReadOnlyList<RelocationFileMove> Moves => this.map.Moves;

    /// <summary>Gets each rewritten file with its locations and the version the transaction wrote.</summary>
    public IReadOnlyList<RelocatedFile> RewrittenFiles { get; }

    /// <summary>Maps an asset reference to its new identity, keeping its form.</summary>
    /// <param name="reference">An asset URI or a canonical virtual path.</param>
    /// <returns>The new reference, or null when the referenced asset did not move.</returns>
    public string? MapReference(string reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        return AuthoredReferenceFormat.MapReference(reference, this.map.MapIdentity);
    }

    /// <summary>Maps an asset to its new identity.</summary>
    /// <param name="asset">The asset's identity.</param>
    /// <returns>The new identity, or null when the asset did not move.</returns>
    public AssetPath? MapAsset(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return this.map.MapIdentity(asset);
    }

    /// <summary>Maps a physical location through the moves.</summary>
    /// <param name="path">A location on disk.</param>
    /// <returns>The new location, or null when the path did not move.</returns>
    public FileSystemPath? MapPath(FileSystemPath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return this.map.IsMoved(path) ? this.map.MapPath(path) : null;
    }

    /// <summary>Finds the rewrite of a file, by its location before or after the moves.</summary>
    /// <param name="path">A location on disk.</param>
    /// <returns>The rewrite, or null when the relocation did not rewrite the file.</returns>
    public RelocatedFile? FindRewrite(FileSystemPath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return this.RewrittenFiles.FirstOrDefault(file => file.OriginalPath == path || file.Path == path);
    }
}
