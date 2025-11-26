// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using DroidNet.Coordinates;
using DroidNet.Tests;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

public partial class TabDragCoordinatorTests
{
    [TestMethod]
    public Task SwitchToTearOutMode_CallsCloseTabOnSource_Async() => EnqueueAsync(async () =>
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
        var initialScreenPoint = this.ToScreen(120, 30);
        var initialPhysicalPoint = this.ToPhysical(120, 30);

        // Setup drag visual service mock for TearOut mode
        var dragServiceMock = new DragVisualServiceMockBuilder()
            .WithSessionToken(default)
            .WithDescriptor(new DragVisualDescriptor())
            .Build();

        var coordinator = this.CreateCoordinator(dragService: dragServiceMock.Object);
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Act: Switch to TearOut mode by calling the private method via reflection
        var method = typeof(TabDragCoordinator).GetMethod(
            "SwitchToTearOutMode",
            BindingFlags.Instance | BindingFlags.NonPublic);
        _ = method?.Invoke(coordinator, [initialPhysicalPoint]);

        // Assert
        tabStripMock.Verify(s => s.DetachTab(draggedItem), Times.Once());

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task HandleReorderModeTransitions_WithNonClosableItem_DoesNotSwitchToTearOutMode_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(draggedIndex);
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;
        draggedItem.IsClosable = false; // Make it non-closable

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();

        // Setup drag visual service mock
        var dragServiceMock = new DragVisualServiceMockBuilder()
            .WithSessionToken(default)
            .WithDescriptor(new DragVisualDescriptor())
            .Build();

        var coordinator = this.CreateCoordinator(dragService: dragServiceMock.Object);
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Act: Invoke HandleReorderModeTransitions with a point far away (outside strip)
        var method = typeof(TabDragCoordinator).GetMethod(
            "HandleReorderModeTransitions",
            BindingFlags.Instance | BindingFlags.NonPublic);

        var farAwayPoint = this.ToPhysical(10000, 10000);
        _ = method?.Invoke(coordinator, [farAwayPoint]);

        // Assert
        // Verify DetachTab was NOT called (which would happen if SwitchToTearOutMode was called)
        tabStripMock.Verify(s => s.DetachTab(It.IsAny<object>()), Times.Never());

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });
}
