// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Insertion")]
public class ViewModelInsertionTests : ViewModelTestBase
{
    [TestMethod]
    [DataRow(-1, "New,A,B")]
    [DataRow(0, "New,A,B")]
    [DataRow(1, "A,New,B")]
    [DataRow(2, "A,B,New")]
    [DataRow(10, "A,B,New")]
    public async Task InsertItem_PlacesItemAtTheClampedRelativeIndex(int relativeIndex, string expectedOrder)
    {
        // Arrange: a relative index below zero clamps to the start, one beyond the child count clamps to the end.
        using var viewModel = new TestViewModel(skipRoot: false);
        var childA = new TestTreeItemAdapter { Label = "A" };
        var childB = new TestTreeItemAdapter { Label = "B" };
        var parentItem = new TestTreeItemAdapter([childA, childB]) { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "New" };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(newItem, parentItem, relativeIndex).ConfigureAwait(false);

        // Assert
        var expected = expectedOrder.Split(',');
        var children = await parentItem.Children.ConfigureAwait(false);
        _ = children.Select(c => c.Label).Should().Equal(expected);
        _ = viewModel.ShownItems.Select(i => i.Label).Should().Equal(new[] { "Root", "Parent" }.Concat(expected));
        _ = newItem.Depth.Should().Be(parentItem.Depth + 1);
    }

    [TestMethod]
    public async Task InsertItem_AsLastChildAfterExpandedSiblingWithChildren_IsShownAfterTheSiblingsDescendants()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild", IsExpanded = true };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var parentItem = new TestTreeItemAdapter([childItem]) { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(newItem, parentItem, 1).ConfigureAwait(false);

        // Assert
        var children = await parentItem.Children.ConfigureAwait(false);
        _ = children.Should().Equal(childItem, newItem);
        _ = viewModel.ShownItems.Should().Equal(rootItem, parentItem, childItem, grandChildItem, newItem);
    }

    [TestMethod]
    public async Task InsertItem_ShouldExpandParentItemBeforeInsertion()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = false };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(newItem, parentItem, 0).ConfigureAwait(false);

        // Assert
        _ = parentItem.IsExpanded.Should().BeTrue();
        _ = viewModel.ShownItems.Should().Equal(rootItem, parentItem, newItem);
    }

    [TestMethod]
    public async Task InsertItem_ShouldClearSelectionBeforeInsertionAndSelectNewItem()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Single };
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        viewModel.SelectItem(parentItem, RequestOrigin.PointerInput);
        _ = viewModel.SelectedItem.Should().Be(parentItem);

        // Act
        await viewModel.InsertItemAsyncPublic(newItem, parentItem, 0).ConfigureAwait(false);

        // Assert
        _ = viewModel.SelectedItem.Should().Be(newItem);
        _ = parentItem.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public async Task InsertItem_ShouldFireItemBeingAddedEventAndProceedBasedOnEventArgs()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
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
        await viewModel.InsertItemAsyncPublic(newItem, parentItem, 0).ConfigureAwait(false);

        // Assert
        _ = eventArgs.Should().BeEquivalentTo(new { Parent = parentItem, TreeItem = newItem }, options => options.ExcludingMissingMembers());
        _ = parentItem.ChildrenCount.Should().Be(0); // Ensure the item was not added
        _ = viewModel.ShownItems.Should().Equal(rootItem, parentItem);
    }

    [TestMethod]
    public async Task InsertItem_UnderAnExpandedParentInACollapsedBranch_AddsNoRowUntilTheBranchExpands()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var inner = new TestTreeItemAdapter { Label = "Inner", IsExpanded = true };
        var branch = new TestTreeItemAdapter([inner]) { Label = "Branch" };
        var rootItem = new TestTreeItemAdapter([branch], isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        // Act
        await viewModel.InsertItemAsyncPublic(newItem, inner, 0).ConfigureAwait(false);

        // Assert
        _ = newItem.Parent.Should().BeSameAs(inner);
        _ = viewModel.ShownItems.Should().Equal(rootItem, branch);

        await viewModel.ExpandItemAsync(branch).ConfigureAwait(false);
        _ = viewModel.ShownItems.Should().Equal(rootItem, branch, inner, newItem);
    }

    [TestMethod]
    public async Task InsertItem_ShouldFireItemAddedEventAfterInsertion()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var parentItem = new TestTreeItemAdapter { Label = "Parent", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        var newItem = new TestTreeItemAdapter { Label = "NewItem" };

        TreeItemAddedEventArgs? eventArgs = null;
        viewModel.ItemAdded += (_, args) => eventArgs = args;

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.InsertItemAsyncPublic(newItem, parentItem, 0).ConfigureAwait(false);

        // Assert
        _ = eventArgs.Should().BeEquivalentTo(new { Parent = parentItem, TreeItem = newItem, RelativeIndex = 0 });
    }
}
