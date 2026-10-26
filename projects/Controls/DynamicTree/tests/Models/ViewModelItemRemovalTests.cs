// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Controls.Selection;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Remove")]
public class ViewModelItemRemovalTests : ViewModelTestBase
{
    [TestMethod]
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
    public async Task RemoveItem_WhenItemIsOrphan_ThrowsInvalidOperation()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var item = new TestTreeItemAdapter { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };
        var orphanItem = new TestTreeItemAdapter { Label = "Orphan" };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.RemoveItemAsyncPublic(orphanItem).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task RemoveItem_WhenRemovingRoot_ThrowsInvalidOperation()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var rootItem = new TestTreeItemAdapter(isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.RemoveItemAsyncPublic(rootItem).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(true, DisplayName = "Shown")]
    [DataRow(false, DisplayName = "Not shown")]
    public async Task RemoveItem_DetachesItemFromParent(bool isShown)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var item = new TestTreeItemAdapter { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        if (!isShown)
        {
            await viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);
        }

        // Act
        await viewModel.RemoveItemAsyncPublic(item).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem]);
        _ = rootItem.ChildrenCount.Should().Be(0);
        _ = item.Parent.Should().BeNull();
    }

    [TestMethod]
    [DataRow(true, DisplayName = "Shown")]
    [DataRow(false, DisplayName = "Not shown")]
    public async Task RemoveItem_DetachesTheWholeSubtree(bool isShown)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild" };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var item = new TestTreeItemAdapter([childItem]) { Label = "Item", IsExpanded = true };
        var anotherItem = new TestTreeItemAdapter { Label = "AnotherItem" };
        var rootItem = new TestTreeItemAdapter([item, anotherItem], isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        if (!isShown)
        {
            await viewModel.CollapseItemAsync(item).ConfigureAwait(false);
        }

        // Act
        await viewModel.RemoveItemAsyncPublic(childItem).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem, item, anotherItem]);
        _ = item.ChildrenCount.Should().Be(0);
        _ = childItem.ChildrenCount.Should().Be(0);
        _ = childItem.Parent.Should().BeNull();
        _ = grandChildItem.Parent.Should().BeNull();
    }

    [TestMethod]
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
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task RemoveItem_WhenTreeIsLeftEmpty_ClearsSelection(SelectionMode mode)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: true) { SelectionMode = mode };
        var item = new TestTreeItemAdapter() { Label = "Item1", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        viewModel.SelectDisplayedItem(item, [item], isControlDown: false, isShiftDown: false);

        // Act
        await viewModel.RemoveItemAsyncPublic(item).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEmpty();
        _ = viewModel.GetSelectionModel()!.IsEmpty.Should().BeTrue();
        _ = viewModel.GetSelectionModel()!.SelectedIndex.Should().Be(-1);
    }

    [TestMethod]
    [DataRow(SelectionMode.Single, 1, "Item2", DisplayName = "Single: selected row is replaced by the row that takes its place")]
    [DataRow(SelectionMode.Multiple, 1, "Item2", DisplayName = "Multiple: selected row is replaced by the row that takes its place")]
    [DataRow(SelectionMode.Single, 2, "Root", DisplayName = "Single: removing the last row falls back to the first row")]
    [DataRow(SelectionMode.Multiple, 2, "Root", DisplayName = "Multiple: removing the last row falls back to the first row")]
    public async Task RemoveItem_WhenItemIsSelected_MovesSelection(SelectionMode mode, int removedNumber, string expectedLabel)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = mode };
        var items = CreateItems(2);
        var rootItem = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        var removed = items[removedNumber - 1];
        viewModel.SelectDisplayedItem(removed, [.. viewModel.ShownItems], isControlDown: false, isShiftDown: false);

        // Act
        await viewModel.RemoveItemAsyncPublic(removed).ConfigureAwait(false);

        // Assert
        _ = viewModel.SelectedItem!.Label.Should().Be(expectedLabel);
        _ = viewModel.SelectedItemsCount.Should().Be(1);
        _ = removed.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task RemoveItem_AboveTheSelectedRow_KeepsTheSelectionOnTheSameItem(SelectionMode mode)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = mode };
        var items = CreateItems(3);
        var rootItem = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        viewModel.SelectDisplayedItem(items[2], [.. viewModel.ShownItems], isControlDown: false, isShiftDown: false);

        // Act
        await viewModel.RemoveItemAsyncPublic(items[0]).ConfigureAwait(false);

        // Assert
        _ = viewModel.SelectedItem.Should().BeSameAs(items[2]);
        _ = viewModel.SelectedItemsCount.Should().Be(1);
        _ = items[2].IsSelected.Should().BeTrue();
        _ = items[1].IsSelected.Should().BeFalse();
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task RemoveItem_TheActiveItem_ForgetsIt(SelectionMode mode)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = mode };
        var items = CreateItems(2);
        var rootItem = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        viewModel.SelectDisplayedItem(items[0], [.. viewModel.ShownItems], isControlDown: false, isShiftDown: false);
        _ = viewModel.ActiveItem.Should().BeSameAs(items[0]);

        // Act
        await viewModel.RemoveItemAsyncPublic(items[0]).ConfigureAwait(false);

        // Assert
        _ = viewModel.ActiveItem.Should().NotBeSameAs(items[0]);
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task RemoveSelectedItems_WhenSelectionIsEmpty_DoesNothing(SelectionMode mode)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = mode };
        var items = CreateItems(1);
        var rootItem = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().ContainInOrder([rootItem, items[0]]);
        _ = viewModel.GetSelectionModel()!.IsEmpty.Should().BeTrue();
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task RemoveSelectedItems_RemovesTheSelectedItem(SelectionMode mode)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = mode };
        var items = CreateItems(1);
        var rootItem = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        viewModel.SelectDisplayedItem(items[0], [.. viewModel.ShownItems], isControlDown: false, isShiftDown: false);

        // Act
        await viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEquivalentTo([rootItem]);
        _ = viewModel.SelectedItem.Should().Be(rootItem);
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task RemoveSelectedItems_DoesNotRemoveLockedItem(SelectionMode mode)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = mode };
        var item = new TestTreeItemAdapter { Label = "Item", IsLocked = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        viewModel.SelectDisplayedItem(item, [.. viewModel.ShownItems], isControlDown: false, isShiftDown: false);

        // Act
        await viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().ContainInOrder([rootItem, item]);
    }

    [TestMethod]
    public async Task RemoveSelectedItems_InMultipleMode_RemovesOnlyTheUnlockedSelectedItems()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Multiple };
        var items = CreateItems(3);
        items[1].IsLocked = true;
        var rootItem = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        ((MultipleSelectionModel<ITreeItem>)viewModel.GetSelectionModel()!).SelectItemsAt(1, 2, 3);

        // Act
        await viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().ContainInOrder([rootItem, items[1]]);
        _ = viewModel.SelectedItem.Should().Be(rootItem);
        _ = viewModel.SelectedItemsCount.Should().Be(1);
    }

    [TestMethod]
    [DataRow(new[] { 1 }, "Item2", DisplayName = "First row")]
    [DataRow(new[] { 2, 3 }, "Item4", DisplayName = "Adjacent rows")]
    [DataRow(new[] { 1, 3 }, "Item4", DisplayName = "Non-adjacent rows")]
    [DataRow(new[] { 4 }, "Root", DisplayName = "Last row falls back to the first row")]
    [DataRow(new[] { 1, 2, 3, 4 }, "Root", DisplayName = "Every row falls back to the first row")]
    public async Task RemoveSelectedItems_InMultipleMode_SelectsTheRowThatTakesTheLastRemovedPlace(int[] removedNumbers, string expectedLabel)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Multiple };
        var items = CreateItems(4);
        var rootItem = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        ((MultipleSelectionModel<ITreeItem>)viewModel.GetSelectionModel()!).SelectItemsAt([.. removedNumbers]);

        // Act
        await viewModel.RemoveSelectedItemsAsyncPublic().ConfigureAwait(false);

        // Assert
        _ = viewModel.SelectedItem!.Label.Should().Be(expectedLabel);
        _ = viewModel.SelectedItemsCount.Should().Be(1);
        _ = viewModel.ShownItems.Count().Should().Be(5 - removedNumbers.Length);
    }

    private static TestTreeItemAdapter[] CreateItems(int count)
        => [.. Enumerable.Range(1, count).Select(n => new TestTreeItemAdapter { Label = $"Item{n}" })];
}
