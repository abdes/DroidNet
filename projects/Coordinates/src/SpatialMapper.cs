// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Windows.Foundation;
using WinRT.Interop;

namespace DroidNet.Coordinates;

/// <summary>
///     Factory delegate for creating ISpatialMapper instances with dependency injection.
/// </summary>
/// <param name="element">The framework element to map coordinates for.</param>
/// <param name="window">The window containing the element.</param>
/// <returns>An instance of ISpatialMapper.</returns>
public delegate ISpatialMapper SpatialMapperFactory(FrameworkElement element, Window window);

/// <summary>
///     Provides spatial coordinate mapping between different UI spaces such as element, window, and screen coordinates.
///     Handles conversions between element-relative, window-relative, and screen-absolute coordinate systems.
/// </summary>
/// <param name="element">The framework element used as reference for element space mappings.</param>
/// <param name="window">The window containing the element, used for window space mappings.</param>
public class SpatialMapper(FrameworkElement element, Window? window = null) : ISpatialMapper
{
    private readonly FrameworkElement element = element ?? throw new ArgumentNullException(nameof(element));
    private readonly Window? window = window;

    private IntPtr cachedHwnd;

    /// <summary>
    ///     Converts a spatial point from the source coordinate space to the target coordinate space.
    /// </summary>
    /// <typeparam name="TSource">The type representing the source coordinate space.</typeparam>
    /// <typeparam name="TTarget">The type representing the target coordinate space.</typeparam>
    /// <param name="point">The spatial point to convert.</param>
    /// <returns>A new spatial point in the target coordinate space.</returns>
    /// <exception cref="NotSupportedException">Thrown when the target space is not supported.</exception>
    public SpatialPoint<TTarget> Convert<TSource, TTarget>(SpatialPoint<TSource> point)
    {
        var screenPoint = this.ToScreenPoint(point);
        var targetType = typeof(TTarget);

        if (targetType == typeof(ScreenSpace))
        {
            return new SpatialPoint<TTarget>(screenPoint);
        }

        if (targetType == typeof(WindowSpace))
        {
            var windowPoint = this.ScreenToWindow(screenPoint);
            return new SpatialPoint<TTarget>(windowPoint);
        }

        if (targetType == typeof(ElementSpace))
        {
            var elementPoint = this.ScreenToElement(screenPoint);
            return new SpatialPoint<TTarget>(elementPoint);
        }

        throw new NotSupportedException($"Unsupported target space {targetType.Name}.");
    }

    /// <summary>
    ///     Converts a spatial point to screen coordinate space.
    /// </summary>
    /// <typeparam name="TSource">The type representing the source coordinate space.</typeparam>
    /// <param name="point">The spatial point to convert.</param>
    /// <returns>A new spatial point in screen coordinate space.</returns>
    public SpatialPoint<ScreenSpace> ToScreen<TSource>(SpatialPoint<TSource> point)
        => this.Convert<TSource, ScreenSpace>(point);

    /// <summary>
    ///     Converts a spatial point to window coordinate space.
    /// </summary>
    /// <typeparam name="TSource">The type representing the source coordinate space.</typeparam>
    /// <param name="point">The spatial point to convert.</param>
    /// <returns>A new spatial point in window coordinate space.</returns>
    public SpatialPoint<WindowSpace> ToWindow<TSource>(SpatialPoint<TSource> point)
        => this.Convert<TSource, WindowSpace>(point);

    /// <summary>
    ///     Converts a spatial point to element coordinate space.
    /// </summary>
    /// <typeparam name="TSource">The type representing the source coordinate space.</typeparam>
    /// <param name="point">The spatial point to convert.</param>
    /// <returns>A new spatial point in element coordinate space.</returns>
    public SpatialPoint<ElementSpace> ToElement<TSource>(SpatialPoint<TSource> point)
        => this.Convert<TSource, ElementSpace>(point);

    private static int RoundToInt(double value)
        => (int)Math.Round(value, MidpointRounding.AwayFromZero);

    private static uint GetWindowDpi(IntPtr hwnd)
    {
        var dpi = Native.GetDpiForWindow(hwnd);
        if (dpi == 0)
        {
            throw new InvalidOperationException("GetDpiForWindow returned 0 for the associated window.");
        }

        return dpi;
    }

    private static IntPtr GetWindowHandleViaWindow(Window owningWindow)
    {
        try
        {
            return WindowNative.GetWindowHandle(owningWindow);
        }
        catch (InvalidCastException ex)
        {
            throw new InvalidOperationException("WindowNative.GetWindowHandle failed for the provided Window instance.", ex);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code clarity")]
    private Point ToScreenPoint<TSource>(SpatialPoint<TSource> point)
    {
        var sourceType = typeof(TSource);

        if (sourceType == typeof(ElementSpace))
        {
            return this.ElementToScreen(point.Point);
        }

        if (sourceType == typeof(WindowSpace))
        {
            return this.WindowToScreen(point.Point);
        }

        if (sourceType == typeof(ScreenSpace))
        {
            return point.Point;
        }

        throw new NotSupportedException($"Unsupported source space {sourceType.Name}.");
    }

    private Point ElementToScreen(Point elementPoint)
    {
        var windowPoint = this.ElementToWindow(elementPoint);
        return this.WindowToScreen(windowPoint);
    }

    private Point WindowToScreen(Point windowPoint)
    {
        var hwnd = this.EnsureWindowHandle();
        var dpi = GetWindowDpi(hwnd);

        var physicalPoint = Native.GetPhysicalPointFromLogical(windowPoint, dpi);
        if (!Native.ClientToScreen(hwnd, ref physicalPoint))
        {
            throw new InvalidOperationException("ClientToScreen failed for the associated window.");
        }

        var monitorDpi = Native.GetDpiForPhysicalPoint(new Point(physicalPoint.X, physicalPoint.Y));
        return Native.GetLogicalPointFromPhysical(physicalPoint, monitorDpi);
    }

    private Point ScreenToWindow(Point screenPoint)
    {
        var hwnd = this.EnsureWindowHandle();
        var dpi = GetWindowDpi(hwnd);

        var physicalScreenPoint = Native.GetPhysicalScreenPointFromLogical(screenPoint);
        var physicalPoint = new Native.POINT(RoundToInt(physicalScreenPoint.X), RoundToInt(physicalScreenPoint.Y));

        if (!Native.ScreenToClient(hwnd, ref physicalPoint))
        {
            throw new InvalidOperationException("ScreenToClient failed for the associated window.");
        }

        var logicalX = Native.PhysicalToLogical(physicalPoint.X, dpi);
        var logicalY = Native.PhysicalToLogical(physicalPoint.Y, dpi);
        return new Point(logicalX, logicalY);
    }

    private Point ScreenToElement(Point screenPoint)
    {
        var windowPoint = this.ScreenToWindow(screenPoint);
        return this.WindowToElement(windowPoint);
    }

    private Point ElementToWindow(Point elementPoint)
    {
        var root = this.GetRootVisual();
        var transform = this.element.TransformToVisual(root);
        return transform.TransformPoint(elementPoint);
    }

    private Point WindowToElement(Point windowPoint)
    {
        var root = this.GetRootVisual();
        var transform = this.element.TransformToVisual(root);
        var inverse = transform.Inverse ?? throw new InvalidOperationException("Unable to invert element transform.");
        return inverse.TransformPoint(windowPoint);
    }

    private UIElement GetRootVisual()
    {
        if (this.window?.Content is UIElement windowContent)
        {
            return windowContent;
        }

        if (this.element.XamlRoot?.Content is UIElement xamlRootContent)
        {
            return xamlRootContent;
        }

        throw new InvalidOperationException("Element is not associated with a visual tree.");
    }

    private IntPtr EnsureWindowHandle()
    {
        if (this.cachedHwnd != IntPtr.Zero)
        {
            return this.cachedHwnd;
        }

        IntPtr handle;

        if (this.window != null)
        {
            handle = GetWindowHandleViaWindow(this.window);
        }
        else
        {
            handle = Native.GetHwndForElement(this.element);
        }

        if (handle == IntPtr.Zero)
        {
            throw new InvalidOperationException("Unable to resolve the HWND for the associated element.");
        }

        this.cachedHwnd = handle;
        return handle;
    }
}
