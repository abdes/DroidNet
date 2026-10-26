// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Keyboard")]
public class ViewModelKeyboardNavigationTests : ViewModelTestBase
{
    public enum FocusMove
    {
        Next,
        Previous,
        FirstInParent,
        LastInParent,
        FirstInTree,
        LastInTree,
    }

    [TestMethod]
    [DataRow(FocusMove.Next, "First", "Middle")]
    [DataRow(FocusMove.Next, "Last", null)]
    [DataRow(FocusMove.Previous, "Middle", "First")]
    [DataRow(FocusMove.Previous, "First", null)]
    [DataRow(FocusMove.FirstInParent, "Last", "First")]
    [DataRow(FocusMove.LastInParent, "First", "Last")]
    [DataRow(FocusMove.FirstInTree, "Last", "First")]
    [DataRow(FocusMove.LastInTree, "First", "Last")]
    public async Task FocusMove_FromFocusedItem_MovesFocusWithTheRequestOrigin(FocusMove move, string startLabel, string? expectedLabel)
    {
        // Arrange: a null expected label means the move is refused and the focus stays where it was.
        var first = new TestTreeItemAdapter { Label = "First" };
        var middle = new TestTreeItemAdapter { Label = "Middle" };
        var last = new TestTreeItemAdapter { Label = "Last" };
        var root = new TestTreeItemAdapter([first, middle, last], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var start = viewModel.ShownItems.Single(item => string.Equals(item.Label, startLabel, StringComparison.Ordinal));
        _ = viewModel.FocusItem(start, RequestOrigin.Programmatic);

        // Act
        var moved = Apply(viewModel, move);

        // Assert
        _ = moved.Should().Be(expectedLabel is not null);
        _ = viewModel.TryGetFocusedItem(out var focusedItem, out var origin).Should().BeTrue();
        _ = focusedItem!.Label.Should().Be(expectedLabel ?? startLabel);
        _ = origin.Should().Be(expectedLabel is not null ? RequestOrigin.KeyboardInput : RequestOrigin.Programmatic);
    }

    [TestMethod]
    public async Task CollapseFocusedItemAsync_WhenExpanded_CollapsesAndKeepsFocus()
    {
        // Arrange
        var grandChild = new TestTreeItemAdapter { Label = "GrandChild" };
        var child = new TestTreeItemAdapter([grandChild]) { Label = "Child", IsExpanded = true };
        var root = new TestTreeItemAdapter([child], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(child, RequestOrigin.KeyboardInput);

        // Act
        var collapsed = await viewModel.CollapseFocusedItemAsync().ConfigureAwait(false);

        // Assert
        _ = collapsed.Should().BeTrue();
        _ = child.IsExpanded.Should().BeFalse();
        _ = viewModel.TryGetFocusedItem(out var focusedItem, out var origin).Should().BeTrue();
        _ = focusedItem.Should().Be(child);
        _ = origin.Should().Be(RequestOrigin.KeyboardInput);
        _ = viewModel.ShownItems.Should().Equal(child);
    }

    [TestMethod]
    public async Task ExpandFocusedItemAsync_WhenCollapsed_ExpandsAndKeepsFocusAndOrigin()
    {
        // Arrange
        var grandChild = new TestTreeItemAdapter { Label = "GrandChild" };
        var child = new TestTreeItemAdapter([grandChild]) { Label = "Child" };
        var root = new TestTreeItemAdapter([child], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(child, RequestOrigin.KeyboardInput);

        // Act
        var expanded = await viewModel.ExpandFocusedItemAsync().ConfigureAwait(false);

        // Assert
        _ = expanded.Should().BeTrue();
        _ = child.IsExpanded.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focusedItem, out var origin).Should().BeTrue();
        _ = focusedItem.Should().Be(child);
        _ = origin.Should().Be(RequestOrigin.KeyboardInput);
        _ = viewModel.ShownItems.Should().Equal(child, grandChild);
    }

    private static bool Apply(TestViewModel viewModel, FocusMove move)
        => move switch
        {
            FocusMove.Next => viewModel.FocusNextVisibleItem(RequestOrigin.KeyboardInput),
            FocusMove.Previous => viewModel.FocusPreviousVisibleItem(RequestOrigin.KeyboardInput),
            FocusMove.FirstInParent => viewModel.FocusFirstVisibleItemInParent(RequestOrigin.KeyboardInput),
            FocusMove.LastInParent => viewModel.FocusLastVisibleItemInParent(RequestOrigin.KeyboardInput),
            FocusMove.FirstInTree => viewModel.FocusFirstVisibleItemInTree(RequestOrigin.KeyboardInput),
            FocusMove.LastInTree => viewModel.FocusLastVisibleItemInTree(RequestOrigin.KeyboardInput),
            _ => throw new ArgumentOutOfRangeException(nameof(move)),
        };
}
