// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// Regression repro: expanding a row with hundreds of children and scrolling down at once, as a fast mouse wheel does,
/// left realized rows showing another row's item, or blank.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed class DynamicTreeFastScrollTests : VisualUserInterfaceTests
{
    // Kept at the size of the original report: a smaller tree may not reproduce the stale rows.
    private const int ChildCount = 700;

    [TestMethod]
    public Task FastWheelScrollAfterExpand_LeavesNoRealizedRowShowingAnotherItemOrBlank_Async() => EnqueueAsync(async () =>
    {
        using var viewModel = new TestViewModel();
        var parent = new LazyTreeItem("Parent", ChildCount, isRoot: true);
        await viewModel.InitializeAsync(parent).ConfigureAwait(true);
        var tree = new DynamicTree { ViewModel = viewModel, Width = 320, Height = 320 };
        await LoadTestContentAsync(tree).ConfigureAwait(true);
        var repeater = tree.FindDescendant<ItemsRepeater>()!;
        var scroller = tree.FindDescendant<ScrollView>()!;

        await viewModel.ExpandItemAsync(parent).ConfigureAwait(true);
        tree.UpdateLayout();

        // A fast wheel: each frame scrolls further down before the previous rows have settled.
        for (var step = 1; step <= 10; step++)
        {
            _ = scroller.ScrollTo(0, scroller.ScrollableHeight * step / 10, new ScrollingScrollOptions(ScrollingAnimationMode.Enabled));
            await TreeRenderHelpers.RenderAsync().ConfigureAwait(true);
        }

        // The wheel stops at the end, whatever the animations still had in flight.
        _ = scroller.ScrollTo(0, scroller.ScrollableHeight, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        await TreeRenderHelpers.RenderAsync(10).ConfigureAwait(true);

        var shown = viewModel.ShownItems.ToList();
        _ = shown.Should().HaveCount(ChildCount + 1);
        var realized = TreeRenderHelpers.RealizedRows(repeater, shown.Count);
        var offending = realized
            .Select(entry => (entry.Index, Problem: DescribeProblem(entry.Container, entry.Row, shown[entry.Index])))
            .Where(entry => entry.Problem is not null)
            .ToList();

        var offset = string.Create(CultureInfo.InvariantCulture, $"{scroller.VerticalOffset:F0} of {scroller.ScrollableHeight:F0}");
        _ = realized.Count.Should().BeGreaterThanOrEqualTo(
            (int)(scroller.ViewportHeight / tree.ItemRowHeight),
            $"the rows in view at offset {offset} are realized");
        _ = offending.Select(entry => entry.Index).Should().BeEmpty(
            $"every realized row of {realized.Count} shows its item at offset {offset}; offending rows: {string.Join("; ", offending.Select(entry => $"#{entry.Index} {entry.Problem}"))}");
    });

    private static string? DescribeProblem(FrameworkElement container, TreeItemControl row, ITreeItem item)
    {
        var label = row.FindDescendant<TextBlock>(block => string.Equals(block.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal));
        var name = label?.Inlines.Count > 0 ? ((Run)label.Inlines[0]).Text : null;
        var thumbnail = row.FindDescendant<ContentPresenter>(presenter => string.Equals(presenter.Name, TreeItemControl.ThumbnailPresenterPart, StringComparison.Ordinal));
        if (ReferenceEquals(row.ItemAdapter, item)
            && string.Equals(name, item.Label, StringComparison.Ordinal)
            && container.Visibility == Visibility.Visible && container.Opacity >= 1 && row.Opacity >= 1
            && row.ActualHeight > 0 && label is not null && label.ActualWidth > 0
            && thumbnail is not null && thumbnail.ActualWidth > 0)
        {
            return null;
        }

        return string.Create(
            CultureInfo.InvariantCulture,
            $"'{item.Label}': adapter '{row.ItemAdapter?.Label}', name '{name}', container {container.Visibility}/{container.Opacity}, "
            + $"row {row.Opacity}/{row.ActualHeight:F0}, label width {label?.ActualWidth:F0}, thumbnail width {thumbnail?.ActualWidth:F0}");
    }
}
