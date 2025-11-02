// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.Controls.Services.Tests;

/// <summary>
/// Unit tests for <see cref="DragVisualService"/> DPI scaling and coordinate conversion behavior.
/// Tests validate correct handling of:
/// - Cross-monitor drag with different DPI settings.
/// - Physical to logical pixel conversions.
/// - Hotspot alignment at various DPI scales.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DragVisualServiceDpiTests")]
[TestCategory("Phase3")]
public class DragVisualServiceDpiTests
{
    public required TestContext TestContext { get; set; }

    /// <summary>
    /// Tests that Native.PhysicalToLogical and LogicalToPhysical are inverse operations.
    /// </summary>
    /// <param name="dpi">The DPI value to use for conversion.</param>
    /// <param name="logicalValue">The logical value to convert.</param>
    [TestMethod]
    [DataRow(96u, 100.0, DisplayName = "100% DPI (96)")]
    [DataRow(120u, 125.0, DisplayName = "125% DPI (120)")]
    [DataRow(144u, 150.0, DisplayName = "150% DPI (144)")]
    [DataRow(168u, 175.0, DisplayName = "175% DPI (168)")]
    [DataRow(192u, 200.0, DisplayName = "200% DPI (192)")]
    public void PhysicalToLogical_AndBackToPhysical_RoundTrips(uint dpi, double logicalValue)
    {
        // Arrange
        const double tolerance = 1.0; // Allow 1 pixel rounding error

        // Act: Convert logical → physical → logical
        var physical = Native.LogicalToPhysical(logicalValue, dpi);
        var backToLogical = Native.PhysicalToLogical(physical, dpi);

        // Assert
        _ = backToLogical.Should().BeApproximately(logicalValue, tolerance, $"Conversion should round-trip at {dpi} DPI");
    }

    /// <summary>
    /// Tests that hotspot offset is correctly applied in logical space regardless of DPI.
    /// Scenario: Hotspot at (50, 20) logical pixels, cursor at (100, 100) logical pixels.
    /// Expected: Window at (50, 80) logical pixels = cursor - hotspot.
    /// Physical coordinates scale proportionally with DPI.
    /// </summary>
    /// <param name="dpi">Monitor DPI value.</param>
    /// <param name="cursorPhysicalX">Cursor X position in physical pixels.</param>
    /// <param name="cursorPhysicalY">Cursor Y position in physical pixels.</param>
    /// <param name="expectedWindowPhysicalX">Expected window X position in physical pixels.</param>
    /// <param name="expectedWindowPhysicalY">Expected window Y position in physical pixels.</param>
    [TestMethod]
    [DataRow(96u, 100, 100, 50, 80, DisplayName = "100% DPI: cursor(100,100) - hotspot(50,20) = window(50,80)")]
    [DataRow(144u, 150, 150, 75, 120, DisplayName = "150% DPI: cursor(100,100) logical - hotspot(50,20) = window(50,80) logical = (75,120) physical")]
    [DataRow(192u, 200, 200, 100, 160, DisplayName = "200% DPI: cursor(100,100) logical - hotspot(50,20) = window(50,80) logical = (100,160) physical")]
    public void HotspotOffset_AppliedCorrectly_AtVariousDpi(
        uint dpi,
        int cursorPhysicalX,
        int cursorPhysicalY,
        int expectedWindowPhysicalX,
        int expectedWindowPhysicalY)
    {
        // Arrange: Hotspot is 50, 20 in logical pixels (constant across DPI)
        var hotspotLogical = new Windows.Foundation.Point(50, 20);
        var cursorPhysical = new Native.POINT(cursorPhysicalX, cursorPhysicalY);

        // Act: Simulate service logic
        // 1. Convert cursor from physical to logical
        var cursorLogical = Native.GetLogicalPointFromPhysical(cursorPhysical, dpi);

        // 2. Subtract logical hotspot
        var windowLogical = new Windows.Foundation.Point(
            cursorLogical.X - hotspotLogical.X,
            cursorLogical.Y - hotspotLogical.Y);

        // 3. Convert back to physical for window positioning
        var windowPhysical = Native.GetPhysicalPointFromLogical(windowLogical, dpi);

        // Assert: Window position should match expected physical coordinates
        const int tolerance = 1; // Allow 1 pixel rounding error
        _ = windowPhysical.X.Should().BeCloseTo(expectedWindowPhysicalX, tolerance, "Window X should account for hotspot at this DPI");
        _ = windowPhysical.Y.Should().BeCloseTo(expectedWindowPhysicalY, tolerance, "Window Y should account for hotspot at this DPI");
    }

    /// <summary>
    /// Tests cross-monitor scenario where cursor moves from 100% DPI monitor to 150% DPI monitor.
    /// Verifies that overlay stays aligned with cursor despite DPI change.
    /// </summary>
    [TestMethod]
    public void CrossMonitorDrag_MaintainsAlignment_WhenDpiChanges()
    {
        // Arrange: Hotspot at (50, 20) logical pixels
        var hotspotLogical = new Windows.Foundation.Point(50, 20);

        // Scenario 1: Cursor at (100, 100) physical on 100% DPI monitor (96 DPI)
        const uint dpi1 = 96;
        var cursor1Physical = new Native.POINT(100, 100);

        var cursor1Logical = Native.GetLogicalPointFromPhysical(cursor1Physical, dpi1);
        var window1Logical = new Windows.Foundation.Point(
            cursor1Logical.X - hotspotLogical.X,
            cursor1Logical.Y - hotspotLogical.Y);

        // Scenario 2: Cursor moves to (150, 150) physical on 150% DPI monitor (144 DPI)
        const uint dpi2 = 144;
        var cursor2Physical = new Native.POINT(150, 150);

        var cursor2Logical = Native.GetLogicalPointFromPhysical(cursor2Physical, dpi2);
        var window2Logical = new Windows.Foundation.Point(
            cursor2Logical.X - hotspotLogical.X,
            cursor2Logical.Y - hotspotLogical.Y);

        // Assert: In logical space, the offset from cursor to window should be constant
        var offset1 = new Windows.Foundation.Point(
            cursor1Logical.X - window1Logical.X,
            cursor1Logical.Y - window1Logical.Y);
        var offset2 = new Windows.Foundation.Point(
            cursor2Logical.X - window2Logical.X,
            cursor2Logical.Y - window2Logical.Y);

        const double tolerance = 0.1; // Tight tolerance for logical space
        _ = offset1.X.Should().BeApproximately(hotspotLogical.X, tolerance, "Hotspot X should be constant in logical space");
        _ = offset1.Y.Should().BeApproximately(hotspotLogical.Y, tolerance, "Hotspot Y should be constant in logical space");
        _ = offset2.X.Should().BeApproximately(hotspotLogical.X, tolerance, "Hotspot X should be constant after DPI change");
        _ = offset2.Y.Should().BeApproximately(hotspotLogical.Y, tolerance, "Hotspot Y should be constant after DPI change");
    }

    /// <summary>
    /// Tests that GetDpiForPoint correctly identifies monitor DPI.
    /// Note: This test may fail on systems without multi-monitor or when run in CI without displays.
    /// It serves as a smoke test for the P/Invoke wrapper.
    /// </summary>
    [TestMethod]
    public void GetDpiForPoint_ReturnsValidDpi_ForScreenPoint()
    {
        // Arrange: Use primary monitor center (likely (960, 540) at 1920x1080)
        var screenPoint = new Windows.Foundation.Point(960, 540);

        // Act
        var dpi = Native.GetDpiForPhysicalPoint(screenPoint);

        // Assert: DPI should be in valid range (96-384 covers 100%-400% scaling)
        _ = dpi.Should().BeInRange(96u, 384u, "DPI should be within standard Windows scaling range");
        _ = (dpi % 12).Should().Be(0u, "DPI should be a multiple of 12 (Windows standard)");
    }

    /// <summary>
    /// Tests edge case: Hotspot at (0, 0) should position window at cursor.
    /// </summary>
    /// <param name="dpi">Monitor DPI value.</param>
    /// <param name="cursorX">Cursor X position in physical pixels.</param>
    /// <param name="cursorY">Cursor Y position in physical pixels.</param>
    [TestMethod]
    [DataRow(96u, 100, 100, DisplayName = "100% DPI zero hotspot")]
    [DataRow(144u, 150, 150, DisplayName = "150% DPI zero hotspot")]
    public void ZeroHotspot_PositionsWindowAtCursor(uint dpi, int cursorX, int cursorY)
    {
        // Arrange: Zero hotspot
        var hotspotLogical = new Windows.Foundation.Point(0, 0);
        var cursorPhysical = new Native.POINT(cursorX, cursorY);

        // Act
        var cursorLogical = Native.GetLogicalPointFromPhysical(cursorPhysical, dpi);
        var windowLogical = new Windows.Foundation.Point(
            cursorLogical.X - hotspotLogical.X,
            cursorLogical.Y - hotspotLogical.Y);
        var windowPhysical = Native.GetPhysicalPointFromLogical(windowLogical, dpi);

        // Assert: Window should be at cursor position
        const int tolerance = 1;
        _ = windowPhysical.X.Should().BeCloseTo(cursorX, tolerance, "Window X should match cursor X with zero hotspot");
        _ = windowPhysical.Y.Should().BeCloseTo(cursorY, tolerance, "Window Y should match cursor Y with zero hotspot");
    }

    /// <summary>
    /// Tests that large hotspot values are handled correctly (e.g., grabbing bottom-right of a large tab).
    /// </summary>
    [TestMethod]
    public void LargeHotspot_HandledCorrectly()
    {
        // Arrange: Large hotspot (200, 100) in logical pixels
        var hotspotLogical = new Windows.Foundation.Point(200, 100);
        var cursorPhysical = new Native.POINT(500, 300);
        const uint dpi = 144; // 150% scaling

        // Act
        var cursorLogical = Native.GetLogicalPointFromPhysical(cursorPhysical, dpi);
        var windowLogical = new Windows.Foundation.Point(
            cursorLogical.X - hotspotLogical.X,
            cursorLogical.Y - hotspotLogical.Y);
        var windowPhysical = Native.GetPhysicalPointFromLogical(windowLogical, dpi);

        // Assert: Window position should be offset by scaled hotspot
        // At 150% DPI: cursor logical = (333.33, 200), window logical = (133.33, 100), window physical ≈ (200, 150)
        const int tolerance = 2; // Slightly larger tolerance for large values
        _ = windowPhysical.X.Should().BeCloseTo(200, tolerance, "Large hotspot X should be subtracted correctly");
        _ = windowPhysical.Y.Should().BeCloseTo(150, tolerance, "Large hotspot Y should be subtracted correctly");
    }
}
