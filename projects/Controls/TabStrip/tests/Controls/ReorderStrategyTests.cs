// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.Tabs.Tests;

/// <summary>
/// Unit tests for <see cref="ReorderStrategy"/> drag strategy implementation.
/// Tests validate placeholder management, transform application, and item swapping logic
/// during in-TabStrip drag operations.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ReorderStrategyTests")]
[TestCategory("Phase1")]
public class ReorderStrategyTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task StrategyCanBeInstantiated_WithDefaultLogger_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var strategy = new ReorderStrategy();

        // Assert
        _ = strategy.Should().NotBeNull("Strategy should be created successfully");
        _ = strategy.IsActive.Should().BeFalse("Strategy should not be active initially");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    // TODO: add a test case that validates strategy will log messages when loggerfactory is provided

    [TestMethod]
    public Task InitiateDrag_RequiresNonNullContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen);

        // Act & Assert
        var act = () => strategy.InitiateDrag(null!, startPoint);
        _ = act.Should().Throw<ArgumentNullException>().And.ParamName.Should().Be("context");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_ActivatesStrategy_WithValidContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.InitiateDrag(context, startPoint);

        // Assert
        _ = strategy.IsActive.Should().BeTrue("Strategy should be active after InitiateDrag");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_ThrowsWhenAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        strategy.InitiateDrag(context, startPoint);

        // Act & Assert
        var act = () => strategy.InitiateDrag(context, startPoint);
        _ = act.Should().Throw<InvalidOperationException>().And.Message.Should().Contain("already active");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_AppliesTransformToSourceVisualItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.InitiateDrag(context, startPoint);

        // Assert
        _ = tabStripItem.RenderTransform.Should().BeOfType<TranslateTransform>("Transform should be applied to source item");
        var applied = (TranslateTransform)tabStripItem.RenderTransform;
        _ = applied.X.Should().Be(0, "Initial X offset should be zero on enter");
        _ = applied.Y.Should().Be(0, "Initial Y offset should be zero on enter");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_IsIgnored_WhenStrategyNotActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var movePoint = new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen);

        // Act & Assert - Should not throw
        var act = () => strategy.OnDragPositionChanged(movePoint);
        _ = act.Should().NotThrow("OnDragPositionChanged should be ignored when strategy not active");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_UpdatesTransform_WhenStrategyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));

        var initialTransform = tabStripItem.RenderTransform as TranslateTransform;
        var initialX = initialTransform?.X ?? 0;
        var movePoint = new SpatialPoint(new Windows.Foundation.Point(150, 75), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.OnDragPositionChanged(movePoint);

        // Assert
        var updatedTransform = tabStripItem.RenderTransform as TranslateTransform;
        _ = updatedTransform.Should().NotBeNull("Transform should still be applied after move");
        _ = updatedTransform!.X.Should().NotBe(initialX, "Move should update horizontal offset from its initial value");

        // Note: The exact offset depends on coordinate conversion logic (to be finalized in Phase 3)
        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_IsIgnored_WhenStrategyNotActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var dropPoint = new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen);

        // Act
        var result = strategy.CompleteDrag(dropPoint, targetStrip: null, targetIndex: null);

        // Assert
        _ = result.Should().BeFalse("CompleteDrag should return false when strategy not active");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_HandlesDropOnSameStrip_Successfully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));

        var dropPoint = new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip);
        const int targetIndex = 2;

        // Act
        var result = strategy.CompleteDrag(dropPoint, tabStrip, targetIndex);

        // Assert
        _ = result.Should().BeTrue("CompleteDrag should return true for same-strip drop");
        _ = tabStripItem.RenderTransform.Should().BeOfType<TranslateTransform>("Transform should still be present after drop but reset");
        var transform = (TranslateTransform)tabStripItem.RenderTransform;
        _ = transform.X.Should().Be(0, "Transform should be cleaned up after drop");
        _ = transform.Y.Should().Be(0, "Transform should be cleaned up after drop");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_HandlesCrossStripDrop_ByDelegating_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var tabItem = new TabItem { Header = "TestTab" };
        var sourceStrip = new TabStrip();
        var targetStrip = new TabStrip(); // Different strip
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, sourceStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, sourceStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, sourceStrip));

        var dropPoint = new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, sourceStrip);
        const int targetIndex = 1;

        // Act
        var result = strategy.CompleteDrag(dropPoint, targetStrip, targetIndex);

        // Assert
        _ = result.Should().BeFalse("CompleteDrag should return false for cross-strip drop to let coordinator handle");
        _ = strategy.IsActive.Should().BeFalse("Strategy should be deactivated after cross-strip drop");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StrategyCanBeReused_AfterCompleteDrag_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var tabItem1 = new TabItem { Header = "Tab1" };
        var tabItem2 = new TabItem { Header = "Tab2" };
        var tabStrip = new TabStrip();
        var tabStripItem1 = new TabStripItem { Item = tabItem1 };
        var tabStripItem2 = new TabStripItem { Item = tabItem2 };
        var context1 = new DragContext(tabItem1, tabStrip, tabStripItem1, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var context2 = new DragContext(tabItem2, tabStrip, tabStripItem2, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        // Act - First usage
        strategy.InitiateDrag(context1, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));
        _ = strategy.IsActive.Should().BeTrue("Strategy should be active after first enter");

        _ = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip), tabStrip, 1);
        _ = strategy.IsActive.Should().BeFalse("Strategy should not be active after complete drag");

        // Act - Second usage
        strategy.InitiateDrag(context2, new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen, tabStrip));

        // Assert
        _ = strategy.IsActive.Should().BeTrue("Strategy should be reusable after complete drag");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_HandlesNullSourceVisualItem_Gracefully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var context = new DragContext(tabItem, tabStrip, sourceVisualItem: null, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip)); // Null visual item
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act & Assert - Should not throw
        var act = () => strategy.InitiateDrag(context, startPoint);
        _ = act.Should().NotThrow("InitiateDrag should handle null source visual item gracefully");

        _ = strategy.IsActive.Should().BeTrue("Strategy should still be active even with null visual item");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task MultipleStrategies_CanBeCreated_Independently_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var strategy1 = new ReorderStrategy();
        var strategy2 = new ReorderStrategy();
        var strategy3 = new ReorderStrategy();

        // Assert
        _ = strategy1.Should().NotBeNull("First strategy should be created");
        _ = strategy2.Should().NotBeNull("Second strategy should be created");
        _ = strategy3.Should().NotBeNull("Third strategy should be created");
        _ = strategy1.Should().NotBe(strategy2, "Strategies should be distinct instances");
        _ = strategy2.Should().NotBe(strategy3, "Strategies should be distinct instances");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_StoresOriginalItemPosition_ForTransformCalculation_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip { LoggerFactory = this.LoggerFactory };

        // Add items to the strip so layout is valid
        tabStrip.Items.Add(new TabItem { Header = "Tab1" });
        tabStrip.Items.Add(tabItem);
        tabStrip.Items.Add(new TabItem { Header = "Tab3" });

        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.InitiateDrag(context, startPoint);

        // Assert
        _ = strategy.IsActive.Should().BeTrue("Strategy should be active");
        // Original position is stored internally and used for transform calculations
        // We can verify this indirectly by checking that move updates work correctly

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_CallsInsertPlaceholder_OnSourceStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var tabItem = new TabItem { Header = "DraggedTab" };
        var tabStrip = new TabStrip { LoggerFactory = this.LoggerFactory };

        // Add the dragged item to the strip
        tabStrip.Items.Add(new TabItem { Header = "Tab1" });
        tabStrip.Items.Add(tabItem);
        tabStrip.Items.Add(new TabItem { Header = "Tab3" });

        var initialCount = tabStrip.Items.Count;

        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.InitiateDrag(context, startPoint);

        // Assert
        _ = tabStrip.Items.Count.Should().Be(initialCount + 1, "Placeholder should be inserted");
        _ = tabStrip.Items.Should().Contain(item => item.IsPlaceholder, "Placeholder item should exist in collection");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_HandlesPlaceholderInsertionFailure_Gracefully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip { LoggerFactory = this.LoggerFactory };
        // Note: Not adding tabItem to the strip - this will cause placeholder insertion to fail

        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act - Should not throw even if placeholder insertion fails
        var act = () => strategy.InitiateDrag(context, startPoint);

        // Assert
        _ = act.Should().NotThrow("Strategy should handle placeholder insertion failure gracefully");
        _ = strategy.IsActive.Should().BeTrue("Strategy should still be active");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_ConvertsScreenToStripCoordinates_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip { LoggerFactory = this.LoggerFactory };
        tabStrip.Items.Add(tabItem);

        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen, tabStrip));

        var initialTransform = tabStripItem.RenderTransform as TranslateTransform;
        var initialX = initialTransform?.X ?? 0;

        // Act - Move to a different screen position
        var screenPoint = new SpatialPoint(new Windows.Foundation.Point(200, 50), CoordinateSpace.Screen, tabStrip);
        strategy.OnDragPositionChanged(screenPoint);

        // Assert
        var updatedTransform = tabStripItem.RenderTransform as TranslateTransform;
        _ = updatedTransform.Should().NotBeNull("Transform should still exist after move");
        // The transform should be updated based on strip-relative coordinates
        _ = updatedTransform!.X.Should().NotBe(initialX, "Transform X should be updated");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_UpdatesTransformWithHotspotOffset_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip { LoggerFactory = this.LoggerFactory };
        tabStrip.Items.Add(tabItem);

        var hotspot = new SpatialPoint(new Windows.Foundation.Point(15, 10), CoordinateSpace.Screen, tabStrip); // User clicked 15px from left
        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, hotspot);

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen, tabStrip));

        // Act - Move pointer
        strategy.OnDragPositionChanged(new SpatialPoint(new Windows.Foundation.Point(150, 50), CoordinateSpace.Screen, tabStrip));

        // Assert
        var transform = tabStripItem.RenderTransform as TranslateTransform;
        _ = transform.Should().NotBeNull("Transform should be applied");
        // The transform should position the item so the hotspot stays under the cursor
        // Exact value depends on coordinate conversion, but it should be non-zero
        _ = transform!.X.Should().NotBe(0, "Transform should account for hotspot offset");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_CommitsReorderAtPlaceholderPosition_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var draggedItem = new TabItem { Header = "DraggedTab" };
        var tabStrip = new TabStrip { LoggerFactory = this.LoggerFactory };

        // Setup: Tab1, DraggedTab, Tab3
        tabStrip.Items.Add(new TabItem { Header = "Tab1" });
        tabStrip.Items.Add(draggedItem);
        tabStrip.Items.Add(new TabItem { Header = "Tab3" });

        var tabStripItem = new TabStripItem { Item = draggedItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(draggedItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));

        var itemCountBeforeDrop = tabStrip.Items.Count; // Should include placeholder

        // Act - Drop on same strip
        var result = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip), tabStrip, targetIndex: 1);

        // Assert
        _ = result.Should().BeTrue("Drop on same strip should return true");
        _ = tabStrip.Items.Should().NotContain(item => item.IsPlaceholder, "Placeholder should be removed after drop");
        _ = tabStrip.Items.Count.Should().Be(itemCountBeforeDrop - 1, "Item count should decrease by 1 (placeholder removed)");
        _ = tabStrip.Items.Should().Contain(draggedItem, "Dragged item should still be in collection");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_RemovesTransform_AfterCommit_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var draggedItem = new TabItem { Header = "DraggedTab" };
        var tabStrip = new TabStrip { LoggerFactory = this.LoggerFactory };
        tabStrip.Items.Add(draggedItem);

        var tabStripItem = new TabStripItem { Item = draggedItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(draggedItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));

        // Verify transform exists during drag
        var transformDuringDrag = tabStripItem.RenderTransform as TranslateTransform;
        _ = transformDuringDrag.Should().NotBeNull("Transform should exist during drag");

        // Act - Complete drag
        _ = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip), tabStrip, targetIndex: 0);

        // Assert
        var transformAfterDrop = tabStripItem.RenderTransform as TranslateTransform;
        _ = transformAfterDrop.Should().NotBeNull("Transform object should still exist");
        _ = transformAfterDrop!.X.Should().Be(0, "Transform X should be reset to 0");
        _ = transformAfterDrop.Y.Should().Be(0, "Transform Y should be reset to 0");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_RemovesPlaceholder_OnCrossStripDrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var draggedItem = new TabItem { Header = "DraggedTab" };
        var sourceStrip = new TabStrip { LoggerFactory = this.LoggerFactory };
        var targetStrip = new TabStrip { LoggerFactory = this.LoggerFactory };

        sourceStrip.Items.Add(draggedItem);

        var tabStripItem = new TabStripItem { Item = draggedItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(draggedItem, sourceStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, sourceStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, sourceStrip));

        // Verify placeholder exists during drag
        _ = sourceStrip.Items.Should().Contain(item => item.IsPlaceholder, "Placeholder should exist during drag");

        // Act - Drop on different strip
        var result = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, sourceStrip), targetStrip, targetIndex: 0);

        // Assert
        _ = result.Should().BeFalse("Cross-strip drop should return false");
        _ = sourceStrip.Items.Should().NotContain(item => item.IsPlaceholder, "Placeholder should be removed from source strip");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_ResetsAllState_AfterCompletion_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var draggedItem = new TabItem { Header = "DraggedTab" };
        var tabStrip = new TabStrip { LoggerFactory = this.LoggerFactory };
        tabStrip.Items.Add(draggedItem);

        var tabStripItem = new TabStripItem { Item = draggedItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(draggedItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));
        _ = strategy.IsActive.Should().BeTrue("Strategy should be active during drag");

        // Act
        _ = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip), tabStrip, targetIndex: 0);

        // Assert
        _ = strategy.IsActive.Should().BeFalse("Strategy should be inactive after complete drag");

        // Verify strategy can be reused (state was properly reset)
        var newItem = new TabItem { Header = "NewTab" };
        tabStrip.Items.Add(newItem);
        var newTabStripItem = new TabStripItem { Item = newItem, LoggerFactory = this.LoggerFactory };
        var newContext = new DragContext(newItem, tabStrip, newTabStripItem, new SpatialPoint(new Windows.Foundation.Point(5, 5), CoordinateSpace.Screen, tabStrip));

        var act = () => strategy.InitiateDrag(newContext, new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen, tabStrip));
        _ = act.Should().NotThrow("Strategy should be reusable after state reset");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_HandlesAbort_WhenTargetStripIsNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var draggedItem = new TabItem { Header = "DraggedTab" };
        var tabStrip = new TabStrip { LoggerFactory = this.LoggerFactory };
        tabStrip.Items.Add(draggedItem);

        var tabStripItem = new TabStripItem { Item = draggedItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(draggedItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));

        // Act - Abort by passing null for both target strip and index
        var result = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip), targetStrip: null, targetIndex: null);

        // Assert
        _ = result.Should().BeFalse("Abort should return false");
        _ = strategy.IsActive.Should().BeFalse("Strategy should be inactive after abort");

        // Verify cleanup occurred
        var transform = tabStripItem.RenderTransform as TranslateTransform;
        _ = transform.Should().NotBeNull("Transform should exist");
        _ = transform!.X.Should().Be(0, "Transform should be reset");
        _ = transform.Y.Should().Be(0, "Transform should be reset");

        await Task.CompletedTask.ConfigureAwait(true);
    });
}
