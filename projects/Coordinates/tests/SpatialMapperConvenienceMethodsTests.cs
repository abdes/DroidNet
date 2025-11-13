// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("SpatialMapperConvenienceMethodsTests")]
[TestCategory("UITest")]
public class SpatialMapperConvenienceMethodsTests : SpatialMapperTestBase
{
    [TestMethod]
    public Task Mapper_ToPhysicalScreen_FromElement_Works_Async() => EnqueueAsync(() =>
    {
        // Act
        var physical = this.Mapper.ToPhysicalScreen(new SpatialPoint<ElementSpace>(new Point(10, 20)));

        // Assert
        _ = physical.Should().BeOfType<SpatialPoint<PhysicalScreenSpace>>();
        _ = physical.Point.X.Should().BeGreaterThan(0);
        _ = physical.Point.Y.Should().BeGreaterThan(0);
    });

    [TestMethod]
    public Task Mapper_ToPhysicalScreen_FromWindow_Works_Async() => EnqueueAsync(() =>
    {
        // Act
        var physical = this.Mapper.ToPhysicalScreen(new SpatialPoint<WindowSpace>(new Point(50, 75)));

        // Assert
        _ = physical.Should().BeOfType<SpatialPoint<PhysicalScreenSpace>>();
    });

    [TestMethod]
    public Task Mapper_ToPhysicalScreen_FromScreen_Works_Async() => EnqueueAsync(() =>
    {
        // Act
        var physical = this.Mapper.ToPhysicalScreen(new SpatialPoint<ScreenSpace>(new Point(100, 150)));

        // Assert
        _ = physical.Should().BeOfType<SpatialPoint<PhysicalScreenSpace>>();
    });

    [TestMethod]
    public Task Mapper_ToPhysicalScreen_FromPhysical_IsNoOp_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var original = new Point(2000, 1500);

        // Act
        var result = this.Mapper.ToPhysicalScreen(new SpatialPoint<PhysicalScreenSpace>(original));

        // Assert - Physical→Physical should be identity
        _ = result.Point.Should().Be(original);
    });

    [TestMethod]
    public Task Mapper_ToScreen_FromAllSources_Works_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var physicalOrigin = this.PhysicalClientOrigin;

        // Act
        var results = new[]
        {
            this.Mapper.ToScreen(new SpatialPoint<ElementSpace>(new Point(5, 10))),
            this.Mapper.ToScreen(new SpatialPoint<WindowSpace>(new Point(5, 10))),
            this.Mapper.ToScreen(new SpatialPoint<ScreenSpace>(new Point(5, 10))),
            this.Mapper.ToScreen(new SpatialPoint<PhysicalScreenSpace>(physicalOrigin)),
        };

        // Assert
        _ = results.Should().AllSatisfy(r => r.Should().BeOfType<SpatialPoint<ScreenSpace>>());
    });

    [TestMethod]
    public Task Mapper_ToWindow_FromAllSources_Works_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var physicalOrigin = this.PhysicalClientOrigin;

        // Act
        var results = new[]
        {
            this.Mapper.ToWindow(new SpatialPoint<ElementSpace>(new Point(5, 10))),
            this.Mapper.ToWindow(new SpatialPoint<WindowSpace>(new Point(5, 10))),
            this.Mapper.ToWindow(new SpatialPoint<ScreenSpace>(new Point(100, 200))),
            this.Mapper.ToWindow(new SpatialPoint<PhysicalScreenSpace>(new Point(physicalOrigin.X + 10, physicalOrigin.Y + 20))),
        };

        // Assert
        _ = results.Should().AllSatisfy(r => r.Should().BeOfType<SpatialPoint<WindowSpace>>());
    });

    [TestMethod]
    public Task Mapper_ToElement_FromAllSources_Works_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var physicalOrigin = this.PhysicalClientOrigin;

        // Act
        var results = new[]
        {
            this.Mapper.ToElement(new SpatialPoint<ElementSpace>(new Point(5, 10))),
            this.Mapper.ToElement(new SpatialPoint<WindowSpace>(new Point(5, 10))),
            this.Mapper.ToElement(new SpatialPoint<ScreenSpace>(new Point(100, 200))),
            this.Mapper.ToElement(new SpatialPoint<PhysicalScreenSpace>(new Point(physicalOrigin.X + 10, physicalOrigin.Y + 20))),
        };

        // Assert
        _ = results.Should().AllSatisfy(r => r.Should().BeOfType<SpatialPoint<ElementSpace>>());
    });
}
