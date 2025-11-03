// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("SpatialPointExtensionsTests")]
[TestCategory("Phase3")]
public class SpatialPointExtensionsTests
{
    [TestMethod]
    public void Point_AsElement_Returns_ElementSpacePoint()
    {
        // Arrange
        var point = new Point(10, 20);

        // Act
        var spatial = point.AsElement();

        // Assert
        _ = spatial.Point.Should().Be(point);
        _ = spatial.ToString().Should().Contain("ElementSpace");
    }

    [TestMethod]
    public void Point_AsWindow_Returns_WindowSpacePoint()
    {
        // Arrange
        var point = new Point(15, 25);

        // Act
        var spatial = point.AsWindow();

        // Assert
        _ = spatial.Point.Should().Be(point);
        _ = spatial.ToString().Should().Contain("WindowSpace");
    }

    [TestMethod]
    public void Point_AsScreen_Returns_ScreenSpacePoint()
    {
        // Arrange
        var point = new Point(30, 40);

        // Act
        var spatial = point.AsScreen();

        // Assert
        _ = spatial.Point.Should().Be(point);
        _ = spatial.ToString().Should().Contain("ScreenSpace");
    }

    [TestMethod]
    public void SpatialPoint_ToPoint_Extension_Matches_RawPoint()
    {
        // Arrange
        var raw = new Point(5, 10);
        var spatial = new SpatialPoint<ElementSpace>(raw);

        // Act
        var result = spatial.ToPoint();

        // Assert
        _ = result.Should().Be(raw);
    }

    [TestMethod]
    public void SpatialPoint_ToPoint_For_WindowSpace()
    {
        // Arrange
        var raw = new Point(15, 25);
        var spatial = new SpatialPoint<WindowSpace>(raw);

        // Act
        var result = spatial.ToPoint();

        // Assert
        _ = result.Should().Be(raw);
    }

    [TestMethod]
    public void SpatialPoint_ToPoint_For_ScreenSpace()
    {
        // Arrange
        var raw = new Point(30, 40);
        var spatial = new SpatialPoint<ScreenSpace>(raw);

        // Act
        var result = spatial.ToPoint();

        // Assert
        _ = result.Should().Be(raw);
    }

    [TestMethod]
    public void Point_AsElement_ZeroPoint()
    {
        // Arrange
        var point = new Point(0, 0);

        // Act
        var spatial = point.AsElement();

        // Assert
        _ = spatial.Point.Should().Be(point);
    }

    [TestMethod]
    public void Point_AsWindow_NegativePoint()
    {
        // Arrange
        var point = new Point(-10, -20);

        // Act
        var spatial = point.AsWindow();

        // Assert
        _ = spatial.Point.Should().Be(point);
    }
}
