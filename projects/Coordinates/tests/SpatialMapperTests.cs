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
[TestCategory("SpatialMapperTests")]
[TestCategory("UITest")]
public class SpatialMapperTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task Mapper_Convert_ElementToScreen_MapsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var button = setup.Button;
        var hwnd = Native.GetHwndForElement(button);
        _ = Native.GetWindowRect(hwnd, out var windowRect);
        var dpi = Native.GetDpiForWindow(hwnd);
        var logicalWindowLeft = Native.PhysicalToLogical(windowRect.Left, dpi);
        var logicalWindowTop = Native.PhysicalToLogical(windowRect.Top, dpi);
        var point = new Point(10, 20);
        var expected = new Point(logicalWindowLeft + point.X, logicalWindowTop + point.Y);

        // Act
        var result = mapper.Convert<ElementSpace, ScreenSpace>(new SpatialPoint<ElementSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_WindowToScreen_MapsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var button = setup.Button;
        var hwnd = Native.GetHwndForElement(button);
        _ = Native.GetWindowRect(hwnd, out var windowRect);
        var dpi = Native.GetDpiForWindow(hwnd);
        var logicalWindowLeft = Native.PhysicalToLogical(windowRect.Left, dpi);
        var logicalWindowTop = Native.PhysicalToLogical(windowRect.Top, dpi);
        var point = new Point(15, 25);
        var expected = new Point(logicalWindowLeft + point.X, logicalWindowTop + point.Y);

        // Act
        var result = mapper.Convert<WindowSpace, ScreenSpace>(new SpatialPoint<WindowSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_ScreenToElement_MapsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var button = setup.Button;
        var hwnd = Native.GetHwndForElement(button);
        _ = Native.GetWindowRect(hwnd, out var windowRect);
        var dpi = Native.GetDpiForWindow(hwnd);
        var logicalWindowLeft = Native.PhysicalToLogical(windowRect.Left, dpi);
        var logicalWindowTop = Native.PhysicalToLogical(windowRect.Top, dpi);
        var screenPoint = new Point(30, 40);
        var expected = new Point(screenPoint.X - logicalWindowLeft, screenPoint.Y - logicalWindowTop);

        // Act
        var result = mapper.Convert<ScreenSpace, ElementSpace>(new SpatialPoint<ScreenSpace>(screenPoint));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_ScreenToWindow_MapsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var button = setup.Button;
        var hwnd = Native.GetHwndForElement(button);
        _ = Native.GetWindowRect(hwnd, out var windowRect);
        var dpi = Native.GetDpiForWindow(hwnd);
        var logicalWindowLeft = Native.PhysicalToLogical(windowRect.Left, dpi);
        var logicalWindowTop = Native.PhysicalToLogical(windowRect.Top, dpi);
        var screenPoint = new Point(35, 45);
        var expected = new Point(screenPoint.X - logicalWindowLeft, screenPoint.Y - logicalWindowTop);

        // Act
        var result = mapper.Convert<ScreenSpace, WindowSpace>(new SpatialPoint<ScreenSpace>(screenPoint));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_ElementToWindow_MapsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var point = new Point(5, 15);

        // Act
        var result = mapper.Convert<ElementSpace, WindowSpace>(new SpatialPoint<ElementSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(point.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_WindowToElement_MapsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var point = new Point(20, 30);

        // Act
        var result = mapper.Convert<WindowSpace, ElementSpace>(new SpatialPoint<WindowSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(point.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_InvalidSourceSpace_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;

        // Create a point with invalid source space
        var point = new SpatialPoint<object>(new Point(0, 0)); // object is not a valid space

        // Act & Assert
        Action act = () => mapper.Convert<object, ScreenSpace>(point);
        _ = act.Should().Throw<NotSupportedException>().WithMessage("*Unsupported source space*");
    });

    [TestMethod]
    public Task Mapper_Convert_InvalidTargetSpace_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var point = new SpatialPoint<ElementSpace>(new Point(0, 0));

        // Act & Assert
        Action act = () => mapper.Convert<ElementSpace, object>(point);
        _ = act.Should().Throw<NotSupportedException>().WithMessage("*Unsupported target space*");
    });

    [TestMethod]
    public Task Mapper_Convert_RoundTrip_ElementToScreenToElement_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var original = new SpatialPoint<ElementSpace>(new Point(10, 20));

        // Act
        var screen = mapper.Convert<ElementSpace, ScreenSpace>(original);
        var back = mapper.Convert<ScreenSpace, ElementSpace>(screen);

        // Assert
        _ = back.Point.X.Should().BeApproximately(original.Point.X, 1.0);
        _ = back.Point.Y.Should().BeApproximately(original.Point.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_Convert_With_ZeroPoint_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var button = setup.Button;
        var hwnd = Native.GetHwndForElement(button);
        _ = Native.GetWindowRect(hwnd, out var windowRect);
        var dpi = Native.GetDpiForWindow(hwnd);
        var logicalWindowLeft = Native.PhysicalToLogical(windowRect.Left, dpi);
        var logicalWindowTop = Native.PhysicalToLogical(windowRect.Top, dpi);
        var point = new Point(0, 0);
        var expected = new Point(logicalWindowLeft + point.X, logicalWindowTop + point.Y);

        // Act
        var result = mapper.Convert<ElementSpace, ScreenSpace>(new SpatialPoint<ElementSpace>(point));

        // Assert
        _ = result.Point.X.Should().BeApproximately(expected.X, 1.0);
        _ = result.Point.Y.Should().BeApproximately(expected.Y, 1.0);
    });

    [TestMethod]
    public Task Mapper_ToScreen_Method_Works_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var point = new SpatialPoint<ElementSpace>(new Point(5, 15));

        // Act
        var result = mapper.ToScreen(point);

        // Assert
        _ = result.Should().BeOfType<SpatialPoint<ScreenSpace>>();
    });

    [TestMethod]
    public Task Mapper_ToWindow_Method_Works_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var point = new SpatialPoint<ElementSpace>(new Point(5, 15));

        // Act
        var result = mapper.ToWindow(point);

        // Assert
        _ = result.Should().BeOfType<SpatialPoint<WindowSpace>>();
    });

    [TestMethod]
    public Task Mapper_ToElement_Method_Works_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = await SetupMapper().ConfigureAwait(true);
        var mapper = setup.Mapper;
        var point = new SpatialPoint<ScreenSpace>(new Point(5, 15));

        // Act
        var result = mapper.ToElement(point);

        // Assert
        _ = result.Should().BeOfType<SpatialPoint<ElementSpace>>();
    });

    internal static async Task<MapperSetup> SetupMapper()
    {
        var window = VisualUserInterfaceTestsApp.MainWindow;
        var button = new Button
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
        };

        window.Content = button;

        await LoadTestContentAsync(button).ConfigureAwait(true);

        return new MapperSetup { Mapper = new SpatialMapper(button, window), Button = button };
    }

    internal sealed class MapperSetup
    {
        internal required SpatialMapper Mapper { get; set; }

        internal required Button Button { get; set; }
    }
}
