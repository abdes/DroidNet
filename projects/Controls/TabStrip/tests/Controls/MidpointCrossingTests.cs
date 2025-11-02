// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.Tabs.Tests;

/// <summary>
/// Phase 3: Midpoint Crossing Tests
/// Tests for push/pop logic, dropIndex updates, and transform correctness during drag.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ReorderStrategy")]
[TestCategory("UITest")]
public sealed class MidpointCrossingTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task CrossMidpointForwardPushesAdjacentItemVisuallyAsync() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStrip, items, metrics) = await SetupTabStripWithVaryingWidths().ConfigureAwait(true);

        var draggedIndex = 1; // "Medium Tab"
        var nextItemIndex = 2; // "A Very Long Tab Header"

        // Start drag
        SimulatePointerPressed(tabStrip, items, draggedIndex, offsetX: 30, offsetY: 20);
        await WaitForRenderCompletion().ConfigureAwait(true);

        var nextMidpoint = GetItemMidpoint(metrics, nextItemIndex);

        // Act - Move pointer to cross the midpoint of the next item (using strip coordinates)
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(nextMidpoint + 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Verify the visual outcome: next item should be translated
        var transform = GetContentTransform(tabStrip, items, nextItemIndex);
        transform.Should().NotBeNull("pushed item should have a transform");
        transform!.X.Should().BeLessThan(0, "pushed item should slide left (negative X offset)");
    });

    [TestMethod]
    public Task CrossMidpointForwardTranslatesPushedContentToCoverPreviousShellAsync() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStrip, items, metrics) = await SetupTabStripWithVaryingWidths().ConfigureAwait(true);

        var draggedIndex = 1;
        var pushedIndex = 2;

        SimulatePointerPressed(tabStrip, items, draggedIndex, offsetX: 30, offsetY: 20);
        await WaitForRenderCompletion().ConfigureAwait(true);

        var draggedShellLeft = metrics[draggedIndex].Left;
        var pushedOriginalLeft = metrics[pushedIndex].Left;
        var pushedMidpoint = GetItemMidpoint(metrics, pushedIndex);

        // Act - Cross midpoint to push item (using strip coordinates)
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(pushedMidpoint + 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        var transform = GetContentTransform(tabStrip, items, pushedIndex);
        transform.Should().NotBeNull("pushed item should have transform");

        var expectedOffset = draggedShellLeft - pushedOriginalLeft;
        expectedOffset.Should().BeLessThan(0, "pushed item should slide left (negative offset)");

        transform!.X.Should().BeApproximately(expectedOffset, 0.5,
            $"pushed item should slide to cover dragged shell: draggedLeft={draggedShellLeft:F2}, pushedLeft={pushedOriginalLeft:F2}");
    });

    [TestMethod]
    public Task CrossMidpointForwardMultiplePushesCascadesCorrectlyAsync() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStrip, items, metrics) = await SetupTabStripWithVaryingWidths().ConfigureAwait(true);

        var draggedIndex = 0; // "Short"

        SimulatePointerPressed(tabStrip, items, draggedIndex, offsetX: 20, offsetY: 20);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Push items 1, 2, 3 in sequence (using strip coordinates)
        var item1Midpoint = GetItemMidpoint(metrics, 1);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(item1Midpoint + 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);

        var item2Midpoint = GetItemMidpoint(metrics, 2);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(item2Midpoint + 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);

        var item3Midpoint = GetItemMidpoint(metrics, 3);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(item3Midpoint + 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        // Item 1 should cover dragged item's shell (index 0)
        var transform1 = GetContentTransform(tabStrip, items, 1);
        var expected1 = metrics[0].Left - metrics[1].Left;
        transform1!.X.Should().BeApproximately(expected1, 0.5, "item 1 should cover item 0's shell");

        // Item 2 should cover item 1's shell (NOT item 0's shell - cascading)
        var transform2 = GetContentTransform(tabStrip, items, 2);
        var expected2 = metrics[1].Left - metrics[2].Left;
        transform2!.X.Should().BeApproximately(expected2, 0.5, "item 2 should cover item 1's shell (cascade)");

        // Item 3 should cover item 2's shell
        var transform3 = GetContentTransform(tabStrip, items, 3);
        var expected3 = metrics[2].Left - metrics[3].Left;
        transform3!.X.Should().BeApproximately(expected3, 0.5, "item 3 should cover item 2's shell (cascade)");
    });

    [TestMethod]
    public Task CrossMidpointBackwardRestoresPoppedItemVisuallyAsync() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStrip, items, metrics) = await SetupTabStripWithVaryingWidths().ConfigureAwait(true);

        var draggedIndex = 1;
        var pushedIndex = 2;

        SimulatePointerPressed(tabStrip, items, draggedIndex, offsetX: 30, offsetY: 20);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Push item 2 (using strip coordinates)
        var item2Midpoint = GetItemMidpoint(metrics, pushedIndex);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(item2Midpoint + 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Verify it's pushed
        var transformBefore = GetContentTransform(tabStrip, items, pushedIndex);
        transformBefore!.X.Should().NotBe(0, "item should be pushed before reversal");

        // Act - Move back to cross midpoint backward (pop) (using strip coordinates)
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(item2Midpoint - 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Verify visual outcome: item should be restored
        var transformAfter = GetContentTransform(tabStrip, items, pushedIndex);
        transformAfter!.X.Should().Be(0, "popped item should be visually restored (no offset)");
    });

    [TestMethod]
    public Task CrossMidpointBackwardMultiplePopsRestoresAllItemsVisuallyAsync() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStrip, items, metrics) = await SetupTabStripWithVaryingWidths().ConfigureAwait(true);

        var draggedIndex = 0;

        SimulatePointerPressed(tabStrip, items, draggedIndex, offsetX: 20, offsetY: 20);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Push items 1, 2, 3 (using strip coordinates)
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(GetItemMidpoint(metrics, 1) + 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(GetItemMidpoint(metrics, 2) + 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(GetItemMidpoint(metrics, 3) + 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Pop them in reverse order (using strip coordinates)
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(GetItemMidpoint(metrics, 3) - 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);
        var transform3After = GetContentTransform(tabStrip, items, 3);
        transform3After!.X.Should().Be(0, "item 3 should be visually restored after first pop");

        tabStrip.SimulateDragMove(new Windows.Foundation.Point(GetItemMidpoint(metrics, 2) - 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);
        var transform2After = GetContentTransform(tabStrip, items, 2);
        transform2After!.X.Should().Be(0, "item 2 should be visually restored after second pop");

        tabStrip.SimulateDragMove(new Windows.Foundation.Point(GetItemMidpoint(metrics, 1) - 1, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);
        var transform1After = GetContentTransform(tabStrip, items, 1);
        transform1After!.X.Should().Be(0, "item 1 should be visually restored after third pop");

        // Assert - All items back to original positions visually
        GetContentTransform(tabStrip, items, 1)!.X.Should().Be(0);
        GetContentTransform(tabStrip, items, 2)!.X.Should().Be(0);
        GetContentTransform(tabStrip, items, 3)!.X.Should().Be(0);
    });

    [TestMethod]
    public Task CrossMidpointRapidBackAndForthMaintainsCorrectStackAsync() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStrip, items, metrics) = await SetupTabStripWithVaryingWidths().ConfigureAwait(true);

        var draggedIndex = 1;

        SimulatePointerPressed(tabStrip, items, draggedIndex, offsetX: 30, offsetY: 20);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Rapid sequence: push 2, push 3, pop 3, push 3, push 4, pop 4, pop 3 (using strip coordinates)
        var mid2 = GetItemMidpoint(metrics, 2);
        var mid3 = GetItemMidpoint(metrics, 3);
        var mid4 = GetItemMidpoint(metrics, 4);

        tabStrip.SimulateDragMove(new Windows.Foundation.Point(mid2 + 1, 20));  // Push 2
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(mid3 + 1, 20));  // Push 3
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(mid3 - 1, 20));  // Pop 3
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(mid3 + 1, 20));  // Push 3 again
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(mid4 + 1, 20));  // Push 4
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(mid4 - 1, 20));  // Pop 4
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStrip.SimulateDragMove(new Windows.Foundation.Point(mid3 - 1, 20));  // Pop 3
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should be back to just item 2 pushed
        var transform2 = GetContentTransform(tabStrip, items, 2);
        transform2!.X.Should().NotBe(0, "item 2 should still be pushed");

        var transform3 = GetContentTransform(tabStrip, items, 3);
        transform3!.X.Should().Be(0, "item 3 should be restored");

        var transform4 = GetContentTransform(tabStrip, items, 4);
        transform4!.X.Should().Be(0, "item 4 should be restored");
    });

    private async Task<(TestableTabStrip TabStrip, ObservableCollection<TabItem> Items, Dictionary<int, ItemMetrics> Metrics)> SetupTabStripWithVaryingWidths()
    {
        var tabStrip = new TestableTabStrip
        {
            Width = 2000, // Wide enough to fit all 6 items without virtualization
            Height = 40,
            TabWidthPolicy = TabWidthPolicy.Auto,
            LoggerFactory = this.LoggerFactory,
        };

        // Set up coordinator with drag service
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService, this.LoggerFactory);
        tabStrip.DragCoordinator = coordinator;

        // Create items with different header lengths (will result in different widths)
        var items = new ObservableCollection<TabItem>
        {
            new() { Header = "Short" },        // Item 0
            new() { Header = "Medium Tab" },   // Item 1
            new() { Header = "A Very Long Tab Header" }, // Item 2
            new() { Header = "Tab" },          // Item 3
            new() { Header = "Another Medium" }, // Item 4
            new() { Header = "X" },            // Item 5
        };

        foreach (var item in items)
        {
            tabStrip.Items.Add(item);
        }

        // Load the control and wait for layout
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Capture layout metrics
        var metrics = await CaptureLayoutMetrics(tabStrip, items).ConfigureAwait(true);

        return (tabStrip, items, metrics);
    }

    private static async Task<Dictionary<int, ItemMetrics>> CaptureLayoutMetrics(
        TestableTabStrip tabStrip,
        ObservableCollection<TabItem> items)
    {
        // Wait for layout to stabilize
        await WaitForRenderCompletion().ConfigureAwait(true);

        var repeater = tabStrip.GetRegularItemsRepeater();
        repeater.Should().NotBeNull("repeater should be available after load");

        var metrics = new Dictionary<int, ItemMetrics>();

        for (var i = 0; i < items.Count; i++)
        {
            var item = items[i];

            // ItemsRepeater may virtualize items. To ensure ALL items are realized for
            // metric capture, we force realization using GetOrCreateElement.
            // This is necessary because tests need accurate positions for all items,
            // regardless of viewport visibility.
            Grid? wrapperGrid = null;

            // First try to find existing realized element
            for (var j = 0; j < repeater!.ItemsSourceView.Count; j++)
            {
                if (repeater.TryGetElement(j) is Grid grid && grid.DataContext == item)
                {
                    wrapperGrid = grid;
                    break;
                }
            }

            // If not realized, force it using GetOrCreateElement (defeats virtualization)
            if (wrapperGrid == null)
            {
                var element = repeater.GetOrCreateElement(i);
                await WaitForRenderCompletion().ConfigureAwait(true);

                // Now try again to find it
                for (var j = 0; j < repeater.ItemsSourceView.Count; j++)
                {
                    if (repeater.TryGetElement(j) is Grid g && g.DataContext == item)
                    {
                        wrapperGrid = g;
                        break;
                    }
                }
            }

            wrapperGrid.Should().NotBeNull($"wrapper grid for item {i} should be realized after forcing");

            var width = wrapperGrid!.ActualWidth;

            // Get position relative to TabStrip
            var transform = wrapperGrid.TransformToVisual(tabStrip);
            var position = transform.TransformPoint(new Windows.Foundation.Point(0, 0));

            metrics[i] = new ItemMetrics
            {
                ItemIndex = i,
                Width = width,
                Left = position.X,
                Header = item.Header?.ToString() ?? string.Empty,
            };
        }

        return metrics;
    }

    private static double GetItemMidpoint(Dictionary<int, ItemMetrics> metrics, int itemIndex)
    {
        metrics.Should().ContainKey(itemIndex);
        var m = metrics[itemIndex];
        return m.Left + (m.Width / 2.0);
    }

    private static void SimulatePointerPressed(
        TestableTabStrip tabStrip,
        ObservableCollection<TabItem> items,
        int itemIndex,
        double offsetX = 0,
        double offsetY = 20)
    {
        var item = items[itemIndex];
        var repeater = tabStrip.GetRegularItemsRepeater();
        repeater.Should().NotBeNull("repeater should be available");

        // Find the TabStripItem directly from the repeater
        TabStripItem? tabStripItem = null;
        for (var i = 0; i < repeater!.ItemsSourceView.Count; i++)
        {
            if (repeater.TryGetElement(i) is Grid grid && grid.DataContext == item)
            {
                tabStripItem = grid.FindDescendant<TabStripItem>();
                break;
            }
        }

        tabStripItem.Should().NotBeNull($"TabStripItem for item {itemIndex} should be found");

        // Get item position
        var transform = tabStripItem!.TransformToVisual(tabStrip);
        var itemPosition = transform.TransformPoint(new Windows.Foundation.Point(0, 0));

        var pointerPoint = new Windows.Foundation.Point(itemPosition.X + offsetX, itemPosition.Y + offsetY);

        // Simulate pointer press
        tabStrip.HandlePointerPressed(tabStripItem, pointerPoint);

        // Simulate pointer move past drag threshold to trigger BeginDrag
        // Move just enough to exceed the 5-pixel threshold (use 10 pixels for safety)
        var movePoint = new Windows.Foundation.Point(pointerPoint.X + 10, pointerPoint.Y);
        tabStrip.HandlePointerMoved(movePoint);

        // IMPORTANT: The drag is now active with initial cursor at (pointerPoint.X + 10)
        // Subsequent SimulateDragMove calls should account for this when calculating deltas
    }

    private static TranslateTransform? GetContentTransform(
        TestableTabStrip tabStrip,
        ObservableCollection<TabItem> items,
        int itemIndex)
    {
        var repeater = tabStrip.GetRegularItemsRepeater();
        if (repeater is null)
        {
            return null;
        }

        var item = items[itemIndex];

        // Find wrapper Grid
        for (var i = 0; i < repeater.ItemsSourceView.Count; i++)
        {
            if (repeater.TryGetElement(i) is Grid grid && grid.DataContext == item)
            {
                return grid.RenderTransform as TranslateTransform;
            }
        }

        return null;
    }

    private static async Task WaitForRenderCompletion() =>
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { })
            .ConfigureAwait(true);

    private sealed class ItemMetrics
    {
        public required int ItemIndex { get; init; }
        public required double Width { get; init; }
        public required double Left { get; init; }
        public required string Header { get; init; }
    }
}
