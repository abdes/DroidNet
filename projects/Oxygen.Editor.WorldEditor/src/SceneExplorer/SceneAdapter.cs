// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

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

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => scene.RootNodes.Count;

    /// <inheritdoc />
    protected override async Task LoadChildren()
    {
        this.ClearChildren();

        if (this.UseLayoutAdapters)
        {
            await this.LoadLayoutChildrenAsync().ConfigureAwait(false);
            return;
        }

        await this.LoadSceneChildrenAsync().ConfigureAwait(false);
    }

    private async Task LoadLayoutChildrenAsync()
    {
        // Build layout using LayoutNodeAdapter wrappers so layout operations stay scene-agnostic.
        var layout = this.AttachedObject.ExplorerLayout;
        var seenNodeIds = new HashSet<System.Guid>();

        if (layout is not null && layout.Count > 0)
        {
            void BuildFromEntry(ExplorerEntryData entry, TreeItemAdapter parent)
            {
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                {
                    var folderId = entry.FolderId ?? Guid.NewGuid();
                    var folder = new FolderAdapter(folderId, entry.Name ?? "Folder") { IsExpanded = false };

                    if (entry.Children is not null)
                    {
                        foreach (var childEntry in entry.Children)
                        {
                            BuildFromEntry(childEntry, folder);
                        }
                    }

                    if (parent is FolderAdapter folderParent)
                    {
                        folderParent.AddChildAdapter(folder);
                    }
                    else
                    {
                        this.AddChildInternal(folder);
                    }

                    return;
                }

                if (!string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) || entry.NodeId is null)
                {
                    return;
                }

                var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == entry.NodeId);
                if (node is null)
                {
                    return;
                }

                var nodeAdapter = new SceneNodeAdapter(node) { IsExpanded = false };
                var layoutNode = new LayoutNodeAdapter(nodeAdapter) { IsExpanded = false };
                _ = seenNodeIds.Add(node.Id);

                if (parent is FolderAdapter folderContainer)
                {
                    folderContainer.AddChildAdapter(layoutNode);
                    return;
                }

                this.AddChildInternal(layoutNode);
            }

            foreach (var entry in layout)
            {
                BuildFromEntry(entry, this);
            }
        }

        foreach (var entity in this.AttachedObject.RootNodes)
        {
            if (seenNodeIds.Contains(entity.Id))
            {
                continue;
            }

            var nodeAdapter = new SceneNodeAdapter(entity) { IsExpanded = false };
            var layoutNode = new LayoutNodeAdapter(nodeAdapter) { IsExpanded = false };
            this.AddChildInternal(layoutNode);
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    private async Task LoadSceneChildrenAsync()
    {
        // Original path: build tree using SceneNodeAdapter only (no layout separation)
        var layout = this.AttachedObject.ExplorerLayout;
        var seenNodeIds = new HashSet<System.Guid>();

        if (layout is not null && layout.Count > 0)
        {
            void BuildFromEntry(ExplorerEntryData entry, TreeItemAdapter parent)
            {
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                {
                    var folderId = entry.FolderId ?? Guid.NewGuid();
                    var folder = new FolderAdapter(folderId, entry.Name ?? "Folder") { IsExpanded = false };

                    if (entry.Children is not null)
                    {
                        foreach (var childEntry in entry.Children)
                        {
                            BuildFromEntry(childEntry, folder);
                        }
                    }

                    if (parent is FolderAdapter folderParent)
                    {
                        folderParent.AddChildAdapter(folder);
                    }
                    else
                    {
                        this.AddChildInternal(folder);
                    }
                }
                else if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId is not null)
                {
                    var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == entry.NodeId);
                    if (node is not null)
                    {
                        var nodeAdapter = new SceneNodeAdapter(node) { IsExpanded = false };
                        _ = seenNodeIds.Add(node.Id);

                        if (parent is FolderAdapter folderParent)
                        {
                            folderParent.AddChildAdapter(nodeAdapter);
                        }
                        else if (parent is SceneAdapter sceneParent)
                        {
                            // Only SceneAdapter can attach SceneNodeAdapter children at the root level
                            sceneParent.AddChildInternal(nodeAdapter);
                        }
                    }
                }
            }

            foreach (var entry in layout)
            {
                BuildFromEntry(entry, this);
            }
        }

        // Add any root nodes that were not present in the layout (backward-compatibility)
        foreach (var entity in this.AttachedObject.RootNodes)
        {
            if (!seenNodeIds.Contains(entity.Id))
            {
                this.AddChildInternal(new SceneNodeAdapter(entity) { IsExpanded = false });
            }
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    /// <summary>
    ///     Reloads the children collection from the current <see cref="AttachedObject.ExplorerLayout"/>
    ///     or the scene root nodes. This forces a refresh of adapters in-place without creating a
    ///     new SceneAdapter instance which would unnecessarily recreate parent links and view state.
    /// </summary>
    public async Task ReloadChildrenAsync()
    {
        // Clear the cached children then call the same loading path used by InitializeChildrenCollectionAsync
        this.ClearChildren();

        // Load children using the same implementation as LoadChildren()
        await this.LoadChildren().ConfigureAwait(true);
    }
}
