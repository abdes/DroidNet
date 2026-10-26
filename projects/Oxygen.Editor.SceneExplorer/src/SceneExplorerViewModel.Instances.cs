// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>
/// An instance's nodes in the tree (scene instances 6.2, 8.4): each instance root's row holds the rows of the nodes its
/// source brings in, read from the source's structure, dimmed and refusing every structural command.
/// </summary>
public partial class SceneExplorerViewModel
{
    // Each source's structure as last read: rows are built from it at once, and a read that finds it changed
    // rebuilds them.
    private readonly Dictionary<AssetPath, SceneStructure> instanceStructures = [];

    // The sources read or being read, so each is read once until content is mounted again.
    private readonly HashSet<AssetPath> readingStructures = [];

    // Counts the mounts the provider reported: a read that started before the latest one is out of date.
    private int structuresGeneration;

    /// <summary>Gets the rows of the given references that the tree realizes, in order.</summary>
    /// <param name="nodes">The nodes.</param>
    /// <returns>The realized rows.</returns>
    internal IEnumerable<NodeRowAdapter> RowsOf(IEnumerable<NodeReference> nodes)
        => nodes.Select(this.projection.GetRow).OfType<NodeRowAdapter>();

    /// <summary>Gets the references of the nodes a row and its scene and instanced ancestors are, deepest first, without the row's own.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="node">The node.</param>
    /// <returns>The ancestors' references: the shorter paths below the instance root, then the instance root's ancestry.</returns>
    private static IEnumerable<NodeReference> AncestorsOf(Scene scene, NodeReference node)
    {
        for (var length = node.Path.Count - 1; length > 0; length--)
        {
            yield return new NodeReference(node.Node, NodePath.Of(node.Path.Nodes.Take(length)));
        }

        var owned = scene.AllNodes.FirstOrDefault(candidate => candidate.Id == node.Node);
        if (!node.IsOwned && owned is not null)
        {
            yield return NodeReference.To(owned.Id);
        }

        for (var parent = owned?.Hierarchy.Parent; parent is not null; parent = parent.Hierarchy.Parent)
        {
            yield return NodeReference.To(parent.Id);
        }
    }

    private static void AddInstancedRows(LayoutItemAdapter row, NodeId instanceRoot, ILookup<NodePath?, StructureNode> children, NodePath? parent)
    {
        foreach (var node in children[parent])
        {
            var child = new InstancedNodeAdapter(instanceRoot, node.Path, node.Name);
            AddInstancedRows(child, instanceRoot, children, node.Path);
            row.AddContent(child);
        }
    }

    private static IEnumerable<SceneNodeAdapter> RealizedNodeRows(SceneAdapter sceneAdapter)
    {
        var pending = new Stack<ITreeItem>(sceneAdapter.RootItems);
        while (pending.TryPop(out var item))
        {
            if (item is SceneNodeAdapter node)
            {
                yield return node;
            }

            if (item is LayoutItemAdapter layoutItem and not InstancedNodeAdapter)
            {
                foreach (var child in layoutItem.CurrentChildren)
                {
                    pending.Push(child);
                }
            }
        }
    }

    /// <summary>
    /// Adds, under every instance root's row, the rows of the nodes its source brings in, from the structures already
    /// read; an instance whose structure is not read yet shows none until it is.
    /// </summary>
    /// <param name="sceneAdapter">The scene's rows.</param>
    private void AddInstancedRows(SceneAdapter sceneAdapter)
    {
        foreach (var root in RealizedNodeRows(sceneAdapter).Where(static row => row.IsInstanceRoot).ToList())
        {
            if (this.instanceStructures.TryGetValue(root.InstanceSource!, out var structure))
            {
                AddInstancedRows(root, root.AttachedObject.Id, structure.Nodes.ToLookup(static node => node.Parent), parent: null);
            }
        }
    }

    /// <summary>
    /// Gets whether the changes give a node its instance component or take it away, which a rebuild shows: rows are
    /// built for what a node is when they are made.
    /// </summary>
    /// <param name="changes">The scene's changes.</param>
    /// <returns><see langword="true"/> when a realized row is an instance root and its node no longer is, or the reverse.</returns>
    private bool ChangesInstanceRoots(IEnumerable<SceneChange> changes)
    {
        foreach (var change in changes)
        {
            var node = change switch
            {
                SceneChange.ComponentAdded added => added.Node,
                SceneChange.ComponentRemoved removed => removed.Node,
                _ => NodeId.Empty,
            };
            if (!node.IsEmpty && this.projection.GetNode(node) is { } row
                && row.InstanceSource != row.AttachedObject.Components.OfType<SceneInstanceComponent>().FirstOrDefault()?.Source)
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>
    /// Expands again the node rows a rebuild replaced, which keep no expansion of their own: an instance root expanded
    /// before its source's structure is read again shows the instance's rows after it.
    /// </summary>
    /// <param name="expanded">The node rows that were expanded, parents before their children.</param>
    private async Task RestoreExpansionAsync(IReadOnlyCollection<NodeReference> expanded)
    {
        foreach (var node in expanded)
        {
            if (this.projection.GetRow(node) is { IsExpanded: false } row)
            {
                await this.ExpandItemAsync(row).ConfigureAwait(true);
            }
        }
    }

    // Content was mounted again or a scene became readable: every source is read again.
    private void OnStructuresChanged(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;
        this.structuresGeneration++;
        this.readingStructures.Clear();
        _ = this.ReadInstanceStructuresAsync();
    }

    /// <summary>
    /// Reads, in the background, the structure of each source the scene instances that is not read yet, drops the hidden
    /// and locked entries of nodes a complete structure does not hold, and rebuilds the rows once a structure arrives. A
    /// source is read once, and again when the provider reports that content was mounted again; a read that a newer
    /// mount overtakes is discarded, since the newer pass reads the source again.
    /// </summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A background read reports its failure in the log; the tree stays usable without the instance's rows.")]
    private async Task ReadInstanceStructuresAsync()
    {
        if (this.isDisposed || this.structures is not { } provider || this.session?.Context is not { } project || this.Scene is not { } sceneAdapter)
        {
            return;
        }

        var generation = this.structuresGeneration;
        var roots = sceneAdapter.AttachedObject.AllNodes
            .Select(static node => (Node: node.Id, Source: node.Components.OfType<SceneInstanceComponent>().FirstOrDefault()?.Source))
            .Where(static root => root.Source is not null)
            .ToList();
        var pending = roots.Select(static root => root.Source!).Distinct().Where(source => this.readingStructures.Add(source)).ToList();
        var changed = false;
        foreach (var source in pending)
        {
            SceneStructure? structure;
            try
            {
                structure = await provider.ReadAsync(project, source).ConfigureAwait(true);
            }
            catch (Exception exception)
            {
                // Includes a read the engine dropped; the next mount reads the source again.
                this.LogInstanceStructureReadFailed(exception, source);
                continue;
            }

            if (this.isDisposed || generation != this.structuresGeneration)
            {
                return;
            }

            if (structure is null)
            {
                // Not mounted yet: the next mount or edit that reaches the tree tries again.
                _ = this.readingStructures.Remove(source);
                continue;
            }

            this.instanceStructures[source] = structure;
            changed = true;

            // A structure with issues lacks the nodes of the scenes it could not read, which may only be unmounted for
            // now: their entries are kept.
            if (structure.Issues.IsEmpty)
            {
                foreach (var root in roots.Where(root => root.Source == source))
                {
                    this.interaction?.DropUnresolvedNodes(root.Node, path => structure.Find(path) is not null);
                }
            }
        }

        if (changed && ReferenceEquals(this.Scene, sceneAdapter))
        {
            try
            {
                await this.ReconcileProjectionAsync().ConfigureAwait(true);
            }
            catch (Exception exception)
            {
                this.LogProjectionUpdateFailed(exception, this.SceneDocumentId);
            }
        }
    }
}
