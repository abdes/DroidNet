// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("SpatialMapperIdentityTests")]
[TestCategory("UITest")]
public class SpatialMapperIdentityTests : SpatialMapperTestBase
{
    [TestMethod]
    public Task Mapper_Convert_ElementToElement_ReturnsIdentical_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new Point(42.5, 99.7);
        var spatial = new SpatialPoint<ElementSpace>(point);

        // Act
        var result = this.Mapper.Convert<ElementSpace, ElementSpace>(spatial);

        // Assert
        _ = result.Point.Should().Be(point);
    });

    [TestMethod]
    public Task Mapper_Convert_WindowToWindow_ReturnsIdentical_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new Point(123.4, 567.8);
        var spatial = new SpatialPoint<WindowSpace>(point);

        // Act
        var result = this.Mapper.Convert<WindowSpace, WindowSpace>(spatial);

        // Assert
        _ = result.Point.Should().Be(point);
    });

    [TestMethod]
    public Task Mapper_Convert_ScreenToScreen_ReturnsIdentical_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new Point(1920.0, 1080.0);
        var spatial = new SpatialPoint<ScreenSpace>(point);

        // Act
        var result = this.Mapper.Convert<ScreenSpace, ScreenSpace>(spatial);

        // Assert
        _ = result.Point.Should().Be(point);
    });

    [TestMethod]
    public Task Mapper_Convert_PhysicalToPhysical_ReturnsIdentical_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var point = new Point(2560.0, 1440.0);
        var spatial = new SpatialPoint<PhysicalScreenSpace>(point);

        // Act
        var result = this.Mapper.Convert<PhysicalScreenSpace, PhysicalScreenSpace>(spatial);

        // Assert - Physical→Physical is a true no-op
        _ = result.Point.Should().Be(point);
    });

    [TestMethod]
    public Task Mapper_Convert_PhysicalToPhysical_NoWindowRequired_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Create mapper WITHOUT window to verify Physical→Physical doesn't need HWND
        var button = new Button();
        await LoadTestContentAsync(button).ConfigureAwait(true);
        var mapperNoWindow = new SpatialMapper(button, window: null);
        var point = new Point(100.0, 200.0);
        var spatial = new SpatialPoint<PhysicalScreenSpace>(point);

        // Act - Should NOT throw even without window
        var result = mapperNoWindow.Convert<PhysicalScreenSpace, PhysicalScreenSpace>(spatial);

        // Assert
        _ = result.Point.Should().Be(point);
    });
}
