// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector;

/// <content>
/// Nodes an instance brings in (scene instances 6.2, 8.5): each selected one shows as a transient node whose components
/// hold its effective values, read from the mounted cooked scenes. The pane shows it read-only, its header naming its
/// source and refusing structural commands.
/// </content>
public sealed partial class SceneInspectorViewModel
{
    // The transient node shown for each selected node an instance brings in.
    private readonly Dictionary<SceneNode, NodeReference> instancedNodes = new(ReferenceEqualityComparer.Instance);

    // Grows with each read of the instanced selection, so a read that a newer selection replaced is dropped.
    private int instancedReadGeneration;
    private bool isReadingInstancedNodes;

    /// <summary>
    /// Gets the second line of the header for a node an instance brings in, naming its instance and the instance's source,
    /// such as "in Car3 · Car.glb (model)"; <see langword="null"/> for any other selection.
    /// </summary>
    public string? SelectedNodeOrigin
        => this.SelectedInstancedNode() is { } reference && this.InstanceRootOf(reference) is { } root
            ? $"in {root.Node.Metadata.Name} · {root.Source.Name} ({(AssetFileTypes.Is(root.Source.Name, AssetFileCategory.Model) ? "model" : "scene")})"
            : null;

    /// <summary>
    /// Gets why a node an instance brings in refuses structural commands, the tooltip of the disabled <i>Add component</i>
    /// and <i>Remove component</i>; <see langword="null"/> for any other selection.
    /// </summary>
    public string? SelectedNodeStructureRefusal
        => this.SelectedInstancedNode() is { } reference && this.InspectedScene is { } scene
            ? InstanceRules.RefusesStructure(scene, [reference], _ => this.SelectedNode?.Metadata.Name)?.Message
            : null;

    /// <summary>
    /// Gets a value indicating whether the single-node header takes input: for a node the scene can edit, and for a node
    /// an instance brings in, whose header still selects its instance while its structural commands stay disabled.
    /// </summary>
    public bool CanUseNodeHeader => this.CanEditSelectedNodes || this.SelectedInstancedNode() is not null;

    // Whether any selected node is one an instance brings in; the pane shows those read-only.
    private bool HasInstancedSelection => this.items.Any(this.instancedNodes.ContainsKey);

    // The node an instance brings in when it is the single selected node.
    private NodeReference? SelectedInstancedNode()
        => this.SelectedNode is { } node ? this.instancedNodes.GetValueOrDefault(node) : null;

    private (SceneNode Node, AssetPath Source)? InstanceRootOf(NodeReference reference)
        => this.InspectedScene?.AllNodes.FirstOrDefault(node => node.Id == reference.Node) is { } root
            && root.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } instance
            ? (root, instance.Source)
            : null;

    // The lock that protects a selected node, a node of the scene by its ancestors and a node an instance brings in by
    // its reference: whether the node itself is locked, and the name of the locked node. A locked instanced ancestor is
    // named by the instance root it is below.
    private (bool IsSelf, string Name)? LockOf(SceneNode node)
    {
        if (this.interaction is not { } interaction)
        {
            return null;
        }

        if (!this.instancedNodes.TryGetValue(node, out var reference))
        {
            return interaction.GetLockOwner(node) is { } owner ? (ReferenceEquals(owner, node), owner.Metadata.Name) : null;
        }

        return interaction.GetLockOwner(node.Scene, reference) is { } lockOwner
            ? (lockOwner == reference, node.Scene.AllNodes.FirstOrDefault(candidate => candidate.Id == lockOwner.Node)?.Metadata.Name ?? string.Empty)
            : null;
    }

    // Selects the instance root of the single selected node an instance brings in.
    [RelayCommand(CanExecute = nameof(CanSelectInstance))]
    private void SelectInstance()
    {
        if (this.SelectedInstancedNode() is { } reference && this.documents.ActiveSceneId is { } documentId)
        {
            this.sceneSelectionService.SetSelection(documentId, [NodeReference.To(reference.Node)], InspectorSelectionSource);
        }
    }

    private bool CanSelectInstance() => this.SelectedInstancedNode() is not null;

    // A new selection drops the transient nodes of the last one and reads those it holds.
    private void ReadInstancedSelection(Scene? scene)
    {
        this.instancedNodes.Clear();
        this.RereadInstancedSelection(scene);
    }

    // Reads the effective values of each selected node an instance brings in; the nodes show once all are read, after
    // the nodes of the scene the selection holds.
    private void RereadInstancedSelection(Scene? scene)
    {
        var generation = ++this.instancedReadGeneration;
        var references = this.HasSelectionSummary || scene is null || this.structures is null
            ? []
            : this.selectionContext.SelectedNodes.Where(static reference => !reference.IsOwned).ToArray();
        this.isReadingInstancedNodes = references.Length != 0;
        if (this.isReadingInstancedNodes)
        {
            _ = this.ReadInstancedNodesAsync(scene!, references, generation);
        }
    }

    private async Task ReadInstancedNodesAsync(Scene scene, IReadOnlyList<NodeReference> references, int generation)
    {
        var read = new List<(SceneNode Node, NodeReference Reference)>();
        try
        {
            foreach (var reference in references)
            {
                if (await this.ReadInstancedNodeAsync(scene, reference).ConfigureAwait(true) is { } node)
                {
                    read.Add((node, reference));
                }
            }
        }
        catch (Exception ex) when (ex is InvalidOperationException or ArgumentException)
        {
            this.LogInstancedReadFailed(ex);
            read.Clear();
        }

        if (generation != this.instancedReadGeneration || this.isDisposed)
        {
            return;
        }

        this.isReadingInstancedNodes = false;
        var owned = this.items.Where(node => !this.instancedNodes.ContainsKey(node)).ToList();
        this.instancedNodes.Clear();
        foreach (var (node, reference) in read)
        {
            this.instancedNodes[node] = reference;
        }

        this.items = [.. owned, .. read.Select(static entry => entry.Node)];
        this.NotifySelectionRoutingChanged();
        this.ShowItems();
        this.SubscribeToComponentCollections();
    }

    private async Task<SceneNode?> ReadInstancedNodeAsync(Scene scene, NodeReference reference)
    {
        if (this.structures is not { } structures
            || this.InstanceSourceOf(scene, reference) is not { } source
            || await structures.ReadNodeAsync(this.session.Context, source, reference.Path).ConfigureAwait(true) is not { } values)
        {
            return null;
        }

        return InstancedNodes.Build(scene, reference, values);
    }

    private AssetPath? InstanceSourceOf(Scene scene, NodeReference reference)
        => scene.AllNodes.FirstOrDefault(node => node.Id == reference.Node)?.Components.OfType<SceneInstanceComponent>().FirstOrDefault()?.Source;

    // Content mounted again, as after a cook: the selected instanced nodes may hold other values now.
    private void OnStructuresChanged(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.dispatcher is { HasThreadAccess: false })
        {
            _ = this.dispatcher.TryEnqueue(() => this.RereadInstancedSelection(this.activeScene));
            return;
        }

        this.RereadInstancedSelection(this.activeScene);
    }
}
