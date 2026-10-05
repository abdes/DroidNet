// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
public sealed class ViewModelLazyWorkloadTests : ViewModelTestBase
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task ThousandRows_FilteringAndValueEdits_DoNotLoadCollapsedBranchesOrRebuildShownItems()
    {
        var root = new LazyTreeItem("Root", 999, isRoot: true) { IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        var elapsed = Stopwatch.StartNew();
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var initializeMilliseconds = elapsed.Elapsed.TotalMilliseconds;
        var rows = viewModel.ShownItems.Cast<LazyTreeItem>().ToArray();
        _ = rows.Should().HaveCount(1000);
        var sourceChanges = 0;
        var source = (INotifyCollectionChanged)viewModel.ShownItems;
        source.CollectionChanged += (_, _) => sourceChanges++;
        var evaluations = 0;
        viewModel.FilterPredicate = item =>
        {
            evaluations++;
            return item.Label.Contains("Row", StringComparison.Ordinal);
        };
        elapsed.Restart();
        _ = viewModel.FilteredItems.Should().BeEmpty();
        var filteringMilliseconds = elapsed.Elapsed.TotalMilliseconds;
        var baselineObservers = rows.Select(item => item.PropertyObservers).ToArray();
        _ = baselineObservers.Should().OnlyContain(count => count > 0);
        evaluations = 0;
        elapsed.Restart();
        for (var index = 1; index <= 50; index++)
        {
            rows[index].Label = $"Row {index:D4}";
        }

        _ = evaluations.Should().Be(0, "value changes are debounced instead of rebuilding per edit");
        viewModel.RefreshFiltering();
        var editMilliseconds = elapsed.Elapsed.TotalMilliseconds;
        _ = evaluations.Should().Be(1000, "one explicit batch refresh evaluates each loaded item once");
        _ = viewModel.FilteredItems.Should().HaveCount(51);
        _ = sourceChanges.Should().Be(0);
        _ = viewModel.ShownItems.Should().Equal(rows);
        _ = rows.Skip(1).Should().OnlyContain(item => item.LoadCount == 0 && !item.AreChildrenLoaded);

        AssertFilterSubscriptionCycles(viewModel, rows, baselineObservers);

        viewModel.Dispose();
        _ = rows.Should().OnlyContain(item => item.PropertyObservers == 0);
        _ = root.LoadCount.Should().Be(1);
        this.TestContext.WriteLine(
            $"1000 rows: initialize {initializeMilliseconds:F2} ms, filter {filteringMilliseconds:F2} ms, "
            + $"50 edits + refresh {editMilliseconds:F2} ms; source changes {sourceChanges}; "
            + $"branch loads {rows.Skip(1).Sum(item => item.LoadCount)}; batch evaluations 1000.");
    }

    private static void AssertFilterSubscriptionCycles(TestViewModel viewModel, LazyTreeItem[] rows, int[] baselineObservers)
    {
        for (var cycle = 0; cycle < 3; cycle++)
        {
            viewModel.FilterPredicate = null;
            _ = rows.Should().OnlyContain(item => item.PropertyObservers == 0);
            viewModel.FilterPredicate = item => item.Label.Contains("Row", StringComparison.Ordinal);
            _ = viewModel.FilteredItems.Should().HaveCount(51);
            _ = rows.Select(item => item.PropertyObservers).Should().Equal(baselineObservers);
        }
    }
}
