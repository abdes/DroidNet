// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Controls.Selection;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Applies only the latest folder scope, without holding input suppression across asynchronous lookup.</summary>
public partial class ContentFoldersViewModel
{
    private long selectionRequestVersion;
    private bool selectionDisposed;

    private static void ApplyLoadedSelection(TreeItemAdapter adapter, HashSet<TreeItemAdapter> selected)
    {
        adapter.IsSelected = selected.Contains(adapter);
        if (adapter.TryGetLoadedChildren(out var children))
        {
            foreach (var child in children.OfType<TreeItemAdapter>())
            {
                ApplyLoadedSelection(child, selected);
            }
        }
    }

    private async Task UpdateTreeSelectionFromStateAsync()
    {
        var version = ++this.selectionRequestVersion;
        var root = this.projectRoot;
        if (root is null || this.selectionDisposed)
        {
            return;
        }

        var paths = contentExplorerState.SelectedFolders.ToArray();
        var selected = new HashSet<TreeItemAdapter>(ReferenceEqualityComparer.Instance);
        if (paths.Length == 0)
        {
            // No selected folder browses the whole project, which the project's row stands for.
            _ = selected.Add(root);
        }

        try
        {
            var shown = new List<AssetPath?>(paths.Length);
            foreach (var path in paths)
            {
                var (existing, target) = await this.FindNearestExistingFolderAsync(root, path).ConfigureAwait(true);
                if (!this.IsSelectionRequestCurrent(version, root))
                {
                    return;
                }

                if (target is TreeItemAdapter adapter)
                {
                    _ = selected.Add(adapter);
                }

                if (existing != path)
                {
                    this.LogMissingSelectedFolder(path, existing);
                }

                shown.Add(existing);
            }

            if (!shown.SequenceEqual(paths))
            {
                // The new scope selects the tree again, through the state's change.
                contentExplorerState.SetSelectedFolders(shown.Contains(null) ? [] : shown.OfType<AssetPath>());
                return;
            }

            if (this.IsSelectionRequestCurrent(version, root) && this.ShownItems.Contains(root)
                && await this.RevealSelectedAncestorsAsync(selected, version, root).ConfigureAwait(true))
            {
                this.SelectInTree(root, selected);
            }
        }
        catch (ObjectDisposedException) when (!this.IsSelectionRequestCurrent(version, root))
        {
            // A retired tree cannot apply its delayed lookup to the new scope.
        }
    }

    // A folder deleted since it was selected, or reached again through Back or Forward, is not shown: its nearest
    // folder that still exists is.
    // When not even the folder's mount is shown, the whole project is.
    private async Task<(AssetPath? Existing, ITreeItem? Target)> FindNearestExistingFolderAsync(TreeItemAdapter root, AssetPath path)
    {
        for (var existing = path; existing is not null; existing = existing.Parent)
        {
            if (await this.FindAdapterByVirtualPathAsync(existing).ConfigureAwait(true) is { } target)
            {
                return (existing, target);
            }
        }

        return (null, root);
    }

    // Selects the shown rows of the selected folders without the selection writing back to the state.
    private void SelectInTree(TreeItemAdapter root, HashSet<TreeItemAdapter> selected)
    {
        this.isUpdatingFromState = true;
        try
        {
            if (this.SelectionModel is MultipleSelectionModel<ITreeItem> selection)
            {
                selection.SelectItemsAt(this.ShownItems.Select((item, index) => (item, index))
                    .Where(pair => pair.item is TreeItemAdapter adapter && selected.Contains(adapter))
                    .Select(static pair => pair.index).ToArray());
            }

            ApplyLoadedSelection(root, selected);
            this.UpdateSelectionDerivedState();
        }
        finally
        {
            this.isUpdatingFromState = false;
        }
    }

    private async Task<bool> RevealSelectedAncestorsAsync(HashSet<TreeItemAdapter> selected, long version, TreeItemAdapter root)
    {
        foreach (var target in selected)
        {
            var ancestors = new Stack<ITreeItem>();
            for (var parent = target.Parent; parent is not null; parent = parent.Parent)
            {
                ancestors.Push(parent);
            }

            while (ancestors.TryPop(out var ancestor))
            {
                await this.ExpandItemAsync(ancestor).ConfigureAwait(true);
                if (!this.IsSelectionRequestCurrent(version, root))
                {
                    return false;
                }
            }
        }

        return true;
    }

    private bool IsSelectionRequestCurrent(long version, TreeItemAdapter root)
        => !this.selectionDisposed && version == this.selectionRequestVersion && ReferenceEquals(root, this.projectRoot);
}
