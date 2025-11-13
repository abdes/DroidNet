// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("SpatialMapperPhysicalScreenTests")]
[TestCategory("UITest")]
public class SpatialMapperPhysicalScreenTests : SpatialMapperTestBase
{
    [TestMethod]
    public Task Mapper_Convert_Screen_To_PhysicalScreen_MapsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange - choose a logical screen point
        var windowTopLeft = this.LogicalWindowTopLeft;
        var screenLogical = new Point(windowTopLeft.X + 42, windowTopLeft.Y + 27);

        // Act - convert to physical and back
        var physical = this.Mapper.Convert<ScreenSpace, PhysicalScreenSpace>(new SpatialPoint<ScreenSpace>(screenLogical));
        var back = this.Mapper.Convert<PhysicalScreenSpace, ScreenSpace>(physical);

        // Assert - roundtrip should return approximately the original logical point
        _ = back.Point.X.Should().BeApproximately(screenLogical.X, 1.0);
        _ = back.Point.Y.Should().BeApproximately(screenLogical.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_PhysicalScreen_To_Screen_MapsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var physicalOrigin = this.PhysicalClientOrigin;
        var windowTopLeft = this.LogicalWindowTopLeft;

        // Act
        var result = this.Mapper.Convert<PhysicalScreenSpace, ScreenSpace>(new SpatialPoint<PhysicalScreenSpace>(physicalOrigin));

        // Assert
        _ = result.Point.X.Should().BeApproximately(windowTopLeft.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(windowTopLeft.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_ToPhysicalScreen_From_Element_Works_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var elementDelta = new Point(11, 13);

        // Act - map element -> physical -> element and verify roundtrip
        var toPhysical = this.Mapper.ToPhysicalScreen(new SpatialPoint<ElementSpace>(elementDelta));
        var backToElement = this.Mapper.Convert<PhysicalScreenSpace, ElementSpace>(toPhysical);

        // Assert - roundtrip should return approximately the original element delta
        _ = backToElement.Point.X.Should().BeApproximately(elementDelta.X, 1.0);
        _ = backToElement.Point.Y.Should().BeApproximately(elementDelta.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_RoundTrip_Screen_Physical_Screen_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var windowTopLeft = this.LogicalWindowTopLeft;
        var screenLogical = new Point(windowTopLeft.X + 7, windowTopLeft.Y + 9);

        // Act
        var toPhysical = this.Mapper.Convert<ScreenSpace, PhysicalScreenSpace>(new SpatialPoint<ScreenSpace>(screenLogical));
        var backToScreen = this.Mapper.Convert<PhysicalScreenSpace, ScreenSpace>(toPhysical);

        // Assert
        _ = backToScreen.Point.X.Should().BeApproximately(screenLogical.X, 1.0);
        _ = backToScreen.Point.Y.Should().BeApproximately(screenLogical.Y, 1.0);
    });
}
