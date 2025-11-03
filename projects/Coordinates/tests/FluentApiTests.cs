// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("FluentApiTests")]
[TestCategory("UITest")]
public class FluentApiTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task Fluent_Chain_ElementToScreen_ToPoint_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var elementPoint = new SpatialPoint<ElementSpace>(new Point(10, 20));

        // Act
        var screenPoint = elementPoint.Flow(mapper).ToScreen().ToPoint();

        // Assert
        _ = screenPoint.Should().BeOfType<Point>();
    });

    [TestMethod]
    public Task Fluent_Chain_WindowToElement_ToPoint_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var windowPoint = new SpatialPoint<WindowSpace>(new Point(15, 25));

        // Act
        var elementPoint = windowPoint.Flow(mapper).ToElement().ToPoint();

        // Assert
        _ = elementPoint.Should().BeOfType<Point>();
    });

    [TestMethod]
    public Task Fluent_Chain_ScreenToWindow_ToPoint_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var screenPoint = new SpatialPoint<ScreenSpace>(new Point(30, 40));

        // Act
        var windowPoint = screenPoint.Flow(mapper).ToWindow().ToPoint();

        // Assert
        _ = windowPoint.Should().BeOfType<Point>();
    });

    [TestMethod]
    public Task Fluent_Chain_PointAsElement_ToScreen_ToPoint_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var rawPoint = new Point(5, 15);

        // Act
        var screenPoint = rawPoint.AsElement().Flow(mapper).ToScreen().ToPoint();

        // Assert
        _ = screenPoint.Should().BeOfType<Point>();
    });

    [TestMethod]
    public Task Fluent_Chain_Complex_RoundTrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var original = new Point(10, 20);

        // Act
        var result = original.AsElement().Flow(mapper).ToScreen().ToWindow().ToElement().ToPoint();

        // Assert
        _ = result.X.Should().BeApproximately(original.X, 1.0);
        _ = result.Y.Should().BeApproximately(original.Y, 1.0);
    });

    [TestMethod]
    public Task Fluent_Chain_WindowToScreen_ToWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var windowPoint = new SpatialPoint<WindowSpace>(new Point(15, 25));

        // Act
        var back = windowPoint.Flow(mapper).ToScreen().ToWindow();

        // Assert
        _ = back.Point.Should().BeOfType<SpatialPoint<WindowSpace>>();
    });

    [TestMethod]
    public Task Fluent_OffsetBy_SameSpace_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var elementPoint = new SpatialPoint<ElementSpace>(new Point(10, 20));
        var offset = new SpatialPoint<ElementSpace>(new Point(5, 5));

        // Act
        var result = elementPoint.Flow(mapper).OffsetBy(offset).ToScreen().ToPoint();

        // Assert
        _ = result.Should().BeOfType<Point>();
    });

    [TestMethod]
    public Task Fluent_OffsetX_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var elementPoint = new SpatialPoint<ElementSpace>(new Point(10, 20));

        // Act
        var result = elementPoint.Flow(mapper).OffsetX(5).ToScreen().ToPoint();

        // Assert
        _ = result.Should().BeOfType<Point>();
    });

    [TestMethod]
    public Task Fluent_OffsetY_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var elementPoint = new SpatialPoint<ElementSpace>(new Point(10, 20));

        // Act
        var result = elementPoint.Flow(mapper).OffsetY(5).ToScreen().ToPoint();

        // Assert
        _ = result.Should().BeOfType<Point>();
    });

    [TestMethod]
    public Task Fluent_Offset_XY_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var mapper = await SetupMapper().ConfigureAwait(true);
        var elementPoint = new SpatialPoint<ElementSpace>(new Point(10, 20));

        // Act
        var result = elementPoint.Flow(mapper).Offset(5, 10).ToScreen().ToPoint();

        // Assert
        _ = result.Should().BeOfType<Point>();
    });

    internal static async Task<SpatialMapper> SetupMapper()
    {
        var window = VisualUserInterfaceTestsApp.MainWindow;
        var button = new Button
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
        };

        window.Content = button;

        await LoadTestContentAsync(button).ConfigureAwait(true);

        return new SpatialMapper(button, window);
    }
}
