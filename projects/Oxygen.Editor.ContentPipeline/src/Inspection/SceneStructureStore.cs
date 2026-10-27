// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>
/// The structures read in a project session, one per source: the session's one copy, which the reader of the shown
/// scene keeps and everything else looks up.
/// </summary>
/// <remarks>
/// The store depends on nothing, so the documents, their edit checks and the engine projection that reads the
/// structures can all use it. The structures are replaced as a whole, so a lookup reads a consistent snapshot from any
/// thread.
/// </remarks>
public sealed class SceneStructureStore : ISceneStructures
{
    private Snapshot structures = new(Project: null, ImmutableDictionary<AssetPath, SceneStructure>.Empty);

    /// <inheritdoc/>
    public event EventHandler? Changed;

    /// <inheritdoc/>
    public SceneStructure? Find(ProjectContext project, AssetPath source)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        var read = Volatile.Read(ref this.structures);
        return ReferenceEquals(read.Project, project) ? read.Structures.GetValueOrDefault(source) : null;
    }

    /// <summary>
    /// Keeps the structures just read for a project, each replacing the last one of its source, and announces them on
    /// the calling thread. Structures read for another project replace all those of the previous one.
    /// </summary>
    /// <param name="project">The project they were read for.</param>
    /// <param name="read">The structures read; none announces nothing.</param>
    public void Keep(ProjectContext project, IReadOnlyCollection<SceneStructure> read)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(read);
        if (read.Count == 0)
        {
            return;
        }

        var current = Volatile.Read(ref this.structures);
        var kept = ReferenceEquals(current.Project, project) ? current.Structures : ImmutableDictionary<AssetPath, SceneStructure>.Empty;
        Volatile.Write(ref this.structures, new Snapshot(project, kept.SetItems(read.Select(static structure => KeyValuePair.Create(structure.Source, structure)))));
        this.Changed?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>The structures read for one project, by source.</summary>
    /// <param name="Project">The project they were read for.</param>
    /// <param name="Structures">The last structure read of each source.</param>
    private sealed record Snapshot(ProjectContext? Project, ImmutableDictionary<AssetPath, SceneStructure> Structures);
}
