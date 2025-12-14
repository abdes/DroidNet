// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / TypeAhead")]
public class ViewModelTypeAheadTests : ViewModelTestBase
{
    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / TypeAhead / Scoring")]
    public async Task FocusNextByPrefix_WithContiguousMatch_PrefersContiguousLabel()
    {
        // Arrange
        var nonMatch = new TestTreeItemAdapter { Label = "ZZZ" };
        var sparse = new TestTreeItemAdapter { Label = "A_B_C" };
        var contiguous = new TestTreeItemAdapter { Label = "ABC" };

        var root = new TestTreeItemAdapter([nonMatch, sparse, contiguous], isRoot: true)
        {
            Label = "Root",
            IsExpanded = true,
        };

        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Precondition: EnsureFocus should focus the first shown item.
        _ = viewModel.TryGetFocusedItem(out var focusedBefore, out _).Should().BeTrue();
        _ = focusedBefore!.Should().Be(nonMatch);

        // Act
        var moved = viewModel.FocusNextByPrefix("abc", RequestOrigin.KeyboardInput);

        // Assert
        _ = moved.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focusedAfter, out var origin).Should().BeTrue();
        _ = focusedAfter.Should().Be(contiguous);
        _ = origin.Should().Be(RequestOrigin.KeyboardInput);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / TypeAhead / Ordering")]
    public async Task FocusNextByPrefix_WhenScoresTie_PrefersNextMatchAfterFocus()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "ABC" };
        var second = new TestTreeItemAdapter { Label = "ABC" };
        var third = new TestTreeItemAdapter { Label = "OTHER" };

        var root = new TestTreeItemAdapter([first, second, third], isRoot: true)
        {
            Label = "Root",
            IsExpanded = true,
        };

        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(first, RequestOrigin.KeyboardInput);

        // Act
        var moved = viewModel.FocusNextByPrefix("ABC", RequestOrigin.KeyboardInput);

        // Assert
        _ = moved.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focusedAfter, out _).Should().BeTrue();
        _ = focusedAfter.Should().Be(second);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / TypeAhead / NoMatch")]
    public async Task FocusNextByPrefix_WhenNoMatch_ReturnsFalseAndLeavesFocusUnchanged()
    {
        // Arrange
        var first = new TestTreeItemAdapter { Label = "Alpha" };
        var second = new TestTreeItemAdapter { Label = "Beta" };

        var root = new TestTreeItemAdapter([first, second], isRoot: true)
        {
            Label = "Root",
            IsExpanded = true,
        };

        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(first, RequestOrigin.KeyboardInput);

        // Act
        var moved = viewModel.FocusNextByPrefix("QQQ", RequestOrigin.KeyboardInput);

        // Assert
        _ = moved.Should().BeFalse();
        _ = viewModel.TryGetFocusedItem(out var focusedAfter, out _).Should().BeTrue();
        _ = focusedAfter.Should().Be(first);
    }
}
