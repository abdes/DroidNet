// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

/// <summary>
/// Base class for SpatialMapper tests that provides common setup for element, window, and mapper.
/// </summary>
[ExcludeFromCodeCoverage]
public abstract class SpatialMapperTestBase : VisualUserInterfaceTests
{
    protected static Window TestWindow => VisualUserInterfaceTestsApp.MainWindow;

    protected SpatialMapper Mapper { get; private set; } = null!;

    protected Button TestButton { get; private set; } = null!;

    [TestInitialize]
    public Task SetupMapper() => EnqueueAsync(async () =>
    {
        this.TestButton = new Button
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
        };

        TestWindow.Content = this.TestButton;
        await LoadTestContentAsync(this.TestButton).ConfigureAwait(true);
        this.Mapper = new SpatialMapper(TestWindow, this.TestButton);
    });

    protected Point GetPhysicalClientOrigin()
    {
        var hwnd = Native.GetHwndForElement(this.TestButton);
        var clientOrigin = new Native.POINT(0, 0);
        _ = Native.ClientToScreen(hwnd, ref clientOrigin);
        return new Point(clientOrigin.X, clientOrigin.Y);
    }

    protected Point GetLogicalWindowTopLeft()
    {
        var hwnd = Native.GetHwndForElement(this.TestButton);
        var clientOrigin = new Native.POINT(0, 0);
        _ = Native.ClientToScreen(hwnd, ref clientOrigin);
        var dpi = Native.GetDpiForPhysicalPoint(new Point(clientOrigin.X, clientOrigin.Y));
        var logicalX = Native.PhysicalToLogical(clientOrigin.X, dpi);
        var logicalY = Native.PhysicalToLogical(clientOrigin.Y, dpi);
        return new Point(logicalX, logicalY);
    }

    protected uint GetWindowDpi()
    {
        var hwnd = Native.GetHwndForElement(this.TestButton);
        return Native.GetDpiForWindow(hwnd);
    }
}
