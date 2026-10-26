// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Tests;

/// <summary>
/// The active (explicitly selected) identity is tracked independently of selection order,
/// and the consumer co-selection/bulk policies reshape interactive, range and bulk
/// selection. The default policy (everything allowed) leaves generic behaviour unchanged.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / SelectionPolicy")]
public sealed partial class ViewModelSelectionPolicyTests : ViewModelTestBase
{
    [TestMethod]
    public async Task ActiveItem_TracksLastExplicitSelectionNotRowOrder()
    {
        var a = Item("A");
        var b = Item("B");
        using var viewModel = await this.CreateAsync(a, b).ConfigureAwait(false);
        var shown = Shown(viewModel);

        viewModel.SelectDisplayedItem(b, [b], isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(a, shown, isControlDown: true, isShiftDown: false);

        _ = viewModel.ActiveItem.Should().Be(a, "the last explicitly clicked row is the active identity");
    }

    [TestMethod]
    public async Task CtrlDeselectingActiveRow_ClearsActiveAndKeepsRemainingSelection()
    {
        var a = Item("A");
        var b = Item("B");
        using var viewModel = await this.CreateAsync(a, b).ConfigureAwait(false);
        var shown = Shown(viewModel);

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(b, shown, isControlDown: true, isShiftDown: false);
        viewModel.SelectDisplayedItem(b, shown, isControlDown: true, isShiftDown: false);

        _ = viewModel.ActiveItem.Should().BeNull("deselecting the active row leaves no explicit identity");
        _ = viewModel.SelectedItem.Should().Be(a, "the remaining selection survives");
    }

    [TestMethod]
    public async Task SelectionSettled_FiresOncePerEntryWithFinalActiveIdentity()
    {
        var a = Item("A");
        var b = Item("B");
        var c = Item("C");
        using var viewModel = await this.CreateAsync(a, b, c).ConfigureAwait(false);
        var shown = Shown(viewModel);

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
    public async Task ShiftRange_Downward_SelectsEveryRowFromTheAnchor()
    {
        var a = Item("A");
        var b = Item("B");
        var c = Item("C");
        using var viewModel = await this.CreateAsync(a, b, c).ConfigureAwait(false);
        var shown = Shown(viewModel);

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(c, shown, isControlDown: false, isShiftDown: true);

        _ = shown.Where(item => item.IsSelected).Should().BeEquivalentTo([a, b, c]);
        _ = viewModel.ActiveItem.Should().Be(c);
    }

    [TestMethod]
    public async Task ShiftRange_Upward_SelectsEveryRowFromTheAnchor()
    {
        var a = Item("A");
        var b = Item("B");
        var c = Item("C");
        using var viewModel = await this.CreateAsync(a, b, c).ConfigureAwait(false);
        var shown = Shown(viewModel);

        viewModel.SelectDisplayedItem(c, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(b, shown, isControlDown: false, isShiftDown: true);

        _ = shown.Where(item => item.IsSelected).Should().BeEquivalentTo([b, c]);
        _ = viewModel.ActiveItem.Should().Be(b);
    }

    [TestMethod]
    public async Task CoSelectionPolicy_RefusedCtrlClick_BecomesExclusiveSelection()
    {
        var a = Item("A");
        var exclusive = Item("Exclusive");
        using var viewModel = await this.CreateExclusiveAsync(exclusive, a, exclusive).ConfigureAwait(false);
        var shown = Shown(viewModel);

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(exclusive, shown, isControlDown: true, isShiftDown: false);

        _ = viewModel.SelectedItemsCount.Should().Be(1, "a refused co-selection candidate replaces the selection");
        _ = viewModel.SelectedItem.Should().Be(exclusive);
        _ = viewModel.ActiveItem.Should().Be(exclusive);
    }

    [TestMethod]
    public async Task CoSelectionPolicy_SelectedExclusiveRow_BlocksAddingAnotherRow()
    {
        var a = Item("A");
        var exclusive = Item("Exclusive");
        using var viewModel = await this.CreateExclusiveAsync(exclusive, a, exclusive).ConfigureAwait(false);
        var shown = Shown(viewModel);

        viewModel.SelectDisplayedItem(exclusive, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(a, shown, isControlDown: true, isShiftDown: false);

        _ = viewModel.SelectedItemsCount.Should().Be(1, "an existing exclusive selection cannot be widened");
        _ = viewModel.SelectedItem.Should().Be(a, "the newly clicked row becomes the exclusive selection");
    }

    [TestMethod]
    public async Task ShiftRange_TargetingExcludedRow_SelectsItAlone()
    {
        var a = Item("A");
        var exclusive = Item("Exclusive");
        using var viewModel = await this.CreateExclusiveAsync(exclusive, a, exclusive).ConfigureAwait(false);
        var shown = Shown(viewModel);

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(exclusive, shown, isControlDown: false, isShiftDown: true);

        _ = viewModel.SelectedItemsCount.Should().Be(1);
        _ = viewModel.SelectedItem.Should().Be(exclusive);
    }

    [TestMethod]
    public async Task ShiftRange_SkipsExcludedRowInsideTheRange()
    {
        var a = Item("A");
        var exclusive = Item("Exclusive");
        var b = Item("B");
        using var viewModel = await this.CreateExclusiveAsync(exclusive, a, exclusive, b).ConfigureAwait(false);
        var shown = Shown(viewModel);

        viewModel.SelectDisplayedItem(a, shown, isControlDown: false, isShiftDown: false);
        viewModel.SelectDisplayedItem(b, shown, isControlDown: false, isShiftDown: true);

        _ = viewModel.SelectedItemsCount.Should().Be(2, "the range covers A and B; the excluded row is skipped");
        _ = shown.Where(item => item.IsSelected).Should().BeEquivalentTo([a, b]);
        _ = viewModel.ActiveItem.Should().Be(b, "the shifted-to row carries the active identity");
    }

    [TestMethod]
    public async Task SelectAll_NormalizesExistingExcludedSelection_AndSkipsExcludedRows()
    {
        var a = Item("A");
        var exclusive = Item("Exclusive");
        var b = Item("B");
        using var viewModel = await this.CreateExclusiveAsync(exclusive, a, exclusive, b).ConfigureAwait(false);

        viewModel.SelectDisplayedItem(exclusive, Shown(viewModel), isControlDown: false, isShiftDown: false);
        viewModel.SelectAllCommand.Execute(parameter: null);

        _ = viewModel.SelectedItemsCount.Should().Be(2, "the bulk set replaces the incompatible exclusive selection");
        _ = a.IsSelected.Should().BeTrue();
        _ = b.IsSelected.Should().BeTrue();
        _ = exclusive.IsSelected.Should().BeFalse("select-all neither adds nor keeps a policy-excluded row");
    }

    [TestMethod]
    public async Task SelectAll_DefaultPolicy_SelectsEveryRow()
    {
        var a = Item("A");
        var b = Item("B");
        using var viewModel = await this.CreateAsync(a, b).ConfigureAwait(false);

        viewModel.SelectAllCommand.Execute(parameter: null);

        _ = viewModel.SelectedItemsCount.Should().Be(2, "the default policy changes no generic behaviour");
        _ = Shown(viewModel).Where(item => item.IsSelected).Should().BeEquivalentTo([a, b]);
    }

    [TestMethod]
    public async Task ToggleDisplayedSelection_NormalizesAnExistingExcludedSelection()
    {
        var a = Item("A");
        var exclusive = Item("Exclusive");
        var b = Item("B");
        using var viewModel = await this.CreateExclusiveAsync(exclusive, a, exclusive, b).ConfigureAwait(false);

        // Start from the incompatible exclusive selection; the bulk add must normalize it away.
        viewModel.SelectDisplayedItem(exclusive, Shown(viewModel), isControlDown: false, isShiftDown: false);
        viewModel.ToggleDisplayedSelection([a, exclusive, b]);

        _ = viewModel.SelectedItemsCount.Should().Be(2);
        _ = a.IsSelected.Should().BeTrue();
        _ = b.IsSelected.Should().BeTrue();
        _ = exclusive.IsSelected.Should().BeFalse("a bulk set never coexists with the excluded row");
    }

    [TestMethod]
    public async Task InvertDisplayedSelection_AddsUnselectedIncludedRows_NeverTheExcludedRow()
    {
        var a = Item("A");
        var exclusive = Item("Exclusive");
        var b = Item("B");
        using var viewModel = await this.CreateExclusiveAsync(exclusive, a, exclusive, b).ConfigureAwait(false);

        viewModel.SelectDisplayedItem(a, Shown(viewModel), isControlDown: false, isShiftDown: false);
        viewModel.InvertDisplayedSelection([a, exclusive, b]);

        _ = a.IsSelected.Should().BeFalse("inverting clears the selected row");
        _ = b.IsSelected.Should().BeTrue("inverting selects the unselected row");
        _ = exclusive.IsSelected.Should().BeFalse("inverting never selects the excluded row");
        _ = viewModel.SelectedItemsCount.Should().Be(1);
    }

    private static TestTreeItemAdapter Item(string label) => new() { Label = label };

    private static List<ITreeItem> Shown(DynamicTreeViewModel viewModel) => [.. viewModel.ShownItems];

    private async Task<TestViewModel> CreateAsync(params TestTreeItemAdapter[] items)
    {
        var root = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance)
        {
            SelectionMode = SelectionMode.Multiple,
        };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        return viewModel;
    }

    private async Task<ExclusiveItemViewModel> CreateExclusiveAsync(ITreeItem exclusive, params TestTreeItemAdapter[] items)
    {
        var root = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new ExclusiveItemViewModel(this.LoggerFactoryInstance, exclusive)
        {
            SelectionMode = SelectionMode.Multiple,
        };
        await viewModel.InitializeRootAsync(root, skipRoot: true).ConfigureAwait(false);
        return viewModel;
    }

    /// <summary>
    /// Test view model marking one row as exclusive: it never coexists with other rows,
    /// never participates in range or bulk selection, and an existing selection of it is
    /// normalized away by bulk operations — the generic seam a consumer uses for a root
    /// row without the control knowing any consumer concept.
    /// </summary>
    private sealed partial class ExclusiveItemViewModel(ILoggerFactory? loggerFactory, ITreeItem excluded)
        : DynamicTreeViewModel(loggerFactory)
    {
        protected override bool AllowsCoSelection(ITreeItem candidate, IReadOnlyList<ITreeItem> currentlySelected)
            => !ReferenceEquals(candidate, excluded) && !currentlySelected.Contains(excluded);

        protected override bool IsIncludedInBulk(ITreeItem item) => !ReferenceEquals(item, excluded);
    }
}
