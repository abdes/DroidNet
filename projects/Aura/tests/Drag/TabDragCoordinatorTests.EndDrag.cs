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

    /// <summary>
    ///     A drag that reorders in a strip drops there, even when the release point hit-tests outside every strip, as
    ///     the release point of a drag that started in another window can.
    /// </summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public Task EndDrag_InReorderMode_WithoutHitStrip_DropsIntoTheReorderedStrip_Async() => EnqueueAsync(async () =>
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
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, new Point(120, 30).AsElement(), new Point(0, 0));
        var strategy = new StubDragStrategy { ReturnIndexOverride = 2 };
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act: no strip is registered, so the release point hits none.
        coordinator.EndDrag(this.ToScreen(5000, 5000));

        // Assert
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, tabStripMock.Object, 2), Times.Once());
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, null, null), Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    ///     A release over another window reaches that window, not the strip that started the drag: the coordinator sees
    ///     the button go up itself, and ends the drag where the pointer is.
    /// </summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public Task ReleasingTheButtonAnywhere_EndsTheDrag_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (coordinator, tabStripMock, draggedItem) = await this.StartReorderAsync().ConfigureAwait(true);

        // Act
        coordinator.ReadButtons = static () => new DragButtons(IsPrimaryDown: false, IsEscapeDown: false);
        await WaitUntilAsync(() => !GetPrivateField<bool>(coordinator, "isActive")).ConfigureAwait(true);

        // Assert: the drag reordered in its strip, so it drops there.
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, tabStripMock.Object, It.IsAny<int?>()), Times.Once());
    });

    /// <summary>Esc cancels the drag wherever the pointer is: the tab goes back to the strip it came from.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public Task Escape_CancelsTheDrag_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (coordinator, tabStripMock, draggedItem) = await this.StartReorderAsync().ConfigureAwait(true);

        // Act
        coordinator.ReadButtons = static () => new DragButtons(IsPrimaryDown: true, IsEscapeDown: true);
        await WaitUntilAsync(() => !GetPrivateField<bool>(coordinator, "isActive")).ConfigureAwait(true);

        // Assert
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, null, null), Times.Once());
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, It.IsNotNull<ITabStrip>(), It.IsAny<int?>()), Times.Never());
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

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        for (var attempt = 0; !condition() && attempt < 100; attempt++)
        {
            await Task.Delay(10).ConfigureAwait(true);
        }

        _ = condition().Should().BeTrue("the coordinator polls the buttons while the drag is active");
    }

    // A drag reordering in its own strip, with the primary button down.
    private async Task<(TabDragCoordinator Coordinator, Mock<ITabStrip> Strip, IDragPayload Item)> StartReorderAsync()
    {
        var coordinator = this.CreateCoordinator();
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(0, "Item", new Point(0, 0), 120);
        var tabStripMock = builder.Build();
        var draggedItem = builder.GetItem(0)!;
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);
        coordinator.StartDrag(draggedItem, 0, tabStripMock.Object, visualElement, visualElement, new Point(120, 30).AsElement(), new Point(0, 0));
        SetPrivateField(coordinator, "currentStrategy", new StubDragStrategy { ReturnIndexOverride = 0 });
        return (coordinator, tabStripMock, draggedItem);
    }
}
