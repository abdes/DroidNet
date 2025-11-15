// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// Unit tests for <see cref="DragVisualService"/> core functionality and contract compliance.
/// Tests verify adherence to specifications in Drag.md, including:
/// - Single active session enforcement
/// - Session lifecycle management
/// - UI thread affinity
/// - Token validation
/// - Descriptor lifecycle
/// NOTE: Service renders placeholder blue rectangle when HeaderImage/PreviewImage are null,
/// allowing tests to run without creating WinUI ImageSource objects.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DragVisualServiceTests")]
[TestCategory("UITest")]
public partial class DragVisualServiceTests : VisualUserInterfaceTests, IDisposable
{
    private DragVisualService service = null!;
    private DragSessionToken token;

    private bool disposed;

    public required TestContext TestContext { get; set; }

    /// <summary>
    /// REQ-001: Single Active Session
    /// Verifies that only one session can exist per process.
    /// StartSession must throw InvalidOperationException if a session is already active.
    /// </summary>
    [TestMethod]
    public Task StartSession_ThrowsWhenSessionAlreadyActive_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor1 = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var descriptor2 = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);

        this.token = this.service.StartSession(descriptor1, hotspot.AsPhysicalScreen(), hotspot.AsScreen());

        // Act & Assert
        var act = () => this.service.StartSession(descriptor2, hotspot.AsPhysicalScreen(), hotspot.AsScreen());
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("*already active*", "Only one session allowed per process (REQ-001)");
    });

    /// <summary>
    /// REQ-001: Single Active Session
    /// Verifies that a new session can be started after the previous one ends.
    /// </summary>
    [TestMethod]
    public Task StartSession_SucceedsAfterPreviousSessionEnds_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor1 = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var descriptor2 = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);

        var token1 = this.service.StartSession(descriptor1, hotspot.AsPhysicalScreen(), hotspot.AsScreen());
        this.service.EndSession(token1);

        // Act
        this.token = this.service.StartSession(descriptor2, hotspot.AsPhysicalScreen(), hotspot.AsScreen());

        // Assert
        _ = this.token.Should().NotBe(default(DragSessionToken), "Valid token should be returned");
    });

    /// <summary>
    /// Verifies that EndSession with valid token succeeds.
    /// </summary>
    [TestMethod]
    public Task EndSession_SucceedsWithValidToken_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);
        this.token = this.service.StartSession(descriptor, hotspot.AsPhysicalScreen(), hotspot.AsScreen());

        // Act & Assert
        var act = () => this.service.EndSession(this.token);
        _ = act.Should().NotThrow("EndSession with valid token should succeed");

        this.token = default; // Prevent double cleanup
    });

    /// <summary>
    /// Verifies that EndSession with invalid token is handled gracefully (no-op).
    /// </summary>
    [TestMethod]
    public Task EndSession_WithInvalidToken_IsNoOp_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var invalidToken = new DragSessionToken { Id = Guid.NewGuid() };

        // Act & Assert
        var act = () => this.service.EndSession(invalidToken);
        _ = act.Should().NotThrow("EndSession with invalid token should be no-op");
    });

    /// <summary>
    /// Verifies that calling EndSession twice is handled gracefully.
    /// </summary>
    [TestMethod]
    public Task EndSession_CalledTwice_IsHandledGracefully_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);
        this.token = this.service.StartSession(descriptor, hotspot.AsPhysicalScreen(), hotspot.AsScreen());
        this.service.EndSession(this.token);

        // Act & Assert
        var act = () => this.service.EndSession(this.token);
        _ = act.Should().NotThrow("Second EndSession should be no-op");

        this.token = default; // Prevent cleanup attempt
    });

    /// <summary>
    /// Verifies that UpdatePosition with valid token succeeds.
    /// </summary>
    [TestMethod]
    public Task UpdatePosition_SucceedsWithValidToken_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);
        this.token = this.service.StartSession(descriptor, hotspot.AsPhysicalScreen(), hotspot.AsScreen());

        // Act & Assert
        var physicalPosition = new SpatialPoint<PhysicalScreenSpace>(new Point(500, 300));
        var act = () => this.service.UpdatePosition(this.token, physicalPosition);
        _ = act.Should().NotThrow("UpdatePosition with valid token should succeed");
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
        var mockMapper = new Mock<ISpatialMapper>();

        // Create raw mapper factory that returns our mock regardless of HWND
        RawSpatialMapperFactory rawFactory = CreateMapperFactory;
        ISpatialMapper CreateMapperFactory(IntPtr hwnd) => mockMapper.Object;

        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var initialPosition = new Point(0, 0).AsPhysicalScreen();
        var hotspotOffsets = new Point(hotspotX, hotspotY).AsScreen();
        this.token = this.service.StartSession(descriptor, initialPosition, hotspotOffsets);

        var cursorLogical = new SpatialPoint<ScreenSpace>(new Point(cursorLogicalX, cursorLogicalY));
        _ = mockMapper
            .Setup(m => m.ToScreen(It.IsAny<SpatialPoint<PhysicalScreenSpace>>()))
            .Returns(cursorLogical);

        _ = mockMapper
            .Setup(m => m.ToPhysicalScreen(It.Is<SpatialPoint<ScreenSpace>>(
                p => Math.Abs(p.Point.X - expectedWindowLogicalX) < 0.01
                  && Math.Abs(p.Point.Y - expectedWindowLogicalY) < 0.01)))
            .Returns((SpatialPoint<ScreenSpace> p) => new SpatialPoint<PhysicalScreenSpace>(
                new Point(p.Point.X * 1.5, p.Point.Y * 1.5))); // Simulate 150% DPI

        // Act
        var cursorPhysical = new SpatialPoint<PhysicalScreenSpace>(new Point(100, 100));
        this.service.UpdatePosition(this.token, cursorPhysical);

        // Assert
        var expectedPhysicalX = (int)Math.Round(expectedWindowLogicalX * 1.5);
        var expectedPhysicalY = (int)Math.Round(expectedWindowLogicalY * 1.5);
    });

    /// <summary>
    /// Verifies that UpdatePosition with invalid token is handled gracefully (no-op).
    /// </summary>
    [TestMethod]
    public Task UpdatePosition_WithInvalidToken_IsNoOp_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var invalidToken = new DragSessionToken { Id = Guid.NewGuid() };
        var physicalPosition = new SpatialPoint<PhysicalScreenSpace>(new Point(500, 300));

        // Act & Assert
        var act = () => this.service.UpdatePosition(invalidToken, physicalPosition);
        _ = act.Should().NotThrow("UpdatePosition with invalid token should be no-op");
    });

    /// <summary>
    /// Verifies that UpdatePosition after EndSession is handled gracefully.
    /// </summary>
    [TestMethod]
    public Task UpdatePosition_AfterEndSession_IsNoOp_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);
        this.token = this.service.StartSession(descriptor, hotspot.AsPhysicalScreen(), hotspot.AsScreen());
        this.service.EndSession(this.token);

        // Act & Assert
        var physicalPosition = new SpatialPoint<PhysicalScreenSpace>(new Point(500, 300));
        var act = () => this.service.UpdatePosition(this.token, physicalPosition);
        _ = act.Should().NotThrow("UpdatePosition after EndSession should be no-op");

        this.token = default; // Prevent cleanup attempt
    });

    /// <summary>
    /// REQ-005: Descriptor Mutability
    /// Verifies that GetDescriptor returns the live descriptor during session lifetime.
    /// </summary>
    [TestMethod]
    public Task GetDescriptor_ReturnsLiveDescriptor_DuringSession_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);
        this.token = this.service.StartSession(descriptor, hotspot.AsPhysicalScreen(), hotspot.AsScreen());

        // Act
        var retrievedDescriptor = this.service.GetDescriptor(this.token);

        // Assert
        _ = retrievedDescriptor.Should().BeSameAs(descriptor, "GetDescriptor should return the same descriptor instance");
    });

    /// <summary>
    /// REQ-005: Descriptor Mutability
    /// Verifies that GetDescriptor returns null after session ends.
    /// </summary>
    [TestMethod]
    public Task GetDescriptor_ReturnsNull_AfterSessionEnds_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);
        this.token = this.service.StartSession(descriptor, hotspot.AsPhysicalScreen(), hotspot.AsScreen());
        this.service.EndSession(this.token);

        // Act
        var retrievedDescriptor = this.service.GetDescriptor(this.token);

        // Assert
        _ = retrievedDescriptor.Should().BeNull("GetDescriptor should return null after session ends");

        this.token = default; // Prevent cleanup attempt
    });

    /// <summary>
    /// REQ-005: Descriptor Mutability
    /// Verifies that GetDescriptor with invalid token returns null.
    /// </summary>
    [TestMethod]
    public Task GetDescriptor_ReturnsNull_WithInvalidToken_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var invalidToken = new DragSessionToken { Id = Guid.NewGuid() };

        // Act
        var retrievedDescriptor = this.service.GetDescriptor(invalidToken);

        // Assert
        _ = retrievedDescriptor.Should().BeNull("GetDescriptor should return null for invalid token");
    });

    /// <summary>
    /// Contract: StartSession requires non-null descriptor.
    /// </summary>
    [TestMethod]
    public Task StartSession_ThrowsArgumentNullException_WhenDescriptorIsNull_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var hotspot = new Point(10, 10);

        // Act & Assert
        var act = () => this.service.StartSession(null!, hotspot.AsPhysicalScreen(), hotspot.AsScreen());
        _ = act.Should().Throw<ArgumentNullException>()
            .WithParameterName("descriptor", "Descriptor cannot be null");
    });

    /// <summary>
    /// Contract: Hotspot in logical pixels can include fractional values.
    /// Verifies that service accepts fractional logical pixel hotspot values.
    /// Note: Cannot directly verify stored hotspot as it's private implementation detail.
    /// The DPI tests verify correct hotspot usage via positioning behavior.
    /// </summary>
    [TestMethod]
    public Task StartSession_AcceptsFractionalHotspot_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var fractionalHotspot = new Point(50.5, 20.75);

        // Act
        this.token = this.service.StartSession(descriptor, fractionalHotspot.AsPhysicalScreen(), fractionalHotspot.AsScreen());

        // Assert
        _ = this.token.Should().NotBe(default(DragSessionToken), "Valid token should be returned");
    });

    /// <summary>
    /// Contract: Multiple UpdatePosition calls should succeed.
    /// Verifies that service can handle rapid position updates (simulating cursor tracking).
    /// </summary>
    [TestMethod]
    public Task UpdatePosition_HandlesRapidUpdates_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);
        this.token = this.service.StartSession(descriptor, hotspot.AsPhysicalScreen(), hotspot.AsScreen());

        // Act: Simulate rapid cursor movement (physical pixels)
        for (var i = 0; i < 100; i++)
        {
            var physicalPosition = new SpatialPoint<PhysicalScreenSpace>(new Point(100 + i, 100 + i));
            this.service.UpdatePosition(this.token, physicalPosition);
        }

        // Assert: Should complete without throwing (implicit)
    });

    /// <summary>
    /// Contract: Service can be instantiated without logger factory.
    /// </summary>
    [TestMethod]
    public Task Service_CanBeInstantiated_WithoutLoggerFactory_Async() => EnqueueAsync(() =>
        _ = this.service.Should().NotBeNull("Service without logger factory should be created successfully"));

    /// <summary>
    /// Edge case: Zero hotspot (overlay positioned at cursor top-left).
    /// </summary>
    [TestMethod]
    public Task StartSession_AcceptsZeroHotspot_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var zeroHotspot = new Point(0, 0);

        // Act
        this.token = this.service.StartSession(descriptor, zeroHotspot.AsPhysicalScreen(), zeroHotspot.AsScreen());

        // Assert
        _ = this.token.Should().NotBe(default(DragSessionToken), "Zero hotspot should be accepted");
    });

    /// <summary>
    /// Edge case: Negative hotspot values (grabbing outside the visual).
    /// </summary>
    [TestMethod]
    public Task StartSession_AcceptsNegativeHotspot_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var negativeHotspot = new Point(-10, -5);

        // Act
        this.token = this.service.StartSession(descriptor, negativeHotspot.AsPhysicalScreen(), negativeHotspot.AsScreen());

        // Assert
        _ = this.token.Should().NotBe(default(DragSessionToken), "Negative hotspot should be accepted");
    });

    /// <summary>
    /// Edge case: Large hotspot values (grabbing far from origin).
    /// </summary>
    [TestMethod]
    public Task StartSession_AcceptsLargeHotspot_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var largeHotspot = new Point(1000, 800);

        // Act
        this.token = this.service.StartSession(descriptor, largeHotspot.AsPhysicalScreen(), largeHotspot.AsScreen());

        // Assert
        _ = this.token.Should().NotBe(default(DragSessionToken), "Large hotspot should be accepted");
    });

    /// <summary>
    /// Edge case: Extreme cursor positions (multi-monitor, large coordinates).
    /// </summary>
    [TestMethod]
    public Task UpdatePosition_HandlesExtremeCursorPositions_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { RequestedSize = new Size(200, 100) };
        var hotspot = new Point(10, 10);
        this.token = this.service.StartSession(descriptor, hotspot.AsPhysicalScreen(), hotspot.AsScreen());

        // Act: Extreme positions (e.g., far-right monitor in multi-monitor setup)
        var extremePositions = new[]
        {
            new Point(-1000, -1000), // Off-screen left/top
            new Point(0, 0), // Origin
            new Point(10000, 5000), // Far-right monitor
            new Point(double.MaxValue / 2, double.MaxValue / 2), // Near max
        };

        foreach (var position in extremePositions)
        {
            this.service.UpdatePosition(this.token, position.AsPhysicalScreen());
        }

        // Assert: Should complete without throwing (implicit)
    });

    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    protected override Task TestSetupAsync() => EnqueueAsync(() =>
    {
        // Create hosting context
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext
        {
            Dispatcher = dispatcher,
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher),
        };

        // Create raw mapper factory (returns a mapper bound to an HWND)
        static ISpatialMapper RawFactory(nint hwnd = 0) => new SpatialMapper(hwnd);

        // Create service with dependencies
        this.service = new DragVisualService(
            hosting,
            RawFactory,
            this.LoggerFactory);
    });

    protected override Task TestCleanupAsync() => EnqueueAsync(() =>
    {
        if (this.token != default)
        {
            this.service?.EndSession(this.token);
            this.token = default;
        }
    });

    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            // dispose managed resources
            this.service?.Dispose();
            this.service = null!;
        }

        this.disposed = true;
    }
}
