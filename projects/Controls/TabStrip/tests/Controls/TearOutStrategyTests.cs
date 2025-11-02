// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Tabs.Tests;

/// <summary>
/// Unit tests for <see cref="TearOutStrategy"/> drag strategy implementation.
/// Tests validate DragVisualService integration, header capture, and cross-window coordination
/// during TearOut mode drag operations.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("TearOutStrategyTests")]
[TestCategory("Phase1")]
public class TearOutStrategyTests : VisualUserInterfaceTests
{
    private readonly MockDragVisualService dragService;
    private readonly TabDragCoordinator coordinator;

    public TearOutStrategyTests()
    {
        this.dragService = new();
        this.coordinator = new(this.dragService);
    }

    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task StrategyCanBeInstantiated_WithDragService_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);

        // Assert
        _ = strategy.Should().NotBeNull("Strategy should be created successfully");
        _ = strategy.IsActive.Should().BeFalse("Strategy should not be active initially");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    // TODO: add a test case that validates strategy will log messages when loggerfactory is provided
    [TestMethod]
    public Task Constructor_RequiresNonNullDragService_Async() => EnqueueAsync(async () =>
    {
        // Act & Assert
        var act = () => new TearOutStrategy(null!, this.coordinator, this.LoggerFactory);
        _ = act.Should().Throw<ArgumentNullException>().And.ParamName.Should().Be("dragService");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_RequiresNonNullContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabStrip = new TabStrip();
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        // Act & Assert
        var act = () => strategy.InitiateDrag(null!, startPoint);
        _ = act.Should().Throw<ArgumentNullException>().And.ParamName.Should().Be("context");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_ActivatesStrategy_AndStartsDragSession_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.InitiateDrag(context, startPoint);

        // Assert
        _ = strategy.IsActive.Should().BeTrue("Strategy should be active after OnEnter");
        _ = this.dragService.StartSessionCallCount.Should().Be(1, "DragService.StartSession should be called once");
        _ = this.dragService.UpdatePositionCallCount.Should().Be(1, "DragService.UpdatePosition should be called with initial position");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_ThrowsWhenAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
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
    public Task InitiateDrag_CreatesDescriptorWithHeaderAndTitle_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTabTitle" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.InitiateDrag(context, startPoint);

        // Assert
        _ = this.dragService.LastDescriptor.Should().NotBeNull("Descriptor should be created");
        _ = this.dragService.LastDescriptor!.Title.Should().Be("TestTabTitle", "Descriptor should use TabItem header as title");
        _ = this.dragService.LastDescriptor.RequestedSize.Width.Should().Be(300, "Default requested width should be 300");
        _ = this.dragService.LastDescriptor.RequestedSize.Height.Should().Be(150, "Default requested height should be 150");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_IsIgnored_WhenStrategyNotActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var movePoint = new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen, null);

        // Act & Assert - Should not throw
        var act = () => strategy.OnDragPositionChanged(movePoint);
        _ = act.Should().NotThrow("OnMove should be ignored when strategy not active");
        _ = this.dragService.UpdatePositionCallCount.Should().Be(0, "UpdatePosition should not be called when inactive");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_UpdatesOverlayPosition_WhenStrategyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));
        var initialUpdateCount = this.dragService.UpdatePositionCallCount;

        var movePoint = new SpatialPoint(new Windows.Foundation.Point(150, 75), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.OnDragPositionChanged(movePoint);

        // Assert
        _ = this.dragService.UpdatePositionCallCount.Should().Be(initialUpdateCount + 1, "UpdatePosition should be called once more");
        _ = this.dragService.LastUpdatePosition.Should().Be(movePoint.Point, "Last position should match the move point");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_IsIgnored_WhenStrategyNotActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var dropPoint = new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, null);

        // Act
        var result = strategy.CompleteDrag(dropPoint, targetStrip: null, targetIndex: null);

        // Assert
        _ = result.Should().BeFalse("CompleteDrag should return false when strategy not active");
        _ = this.dragService.EndSessionCallCount.Should().Be(0, "EndSession should not be called when not active");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_HandlesDropOnTabStrip_Successfully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var sourceStrip = new TabStrip();
        var targetStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, sourceStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, sourceStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, sourceStrip));

        var dropPoint = new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, sourceStrip);
        const int targetIndex = 2;

        // Act
        var result = strategy.CompleteDrag(dropPoint, targetStrip, targetIndex);

        // Assert
        _ = result.Should().BeTrue("OnDrop should return true for TabStrip drop");
        _ = strategy.IsActive.Should().BeFalse("Strategy should be deactivated after drop");
        _ = this.dragService.EndSessionCallCount.Should().Be(1, "Drag session should be ended");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_HandlesDropOutsideTabStrip_ByDelegating_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));

        var dropPoint = new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip);

        // Act - Drop without target strip (outside any TabStrip)
        var result = strategy.CompleteDrag(dropPoint, targetStrip: null, targetIndex: null);

        // Assert
        _ = result.Should().BeFalse("OnDrop should return false for outside drop to let coordinator handle TearOut event");
        _ = strategy.IsActive.Should().BeFalse("Strategy should be deactivated after drop");
        _ = this.dragService.EndSessionCallCount.Should().Be(1, "Drag session should be ended");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StrategyCanBeReused_AfterCompleteDrag_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
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

        var firstSessionCount = this.dragService.StartSessionCallCount;

        // Act - Second usage
        strategy.InitiateDrag(context2, new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen, tabStrip));

        // Assert
        _ = strategy.IsActive.Should().BeTrue("Strategy should be reusable after complete drag");
        _ = this.dragService.StartSessionCallCount.Should().Be(firstSessionCount + 1, "New drag session should be started");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_HandlesNullSourceVisualItem_Gracefully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var context = new DragContext(tabItem, tabStrip, sourceVisualItem: null, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip)); // Null visual item
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act & Assert - Should not throw
        var act = () => strategy.InitiateDrag(context, startPoint);
        _ = act.Should().NotThrow("OnEnter should handle null source visual item gracefully");

        _ = strategy.IsActive.Should().BeTrue("Strategy should still be active even with null visual item");
        _ = this.dragService.StartSessionCallCount.Should().Be(1, "Drag session should still be started");
        _ = this.dragService.LastDescriptor.Should().NotBeNull("Descriptor should still be created even without a source visual");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task MultipleStrategies_CanShareSameDragService_Async() => EnqueueAsync(async () =>
    {
        // Arrange

        // Act
        var strategy1 = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var strategy2 = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var strategy3 = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);

        // Assert
        _ = strategy1.Should().NotBeNull("First strategy should be created");
        _ = strategy2.Should().NotBeNull("Second strategy should be created");
        _ = strategy3.Should().NotBeNull("Third strategy should be created");
        _ = strategy1.Should().NotBe(strategy2, "Strategies should be distinct instances");
        _ = strategy2.Should().NotBe(strategy3, "Strategies should be distinct instances");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_AlwaysEndsSession_EvenOnFailure_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));
        var dropPoint = new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip);

        // Act - Both success and failure cases
        _ = strategy.CompleteDrag(dropPoint, tabStrip, 1); // Success case

        // Reset and test failure case
        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));
        _ = strategy.CompleteDrag(dropPoint, targetStrip: null, targetIndex: null); // Failure case (outside drop)

        // Assert
        _ = this.dragService.EndSessionCallCount.Should().Be(2, "EndSession should be called for both success and failure cases");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_CapturesHeaderImage_WhenSourceVisualItemExists_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.InitiateDrag(context, startPoint);

        // Assert
        // Note: TabStripItem is not realized in the visual tree in this test, so capture returns null.
        // This verifies we don't throw and descriptor is still created.
        _ = this.dragService.LastDescriptor.Should().NotBeNull("Descriptor should be created");

        // HeaderImage is null because the visual isn't realized (ActualWidth/Height <= 0)
        _ = this.dragService.LastDescriptor!.HeaderImage.Should().BeNull("Header capture requires realized visuals");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_HandlesHeaderCaptureFailure_Gracefully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        // Act - Even if header capture fails, strategy should continue
        var act = () => strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));

        // Assert
        _ = act.Should().NotThrow("Strategy should handle header capture failure gracefully");
        _ = strategy.IsActive.Should().BeTrue("Strategy should be active despite header capture failure");
        _ = this.dragService.StartSessionCallCount.Should().Be(1, "Drag session should still start");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_RequestsPreviewImage_FromApplication_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.InitiateDrag(context, startPoint);

        // Assert
        // Note: Current implementation has TODO for TabDragImageRequest event (Phase 4)
        // This test verifies the method is called without throwing
        _ = strategy.IsActive.Should().BeTrue("Strategy should be active");

        // Event will be implemented in Phase 4, but the call should not throw
        _ = this.dragService.LastDescriptor.Should().NotBeNull("Descriptor should be created for preview request");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_UsesCorrectHotspot_FromContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };

        var expectedHotspot = new SpatialPoint(new Windows.Foundation.Point(20, 15), CoordinateSpace.Screen, tabStrip); // Specific hotspot
        var context = new DragContext(tabItem, tabStrip, tabStripItem, expectedHotspot);
        var startPoint = new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen, tabStrip);

        // Act
        strategy.InitiateDrag(context, startPoint);

        // Assert
        _ = this.dragService.StartSessionHotspot.Should().Be(expectedHotspot.Point, "Hotspot passed to drag service should match context hotspot");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_StartsSessionWithPhysicalInitialPoint_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        var physicalStartPoint = new SpatialPoint(new Windows.Foundation.Point(500, 300), CoordinateSpace.Screen, tabStrip); // Physical screen pixels

        // Act
        strategy.InitiateDrag(context, physicalStartPoint);

        // Assert
        _ = this.dragService.UpdatePositionCallCount.Should().Be(1, "UpdatePosition should be called with initial point");
        _ = this.dragService.LastUpdatePosition.Should().Be(physicalStartPoint, "Initial position should be passed to drag service");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_RequiresActiveSession_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));
        var initialUpdateCount = this.dragService.UpdatePositionCallCount;

        // Complete drag to end session
        _ = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen, tabStrip), targetStrip: null, targetIndex: null);

        // Act - Try to update position after session ended
        strategy.OnDragPositionChanged(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip));

        // Assert
        _ = this.dragService.UpdatePositionCallCount.Should().Be(initialUpdateCount, "UpdatePosition should not be called when strategy is not active");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_PassesPhysicalScreenPoint_ToDragService_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));
        this.dragService.Reset(); // Clear initial position update

        var physicalPoint1 = new SpatialPoint(new Windows.Foundation.Point(150, 75), CoordinateSpace.Screen, tabStrip);
        var physicalPoint2 = new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip);
        var physicalPoint3 = new SpatialPoint(new Windows.Foundation.Point(250, 125), CoordinateSpace.Screen, tabStrip);

        // Act - Multiple position updates with physical screen pixels
        strategy.OnDragPositionChanged(physicalPoint1);
        strategy.OnDragPositionChanged(physicalPoint2);
        strategy.OnDragPositionChanged(physicalPoint3);

        // Assert
        _ = this.dragService.UpdatePositionCalls.Should().HaveCount(3, "All position updates should be tracked");
        _ = this.dragService.UpdatePositionCalls[0].Should().Be(physicalPoint1.Point, "First position should match");
        _ = this.dragService.UpdatePositionCalls[1].Should().Be(physicalPoint2.Point, "Second position should match");
        _ = this.dragService.UpdatePositionCalls[2].Should().Be(physicalPoint3.Point, "Third position should match");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_EndsSession_BeforeResetingState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));
        _ = this.dragService.EndSessionCallCount.Should().Be(0, "Session should not be ended yet");

        // Act
        _ = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip), targetStrip: null, targetIndex: null);

        // Assert
        _ = this.dragService.EndSessionCallCount.Should().Be(1, "Session should be ended");
        _ = strategy.IsActive.Should().BeFalse("State should be reset after ending session");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_ReturnsTrue_WhenDroppedOnTabStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var sourceStrip = new TabStrip();
        var targetStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, sourceStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, sourceStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, sourceStrip));

        // Act - Drop on a TabStrip with valid index
        var result = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, sourceStrip), targetStrip, targetIndex: 2);

        // Assert
        _ = result.Should().BeTrue("CompleteDrag should return true when dropped on a TabStrip");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_ReturnsFalse_WhenDroppedOutside_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));

        // Act - Drop outside any TabStrip (null target)
        var result = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip), targetStrip: null, targetIndex: null);

        // Assert
        _ = result.Should().BeFalse("CompleteDrag should return false when dropped outside TabStrips");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_ClearsAllState_AfterCompletion_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new TearOutStrategy(this.dragService, this.coordinator, this.LoggerFactory);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var tabStripItem = new TabStripItem { Item = tabItem, LoggerFactory = this.LoggerFactory };
        var context = new DragContext(tabItem, tabStrip, tabStripItem, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        strategy.InitiateDrag(context, new SpatialPoint(new Windows.Foundation.Point(50, 25), CoordinateSpace.Screen, tabStrip));
        _ = strategy.IsActive.Should().BeTrue("Strategy should be active during drag");

        // Act
        _ = strategy.CompleteDrag(new SpatialPoint(new Windows.Foundation.Point(200, 100), CoordinateSpace.Screen, tabStrip), targetStrip: null, targetIndex: null);

        // Assert
        _ = strategy.IsActive.Should().BeFalse("Strategy should be inactive after complete drag");

        // Verify strategy can be reused (state was properly reset)
        var newItem = new TabItem { Header = "NewTab" };
        var newTabStrip = new TabStrip();
        var newTabStripItem = new TabStripItem { Item = newItem, LoggerFactory = this.LoggerFactory };
        var newContext = new DragContext(newItem, newTabStrip, newTabStripItem, new SpatialPoint(new Windows.Foundation.Point(5, 5), CoordinateSpace.Screen, newTabStrip));

        var act = () => strategy.InitiateDrag(newContext, new SpatialPoint(new Windows.Foundation.Point(100, 50), CoordinateSpace.Screen, newTabStrip));
        _ = act.Should().NotThrow("Strategy should be reusable after state reset");
        _ = strategy.IsActive.Should().BeTrue("Strategy should be active again after reuse");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Helper to create a SpatialPoint in Screen space for TearOut strategy tests.
    /// </summary>
    private static SpatialPoint ScreenPoint(double x, double y, UIElement referenceElement)
        => new(new Windows.Foundation.Point(x, y), CoordinateSpace.Screen, referenceElement);
}
