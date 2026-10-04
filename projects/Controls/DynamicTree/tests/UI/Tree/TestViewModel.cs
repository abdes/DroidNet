// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Tests.Tree;

internal partial class TestViewModel(ILoggerFactory? loggerFactory = null) : DynamicTreeViewModel(loggerFactory)
{
    public int RenameCommitCount { get; private set; }

    public TreeItemRenameResult? NextRenameResult { get; set; }

    public TaskCompletionSource<TreeItemRenameResult>? PendingRename { get; set; }

    public TaskCompletionSource? RenameCommitStarted { get; set; }

    public Task InitializeAsync(ITreeItem root) => this.InitializeRootAsync(root, skipRoot: false);

    public override async Task<TreeItemRenameResult> CommitRenameAsync(ITreeItem item, string newName)
    {
        this.RenameCommitCount++;
        if (this.PendingRename is { } pending)
        {
            this.PendingRename = null;
            _ = this.RenameCommitStarted?.TrySetResult();
            return await pending.Task.ConfigureAwait(true);
        }

        if (this.NextRenameResult is { } result)
        {
            this.NextRenameResult = null;
            if (!result.Succeeded)
            {
                return result;
            }
        }

        return await base.CommitRenameAsync(item, newName).ConfigureAwait(true);
    }

    /// <summary>
    /// Loads the tree structure asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public async Task LoadTreeStructureAsync()
    {
        // Create a richer tree structure using the R / Cn / GCn naming convention.
        var rootNode = new TestTreeItem
        {
            Label = "R",
            Children =
            [
                new TestTreeItem
                {
                    Label = "R-C1",
                    Children =
                    [
                        new TestTreeItem { Label = "R-C1-GC1", Children = [] },
                        new TestTreeItem { Label = "R-C1-GC2", Children = [] },
                    ],
                },
                new TestTreeItem
                {
                    Label = "R-C2",
                    Children =
                    [
                        new TestTreeItem { Label = "R-C2-GC1", Children = [] },
                    ],
                },
                new TestTreeItem
                {
                    Label = "R-C3",
                    Children =
                    [],
                },
            ],
        };

        var rootItem = new TestItemAdapter(rootNode, isRoot: true, isHidden: false);
        await this.InitializeRootAsync(rootItem, skipRoot: false).ConfigureAwait(false);
    }

    // Compatibility helpers used by UI tests
    public void SelectItem(ITreeItem item, RequestOrigin origin)
    {
        var isCtrl = this.SelectionMode == SelectionMode.Multiple && this.SelectionModel?.SelectedItem is not null;
        this.SelectItemCommand.Execute(new ItemSelectionArgs(item, origin, IsCtrlKeyDown: isCtrl, IsShiftKeyDown: false));
    }

    public void ClearAndSelectItem(ITreeItem item) => this.SelectionModel?.ClearAndSelectItem(item);

    public new bool FocusItem(ITreeItem item, RequestOrigin origin) => base.FocusItem(item, origin);

    public new bool FocusNextVisibleItem(RequestOrigin origin) => base.FocusNextVisibleItem(origin);

    public new bool FocusPreviousVisibleItem(RequestOrigin origin) => base.FocusPreviousVisibleItem(origin);

    public new bool FocusFirstVisibleItemInParent(RequestOrigin origin) => base.FocusFirstVisibleItemInParent(origin);

    public new bool FocusLastVisibleItemInParent(RequestOrigin origin) => base.FocusLastVisibleItemInParent(origin);

    public new bool FocusFirstVisibleItemInTree(RequestOrigin origin) => base.FocusFirstVisibleItemInTree(origin);

    public new bool FocusLastVisibleItemInTree(RequestOrigin origin) => base.FocusLastVisibleItemInTree(origin);
}
