// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard")]
public class ViewModelKeyboardNavigationTests : ViewModelTestBase
{
    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard / Focus")]
    public async Task FocusNextVisibleItem_WhenFocused_MovesToNextShownItem()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second" };
        var root = new TestTreeItemAdapter([first, second], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(first);

        // Act
        var moved = viewModel.FocusNextVisibleItem();

        // Assert
        _ = moved.Should().BeTrue();
        _ = viewModel.FocusedItem.Should().Be(second);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard / Focus")]
    public async Task FocusFirstVisibleItemInParent_WithMultipleSiblings_MovesToFirstSibling()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var middle = new TestTreeItemAdapter { Label = "Middle" };
        var last = new TestTreeItemAdapter { Label = "Last" };
        var root = new TestTreeItemAdapter([first, middle, last], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(last);

        // Act
        var moved = viewModel.FocusFirstVisibleItemInParent();

        // Assert
        _ = moved.Should().BeTrue();
        _ = viewModel.FocusedItem.Should().Be(first);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard / Focus")]
    public async Task FocusLastVisibleItemInParent_WithMultipleSiblings_MovesToLastSibling()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var middle = new TestTreeItemAdapter { Label = "Middle" };
        var last = new TestTreeItemAdapter { Label = "Last" };
        var root = new TestTreeItemAdapter([first, middle, last], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(first);

        // Act
        var moved = viewModel.FocusLastVisibleItemInParent();

        // Assert
        _ = moved.Should().BeTrue();
        _ = viewModel.FocusedItem.Should().Be(last);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard / Focus")]
    public async Task FocusFirstVisibleItemInTree_WithShownItems_MovesToFirst()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second" };
        var root = new TestTreeItemAdapter([first, second], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(second);

        // Act
        var moved = viewModel.FocusFirstVisibleItemInTree();

        // Assert
        _ = moved.Should().BeTrue();
        _ = viewModel.FocusedItem.Should().Be(first);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard / ExpandCollapse")]
    public async Task CollapseFocusedItemAsync_WhenExpanded_CollapsesAndKeepsFocus()
    {
        // Arrange
        var child = new TestTreeItemAdapter { Label = "Child", IsExpanded = true };
        var grandChild = new TestTreeItemAdapter { Label = "GrandChild" };
        child.AddChild(grandChild);
        var root = new TestTreeItemAdapter([child], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(child);

        // Act
        var collapsed = await viewModel.CollapseFocusedItemAsync().ConfigureAwait(false);

        // Assert
        _ = collapsed.Should().BeTrue();
        _ = child.IsExpanded.Should().BeFalse();
        _ = viewModel.FocusedItem.Should().Be(child);
        _ = viewModel.ShownItems.Should().Contain(child).And.NotContain(grandChild);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard / ExpandCollapse")]
    public async Task ExpandFocusedItemAsync_WhenCollapsed_ExpandsAndKeepsFocus()
    {
        // Arrange
        var child = new TestTreeItemAdapter { Label = "Child" };
        var grandChild = new TestTreeItemAdapter { Label = "GrandChild" };
        child.AddChild(grandChild);
        var root = new TestTreeItemAdapter([child], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(child);

        // Act
        var expanded = await viewModel.ExpandFocusedItemAsync().ConfigureAwait(false);

        // Assert
        _ = expanded.Should().BeTrue();
        _ = child.IsExpanded.Should().BeTrue();
        _ = viewModel.FocusedItem.Should().Be(child);
        _ = viewModel.ShownItems.Should().Contain(grandChild);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard / Selection")]
    public async Task ToggleSelectionForFocused_WithControl_TogglesSelectionState()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var root = new TestTreeItemAdapter([first], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Multiple };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.ClearAndSelectItem(first);
        _ = viewModel.FocusItem(first);

        // Act
        var toggled = viewModel.ToggleSelectionForFocused(isControlKeyDown: true, isShiftKeyDown: false);

        // Assert
        _ = toggled.Should().BeTrue();
        _ = first.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard / Selection")]
    public async Task ToggleSelectionForFocused_WithShift_ExtendsSelectionRange()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second" };
        var third = new TestTreeItemAdapter { Label = "Third" };
        var root = new TestTreeItemAdapter([first, second, third], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Multiple };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.ClearAndSelectItem(first);
        _ = viewModel.FocusItem(third);

        // Act
        var extended = viewModel.ToggleSelectionForFocused(isControlKeyDown: false, isShiftKeyDown: true);

        // Assert
        _ = extended.Should().BeTrue();
        _ = first.IsSelected.Should().BeTrue();
        _ = second.IsSelected.Should().BeTrue();
        _ = third.IsSelected.Should().BeTrue();
    }
}
