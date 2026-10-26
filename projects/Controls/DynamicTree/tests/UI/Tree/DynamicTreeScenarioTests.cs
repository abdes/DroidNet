// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Documents;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// A user's view of the tree: what the rows show, and what the expander, the selection, the filter bar and the rename
/// editor do to them. The shared fixture is the <see cref="TestViewModel"/> tree, a root R with children R-C1 (two
/// children), R-C2 (one child) and R-C3, loaded in a tree with the filter bar enabled.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed partial class DynamicTreeScenarioTests : VisualUserInterfaceTests, IDisposable
{
    private TestableDynamicTree? tree;
    private TestViewModel? viewModel;

    public TestContext TestContext { get; set; } = null!;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    public Task ShowingATree_ShowsOneRowPerShownItemWithItsLabel_Async() => EnqueueAsync(async () =>
    {
        await this.ExpandRootAsync().ConfigureAwait(true);

        var rows = TreeRenderHelpers.Rows(this.tree!);
        _ = rows.Should().HaveCount(this.viewModel!.ShownItemsCount);
        _ = rows.Select(LabelOf).Should().BeEquivalentTo("R", "R-C1", "R-C2", "R-C3");
    });

    [TestMethod]
    public Task ExpandingAndCollapsingARow_AddsAndRemovesItsChildRows_Async() => EnqueueAsync(async () =>
    {
        var vm = this.viewModel!;
        var rootRow = this.RowOf("R");

        rootRow.FindDescendant<Controls.Expander>()!.Toggle();
        await this.WaitForShownCountAsync(4).ConfigureAwait(true);
        await TreeRenderHelpers.RenderAsync().ConfigureAwait(true);
        _ = TreeRenderHelpers.Rows(this.tree!).Select(LabelOf).Should().BeEquivalentTo("R", "R-C1", "R-C2", "R-C3");

        rootRow.FindDescendant<Controls.Expander>()!.Toggle();
        await this.WaitForShownCountAsync(1).ConfigureAwait(true);
        await TreeRenderHelpers.RenderAsync().ConfigureAwait(true);
        _ = vm.ShownItems.Should().ContainSingle();
        _ = TreeRenderHelpers.Rows(this.tree!).Select(LabelOf).Should().BeEquivalentTo("R");
    });

    [TestMethod]
    public Task ClickingRows_ShowsTheSelectionInTheirVisualState_Async() => EnqueueAsync(async () =>
    {
        await this.ExpandRootAsync().ConfigureAwait(true);
        var states = new TestVisualStateManager();
        var rows = new[] { "R-C1", "R-C2", "R-C3" }.Select(this.RowOf).ToArray();
        foreach (var row in rows)
        {
            VisualStateManager.SetCustomVisualStateManager(
                row.FindDescendant<Grid>(grid => string.Equals(grid.Name, TreeItemControl.RootGridPart, StringComparison.Ordinal)),
                states);
        }

        _ = this.tree!.InvokeItemPointerPressed((TreeItemAdapter)rows[0].ItemAdapter!, isControlDown: false, isShiftDown: false, leftButtonPressed: true);
        AssertSelected(states, rows, true, false, false);

        _ = this.tree.InvokeItemPointerPressed((TreeItemAdapter)rows[1].ItemAdapter!, isControlDown: true, isShiftDown: false, leftButtonPressed: true);
        AssertSelected(states, rows, true, true, false);

        _ = this.tree.InvokeItemPointerPressed((TreeItemAdapter)rows[2].ItemAdapter!, isControlDown: false, isShiftDown: false, leftButtonPressed: true);
        AssertSelected(states, rows, false, false, true);
    });

    [TestMethod]
    public Task FilteringFromTheFilterBar_ShowsMatchesAndTheirAncestors_AndClearingRestoresTheTree_Async() => EnqueueAsync(async () =>
    {
        var vm = this.viewModel!;
        var control = this.tree!;
        await this.ExpandRootAsync().ConfigureAwait(true);
        foreach (var child in (await vm.ShownItems.First().Children.ConfigureAwait(true)).Take(2))
        {
            await vm.ExpandItemAsync(child).ConfigureAwait(true);
        }

        var input = control.FindDescendant<TextBox>(e => string.Equals(e.Name, DynamicTree.FilterTextBoxPart, StringComparison.Ordinal))!;
        input.Text = "GC1";

        // The filter bar owns the text; the host turns it into the view model's predicate.
        _ = control.FilterText.Should().Be("GC1");
        vm.FilterPredicate = item => item.Label.Contains(control.FilterText, StringComparison.Ordinal);
        control.IsFilteringEnabled = true;
        await TreeRenderHelpers.RenderAsync().ConfigureAwait(true);
        _ = TreeRenderHelpers.Rows(control).Select(LabelOf).Should().BeEquivalentTo("R", "R-C1", "R-C1-GC1", "R-C2", "R-C2-GC1");

        input.Text = string.Empty;
        vm.FilterPredicate = null;
        await TreeRenderHelpers.RenderAsync().ConfigureAwait(true);
        _ = TreeRenderHelpers.Rows(control).Should().HaveCount(vm.ShownItemsCount);
    });

    [TestMethod]
    public Task RenamingARow_CommittingTheEditorUpdatesItsLabel_Async() => EnqueueAsync(async () =>
    {
        var item = this.viewModel!.ShownItems.First();
        var row = this.RowOf("R");
        _ = await this.tree!.BeginRenameAsync(item).ConfigureAwait(true);
        var popup = row.FindDescendant<Popup>(part => string.Equals(part.Name, TreeItemControl.InPlaceRenamePart, StringComparison.Ordinal))!;
        _ = popup.IsOpen.Should().BeTrue();
        ((StackPanel)popup.Child).Children.OfType<TextBox>().Single().Text = "Renamed";

        await row.CommitRenameDraftAsync().ConfigureAwait(true);

        await TreeRenderHelpers.RenderAsync().ConfigureAwait(true);
        _ = item.Label.Should().Be("Renamed");
        _ = LabelOf(row).Should().Be("Renamed");
        _ = popup.IsOpen.Should().BeFalse();
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(true);
        this.viewModel = new TestViewModel(this.LoggerFactory);
        await this.viewModel.LoadTreeStructureAsync().ConfigureAwait(true);
        this.viewModel.SelectionMode = SelectionMode.Multiple;

        this.tree = new TestableDynamicTree { ViewModel = this.viewModel, Height = 400, IsFilterBarEnabled = true };
        await LoadTestContentAsync(this.tree).ConfigureAwait(true);
        await TreeRenderHelpers.RenderAsync().ConfigureAwait(true);
    }

    private static string LabelOf(TreeItemControl row)
    {
        var label = row.FindDescendant<TextBlock>(block => string.Equals(block.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal))!;
        return string.Concat(label.Inlines.OfType<Run>().Select(static run => run.Text));
    }

    private static void AssertSelected(TestVisualStateManager states, TreeItemControl[] rows, params bool[] expected)
    {
        for (var index = 0; index < rows.Length; index++)
        {
            var current = states.GetCurrentStates(rows[index]);
            if (expected[index])
            {
                _ = current.Should().Contain(TreeItemControl.SelectedVisualState);
            }
            else
            {
                // The state manager logs every state entered; the latest one is the current state.
                _ = (current ?? []).LastOrDefault().Should().NotBe(TreeItemControl.SelectedVisualState);
            }
        }
    }

    private TreeItemControl RowOf(string label)
        => this.tree!.FindDescendant<TreeItemControl>(row => string.Equals(row.ItemAdapter?.Label, label, StringComparison.Ordinal))!;

    private async Task ExpandRootAsync()
    {
        await this.viewModel!.ExpandItemAsync(this.viewModel.ShownItems.First()).ConfigureAwait(true);
        await TreeRenderHelpers.RenderAsync().ConfigureAwait(true);
    }

    private async Task WaitForShownCountAsync(int count)
    {
        var vm = this.viewModel!;
        var reached = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var shown = (INotifyCollectionChanged)vm.ShownItems;
        void OnChanged(object? sender, NotifyCollectionChangedEventArgs args)
        {
            if (vm.ShownItemsCount == count)
            {
                _ = reached.TrySetResult();
            }
        }

        shown.CollectionChanged += OnChanged;
        try
        {
            if (vm.ShownItemsCount != count)
            {
                await reached.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            }
        }
        finally
        {
            shown.CollectionChanged -= OnChanged;
        }
    }
}
