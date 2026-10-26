// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

/// <summary>
/// Range selection and select-all over the displayed (filtered) rows never reach a row the filter hid.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / DisplayedScope")]
public sealed class ViewModelDisplayedScopeSelectionTests : ViewModelTestBase
{
    private TestTreeItemAdapter first = null!;
    private TestTreeItemAdapter hidden = null!;
    private TestTreeItemAdapter middle = null!;
    private TestTreeItemAdapter last = null!;
    private TestViewModel viewModel = null!;

    [TestInitialize]
    public async Task InitializeFilteredTreeAsync()
    {
        this.first = new TestTreeItemAdapter { Label = "Visible first" };
        this.hidden = new TestTreeItemAdapter { Label = "Hidden" };
        this.middle = new TestTreeItemAdapter { Label = "Visible middle" };
        this.last = new TestTreeItemAdapter { Label = "Visible last" };
        var root = new TestTreeItemAdapter([this.first, this.hidden, this.middle, this.last], isRoot: true)
        {
            Label = "Root",
            IsExpanded = true,
        };
        this.viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Multiple };
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        this.viewModel.FilterPredicate = item => item.Label.StartsWith("Visible", StringComparison.Ordinal);
        this.viewModel.InteractionScope = TreeSelectionScope.DisplayedItems;
    }

    [TestCleanup]
    public void DisposeViewModel() => this.viewModel.Dispose();

    [TestMethod]
    public void SelectDisplayedItem_WithShift_SelectsTheDisplayedRangeOnly()
    {
        var displayed = this.viewModel.FilteredItems.ToArray();
        this.viewModel.SelectDisplayedItem(this.first, displayed, isControlDown: false, isShiftDown: false);

        this.viewModel.SelectDisplayedItem(this.last, displayed, isControlDown: false, isShiftDown: true);

        _ = this.viewModel.ShownItems.Where(item => item.IsSelected).Should().Equal(this.first, this.middle, this.last);
        _ = this.hidden.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public void ToggleDisplayedSelection_WhenNothingSelected_SelectsDisplayedRowsOnly()
    {
        this.viewModel.ToggleDisplayedSelection(this.viewModel.FilteredItems.ToArray());

        _ = this.viewModel.ShownItems.Where(item => item.IsSelected).Should().Equal(this.first, this.middle, this.last);
        _ = this.hidden.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public void ToggleDisplayedSelection_WhenAllDisplayedRowsSelected_ClearsThem()
    {
        var displayed = this.viewModel.FilteredItems.ToArray();
        this.viewModel.ToggleDisplayedSelection(displayed);

        this.viewModel.ToggleDisplayedSelection(displayed);

        _ = this.viewModel.ShownItems.Where(item => item.IsSelected).Should().BeEmpty();
    }
}
