// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

public abstract partial class DynamicTreeViewModel
{
    private TreeDisplayHelper? displayHelper;

    private TreeDisplayHelper DisplayHelper => this.displayHelper ??=
        new TreeDisplayHelper(
            this.ShownItems,
            () => this.SelectionModel,
            this.ExpandItemAsync,
            new TreeDisplayEventCallbacks(
                args =>
                {
                    this.ItemBeingAdded?.Invoke(this, args);
                    return args.Proceed;
                },
                args => this.ItemAdded?.Invoke(this, args),
                args =>
                {
                    this.ItemBeingRemoved?.Invoke(this, args);
                    return args.Proceed;
                },
                args => this.ItemRemoved?.Invoke(this, args),
                args =>
                {
                    this.ItemBeingMoved?.Invoke(this, args);
                    return args.Proceed;
                },
                args => this.ItemMoved?.Invoke(this, args)),
            this.logger);

    public Task MoveItemAsync(ITreeItem item, ITreeItem newParent, int newIndex)
        => this.DisplayHelper.MoveItemAsync(item, newParent, newIndex);

    public Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
        => this.DisplayHelper.MoveItemsAsync(items, newParent, startIndex);

    public Task ReorderItemAsync(ITreeItem item, int newIndex)
        => this.DisplayHelper.ReorderItemAsync(item, newIndex);

    public Task ReorderItemsAsync(IReadOnlyList<ITreeItem> items, int startIndex)
        => this.DisplayHelper.ReorderItemsAsync(items, startIndex);
}
