// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Documents.Selection;

/// <inheritdoc />
public sealed class SceneSelectionService : ISceneSelectionService
{
    private readonly Dictionary<Guid, SelectionSnapshot> selections = [];

    /// <inheritdoc />
    public void SetSelection(Guid documentId, IReadOnlyList<SceneNode> nodes, string source)
    {
        ArgumentNullException.ThrowIfNull(nodes);

        var orderedIds = nodes
            .Where(node => node is not null)
            .Select(node => node.Id)
            .Distinct()
            .ToArray();

        this.selections[documentId] = new SelectionSnapshot(
            orderedIds,
            orderedIds.Length == 0 ? null : orderedIds[^1],
            source,
            DateTimeOffset.Now);
    }

    /// <inheritdoc />
    public IReadOnlyList<SceneNode> GetSelectedNodes(Guid documentId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        if (!this.selections.TryGetValue(documentId, out var snapshot))
        {
            return [];
        }

        var byId = scene.AllNodes.ToDictionary(node => node.Id);
        return snapshot.SelectedNodeIds
            .Where(byId.ContainsKey)
            .Select(id => byId[id])
            .ToArray();
    }

    /// <inheritdoc />
    public IReadOnlyList<SceneNode> Reconcile(Guid documentId, Scene scene)
    {
        var nodes = this.GetSelectedNodes(documentId, scene);
        if (nodes.Count == 0)
        {
            this.Clear(documentId);
            return [];
        }

        this.SetSelection(documentId, nodes, "Reconcile");
        return nodes;
    }

    /// <inheritdoc />
    public void Clear(Guid documentId) => _ = this.selections.Remove(documentId);

    private sealed record SelectionSnapshot(
        IReadOnlyList<Guid> SelectedNodeIds,
        Guid? PrimaryNodeId,
        string Source,
        DateTimeOffset UpdatedAt);
}
