// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls.Demo.Tree;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

[TestClass]
public sealed class DynamicTreeDensityWorkloadTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [DataRow(true, true)]
    [DataRow(true, false)]
    [DataRow(false, true)]
    [DataRow(false, false)]
    public Task DensityProfiles_MeasureExactRowsAndPropagateToRecycledRows(bool compact, bool narrow) => EnqueueAsync(async () =>
    {
        using var viewModel = new TestViewModel();
        var root = new LazyTreeItem("Root", 999, isRoot: true) { IsExpanded = true };
        await viewModel.InitializeAsync(root).ConfigureAwait(true);
        var rows = viewModel.ShownItems.Cast<LazyTreeItem>().ToArray();
        var tree = new DynamicTree { ViewModel = viewModel, Width = narrow ? 300 : 600, Height = 320 };
        TreePresentation.ApplyDensity(tree, compact, narrow);
        await LoadTestContentAsync(tree).ConfigureAwait(true);
        var repeater = tree.FindDescendant<ItemsRepeater>()!;
        var scroller = tree.FindDescendant<ScrollViewer>()!;
        var height = compact ? 32d : 40d;
        _ = tree.ItemRowHeight.Should().Be(height);
        AssertRealizedDensity(repeater, rows.Length, height, compact ? 18 : 24);
        _ = scroller.ViewportHeight.Should().BeApproximately(320, 1);
        var maximumRealized = RealizedCount(repeater, rows.Length);
        var observerCounts = rows.Select(item => item.PropertyObservers).ToArray();
        var elapsed = Stopwatch.StartNew();
        foreach (var offset in new[] { scroller.ScrollableHeight, scroller.ScrollableHeight / 2, 0d })
        {
            await ScrollAndRenderAsync(scroller, offset).ConfigureAwait(true);
            maximumRealized = Math.Max(maximumRealized, RealizedCount(repeater, rows.Length));
            AssertRealizedDensity(repeater, rows.Length, height, compact ? 18 : 24);
        }

        var scrollingMilliseconds = elapsed.Elapsed.TotalMilliseconds;
        var realizationLimit = ((int)Math.Ceiling(scroller.ViewportHeight / height) * 6) + 4;
        _ = maximumRealized.Should().BeLessThanOrEqualTo(realizationLimit);
        _ = rows.Skip(1).Should().OnlyContain(item => item.LoadCount == 0 && !item.AreChildrenLoaded);
        await AssertReloadObserverCountsAsync(tree, rows, observerCounts).ConfigureAwait(true);

        TreePresentation.ApplyDensity(tree, !compact, narrow);
        tree.UpdateLayout();
        await RenderAsync().ConfigureAwait(true);
        AssertRealizedDensity(repeater, rows.Length, compact ? 40 : 32, compact ? 24 : 18);
        tree.ItemFontSize = 28;
        tree.UpdateLayout();
        await RenderAsync().ConfigureAwait(true);
        var enlarged = (TreeItemControl)((FrameworkElement)repeater.TryGetElement(1)!).FindName(DynamicTree.TreeItemPart);
        var label = enlarged.FindDescendant<TextBlock>(item => string.Equals(item.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal))!;
        _ = enlarged.ActualHeight.Should().BeGreaterThanOrEqualTo(label.ActualHeight + label.Margin.Top + label.Margin.Bottom);
        _ = label.FontSize.Should().Be(28);
        _ = rows.Skip(1).Sum(item => item.LoadCount).Should().Be(0);
        this.TestContext.WriteLine(
            $"{height:F0} DIP / 14 font, width {tree.Width:F0}: 1000 rows, maximum realized {maximumRealized}/{realizationLimit}, "
            + $"three scrolls {scrollingMilliseconds:F2} ms; branch loads 0; observers unchanged after two reloads.");
    });

    private static async Task AssertReloadObserverCountsAsync(DynamicTree tree, LazyTreeItem[] rows, int[] observerCounts)
    {
        for (var cycle = 0; cycle < 2; cycle++)
        {
            await LoadTestContentAsync(new Border()).ConfigureAwait(true);
            await LoadTestContentAsync(tree).ConfigureAwait(true);
            _ = rows.Select(item => item.PropertyObservers).Should().Equal(observerCounts);
        }
    }

    private static void AssertRealizedDensity(ItemsRepeater repeater, int count, double height, double iconSize)
    {
        var measured = 0;
        for (var index = 0; index < count; index++)
        {
            if (repeater.TryGetElement(index) is not FrameworkElement container
                || container.FindName(DynamicTree.TreeItemPart) is not TreeItemControl row)
            {
                continue;
            }

            measured++;
            _ = row.ActualHeight.Should().BeApproximately(height, 0.5);
            _ = row.ItemFontSize.Should().Be(14);
            _ = row.ItemIconSize.Should().Be(iconSize);
            var label = row.FindDescendant<TextBlock>(item => string.Equals(item.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal))!;
            _ = label.FontSize.Should().Be(14);
            _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
        }

        _ = measured.Should().BePositive();
    }

    private static int RealizedCount(ItemsRepeater repeater, int count)
        => Enumerable.Range(0, count).Count(index => repeater.TryGetElement(index) is not null);

    private static async Task ScrollAndRenderAsync(ScrollViewer scroller, double offset)
    {
        _ = scroller.ChangeView(horizontalOffset: null, offset, zoomFactor: null, disableAnimation: true);
        for (var frame = 0; frame < 20; frame++)
        {
            await RenderAsync().ConfigureAwait(true);
            if (Math.Abs(scroller.VerticalOffset - offset) <= 1)
            {
                await RenderAsync().ConfigureAwait(true);
                return;
            }
        }

        _ = scroller.VerticalOffset.Should().BeApproximately(offset, 1);
    }

    private static async Task RenderAsync()
        => _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
}
