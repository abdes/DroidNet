// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Controls.Selection;
using Oxygen.Managed.Assets.Filesystem;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Applies only the latest folder scope, without holding input suppression across asynchronous lookup.</summary>
public partial class ProjectLayoutViewModel
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

        var paths = contentBrowserState.SelectedFolders.ToArray();
        var selected = new HashSet<TreeItemAdapter>(ReferenceEqualityComparer.Instance);
        try
        {
            foreach (var path in paths)
            {
                var target = path is "/" or "." ? root
                    : VirtualPath.IsCanonicalAbsolute(path)
                        ? await this.FindAdapterByVirtualPathAsync(path).ConfigureAwait(true)
                        : await FindFolderAdapterAsync(root, path).ConfigureAwait(true);
                if (!this.IsSelectionRequestCurrent(version, root))
                {
                    return;
                }

                if (target is TreeItemAdapter adapter)
                {
                    _ = selected.Add(adapter);
                }
            }

            if (this.IsSelectionRequestCurrent(version, root) && this.ShownItems.Contains(root)
                && await this.RevealSelectedAncestorsAsync(selected, version, root).ConfigureAwait(true))
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
        }
        catch (ObjectDisposedException) when (!this.IsSelectionRequestCurrent(version, root))
        {
            // A retired tree cannot apply its delayed lookup to the new scope.
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
