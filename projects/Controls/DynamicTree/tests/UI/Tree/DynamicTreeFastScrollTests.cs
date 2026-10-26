// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
/// Expanding a row with hundreds of children and scrolling down at once, as a fast mouse wheel does, leaves every row
/// the repeater realizes showing its own item: its name and its glyph.
/// </summary>
[TestClass]
public sealed class DynamicTreeFastScrollTests : VisualUserInterfaceTests
{
    private const int ChildCount = 700;

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ExpandingAndScrollingAtOnce_RealizesEveryRowWithItsItem(bool dimmed) => EnqueueAsync(async () =>
    {
        using var viewModel = new TestViewModel();
        var parent = new LazyTreeItem("Parent", ChildCount, isRoot: true);
        await viewModel.InitializeAsync(parent).ConfigureAwait(true);
        var tree = new DynamicTree { ViewModel = viewModel, Width = 320, Height = 320 };
        await LoadTestContentAsync(tree).ConfigureAwait(true);
        var repeater = tree.FindDescendant<ItemsRepeater>()!;
        var scroller = tree.FindDescendant<ScrollView>()!;

        await viewModel.ExpandItemAsync(parent).ConfigureAwait(true);
        if (dimmed)
        {
            foreach (var child in viewModel.ShownItems.OfType<LazyTreeItem>().Skip(1))
            {
                child.IsDimmed = true;
                child.Annotation = "Source.glb";
            }
        }

        // A fast wheel: each frame scrolls further down before the previous rows have settled.
        tree.UpdateLayout();
        for (var step = 1; step <= 10; step++)
        {
            _ = scroller.ScrollTo(0, scroller.ScrollableHeight * step / 10, new ScrollingScrollOptions(ScrollingAnimationMode.Enabled));
            await RenderAsync().ConfigureAwait(true);
        }

        // The wheel stops at the end, whatever the animations still had in flight.
        _ = scroller.ScrollTo(0, scroller.ScrollableHeight, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));

        for (var frame = 0; frame < 10; frame++)
        {
            await RenderAsync().ConfigureAwait(true);
        }

        var shown = viewModel.ShownItems.ToList();
        _ = shown.Should().HaveCount(ChildCount + 1);
        var realized = 0;
        var problems = new List<string>();
        for (var index = 0; index < shown.Count; index++)
        {
            if (repeater.TryGetElement(index) is not FrameworkElement container
                || container.FindName(DynamicTree.TreeItemPart) is not TreeItemControl row)
            {
                continue;
            }

            realized++;
            var label = row.FindDescendant<TextBlock>(block => string.Equals(block.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal));
            var name = label?.Inlines.Count > 0 ? ((Run)label.Inlines[0]).Text : null;
            var thumbnail = row.FindDescendant<ContentPresenter>(presenter => string.Equals(presenter.Name, "PartThumbnailPresenter", StringComparison.Ordinal));
            if (!ReferenceEquals(row.ItemAdapter, shown[index])
                || !string.Equals(name, shown[index].Label, StringComparison.Ordinal)
                || container.Visibility != Visibility.Visible || container.Opacity < 1 || row.Opacity < 1
                || row.ActualHeight <= 0 || label is null || label.ActualWidth <= 0
                || thumbnail is null || thumbnail.ActualWidth <= 0)
            {
                problems.Add(string.Create(
                    CultureInfo.InvariantCulture,
                    $"#{index} '{shown[index].Label}': adapter '{row.ItemAdapter?.Label}', name '{name}', container {container.Visibility}/{container.Opacity}, "
                    + $"row {row.Opacity}/{row.ActualHeight:F0}, label width {label?.ActualWidth:F0}, thumbnail width {thumbnail?.ActualWidth:F0}"));
            }
        }

        _ = realized.Should().BeGreaterThanOrEqualTo(
            (int)(scroller.ViewportHeight / tree.ItemRowHeight),
            string.Create(CultureInfo.InvariantCulture, $"the rows in view at offset {scroller.VerticalOffset:F0} of {scroller.ScrollableHeight:F0} are realized"));
        _ = problems.Should().BeEmpty($"every realized row of {realized} shows its item at offset {scroller.VerticalOffset:F0}");
    });

    private static async Task RenderAsync()
        => _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
}
