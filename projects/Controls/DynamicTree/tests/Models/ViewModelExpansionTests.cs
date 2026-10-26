// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Controls.Selection;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Expansion")]
public sealed partial class ViewModelExpansionTests : ViewModelTestBase, IDisposable
{
    private TestViewModel viewModel = null!;

    public void Dispose() => this.viewModel.Dispose();

    [TestInitialize]
    public void Initialize() => this.viewModel = new TestViewModel(skipRoot: false, loggerFactory: this.LoggerFactoryInstance);

    [TestMethod]
    public async Task ExpandItemAsync_ShouldExpandItemAndShowItsChildren()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root" };
        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.ExpandItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = rootItem.IsExpanded.Should().BeTrue();
        _ = this.viewModel.ShownItems.Should().Equal(rootItem, item);
    }

    [TestMethod]
    public async Task ExpandItemAsync_WhenAlreadyExpanded_IsANoOp()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };
        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        var shownBefore = this.viewModel.ShownItems.ToArray();
        var shownChanges = CountShownItemsChanges(this.viewModel);

        // Act
        await this.viewModel.ExpandItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = shownChanges().Should().Be(0);
        _ = this.viewModel.ShownItems.Should().Equal(shownBefore);
        _ = rootItem.IsExpanded.Should().BeTrue();
        _ = item.IsExpanded.Should().BeTrue();
    }

    [TestMethod]
    public async Task ExpandItemAsync_WhenParentIsNotExpanded_Throws()
    {
        // Arrange
        var childItem = new TestTreeItemAdapter { Label = "Child", IsExpanded = false };
        var parentItem = new TestTreeItemAdapter([childItem]) { Label = "Parent", IsExpanded = false };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };
        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        var act = async () => await this.viewModel.ExpandItemAsync(childItem).ConfigureAwait(false);

        // Assert: the parent never expanded
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);

        // Assert: the parent expanded, then collapsed
        await this.viewModel.ExpandItemAsync(parentItem).ConfigureAwait(false);
        await this.viewModel.CollapseItemAsync(parentItem).ConfigureAwait(false);
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task ExpandItemAsync_ShouldRestoreExpandedChildrenOfItem()
    {
        // Arrange
        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild", IsExpanded = true };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([childItem], isRoot: true) { Label = "Root" };
        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.ExpandItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().Equal(rootItem, childItem, grandChildItem);
    }

    [TestMethod]
    public async Task CollapseItemAsync_ShouldCollapseItemAndHideItsDescendants()
    {
        // Arrange
        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild" };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([childItem], isRoot: true) { Label = "Root", IsExpanded = true };
        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = rootItem.IsExpanded.Should().BeFalse();
        _ = this.viewModel.ShownItems.Should().Equal(rootItem);
    }

    [TestMethod]
    public async Task CollapseItemAsync_WhenAlreadyCollapsed_IsANoOp()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = false };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = false };
        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        var shownBefore = this.viewModel.ShownItems.ToArray();
        var shownChanges = CountShownItemsChanges(this.viewModel);

        // Act
        await this.viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = shownChanges().Should().Be(0);
        _ = this.viewModel.ShownItems.Should().Equal(shownBefore);
        _ = rootItem.IsExpanded.Should().BeFalse();
        _ = item.IsExpanded.Should().BeFalse();
    }

    [TestMethod]
    public async Task CollapseItemAsync_AlreadyHiddenBranch_PreservesShownItems()
    {
        // Arrange
        var leaf = new TestTreeItemAdapter { Label = "Leaf" };
        var inner = new TestTreeItemAdapter([leaf]) { Label = "Inner", IsExpanded = true };
        var outer = new TestTreeItemAdapter([inner]) { Label = "Outer", IsExpanded = true };
        var sibling = new TestTreeItemAdapter { Label = "Sibling" };
        var root = new TestTreeItemAdapter([outer, sibling], isRoot: true) { Label = "Root", IsExpanded = true };
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await this.viewModel.CollapseItemAsync(outer).ConfigureAwait(false);
        var shown = this.viewModel.ShownItems.ToArray();

        // Act
        await this.viewModel.CollapseItemAsync(inner).ConfigureAwait(false);

        // Assert
        _ = inner.IsExpanded.Should().BeFalse();
        _ = this.viewModel.ShownItems.Should().Equal(shown);
        await this.viewModel.ExpandItemAsync(outer).ConfigureAwait(false);
        _ = this.viewModel.ShownItems.Should().Equal(root, outer, inner, sibling);
    }

    [TestMethod]
    public async Task CollapseAndReexpand_ShouldClearSelectionForChildren()
    {
        // Arrange
        this.viewModel.SelectionMode = SelectionMode.Multiple;

        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild" };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([childItem], isRoot: true) { Label = "Root", IsExpanded = true };
        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Select both the child and its grandchild
        var selection = (MultipleSelectionModel<ITreeItem>?)this.viewModel.GetSelectionModel();
        selection!.SelectItemsAt(this.viewModel.ShownIndexOf(childItem), this.viewModel.ShownIndexOf(grandChildItem));

        // Act: collapse, select the root while its descendants are hidden, then expand again.
        await this.viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);
        this.viewModel.ClearAndSelectItem(rootItem);
        await this.viewModel.ExpandItemAsync(rootItem).ConfigureAwait(false);

        // Assert: only the root remains selected; the re-shown descendants are not.
        _ = this.viewModel.ShownItems.Should().Equal(rootItem, childItem, grandChildItem);
        _ = selection.SelectedItems.Should().Equal(rootItem);
        _ = childItem.IsSelected.Should().BeFalse();
        _ = grandChildItem.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public async Task ExpandItemAsync_WhenARowBelowIsSelected_KeepsTheSelectionOnThatRow()
    {
        // Arrange
        this.viewModel.SelectionMode = SelectionMode.Multiple;
        var (root, branch, child, leaf) = CreateBranchAndLeaf(expandBranch: false);
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        this.viewModel.ClearAndSelectItem(leaf);

        // Act
        await this.viewModel.ExpandItemAsync(branch).ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().Equal(root, branch, child, leaf);
        _ = this.viewModel.SelectedItem.Should().BeSameAs(leaf);
        _ = this.viewModel.SelectedItemsCount.Should().Be(1);
        _ = leaf.IsSelected.Should().BeTrue();
        _ = child.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public async Task CollapseItemAsync_WhenARowBelowIsSelected_KeepsTheSelectionOnThatRowAndDeselectsHiddenRows()
    {
        // Arrange
        this.viewModel.SelectionMode = SelectionMode.Multiple;
        var (root, branch, child, leaf) = CreateBranchAndLeaf(expandBranch: true);
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var selection = (MultipleSelectionModel<ITreeItem>)this.viewModel.GetSelectionModel()!;
        selection.SelectItemsAt(this.viewModel.ShownIndexOf(child), this.viewModel.ShownIndexOf(leaf));

        // Act
        await this.viewModel.CollapseItemAsync(branch).ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().Equal(root, branch, leaf);
        _ = selection.SelectedItems.Should().Equal(leaf);
        _ = leaf.IsSelected.Should().BeTrue();
        _ = child.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public async Task CollapseItemAsync_WhenAHiddenDescendantWasActive_ForgetsTheActiveItemInOneSettledSelection()
    {
        // Arrange
        this.viewModel.SelectionMode = SelectionMode.Multiple;
        var (root, branch, child, _) = CreateBranchAndLeaf(expandBranch: true);
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        this.viewModel.SelectItem(child);
        var settled = 0;
        this.viewModel.SelectionSettled += (_, _) => settled++;

        // Act
        await this.viewModel.CollapseItemAsync(branch).ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ActiveItem.Should().BeNull();
        _ = settled.Should().Be(1);
    }

    [TestMethod]
    public async Task CollapseItemAsync_WhenFocusIsOnADescendant_MovesFocusToTheCollapsedItem()
    {
        // Arrange
        var (root, branch, child, _) = CreateBranchAndLeaf(expandBranch: true);
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = this.viewModel.FocusItem(child, RequestOrigin.KeyboardInput);

        // Act
        await this.viewModel.CollapseItemAsync(branch).ConfigureAwait(false);

        // Assert
        _ = this.viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().BeSameAs(branch);
    }

    [TestMethod]
    public async Task ExpandItemAsync_WhenTheItemIsNotShown_OnlyRecordsTheExpansion()
    {
        // Arrange
        var (root, branch, child, _) = CreateBranchAndLeaf(expandBranch: true);
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await this.viewModel.CollapseItemAsync(branch).ConfigureAwait(false);
        branch.IsExpanded = true; // recorded on the item alone, as a filter clear does
        var shownBefore = this.viewModel.ShownItems.ToArray();

        // Act
        await this.viewModel.ExpandItemAsync(child).ConfigureAwait(false);

        // Assert
        _ = child.IsExpanded.Should().BeTrue();
        _ = this.viewModel.ShownItems.Should().Equal(shownBefore);
    }

    [TestMethod]
    public async Task ExpandItemAsync_WhenCalledTwiceWhileLoading_ShowsTheChildrenOnce()
    {
        // Arrange
        var root = new TestTreeItemAdapter(isRoot: true) { Label = "Root", IsExpanded = true };
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var gated = new ControlledLoadItem("Gated", childCount: 2) { Gate = gate };
        await this.viewModel.InsertItemAsyncPublic(gated, root, 0).ConfigureAwait(false);

        // Act
        var first = this.viewModel.ExpandItemAsync(gated);
        var second = this.viewModel.ExpandItemAsync(gated);
        gate.SetResult();
        await Task.WhenAll(first, second).ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().HaveCount(4, "the root, the gated item and its two children, each once");
        _ = gated.IsExpanded.Should().BeTrue();
        _ = gated.LoadCount.Should().Be(1);
    }

    [TestMethod]
    [DataRow(false, 1, true)]
    [DataRow(true, 1, false)]
    [DataRow(false, 2, false)]
    public async Task ToggleExpanded_ShouldFlipTheExpandedState(bool initiallyExpanded, int toggles, bool expectedExpanded)
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = initiallyExpanded };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = initiallyExpanded };
        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        for (var i = 0; i < toggles; i++)
        {
            this.viewModel.ToggleExpandedCommand.Execute(rootItem);
        }

        // Assert
        _ = rootItem.IsExpanded.Should().Be(expectedExpanded);
    }

    private static (TestTreeItemAdapter Root, TestTreeItemAdapter Branch, TestTreeItemAdapter Child, TestTreeItemAdapter Leaf) CreateBranchAndLeaf(bool expandBranch)
    {
        var child = new TestTreeItemAdapter { Label = "Child" };
        var branch = new TestTreeItemAdapter([child]) { Label = "Branch", IsExpanded = expandBranch };
        var leaf = new TestTreeItemAdapter { Label = "Leaf" };
        var root = new TestTreeItemAdapter([branch, leaf], isRoot: true) { Label = "Root", IsExpanded = true };
        return (root, branch, child, leaf);
    }

    private static Func<int> CountShownItemsChanges(TestViewModel viewModel)
    {
        var count = 0;
        ((INotifyCollectionChanged)viewModel.ShownItems).CollectionChanged += (_, _) => count++;
        return () => count;
    }
}
