// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel")]
public partial class ViewModelMoveTests : ViewModelTestBase
{
    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Move")]
    public async Task MoveItem_ShouldRelocateSubtreeUnderNewParent()
    {
        // Arrange
        var grandChild = new TestTreeItemAdapter { Label = "GrandChild" };
        var child = new TestTreeItemAdapter([grandChild]) { Label = "Child", IsExpanded = true };
        var sourceParent = new TestTreeItemAdapter([child]) { Label = "SourceParent", IsExpanded = true };
        var targetExisting = new TestTreeItemAdapter { Label = "TargetExisting" };
        var targetParent = new TestTreeItemAdapter([targetExisting]) { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: false);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        await viewModel.MoveItemAsyncPublic(child, targetParent, 0).ConfigureAwait(false);

        // Assert
        var targetChildren = await targetParent.Children.ConfigureAwait(false);
        _ = targetChildren.Should().ContainInOrder(child, targetExisting);
        _ = viewModel.ShownItems.Should().ContainInOrder(root, sourceParent, targetParent, child, grandChild, targetExisting);
        _ = child.Parent.Should().Be(targetParent);
        _ = child.Depth.Should().Be(targetParent.Depth + 1);
        _ = grandChild.Parent.Should().Be(child);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Move")]
    public async Task MoveItems_ShouldPreserveOrderAndRestoreSelection()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter { Label = "Item1" };
        var item2 = new TestTreeItemAdapter { Label = "Item2" };
        var sourceParent = new TestTreeItemAdapter([item1, item2]) { Label = "SourceParent", IsExpanded = true };
        var targetExisting = new TestTreeItemAdapter { Label = "TargetExisting" };
        var targetParent = new TestTreeItemAdapter([targetExisting]) { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Multiple };

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.SelectItem(item1);
        viewModel.SelectItem(item2);

        // Act
        await viewModel.MoveItemsAsyncPublic([item1, item2], targetParent, 1).ConfigureAwait(false);

        // Assert
        var sm = viewModel.GetSelectionModel();
        _ = sm.Should().NotBeNull();
        _ = sm.SelectedItem.Should().Be(item1);
        _ = sm.SelectedIndex.Should().Be(4);
        var targetChildren = await targetParent.Children.ConfigureAwait(false);
        _ = targetChildren.Should().ContainInOrder(targetExisting, item1, item2);
        _ = viewModel.ShownItems.Should().ContainInOrder(root, sourceParent, targetParent, targetExisting, item1, item2);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Reorder")]
    public async Task ReorderItems_ShouldAdjustStartIndexWhenMovingWithinSameParent()
    {
        // Arrange
        var itemA = new TestTreeItemAdapter { Label = "A" };
        var itemB = new TestTreeItemAdapter { Label = "B" };
        var itemC = new TestTreeItemAdapter { Label = "C" };
        var itemD = new TestTreeItemAdapter { Label = "D" };
        var parent = new TestTreeItemAdapter([itemA, itemB, itemC, itemD]) { Label = "Parent", IsExpanded = true };
        var root = new TestTreeItemAdapter([parent], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: false);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        await viewModel.ReorderItemsAsyncPublic([itemA, itemB], 4).ConfigureAwait(false);

        // Assert
        var children = await parent.Children.ConfigureAwait(false);
        _ = children.Should().ContainInOrder(itemC, itemD, itemA, itemB);
        _ = viewModel.ShownItems.Should().ContainInOrder(root, parent, itemC, itemD, itemA, itemB);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Move")]
    public async Task MoveItem_WhenHandlerVetoesMove_ShouldNotThrowAndNotMove()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var sourceParent = new TestTreeItemAdapter([item]) { Label = "SourceParent", IsExpanded = true };
        var targetParent = new TestTreeItemAdapter { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: false);

        viewModel.ItemBeingMoved += (_, args) =>
        {
            args.Proceed = false;
            args.VetoReason = "blocked";
        };

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.MoveItemAsyncPublic(item, targetParent, 0).ConfigureAwait(false);

        // Assert: should not throw
        _ = await act.Should().NotThrowAsync().ConfigureAwait(false);

        // and the item must still belong to the original source parent
        _ = item.Parent.Should().Be(sourceParent);
        var children = await sourceParent.Children.ConfigureAwait(false);
        _ = children.Should().Contain(item);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Move")]
    public async Task MoveItem_WhenTargetExceedsMaxDepth_ShouldThrow()
    {
        // Arrange
        var deepest = new TestTreeItemAdapter { Label = "Node32", IsExpanded = true };
        var head = deepest;
        for (var i = 31; i >= 1; i--)
        {
            head = new TestTreeItemAdapter([head])
            {
                Label = string.Create(CultureInfo.InvariantCulture, $"Node{i}"),
                IsExpanded = true,
            };
        }

        var root = new TestTreeItemAdapter([head], isRoot: true) { Label = "Root", IsExpanded = true };

        var movableParent = new TestTreeItemAdapter { Label = "MovableParent", IsExpanded = true };
        var movable = new TestTreeItemAdapter { Label = "Movable" };
        head.AddChild(movableParent);
        movableParent.AddChild(movable);

        var viewModel = new TestViewModel(skipRoot: false);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.MoveItemAsyncPublic(movable, deepest, 0).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().WithMessage("*maximum depth*").ConfigureAwait(false);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Move")]
    public async Task MoveItem_WhenTargetCannotAcceptChildren_ShouldThrow()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var sourceParent = new TestTreeItemAdapter([item]) { Label = "SourceParent", IsExpanded = true };
        var targetParent = new NonAcceptingTreeItemAdapter { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: false);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.MoveItemAsyncPublic(item, targetParent, 0).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().WithMessage("*does not accept children*").ConfigureAwait(false);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Move")]
    public async Task MoveItems_WhenFirstItemSelected_ShouldRestoreSelection()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter { Label = "Item1" };
        var item2 = new TestTreeItemAdapter { Label = "Item2" };
        var sourceParent = new TestTreeItemAdapter([item1, item2]) { Label = "SourceParent", IsExpanded = true };
        var targetParent = new TestTreeItemAdapter { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Multiple };

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.SelectItem(item1);

        // Act
        await viewModel.MoveItemsAsyncPublic([item1, item2], targetParent, 0).ConfigureAwait(false);

        // Assert
        var sm = viewModel.GetSelectionModel();
        _ = sm.Should().NotBeNull();
        _ = sm.SelectedItem.Should().Be(item1);
        _ = sm.SelectedIndex.Should().Be(3);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Move")]
    public async Task MoveItems_WhenFirstItemNotSelected_ShouldClearSelection()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter { Label = "Item1" };
        var item2 = new TestTreeItemAdapter { Label = "Item2" };
        var sourceParent = new TestTreeItemAdapter([item1, item2]) { Label = "SourceParent", IsExpanded = true };
        var targetParent = new TestTreeItemAdapter { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Multiple };

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.SelectItem(item2);

        // Act
        await viewModel.MoveItemsAsyncPublic([item1, item2], targetParent, 0).ConfigureAwait(false);

        // Assert
        var sm = viewModel.GetSelectionModel();
        _ = sm.Should().NotBeNull();
        _ = sm.IsEmpty.Should().BeTrue();
        _ = sm.SelectedIndex.Should().Be(-1);
        _ = sm.SelectedItem.Should().BeNull();
    }

    private sealed partial class NonAcceptingTreeItemAdapter : TestTreeItemAdapter
    {
        public override bool CanAcceptChildren => false;
    }
}
