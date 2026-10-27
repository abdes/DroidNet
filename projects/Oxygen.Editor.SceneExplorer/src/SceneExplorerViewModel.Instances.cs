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
    /// <summary>Gets the rows of the given references that the tree realizes, in order.</summary>
    /// <param name="nodes">The nodes.</param>
    /// <returns>The realized rows.</returns>
    internal IEnumerable<NodeRowAdapter> RowsOf(IEnumerable<NodeReference> nodes)
        => nodes.Select(this.projection.GetRow).OfType<NodeRowAdapter>();

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
            if (this.StructureOf(root.InstanceSource!) is { } structure)
            {
                AddInstancedRows(root, root.AttachedObject.Id, structure.Nodes.ToLookup(static node => node.Parent), parent: null);
            }
        }
    }

    /// <summary>
    /// Gets whether the changes give a node its instance component, take it away or swap its source, which a rebuild
    /// shows: rows are built for what a node is when they are made.
    /// </summary>
    /// <param name="changes">The scene's changes.</param>
    /// <returns>
    /// <see langword="true"/> when a realized row's node is no longer the instance root of the source the row shows, or
    /// the reverse.
    /// </returns>
    private bool ChangesInstanceRoots(IEnumerable<SceneChange> changes)
    {
        foreach (var change in changes)
        {
            var node = change switch
            {
                SceneChange.ComponentAdded added => added.Node,
                SceneChange.ComponentRemoved removed => removed.Node,
                SceneChange.InstanceSourceChanged swapped => swapped.Node,
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

    // The structure the provider last read of a source, or null when none was read.
    private SceneStructure? StructureOf(AssetPath source)
        => this.session?.Context is { } project ? this.structures?.Find(project, source) : null;

    // Structures were read or read again: the hidden and locked entries of nodes they no longer hold are dropped, and the
    // instances' rows are rebuilt from them.
    private void OnStructuresChanged(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;
        _ = this.ApplyStructuresAsync();
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed rebuild is logged; the tree stays usable without the instance's rows.")]
    private async Task ApplyStructuresAsync()
    {
        if (this.isDisposed || this.Scene is not { } sceneAdapter)
        {
            return;
        }

        this.DropUnresolvedInstancedNodes(sceneAdapter.AttachedObject);
        try
        {
            await this.ReconcileProjectionAsync().ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.LogProjectionUpdateFailed(exception, this.SceneDocumentId);
        }
    }

    /// <summary>
    /// Drops the hidden and locked entries of the nodes an instance's source no longer holds, by the structure read of
    /// the source. A structure with issues lacks the nodes of the scenes it could not read, which may only be unmounted
    /// for now: their entries are kept.
    /// </summary>
    /// <param name="scene">The scene.</param>
    private void DropUnresolvedInstancedNodes(Scene scene)
    {
        foreach (var node in scene.AllNodes)
        {
            if (node.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } instance
                && this.StructureOf(instance.Source) is { Issues.IsEmpty: true } structure)
            {
                this.interaction?.DropUnresolvedNodes(node.Id, path => structure.Find(path) is not null);
            }
        }
    }
}
