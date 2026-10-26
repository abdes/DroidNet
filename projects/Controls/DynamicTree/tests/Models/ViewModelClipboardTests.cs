// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Clipboard")]
public class ViewModelClipboardTests : ViewModelTestBase
{
    [TestMethod]
    public async Task CopyItems_VisibleItems_ClipboardStateUpdated()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter { Label = "Item1" };
        var item2 = new TestTreeItemAdapter { Label = "Item2" };
        var root = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        var eventCalls = 0;
        viewModel.ClipboardContentChanged += (_, __) => eventCalls++;

        // Act
        await viewModel.CopyItemsAsync([item1, item2]).ConfigureAwait(false);

        // Assert
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Copied);
        _ = viewModel.IsClipboardValid.Should().BeTrue();
        _ = viewModel.ClipboardItems.Should().Equal(item1, item2);
        _ = item1.IsCut.Should().BeFalse();
        _ = item2.IsCut.Should().BeFalse();
        _ = eventCalls.Should().Be(1);
    }

    [TestMethod]
    public async Task CutItems_LockedItemsSkipped_MarksEligibleAndState()
    {
        // Arrange
        var locked = new TestTreeItemAdapter { Label = "Locked", IsLocked = true };
        var cuttable = new TestTreeItemAdapter { Label = "Cuttable" };
        var root = new TestTreeItemAdapter([locked, cuttable], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        await viewModel.CutItemsAsync([locked, cuttable]).ConfigureAwait(false);

        // Assert
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Cut);
        _ = viewModel.IsClipboardValid.Should().BeTrue();
        _ = viewModel.ClipboardItems.Should().ContainSingle().Which.Should().Be(cuttable);
        _ = cuttable.IsCut.Should().BeTrue();
        _ = locked.IsCut.Should().BeFalse();
    }

    [TestMethod]
    public async Task PasteItems_FromCopy_InsertsClonesAndKeepsTheClipboard()
    {
        // Arrange
        var child = new TestTreeItemAdapter { Label = "Child" };
        var parent = new TestTreeItemAdapter([child]) { Label = "Parent", IsExpanded = true };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([parent, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Single };

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.SelectItem(target, RequestOrigin.PointerInput);
        _ = viewModel.FocusItem(target, RequestOrigin.PointerInput);

        await viewModel.CopyItemsAsync([parent]).ConfigureAwait(false);

        // Act
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Assert: the whole subtree is cloned under the target
        var targetChildren = await target.Children.ConfigureAwait(false);
        var pastedParent = targetChildren.Should().ContainSingle().Which;
        _ = pastedParent.Should().NotBeSameAs(parent);
        _ = pastedParent.Label.Should().Be(parent.Label);

        var pastedChild = (await pastedParent.Children.ConfigureAwait(false)).Should().ContainSingle().Which;
        _ = pastedChild.Should().NotBeSameAs(child);
        _ = pastedChild.Label.Should().Be(child.Label);
        _ = pastedChild.Parent.Should().Be(pastedParent);

        // Pasting does not consume the clipboard
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Copied);
        _ = viewModel.ClipboardItems.Should().Equal(parent, child);
    }

    [TestMethod]
    public async Task PasteItems_FromCopy_CanBePastedAgain()
    {
        // Arrange
        var source = new TestTreeItemAdapter { Label = "Source" };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([source, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CopyItemsAsync([source]).ConfigureAwait(false);

        // Act
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Assert
        _ = (await target.Children.ConfigureAwait(false)).Should().HaveCount(2);
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Copied);
    }

    [TestMethod]
    public async Task CopyItemsAsync_AfterAPaste_ReplacesTheClipboard()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second" };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([first, second, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CopyItemsAsync([first]).ConfigureAwait(false);
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Act
        await viewModel.CopyItemsAsync([second]).ConfigureAwait(false);

        // Assert
        _ = viewModel.ClipboardItems.Should().Equal(second);
    }

    [TestMethod]
    public async Task PasteItems_AfterCut_MovesItemsAndEmptiesTheClipboard()
    {
        // Arrange
        var child = new TestTreeItemAdapter { Label = "Child" };
        var source = new TestTreeItemAdapter([child]) { Label = "Source", IsExpanded = true };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([source, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CutItemsAsync([source]).ConfigureAwait(false);

        // Act
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Assert: the original items moved (no clones) and are no longer marked as cut
        _ = source.Parent.Should().BeSameAs(target);
        _ = child.Parent.Should().BeSameAs(source);
        _ = viewModel.ShownItems.Should().Equal(root, target, source, child);
        _ = source.IsCut.Should().BeFalse();
        _ = child.IsCut.Should().BeFalse();

        // A pasted cut is consumed: nothing is left to paste
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Empty);
        _ = viewModel.ClipboardItems.Should().BeEmpty();
    }

    [TestMethod]
    public async Task PasteItems_WithEmptyClipboard_Throws()
    {
        // Arrange
        var root = new TestTreeItemAdapter(isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.PasteItemsAsync(root, insertIndex: 0).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task DuplicateItemsAsync_InsertsClonesAndLeavesTheClipboardUntouched()
    {
        // Arrange
        var source = new TestTreeItemAdapter { Label = "Source" };
        var root = new TestTreeItemAdapter([source], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var duplicates = await viewModel.DuplicateItemsAsync([source], root, insertIndex: 1).ConfigureAwait(false);

        // Assert
        var duplicate = duplicates.Should().ContainSingle().Which;
        _ = duplicate.Should().NotBeSameAs(source);
        _ = duplicate.Label.Should().Be(source.Label);
        _ = duplicate.Parent.Should().BeSameAs(root);
        _ = viewModel.ShownItems.Should().Equal(root, source, duplicate);
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Empty);
    }

    [TestMethod]
    public async Task ClearClipboardAsync_AfterCut_EmptiesTheClipboardAndClearsCutMarks()
    {
        // Arrange
        var child = new TestTreeItemAdapter { Label = "Child" };
        var source = new TestTreeItemAdapter([child]) { Label = "Source", IsExpanded = true };
        var root = new TestTreeItemAdapter([source], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CutItemsAsync([source]).ConfigureAwait(false);

        var eventCalls = 0;
        viewModel.ClipboardContentChanged += (_, __) => eventCalls++;

        // Act
        await viewModel.ClearClipboardAsync().ConfigureAwait(false);

        // Assert
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Empty);
        _ = viewModel.ClipboardItems.Should().BeEmpty();
        _ = source.IsCut.Should().BeFalse();
        _ = child.IsCut.Should().BeFalse();
        _ = eventCalls.Should().Be(1);
    }

    [TestMethod]
    public async Task CutParent_MarksDescendantsAsCut()
    {
        // Arrange
        var grandChild = new TestTreeItemAdapter { Label = "GChild" };
        var child = new TestTreeItemAdapter([grandChild]) { Label = "Child", IsExpanded = true };
        var parent = new TestTreeItemAdapter([child]) { Label = "Parent", IsExpanded = true };
        var root = new TestTreeItemAdapter([parent], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        await viewModel.CutItemsAsync([parent]).ConfigureAwait(false);

        // Assert: parent and children should be marked as cut
        _ = parent.IsCut.Should().BeTrue();
        _ = child.IsCut.Should().BeTrue();
        _ = grandChild.IsCut.Should().BeTrue();
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task Clipboard_UnrelatedInsertion_PreservesTheStagedSources(bool cut)
    {
        // Arrange
        var source = new TestTreeItemAdapter { Label = "Source" };
        var root = new TestTreeItemAdapter([source], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        if (cut)
        {
            await viewModel.CutItemsAsync([source]).ConfigureAwait(false);
        }
        else
        {
            await viewModel.CopyItemsAsync([source]).ConfigureAwait(false);
        }

        var changes = 0;
        viewModel.ClipboardContentChanged += (_, __) => changes++;

        // Act
        var newItem = new TestTreeItemAdapter { Label = "New" };
        await viewModel.InsertItemAsync(newItem, root, 0).ConfigureAwait(false);

        // Assert
        _ = viewModel.IsClipboardValid.Should().BeTrue();
        _ = viewModel.CurrentClipboardState.Should().Be(cut ? ClipboardState.Cut : ClipboardState.Copied);
        _ = viewModel.ClipboardItems.Should().Equal(source);
        _ = source.IsCut.Should().Be(cut);
        _ = changes.Should().Be(0);
    }

    [TestMethod]
    public async Task PasteItems_CutIntoSelf_SkipsWithoutException()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = true };
        var root = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Single };

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.SelectItem(item, RequestOrigin.PointerInput);
        _ = viewModel.FocusItem(item, RequestOrigin.PointerInput);

        await viewModel.CutItemsAsync([item]).ConfigureAwait(false);

        // Act
        await viewModel.PasteItemsAsync().ConfigureAwait(false);

        // Assert
        var rootChildren = await root.Children.ConfigureAwait(false);
        _ = rootChildren.Should().ContainSingle().Which.Should().BeSameAs(item);
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Cut);
        _ = viewModel.IsClipboardValid.Should().BeTrue();
        _ = viewModel.ClipboardItems.Should().ContainSingle().Which.Should().BeSameAs(item);
        _ = item.IsCut.Should().BeTrue();
    }

    [TestMethod]
    public async Task PasteItems_CopyInMultipleMode_SelectsAllPastedItemsInOneSettledSelection()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second" };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([first, second, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Multiple };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CopyItemsAsync([first, second]).ConfigureAwait(false);
        var settled = 0;
        viewModel.SelectionSettled += (_, _) => settled++;

        // Act
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Assert
        var pasted = await target.Children.ConfigureAwait(false);
        _ = pasted.Select(item => item.Label).Should().Equal("First", "Second");
        _ = pasted.Should().OnlyContain(item => item.IsSelected);
        _ = viewModel.SelectedItemsCount.Should().Be(2);
        _ = viewModel.ActiveItem.Should().BeSameAs(pasted[0]);
        _ = settled.Should().Be(1);
    }

    [TestMethod]
    public async Task PasteItems_AfterCut_RaisesOneClipboardChangeWhenTheClipboardEmpties()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([item, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Single };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CutItemsAsync([item]).ConfigureAwait(false);
        var changes = new List<ClipboardContentChangedEventArgs>();
        viewModel.ClipboardContentChanged += (_, args) => changes.Add(args);

        // Act
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Assert
        _ = item.Parent.Should().BeSameAs(target);
        var change = changes.Should().ContainSingle().Which;
        _ = change.NewState.Should().Be(ClipboardState.Empty);
        _ = change.Items.Should().BeEmpty();
        _ = change.IsValid.Should().BeTrue();
        _ = viewModel.IsClipboardValid.Should().BeTrue();
    }

    [TestMethod]
    public async Task PasteItems_AfterACutWasPasted_Throws()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var first = new TestTreeItemAdapter { Label = "First", IsExpanded = true };
        var second = new TestTreeItemAdapter { Label = "Second", IsExpanded = true };
        var root = new TestTreeItemAdapter([item, first, second], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CutItemsAsync([item]).ConfigureAwait(false);
        await viewModel.PasteItemsAsync(first, insertIndex: 0).ConfigureAwait(false);

        // Act
        var act = async () => await viewModel.PasteItemsAsync(second, insertIndex: 0).ConfigureAwait(false);

        // Assert: the moved item is not moved again
        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        _ = item.Parent.Should().BeSameAs(first);
    }

    [TestMethod]
    public async Task PasteItems_WhenTheCutMoveIsVetoed_KeepsTheClipboardAndTheSelection()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([item, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Single };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.SelectItem(target, RequestOrigin.PointerInput);
        await viewModel.CutItemsAsync([item]).ConfigureAwait(false);
        viewModel.ItemBeingMoved += (_, args) => args.Proceed = false;

        // Act
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Assert
        _ = item.Parent.Should().BeSameAs(root);
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Cut);
        _ = viewModel.IsClipboardValid.Should().BeTrue();
        _ = item.IsCut.Should().BeTrue();
        _ = target.IsSelected.Should().BeTrue();
        _ = item.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public async Task CopyItems_WithANonClonableParent_SkipsItsWholeSubtree()
    {
        // Arrange
        var grandchild = new TestTreeItemAdapter { Label = "Grandchild" };
        var parent = new NonClonableItem(grandchild) { Label = "Parent", IsExpanded = true };
        var sibling = new TestTreeItemAdapter { Label = "Sibling" };
        var root = new TestTreeItemAdapter([sibling], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.InsertItemAsyncPublic(parent, root, 1).ConfigureAwait(false);

        // Act
        await viewModel.CopyItemsAsync([parent, sibling]).ConfigureAwait(false);

        // Assert
        _ = viewModel.ClipboardItems.Should().ContainSingle().Which.Should().BeSameAs(sibling);
    }

    [TestMethod]
    public async Task PasteItems_WhenTheTargetCannotAcceptChildren_IsSkippedAndKeepsTheClipboard()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var leaf = new LeafItem { Label = "Leaf" };
        var root = new TestTreeItemAdapter([item, leaf], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CopyItemsAsync([item]).ConfigureAwait(false);

        // Act
        await viewModel.PasteItemsAsync(leaf).ConfigureAwait(false);

        // Assert
        _ = leaf.ChildrenCount.Should().Be(0);
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Copied);
    }

    [TestMethod]
    public async Task PasteItems_WithoutATarget_PastesIntoTheFocusedItem()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([item, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(target, RequestOrigin.Programmatic);
        await viewModel.CopyItemsAsync([item]).ConfigureAwait(false);

        // Act
        await viewModel.PasteItemsAsync().ConfigureAwait(false);

        // Assert
        _ = target.ChildrenCount.Should().Be(1);
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Copied);
    }

    [TestMethod]
    public async Task DuplicateItemsAsync_SelectsTheClonesInOneSettledSelection()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second" };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([first, second, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Multiple };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var settled = 0;
        viewModel.SelectionSettled += (_, _) => settled++;

        // Act
        var clones = await viewModel.DuplicateItemsAsync([first, second], target, 0).ConfigureAwait(false);

        // Assert
        _ = clones.Select(clone => clone.Label).Should().Equal("First", "Second");
        _ = clones.Should().OnlyContain(clone => clone.IsSelected);
        _ = viewModel.SelectedItemsCount.Should().Be(2);
        _ = viewModel.ActiveItem.Should().BeSameAs(clones[0]);
        _ = settled.Should().Be(1);
    }

    [TestMethod]
    public async Task CopyItems_ChildListedBeforeItsParent_PastesTheChildrenInTreeOrder()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second" };
        var parent = new TestTreeItemAdapter([first, second]) { Label = "Parent", IsExpanded = true };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([parent, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CopyItemsAsync([second, parent]).ConfigureAwait(false);

        // Act
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Assert
        var pastedParent = (await target.Children.ConfigureAwait(false)).Should().ContainSingle().Which;
        _ = (await pastedParent.Children.ConfigureAwait(false)).Select(child => child.Label).Should().Equal("First", "Second");
    }

    [TestMethod]
    public async Task PasteItems_FromCopy_WhenARootInsertionIsVetoed_PastesNothingAndKeepsTheClipboard()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second" };
        var target = new TestTreeItemAdapter { Label = "Target", IsExpanded = true };
        var root = new TestTreeItemAdapter([first, second, target], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CopyItemsAsync([first, second]).ConfigureAwait(false);
        viewModel.ItemBeingAdded += (_, args) => args.Proceed = !string.Equals(args.TreeItem.Label, "Second", StringComparison.Ordinal);

        // Act
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Assert
        _ = target.ChildrenCount.Should().Be(0);
        _ = viewModel.ShownItems.Should().Equal(root, first, second, target);
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Copied);
    }

    [TestMethod]
    public async Task PasteItems_IntoAHiddenRoot_AddsATopLevelRow()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var root = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root" };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CopyItemsAsync([item]).ConfigureAwait(false);

        // Act
        await viewModel.PasteItemsAsync(root, insertIndex: 1).ConfigureAwait(false);

        // Assert
        _ = root.ChildrenCount.Should().Be(2);
        _ = viewModel.ShownItems.Should().HaveCount(2).And.StartWith(item);
    }

    [TestMethod]
    public async Task StageClipboard_ThenClearClipboard_RemovesTheCutMarksAndTheState()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var root = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.StageClipboardPublic([item], cut: true);
        _ = item.IsCut.Should().BeTrue();

        // Act
        viewModel.ClearClipboardPublic();

        // Assert
        _ = item.IsCut.Should().BeFalse();
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Empty);
        _ = viewModel.ClipboardItems.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ReleaseClipboardRows_KeepsTheStateAndRemovesTheRowsAndTheirMarks()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var root = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.StageClipboardPublic([item], cut: true);
        var changes = 0;
        viewModel.ClipboardContentChanged += (_, _) => changes++;

        // Act
        viewModel.ReleaseClipboardRowsPublic();

        // Assert
        _ = item.IsCut.Should().BeFalse();
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Cut);
        _ = viewModel.ClipboardItems.Should().BeEmpty();
        _ = changes.Should().Be(1);
    }

    private sealed class LeafItem : TestTreeItemAdapter
    {
        public override bool CanAcceptChildren => false;
    }

    private sealed class NonClonableItem(params TreeItemAdapter[] children) : TreeItemAdapter(isRoot: false)
    {
        public override string Label { get; set; } = string.Empty;

        public override bool ValidateItemName(string name) => !string.IsNullOrEmpty(name);

        protected override int DoGetChildrenCount() => children.Length;

        protected override Task LoadChildren()
        {
            foreach (var child in children)
            {
                this.AddChildInternal(child);
            }

            return Task.CompletedTask;
        }
    }
}
