// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Coordinates;
using DroidNet.Tests;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

public partial class TabDragCoordinatorTests
{
    [TestMethod]
    public Task Abort_WhenActive_EndsStrategyAndCleansUpState_Async() => EnqueueAsync(async () =>
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

        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, new Point(100, 25).AsElement(), new Point(0, 0));

        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.Abort();

        // Assert
        _ = strategy.CompleteCalled.Should().BeTrue();
        _ = strategy.ReturnedIndex.Should().BeNull("abort returns null");
        _ = GetPrivateField<bool>(coordinator, "isActive").Should().BeFalse();
        _ = GetPrivateField<DragContext?>(coordinator, "dragContext").Should().BeNull();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Abort_WhenInactive_IsIgnored_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.Abort();

        // Assert
        _ = strategy.CompleteCalled.Should().BeFalse();

        await Task.CompletedTask.ConfigureAwait(true);
    });
}
