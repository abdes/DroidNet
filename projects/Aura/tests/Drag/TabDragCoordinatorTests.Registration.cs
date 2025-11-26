// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using AwesomeAssertions;
using DroidNet.Coordinates;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

public partial class TabDragCoordinatorTests
{
    [TestMethod]
    public Task RegisterTabStrip_AddsReference_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id);
        var tabStripMock = builder.Build();

        // Act
        coordinator.RegisterTabStrip(tabStripMock.Object);

        // Assert
        var registered = GetRegisteredStrips(coordinator);
        _ = registered.Should().ContainSingle();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task UnregisterTabStrip_RemovesReference_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var builderA = new TabStripMockBuilder().WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id);
        var stripAMock = builderA.Build();
        var builderB = new TabStripMockBuilder().WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id);
        var stripBMock = builderB.Build();

        coordinator.RegisterTabStrip(stripAMock.Object);
        coordinator.RegisterTabStrip(stripBMock.Object);

        // Act
        coordinator.UnregisterTabStrip(stripAMock.Object);

        // Assert
        var registered = GetRegisteredStrips(coordinator);
        _ = registered.Should().ContainSingle().Which.Should().Be(stripBMock.Object);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task RegisterTabStrip_AddsMultipleReferences_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var secondStripMock = new TabStripMockBuilder().WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id).Build();
        var thirdStripMock = new TabStripMockBuilder().WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id).Build();

        // Act
        coordinator.RegisterTabStrip(secondStripMock.Object);
        coordinator.RegisterTabStrip(thirdStripMock.Object);

        // Assert
        var registered = GetRegisteredStrips(coordinator);
        _ = registered.Should().Contain(secondStripMock.Object, "second strip should be registered");
        _ = registered.Should().Contain(thirdStripMock.Object, "third strip should be registered");
        _ = registered.Should().HaveCount(2, "exactly two strips should be registered");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task HitTest_SkipsStaleTabStrips_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        // Simulate failure for stale element
        var coordinator = this.CreateCoordinator(
            mapperFactory: (window, element) =>
                element is Border b && string.Equals(b.Tag as string, "stale", StringComparison.Ordinal)
                    ? throw new InvalidOperationException("Stale element")
                    : (ISpatialMapper)new SpatialMapper(window, element));

        // 1. Create a "stale" strip
        var staleStripMock = new Mock<ITabStrip>();
        var staleContainer = new Border { Tag = "stale" };
        _ = staleStripMock.Setup(s => s.GetContainerElement()).Returns(staleContainer);
        _ = staleStripMock.SetupGet(s => s.WindowId).Returns(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id);

        // 2. Create a "valid" strip
        var validStripMock = new Mock<ITabStrip>();
        var validContainer = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        _ = validStripMock.Setup(s => s.GetContainerElement()).Returns(validContainer);
        _ = validStripMock.SetupGet(s => s.WindowId).Returns(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id);

        // Setup HitTest to return success so we know it was checked
        _ = validStripMock.Setup(s => s.HitTestWithThreshold(It.IsAny<SpatialPoint<ElementSpace>>(), It.IsAny<double>())).Returns(1);

        // Register both
        coordinator.RegisterTabStrip(staleStripMock.Object);
        coordinator.RegisterTabStrip(validStripMock.Object);

        // Start a drag to initialize context
        var sourceBuilder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(0, "Item");
        var sourceStripMock = sourceBuilder.Build();
        var item = sourceBuilder.GetItem(0);
        var sourceContainer = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        coordinator.StartDrag(item, 0, sourceStripMock.Object, sourceContainer, sourceContainer, new Point(0, 0).AsElement(), new Point(0, 0));

        // Act
        // Invoke GetHitTestTabStrip via reflection
        var getHitTestTabStripMethod = typeof(TabDragCoordinator).GetMethod("GetHitTestTabStrip", BindingFlags.NonPublic | BindingFlags.Instance);
        _ = getHitTestTabStripMethod!.Invoke(coordinator, [new Point(100, 100).AsScreen()]);

        // Assert
        // Verify valid strip was checked
        validStripMock.Verify(s => s.HitTestWithThreshold(It.IsAny<SpatialPoint<ElementSpace>>(), It.IsAny<double>()), Times.AtLeastOnce, "Valid strip should be hit-tested");

        // Verify stale strip was NOT checked (because mapper creation failed)
        staleStripMock.Verify(s => s.HitTestWithThreshold(It.IsAny<SpatialPoint<ElementSpace>>(), It.IsAny<double>()), Times.Never, "Stale strip should be skipped before hit-test");

        coordinator.Abort();
    });
}
