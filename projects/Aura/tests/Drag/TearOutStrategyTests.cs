// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Controls;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// UI test suite for <see cref="TearOutStrategy"/> that validates drag visual session management
/// and interaction with <see cref="IDragVisualService"/>.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("TearOutStrategyTests")]
[TestCategory("UITest")]
public class TearOutStrategyTests : VisualUserInterfaceTests
{
    private const double DefaultElementWidth = 200;
    private const double DefaultElementHeight = 120;

    private Grid? testRoot;
    private StackPanel? elementHost;
    private TabDragCoordinator? coordinator;
    private SpatialMapper? mapper;

    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task InitiateDrag_ThrowsWhenContextNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new Mock<IDragVisualService>(MockBehavior.Strict);
        var strategy = new TearOutStrategy(dragService.Object, this.coordinator!, this.LoggerFactory);
        var initialPoint = this.ToPhysical(120, 80);

        // Act
        var act = () => strategy.InitiateDrag(null!, initialPoint);

        // Assert
        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("context");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_StartsSessionRequestsPreviewAndUpdatesPosition_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var initialPoint = this.ToScreen(320, 240);
        var physicalInitial = this.ToPhysical(320, 240);
        var setup = await this.StartTearOutAsync(
            initialPoint,
            [physicalInitial]).ConfigureAwait(true);

        // Assert
        setup.DragService.Verify(
            s => s.StartSession(
                It.Is<DragVisualDescriptor>(d => d.RequestedSize == new Size(300, 150)),
                It.Is<SpatialPoint<PhysicalScreenSpace>>(p => AreClose(p.Point, physicalInitial.Point, 0.1)),
                It.IsAny<SpatialPoint<ScreenSpace>>()),
            Times.Once());
        setup.TabStrip.Verify(
            ts => ts.RequestPreviewImage(
                setup.Context.DraggedItemData,
                It.Is<DragVisualDescriptor>(d => ReferenceEquals(d, setup.Descriptor))),
            Times.Once());
        _ = setup.Descriptor.RequestedSize.Should().Be(new Size(300, 150));

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_ThrowsWhenAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await this.StartTearOutAsync().ConfigureAwait(true);

        // Act
        var act = () => setup.Strategy.InitiateDrag(setup.Context, this.ToPhysical(10, 10));

        // Assert
        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*already active*");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_IgnoredWhenNotActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new Mock<IDragVisualService>(MockBehavior.Strict);
        var strategy = new TearOutStrategy(dragService.Object, this.coordinator!, this.LoggerFactory);

        // Act
        strategy.OnDragPositionChanged(this.ToPhysical(200, 120));

        // Assert
        dragService.VerifyNoOtherCalls();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_UpdatesOverlayPosition_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var initialPoint = this.ToScreen(240, 180);
        var initialPhysical = this.ToPhysical(240, 180);
        var movedPhysical = this.ToPhysical(260, 200);
        var setup = await this.StartTearOutAsync(
            initialPoint,
            [initialPhysical, movedPhysical]).ConfigureAwait(true);

        // Clear any initial invocations so we only observe the move under test
        setup.DragService.Invocations.Clear();

        // Act
        setup.Strategy.OnDragPositionChanged(movedPhysical);

        // Assert
        setup.DragService.Verify(
            s => s.UpdatePosition(
                setup.Token,
                It.Is<SpatialPoint<PhysicalScreenSpace>>(p => AreClose(p.Point, movedPhysical.Point, 0.1))),
            Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_EndsSessionAndResetsDescriptor_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await this.StartTearOutAsync().ConfigureAwait(true);
        setup.DragService.Invocations.Clear();

        // Act
        var result = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        _ = result.Should().BeNull("TearOut strategy returns null since item is not in any TabStrip yet");
        setup.DragService.Verify(s => s.EndSession(setup.Token), Times.Once());
        setup.DragService.VerifyNoOtherCalls();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_ReturnsFalseWhenInactive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new Mock<IDragVisualService>(MockBehavior.Strict);
        var strategy = new TearOutStrategy(dragService.Object, this.coordinator!, this.LoggerFactory);

        // Act
        var result = strategy.CompleteDrag(drop: true);

        // Assert
        _ = result.Should().BeNull("strategy is not active");
        dragService.VerifyNoOtherCalls();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(true);

        this.testRoot = new Grid();
        this.elementHost = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top,
            Spacing = 8,
        };

        this.testRoot.Children.Add(this.elementHost);
        this.coordinator = this.CreateStubCoordinator();

        await LoadTestContentAsync(this.testRoot).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        // Initialize the mapper with the main window and elementHost
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, this.elementHost);
    }

    /// <summary>
    /// Checks if two points are close enough (within tolerance).
    /// </summary>
    private static bool AreClose(Point left, Point right, double tolerance)
        => Math.Abs(left.X - right.X) < tolerance && Math.Abs(left.Y - right.Y) < tolerance;

    private static TabItem CreateTabItem(string header) => new() { Header = header };

    private static Border CreateVisualElement()
        => new()
        {
            Width = DefaultElementWidth,
            Height = DefaultElementHeight,
            Background = new SolidColorBrush(Colors.DimGray),
        };

    /// <summary>
    /// Converts element-space coordinates to physical screen space using the mapper.
    /// </summary>
    private SpatialPoint<PhysicalScreenSpace> ToPhysical(double x, double y)
    {
        if (this.mapper is null)
        {
            throw new InvalidOperationException("Mapper not initialized. Ensure TestSetupAsync has been called.");
        }

        var elementPoint = new SpatialPoint<ElementSpace>(new Point(x, y));
        return this.mapper.Convert<ElementSpace, PhysicalScreenSpace>(elementPoint);
    }

    /// <summary>
    /// Converts element-space coordinates to screen space using the mapper.
    /// </summary>
    private SpatialPoint<ScreenSpace> ToScreen(double x, double y)
    {
        if (this.mapper is null)
        {
            throw new InvalidOperationException("Mapper not initialized. Ensure TestSetupAsync has been called.");
        }

        var elementPoint = new SpatialPoint<ElementSpace>(new Point(x, y));
        return this.mapper.Convert<ElementSpace, ScreenSpace>(elementPoint);
    }

    private async Task<TearOutTestSetup> StartTearOutAsync(
        SpatialPoint<ScreenSpace>? initialPoint = null,
        IReadOnlyList<SpatialPoint<PhysicalScreenSpace>>? physicalResponses = null)
    {
        // If initialPoint not provided, use element coords and convert to screen
        var screenPoint = initialPoint ?? this.ToScreen(320, 200);

        DragVisualDescriptor? capturedDescriptor = null;
        SpatialPoint<ScreenSpace>? capturedHotspot = null;
        var token = new DragSessionToken { Id = Guid.NewGuid() };

        var dragService = new DragVisualServiceMockBuilder()
            .WithSessionToken(token)
            .CaptureStartSession((descriptor, initialPosition, hotspot) =>
            {
                capturedDescriptor = descriptor;
                capturedHotspot = hotspot;
            })
            .Build();

        var tabStrip = new TabStripMockBuilder().Build();

        var strategy = new TearOutStrategy(dragService.Object, this.coordinator!, this.LoggerFactory);

        var visualElement = CreateVisualElement();
        await this.AttachElementAsync(visualElement).ConfigureAwait(true);

        // Update mapper to use the visual element
        var realMapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        // If physical responses were provided, use the first one; otherwise convert screenPoint to physical
        var initialPhysical = physicalResponses is { Count: > 0 }
            ? physicalResponses[0]
            : realMapper.Convert<ScreenSpace, PhysicalScreenSpace>(screenPoint);

        var context = new DragContext(
            tabStrip.Object,
            CreateTabItem("Dragged"),
            new Point(0, 0),
            visualElement,
            visualElement,
            realMapper);

        // Strategy now expects a physical screen position
        strategy.InitiateDrag(context, initialPhysical);

        return capturedDescriptor is null || !capturedHotspot.HasValue
            ? throw new InvalidOperationException("Drag visual session was not started as expected.")
            : new TearOutTestSetup(
            strategy,
            context,
            dragService,
            tabStrip,
            realMapper,
            token,
            capturedDescriptor,
            capturedHotspot!.Value.Point,
            initialPhysical);
    }

    private async Task AttachElementAsync(FrameworkElement element)
    {
        if (this.elementHost is null)
        {
            throw new InvalidOperationException("Element host is not initialized.");
        }

        if (!this.elementHost.Children.Contains(element))
        {
            this.elementHost.Children.Add(element);
        }

        await WaitForRenderAsync().ConfigureAwait(true);
    }

    private TabDragCoordinator CreateStubCoordinator()
    {
        var windowManager = Mock.Of<IWindowManagerService>();
        static ISpatialMapper MapperFactory(Window? wnd, FrameworkElement? el) => Mock.Of<ISpatialMapper>();
        var dragService = Mock.Of<IDragVisualService>();

        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext
        {
            Dispatcher = dispatcher,
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher),
        };

        return new TabDragCoordinator(hosting, windowManager, MapperFactory, dragService, this.LoggerFactory);
    }

    private sealed record TearOutTestSetup(
        TearOutStrategy Strategy,
        DragContext Context,
        Mock<IDragVisualService> DragService,
        Mock<ITabStrip> TabStrip,
        ISpatialMapper Mapper,
        DragSessionToken Token,
        DragVisualDescriptor Descriptor,
        Point Hotspot,
        SpatialPoint<PhysicalScreenSpace> InitialPhysicalPoint);
}
