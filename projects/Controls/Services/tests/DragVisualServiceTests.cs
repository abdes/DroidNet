// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;

namespace DroidNet.Controls.Services.Tests;

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
[TestCategory("Phase3")]
public class DragVisualServiceTests : VisualUserInterfaceTests
{
    public required TestContext TestContext { get; set; }

    /// <summary>
    /// REQ-001: Single Active Session
    /// Verifies that only one session can exist per process.
    /// StartSession must throw InvalidOperationException if a session is already active.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task StartSession_ThrowsWhenSessionAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor1 = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var descriptor2 = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);

        var token1 = service.StartSession(descriptor1, hotspot);

        // Act & Assert
        var act = () => service.StartSession(descriptor2, hotspot);
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("*already active*", "Only one session allowed per process (REQ-001)");

        // Cleanup
        service.EndSession(token1);
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// REQ-001: Single Active Session
    /// Verifies that a new session can be started after the previous one ends.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task StartSession_SucceedsAfterPreviousSessionEnds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor1 = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var descriptor2 = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);

        var token1 = service.StartSession(descriptor1, hotspot);
        service.EndSession(token1);

        // Act - Start second session after first ended
        DragSessionToken token2 = default;
        var act = () => token2 = service.StartSession(descriptor2, hotspot);

        // Assert
        _ = act.Should().NotThrow("New session should be allowed after previous session ends");
        _ = token2.Should().NotBe(default(DragSessionToken), "Valid token should be returned");

        // Cleanup
        service.EndSession(token2);
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that EndSession with valid token succeeds.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task EndSession_SucceedsWithValidToken_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);
        var token = service.StartSession(descriptor, hotspot);

        // Act & Assert
        var act = () => service.EndSession(token);
        _ = act.Should().NotThrow("EndSession with valid token should succeed");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that EndSession with invalid token is handled gracefully (no-op).
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task EndSession_WithInvalidToken_IsNoOp_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var invalidToken = new DragSessionToken { Id = Guid.NewGuid() };

        // Act & Assert
        var act = () => service.EndSession(invalidToken);
        _ = act.Should().NotThrow("EndSession with invalid token should be no-op");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that calling EndSession twice is handled gracefully.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task EndSession_CalledTwice_IsHandledGracefully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);
        var token = service.StartSession(descriptor, hotspot);
        service.EndSession(token);

        // Act & Assert
        var act = () => service.EndSession(token);
        _ = act.Should().NotThrow("Second EndSession should be no-op");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that UpdatePosition with valid token succeeds.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task UpdatePosition_SucceedsWithValidToken_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);
        var token = service.StartSession(descriptor, hotspot);

        // Act & Assert - Physical pixels from GetCursorPos
        var physicalPosition = new Windows.Foundation.Point(500, 300);
        var act = () => service.UpdatePosition(token, physicalPosition);
        _ = act.Should().NotThrow("UpdatePosition with valid token should succeed");

        // Cleanup
        service.EndSession(token);
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that UpdatePosition with invalid token is handled gracefully (no-op).
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task UpdatePosition_WithInvalidToken_IsNoOp_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var invalidToken = new DragSessionToken { Id = Guid.NewGuid() };
        var physicalPosition = new Windows.Foundation.Point(500, 300);

        // Act & Assert
        var act = () => service.UpdatePosition(invalidToken, physicalPosition);
        _ = act.Should().NotThrow("UpdatePosition with invalid token should be no-op");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that UpdatePosition after EndSession is handled gracefully.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task UpdatePosition_AfterEndSession_IsNoOp_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);
        var token = service.StartSession(descriptor, hotspot);
        service.EndSession(token);

        // Act & Assert
        var physicalPosition = new Windows.Foundation.Point(500, 300);
        var act = () => service.UpdatePosition(token, physicalPosition);
        _ = act.Should().NotThrow("UpdatePosition after EndSession should be no-op");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// REQ-005: Descriptor Mutability
    /// Verifies that GetDescriptor returns the live descriptor during session lifetime.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task GetDescriptor_ReturnsLiveDescriptor_DuringSession_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);
        var token = service.StartSession(descriptor, hotspot);

        // Act
        var retrievedDescriptor = service.GetDescriptor(token);

        // Assert
        _ = retrievedDescriptor.Should().BeSameAs(descriptor, "GetDescriptor should return the same descriptor instance");

        // Cleanup
        service.EndSession(token);
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// REQ-005: Descriptor Mutability
    /// Verifies that GetDescriptor returns null after session ends.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task GetDescriptor_ReturnsNull_AfterSessionEnds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);
        var token = service.StartSession(descriptor, hotspot);
        service.EndSession(token);

        // Act
        var retrievedDescriptor = service.GetDescriptor(token);

        // Assert
        _ = retrievedDescriptor.Should().BeNull("GetDescriptor should return null after session ends");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// REQ-005: Descriptor Mutability
    /// Verifies that GetDescriptor with invalid token returns null.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task GetDescriptor_ReturnsNull_WithInvalidToken_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var invalidToken = new DragSessionToken { Id = Guid.NewGuid() };

        // Act
        var retrievedDescriptor = service.GetDescriptor(invalidToken);

        // Assert
        _ = retrievedDescriptor.Should().BeNull("GetDescriptor should return null for invalid token");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Contract: StartSession requires non-null descriptor.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task StartSession_ThrowsArgumentNullException_WhenDescriptorIsNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var hotspot = new Windows.Foundation.Point(10, 10);

        // Act & Assert
        var act = () => service.StartSession(null!, hotspot);
        _ = act.Should().Throw<ArgumentNullException>()
            .WithParameterName("descriptor", "Descriptor cannot be null");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Contract: Hotspot in logical pixels can include fractional values.
    /// Verifies that service accepts fractional logical pixel hotspot values.
    /// Note: Cannot directly verify stored hotspot as it's private implementation detail.
    /// The DPI tests verify correct hotspot usage via positioning behavior.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task StartSession_AcceptsFractionalHotspot_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var fractionalHotspot = new Windows.Foundation.Point(50.5, 20.75);

        // Act
        var token = service.StartSession(descriptor, fractionalHotspot);

        // Assert: Service should accept fractional logical pixels without throwing
        _ = token.Should().NotBe(default(DragSessionToken), "Valid token should be returned");

        // Cleanup
        service.EndSession(token);
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Contract: Multiple UpdatePosition calls should succeed.
    /// Verifies that service can handle rapid position updates (simulating cursor tracking).
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task UpdatePosition_HandlesRapidUpdates_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);
        var token = service.StartSession(descriptor, hotspot);

        // Act: Simulate rapid cursor movement (physical pixels)
        for (var i = 0; i < 100; i++)
        {
            var physicalPosition = new Windows.Foundation.Point(100 + i, 100 + i);
            service.UpdatePosition(token, physicalPosition);
        }

        // Assert: Should complete without throwing
        // (No explicit assertion needed - test passes if no exception thrown)

        // Cleanup
        service.EndSession(token);
        await Task.CompletedTask.ConfigureAwait(true);
    });


    // TODO: add a test case that validates strategy will log messages when loggerfactory is provided

    /// <summary>
    /// Contract: Service can be instantiated without logger factory.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task Service_CanBeInstantiated_WithoutLoggerFactory_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var service = new DragVisualService();

        // Assert
        _ = service.Should().NotBeNull("Service without logger factory should be created successfully");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Edge case: Zero hotspot (overlay positioned at cursor top-left).
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task StartSession_AcceptsZeroHotspot_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var zeroHotspot = new Windows.Foundation.Point(0, 0);

        // Act
        var token = service.StartSession(descriptor, zeroHotspot);

        // Assert
        _ = token.Should().NotBe(default(DragSessionToken), "Zero hotspot should be accepted");

        // Cleanup
        service.EndSession(token);
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Edge case: Negative hotspot values (grabbing outside the visual).
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task StartSession_AcceptsNegativeHotspot_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var negativeHotspot = new Windows.Foundation.Point(-10, -5);

        // Act
        var token = service.StartSession(descriptor, negativeHotspot);

        // Assert
        _ = token.Should().NotBe(default(DragSessionToken), "Negative hotspot should be accepted");

        // Cleanup
        service.EndSession(token);
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Edge case: Large hotspot values (grabbing far from origin).
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task StartSession_AcceptsLargeHotspot_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var largeHotspot = new Windows.Foundation.Point(1000, 800);

        // Act
        var token = service.StartSession(descriptor, largeHotspot);

        // Assert
        _ = token.Should().NotBe(default(DragSessionToken), "Large hotspot should be accepted");

        // Cleanup
        service.EndSession(token);
        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Edge case: Extreme cursor positions (multi-monitor, large coordinates).
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task UpdatePosition_HandlesExtremeCursorPositions_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var service = new DragVisualService();
        var descriptor = new DragVisualDescriptor { RequestedSize = new Windows.Foundation.Size(200, 100) };
        var hotspot = new Windows.Foundation.Point(10, 10);
        var token = service.StartSession(descriptor, hotspot);

        // Act: Extreme positions (e.g., far-right monitor in multi-monitor setup)
        var extremePositions = new[]
        {
            new Windows.Foundation.Point(-1000, -1000), // Off-screen left/top
            new Windows.Foundation.Point(0, 0), // Origin
            new Windows.Foundation.Point(10000, 5000), // Far-right monitor
            new Windows.Foundation.Point(double.MaxValue / 2, double.MaxValue / 2), // Near max
        };

        foreach (var position in extremePositions)
        {
            service.UpdatePosition(token, position);
        }

        // Assert: Should complete without throwing
        // (No explicit assertion needed - test passes if no exception thrown)

        // Cleanup
        service.EndSession(token);
        await Task.CompletedTask.ConfigureAwait(true);
    });
}
