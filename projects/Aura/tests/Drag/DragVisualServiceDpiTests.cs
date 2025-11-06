// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using DroidNet.Aura.Drag;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests.Drag;

/// <summary>
///     Unit tests for <see cref="DragVisualService"/> hotspot application and window positioning
///     behavior.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DragVisualServiceDpiTests")]
[TestCategory("Phase3")]
public class DragVisualServiceDpiTests : VisualUserInterfaceTests
{
    private Mock<ISpatialMapper> mockMapper = null!;
    private TestDragOverlayWindow testWindow = null!;
    private DragVisualService service = null!;
    private DragSessionToken token;

    public required TestContext TestContext { get; set; }

    [TestInitialize]
    public Task InitializeAsync() => EnqueueAsync(() =>
    {
        this.mockMapper = new Mock<ISpatialMapper>();
        this.testWindow = new TestDragOverlayWindow();
        var mockFactory = new Mock<IWindowFactory>();

        // Configure factory to return test window
        // Pass explicit null for the optional metadata parameter so the expression
        // tree used by Moq does not contain a call that relies on optional args.
        _ = mockFactory
            .Setup(f => f.CreateWindow<DragOverlayWindow>(null))
            .ReturnsAsync(this.testWindow);

        // Create mapper factory that returns our mock
        ISpatialMapper MapperFactory(Window? window, FrameworkElement? element) => this.mockMapper.Object;

        // Create hosting context
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext
        {
            Dispatcher = dispatcher,
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher),
        };

        // Create service
        this.service = new DragVisualService(
            hosting,
            mockFactory.Object,
            MapperFactory,
            NullLoggerFactory.Instance);
    });

    [TestCleanup]
    public Task CleanupAsync() => EnqueueAsync(() =>
    {
        if (this.token != default)
        {
            this.service.EndSession(this.token);
        }
    });

    /// <summary>
    ///     Tests that hotspot offset is correctly applied in logical space. Service should compute:
    ///     windowPosition = cursorPosition - hotspot (all in logical pixels).
    /// </summary>
    /// <param name="hotspotX">Hotspot X offset in logical pixels.</param>
    /// <param name="hotspotY">Hotspot Y offset in logical pixels.</param>
    /// <param name="cursorLogicalX">Cursor X position in logical screen space.</param>
    /// <param name="cursorLogicalY">Cursor Y position in logical screen space.</param>
    /// <param name="expectedWindowLogicalX">Expected window X position in logical screen
    /// space.</param>
    /// <param name="expectedWindowLogicalY">Expected window Y position in logical screen
    /// space.</param>
    [TestMethod]
    [DataRow(50.0, 20.0, 100.0, 100.0, 50.0, 80.0, DisplayName = "Standard hotspot: cursor(100,100) - hotspot(50,20) = window(50,80)")]
    [DataRow(0.0, 0.0, 100.0, 100.0, 100.0, 100.0, DisplayName = "Zero hotspot: window positioned at cursor")]
    [DataRow(100.0, 50.0, 100.0, 100.0, 0.0, 50.0, DisplayName = "Hotspot equals cursor X: window X = 0")]
    [DataRow(200.0, 100.0, 500.0, 300.0, 300.0, 200.0, DisplayName = "Large hotspot: cursor(500,300) - hotspot(200,100) = window(300,200)")]
    public Task HotspotOffset_AppliedCorrectly_InLogicalSpace_Async(
        double hotspotX,
        double hotspotY,
        double cursorLogicalX,
        double cursorLogicalY,
        double expectedWindowLogicalX,
        double expectedWindowLogicalY) => EnqueueAsync(() =>
    {
        // Arrange
        var hotspot = new Windows.Foundation.Point(hotspotX, hotspotY);
        this.StartSession(hotspot);

        var cursorLogical = new SpatialPoint<ScreenSpace>(new Windows.Foundation.Point(cursorLogicalX, cursorLogicalY));
        _ = this.mockMapper
            .Setup(m => m.ToScreen(It.IsAny<SpatialPoint<PhysicalScreenSpace>>()))
            .Returns(cursorLogical);

        _ = this.mockMapper
            .Setup(m => m.ToPhysicalScreen(It.Is<SpatialPoint<ScreenSpace>>(
                p => Math.Abs(p.Point.X - expectedWindowLogicalX) < 0.01
                  && Math.Abs(p.Point.Y - expectedWindowLogicalY) < 0.01)))
            .Returns((SpatialPoint<ScreenSpace> p) => new SpatialPoint<PhysicalScreenSpace>(
                new Windows.Foundation.Point(p.Point.X * 1.5, p.Point.Y * 1.5))); // Simulate 150% DPI

        // Act
        var cursorPhysical = new SpatialPoint<PhysicalScreenSpace>(new Windows.Foundation.Point(100, 100));
        this.service.UpdatePosition(this.token, cursorPhysical);

        // Assert
        var expectedPhysicalX = (int)Math.Round(expectedWindowLogicalX * 1.5);
        var expectedPhysicalY = (int)Math.Round(expectedWindowLogicalY * 1.5);

        _ = this.testWindow.MoveCallHistory.Should().ContainSingle(
            p => p.X == expectedPhysicalX && p.Y == expectedPhysicalY,
            string.Create(CultureInfo.InvariantCulture, $"Window should be positioned at physical ({expectedPhysicalX}, {expectedPhysicalY}) derived from logical ({expectedWindowLogicalX}, {expectedWindowLogicalY}) with hotspot ({hotspotX}, {hotspotY})"));
    });

    /// <summary>
    ///     Tests that hotspot offset remains constant in logical space across DPI changes. When
    ///     cursor moves between monitors with different DPI, the visual offset should remain
    ///     stable.
    /// </summary>
    [TestMethod]
    public Task CrossMonitorDrag_MaintainsVisualAlignment_WhenDpiChanges_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var hotspot = new Windows.Foundation.Point(50, 20);
        this.StartSession(hotspot);

        var cursor1Logical = new SpatialPoint<ScreenSpace>(new Windows.Foundation.Point(100, 100));
        var window1Logical = new Windows.Foundation.Point(50, 80); // cursor - hotspot
        var window1Physical = new Windows.Foundation.Point(50, 80); // 1:1 at 100% DPI

        var cursor2Logical = new SpatialPoint<ScreenSpace>(new Windows.Foundation.Point(100, 100)); // Same logical cursor!
        var window2Physical = new Windows.Foundation.Point(75, 120); // Scaled by 1.5

        _ = this.mockMapper
            .SetupSequence(m => m.ToScreen(It.IsAny<SpatialPoint<PhysicalScreenSpace>>()))
            .Returns(cursor1Logical)
            .Returns(cursor2Logical);

        _ = this.mockMapper
            .SetupSequence(m => m.ToPhysicalScreen(It.Is<SpatialPoint<ScreenSpace>>(
                p => Math.Abs(p.Point.X - window1Logical.X) < 0.01
                  && Math.Abs(p.Point.Y - window1Logical.Y) < 0.01)))
            .Returns(new SpatialPoint<PhysicalScreenSpace>(window1Physical))
            .Returns(new SpatialPoint<PhysicalScreenSpace>(window2Physical));

        // Act: Simulate cross-monitor drag
        this.service.UpdatePosition(this.token, new SpatialPoint<PhysicalScreenSpace>(new Windows.Foundation.Point(100, 100)));
        this.service.UpdatePosition(this.token, new SpatialPoint<PhysicalScreenSpace>(new Windows.Foundation.Point(150, 150)));

        // Assert
        _ = this.testWindow.MoveCallHistory.Should().HaveCount(2, "Two position updates should have been made");
        _ = this.testWindow.MoveCallHistory[0].X.Should().Be(50, "First position X should be 50 at 100% DPI");
        _ = this.testWindow.MoveCallHistory[0].Y.Should().Be(80, "First position Y should be 80 at 100% DPI");
        _ = this.testWindow.MoveCallHistory[1].X.Should().Be(75, "Second position X should be 75 at 150% DPI");
        _ = this.testWindow.MoveCallHistory[1].Y.Should().Be(120, "Second position Y should be 120 at 150% DPI (same logical offset, different physical)");
    });

    /// <summary>
    ///     Tests that negative window positions are handled correctly when hotspot is larger than
    ///     cursor position.
    /// </summary>
    [TestMethod]
    public Task LargeHotspot_CanProduceNegativeWindowPosition_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var hotspot = new Windows.Foundation.Point(50, 60);
        this.StartSession(hotspot);

        var cursorLogical = new SpatialPoint<ScreenSpace>(new Windows.Foundation.Point(30, 40));
        var expectedWindowLogical = new Windows.Foundation.Point(-20, -20);
        var expectedWindowPhysical = new SpatialPoint<PhysicalScreenSpace>(new Windows.Foundation.Point(-20, -20));

        _ = this.mockMapper
            .Setup(m => m.ToScreen(It.IsAny<SpatialPoint<PhysicalScreenSpace>>()))
            .Returns(cursorLogical);

        _ = this.mockMapper
            .Setup(m => m.ToPhysicalScreen(It.Is<SpatialPoint<ScreenSpace>>(
                p => Math.Abs(p.Point.X - expectedWindowLogical.X) < 0.01
                  && Math.Abs(p.Point.Y - expectedWindowLogical.Y) < 0.01)))
            .Returns(expectedWindowPhysical);

        // Act
        this.service.UpdatePosition(this.token, new SpatialPoint<PhysicalScreenSpace>(new Windows.Foundation.Point(100, 100)));

        // Assert
        _ = this.testWindow.MoveCallHistory.Should().ContainSingle(
            p => p.X == -20 && p.Y == -20,
            "Window should be positioned at (-20, -20) even if off-screen");
    });

    private void StartSession(Windows.Foundation.Point hotspot, Windows.Foundation.Size? windowSize = null)
    {
        var descriptor = new DragVisualDescriptor { RequestedSize = windowSize ?? new Windows.Foundation.Size(200, 100) };
        this.token = this.service.StartSession(descriptor, hotspot);
    }
}
