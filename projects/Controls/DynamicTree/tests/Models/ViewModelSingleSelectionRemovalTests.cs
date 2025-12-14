// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Controls.Selection;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel")]
public sealed partial class ViewModelSingleSelectionRemovalTests : ViewModelTestBase, IDisposable
{
    private readonly TestViewModel viewModel = new(skipRoot: false) { SelectionMode = SelectionMode.Single };

    private SelectionModel<ITreeItem> Selection => this.viewModel.GetSelectionModel()!;

    public void Dispose() => this.viewModel.Dispose();

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove / SingleSelection")]
    public async Task RemoveSelectedItems_ShouldDoNothingIfSelectionIsEmpty()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().ContainInOrder([rootItem, item]);
        _ = this.Selection.IsEmpty.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove / SingleSelection")]
    public async Task RemoveSelectedItems_ShouldRemoveSelectedItemIfNotLocked()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item, RequestOrigin.PointerInput);

        // Act
        await this.viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().Contain(rootItem);
        _ = this.viewModel.ShownItems.Should().NotContain(item);
        _ = this.Selection.SelectedItem.Should().Be(rootItem);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove / SingleSelection")]
    public async Task RemoveSelectedItems_ShouldNotRemoveItemWhenLocked()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item", IsExpanded = true, IsLocked = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item, RequestOrigin.PointerInput);

        // Act
        await this.viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().ContainInOrder([rootItem, item]);
        _ = this.Selection.SelectedItem.Should().Be(item);
    }
}
