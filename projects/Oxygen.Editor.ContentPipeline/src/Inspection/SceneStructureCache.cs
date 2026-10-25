// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>
/// The structures read in a project session, one per source, so every open view reads one copy. Each is keyed by the
/// digest of the cooked index it was read from: a publication changes the index, so the next read misses and replaces it.
/// </summary>
internal sealed class SceneStructureCache
{
    private readonly Lock gate = new();
    private readonly Dictionary<AssetPath, Entry> entries = [];
    private ProjectContext? project;

    /// <summary>Finds the structure read from a cooked index.</summary>
    /// <param name="project">The project the structure is read for.</param>
    /// <param name="source">The source.</param>
    /// <param name="indexDigest">The digest of the cooked index that holds the source's scene now.</param>
    /// <returns>The structure, or null when none was read from that index.</returns>
    public SceneStructure? Find(ProjectContext project, AssetPath source, string indexDigest)
    {
        lock (this.gate)
        {
            return ReferenceEquals(this.project, project)
                && this.entries.TryGetValue(source, out var entry)
                && string.Equals(entry.IndexDigest, indexDigest, StringComparison.Ordinal)
                    ? entry.Structure
                    : null;
        }
    }

    /// <summary>Keeps a structure, replacing the source's earlier one and every structure of another project.</summary>
    /// <param name="project">The project the structure is read for.</param>
    /// <param name="indexDigest">The digest of the cooked index the structure is read from.</param>
    /// <param name="structure">The structure.</param>
    public void Store(ProjectContext project, string indexDigest, SceneStructure structure)
    {
        lock (this.gate)
        {
            if (!ReferenceEquals(this.project, project))
            {
                this.entries.Clear();
                this.project = project;
            }

            this.entries[structure.Source] = new(indexDigest, structure);
        }
    }

    private sealed record Entry(string IndexDigest, SceneStructure Structure);
}
