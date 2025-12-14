// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel")]
public class ViewModelItemRemovalTests : ViewModelTestBase
{
    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
    public async Task RemoveItem_WhenItemIsLocked_ThrowsInvalidOperation()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var item = new TestTreeItemAdapter { Label = "Item", IsLocked = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.RemoveItemAsyncPublic(item).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
    public async Task RemoveItem_WhenItemIsOrphan_ThrowsInvalidOperation()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Single };
        var childItem = new TestTreeItemAdapter { Label = "Child" };
        var item = new TestTreeItemAdapter([childItem]) { Label = "Item", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };
        var orphanItem = new TestTreeItemAdapter { Label = "Orphan" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        viewModel.SelectItem(childItem, RequestOrigin.PointerInput);

        // Act
        var act = async () => await viewModel.RemoveItemAsyncPublic(orphanItem).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
    public async Task RemoveItem_WhenItemIsNotShown_ShouldRemoveItemFromParent()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var item = new TestTreeItemAdapter { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        await viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem]);

        // Act
        await viewModel.RemoveItemAsyncPublic(item).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem]);
        _ = rootItem.ChildrenCount.Should().Be(0);
        _ = item.Parent.Should().BeNull();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
    public async Task RemoveItem_WhenItemIsNotShown_ShouldRemoveItemAndChildrenFromParent()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Single };
        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild" };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var item = new TestTreeItemAdapter([childItem]) { Label = "Item", IsExpanded = true };
        var anotherItem = new TestTreeItemAdapter { Label = "AnotherItem" };
        var rootItem = new TestTreeItemAdapter([item, anotherItem], isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        await viewModel.CollapseItemAsync(item).ConfigureAwait(false);
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem, item, anotherItem]);

        // Act
        await viewModel.RemoveItemAsyncPublic(childItem).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem, item, anotherItem]);
        _ = childItem.ChildrenCount.Should().Be(0);
        _ = item.ChildrenCount.Should().Be(0);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
    public async Task RemoveItem_WhenItemIsShown_ShouldRemoveItemFromShownItems()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var item = new TestTreeItemAdapter { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem, item]);

        // Act
        await viewModel.RemoveItemAsyncPublic(item).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem]);
        _ = rootItem.ChildrenCount.Should().Be(0);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
    public async Task RemoveItem_WhenItemIsShown_ShouldRemoveItemsAndChildrenFromShownItems()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Single };
        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild" };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var item = new TestTreeItemAdapter([childItem]) { Label = "Item", IsExpanded = true };
        var anotherItem = new TestTreeItemAdapter { Label = "AnotherItem" };
        var rootItem = new TestTreeItemAdapter([item, anotherItem], isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.RemoveItemAsyncPublic(childItem).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem, item, anotherItem]);
        _ = childItem.ChildrenCount.Should().Be(0);
        _ = item.ChildrenCount.Should().Be(0);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task RemoveItem_ShouldFireItemBeingRemovedEventAndProceedBasedOnEventArgs(bool hideItem)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var item = new TestTreeItemAdapter { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        TreeItemBeingRemovedEventArgs? eventArgs = null;
        viewModel.ItemBeingRemoved += (_, args) =>
        {
            eventArgs = args;
            args.Proceed = false;
        };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        if (hideItem)
        {
            await viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);
        }

        // Act
        await viewModel.RemoveItemAsyncPublic(item).ConfigureAwait(false);

        // Assert
        _ = eventArgs.Should().BeEquivalentTo(new { TreeItem = item }, options => options.ExcludingMissingMembers());
        _ = rootItem.ChildrenCount.Should().Be(1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task RemoveItem_ShouldFireItemRemovedEventAfterRemoval(bool hideItem)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var item = new TestTreeItemAdapter { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        TreeItemRemovedEventArgs? eventArgs = null;
        viewModel.ItemRemoved += (_, args) => eventArgs = args;

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        if (hideItem)
        {
            await viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);
        }

        // Act
        await viewModel.RemoveItemAsyncPublic(item).ConfigureAwait(false);

        // Assert
        _ = eventArgs.Should().BeEquivalentTo(new { Parent = rootItem, TreeItem = item, RelativeIndex = 0 });
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task Remove_WhenTreeIsLeftEmpty_ShouldWork(SelectionMode mode)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: true) { SelectionMode = mode };
        var item = new TestTreeItemAdapter() { Label = "Item1", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        viewModel.SelectItem(item, RequestOrigin.PointerInput);

        // Act
        await viewModel.RemoveItemAsyncPublic(item).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEmpty();
        _ = viewModel.GetSelectionModel()!.IsEmpty.Should().BeTrue();
        _ = viewModel.GetSelectionModel()!.SelectedIndex.Should().Be(-1);
    }
}
