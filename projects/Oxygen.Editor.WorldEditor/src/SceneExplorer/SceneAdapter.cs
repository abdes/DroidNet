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

        // If an editor supplied explorer layout exists, use it to build the
        // visual tree (folders + node references). Otherwise fall back to the
        // simple flat listing of root nodes.
        var layout = this.AttachedObject.ExplorerLayout;
        var seenNodeIds = new HashSet<System.Guid>();

        if (layout is not null && layout.Count > 0)
        {
            // Recursive local function to build items from entry data
            void BuildFromEntry(ExplorerEntryData entry)
            {
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                {
                    var folderId = entry.FolderId ?? Guid.NewGuid();
                    var folder = new FolderAdapter(folderId, entry.Name ?? "Folder") { IsExpanded = false };

                    if (entry.Children is not null)
                    {
                        foreach (var childEntry in entry.Children)
                        {
                            // recurse and add child adapters directly to the folder
                            if (string.Equals(childEntry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                            {
                                // Use AddChildInternal after building children to preserve parent links
                                var subFolderId = childEntry.FolderId ?? Guid.NewGuid();
                                var subFolder = new FolderAdapter(subFolderId, childEntry.Name ?? "Folder") { IsExpanded = false };
                                // nested entries will be appended later by the simple approach below
                                // populate children of subFolder
                                if (childEntry.Children is not null)
                                {
                                    foreach (var nested in childEntry.Children)
                                    {
                                        // For nested items we either add node adapters or deeper folders
                                        if (string.Equals(nested.Type, "Node", StringComparison.OrdinalIgnoreCase) && nested.NodeId is not null)
                                        {
                                            var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == nested.NodeId);
                                            if (node is not null)
                                            {
                                                var nodeAdapter = new SceneNodeAdapter(node) { IsExpanded = false };
                                                subFolder.AddChildAdapter(nodeAdapter);
                                                seenNodeIds.Add(node.Id);
                                            }
                                        }
                                        else if (string.Equals(nested.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                                        {
                                            // deeper nesting - build an inner folder recursively (simple approach)
                                            BuildFromEntry(nested);
                                        }
                                    }
                                }

                                folder.AddChildAdapter(subFolder);
                            }
                            else if (string.Equals(childEntry.Type, "Node", StringComparison.OrdinalIgnoreCase) && childEntry.NodeId is not null)
                            {
                                var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == childEntry.NodeId);
                                if (node is not null)
                                {
                                    var nodeAdapter = new SceneNodeAdapter(node) { IsExpanded = false };
                                    folder.AddChildAdapter(nodeAdapter);
                                    seenNodeIds.Add(node.Id);
                                }
                            }
                        }
                    }

                    this.AddChildInternal(folder);
                }
                else if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId is not null)
                {
                    var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == entry.NodeId);
                    if (node is not null)
                    {
                        this.AddChildInternal(new SceneNodeAdapter(node) { IsExpanded = false });
                        seenNodeIds.Add(node.Id);
                    }
                }
            }

            foreach (var entry in layout)
            {
                BuildFromEntry(entry);
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

        await Task.CompletedTask.ConfigureAwait(true);
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
