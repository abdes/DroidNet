// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Testing;

/// <summary>
/// An <see cref="ISceneStructureProvider"/> for tests: the structures of the sources a test publishes. A source with no
/// structure reads as <see langword="null"/> (not cooked), as the real reader answers.
/// </summary>
/// <remarks>Structures do not depend on the project a read names.</remarks>
public sealed class FakeSceneStructures : ISceneStructureProvider
{
    private readonly Lock gate = new();
    private readonly Dictionary<AssetPath, SceneStructure> structures = [];

    /// <inheritdoc />
    public event EventHandler? Changed;

    /// <summary>
    /// Publishes what a source brings in, replacing any earlier structure: each node by its path, name and parent's path,
    /// parents before their children.
    /// </summary>
    /// <param name="source">The source: a model's source file or an authored scene.</param>
    /// <param name="nodes">The nodes; a null parent is a root node of the source.</param>
    /// <returns>The published structure.</returns>
    public SceneStructure Publish(AssetPath source, params IEnumerable<(NodePath Path, string Name, NodePath? Parent)> nodes)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(nodes);
        var scene = new StructureScene(Guid.NewGuid(), source.VirtualPath);
        var structure = new SceneStructure(
            source,
            scene,
            [.. nodes.Select(node => new StructureNode(node.Path, node.Name, node.Parent, scene, Instance: null))],
            ImmutableArray<StructureIssue>.Empty);
        lock (this.gate)
        {
            this.structures[source] = structure;
        }

        return structure;
    }

    /// <summary>Withdraws a source's structure, as when its cook goes stale.</summary>
    /// <param name="source">The source.</param>
    public void Withdraw(AssetPath source)
    {
        lock (this.gate)
        {
            _ = this.structures.Remove(source);
        }
    }

    /// <summary>Announces that structures read before may differ now, as when content is mounted again.</summary>
    public void RaiseChanged() => this.Changed?.Invoke(this, EventArgs.Empty);

    /// <inheritdoc />
    public Task<SceneStructure?> ReadAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        cancellationToken.ThrowIfCancellationRequested();
        lock (this.gate)
        {
            return Task.FromResult(this.structures.GetValueOrDefault(source));
        }
    }
}
