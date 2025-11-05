// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("SpatialMapperErrorTests")]
[TestCategory("UITest")]
public class SpatialMapperErrorTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task Mapper_LogicalToPhysical_WithoutWindow_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Create mapper WITHOUT associated window
        var button = new Button();
        await LoadTestContentAsync(button).ConfigureAwait(true);
        var mapperNoWindow = new SpatialMapper(button, window: null);
        var screenPoint = new SpatialPoint<ScreenSpace>(new Point(100, 200));

        // Act & Assert - Logical→Physical requires valid Window/HWND
        Action act = () => mapperNoWindow.Convert<ScreenSpace, PhysicalScreenSpace>(screenPoint);
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("*Unable to resolve the HWND*");
    });

    [TestMethod]
    public Task Mapper_ElementToPhysical_WithoutWindow_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var button = new Button();
        await LoadTestContentAsync(button).ConfigureAwait(true);
        var mapperNoWindow = new SpatialMapper(button, window: null);
        var elementPoint = new SpatialPoint<ElementSpace>(new Point(10, 20));

        // Act & Assert
        Action act = () => mapperNoWindow.Convert<ElementSpace, PhysicalScreenSpace>(elementPoint);
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("*Unable to resolve the HWND*");
    });

    [TestMethod]
    public Task Mapper_WindowToPhysical_WithoutWindow_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var button = new Button();
        await LoadTestContentAsync(button).ConfigureAwait(true);
        var mapperNoWindow = new SpatialMapper(button, window: null);
        var windowPoint = new SpatialPoint<WindowSpace>(new Point(50, 75));

        // Act & Assert
        Action act = () => mapperNoWindow.Convert<WindowSpace, PhysicalScreenSpace>(windowPoint);
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("*Unable to resolve the HWND*");
    });

    [TestMethod]
    public void Mapper_Constructor_NullElement_Throws()
    {
        // Act & Assert
        Action act = () => _ = new SpatialMapper(null!, window: null);
        _ = act.Should().Throw<ArgumentNullException>()
            .WithParameterName("element");
    }

    [TestMethod]
    public Task Mapper_ElementNotInVisualTree_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Create element that's not in ANY visual tree and no window provided
        var orphanElement = new Button();

        // Load a completely different element in the test window
        await LoadTestContentAsync(new TextBlock()).ConfigureAwait(true);

        // Create mapper WITHOUT window - so it must rely on element's XamlRoot
        // Since orphanElement has no XamlRoot, this should fail
        var mapper = new SpatialMapper(orphanElement, window: null);
        var point = new SpatialPoint<ElementSpace>(new Point(10, 20));

        // Act & Assert - Element not in visual tree should throw when trying to get root
        Action act = () => mapper.Convert<ElementSpace, WindowSpace>(point);
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("*Element is not associated with a visual tree*");
    });

    [TestMethod]
    public Task Mapper_ToPhysicalScreen_WithoutWindow_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var button = new Button();
        await LoadTestContentAsync(button).ConfigureAwait(true);
        var mapperNoWindow = new SpatialMapper(button, window: null);
        var elementPoint = new SpatialPoint<ElementSpace>(new Point(10, 20));

        // Act & Assert - ToPhysicalScreen helper should also enforce HWND requirement
        Action act = () => mapperNoWindow.ToPhysicalScreen(elementPoint);
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("*Unable to resolve the HWND*");
    });

    [TestMethod]
    public Task Mapper_Convert_UnsupportedSourceSpace_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = VisualUserInterfaceTestsApp.MainWindow;
        var button = new Button();
        window.Content = button;
        await LoadTestContentAsync(button).ConfigureAwait(true);

        var mapper = new SpatialMapper(button, window);
        var point = new SpatialPoint<string>(new Point(0, 0)); // Invalid space type

        // Act & Assert
        Action act = () => mapper.Convert<string, ScreenSpace>(point);
        _ = act.Should().Throw<NotSupportedException>()
            .WithMessage("*Unsupported source space*");
    });

    [TestMethod]
    public Task Mapper_Convert_UnsupportedTargetSpace_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = VisualUserInterfaceTestsApp.MainWindow;
        var button = new Button();
        window.Content = button;
        await LoadTestContentAsync(button).ConfigureAwait(true);

        var mapper = new SpatialMapper(button, window);
        var point = new SpatialPoint<ElementSpace>(new Point(0, 0));

        // Act & Assert
        Action act = () => mapper.Convert<ElementSpace, string>(point);
        _ = act.Should().Throw<NotSupportedException>()
            .WithMessage("*Unsupported target space*");
    });
}
