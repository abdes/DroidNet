// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// The row height, font size and icon size set on the tree are pushed to the rows it realizes, and a large tree realizes
/// only the rows around its viewport.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed class DynamicTreeDensityWorkloadTests : VisualUserInterfaceTests
{
    private const double ViewportHeight = 320;

    [TestMethod]
    [DataRow(32d, 14d, 18d)]
    [DataRow(40d, 16d, 24d)]
    public Task RealizedRows_TakeRowHeightFontSizeAndIconSizeFromTheTree_Async(double rowHeight, double fontSize, double iconSize)
        => EnqueueAsync(async () =>
        {
            using var viewModel = new TestViewModel();
            var root = new LazyTreeItem("Root", 50, isRoot: true) { IsExpanded = true };
            await viewModel.InitializeAsync(root).ConfigureAwait(true);
            var tree = new DynamicTree
            {
                ViewModel = viewModel,
                Width = 400,
                Height = ViewportHeight,
                ItemRowHeight = rowHeight,
                ItemFontSize = fontSize,
                ItemIconSize = iconSize,
            };
            await LoadTestContentAsync(tree).ConfigureAwait(true);
            var repeater = tree.FindDescendant<ItemsRepeater>()!;

            AssertRealizedRows(repeater, viewModel.ShownItemsCount, rowHeight, fontSize, iconSize);

            // The rows already realized follow a later change of the tree.
            tree.ItemRowHeight = rowHeight + 8;
            tree.UpdateLayout();
            await TreeRenderHelpers.RenderAsync().ConfigureAwait(true);
            AssertRealizedRows(repeater, viewModel.ShownItemsCount, rowHeight + 8, fontSize, iconSize);
        });

    [TestMethod]
    public Task LargeTree_RealizesOnlyTheRowsAroundTheViewport_Async() => EnqueueAsync(async () =>
    {
        const double rowHeight = 32;
        using var viewModel = new TestViewModel();
        var root = new LazyTreeItem("Root", 200, isRoot: true) { IsExpanded = true };
        await viewModel.InitializeAsync(root).ConfigureAwait(true);
        var tree = new DynamicTree { ViewModel = viewModel, Width = 400, Height = ViewportHeight, ItemRowHeight = rowHeight };
        await LoadTestContentAsync(tree).ConfigureAwait(true);
        var repeater = tree.FindDescendant<ItemsRepeater>()!;
        var scroller = tree.FindDescendant<ScrollView>()!;
        var count = viewModel.ShownItemsCount;
        _ = scroller.ScrollableHeight.Should().BeGreaterThan(0, "the rows overflow the viewport");

        // The repeater keeps a few viewports of recycled rows around the one in view.
        var realizationLimit = ((int)Math.Ceiling(scroller.ViewportHeight / rowHeight) * 6) + 4;
        var maximumRealized = TreeRenderHelpers.RealizedRows(repeater, count).Count;
        foreach (var offset in new[] { scroller.ScrollableHeight, scroller.ScrollableHeight / 2, 0d })
        {
            await TreeRenderHelpers.ScrollToAndRenderAsync(scroller, offset).ConfigureAwait(true);
            maximumRealized = Math.Max(maximumRealized, TreeRenderHelpers.RealizedRows(repeater, count).Count);
        }

        _ = maximumRealized.Should().BePositive();
        _ = maximumRealized.Should().BeLessThanOrEqualTo(realizationLimit);
    });

    private static void AssertRealizedRows(ItemsRepeater repeater, int count, double rowHeight, double fontSize, double iconSize)
    {
        var realized = TreeRenderHelpers.RealizedRows(repeater, count);
        _ = realized.Should().NotBeEmpty();
        foreach (var (_, _, row) in realized)
        {
            _ = row.ItemRowHeight.Should().Be(rowHeight);
            _ = row.ItemFontSize.Should().Be(fontSize);
            _ = row.ItemIconSize.Should().Be(iconSize);
            var label = row.FindDescendant<TextBlock>(item => string.Equals(item.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal))!;
            _ = label.FontSize.Should().Be(fontSize);
        }
    }
}
