// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using AwesomeAssertions;
using DroidNet.Controls.Selection;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Move")]
public partial class ViewModelMoveTests : ViewModelTestBase
{
    [TestMethod]
    public async Task MoveItem_ShouldRelocateSubtreeUnderNewParent()
    {
        // Arrange
        var grandChild = new TestTreeItemAdapter { Label = "GrandChild" };
        var child = new TestTreeItemAdapter([grandChild]) { Label = "Child", IsExpanded = true };
        var sourceParent = new TestTreeItemAdapter([child]) { Label = "SourceParent", IsExpanded = true };
        var targetExisting = new TestTreeItemAdapter { Label = "TargetExisting" };
        var targetParent = new TestTreeItemAdapter([targetExisting]) { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        await viewModel.MoveItemAsyncPublic(child, targetParent, 0).ConfigureAwait(false);

        // Assert
        var targetChildren = await targetParent.Children.ConfigureAwait(false);
        _ = targetChildren.Should().Equal(child, targetExisting);
        _ = viewModel.ShownItems.Should().Equal(root, sourceParent, targetParent, child, grandChild, targetExisting);
        _ = child.Parent.Should().Be(targetParent);
        _ = child.Depth.Should().Be(targetParent.Depth + 1);
        _ = grandChild.Parent.Should().Be(child);
    }

    [TestMethod]
    public async Task MoveItems_ShouldPreserveOrderAndKeepTheFirstMovedItemSelected()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter { Label = "Item1" };
        var item2 = new TestTreeItemAdapter { Label = "Item2" };
        var sourceParent = new TestTreeItemAdapter([item1, item2]) { Label = "SourceParent", IsExpanded = true };
        var targetExisting = new TestTreeItemAdapter { Label = "TargetExisting" };
        var targetParent = new TestTreeItemAdapter([targetExisting]) { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Multiple };

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.SelectItem(item1, RequestOrigin.PointerInput);
        viewModel.SelectItem(item2, RequestOrigin.PointerInput);

        // Act
        await viewModel.MoveItemsAsyncPublic([item1, item2], targetParent, 1).ConfigureAwait(false);

        // Assert
        var targetChildren = await targetParent.Children.ConfigureAwait(false);
        _ = targetChildren.Should().Equal(targetExisting, item1, item2);
        _ = viewModel.ShownItems.Should().Equal(root, sourceParent, targetParent, targetExisting, item1, item2);
        var selection = (MultipleSelectionModel<ITreeItem>)viewModel.GetSelectionModel()!;
        _ = selection.SelectedItem.Should().Be(item1);
        _ = selection.SelectedItems.Should().BeEquivalentTo([item1, item2]);
    }

    [TestMethod]
    [DataRow("Item1", "Item1", "Item1")]
    [DataRow("Item1,Item2", "Item1,Item2", "Item1")]
    [DataRow("Item2", "", "")]
    public async Task MoveItems_RestoresTheSelectionOnlyWhenTheFirstMovedItemWasSelected(string selectedLabels, string expectedSelectedLabels, string expectedPrimaryLabel)
    {
        // Arrange
        var item1 = new TestTreeItemAdapter { Label = "Item1" };
        var item2 = new TestTreeItemAdapter { Label = "Item2" };
        var sourceParent = new TestTreeItemAdapter([item1, item2]) { Label = "SourceParent", IsExpanded = true };
        var targetParent = new TestTreeItemAdapter { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = SelectionMode.Multiple };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        foreach (var label in selectedLabels.Split(','))
        {
            viewModel.SelectItem(string.Equals(label, "Item1", StringComparison.Ordinal) ? item1 : item2, RequestOrigin.PointerInput);
        }

        // Act
        await viewModel.MoveItemsAsyncPublic([item1, item2], targetParent, 0).ConfigureAwait(false);

        // Assert
        var selection = (MultipleSelectionModel<ITreeItem>)viewModel.GetSelectionModel()!;
        _ = selection.SelectedItems.Select(i => i.Label).Should().BeEquivalentTo(expectedSelectedLabels.Split(',', StringSplitOptions.RemoveEmptyEntries));
        _ = (selection.SelectedItem?.Label ?? string.Empty).Should().Be(expectedPrimaryLabel);
        _ = selection.IsEmpty.Should().Be(expectedSelectedLabels.Length == 0);
    }

    [TestMethod]
    public async Task MoveItem_ShouldRaiseItemMovedWithPreviousAndNewIndices()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var other = new TestTreeItemAdapter { Label = "Other" };
        var sourceParent = new TestTreeItemAdapter([item, other]) { Label = "SourceParent", IsExpanded = true };
        var targetExisting = new TestTreeItemAdapter { Label = "TargetExisting" };
        var targetParent = new TestTreeItemAdapter([targetExisting]) { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false);
        TreeItemsMovedEventArgs? movedArgs = null;
        viewModel.ItemMoved += (_, args) => movedArgs = args;
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        await viewModel.MoveItemAsyncPublic(item, targetParent, 1).ConfigureAwait(false);

        // Assert
        _ = movedArgs.Should().NotBeNull();
        _ = movedArgs!.IsBatch.Should().BeFalse();
        _ = movedArgs.PrimaryMove.Should().Be(new MovedItemInfo(item, sourceParent, targetParent, PreviousIndex: 0, NewIndex: 1));
    }

    [TestMethod]
    public async Task ReorderItems_ShouldAdjustStartIndexWhenMovingWithinSameParent()
    {
        // Arrange
        var itemA = new TestTreeItemAdapter { Label = "A" };
        var itemB = new TestTreeItemAdapter { Label = "B" };
        var itemC = new TestTreeItemAdapter { Label = "C" };
        var itemD = new TestTreeItemAdapter { Label = "D" };
        var parent = new TestTreeItemAdapter([itemA, itemB, itemC, itemD]) { Label = "Parent", IsExpanded = true };
        var root = new TestTreeItemAdapter([parent], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        await viewModel.ReorderItemsAsyncPublic([itemA, itemB], 4).ConfigureAwait(false);

        // Assert
        var children = await parent.Children.ConfigureAwait(false);
        _ = children.Should().Equal(itemC, itemD, itemA, itemB);
        _ = viewModel.ShownItems.Should().Equal(root, parent, itemC, itemD, itemA, itemB);
    }

    [TestMethod]
    public async Task MoveItem_WhenHandlerVetoesMove_ShouldNotThrowAndNotMove()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var sourceParent = new TestTreeItemAdapter([item]) { Label = "SourceParent", IsExpanded = true };
        var targetParent = new TestTreeItemAdapter { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false);

        viewModel.ItemBeingMoved += (_, args) =>
        {
            args.Proceed = false;
            args.VetoReason = "blocked";
        };

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.MoveItemAsyncPublic(item, targetParent, 0).ConfigureAwait(false);

        // Assert: should not throw, and the item must still belong to the original source parent
        _ = await act.Should().NotThrowAsync().ConfigureAwait(false);
        _ = item.Parent.Should().Be(sourceParent);
        var children = await sourceParent.Children.ConfigureAwait(false);
        _ = children.Should().Contain(item);
    }

    [TestMethod]
    public async Task MoveItem_WhenTargetExceedsMaxDepth_ShouldThrow()
    {
        // Arrange: a 32-level chain; the movable item lives next to the chain's second node.
        var movable = new TestTreeItemAdapter { Label = "Movable" };
        var movableParent = new TestTreeItemAdapter([movable]) { Label = "MovableParent", IsExpanded = true };
        var head = CreateChain(32, movableParent, out var deepest);
        var root = new TestTreeItemAdapter([head], isRoot: true) { Label = "Root", IsExpanded = true };

        using var viewModel = new TestViewModel(skipRoot: false);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.MoveItemAsyncPublic(movable, deepest, 0).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task MoveItem_WhenTargetCannotAcceptChildren_ShouldThrow()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var sourceParent = new TestTreeItemAdapter([item]) { Label = "SourceParent", IsExpanded = true };
        var targetParent = new NonAcceptingTreeItemAdapter { Label = "TargetParent", IsExpanded = true };
        var root = new TestTreeItemAdapter([sourceParent, targetParent], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.MoveItemAsyncPublic(item, targetParent, 0).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task MoveItem_IntoItsOwnDescendant_ShouldThrowAndNotMove()
    {
        // Arrange
        var descendant = new TestTreeItemAdapter { Label = "Descendant" };
        var item = new TestTreeItemAdapter([descendant]) { Label = "Item", IsExpanded = true };
        var root = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.MoveItemAsyncPublic(item, descendant, 0).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        _ = item.Parent.Should().Be(root);
        _ = viewModel.ShownItems.Should().Equal(root, item, descendant);
    }

    private static TestTreeItemAdapter CreateChain(int length, TestTreeItemAdapter extraChildOfHead, out TestTreeItemAdapter deepest)
    {
        deepest = new TestTreeItemAdapter { Label = string.Create(CultureInfo.InvariantCulture, $"Node{length}"), IsExpanded = true };
        var head = deepest;
        for (var i = length - 1; i >= 1; i--)
        {
            List<TestTreeItemAdapter> children = i == 1 ? [head, extraChildOfHead] : [head];
            head = new TestTreeItemAdapter(children)
            {
                Label = string.Create(CultureInfo.InvariantCulture, $"Node{i}"),
                IsExpanded = true,
            };
        }

        return head;
    }

    private sealed partial class NonAcceptingTreeItemAdapter : TestTreeItemAdapter
    {
        public override bool CanAcceptChildren => false;
    }
}
