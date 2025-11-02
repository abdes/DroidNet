// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;

namespace DroidNet.Controls.Tabs.Tests;

/// <summary>
/// Unit tests for <see cref="TabDragCoordinator"/> drag coordination logic.
/// Tests validate coordinator initialization, drag lifecycle operations, and event handling.
/// These tests focus on the coordinator's behavior in isolation, without depending on TabStrip containers.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("TabDragCoordinatorTests")]
[TestCategory("Phase3")]
public class TabDragCoordinatorTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task CoordinatorCanBeInstantiated_WithDragVisualService_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);

        // Assert
        _ = coordinator.Should().NotBeNull("Coordinator should be created successfully");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    // TODO: add a test case that validates strategy will log messages when loggerfactory is provided
    [TestMethod]
    public Task StartDrag_RequiresNonNullItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var descriptor = new DragVisualDescriptor();
        var tabStrip = new TabStrip();
        var container = new TabStripItem();

        // Act & Assert
        var act = () => coordinator.StartDrag(null!, tabStrip, container, descriptor, new SpatialPoint(new Windows.Foundation.Point(0, 0), CoordinateSpace.Screen, tabStrip));
        _ = act.Should().Throw<ArgumentNullException>().And.ParamName.Should().Be("item");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_RequiresNonNullSource_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "Test" };
        var descriptor = new DragVisualDescriptor();
        var container = CreateMockTabStripItem(tabItem);

        // Act & Assert
        var act = () => coordinator.StartDrag(tabItem, null!, container, descriptor, new SpatialPoint(new Windows.Foundation.Point(0, 0), CoordinateSpace.Screen));
        _ = act.Should().Throw<ArgumentNullException>().And.ParamName.Should().Be("source");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_RequiresNonNullDescriptor_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "Test" };
        var tabStrip = new TabStrip();
        var container = CreateMockTabStripItem(tabItem);

        // Act & Assert
        var act = () => coordinator.StartDrag(tabItem, tabStrip, container, null!, new SpatialPoint(new Windows.Foundation.Point(0, 0), CoordinateSpace.Screen, tabStrip));
        _ = act.Should().Throw<ArgumentNullException>().And.ParamName.Should().Be("visualDescriptor");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_SucceedsWithValidParameters_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        // Act & Assert
        var act = () => coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);
        _ = act.Should().NotThrow();

        // Cleanup
        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenDragAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem1 = new TabItem { Header = "Tab1" };
        var tabItem2 = new TabItem { Header = "Tab2" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        coordinator.StartDrag(tabItem1, tabStrip, CreateMockTabStripItem(tabItem1), descriptor, hotspot);

        // Act & Assert
        var act = () => coordinator.StartDrag(tabItem2, tabStrip, CreateMockTabStripItem(tabItem2), descriptor, hotspot);
        _ = act.Should().Throw<InvalidOperationException>().And.Message.Should()
            .Contain("already active");

        // Cleanup
        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task DragMovedEvent_IsRaisedWhenMoveIsCalled_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        var dragMovedEvents = new System.Collections.Generic.List<DragMovedEventArgs>();

        coordinator.DragMoved += (sender, args) => dragMovedEvents.Add(args);

        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);
        var movePoint = new Windows.Foundation.Point(50, 75);

        // Wait a bit for any initial polling, but don't clear - we want to capture the Move call
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var eventsBeforeMove = dragMovedEvents.Count;
        coordinator.UpdateDragPosition(movePoint);
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = dragMovedEvents.Should().HaveCountGreaterThan(eventsBeforeMove, "DragMoved event should be raised when Move is called");

        // Find the event that matches our explicit move point
        var moveEvent = dragMovedEvents.FirstOrDefault(e => e.ScreenPoint == movePoint);
        _ = moveEvent.Should().NotBeNull("Should have received a DragMoved event with the screen point from Move call");

        // Cleanup
        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task DragEndedEvent_IsRaisedWhenEndDragIsCalled_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        var dragEndedRaised = false;
        DragEndedEventArgs? capturedArgs = null;

        coordinator.DragEnded += (sender, args) =>
        {
            dragEndedRaised = true;
            capturedArgs = args;
        };

        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);
        var endPoint = new Windows.Foundation.Point(100, 150);

        // Act
        coordinator.EndDrag(endPoint, droppedOverStrip: false, destination: null, newIndex: null);
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = dragEndedRaised.Should().BeTrue("DragEnded event should be raised when EndDrag is called");
        _ = capturedArgs.Should().NotBeNull("DragEnded event args should be captured");
        _ = capturedArgs!.ScreenPoint.Should().Be(endPoint, "Event args should contain the end point");
        _ = capturedArgs.DroppedOverStrip.Should().BeFalse("DroppedOverStrip should be false");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task DragEndedEvent_ContainsDestinationInfo_WhenDroppedOverStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var sourceStrip = new TabStrip();
        var destStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, sourceStrip);

        var dragEndedRaised = false;
        DragEndedEventArgs? capturedArgs = null;

        coordinator.DragEnded += (sender, args) =>
        {
            dragEndedRaised = true;
            capturedArgs = args;
        };

        coordinator.StartDrag(tabItem, sourceStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);

        // Act
        coordinator.EndDrag(
            new Windows.Foundation.Point(100, 100),
            droppedOverStrip: true,
            destination: destStrip,
            newIndex: 0);
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = dragEndedRaised.Should().BeTrue("DragEnded event should be raised");
        _ = capturedArgs!.DroppedOverStrip.Should().BeTrue("DroppedOverStrip should be true");
        _ = capturedArgs.Destination.Should().Be(destStrip, "Destination should be the drop target");
        _ = capturedArgs.NewIndex.Should().Be(0, "NewIndex should be the insertion index");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task MoveIsIgnored_WhenNoDragIsActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var moveEventRaised = false;

        coordinator.DragMoved += (sender, args) => moveEventRaised = true;

        // Act
        coordinator.UpdateDragPosition(new Windows.Foundation.Point(50, 75));
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = moveEventRaised.Should().BeFalse("DragMoved event should not be raised when no drag is active");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDragIsIgnored_WhenNoDragIsActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var dragEndedRaised = false;

        coordinator.DragEnded += (sender, args) => dragEndedRaised = true;

        // Act
        coordinator.EndDrag(
            new Windows.Foundation.Point(100, 100),
            droppedOverStrip: false,
            destination: null,
            newIndex: null);
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = dragEndedRaised.Should().BeFalse("DragEnded event should not be raised when no drag is active");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task AbortClearsActiveState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);

        // Act
        coordinator.Abort();

        // Now try to start a new drag - should succeed if state was properly cleared
        var act = () =>
        {
            coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);
            coordinator.Abort();
        };
        _ = act.Should().NotThrow();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task AbortIsIgnored_WhenNoDragIsActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);

        // Act & Assert
        var act = () => coordinator.Abort();
        _ = act.Should().NotThrow();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CanCreateMultipleCoordinatorInstances_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService1 = new MockDragVisualService();
        var dragService2 = new MockDragVisualService();
        var dragService3 = new MockDragVisualService();

        // Act
        var coordinator1 = new TabDragCoordinator(dragService1);
        var coordinator2 = new TabDragCoordinator(dragService2);
        var coordinator3 = new TabDragCoordinator(dragService3);

        // Assert
        _ = coordinator1.Should().NotBeNull("First coordinator should be created");
        _ = coordinator2.Should().NotBeNull("Second coordinator should be created");
        _ = coordinator3.Should().NotBeNull("Third coordinator should be created");
        _ = coordinator1.Should().NotBe(coordinator2, "Coordinators should be distinct instances");
        _ = coordinator2.Should().NotBe(coordinator3, "Coordinators should be distinct instances");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task MultipleCoordinators_CanRunIndependentDrags_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService1 = new MockDragVisualService();
        var dragService2 = new MockDragVisualService();
        var coordinator1 = new TabDragCoordinator(dragService1);
        var coordinator2 = new TabDragCoordinator(dragService2);

        var tabItem1 = new TabItem { Header = "Tab1" };
        var tabItem2 = new TabItem { Header = "Tab2" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        coordinator1.StartDrag(tabItem1, tabStrip, CreateMockTabStripItem(tabItem1), descriptor, hotspot);

        // Act & Assert - Coordinator2 should also be able to start a drag independently
        var act = () => coordinator2.StartDrag(tabItem2, tabStrip, CreateMockTabStripItem(tabItem2), descriptor, hotspot);
        _ = act.Should().NotThrow();

        // Cleanup
        coordinator1.Abort();
        coordinator2.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CoordinatorSubscriptionsAreIndependent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator1 = new TabDragCoordinator(dragService);
        var coordinator2 = new TabDragCoordinator(dragService);

        var coordinator1EventsRaised = 0;
        var coordinator2EventsRaised = 0;

        coordinator1.DragMoved += (sender, args) => coordinator1EventsRaised++;
        coordinator2.DragMoved += (sender, args) => coordinator2EventsRaised++;

        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        // Act
        coordinator1.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
        coordinator1.UpdateDragPosition(new Windows.Foundation.Point(50, 50));
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
        coordinator1.Abort();

        // Assert - Only coordinator1 should have received events
        _ = coordinator1EventsRaised.Should().BeGreaterThan(0, "Coordinator1 should have received DragMoved events");
        _ = coordinator2EventsRaised.Should().Be(0, "Coordinator2 should not receive events from Coordinator1");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task MoveDoesNotRaiseEvent_BeforeStartDragIsCalled_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var dragMovedEventRaised = false;

        coordinator.DragMoved += (sender, args) => dragMovedEventRaised = true;

        // Act - Call Move without starting a drag
        coordinator.UpdateDragPosition(new Windows.Foundation.Point(100, 100));
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = dragMovedEventRaised.Should().BeFalse("DragMoved should not be raised when no drag is active");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task MultipleMovesRaisedMultipleEvents_DuringActiveDrag_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        var dragMovedEvents = new System.Collections.Generic.List<DragMovedEventArgs>();
        coordinator.DragMoved += (sender, args) => dragMovedEvents.Add(args);

        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);

        // Wait for any initial polling timer events to settle
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
        var eventsBeforeExplicitMoves = dragMovedEvents.Count;

        // Act - Call Move three times with explicit points
        var point1 = new Windows.Foundation.Point(50, 50);
        var point2 = new Windows.Foundation.Point(75, 75);
        var point3 = new Windows.Foundation.Point(100, 100);

        coordinator.UpdateDragPosition(point1);
        coordinator.UpdateDragPosition(point2);
        coordinator.UpdateDragPosition(point3);

        // Wait for the moves to be processed (but don't wait long enough for more polling)
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - We should have more events than before (polling + explicit moves)
        _ = dragMovedEvents.Should()
            .HaveCountGreaterThan(
                eventsBeforeExplicitMoves,
                "Multiple Move calls should raise multiple DragMoved events");

        // Assert - The explicit move points should all be present in the event list
        var point1Event = dragMovedEvents.FirstOrDefault(e => e.ScreenPoint == point1);
        var point2Event = dragMovedEvents.FirstOrDefault(e => e.ScreenPoint == point2);
        var point3Event = dragMovedEvents.FirstOrDefault(e => e.ScreenPoint == point3);

        _ = point1Event.Should().NotBeNull("First explicit move point should be in events");
        _ = point2Event.Should().NotBeNull("Second explicit move point should be in events");
        _ = point3Event.Should().NotBeNull("Third explicit move point should be in events");

        // Cleanup
        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDragMultipleTimes_OnlyFirstEndDragRaisesEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        var dragEndedEventCount = 0;
        coordinator.DragEnded += (sender, args) => dragEndedEventCount++;

        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);

        // Act - Call EndDrag multiple times
        coordinator.EndDrag(
            new Windows.Foundation.Point(100, 100),
            droppedOverStrip: false,
            destination: null,
            newIndex: null);
        await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true);

        coordinator.EndDrag(
            new Windows.Foundation.Point(100, 100),
            droppedOverStrip: false,
            destination: null,
            newIndex: null);
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = dragEndedEventCount.Should().Be(1, "DragEnded should only be raised once");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task DragEndedEventArgs_WithDropDestination_ContainsAllInformation_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "SourceTab" };
        var sourceStrip = new TabStrip();
        var destinationStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor { Title = "DraggedTab" };
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, sourceStrip);
        var endPoint = new Windows.Foundation.Point(250, 150);
        const int expectedNewIndex = 3;

        DragEndedEventArgs? capturedArgs = null;
        coordinator.DragEnded += (sender, args) => capturedArgs = args;

        coordinator.StartDrag(tabItem, sourceStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);

        // Act
        coordinator.EndDrag(
            endPoint,
            droppedOverStrip: true,
            destination: destinationStrip,
            newIndex: expectedNewIndex);
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = capturedArgs.Should().NotBeNull("DragEnded event args should be captured");
        _ = capturedArgs!.ScreenPoint.Should().Be(endPoint, "Event args should contain the end point");
        _ = capturedArgs.DroppedOverStrip.Should().BeTrue("DroppedOverStrip should be true");
        _ = capturedArgs.Destination.Should().Be(destinationStrip, "Destination should match the drop target");
        _ = capturedArgs.NewIndex.Should().Be(expectedNewIndex, "NewIndex should match the insertion position");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task DragMovedEventArgs_ContainsCorrectScreenPoint_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        var capturedScreenPoints = new System.Collections.Generic.List<Windows.Foundation.Point>();
        coordinator.DragMoved += (sender, args) => capturedScreenPoints.Add(args.ScreenPoint);

        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);

        // Wait for any initial polling events and clear them
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
        capturedScreenPoints.Clear();

        // Act
        var point1 = new Windows.Foundation.Point(50, 50);
        var point2 = new Windows.Foundation.Point(100, 75);
        var point3 = new Windows.Foundation.Point(150, 100);

        coordinator.UpdateDragPosition(point1);
        coordinator.UpdateDragPosition(point2);
        coordinator.UpdateDragPosition(point3);

        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - The first three events should be our explicit moves
        _ = capturedScreenPoints.Should().HaveCountGreaterThanOrEqualTo(3, "Should capture at least three move events");
        _ = capturedScreenPoints[0].Should().Be(point1, "First point should match");
        _ = capturedScreenPoints[1].Should().Be(point2, "Second point should match");
        _ = capturedScreenPoints[2].Should().Be(point3, "Third point should match");

        // Cleanup
        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_WithDescriptor_CanBeAbortedAndRestarted_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Verify we can start a drag with a descriptor, abort it, and start a new one
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor1 = new DragVisualDescriptor { Title = "FirstDrag" };
        var descriptor2 = new DragVisualDescriptor { Title = "SecondDrag" };
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        // Act - Start first drag
        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor1, hotspot);

        // Verify we can't start another drag with same coordinator
        var act1 = () => coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor2, hotspot);
        _ = act1.Should().Throw<InvalidOperationException>("Should not allow concurrent drags");

        // Abort first drag
        coordinator.Abort();
        await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act - Start second drag with different descriptor
        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor2, hotspot);

        // Assert - Second drag should be active, first should be aborted
        var secondDragEventRaised = false;
        coordinator.DragMoved += (s, e) => secondDragEventRaised = true;

        coordinator.UpdateDragPosition(new Windows.Foundation.Point(50, 50));
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = secondDragEventRaised.Should().BeTrue("Second drag with new descriptor should work after abort");

        // Cleanup
        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task AbortAfterMove_ClearsState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "TestTab" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var hotspot = new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip);

        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);
        coordinator.UpdateDragPosition(new Windows.Foundation.Point(100, 100));

        // Act
        coordinator.Abort();

        // Now verify we can start a new drag
        var dragMovedRaised = false;
        coordinator.DragMoved += (sender, args) => dragMovedRaised = true;

        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, hotspot);
        coordinator.UpdateDragPosition(new Windows.Foundation.Point(50, 50));
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = dragMovedRaised.Should().BeTrue("Should be able to start new drag and raise events after Abort");

        // Cleanup
        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_WithoutActiveDrag_DoesNotRaiseEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Coordinator with no active drag
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var dragEndedRaised = false;

        coordinator.DragEnded += (sender, args) => dragEndedRaised = true;

        // Act - Call EndDrag without starting a drag first
        coordinator.EndDrag(
            new Windows.Foundation.Point(100, 100),
            droppedOverStrip: false,
            destination: null,
            newIndex: null);
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = dragEndedRaised.Should().BeFalse("DragEnded should not be raised when no active drag");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Tests that StartDrag passes hotspot in logical pixels to the service (XAML coordinate space).
    /// After Phase 2 refactor: Hotspot is stored in DragContext and passed to TearOutStrategy,
    /// which then passes it to the service when entering TearOut mode.
    /// </summary>
    [TestMethod]
    public Task StartDrag_PassesLogicalHotspotToService_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "Test" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        var logicalHotspot = new SpatialPoint(new Windows.Foundation.Point(50, 20), CoordinateSpace.Element, tabStrip); // Logical pixels (XAML)

        // Act: Start drag (enters Reorder mode initially)
        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, logicalHotspot);

        // Simulate switching to TearOut mode (this is when service session starts)
        // In real scenario, this happens when drag crosses TearOut threshold
        var tearOutPosition = new Windows.Foundation.Point(100, 100);

        // Use reflection to call SwitchToTearOutMode (internal method) for testing
        var switchMethod = typeof(TabDragCoordinator).GetMethod(
            "SwitchToTearOutMode",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        _ = switchMethod?.Invoke(coordinator, [tearOutPosition]);

        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert: Service should receive hotspot in logical pixels when TearOut session starts
        _ = dragService.StartSessionHotspot.Should().Be(logicalHotspot, "Hotspot should be passed to service in logical pixels when entering TearOut mode");
        _ = dragService.StartSessionCallCount.Should().Be(1, "Service session should be started when entering TearOut mode");

        // Cleanup
        coordinator.Abort();
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Tests that Move() passes physical screen pixels to the service (GetCursorPos contract).
    /// The coordinator should NOT perform DPI conversion; service owns all DPI handling.
    /// After Phase 2 refactor: Move() calls are delegated to strategies. Only TearOutStrategy
    /// calls the service's UpdatePosition method.
    /// </summary>
    [TestMethod]
    public Task Move_PassesPhysicalPixelsToService_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "Test" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        // Switch to TearOut mode (this is when service UpdatePosition gets called)
        var switchMethod = typeof(TabDragCoordinator).GetMethod(
            "SwitchToTearOutMode",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        _ = switchMethod?.Invoke(coordinator, [new Windows.Foundation.Point(50, 50)]);

        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Clear any initial position calls from mode switch
        dragService.UpdatePositionCalls.ToList().Clear();

        // Act: Simulate cursor movement in physical pixels (as returned by GetCursorPos)
        var physicalPosition1 = new Windows.Foundation.Point(100, 100);
        var physicalPosition2 = new Windows.Foundation.Point(200, 200);

        coordinator.UpdateDragPosition(physicalPosition1);
        coordinator.UpdateDragPosition(physicalPosition2);

        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert: Service should receive physical pixels unchanged (in TearOut mode)
        _ = dragService.UpdatePositionCalls.Should().Contain(physicalPosition1, "First physical position should be passed to service in TearOut mode");
        _ = dragService.UpdatePositionCalls.Should().Contain(physicalPosition2, "Second physical position should be passed to service in TearOut mode");

        // Cleanup
        coordinator.Abort();
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Tests that the coordinator does NOT convert coordinates from physical to logical.
    /// This ensures the service is the single owner of DPI conversions (critical for cross-monitor support).
    /// After Phase 2 refactor: TearOutStrategy delegates to service without coordinate conversion.
    /// </summary>
    [TestMethod]
    public Task Coordinator_DoesNotConvertPhysicalToLogical_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "Test" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();
        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));

        // Switch to TearOut mode (this is when service UpdatePosition gets called)
        var switchMethod = typeof(TabDragCoordinator).GetMethod(
            "SwitchToTearOutMode",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        _ = switchMethod?.Invoke(coordinator, [new Windows.Foundation.Point(50, 50)]);

        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act: Pass physical pixels (e.g., 200x200 on a 200% DPI monitor)
        // If coordinator incorrectly converts to logical, service would receive 100x100 instead
        var physicalPosition = new Windows.Foundation.Point(200, 200);
        coordinator.UpdateDragPosition(physicalPosition);

        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert: Service should receive exact physical coordinates (in TearOut mode)
        _ = dragService.UpdatePositionCalls.Should().Contain(
            physicalPosition,
            "Coordinator must pass physical pixels unchanged to TearOutStrategy, which passes them to service (service owns DPI conversion)");

        // Cleanup
        coordinator.Abort();
        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Coordinator_RegistersTabStrip_Successfully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabStrip = new TabStrip();

        // Act
        coordinator.RegisterTabStrip(tabStrip);

        // Assert - Registration should succeed without errors
        // We can't directly verify the internal registry, but we can verify the method completes
        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Coordinator_UnregistersTabStrip_Successfully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabStrip = new TabStrip();
        coordinator.RegisterTabStrip(tabStrip);

        // Act
        coordinator.UnregisterTabStrip(tabStrip);

        // Assert - Unregistration should succeed without errors
        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Coordinator_RegisterTabStrip_ThrowsOnNullStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);

        // Act & Assert
        var act = () => coordinator.RegisterTabStrip(null!);
        _ = act.Should().Throw<ArgumentNullException>().And.ParamName.Should().Be("strip");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Coordinator_UnregisterTabStrip_ThrowsOnNullStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);

        // Act & Assert
        var act = () => coordinator.UnregisterTabStrip(null!);
        _ = act.Should().Throw<ArgumentNullException>().And.ParamName.Should().Be("strip");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Coordinator_DragMovedEvent_IncludesAllRequiredProperties_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "Test" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();

        DragMovedEventArgs? receivedArgs = null;
        coordinator.DragMoved += (s, e) => receivedArgs = e;

        // Act
        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        coordinator.UpdateDragPosition(new Windows.Foundation.Point(50, 50));

        // Assert
        _ = receivedArgs.Should().NotBeNull("DragMoved event should be raised");
        _ = receivedArgs!.ScreenPoint.Should().NotBeNull("ScreenPoint should be populated");
        _ = receivedArgs.Item.Should().Be(tabItem, "Item should be the dragged TabItem");
        _ = receivedArgs.IsInReorderMode.Should().BeTrue("Should start in Reorder mode");
        _ = receivedArgs.HitStrip.Should().BeNull("HitStrip should be null when no strips are registered");
        _ = receivedArgs.DropIndex.Should().BeNull("DropIndex should be null (populated by strategies in later phases)");

        coordinator.Abort();
        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Coordinator_DragEndedEvent_IncludesAllRequiredProperties_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabItem = new TabItem { Header = "Test" };
        var tabStrip = new TabStrip();
        var descriptor = new DragVisualDescriptor();

        DragEndedEventArgs? receivedArgs = null;
        coordinator.DragEnded += (s, e) => receivedArgs = e;

        // Act
        coordinator.StartDrag(tabItem, tabStrip, CreateMockTabStripItem(tabItem), descriptor, new SpatialPoint(new Windows.Foundation.Point(10, 10), CoordinateSpace.Screen, tabStrip));
        coordinator.EndDrag(new Windows.Foundation.Point(100, 100), droppedOverStrip: false, destination: null, newIndex: null);

        // Assert
        _ = receivedArgs.Should().NotBeNull("DragEnded event should be raised");
        _ = receivedArgs!.ScreenPoint.Should().NotBeNull("ScreenPoint should be populated");
        _ = receivedArgs.Item.Should().Be(tabItem, "Item should be the dragged TabItem");
        _ = receivedArgs.IsInReorderMode.Should().BeTrue("Should be in Reorder mode when ended");
        _ = receivedArgs.DroppedOverStrip.Should().BeFalse("DroppedOverStrip should match parameter");
        _ = receivedArgs.Destination.Should().BeNull("Destination should be null as specified");
        _ = receivedArgs.NewIndex.Should().BeNull("NewIndex should be null as specified");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Coordinator_MultipleTabStrips_CanBeRegistered_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        var tabStrip1 = new TabStrip();
        var tabStrip2 = new TabStrip();
        var tabStrip3 = new TabStrip();

        // Act - Register multiple strips
        coordinator.RegisterTabStrip(tabStrip1);
        coordinator.RegisterTabStrip(tabStrip2);
        coordinator.RegisterTabStrip(tabStrip3);

        // Assert - All registrations should succeed
        // Verification is implicit - method completes without throwing
        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Coordinator_CleanUpDeadReferences_WhenRegisteringNewStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);

        // Register a strip and let it go out of scope
        {
            var tempStrip = new TabStrip();
            coordinator.RegisterTabStrip(tempStrip);
        }

        // Force garbage collection to make the weak reference dead
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        // Act - Registering a new strip should clean up dead references
        var newStrip = new TabStrip();
        coordinator.RegisterTabStrip(newStrip);

        // Assert - Should complete without error (dead references cleaned up)
        await Task.CompletedTask.ConfigureAwait(true);
    });

    private static TabStripItem CreateMockTabStripItem(TabItem item)
    {
        var container = new TabStripItem { Item = item };
        return container;
    }
}
