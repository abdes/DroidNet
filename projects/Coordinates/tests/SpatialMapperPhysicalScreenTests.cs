// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
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
        // Arrange
        const int dx = 42;
        const int dy = 27;
        var windowTopLeft = this.GetLogicalWindowTopLeft();
        var screenLogical = new Point(windowTopLeft.X + dx, windowTopLeft.Y + dy);
        var expectedPhysical = Native.GetPhysicalScreenPointFromLogical(screenLogical);

        // Act
        var result = this.Mapper.Convert<ScreenSpace, PhysicalScreenSpace>(new SpatialPoint<ScreenSpace>(screenLogical));

        // Assert (approximate comparison due to rounding)
        _ = result.Point.X.Should().BeApproximately(expectedPhysical.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expectedPhysical.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_PhysicalScreen_To_Screen_MapsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var physicalOrigin = this.GetPhysicalClientOrigin();
        var windowTopLeft = this.GetLogicalWindowTopLeft();

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
        var dpi = this.GetWindowDpi();

        // Compute expected PHYSICAL target based on CLIENT origin to avoid non-client offset issues
        var clientOrigin = this.GetPhysicalClientOrigin();
        var dx = Native.LogicalToPhysical(elementDelta.X, dpi);
        var dy = Native.LogicalToPhysical(elementDelta.Y, dpi);
        var expectedPhysical = new Point(clientOrigin.X + dx, clientOrigin.Y + dy);

        // Act
        var result = this.Mapper.ToPhysicalScreen(new SpatialPoint<ElementSpace>(elementDelta));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expectedPhysical.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expectedPhysical.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_RoundTrip_Screen_Physical_Screen_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var windowTopLeft = this.GetLogicalWindowTopLeft();
        var screenLogical = new Point(windowTopLeft.X + 7, windowTopLeft.Y + 9);

        // Act
        var toPhysical = this.Mapper.Convert<ScreenSpace, PhysicalScreenSpace>(new SpatialPoint<ScreenSpace>(screenLogical));
        var backToScreen = this.Mapper.Convert<PhysicalScreenSpace, ScreenSpace>(toPhysical);

        // Assert
        _ = backToScreen.Point.X.Should().BeApproximately(screenLogical.X, 1.0);
        _ = backToScreen.Point.Y.Should().BeApproximately(screenLogical.Y, 1.0);
    });
}
