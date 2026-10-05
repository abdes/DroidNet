// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Tests;

/// <summary>
/// C17/C19 seams: the active (explicitly selected) identity is tracked independently of
/// selection order, and the consumer co-selection/bulk policies reshape interactive,
/// range and bulk selection. The default policy (everything allowed) must keep the
/// historical behaviour, which the existing suites prove unchanged.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / SelectionPolicy")]
public sealed class ViewModelSelectionPolicyTests : ViewModelTestBase
{
    [TestMethod]
    public async Task ActiveItem_TracksLastExplicitSelectionNotRowOrder()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var root = new TestTreeItemAdapter([a, b], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance)
        {
            SelectionMode = SelectionMode.Multiple,
        };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        viewModel.SelectDisplayedItem(b, [b], isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(a, shown, isControlDown: true, isShiftDown: false);

        _ = viewModel.ActiveItem.Should().Be(a, "the last explicitly clicked row is the active identity");
        _ = shown.IndexOf(a).Should().BeLessThan(shown.IndexOf(b), "A still precedes B in selection order");
    }

    [TestMethod]
    public async Task CtrlDeselectingActiveRow_ClearsActiveAndKeepsRemainingSelection()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var root = new TestTreeItemAdapter([a, b], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance)
        {
            SelectionMode = SelectionMode.Multiple,
        };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(b, shown, isControlDown: true, isShiftDown: false);
        viewModel.SelectDisplayedItem(b, shown, isControlDown: true, isShiftDown: false);

        _ = viewModel.ActiveItem.Should().BeNull("deselecting the active row leaves no explicit identity");
        _ = viewModel.SelectedItem.Should().Be(a, "the remaining selection survives");
    }

    [TestMethod]
    public async Task SelectionSettled_FiresOncePerEntryWithFinalActiveIdentity()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var c = new TestTreeItemAdapter { Label = "C" };
        var root = new TestTreeItemAdapter([a, b, c], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance)
        {
            SelectionMode = SelectionMode.Multiple,
        };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        var settles = 0;
        ITreeItem? activeAtSettle = null;
        viewModel.SelectionSettled += (_, _) =>
        {
            settles++;
            activeAtSettle = viewModel.ActiveItem;
        };

        viewModel.SelectDisplayedItem(c, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: true);

        _ = settles.Should().Be(2, "one settle notification per entry point, whatever the model notified in between");
        _ = activeAtSettle.Should().Be(a, "the settled pass sees the final active identity, not the mid-transaction one");
        _ = viewModel.SelectedItemsCount.Should().Be(3, "the shift range A..C is inclusive");
        _ = shown.Where(item => item.IsSelected).Should().BeEquivalentTo([a, b, c]);
    }

    [TestMethod]
    public async Task CoSelectionPolicy_RefusedCtrlClick_BecomesExclusiveSelection()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var exclusive = new TestTreeItemAdapter { Label = "Exclusive" };
        var root = new TestTreeItemAdapter([a, exclusive], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new ExclusiveItemViewModel(this.LoggerFactoryInstance, exclusive);
        viewModel.SelectionMode = SelectionMode.Multiple;
        await viewModel.InitializeRootAsync(root, skipRoot: true).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(exclusive, shown, isControlDown: true, isShiftDown: false);

        _ = viewModel.SelectedItemsCount.Should().Be(1, "a refused co-selection candidate replaces the selection");
        _ = viewModel.SelectedItem.Should().Be(exclusive);
        _ = viewModel.ActiveItem.Should().Be(exclusive);
    }

    [TestMethod]
    public async Task CoSelectionPolicy_SelectedExclusiveRow_BlocksAddingAnotherRow()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var exclusive = new TestTreeItemAdapter { Label = "Exclusive" };
        var root = new TestTreeItemAdapter([a, exclusive], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new ExclusiveItemViewModel(this.LoggerFactoryInstance, exclusive);
        viewModel.SelectionMode = SelectionMode.Multiple;
        await viewModel.InitializeRootAsync(root, skipRoot: true).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        viewModel.SelectDisplayedItem(exclusive, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(a, shown, isControlDown: true, isShiftDown: false);

        _ = viewModel.SelectedItemsCount.Should().Be(1, "an existing exclusive selection cannot be widened");
        _ = viewModel.SelectedItem.Should().Be(a, "the newly clicked row becomes the exclusive selection");
    }

    [TestMethod]
    public async Task ShiftRange_TargetingExcludedRow_SelectsItAlone()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var exclusive = new TestTreeItemAdapter { Label = "Exclusive" };
        var root = new TestTreeItemAdapter([a, exclusive], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new ExclusiveItemViewModel(this.LoggerFactoryInstance, exclusive);
        viewModel.SelectionMode = SelectionMode.Multiple;
        await viewModel.InitializeRootAsync(root, skipRoot: true).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(exclusive, shown, isControlDown: false, isShiftDown: true);

        _ = viewModel.SelectedItemsCount.Should().Be(1);
        _ = viewModel.SelectedItem.Should().Be(exclusive);
    }

    [TestMethod]
    public async Task ShiftRange_SkipsExcludedRowInsideTheRange()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var exclusive = new TestTreeItemAdapter { Label = "Exclusive" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var root = new TestTreeItemAdapter([a, exclusive, b], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new ExclusiveItemViewModel(this.LoggerFactoryInstance, exclusive);
        viewModel.SelectionMode = SelectionMode.Multiple;
        await viewModel.InitializeRootAsync(root, skipRoot: true).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(b, shown, isControlDown: false, isShiftDown: true);

        _ = viewModel.SelectedItemsCount.Should().Be(2, "the range covers A and B; the excluded row is skipped");
        _ = shown.Where(item => item.IsSelected).Should().BeEquivalentTo([a, b]);
        _ = viewModel.ActiveItem.Should().Be(b, "the shifted-to row carries the active identity");
    }

    [TestMethod]
    public async Task SelectAll_NormalizesExistingExcludedSelection_AndSkipsExcludedRows()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var exclusive = new TestTreeItemAdapter { Label = "Exclusive" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var root = new TestTreeItemAdapter([a, exclusive, b], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new ExclusiveItemViewModel(this.LoggerFactoryInstance, exclusive);
        viewModel.SelectionMode = SelectionMode.Multiple;
        await viewModel.InitializeRootAsync(root, skipRoot: true).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        viewModel.SelectDisplayedItem(exclusive, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectAllCommand.Execute(parameter: null);

        _ = viewModel.SelectedItemsCount.Should().Be(2, "the bulk set replaces the incompatible exclusive selection");
        _ = a.IsSelected.Should().BeTrue();
        _ = b.IsSelected.Should().BeTrue();
        _ = exclusive.IsSelected.Should().BeFalse("select-all neither adds nor keeps a policy-excluded row");
    }

    [TestMethod]
    public async Task SelectAll_DefaultPolicy_SelectsEveryRow()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var root = new TestTreeItemAdapter([a, b], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance)
        {
            SelectionMode = SelectionMode.Multiple,
        };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        viewModel.SelectAllCommand.Execute(parameter: null);

        _ = viewModel.SelectedItemsCount.Should().Be(2, "the default policy changes no generic behaviour");
        _ = viewModel.ShownItems.Should().BeEquivalentTo([a, b]);
    }

    [TestMethod]
    public async Task ToggleDisplayedSelection_NormalizesAnExistingExcludedSelection()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var exclusive = new TestTreeItemAdapter { Label = "Exclusive" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var root = new TestTreeItemAdapter([a, exclusive, b], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new ExclusiveItemViewModel(this.LoggerFactoryInstance, exclusive);
        viewModel.SelectionMode = SelectionMode.Multiple;
        await viewModel.InitializeRootAsync(root, skipRoot: true).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        // Start from the incompatible exclusive selection; the bulk add must normalize it away.
        viewModel.SelectDisplayedItem(exclusive, shown, isControlDown: false, isShiftDown: false);
        viewModel.ToggleDisplayedSelection([a, exclusive, b]);

        _ = viewModel.SelectedItemsCount.Should().Be(2);
        _ = a.IsSelected.Should().BeTrue();
        _ = b.IsSelected.Should().BeTrue();
        _ = exclusive.IsSelected.Should().BeFalse("a bulk set never coexists with the excluded row");
    }

    [TestMethod]
    public async Task InvertDisplayedSelection_AddsUnselectedIncludedRows_NeverTheExcludedRow()
    {
        var a = new TestTreeItemAdapter { Label = "A" };
        var exclusive = new TestTreeItemAdapter { Label = "Exclusive" };
        var b = new TestTreeItemAdapter { Label = "B" };
        var root = new TestTreeItemAdapter([a, exclusive, b], isRoot: true) { Label = "Root", IsExpanded = true };
        using var viewModel = new ExclusiveItemViewModel(this.LoggerFactoryInstance, exclusive);
        viewModel.SelectionMode = SelectionMode.Multiple;
        await viewModel.InitializeRootAsync(root, skipRoot: true).ConfigureAwait(false);
        var shown = viewModel.ShownItems.ToList();

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.InvertDisplayedSelection([a, exclusive, b]);

        _ = a.IsSelected.Should().BeFalse("inverting clears the selected row");
        _ = b.IsSelected.Should().BeTrue("inverting selects the unselected row");
        _ = exclusive.IsSelected.Should().BeFalse("inverting never selects the excluded row");
        _ = viewModel.SelectedItemsCount.Should().Be(1);
    }

    /// <summary>
    /// Test view model marking one row as exclusive: it never coexists with other rows,
    /// never participates in range or bulk selection, and an existing selection of it is
    /// normalized away by bulk operations — the generic seam C19 uses for the scene root
    /// row without the control knowing any scene concept.
    /// </summary>
    private sealed class ExclusiveItemViewModel(ILoggerFactory? loggerFactory, ITreeItem excluded)
        : DynamicTreeViewModel(loggerFactory)
    {
        protected override bool AllowsCoSelection(ITreeItem candidate, IReadOnlyList<ITreeItem> currentlySelected)
            => !ReferenceEquals(candidate, excluded) && !currentlySelected.Contains(excluded);

        protected override bool IsIncludedInBulk(ITreeItem item) => !ReferenceEquals(item, excluded);
    }
}
