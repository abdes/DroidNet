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
/// Phase 1: Drag Initiation Tests
/// Tests for InitiateDrag, state initialization, and initial transform application.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ReorderStrategy")]
[TestCategory("UITest")]
public sealed class DragInitiationTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task InitiateDragSetsDraggedItemAsync() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStrip, items) = await SetupTabStripWithVaryingWidths().ConfigureAwait(true);

        var dragItemIndex = 1; // "Medium Tab"

        // Act
        SimulatePointerPressed(tabStrip, items, dragItemIndex, offsetX: 30, offsetY: 20);

        // Assert - Verify observable state
        tabStrip.DraggedItem.Should().NotBeNull("dragged item should be set");
        tabStrip.DraggedItem!.Item.Should().Be(items[dragItemIndex], "correct item should be dragged");
    });

    [TestMethod]
    public Task InitiateDragDoesNotMutateItemsCollectionAsync() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStrip, items) = await SetupTabStripWithVaryingWidths().ConfigureAwait(true);

        var initialCount = items.Count;
        var initialOrder = items.ToList();

        var dragItemIndex = 1;

        // Act
        SimulatePointerPressed(tabStrip, items, dragItemIndex, offsetX: 30, offsetY: 20);

        // Assert
        items.Should().HaveCount(initialCount, "items collection should not change on drag start");

        for (var i = 0; i < items.Count; i++)
        {
            items[i].Should().Be(initialOrder[i], $"item at index {i} should be unchanged");
        }
    });

    [TestMethod]
    public Task DragAppliesTransformToDraggedItemAsync() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStrip, items, metrics) = await SetupTabStripWithVaryingWidthsAndMetrics().ConfigureAwait(true);

        var dragItemIndex = 1; // "Medium Tab"
        var dragOffsetX = 30.0;

        // Act - Simulate pointer press to initialize coordinator and start drag
        SimulatePointerPressed(tabStrip, items, dragItemIndex, offsetX: dragOffsetX, offsetY: 20);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Verify drag was initiated
        tabStrip.DraggedItem.Should().NotBeNull("drag should have started");

        // Move pointer to trigger transform update (using strip coordinates)
        var draggedItemLeft = metrics[dragItemIndex].Left;
        var newPointerX = draggedItemLeft + dragOffsetX + 50; // Move 50px to the right

        tabStrip.SimulateDragMove(new Windows.Foundation.Point(newPointerX, 20));
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Dragged item should have a transform
        var transform = GetContentTransform(tabStrip, items, dragItemIndex);
        transform.Should().NotBeNull("dragged item should have a transform applied");
        transform!.X.Should().NotBe(0, "dragged item should be visually offset from its original position");
    });

    private async Task<(TestableTabStrip TabStrip, ObservableCollection<TabItem> Items)> SetupTabStripWithVaryingWidths()
    {
        var tabStrip = new TestableTabStrip
        {
            Width = 900,
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
        await WaitForRenderCompletion().ConfigureAwait(true); // Extra wait to ensure visual tree is fully realized

        return (tabStrip, items);
    }

    private async Task<(TestableTabStrip TabStrip, ObservableCollection<TabItem> Items, Dictionary<int, ItemMetrics> Metrics)> SetupTabStripWithVaryingWidthsAndMetrics()
    {
        var (tabStrip, items) = await this.SetupTabStripWithVaryingWidths().ConfigureAwait(true);
        var metrics = await CaptureLayoutMetrics(tabStrip, items).ConfigureAwait(true);
        return (tabStrip, items, metrics);
    }

    private static async Task<Dictionary<int, ItemMetrics>> CaptureLayoutMetrics(
        TestableTabStrip tabStrip,
        ObservableCollection<TabItem> items)
    {
        await WaitForRenderCompletion().ConfigureAwait(true);

        var repeater = tabStrip.GetRegularItemsRepeater();
        repeater.Should().NotBeNull("repeater should be available after load");

        var metrics = new Dictionary<int, ItemMetrics>();

        for (var i = 0; i < items.Count; i++)
        {
            var item = items[i];

            // ItemsRepeater may virtualize items. Force realization using GetOrCreateElement.
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
        // Move 20 pixels to the right (exceeds 10-pixel threshold)
        var movePoint = new Windows.Foundation.Point(pointerPoint.X + 20, pointerPoint.Y);
        tabStrip.HandlePointerMoved(movePoint);
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
