// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[TestCategory("DynamicTree / ViewModel / Clipboard")]
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
        _ = viewModel.ClipboardItems.Should().ContainInOrder(item1, item2);
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
    public async Task PasteItems_FromCopy_InsertsClonesAndClearsClipboard()
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

        await viewModel.CopyItemsAsync([parent, child]).ConfigureAwait(false);

        // Act
        await viewModel.PasteItemsAsync(target, insertIndex: 0).ConfigureAwait(false);

        // Assert
        var targetChildren = await target.Children.ConfigureAwait(false);
        var pastedParent = targetChildren[0];
        _ = pastedParent.Should().NotBeSameAs(parent);
        _ = pastedParent.Label.Should().Be(parent.Label);

        var pastedChild = (await pastedParent.Children.ConfigureAwait(false))[0];
        _ = pastedChild.Should().NotBeSameAs(child);
        _ = pastedChild.Parent.Should().Be(pastedParent);

        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Empty);
        _ = viewModel.ClipboardItems.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CopyParentOnly_CopiesWholeSubtree()
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

        // Assert
        var targetChildren = await target.Children.ConfigureAwait(false);
        var pastedParent = targetChildren[0];
        _ = pastedParent.Should().NotBeSameAs(parent);
        _ = pastedParent.Label.Should().Be(parent.Label);

        var pastedChild = (await pastedParent.Children.ConfigureAwait(false))[0];
        _ = pastedChild.Should().NotBeSameAs(child);
        _ = pastedChild.Parent.Should().Be(pastedParent);
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
    public async Task Clipboard_AnyMutation_InvalidatesClipboard()
    {
        // Arrange
        var source = new TestTreeItemAdapter { Label = "Source" };
        var root = new TestTreeItemAdapter([source], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CopyItemsAsync([source]).ConfigureAwait(false);

        var changes = 0;
        viewModel.ClipboardContentChanged += (_, __) => changes++;

        // Act
        var newItem = new TestTreeItemAdapter { Label = "New" };
        await viewModel.InsertItemAsync(newItem, root, 0).ConfigureAwait(false);

        // Assert
        _ = viewModel.IsClipboardValid.Should().BeFalse();
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Copied);
        _ = changes.Should().BeGreaterThanOrEqualTo(1);
    }

    [TestMethod]
    public async Task Clipboard_Mutation_ClearsCutMarks()
    {
        // Arrange
        var cutItem = new TestTreeItemAdapter { Label = "Cut", IsExpanded = true };
        var root = new TestTreeItemAdapter([cutItem], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);

        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.SelectItem(cutItem, RequestOrigin.PointerInput);
        _ = viewModel.FocusItem(cutItem, RequestOrigin.PointerInput);

        await viewModel.CutItemsAsync([cutItem]).ConfigureAwait(false);
        _ = cutItem.IsCut.Should().BeTrue();

        // Act: mutate tree to invalidate clipboard
        var newItem = new TestTreeItemAdapter { Label = "New" };
        await viewModel.InsertItemAsync(newItem, root, 0).ConfigureAwait(false);

        // Assert: clipboard invalidated and cut visual cleared
        _ = viewModel.IsClipboardValid.Should().BeFalse();
        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Cut);
        _ = cutItem.IsCut.Should().BeFalse();
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
}
