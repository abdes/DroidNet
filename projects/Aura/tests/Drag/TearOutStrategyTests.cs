// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Controls;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI;
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
    public TestContext TestContext { get; set; }

    private const double DefaultElementWidth = 200;
    private const double DefaultElementHeight = 120;

    private Grid? testRoot;
    private StackPanel? elementHost;
    private TabDragCoordinator? coordinator;

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
        await WaitForRenderCompletionAsync().ConfigureAwait(true);
    }

    [TestMethod]
    public Task InitiateDrag_ThrowsWhenContextNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var dragService = new Mock<IDragVisualService>(MockBehavior.Strict);
        var strategy = new TearOutStrategy(dragService.Object, this.coordinator!, this.LoggerFactory);
        var initialPoint = DragTestHelpers.ScreenPoint(120, 80);

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
        var initialPoint = DragTestHelpers.ScreenPoint(320, 240);
        var physicalInitial = DragTestHelpers.PhysicalPoint(640, 480);
        var setup = await this.StartTearOutAsync(
            initialPoint,
            [physicalInitial]).ConfigureAwait(true);

        // Assert
        setup.DragService.Verify(
            s => s.StartSession(
                It.Is<DragVisualDescriptor>(d => d.RequestedSize == new Size(300, 150)),
                It.Is<Point>(p => DragTestHelpers.AreClose(p, initialPoint.Point))),
            Times.Once());
        setup.TabStrip.Verify(
            ts => ts.RequestPreviewImage(
                setup.Context.DraggedItem,
                It.Is<DragVisualDescriptor>(d => ReferenceEquals(d, setup.Descriptor))),
            Times.Once());
        setup.DragService.Verify(
            s => s.UpdatePosition(
                setup.Token,
                It.Is<SpatialPoint<PhysicalScreenSpace>>(p => DragTestHelpers.AreClose(p.Point, physicalInitial.Point))),
            Times.Once());
        _ = setup.Descriptor.RequestedSize.Should().Be(new Size(300, 150));
        _ = setup.Hotspot.Should().Be(initialPoint.Point);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_ThrowsWhenAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await this.StartTearOutAsync().ConfigureAwait(true);

        // Act
        var act = () => setup.Strategy.InitiateDrag(setup.Context, DragTestHelpers.ScreenPoint(10, 10));

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
        strategy.OnDragPositionChanged(DragTestHelpers.ScreenPoint(200, 120));

        // Assert
        dragService.VerifyNoOtherCalls();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_UpdatesOverlayPosition_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var initialPoint = DragTestHelpers.ScreenPoint(240, 180);
        var initialPhysical = DragTestHelpers.PhysicalPoint(480, 360);
        var movedPhysical = DragTestHelpers.PhysicalPoint(520, 410);
        var setup = await this.StartTearOutAsync(
            initialPoint,
            [initialPhysical, movedPhysical]).ConfigureAwait(true);

        setup.DragService.Verify(
            s => s.UpdatePosition(
                setup.Token,
                It.Is<SpatialPoint<PhysicalScreenSpace>>(p => DragTestHelpers.AreClose(p.Point, initialPhysical.Point))),
            Times.Once());
        setup.DragService.Invocations.Clear();

        // Act
        setup.Strategy.OnDragPositionChanged(DragTestHelpers.ScreenPoint(260, 200));

        // Assert
        setup.DragService.Verify(
            s => s.UpdatePosition(
                setup.Token,
                It.Is<SpatialPoint<PhysicalScreenSpace>>(p => DragTestHelpers.AreClose(p.Point, movedPhysical.Point))),
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
        var result = setup.Strategy.CompleteDrag();

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
        var result = strategy.CompleteDrag();

        // Assert
        _ = result.Should().BeNull("strategy is not active");
        dragService.VerifyNoOtherCalls();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    private async Task<TearOutTestSetup> StartTearOutAsync(
        SpatialPoint<ScreenSpace>? initialPoint = null,
        IReadOnlyList<SpatialPoint<PhysicalScreenSpace>>? physicalResponses = null,
        int draggedIndex = 0)
    {
        var screenPoint = initialPoint ?? DragTestHelpers.ScreenPoint(320, 200);
        var responses = physicalResponses is { Count: > 0 }
            ? physicalResponses
            : new[] { DragTestHelpers.PhysicalPoint(640, 400) };

        DragVisualDescriptor? capturedDescriptor = null;
        Point? capturedHotspot = null;
        var token = new DragSessionToken { Id = Guid.NewGuid() };

        var dragService = new DragVisualServiceMockBuilder()
            .WithSessionToken(token)
            .CaptureStartSession((descriptor, hotspot) =>
            {
                capturedDescriptor = descriptor;
                capturedHotspot = hotspot;
            })
            .Build();

        var tabStrip = new TabStripMockBuilder().Build();
        var mapper = new SpatialMapperMockBuilder()
            .WithPhysicalResponses(responses)
            .Build();

        var strategy = new TearOutStrategy(dragService.Object, this.coordinator!, this.LoggerFactory);

        var visualElement = this.CreateVisualElement();
        await this.AttachElementAsync(visualElement).ConfigureAwait(true);

        var context = new DragContext(
            tabStrip.Object,
            CreateTabItem("Dragged"),
            draggedIndex,
            visualElement,
            mapper.Object);

        strategy.InitiateDrag(context, screenPoint);

        if (capturedDescriptor is null || !capturedHotspot.HasValue)
        {
            throw new InvalidOperationException("Drag visual session was not started as expected.");
        }

        return new TearOutTestSetup(
            strategy,
            context,
            dragService,
            tabStrip,
            mapper,
            token,
            capturedDescriptor,
            capturedHotspot.Value,
            responses[0]);
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

        await WaitForRenderCompletionAsync().ConfigureAwait(true);
    }

    private static async Task WaitForRenderCompletionAsync() =>
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

    private Border CreateVisualElement()
    {
        return new Border
        {
            Width = DefaultElementWidth,
            Height = DefaultElementHeight,
            Background = new SolidColorBrush(Colors.DimGray),
        };
    }

    private TabDragCoordinator CreateStubCoordinator()
    {
        var windowManager = Mock.Of<IWindowManagerService>();
        SpatialMapperFactory mapperFactory = (_, _) => Mock.Of<ISpatialMapper>();
        var dragService = Mock.Of<IDragVisualService>();
        return new TabDragCoordinator(windowManager, mapperFactory, dragService, this.LoggerFactory);
    }

    private static TabItem CreateTabItem(string header) => new() { Header = header };

    private sealed record TearOutTestSetup(
        TearOutStrategy Strategy,
        DragContext Context,
        Mock<IDragVisualService> DragService,
        Mock<ITabStrip> TabStrip,
        Mock<ISpatialMapper> Mapper,
        DragSessionToken Token,
        DragVisualDescriptor Descriptor,
        Point Hotspot,
        SpatialPoint<PhysicalScreenSpace> InitialPhysicalPoint);
}
