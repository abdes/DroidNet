// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

/// <summary>
/// C34: with an explicit displayed-interaction scope, keyboard focus and typeahead must stay within
/// the displayed (filtered) projection and never land on a row the filter hid. The default
/// <see cref="TreeSelectionScope.ShownItems"/> behaviour is unchanged and covered elsewhere.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / DisplayedScope")]
public sealed class ViewModelDisplayedScopeTests : ViewModelTestBase
{
    [TestMethod]
    public async Task FocusNext_DisplayedScope_EntersScopeAtFirstDisplayedNotNextShown()
    {
        // Arrange: A, B, C shown; filter leaves only C displayed.
        var a = new TestTreeItemAdapter { Label = "A" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var c = new TestTreeItemAdapter { Label = "C" };
        var root = new TestTreeItemAdapter([a, b, c], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        viewModel.FilterPredicate = item => string.Equals(item.Label, "C", StringComparison.Ordinal);
        viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;
        _ = viewModel.FocusItem(a, RequestOrigin.KeyboardInput);

        // Act
        var moved = viewModel.FocusNextVisibleItem(RequestOrigin.KeyboardInput);

        // Assert: from a focus outside the scope, navigation enters the displayed scope at C — it
        // must NOT step to B, which the filter hid.
        _ = moved.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().Be(c, "displayed scope skips filtered-out B");
    }

    [TestMethod]
    public async Task FocusNext_DisplayedScope_DoesNotStepToAHiddenItem()
    {
        // Arrange: A, B, C shown; filter leaves only B displayed.
        var a = new TestTreeItemAdapter { Label = "A" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var c = new TestTreeItemAdapter { Label = "C" };
        var root = new TestTreeItemAdapter([a, b, c], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        viewModel.FilterPredicate = item => string.Equals(item.Label, "B", StringComparison.Ordinal);
        viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;
        _ = viewModel.FocusItem(b, RequestOrigin.KeyboardInput);

        // Act: B is the last displayed item, so there is nothing to move to.
        var moved = viewModel.FocusNextVisibleItem(RequestOrigin.KeyboardInput);

        // Assert: focus stays on B; it must not advance to the filtered-out C.
        _ = moved.Should().BeFalse();
        _ = viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().Be(b, "navigation stops at the end of the displayed scope");
    }

    [TestMethod]
    public async Task TypeAhead_DisplayedScope_IgnoresFilteredOutMatches()
    {
        // Arrange: ALPHA and GAMMA shown; filter leaves only ALPHA displayed.
        var alpha = new TestTreeItemAdapter { Label = "ALPHA" };
        var gamma = new TestTreeItemAdapter { Label = "GAMMA" };
        var root = new TestTreeItemAdapter([alpha, gamma], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        viewModel.FilterPredicate = item => string.Equals(item.Label, "ALPHA", StringComparison.Ordinal);
        viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;
        _ = viewModel.FocusItem(alpha, RequestOrigin.KeyboardInput);

        // Act: typeahead for a label that only a hidden row carries.
        var moved = viewModel.FocusNextByPrefix("GAMMA", RequestOrigin.KeyboardInput);

        // Assert: no displayed match, so focus is unchanged; it must not jump to the hidden GAMMA.
        _ = moved.Should().BeFalse("the only GAMMA row is filtered out of the displayed scope");
        _ = viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().Be(alpha);
    }

    [TestMethod]
    public async Task FocusNext_ShownItemsScope_StillWalksEveryShownItem()
    {
        // Arrange: default scope; filter is present but must not affect navigation.
        var a = new TestTreeItemAdapter { Label = "A" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var root = new TestTreeItemAdapter([a, b], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        viewModel.FilterPredicate = item => string.Equals(item.Label, "A", StringComparison.Ordinal);

        // InteractionScope left at its default (ShownItems).
        _ = viewModel.FocusItem(a, RequestOrigin.KeyboardInput);

        // Act
        var moved = viewModel.FocusNextVisibleItem(RequestOrigin.KeyboardInput);

        // Assert: the historical default is preserved — B is reachable even though filtered out.
        _ = moved.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().Be(b, "ShownItems scope is unchanged by a filter");
    }

    [TestMethod]
    [DataRow(true, true)]
    [DataRow(true, false)]
    [DataRow(false, true)]
    [DataRow(false, false)]
    public async Task Navigate_DisplayedScope_EntersAtBoundary(bool forward, bool hasFocus)
    {
        var hidden = new TestTreeItemAdapter { Label = "Hidden" };
        var first = new TestTreeItemAdapter { Label = "Visible first" };
        var last = new TestTreeItemAdapter { Label = "Visible last" };
        var root = new TestTreeItemAdapter([first, hidden, last], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.FilterPredicate = item => item.Label.StartsWith("Visible", StringComparison.Ordinal);
        viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;
        _ = viewModel.FilteredItems.Should().Equal(first, last);
        if (hasFocus)
        {
            _ = viewModel.FocusItem(hidden, RequestOrigin.Programmatic);
        }
        else
        {
            viewModel.ClearFocus();
        }

        var moved = forward
            ? viewModel.FocusNextVisibleItem(RequestOrigin.KeyboardInput)
            : viewModel.FocusPreviousVisibleItem(RequestOrigin.KeyboardInput);

        _ = moved.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focused, out var origin).Should().BeTrue();
        _ = focused.Should().BeSameAs(forward ? first : last);
        _ = origin.Should().Be(RequestOrigin.KeyboardInput);
    }

    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task Navigate_DisplayedScope_SkipsHiddenRowsAndStopsAtBoundary(bool forward)
    {
        var first = new TestTreeItemAdapter { Label = "Visible first" };
        var hidden = new TestTreeItemAdapter { Label = "Hidden" };
        var last = new TestTreeItemAdapter { Label = "Visible last" };
        var root = new TestTreeItemAdapter([first, hidden, last], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.FilterPredicate = item => item.Label.StartsWith("Visible", StringComparison.Ordinal);
        viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;
        _ = viewModel.FocusItem(forward ? first : last, RequestOrigin.Programmatic);

        bool Navigate() => forward
            ? viewModel.FocusNextVisibleItem(RequestOrigin.KeyboardInput)
            : viewModel.FocusPreviousVisibleItem(RequestOrigin.KeyboardInput);

        _ = Navigate().Should().BeTrue();
        _ = Navigate().Should().BeFalse();
        _ = viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().BeSameAs(forward ? last : first);
    }

    [TestMethod]
    [DataRow(true, true)]
    [DataRow(true, false)]
    [DataRow(false, true)]
    [DataRow(false, false)]
    public async Task FocusBoundary_DisplayedScope_IgnoresHiddenEndpoints(bool first, bool inTree)
    {
        var hiddenFirst = new TestTreeItemAdapter { Label = "Hidden first" };
        var visibleFirst = new TestTreeItemAdapter { Label = "Visible first" };
        var visibleLast = new TestTreeItemAdapter { Label = "Visible last" };
        var hiddenLast = new TestTreeItemAdapter { Label = "Hidden last" };
        var root = new TestTreeItemAdapter([hiddenFirst, visibleFirst, visibleLast, hiddenLast], isRoot: true)
        {
            Label = "Root",
            IsExpanded = true,
        };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.FilterPredicate = item => item.Label.StartsWith("Visible", StringComparison.Ordinal);
        viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;
        _ = viewModel.FocusItem(visibleFirst, RequestOrigin.Programmatic);

        var moved = (first, inTree) switch
        {
            (true, true) => viewModel.FocusFirstVisibleItemInTree(RequestOrigin.KeyboardInput),
            (true, false) => viewModel.FocusFirstVisibleItemInParent(RequestOrigin.KeyboardInput),
            (false, true) => viewModel.FocusLastVisibleItemInTree(RequestOrigin.KeyboardInput),
            (false, false) => viewModel.FocusLastVisibleItemInParent(RequestOrigin.KeyboardInput),
        };

        _ = moved.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().BeSameAs(first ? visibleFirst : visibleLast);
    }

    [TestMethod]
    public async Task Navigation_DisplayedScope_WithNoMatches_ClearsFocusAndCannotNavigate()
    {
        var item = new TestTreeItemAdapter { Label = "Hidden" };
        var root = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.FilterPredicate = _ => false;
        viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;

        Func<bool>[] actions =
        [
            () => viewModel.FocusNextVisibleItem(RequestOrigin.KeyboardInput),
            () => viewModel.FocusPreviousVisibleItem(RequestOrigin.KeyboardInput),
            () => viewModel.FocusFirstVisibleItemInTree(RequestOrigin.KeyboardInput),
            () => viewModel.FocusLastVisibleItemInTree(RequestOrigin.KeyboardInput),
            () => viewModel.FocusFirstVisibleItemInParent(RequestOrigin.KeyboardInput),
            () => viewModel.FocusLastVisibleItemInParent(RequestOrigin.KeyboardInput),
            () => viewModel.EnsureFocus(RequestOrigin.KeyboardInput),
        ];
        foreach (var action in actions)
        {
            _ = viewModel.FocusItem(item, RequestOrigin.Programmatic);
            _ = action().Should().BeFalse();
            _ = viewModel.TryGetFocusedItem(out _, out _).Should().BeFalse();
        }

        _ = viewModel.FocusNextByPrefix("Hidden", RequestOrigin.KeyboardInput).Should().BeFalse();
    }

    [TestMethod]
    public async Task EnsureFocus_DisplayedScope_IgnoresHiddenSelection()
    {
        var hidden = new TestTreeItemAdapter { Label = "Hidden" };
        var visible = new TestTreeItemAdapter { Label = "Visible" };
        var root = new TestTreeItemAdapter([hidden, visible], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.SelectionMode = SelectionMode.Single;
        viewModel.ClearAndSelectItem(hidden);
        viewModel.FilterPredicate = item => string.Equals(item.Label, "Visible", StringComparison.Ordinal);
        viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;
        viewModel.ClearFocus();

        _ = viewModel.EnsureFocus(RequestOrigin.KeyboardInput).Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().BeSameAs(visible);
        _ = hidden.IsSelected.Should().BeTrue();
    }

    [TestMethod]
    public async Task TypeAhead_DisplayedScope_WrapsAmongDisplayedMatches()
    {
        var first = new TestTreeItemAdapter { Label = "BETA" };
        var hidden = new TestTreeItemAdapter { Label = "BETA" };
        var last = new TestTreeItemAdapter { Label = "BETA" };
        var root = new TestTreeItemAdapter([first, hidden, last], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.FilterPredicate = item => !ReferenceEquals(item, hidden) && !item.IsRoot;
        viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;
        _ = viewModel.FocusItem(first, RequestOrigin.KeyboardInput);

        _ = viewModel.FocusNextByPrefix("BETA", RequestOrigin.KeyboardInput).Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().BeSameAs(last);
        _ = viewModel.FocusNextByPrefix("BETA", RequestOrigin.KeyboardInput).Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out focused, out _).Should().BeTrue();
        _ = focused.Should().BeSameAs(first);

        viewModel.FilterPredicate = null;
        _ = viewModel.FocusNextVisibleItem(RequestOrigin.KeyboardInput).Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out focused, out _).Should().BeTrue();
        _ = focused.Should().BeSameAs(hidden);
    }
}
