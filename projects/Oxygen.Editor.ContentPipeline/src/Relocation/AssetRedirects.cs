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
/// record resets when the project changes, together with every document's history.
/// </summary>
public sealed partial class AssetRedirects : IAssetRedirects, IDisposable
{
    private readonly Lock sync = new();
    private readonly List<RelocationMap> relocations = [];
    private readonly HashSet<AssetPath> deleted = [];
    private readonly IProjectContextService projects;
    private readonly ILogger logger;
    private readonly IDisposable projectChanges;

    /// <summary>Initializes a new instance of the <see cref="AssetRedirects"/> class.</summary>
    /// <param name="projects">The active project; a project change clears the record.</param>
    /// <param name="logger">Records each resolved redirect.</param>
    public AssetRedirects(IProjectContextService projects, ILogger<AssetRedirects>? logger = null)
    {
        ArgumentNullException.ThrowIfNull(projects);
        this.projects = projects;
        this.logger = (ILogger?)logger ?? NullLogger.Instance;
        this.projectChanges = projects.ProjectChanged.Subscribe(_ => this.Clear());
    }

    /// <inheritdoc/>
    public string Resolve(string reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        RelocationMap[] snapshot;
        lock (this.sync)
        {
            if (this.relocations.Count == 0)
            {
                return reference;
            }

            snapshot = [.. this.relocations];
        }

        var resolved = AuthoredReferenceFormat.MapReference(reference, identity => this.ResolveIdentity(identity, snapshot));
        if (resolved is null)
        {
            return reference;
        }

        this.LogRedirected(reference, resolved);
        return resolved;
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

        this.LogRedirected(reference.ToString(), resolved.ToString());
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

    /// <inheritdoc/>
    public void Dispose() => this.projectChanges.Dispose();

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
        => this.projects.ActiveProject is { } project && RelocationPaths.FindWritableMount(project, identity) is not null
            && (project.FindSource(identity) is not null || (RelocationPaths.ToPhysical(project, identity) is { } folder && Directory.Exists(folder.FullPath)));

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
