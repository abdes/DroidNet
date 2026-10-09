// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;

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
    private readonly HashSet<string> deleted = [with(StringComparer.OrdinalIgnoreCase)];
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
    public Uri Resolve(Uri reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        var original = reference.ToString();
        var resolved = this.Resolve(original);
        return string.Equals(resolved, original, StringComparison.Ordinal) ? reference : new Uri(resolved);
    }

    /// <inheritdoc/>
    public bool WasDeleted(Uri reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        string[] snapshot;
        lock (this.sync)
        {
            if (this.deleted.Count == 0)
            {
                return false;
            }

            snapshot = [.. this.deleted];
        }

        var identity = RelocationPaths.ToIdentity(Uri.UnescapeDataString(reference.AbsolutePath));
        return !this.Exists(identity) && snapshot.Any(item => RelocationPaths.IsSameOrInside(identity, item));
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
    internal void RecordDeleted(IEnumerable<string> identities)
    {
        lock (this.sync)
        {
            this.deleted.UnionWith(identities);
        }
    }

    private string? ResolveIdentity(string identity, RelocationMap[] relocations)
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

        return string.Equals(current, identity, StringComparison.Ordinal) ? null : current;
    }

    private bool Exists(string identity)
    {
        if (this.projects.ActiveProject is not { } project || RelocationPaths.ToPhysical(project, identity) is not { } path)
        {
            return false;
        }

        return File.Exists(path) || File.Exists(path + ".json") || Directory.Exists(path);
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
