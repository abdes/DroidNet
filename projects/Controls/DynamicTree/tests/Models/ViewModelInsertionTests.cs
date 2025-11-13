// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel")]
public class ViewModelInsertionTests
{
    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
    public async Task InsertItem_ShouldValidateRelativeIndex()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false);
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.InsertItemAsyncPublic(-1, parentItem, newItem).ConfigureAwait(false);

        // Assert
        _ = await act.Should().NotThrowAsync().ConfigureAwait(false);
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem, newItem);
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
    public async Task InsertItem_ShouldExpandParentItemBeforeInsertion()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false);
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = false };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(0, parentItem, newItem).ConfigureAwait(false);

        // Assert
        _ = parentItem.IsExpanded.Should().BeTrue();
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem, newItem);
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
    public async Task InsertItem_ShouldClearSelectionBeforeInsertionAndSelectNewItem()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Single };
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        viewModel.SelectItem(parentItem);
        _ = viewModel.GetSelectionModel()?.SelectedItem.Should().Be(parentItem);

        // Act
        await viewModel.InsertItemAsyncPublic(0, parentItem, newItem).ConfigureAwait(false);

        // Assert
        _ = viewModel.GetSelectionModel()?.SelectedItem.Should().Be(newItem);
        _ = parentItem.IsSelected.Should().BeFalse();
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem, newItem);
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
    public async Task InsertItem_ShouldInsertItemAtCorrectIndexInShownItemsCollection()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false);
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(0, parentItem, newItem).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem, newItem);
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }

    [TestMethod]
    public async Task InsertItem_ShouldHandleRelativeIndexGreaterThanChildrenCount()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false);
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(10, parentItem, newItem).ConfigureAwait(false);

        // Assert
        var children = await parentItem.Children.ConfigureAwait(false);
        _ = children.Should().Contain(newItem);
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem, newItem);
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
    public async Task InsertItem_ShouldHandleRelativeIndexEqualToChildrenCountMinusOne()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false);
        var childItem1 = new TestTreeItemAdapter { Label = "Child1", IsExpanded = true };
        var childItem2 = new TestTreeItemAdapter { Label = "Child2", IsExpanded = true };
        var parentItem = new TestTreeItemAdapter([childItem1, childItem2]) { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(1, parentItem, newItem).ConfigureAwait(false);

        // Assert
        var children = await parentItem.Children.ConfigureAwait(false);
        _ = children.Should().ContainInOrder(childItem1, newItem, childItem2);
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem, childItem1, newItem, childItem2);
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
    public async Task InsertItem_ShouldHandleRelativeIndexInBetweenChildren()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false);
        var childItem1 = new TestTreeItemAdapter { Label = "Child1", IsExpanded = true };
        var childItem2 = new TestTreeItemAdapter { Label = "Child2", IsExpanded = true };
        var childItem3 = new TestTreeItemAdapter { Label = "Child3", IsExpanded = true };
        var parentItem = new TestTreeItemAdapter([childItem1, childItem2, childItem3]) { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(1, parentItem, newItem).ConfigureAwait(false);

        // Assert
        var children = await parentItem.Children.ConfigureAwait(false);
        _ = children.Should().ContainInOrder(childItem1, newItem, childItem2, childItem3);
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem, childItem1, newItem, childItem2, childItem3);
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }

    [TestMethod]
    [ExcludeFromCodeCoverage]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
    public async Task InsertItem_ShouldHandleInsertingAsLastChildWithExpandedChildHavingChildren()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false);
        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild", IsExpanded = true };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var parentItem = new TestTreeItemAdapter([childItem]) { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(1, parentItem, newItem).ConfigureAwait(false);

        // Assert
        var children = await parentItem.Children.ConfigureAwait(false);
        _ = children.Should().ContainInOrder(childItem, newItem);
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem, childItem, grandChildItem, newItem);
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
    public async Task InsertItem_ShouldFireItemBeingAddedEventAndProceedBasedOnEventArgs()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false);
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        TreeItemBeingAddedEventArgs? eventArgs = null;
        viewModel.ItemBeingAdded += (_, args) =>
        {
            eventArgs = args;
            args.Proceed = false; // Prevent the addition
        };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(0, parentItem, newItem).ConfigureAwait(false);

        // Assert
        _ = eventArgs.Should().BeEquivalentTo(new { Parent = parentItem, TreeItem = newItem }, options => options.ExcludingMissingMembers());
        _ = parentItem.ChildrenCount.Should().Be(0); // Ensure the item was not added
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
    public async Task InsertItem_ShouldFireItemAddedEventAfterInsertion()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: false);
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        TreeItemAddedEventArgs? eventArgs = null;
        viewModel.ItemAdded += (_, args) => eventArgs = args;

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(0, parentItem, newItem).ConfigureAwait(false);

        // Assert
        _ = eventArgs.Should().BeEquivalentTo(new { Parent = parentItem, TreeItem = newItem, RelativeIndex = 0 });
        _ = viewModel.ShownItems.Should().ContainInOrder(rootItem, parentItem, newItem);
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }
}
