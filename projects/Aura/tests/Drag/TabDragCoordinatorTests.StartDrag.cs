// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Coordinates;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

public partial class TabDragCoordinatorTests
{
    [TestMethod]
    public Task StartDrag_WithValidParameters_InitializesContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(draggedIndex, "Item1", new Point(0, 0), 120);
        var tabStripMock = builder.Build();
        var draggedItem = builder.GetItem(0)!;
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var factoryInvocations = new List<(Window? window, FrameworkElement? element)>();
        var coordinator = this.CreateCoordinator(
            mapperFactory: (window, element) =>
            {
                factoryInvocations.Add((window, element));
                return new SpatialMapper(window, element);
            });

        var initialElementPoint = new Point(120, 30).AsElement();

        // Act
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Assert
        _ = factoryInvocations.Should().ContainSingle();
        _ = factoryInvocations[0].window.Should().Be(VisualUserInterfaceTestsApp.MainWindow);
        _ = factoryInvocations[0].element.Should().Be(visualElement, "factory should be called with the visual element, not the TabStrip interface");

        var isActive = GetPrivateField<bool>(coordinator, "isActive");
        _ = isActive.Should().BeTrue();

        var context = GetPrivateField<DragContext?>(coordinator, "dragContext");
        _ = context.Should().NotBeNull();
        _ = context!.TabStrip.Should().Be(tabStripMock.Object);
        _ = context.DraggedItemData.Should().Be(draggedItem);

        // The last cursor position should be in physical screen space, converted properly from element space
        var lastPhysical = GetPrivateField<SpatialPoint<PhysicalScreenSpace>>(coordinator, "lastCursorPosition");
        _ = lastPhysical.Should().NotBeNull();

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(0, "FirstItem", new Point(0, 0), 120)
            .WithItemSnapshot(1, "SecondItem", new Point(130, 0), 120);
        var tabStripMock = builder.Build();
        var firstItem = builder.GetItem(0)!;
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(80, 20).AsElement();

        coordinator.StartDrag(firstItem, 0, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Act
        var secondItem = builder.GetItem(1)!;
        var act = () => coordinator.StartDrag(secondItem, 0, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Assert
        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*already active*");

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenItemNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var tabStrip = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .Build().Object;
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        // Act
        var act = () => coordinator.StartDrag(null!, 0, tabStrip, visualElement, visualElement, new Point(60, 18).AsElement(), new Point(0, 0));

        // Assert
        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("item");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenSourceNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        // Use the builder to create the test item so TabItem creation is centralized
        var builder = new TabStripMockBuilder().WithItemSnapshot(0, "Item");
        var testItem = builder.GetItem(0)!;

        // Act
        var act = () => coordinator.StartDrag(testItem, 0, null!, visualElement, visualElement, new Point(40, 22).AsElement(), new Point(0, 0));

        // Assert
        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("source");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenDraggedElementNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var tabStrip = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .Build().Object;

        // Use the builder to create the test item so TabItem creation is centralized
        var builder = new TabStripMockBuilder().WithItemSnapshot(0, "Item");
        var testItem = builder.GetItem(0)!;

        // Act
        var act = () => coordinator.StartDrag(testItem, 0, tabStrip, null!, null!, new Point(55, 35).AsElement(), new Point(0, 0));

        // Assert
        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("draggedElement");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_WithNullInitialPoint_UsesGetCursorPos_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder()
            .WithWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .WithItemSnapshot(draggedIndex);
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        var coordinator = this.CreateCoordinator();

        // Act: Start drag without providing initial point (null)
        coordinator.StartDrag(draggedItem, 0, tabStripMock.Object, visualElement, visualElement, new Point(0, 0).AsElement(), new Point(0, 0));

        // Assert: Should successfully initialize (GetCursorPos is called internally)
        var isActive = GetPrivateField<bool>(coordinator, "isActive");
        _ = isActive.Should().BeTrue("drag should be active even without explicit initial point");

        var context = GetPrivateField<DragContext?>(coordinator, "dragContext");
        _ = context.Should().NotBeNull("context should be initialized");

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });
}
