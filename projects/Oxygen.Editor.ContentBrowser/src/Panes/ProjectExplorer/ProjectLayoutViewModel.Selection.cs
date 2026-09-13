// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
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

            if (this.IsSelectionRequestCurrent(version, root))
            {
                this.isUpdatingFromState = true;
                try
                {
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

    private bool IsSelectionRequestCurrent(long version, TreeItemAdapter root)
        => !this.selectionDisposed && version == this.selectionRequestVersion && ReferenceEquals(root, this.projectRoot);
}
