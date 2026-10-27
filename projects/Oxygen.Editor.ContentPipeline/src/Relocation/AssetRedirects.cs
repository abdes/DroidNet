// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The in-memory record of the session's committed relocations. A captured reference replays every later
/// relocation in order, so A to B then B to C resolves A to C, and an Undo maps back. A reference whose asset
/// still exists is never redirected, so an asset created later at an old path keeps its own references. The
/// record is the project session's: it ends with the session, together with every document's history.
/// </summary>
public sealed partial class AssetRedirects : IAssetRedirects
{
    private readonly Lock sync = new();
    private readonly List<RelocationMap> relocations = [];
    private readonly HashSet<AssetPath> deleted = [];
    private readonly IProjectSession session;
    private readonly ILogger logger;

    /// <summary>Initializes a new instance of the <see cref="AssetRedirects"/> class.</summary>
    /// <param name="session">The project session, whose relocations the record keeps.</param>
    /// <param name="logger">Records each resolved redirect.</param>
    public AssetRedirects(IProjectSession session, ILogger<AssetRedirects>? logger = null)
    {
        ArgumentNullException.ThrowIfNull(session);
        this.session = session;
        this.logger = (ILogger?)logger ?? NullLogger.Instance;
    }

    /// <inheritdoc/>
    public AssetPath Resolve(AssetPath reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        RelocationMap[] snapshot;
        lock (this.sync)
        {
            snapshot = [.. this.relocations];
        }

        if (snapshot.Length == 0 || this.ResolveIdentity(reference, snapshot) is not { } resolved)
        {
            return reference;
        }

        this.LogRedirected(reference, resolved);
        return resolved;
    }

    /// <inheritdoc/>
    public bool WasDeleted(AssetPath reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        AssetPath[] snapshot;
        lock (this.sync)
        {
            if (this.deleted.Count == 0)
            {
                return false;
            }

            snapshot = [.. this.deleted];
        }

        var identity = AssetFileTypes.ToIdentity(reference);
        return !this.Exists(identity) && snapshot.Any(identity.IsSameOrInside);
    }

    /// <summary>Records a committed relocation.</summary>
    /// <param name="map">The relocation's identity mapping.</param>
    internal void Record(RelocationMap map)
    {
        lock (this.sync)
        {
            this.relocations.Add(map);
        }
    }

    /// <summary>Records deleted assets or folders.</summary>
    /// <param name="identities">The deleted identities.</param>
    internal void RecordDeleted(IEnumerable<AssetPath> identities)
    {
        lock (this.sync)
        {
            this.deleted.UnionWith(identities);
        }
    }

    private AssetPath? ResolveIdentity(AssetPath identity, RelocationMap[] relocations)
    {
        if (this.Exists(identity))
        {
            return null;
        }

        var current = identity;
        foreach (var relocation in relocations)
        {
            current = relocation.MapIdentity(current) ?? current;
        }

        return current == identity ? null : current;
    }

    // An asset exists while its source does; a folder while it is on disk.
    private bool Exists(AssetPath identity)
    {
        var project = this.session.Context;
        return RelocationPaths.FindWritableMount(project, identity) is not null
            && (project.FindSource(identity) is not null || (RelocationPaths.ToPhysical(project, identity) is { } folder && project.FileSystem.Directory.Exists(folder.FullPath)));
    }

    private void Clear()
    {
        int count;
        lock (this.sync)
        {
            count = this.relocations.Count + this.deleted.Count;
            this.relocations.Clear();
            this.deleted.Clear();
        }

        if (count != 0)
        {
            this.LogCleared(count);
        }
    }
}
