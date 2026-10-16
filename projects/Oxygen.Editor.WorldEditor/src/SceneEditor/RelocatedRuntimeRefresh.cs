// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Holds the nodes and environment of an open scene that followed a relocation, until the assets they now reference
/// are mounted in the preview. Until then the runtime keeps showing what it showed; whoever cooks the moved assets,
/// the next mounted state shows them.
/// </summary>
internal sealed class RelocatedRuntimeRefresh
{
    private readonly HashSet<Guid> nodes = [];
    private bool environment;

    /// <summary>Gets a value indicating whether nodes or the environment wait for their assets.</summary>
    public bool IsPending => this.nodes.Count != 0 || this.environment;

    /// <summary>Adds what a relocation re-pointed.</summary>
    /// <param name="result">The scene's result of following the relocation.</param>
    public void Add(SceneRelocationResult result)
    {
        ArgumentNullException.ThrowIfNull(result);
        this.nodes.UnionWith(result.RuntimeNodes);
        this.environment |= result.RuntimeEnvironment;
    }

    /// <summary>Takes the waiting nodes and environment whose assets are all mounted; nodes no longer in the scene are dropped.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="isMounted">Whether the preview has mounted an asset.</param>
    /// <returns>The nodes to attach again, and whether to update the environment.</returns>
    public Ready TakeReady(Scene scene, Func<Uri, bool> isMounted)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(isMounted);
        var ready = new List<Guid>();
        var current = scene.AllNodes.ToDictionary(static node => node.Id);
        foreach (var id in this.nodes.ToArray())
        {
            if (!current.TryGetValue(id, out var node))
            {
                _ = this.nodes.Remove(id);
            }
            else if (SceneReferenceRetargeting.RuntimeReferences(node).All(isMounted))
            {
                _ = this.nodes.Remove(id);
                ready.Add(id);
            }
        }

        var environmentReady = this.environment && SceneReferenceRetargeting.ReferencedUris(scene.Environment).All(isMounted);
        this.environment &= !environmentReady;
        return new Ready(ready, environmentReady);
    }

    /// <summary>Forgets every waiting node and the environment.</summary>
    public void Clear()
    {
        this.nodes.Clear();
        this.environment = false;
    }

    /// <summary>What to show again in the runtime.</summary>
    /// <param name="Nodes">The nodes to attach again.</param>
    /// <param name="Environment">Whether to update the environment.</param>
    public sealed record Ready(IReadOnlyList<Guid> Nodes, bool Environment);
}
