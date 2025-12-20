// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
///     A <see cref="DynamicTree" /> item adapter for the <see cref="Scene" /> model class.
/// </summary>
/// <param name="scene">The <see cref="SceneNode" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class SceneAdapter(Scene scene) : TreeItemAdapter, ITreeItem<Scene>
{
    internal bool UseLayoutAdapters { get; init; }

    /// <inheritdoc />
    public override string Label
    {
        get => this.AttachedObject.Name;
        set
        {
            if (string.Equals(value, this.AttachedObject.Name, StringComparison.Ordinal))
            {
                return;
            }

            this.AttachedObject.Name = value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc />
    public Scene AttachedObject => scene;

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    // Cache root items to avoid blocking .Result calls on the base Children task
    private readonly List<ITreeItem> rootItemsCache = [];

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => scene.RootNodes.Count;

    /// <inheritdoc />
    protected override async Task LoadChildren()
    {
        this.ClearChildren();
        this.rootItemsCache.Clear();

        await this.RebuildTreeAsync().ConfigureAwait(false);
    }

    private async Task RebuildTreeAsync(HashSet<Guid>? expandedFolderIds = null, bool preserveNodeExpansion = false)
    {
        var layout = this.AttachedObject.ExplorerLayout;
        var seenNodeIds = new HashSet<Guid>();

        // 1. Build from Layout (The "Seating Chart")
        if (this.UseLayoutAdapters && layout is { Count: > 0 })
        {
            foreach (var entry in layout)
            {
                await this.ProcessLayoutEntryAsync(entry, this, seenNodeIds, expandedFolderIds, preserveNodeExpansion).ConfigureAwait(false);
            }
        }

        // 2. Fallback for Root Nodes not in Layout (The "Unassigned Guests")
        foreach (var node in this.AttachedObject.RootNodes)
        {
            if (seenNodeIds.Add(node.Id))
            {
                var adapter = new SceneNodeAdapter(node);
                this.PopulateMissingChildren(adapter);
                this.AddChildSafe(adapter);
            }
        }
    }

    private async Task ProcessLayoutEntryAsync(
        ExplorerEntryData entry,
        ITreeItem parent,
        HashSet<Guid> seenNodeIds,
        HashSet<Guid>? expandedFolderIds,
        bool preserveNodeExpansion)
    {
        // Case A: Folder
        if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
        {
            var folder = new FolderAdapter(entry);

            // Restore expansion state
            if (expandedFolderIds?.Contains(entry.FolderId ?? Guid.Empty) == true || entry.IsExpanded == true)
            {
                folder.IsExpanded = true;
            }

            // Recurse
            if (entry.Children != null)
            {
                foreach (var childEntry in entry.Children)
                {
                    await this.ProcessLayoutEntryAsync(childEntry, folder, seenNodeIds, expandedFolderIds, preserveNodeExpansion).ConfigureAwait(false);
                }
            }

            this.AddToParent(parent, folder);
            return;
        }

        // Case B: Scene Node
        if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId.HasValue)
        {
            var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == entry.NodeId);
            if (node == null)
            {
                return;
            }

            var adapter = new SceneNodeAdapter(node);

            if (!preserveNodeExpansion && entry.IsExpanded.HasValue)
            {
                adapter.IsExpanded = entry.IsExpanded.Value;
            }

            _ = seenNodeIds.Add(node.Id);

            // Recurse (Layout Children)
            if (entry.Children != null)
            {
                foreach (var childEntry in entry.Children)
                {
                    await this.ProcessLayoutEntryAsync(childEntry, adapter, seenNodeIds, expandedFolderIds, preserveNodeExpansion).ConfigureAwait(false);
                }
            }

            this.AddToParent(parent, adapter);
        }
    }

    private void AddToParent(ITreeItem parent, LayoutItemAdapter child)
    {
        switch (parent)
        {
            case LayoutItemAdapter layoutParent:
                if (child is FolderAdapter f)
                {
                    layoutParent.AddFolder(f);
                }
                else
                {
                    layoutParent.AddContent(child);
                }

                break;
            case SceneAdapter:
                this.AddChildSafe(child);
                break;
        }
    }

    private void AddChildSafe(TreeItemAdapter item)
    {
        this.AddChildInternal(item);
        this.rootItemsCache.Add(item);
    }

    private void PopulateMissingChildren(SceneNodeAdapter parentAdapter)
    {
        // If a node is created from fallback, it might have children that are also not in the layout.
        // We need to show them.
        foreach (var childNode in parentAdapter.AttachedObject.Children)
        {
            var childAdapter = new SceneNodeAdapter(childNode);
            this.PopulateMissingChildren(childAdapter); // Recurse
            parentAdapter.AddContent(childAdapter);
        }
    }

    /// <inheritdoc />
    public async Task ReloadChildrenAsync(HashSet<Guid>? expandedFolderIds = null, bool preserveNodeExpansion = false)
    {
        this.ClearChildren();
        this.rootItemsCache.Clear();
        await this.RebuildTreeAsync(expandedFolderIds, preserveNodeExpansion).ConfigureAwait(false);
    }

    /// <summary>
    /// Retrieves the IDs of all currently expanded folders in the UI tree.
    /// Uses the internal cache to avoid deadlocks on the UI thread.
    /// </summary>
    public HashSet<Guid> GetExpandedFolderIds()
    {
        var expanded = new HashSet<Guid>();

        // Use the cache! No more .Result deadlocks.
        var stack = new Stack<ITreeItem>(this.rootItemsCache);

        while (stack.Count > 0)
        {
            var item = stack.Pop();
            if (item is FolderAdapter folder)
            {
                if (folder.IsExpanded)
                {
                    expanded.Add(folder.Id);
                }

                // LayoutItemAdapter exposes CurrentChildren synchronously
                foreach (var child in folder.CurrentChildren)
                {
                    stack.Push(child);
                }
            }
            else if (item is SceneNodeAdapter node)
            {
                // Nodes can also contain folders now
                foreach (var child in node.CurrentChildren)
                {
                    stack.Push(child);
                }
            }
        }

        return expanded;
    }
}
