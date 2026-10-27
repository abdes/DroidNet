// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector;

/// <content>
/// Nodes an instance brings in (scene instances 6.2, 8.5): each selected one shows as a transient node whose components
/// hold its effective values, read from the mounted cooked scenes with this scene's changes over them. Its sections edit
/// it through its <see cref="NodeReference"/>, as changes of its instance root; its header names its source and refuses
/// structural commands, and the pane shows each change the scene makes of it, as from the viewport, when it is made.
/// </content>
public sealed partial class SceneInspectorViewModel
{
    // The transient node shown for each selected node an instance brings in.
    private readonly Dictionary<SceneNode, NodeReference> instancedNodes = [with(ReferenceEqualityComparer.Instance)];

    // The values read of each transient node, which this scene's changes override.
    private readonly Dictionary<SceneNode, InstancedNodeValues> instancedValues = [with(ReferenceEqualityComparer.Instance)];

    // The instance components whose changes the transient nodes show.
    private readonly HashSet<SceneInstanceComponent> observedInstances = [];

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
    private bool HasInstancedSelection => this.items.Exists(this.instancedNodes.ContainsKey);

    private static SceneInstanceComponent? InstanceOf(Scene scene, NodeReference reference)
        => scene.AllNodes.FirstOrDefault(node => node.Id == reference.Node)?.Components.OfType<SceneInstanceComponent>().FirstOrDefault();

    // The reference a section's edits name a selected node by: its instance root and path for a node an instance brings
    // in, the node itself otherwise.
    private NodeReference ReferenceOf(SceneNode node)
        => this.instancedNodes.TryGetValue(node, out var reference) ? reference : NodeReference.To(node.Id);

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

        // Contents not read yet protect the node through its instance root.
        return interaction.GetLock(node.Scene, reference) switch
        {
            { Owner: { } lockOwner } => (lockOwner == reference, NameOf(lockOwner.Node)),
            { UnreadInstance: { } root } => (false, NameOf(root)),
            _ => null,
        };

        string NameOf(NodeId id) => node.Scene.AllNodes.FirstOrDefault(candidate => candidate.Id == id)?.Metadata.Name ?? string.Empty;
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
        this.ForgetInstancedNodes();
        this.RereadInstancedSelection(scene);
    }

    // Drops the transient nodes and stops following their instances' changes.
    private void ForgetInstancedNodes()
    {
        this.instancedNodes.Clear();
        this.instancedValues.Clear();
        foreach (var instance in this.observedInstances)
        {
            instance.PropertyChanged -= this.OnInstanceChanged;
        }

        this.observedInstances.Clear();
    }

    // Reads the effective values of each selected node an instance brings in; the nodes show once all are read, after
    // the nodes of the scene the selection holds.
    private void RereadInstancedSelection(Scene? scene)
    {
        var generation = ++this.instancedReadGeneration;
        var references = this.HasSelectionSummary || scene is null || this.nodes is null
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
        var read = new List<(SceneNode Node, NodeReference Reference, InstancedNodeValues Values)>();
        var lifetime = this.session.Lifetime;
        try
        {
            foreach (var reference in references)
            {
                if (await this.ReadInstancedNodeAsync(scene, reference, lifetime).ConfigureAwait(true) is { } node)
                {
                    read.Add((node.Node, reference, node.Values));
                }
            }
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested)
        {
            // The project is closing: there is nothing left to show.
            return;
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
        this.ForgetInstancedNodes();
        foreach (var (node, reference, values) in read)
        {
            this.instancedNodes[node] = reference;
            this.instancedValues[node] = values;
            if (InstanceOf(scene, reference) is { } instance && this.observedInstances.Add(instance))
            {
                instance.PropertyChanged += this.OnInstanceChanged;
            }
        }

        this.items = [.. owned, .. read.Select(static entry => entry.Node)];
        this.NotifySelectionRoutingChanged();
        this.ShowItems();
        this.SubscribeToComponentCollections();
    }

    private async Task<(SceneNode Node, InstancedNodeValues Values)?> ReadInstancedNodeAsync(Scene scene, NodeReference reference, CancellationToken cancellationToken)
    {
        if (this.nodes is not { } nodes
            || InstanceOf(scene, reference) is not { } instance
            || await nodes.ReadNodeAsync(this.session.Context, instance.Source, reference.Path, cancellationToken).ConfigureAwait(true) is not { } values)
        {
            return null;
        }

        return (InstancedNodes.Build(scene, reference, values, instance.Changes.Of(reference.Path)), values);
    }

    // The scene changed an instance's nodes, as a viewport drag does: each transient node shows its values again.
    private void OnInstanceChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is not SceneInstanceComponent changed
            || !string.Equals(args.PropertyName, nameof(SceneInstanceComponent.Changes), StringComparison.Ordinal))
        {
            return;
        }

        if (this.dispatcher is { HasThreadAccess: false })
        {
            _ = this.dispatcher.TryEnqueue(() => this.ShowInstanceChanges(changed));
            return;
        }

        this.ShowInstanceChanges(changed);
    }

    private void ShowInstanceChanges(SceneInstanceComponent changed)
    {
        if (this.isDisposed || this.InspectedScene is not { } scene)
        {
            return;
        }

        foreach (var (node, reference) in this.instancedNodes)
        {
            if (ReferenceEquals(InstanceOf(scene, reference), changed) && this.instancedValues.TryGetValue(node, out var values))
            {
                InstancedNodes.Write(scene, node, values, changed.Changes.Of(reference.Path));
            }
        }

        // A change that leaves a value as it was, as a reset to the source's own value, still changes the override bars.
        this.RefreshPropertyEditorValues();
    }

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
