// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Drag;
using DroidNet.Coordinates;
using DroidNet.Tests;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class TabStripDragTests : TabStripTestsBase
{
    private Mock<ITabDragCoordinator>? mockCoordinator;

    public TestContext TestContext { get; set; } = default!;

    [TestMethod]
    public Task HandlePointerPressed_SetsIsDragEngaged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.DragCoordinator = this.mockCoordinator!.Object;
        var container = tabStrip.GetTabStripItemForIndex(0);
        _ = container.Should().NotBeNull();
        var position = new Point(100, 10).AsElement();

        // Act - provide hotspot offsets as (0,0) to mirror PointerRoutedEventArgs behavior
        tabStrip.HandlePointerPressed(container, position, new Point(0, 0));

        // Assert
        _ = tabStrip.IsDragEngaged.Should().BeTrue("pointer pressed on draggable tab should engage drag");
        _ = tabStrip.IsDragOngoing.Should().BeFalse("drag should not be ongoing until threshold exceeded");
    });

    [TestMethod]
    public Task HandlePointerMoved_BelowThreshold_RemainsEngaged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.DragCoordinator = this.mockCoordinator!.Object;
        var container = tabStrip.GetTabStripItemForIndex(0);
        _ = container.Should().NotBeNull();
        var pressPosition = new Point(PreferredItemWidth, PreferredItemWidth / 2).AsElement();
        tabStrip.HandlePointerPressed(container, pressPosition, new Point(0, 0));

        // Act - Move slightly (below drag threshold)
        tabStrip.MovePointer(pressPosition, DragThresholds.InitiationThreshold / 2, DragThresholds.InitiationThreshold / 2);

        // Assert
        _ = tabStrip.IsDragEngaged.Should().BeTrue("drag should remain engaged with small movement");
    });

    [TestMethod]
    public Task HandlePointerReleased_BeforeThreshold_ClearsEngaged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.DragCoordinator = this.mockCoordinator!.Object;
        var container = tabStrip.GetTabStripItemForIndex(0);
        _ = container.Should().NotBeNull();
        var pressPosition = new Point(PreferredItemWidth, PreferredItemWidth / 2).AsElement();
        tabStrip.HandlePointerPressed(container, pressPosition, new Point(0, 0));

        // Act
        _ = tabStrip.HandlePointerReleased(ToScreen(tabStrip, pressPosition.Point.X, pressPosition.Point.Y));

        // Assert
        _ = tabStrip.IsDragEngaged.Should().BeFalse("releasing pointer should clear drag engagement");
    });

    [TestMethod]
    public Task HandlePointerPressed_OnPinnedTab_DoesNotEngage_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 1).ConfigureAwait(true);
        tabStrip.DragCoordinator = this.mockCoordinator!.Object;
        var container = tabStrip.GetTabStripItemForIndex(0);
        _ = container.Should().NotBeNull();

        // Act
        var pressPosition = new Point(PreferredItemWidth, PreferredItemWidth / 2).AsElement();
        tabStrip.HandlePointerPressed(container, pressPosition, new Point(0, 0));

        // Assert
        _ = tabStrip.IsDragEngaged.Should().BeFalse("pinned tabs should not engage drag");
    });

    [TestMethod]
    public Task HandlePointerPressed_WithoutCoordinator_DoesNotEngage_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var container = tabStrip.GetTabStripItemForIndex(0);
        _ = container.Should().NotBeNull();

        // Act
        var pressPosition = new Point(PreferredItemWidth, PreferredItemWidth / 2).AsElement();
        tabStrip.HandlePointerPressed(container, pressPosition, new Point(0, 0));

        // Assert
        _ = tabStrip.IsDragEngaged.Should().BeFalse("drag should not engage without coordinator");
    });

    [TestMethod]
    public Task ApplyTransformToItem_CreatesTranslateTransform_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var container = tabStrip.GetContainerForIndex(1);
        _ = container.Should().NotBeNull();

        // Measure before position
        var beforePt = container!.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));

        // Act
        var contentId = tabStrip.Items[1].ContentId;
        tabStrip.ApplyTransformToItem(contentId, 50.0);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Measure after position and assert it moved by ~50
        var afterPt = container.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));
        _ = afterPt.X.Should().BeApproximately(beforePt.X + 50.0, 0.5, "item should be visually offset by the applied transform");
    });

    [TestMethod]
    public Task ApplyTransformToItem_SetsOffsetX_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(5).ConfigureAwait(true);
        const double expectedOffset = 120.0;

        // Act
        var container = tabStrip.GetContainerForIndex(2);
        _ = container.Should().NotBeNull();

        var before = container!.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));
        var contentId = tabStrip.Items[2].ContentId;
        tabStrip.ApplyTransformToItem(contentId, expectedOffset);
        await WaitForRenderCompletion().ConfigureAwait(true);

        var after = container.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));
        _ = after.X.Should().BeApproximately(before.X + expectedOffset, 0.5, "offset should match provided value");
    });

    [TestMethod]
    public Task ApplyTransformToItem_SkipsNonExistentContentId_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Act - Apply transform to non-existent ContentId (should not throw)
        var nonExistentId = Guid.NewGuid();
        tabStrip.ApplyTransformToItem(nonExistentId, 50.0);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should complete without error
        _ = tabStrip.Items.Should().HaveCount(3, "collection should be unchanged");
    });

    [TestMethod]
    public Task ApplyTransformToItem_SkipsPinnedItems_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 1).ConfigureAwait(true);

        // Act - Try to apply transform to pinned item
        var pinnedContainer = tabStrip.GetContainerForIndex(0);
        _ = pinnedContainer.Should().NotBeNull();
        var beforePinned = pinnedContainer!.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));

        var pinnedContentId = tabStrip.Items[0].ContentId;
        tabStrip.ApplyTransformToItem(pinnedContentId, 50.0);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Pinned item position should be unchanged
        var afterPinned = pinnedContainer.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));
        _ = afterPinned.X.Should().BeApproximately(beforePinned.X, 0.5, "pinned items should not be translated");
    });

    [TestMethod]
    public Task ApplyTransformToItem_OnlyAffectsRealizedContainers_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Act - Apply transform to realized item
        var container0 = tabStrip.GetContainerForIndex(0);
        var container1 = tabStrip.GetContainerForIndex(1);
        var container2 = tabStrip.GetContainerForIndex(2);

        _ = container0.Should().NotBeNull();
        _ = container1.Should().NotBeNull();
        _ = container2.Should().NotBeNull();

        var before0 = container0!.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));
        var before1 = container1!.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));
        var before2 = container2!.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));

        var contentId1 = tabStrip.Items[1].ContentId;
        tabStrip.ApplyTransformToItem(contentId1, 30.0);
        await WaitForRenderCompletion().ConfigureAwait(true);

        var after0 = container0.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));
        var after1 = container1.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));
        var after2 = container2.TransformToVisual(tabStrip).TransformPoint(new Windows.Foundation.Point(0, 0));

        _ = after0.X.Should().BeApproximately(before0.X, 0.5, "item 0 should not be transformed");
        _ = after1.X.Should().BeApproximately(before1.X + 30.0, 0.5, "item 1 should be transformed");
        _ = after2.X.Should().BeApproximately(before2.X, 0.5, "item 2 should not be transformed");
    });

    [TestMethod]
    public Task RemoveItemAt_RemovesFromItems_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(5).ConfigureAwait(true);
        var initialCount = tabStrip.Items.Count;
        var itemToRemove = tabStrip.Items[2];

        // Act
        tabStrip.RemoveItemAt(2);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().HaveCount(initialCount - 1, "item should be removed");
        _ = tabStrip.Items.Should().NotContain(itemToRemove, "specific item should be removed");
    });

    [TestMethod]
    public Task InsertItemAt_InsertsAtIndex_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var initialCount = tabStrip.Items.Count;
        var newItem = CreateTabItem("Inserted Tab");

        // Act
        tabStrip.InsertItemAt(1, newItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().HaveCount(initialCount + 1, "item should be added");
        _ = tabStrip.Items.Should().HaveElementAt(1, newItem, "item should be at specified index");
    });

    [TestMethod]
    public Task MoveItem_ReordersWithinCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(5).ConfigureAwait(true);
        var itemToMove = tabStrip.Items[1];

        // Act - Move item from index 1 to index 3
        tabStrip.MoveItem(1, 3);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().HaveElementAt(3, itemToMove, "item should be at new index");
        _ = tabStrip.Items.Should().HaveCount(5, "count should remain same");
    });

    [TestMethod]
    public Task MoveItem_DoesNotTriggerRemoveAdd_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(4).ConfigureAwait(true);
        var itemToMove = tabStrip.Items[0];
        var removeCount = 0;
        var addCount = 0;

        tabStrip.Items.CollectionChanged += (_, e) =>
        {
            if (e.Action == System.Collections.Specialized.NotifyCollectionChangedAction.Remove)
            {
                removeCount++;
            }

            if (e.Action == System.Collections.Specialized.NotifyCollectionChangedAction.Add)
            {
                addCount++;
            }
        };

        // Act
        tabStrip.MoveItem(0, 2);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().HaveElementAt(2, itemToMove, "item should be moved");
        _ = removeCount.Should().Be(0, "MoveItem should not trigger Remove event");
        _ = addCount.Should().Be(0, "MoveItem should not trigger Add event");
    });

    [TestMethod]
    public Task InsertItemAsync_WaitsForRealization_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var newItem = CreateTabItem("Async Inserted");
        using var cts = new CancellationTokenSource();

        // Act
        var result = await tabStrip.InsertItemAsync(1, newItem, cts.Token, timeoutMs: 1000).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = result.CurrentStatus.Should().Be(RealizationResult.Status.Realized, "realization should succeed");
        _ = result.Container.Should().NotBeNull("container should be realized");
        _ = tabStrip.Items.Should().HaveElementAt(1, newItem, "item should be inserted");
    });

    [TestMethod]
    public Task TakeSnapshot_ReturnsOrderedItems_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(5).ConfigureAwait(true);

        // Act
        var snapshot = tabStrip.TakeSnapshot();

        // Assert
        _ = snapshot.Should().NotBeNull();
        _ = snapshot.Count.Should().BePositive("snapshot should contain items");

        for (var i = 1; i < snapshot.Count; i++)
        {
            _ = snapshot[i].LayoutOrigin.Point.X.Should().BeGreaterThanOrEqualTo(
                snapshot[i - 1].LayoutOrigin.Point.X,
                "items should be ordered by X position");
        }
    });

    [TestMethod]
    public Task TakeSnapshot_CapturesPositionAndWidth_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Act
        var snapshot = tabStrip.TakeSnapshot();

        // Assert
        foreach (var item in snapshot)
        {
            _ = item.ItemIndex.Should().BeGreaterThanOrEqualTo(0, "ItemIndex should be valid");
            _ = item.Width.Should().BePositive("Width should be positive");
            _ = item.LayoutOrigin.Should().NotBeNull("LayoutOrigin should be set");
        }
    });

    [TestMethod]
    public Task TakeSnapshot_IncludesRealizedContainers_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(4).ConfigureAwait(true);

        // Act
        var snapshot = tabStrip.TakeSnapshot();

        // Assert
        _ = snapshot.Should().NotBeEmpty();
        foreach (var item in snapshot)
        {
            _ = item.Container.Should().NotBeNull("realized containers should be included in snapshot");
        }
    });

    [TestMethod]
    public Task TearOutTab_RaisesTabTearOutRequested_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        var itemToTearOut = tabStrip.Items[1];
        var eventRaised = false;
        object? eventItem = null;
        Point? eventDropPoint = null;

        tabStrip.TabTearOutRequested += (sender, e) =>
        {
            eventRaised = true;
            eventItem = e.Item;
            eventDropPoint = e.ScreenDropPoint;
        };

        var dropPoint = ToScreen(tabStrip, 500, 300);

        // Act
        tabStrip.TearOutTab(itemToTearOut, dropPoint);

        // Assert
        _ = eventRaised.Should().BeTrue("TabTearOutRequested event should be raised");
        _ = eventItem.Should().Be(itemToTearOut, "event should include correct item");
        _ = eventDropPoint.Should().NotBeNull("event should include drop point");
    });

    [TestMethod]
    public Task TearOutTab_ProvidesDropPoint_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(2).ConfigureAwait(true);
        var itemToTearOut = tabStrip.Items[0];
        Point? capturedDropPoint = null;

        tabStrip.TabTearOutRequested += (_, e) => capturedDropPoint = e.ScreenDropPoint;

        var expectedDropPoint = ToScreen(tabStrip, 800, 600);

        // Act
        tabStrip.TearOutTab(itemToTearOut, expectedDropPoint);

        // Assert
        _ = capturedDropPoint.Should().NotBeNull("drop point should be provided");
        _ = capturedDropPoint!.Value.X.Should().BeApproximately(expectedDropPoint.Point.X, 0.1, "X coordinate should match");
        _ = capturedDropPoint.Value.Y.Should().BeApproximately(expectedDropPoint.Point.Y, 0.1, "Y coordinate should match");
    });

    [TestMethod]
    public Task RequestPreviewImage_RaisesTabDragImageRequest_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(2).ConfigureAwait(true);
        var item = tabStrip.Items[0];
        var eventRaised = false;
        object? eventItem = null;

        tabStrip.TabDragImageRequest += (sender, e) =>
        {
            eventRaised = true;
            eventItem = e.Item;
        };

        var descriptor = new DragVisualDescriptor();

        // Act
        tabStrip.RequestPreviewImage(item, descriptor);

        // Assert
        _ = eventRaised.Should().BeTrue("TabDragImageRequest event should be raised");
        _ = eventItem.Should().Be(item, "event should include correct item");
    });

    [TestMethod]
    public Task TryCompleteDrag_RaisesTabDragComplete_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var sourceStrip = this.CreateTabStrip(3);
        var destinationStrip = this.CreateTabStrip(2);
        var draggedItem = sourceStrip.Items[1];
        var eventRaised = false;
        object? eventItem = null;
        TabStrip? eventDestination = null;
        int? eventNewIndex = null;

        sourceStrip.TabDragComplete += (sender, e) =>
        {
            eventRaised = true;
            eventItem = e.Item;
            eventDestination = e.DestinationStrip;
            eventNewIndex = e.NewIndex;
        };

        // Act
        sourceStrip.TryCompleteDrag(draggedItem, destinationStrip, 1);

        // Assert
        _ = eventRaised.Should().BeTrue("TabDragComplete event should be raised");
        _ = eventItem.Should().Be(draggedItem, "event should include dragged item");
        _ = eventDestination.Should().Be(destinationStrip, "event should include destination strip");
        _ = eventNewIndex.Should().Be(1, "event should include new index");
    });

    protected override void TestSetup()
        => this.mockCoordinator = new Mock<ITabDragCoordinator>();

    protected override void TestCleanup()
        => this.mockCoordinator = null;

    private static SpatialPoint<ScreenSpace> ToScreen(TestableTabStrip tabStrip, double x, double y)
    {
        var mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, tabStrip);
        return new Point(x, y).AsElement().Flow(mapper).ToScreen().Point;
    }
}
