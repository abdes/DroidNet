// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("SpatialMapperCompositionTests")]
[TestCategory("UITest")]
public class SpatialMapperCompositionTests : SpatialMapperTestBase
{
    [TestMethod]
    public Task Mapper_Convert_ElementToPhysical_ThreeLevelComposition_Async() => EnqueueAsync(() =>
    {
        // Arrange - Element→Window→Screen→Physical (3-level composition)
        var elementPoint = new Point(10, 20);

        // Act
        var physical = this.Mapper.Convert<ElementSpace, PhysicalScreenSpace>(new SpatialPoint<ElementSpace>(elementPoint));

        // Manually verify the chain
        var window = this.Mapper.Convert<ElementSpace, WindowSpace>(new SpatialPoint<ElementSpace>(elementPoint));
        var screen = this.Mapper.Convert<WindowSpace, ScreenSpace>(window);
        var expectedPhysical = this.Mapper.Convert<ScreenSpace, PhysicalScreenSpace>(screen);

        // Assert - Should match manual 3-level composition
        _ = physical.Point.X.Should().BeApproximately(expectedPhysical.Point.X, 1.0);
        _ = physical.Point.Y.Should().BeApproximately(expectedPhysical.Point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_PhysicalToElement_ThreeLevelComposition_Async() => EnqueueAsync(() =>
    {
        // Arrange - Physical→Window→Element (2-level composition)
        var physicalOrigin = this.PhysicalClientOrigin;
        var physicalPoint = new Point(physicalOrigin.X + 50, physicalOrigin.Y + 75);

        // Act
        var element = this.Mapper.Convert<PhysicalScreenSpace, ElementSpace>(new SpatialPoint<PhysicalScreenSpace>(physicalPoint));

        // Manually verify the chain
        var window = this.Mapper.Convert<PhysicalScreenSpace, WindowSpace>(new SpatialPoint<PhysicalScreenSpace>(physicalPoint));
        var expectedElement = this.Mapper.Convert<WindowSpace, ElementSpace>(window);

        // Assert - Should match manual 2-level composition
        _ = element.Point.X.Should().BeApproximately(expectedElement.Point.X, 1.0);
        _ = element.Point.Y.Should().BeApproximately(expectedElement.Point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_WindowToPhysical_TwoLevelComposition_Async() => EnqueueAsync(() =>
    {
        // Arrange - Window→Screen→Physical (2-level composition)
        var windowPoint = new Point(25, 35);

        // Act
        var physical = this.Mapper.Convert<WindowSpace, PhysicalScreenSpace>(new SpatialPoint<WindowSpace>(windowPoint));

        // Manually verify the chain
        var screen = this.Mapper.Convert<WindowSpace, ScreenSpace>(new SpatialPoint<WindowSpace>(windowPoint));
        var expectedPhysical = this.Mapper.Convert<ScreenSpace, PhysicalScreenSpace>(screen);

        // Assert - Should match manual 2-level composition
        _ = physical.Point.X.Should().BeApproximately(expectedPhysical.Point.X, 1.0);
        _ = physical.Point.Y.Should().BeApproximately(expectedPhysical.Point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_PhysicalToWindow_DirectConversion_Async() => EnqueueAsync(() =>
    {
        // Arrange - Physical→Window is a direct 1-level conversion
        var dpi = this.WindowDpi;
        var physicalOrigin = this.PhysicalClientOrigin;

        const int dx = 30;
        const int dy = 40;
        var physicalPoint = new Point(physicalOrigin.X + dx, physicalOrigin.Y + dy);

        // Act
        var window = this.Mapper.Convert<PhysicalScreenSpace, WindowSpace>(new SpatialPoint<PhysicalScreenSpace>(physicalPoint));

        // Assert - Verify by roundtripping: map back to physical and compare offsets
        var backToPhysical = this.Mapper.Convert<WindowSpace, PhysicalScreenSpace>(window);
        var roundDx = backToPhysical.Point.X - physicalOrigin.X;
        var roundDy = backToPhysical.Point.Y - physicalOrigin.Y;

        _ = roundDx.Should().BeApproximately(dx, 1.0);
        _ = roundDy.Should().BeApproximately(dy, 1.0);
    });

    [TestMethod]
    public Task Mapper_RoundTrip_ElementToPhysicalToElement_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var original = new SpatialPoint<ElementSpace>(new Point(15, 25));

        // Act - Full round trip through physical
        var physical = this.Mapper.Convert<ElementSpace, PhysicalScreenSpace>(original);
        var back = this.Mapper.Convert<PhysicalScreenSpace, ElementSpace>(physical);

        // Assert - Should return to original (within rounding tolerance)
        _ = back.Point.X.Should().BeApproximately(original.Point.X, 1.0);
        _ = back.Point.Y.Should().BeApproximately(original.Point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_RoundTrip_WindowToPhysicalToWindow_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var original = new SpatialPoint<WindowSpace>(new Point(100, 150));

        // Act
        var physical = this.Mapper.Convert<WindowSpace, PhysicalScreenSpace>(original);
        var back = this.Mapper.Convert<PhysicalScreenSpace, WindowSpace>(physical);

        // Assert
        _ = back.Point.X.Should().BeApproximately(original.Point.X, 1.0);
        _ = back.Point.Y.Should().BeApproximately(original.Point.Y, 1.0);
    });
}
