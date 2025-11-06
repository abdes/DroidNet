// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("SpatialPointTests")]
public class SpatialPointTests
{
    [TestMethod]
    public void SpatialPoint_Construct_With_Point()
    {
        // Arrange
        var raw = new Point(42, 99);

        // Act
        var spatial = new SpatialPoint<ElementSpace>(raw);

        // Assert
        _ = spatial.Point.Should().Be(raw);
        _ = spatial.ToString().Should().Contain(nameof(ElementSpace));
    }

    [TestMethod]
    public void SpatialPoint_ToString_Includes_Space()
    {
        // Arrange
        var spatial = new SpatialPoint<ElementSpace>(new Point(1, 2));

        // Act
        var result = spatial.ToString();

        // Assert
        _ = result.Should().Contain("ElementSpace");
    }

    [TestMethod]
    public void SpatialPoint_ToPoint_Returns_RawPoint()
    {
        // Arrange
        var raw = new Point(1, 2);
        var spatial = new SpatialPoint<ElementSpace>(raw);

        // Act
        var result = spatial.Point;

        // Assert
        _ = result.Should().Be(raw);
    }

    [TestMethod]
    public void SpatialPoint_Add_SameSpace_Returns_Sum()
    {
        // Arrange
        var a = new SpatialPoint<ElementSpace>(new Point(1, 2));
        var b = new SpatialPoint<ElementSpace>(new Point(3, 4));

        // Act
        var sum = a + b;

        // Assert
        _ = sum.Point.Should().Be(new Point(4, 6));
    }

    [TestMethod]
    public void SpatialPoint_Subtract_SameSpace_Returns_Difference()
    {
        // Arrange
        var a = new SpatialPoint<ElementSpace>(new Point(5, 6));
        var b = new SpatialPoint<ElementSpace>(new Point(3, 4));

        // Act
        var diff = a - b;

        // Assert
        _ = diff.Point.Should().Be(new Point(2, 2));
    }

#pragma warning disable IDE0022 // Use expression body for method
    [TestMethod]
    public void SpatialPoint_Add_DifferentSpace_CompileError()
    {
        // This test demonstrates that adding SpatialPoints of different spaces does not compile
        // Uncommenting the following lines would cause a compile error:
        // var a = new SpatialPoint<ElementSpace>(new Point(1, 2));
        // var b = new SpatialPoint<WindowSpace>(new Point(3, 4));
        // var sum = a + b; // Compile error: cannot implicitly convert
        Assert.Inconclusive("This test shows that adding different spaces should not compile.");
    }

    [TestMethod]
    public void SpatialPoint_Subtract_DifferentSpace_CompileError()
    {
        // This test demonstrates that subtracting SpatialPoints of different spaces does not compile
        // Uncommenting the following lines would cause a compile error:
        // var a = new SpatialPoint<ElementSpace>(new Point(1, 2));
        // var b = new SpatialPoint<WindowSpace>(new Point(3, 4));
        // var diff = a - b; // Compile error: cannot implicitly convert
        Assert.Inconclusive("This test shows that subtracting different spaces should not compile.");
    }
#pragma warning restore IDE0022 // Use expression body for method

    [TestMethod]
    public void SpatialPoint_Equality_SameSpace_SamePoint()
    {
        // Arrange
        var point = new Point(10, 20);
        var a = new SpatialPoint<ElementSpace>(point);
        var b = new SpatialPoint<ElementSpace>(point);

        // Act & Assert
        _ = a.Should().Be(b);
        _ = a.GetHashCode().Should().Be(b.GetHashCode());
    }

    [TestMethod]
    public void SpatialPoint_Equality_DifferentSpace_SamePoint_NotEqual()
    {
        // Arrange
        var point = new Point(10, 20);
        var a = new SpatialPoint<ElementSpace>(point);
        var b = new SpatialPoint<WindowSpace>(point);

        // Act & Assert
        _ = a.Should().NotBe(b);
    }

    [TestMethod]
    public void SpatialPoint_Construct_With_ZeroPoint()
    {
        // Arrange
        var raw = new Point(0, 0);

        // Act
        var spatial = new SpatialPoint<ElementSpace>(raw);

        // Assert
        _ = spatial.Point.Should().Be(raw);
    }

    [TestMethod]
    public void SpatialPoint_Construct_With_NegativePoint()
    {
        // Arrange
        var raw = new Point(-5, -10);

        // Act
        var spatial = new SpatialPoint<ElementSpace>(raw);

        // Assert
        _ = spatial.Point.Should().Be(raw);
    }

    [TestMethod]
    public void SpatialPoint_Add_With_Zero()
    {
        // Arrange
        var a = new SpatialPoint<ElementSpace>(new Point(5, 10));
        var zero = new SpatialPoint<ElementSpace>(new Point(0, 0));

        // Act
        var sum = a + zero;

        // Assert
        _ = sum.Point.Should().Be(new Point(5, 10));
    }

    [TestMethod]
    public void SpatialPoint_Subtract_With_Zero()
    {
        // Arrange
        var a = new SpatialPoint<ElementSpace>(new Point(5, 10));
        var zero = new SpatialPoint<ElementSpace>(new Point(0, 0));

        // Act
        var diff = a - zero;

        // Assert
        _ = diff.Point.Should().Be(new Point(5, 10));
    }

    [TestMethod]
    public void SpatialPoint_ToString_For_WindowSpace()
    {
        // Arrange
        var spatial = new SpatialPoint<WindowSpace>(new Point(1, 2));

        // Act
        var result = spatial.ToString();

        // Assert
        _ = result.Should().Contain("WindowSpace");
    }

    [TestMethod]
    public void SpatialPoint_ToString_For_ScreenSpace()
    {
        // Arrange
        var spatial = new SpatialPoint<ScreenSpace>(new Point(1, 2));

        // Act
        var result = spatial.ToString();

        // Assert
        _ = result.Should().Contain("ScreenSpace");
    }
}
