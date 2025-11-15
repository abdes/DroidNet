// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Controls.Selection;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel")]
public class ViewModelMultipleSelectionRemovalTests
{
    private readonly TestViewModel viewModel = new(skipRoot: false) { SelectionMode = SelectionMode.Multiple };

    private MultipleSelectionModel<ITreeItem> Selection => (MultipleSelectionModel<ITreeItem>)this.viewModel.GetSelectionModel()!;

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove / MultipleSelection")]
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
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove / MultipleSelection")]
    public async Task RemoveSelectedItems_ShouldRemoveAllSelectedItemsThatAreNotLocked()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1", IsExpanded = true };
        var item2 = new TestTreeItemAdapter() { Label = "Item2", IsExpanded = true, IsLocked = true };
        var item3 = new TestTreeItemAdapter() { Label = "Item3", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item1, item2, item3], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.Selection.SelectItemsAt(1, 2, 3);

        // Act
        await this.viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().ContainInOrder([rootItem, item2]);
        _ = this.Selection.SelectedIndices.Should().ContainSingle().Which.Should().Be(0);
        _ = this.Selection.SelectedItem.Should().Be(rootItem);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove / MultipleSelection")]
    public async Task RemoveSelectedItems_ShouldUpdateSelectionToNewItemAfterRemoval()
    {
        // Arrange TODO: use DataRow and test all cases of selected items with deep tree structure
        var item1 = new TestTreeItemAdapter() { Label = "Item1", IsExpanded = true };
        var item2 = new TestTreeItemAdapter() { Label = "Item2", IsExpanded = true };
        var item3 = new TestTreeItemAdapter() { Label = "Item3", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item1, item2, item3], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.Selection.SelectItemsAt(1, 2);

        // Act
        await this.viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().ContainInOrder([rootItem, item3]);
        _ = this.Selection.SelectedIndices.Should().ContainSingle().Which.Should().Be(1);
        _ = this.Selection.SelectedItem.Should().Be(item3);
    }
}
