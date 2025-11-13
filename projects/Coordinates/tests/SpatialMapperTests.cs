// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("SpatialMapperTests")]
[TestCategory("UITest")]
public class SpatialMapperTests : SpatialMapperTestBase
{
    [TestMethod]
    public Task Mapper_Convert_ElementToScreen_MapsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new Point(10, 20);
        var windowTopLeft = this.LogicalWindowTopLeft;
        var expected = new Point(windowTopLeft.X + point.X, windowTopLeft.Y + point.Y);

        // Act
        var result = this.Mapper.Convert<ElementSpace, ScreenSpace>(new SpatialPoint<ElementSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_WindowToScreen_MapsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new Point(15, 25);
        var windowTopLeft = this.LogicalWindowTopLeft;
        var expected = new Point(windowTopLeft.X + point.X, windowTopLeft.Y + point.Y);

        // Act
        var result = this.Mapper.Convert<WindowSpace, ScreenSpace>(new SpatialPoint<WindowSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_ScreenToElement_MapsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var screenPoint = new Point(30, 40);
        var windowTopLeft = this.LogicalWindowTopLeft;
        var expected = new Point(screenPoint.X - windowTopLeft.X, screenPoint.Y - windowTopLeft.Y);

        // Act
        var result = this.Mapper.Convert<ScreenSpace, ElementSpace>(new SpatialPoint<ScreenSpace>(screenPoint));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_ScreenToWindow_MapsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var screenPoint = new Point(35, 45);
        var windowTopLeft = this.LogicalWindowTopLeft;
        var expected = new Point(screenPoint.X - windowTopLeft.X, screenPoint.Y - windowTopLeft.Y);

        // Act
        var result = this.Mapper.Convert<ScreenSpace, WindowSpace>(new SpatialPoint<ScreenSpace>(screenPoint));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_ElementToWindow_MapsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new Point(5, 15);

        // Act
        var result = this.Mapper.Convert<ElementSpace, WindowSpace>(new SpatialPoint<ElementSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(point.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_WindowToElement_MapsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new Point(20, 30);

        // Act
        var result = this.Mapper.Convert<WindowSpace, ElementSpace>(new SpatialPoint<WindowSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(point.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_InvalidSourceSpace_Throws_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new SpatialPoint<object>(new Point(0, 0)); // object is not a valid space

        // Act & Assert
        Action act = () => this.Mapper.Convert<object, ScreenSpace>(point);
        _ = act.Should().Throw<NotSupportedException>().WithMessage("*Unsupported source space*");
    });

    [TestMethod]
    public Task Mapper_Convert_InvalidTargetSpace_Throws_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new SpatialPoint<ElementSpace>(new Point(0, 0));

        // Act & Assert
        Action act = () => this.Mapper.Convert<ElementSpace, object>(point);
        _ = act.Should().Throw<NotSupportedException>().WithMessage("*Unsupported target space*");
    });

    [TestMethod]
    public Task Mapper_Convert_RoundTrip_ElementToScreenToElement_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var original = new SpatialPoint<ElementSpace>(new Point(10, 20));

        // Act
        var screen = this.Mapper.Convert<ElementSpace, ScreenSpace>(original);
        var back = this.Mapper.Convert<ScreenSpace, ElementSpace>(screen);

        // Assert
        _ = back.Point.X.Should().BeApproximately(original.Point.X, 1.0);
        _ = back.Point.Y.Should().BeApproximately(original.Point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_With_ZeroPoint_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new Point(0, 0);
        var windowTopLeft = this.LogicalWindowTopLeft;
        var expected = new Point(windowTopLeft.X + point.X, windowTopLeft.Y + point.Y);

        // Act
        var result = this.Mapper.Convert<ElementSpace, ScreenSpace>(new SpatialPoint<ElementSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_ToScreen_Method_Works_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new SpatialPoint<ElementSpace>(new Point(5, 15));

        // Act
        var result = this.Mapper.ToScreen(point);

        // Assert
        _ = result.Should().BeOfType<SpatialPoint<ScreenSpace>>();
    });

    [TestMethod]
    public Task Mapper_ToWindow_Method_Works_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new SpatialPoint<ElementSpace>(new Point(5, 15));

        // Act
        var result = this.Mapper.ToWindow(point);

        // Assert
        _ = result.Should().BeOfType<SpatialPoint<WindowSpace>>();
    });

    [TestMethod]
    public Task Mapper_ToElement_Method_Works_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new SpatialPoint<ScreenSpace>(new Point(5, 15));

        // Act
        var result = this.Mapper.ToElement(point);

        // Assert
        _ = result.Should().BeOfType<SpatialPoint<ElementSpace>>();
    });
}
