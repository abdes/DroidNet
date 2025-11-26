// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using AwesomeAssertions;
using DroidNet.Coordinates;
using DroidNet.Tests;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

public partial class TabDragCoordinatorTests
{
    [TestMethod]
    public Task EndDrag_WhenActive_CompletesStrategyAndResetsState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        const int draggedIndex = 0;
        var coordinator = this.CreateCoordinator();
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(draggedIndex, "Item", new Point(0, 0), 120);
        var tabStripMock = builder.Build();
        var draggedItem = builder.GetItem(0)!;
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();

        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.EndDrag(this.ToScreen(140, 40));

        // Assert
        _ = strategy.CompleteCalled.Should().BeTrue();
        _ = strategy.ReturnedIndex.Should().BeNull("stub strategy returns null");
        _ = GetPrivateField<bool>(coordinator, "isActive").Should().BeFalse();
        _ = GetPrivateField<DragContext?>(coordinator, "dragContext").Should().BeNull();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_WhenInactive_DoesNotInvokeStrategy_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.EndDrag(this.ToScreen(20, 15));

        // Assert
        _ = strategy.CompleteCalled.Should().BeFalse();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_InTearOutMode_WithoutHitStrip_RaisesTabTearOutRequested_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(draggedIndex, "ItemA");
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();
        var initialScreenPoint = this.ToScreen(120, 30);

        // SwitchToTearOutMode expects a PhysicalScreenSpace point; convert the ScreenSpace point
        var initialPhysicalPoint = this.ToPhysical(120, 30);

        // Setup drag visual service mock for TearOut mode
        var dragServiceMock = new DragVisualServiceMockBuilder()
            .WithSessionToken(default)
            .WithDescriptor(new DragVisualDescriptor())
            .Build();

        var coordinator = this.CreateCoordinator(dragService: dragServiceMock.Object);
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Switch to TearOut mode explicitly via SwitchToTearOutMode
        var switchMethod = typeof(TabDragCoordinator).GetMethod(
            "SwitchToTearOutMode",
            BindingFlags.Instance | BindingFlags.NonPublic);
        _ = switchMethod?.Invoke(coordinator, [initialPhysicalPoint]);

        // Act: Drop outside any TabStrip (far away point that won't hit test)
        var dropPoint = this.ToScreen(5000, 5000);
        coordinator.EndDrag(dropPoint);

        // Assert
        tabStripMock.Verify(s => s.TearOutTab(draggedItem, It.IsAny<SpatialPoint<ScreenSpace>>()), Times.Once());
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, destinationStrip: null, newIndex: null), Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_WithHitStrip_CallsCompleteDragWithDestination_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(draggedIndex);
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();

        var coordinator = this.CreateCoordinator(mapperFactory: (w, e) => new SpatialMapper(w, e));

        // Load the wrapper element into the visual tree so the coordinator can create a mapper for it
        await LoadTestContentAsync(stripElement).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, stripElement, stripElement, initialElementPoint, new Point(0, 0));

        // Register the wrapper element so it can be hit-tested
        coordinator.RegisterTabStrip((ITabStrip)stripElement);

        // Use ReorderStrategy which returns an actual drop index
        var reorderStrategy = new StubDragStrategy { ReturnIndexOverride = 2 };
        SetPrivateField(coordinator, "currentStrategy", reorderStrategy);

        // Act: Drop within the TabStrip bounds
        var dropPoint = this.ToScreen(50, 20);
        coordinator.EndDrag(dropPoint);

        // Assert
        _ = reorderStrategy.CompleteCalled.Should().BeTrue();
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, tabStripMock.Object, 2), Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_InReorderMode_WhenStrategyReturnsNull_CallsCompleteDragWithNullIndex_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(draggedIndex);
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();

        var coordinator = this.CreateCoordinator(mapperFactory: (w, e) => new SpatialMapper(w, e));

        // Ensure wrapper is loaded so the coordinator can create a SpatialMapper for it
        await LoadTestContentAsync(stripElement).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));
        coordinator.RegisterTabStrip((ITabStrip)stripElement);

        // Use a strategy that returns null (error case)
        var nullReturningStrategy = new StubDragStrategy { ReturnIndexOverride = null };
        SetPrivateField(coordinator, "currentStrategy", nullReturningStrategy);

        // Act: Drop within bounds but strategy returns null (error case)
        var dropPoint = this.ToScreen(50, 20);
        coordinator.EndDrag(dropPoint);

        // Assert: hitStrip is not null but finalDropIndex is null => error case
        _ = nullReturningStrategy.CompleteCalled.Should().BeTrue();
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, destinationStrip: null, newIndex: null), Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });
}
