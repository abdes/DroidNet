// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

/// <summary>
/// Tests that filtering a lazily loaded tree never loads collapsed branches and keeps its item subscriptions balanced.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Lazy workload")]
public sealed class ViewModelLazyWorkloadTests : ViewModelTestBase
{
    private const int ChildCount = 49;
    private const int EditedRows = 10;

    [TestMethod]
    public async Task FilterPredicate_CollapsedBranches_AreNotLoaded()
    {
        // Arrange
        var root = new LazyTreeItem("Root", ChildCount, isRoot: true) { IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var rows = viewModel.ShownItems.Cast<LazyTreeItem>().ToArray();
        var sourceChanges = 0;
        ((INotifyCollectionChanged)viewModel.ShownItems).CollectionChanged += (_, _) => sourceChanges++;

        // Act
        viewModel.FilterPredicate = item => item.Label.Contains("NoSuchRow", StringComparison.Ordinal);
        var filtered = viewModel.FilteredItems.ToList();

        // Assert
        _ = rows.Should().HaveCount(ChildCount + 1);
        _ = filtered.Should().BeEmpty();
        _ = sourceChanges.Should().Be(0, "filtering is a projection and never changes ShownItems");
        _ = viewModel.ShownItems.Should().Equal(rows);
        _ = rows.Skip(1).Should().OnlyContain(item => item.LoadCount == 0 && !item.AreChildrenLoaded);
        _ = root.LoadCount.Should().Be(1);
    }

    [TestMethod]
    public async Task ValueEdits_AreDebouncedUntilRefreshFiltering()
    {
        // Arrange
        var root = new LazyTreeItem("Root", ChildCount, isRoot: true) { IsExpanded = true };
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var rows = viewModel.ShownItems.Cast<LazyTreeItem>().ToArray();
        var evaluations = 0;
        viewModel.FilterPredicate = item =>
        {
            evaluations++;
            return item.Label.Contains("Row", StringComparison.Ordinal);
        };
        _ = viewModel.FilteredItems.Should().BeEmpty();
        evaluations = 0;

        // Act
        for (var index = 1; index <= EditedRows; index++)
        {
            rows[index].Label = string.Create(CultureInfo.InvariantCulture, $"Row {index:D4}");
        }

        // Assert
        _ = evaluations.Should().Be(0, "value changes are debounced instead of re-evaluating per edit");

        viewModel.RefreshFiltering();

        _ = evaluations.Should().Be(rows.Length, "one batch refresh evaluates each loaded item once");
        _ = viewModel.FilteredItems.Should().HaveCount(EditedRows + 1, "the matching rows plus their ancestor");
        _ = rows.Skip(1).Should().OnlyContain(item => item.LoadCount == 0);
    }

    [TestMethod]
    public async Task FilterPredicate_ToggledRepeatedly_KeepsObserversBalancedAndDisposeReleasesThem()
    {
        // Arrange
        var root = new LazyTreeItem("Root", ChildCount, isRoot: true) { IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var rows = viewModel.ShownItems.Cast<LazyTreeItem>().ToArray();
        Predicate<ITreeItem> predicate = item => item.Label.Contains("Root", StringComparison.Ordinal);
        viewModel.FilterPredicate = predicate;
        _ = viewModel.FilteredItems.Should().HaveCount(rows.Length);
        var baselineObservers = rows.Select(item => item.PropertyObservers).ToArray();
        _ = baselineObservers.Should().OnlyContain(count => count > 0);

        // Act / Assert
        for (var cycle = 0; cycle < 3; cycle++)
        {
            viewModel.FilterPredicate = null;
            _ = rows.Should().OnlyContain(item => item.PropertyObservers == 0);

            viewModel.FilterPredicate = predicate;
            _ = viewModel.FilteredItems.Should().HaveCount(rows.Length);
            _ = rows.Select(item => item.PropertyObservers).Should().Equal(baselineObservers);
        }

        viewModel.Dispose();

        _ = rows.Should().OnlyContain(item => item.PropertyObservers == 0);
    }
}
