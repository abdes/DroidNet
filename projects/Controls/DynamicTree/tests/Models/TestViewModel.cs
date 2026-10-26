// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Controls.Selection;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Tests;

/// <summary>
/// A derived class from DynamicTreeViewModel to expose the InitializeRootAsync method for testing.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed partial class TestViewModel(bool skipRoot, ILoggerFactory? loggerFactory = null) :
    DynamicTreeViewModel(
        loggerFactory ?? Microsoft.Extensions.Logging.Abstractions.NullLoggerFactory.Instance)
{
    /// <inheritdoc cref="DynamicTreeViewModel.InitializeRootAsync"/>
    public async Task InitializeRootAsyncPublic(ITreeItem root) => await this.InitializeRootAsync(root, skipRoot).ConfigureAwait(false);

    /// <inheritdoc cref="DynamicTreeViewModel.InsertItemAsync"/>
    public async Task InsertItemAsyncPublic(ITreeItem item, ITreeItem parent, int relativeIndex) => await this.InsertItemAsync(item, parent, relativeIndex).ConfigureAwait(false);

    public async Task MoveItemAsyncPublic(ITreeItem item, ITreeItem newParent, int newIndex) => await this.MoveItemAsync(item, newParent, newIndex).ConfigureAwait(false);

    public async Task MoveItemsAsyncPublic(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex) => await this.MoveItemsAsync(items, newParent, startIndex).ConfigureAwait(false);

    public async Task ReorderItemAsyncPublic(ITreeItem item, int newIndex) => await this.ReorderItemAsync(item, newIndex).ConfigureAwait(false);

    public async Task ReorderItemsAsyncPublic(IReadOnlyList<ITreeItem> items, int startIndex) => await this.ReorderItemsAsync(items, startIndex).ConfigureAwait(false);

    /// <inheritdoc cref="DynamicTreeViewModel.SelectionModel"/>
    public SelectionModel<ITreeItem>? GetSelectionModel() => this.SelectionModel;

    /// <summary>
    /// Forwards to the production <c>SelectItem</c> command; Ctrl is implied in multiple-selection mode when
    /// something is already selected, so successive calls accumulate the way a Ctrl-click sequence does.
    /// </summary>
    /// <param name="item">The tree item to select.</param>
    /// <param name="origin">The origin of the selection request.</param>
    public void SelectItem(ITreeItem item, RequestOrigin origin = RequestOrigin.PointerInput)
    {
        var isCtrl = this.SelectionMode == SelectionMode.Multiple && this.SelectionModel?.SelectedItem is not null;
        this.SelectItemCommand.Execute(new ItemSelectionArgs(item, origin, IsCtrlKeyDown: isCtrl, IsShiftKeyDown: false));
    }

    /// <summary>
    /// Replaces the selection with <paramref name="item" /> through the selection model, bypassing the view model's
    /// interactive entry points (used to arrange state).
    /// </summary>
    /// <param name="item">The tree item to select exclusively.</param>
    public void ClearAndSelectItem(ITreeItem item) => this.SelectionModel?.ClearAndSelectItem(item);

    public bool TryGetFocusedItem([System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out ITreeItem? item, out RequestOrigin origin)
    {
        var fi = this.FocusedItem;
        if (fi is null)
        {
            item = null;
            origin = default;
            return false;
        }

        item = fi.Item;
        origin = fi.Origin;
        return true;
    }

    public new bool FocusItem(ITreeItem item, RequestOrigin origin = RequestOrigin.Programmatic)
        => base.FocusItem(item, origin);

    // Parameterless overloads preserve the current focused origin (if any), matching
    // how the view expects keyboard-triggered focus changes to retain origin.
    public new bool FocusNextVisibleItem(RequestOrigin origin) => base.FocusNextVisibleItem(origin);

    public new bool FocusPreviousVisibleItem(RequestOrigin origin) => base.FocusPreviousVisibleItem(origin);

    public new bool FocusFirstVisibleItemInParent(RequestOrigin origin) => base.FocusFirstVisibleItemInParent(origin);

    public new bool FocusLastVisibleItemInParent(RequestOrigin origin) => base.FocusLastVisibleItemInParent(origin);

    public new bool FocusFirstVisibleItemInTree(RequestOrigin origin) => base.FocusFirstVisibleItemInTree(origin);

    public new bool FocusLastVisibleItemInTree(RequestOrigin origin) => base.FocusLastVisibleItemInTree(origin);

    /// <inheritdoc cref="DynamicTreeViewModel.RemoveItemAsync"/>
    public async Task RemoveItemAsyncPublic(ITreeItem item, bool updateSelection = true) => await this.RemoveItemAsync(item, updateSelection).ConfigureAwait(false);

    /// <inheritdoc cref="DynamicTreeViewModel.RemoveSelectedItems"/>
    public async Task RemoveSelectedItemsAsyncPublic() => await this.RemoveSelectedItems().ConfigureAwait(false);

    /// <inheritdoc cref="DynamicTreeViewModel.StageClipboard"/>
    public void StageClipboardPublic(IReadOnlyList<ITreeItem> items, bool cut) => this.StageClipboard(items, cut);

    /// <inheritdoc cref="DynamicTreeViewModel.ReleaseClipboardRows"/>
    public void ReleaseClipboardRowsPublic() => this.ReleaseClipboardRows();

    /// <inheritdoc cref="DynamicTreeViewModel.ClearClipboard"/>
    public void ClearClipboardPublic() => this.ClearClipboard();
}
