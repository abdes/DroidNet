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
internal partial class TestViewModel(bool skipRoot, ILoggerFactory? loggerFactory = null) :
    DynamicTreeViewModel(
        loggerFactory ?? Microsoft.Extensions.Logging.Abstractions.NullLoggerFactory.Instance)
{
    /// <inheritdoc cref="DynamicTreeViewModel.InitializeRootAsync"/>
    public async Task InitializeRootAsyncPublic(ITreeItem root) => await this.InitializeRootAsync(root, skipRoot).ConfigureAwait(false);

    /// <inheritdoc cref="DynamicTreeViewModel.InsertItemAsync"/>
    public async Task InsertItemAsyncPublic(int relativeIndex, ITreeItem parent, ITreeItem item) => await this.InsertItemAsync(relativeIndex, parent, item).ConfigureAwait(false);

    public async Task MoveItemAsyncPublic(ITreeItem item, ITreeItem newParent, int newIndex) => await this.MoveItemAsync(item, newParent, newIndex).ConfigureAwait(false);

    public async Task MoveItemsAsyncPublic(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex) => await this.MoveItemsAsync(items, newParent, startIndex).ConfigureAwait(false);

    public async Task ReorderItemAsyncPublic(ITreeItem item, int newIndex) => await this.ReorderItemAsync(item, newIndex).ConfigureAwait(false);

    public async Task ReorderItemsAsyncPublic(IReadOnlyList<ITreeItem> items, int startIndex) => await this.ReorderItemsAsync(items, startIndex).ConfigureAwait(false);

    /// <inheritdoc cref="DynamicTreeViewModel.SelectionModel"/>
    public SelectionModel<ITreeItem>? GetSelectionModel() => this.SelectionModel;

    /// <inheritdoc cref="DynamicTreeViewModel.RemoveItemAsync"/>
    public async Task RemoveItemAsyncPublic(ITreeItem item, bool updateSelection = true) => await this.RemoveItemAsync(item, updateSelection).ConfigureAwait(false);

    /// <inheritdoc cref="DynamicTreeViewModel.RemoveSelectedItems"/>
    public async Task RemoveSelectedItemsAsyncPublic() => await this.RemoveSelectedItems().ConfigureAwait(false);
}
