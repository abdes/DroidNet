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
public class SpatialMapper(FrameworkElement element, Window window) : ISpatialMapper
{
    private const uint DefaultDpi = 96;

    private readonly FrameworkElement element = element ?? throw new ArgumentNullException(nameof(element));
    private readonly Window window = window ?? throw new ArgumentNullException(nameof(window));

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
        => this.TryElementToWindow(elementPoint, out var windowPoint)
            ? this.WindowToScreen(windowPoint)
            : this.FallbackElementToScreen(elementPoint);

    private Point WindowToScreen(Point windowPoint)
    {
        if (this.TryGetWindowMetrics(out _, out var windowRect, out var dpiWindow))
        {
            // Convert window-relative logical coordinates to physical pixels for DPI-aware projection.
            var physicalX = windowRect.Left + Native.LogicalToPhysical(windowPoint.X, dpiWindow);
            var physicalY = windowRect.Top + Native.LogicalToPhysical(windowPoint.Y, dpiWindow);

            var monitorDpi = Native.GetDpiForPhysicalPoint(new Point(physicalX, physicalY));
            return Native.GetLogicalPointFromPhysical(new Native.POINT(physicalX, physicalY), monitorDpi);
        }

        return this.FallbackWindowToScreen(windowPoint);
    }

    private Point ScreenToWindow(Point screenPoint)
    {
        if (this.TryGetWindowMetrics(out _, out var windowRect, out var dpiWindow))
        {
            var physical = Native.GetPhysicalScreenPointFromLogical(screenPoint);
            var physicalX = RoundToInt(physical.X) - windowRect.Left;
            var physicalY = RoundToInt(physical.Y) - windowRect.Top;

            var logicalX = Native.PhysicalToLogical(physicalX, dpiWindow);
            var logicalY = Native.PhysicalToLogical(physicalY, dpiWindow);

            return new Point(logicalX, logicalY);
        }

        return this.FallbackScreenToWindow(screenPoint);
    }

    private Point ScreenToElement(Point screenPoint)
    {
        var windowPoint = this.ScreenToWindow(screenPoint);
        return this.TryWindowToElement(windowPoint, out var elementPoint)
            ? elementPoint
            : this.FallbackScreenToElement(screenPoint);
    }

    private bool TryElementToWindow(Point elementPoint, out Point windowPoint)
    {
        windowPoint = default;

        if (this.element.XamlRoot?.Content is UIElement root)
        {
#pragma warning disable CA1031 // Do not catch general exception types
            try
            {
                var transform = this.element.TransformToVisual(root);
                windowPoint = transform.TransformPoint(elementPoint);
                return true;
            }
            catch
            {
            }
#pragma warning restore CA1031 // Do not catch general exception types
        }

        return false;
    }

    private bool TryWindowToElement(Point windowPoint, out Point elementPoint)
    {
        elementPoint = default;

        if (this.element.XamlRoot?.Content is UIElement root)
        {
#pragma warning disable CA1031 // Do not catch general exception types
            try
            {
                var transform = this.element.TransformToVisual(root);
                var inverse = transform.Inverse;
                if (inverse != null)
                {
                    elementPoint = inverse.TransformPoint(windowPoint);
                    return true;
                }
            }
            catch
            {
            }
#pragma warning restore CA1031 // Do not catch general exception types
        }

        return false;
    }

    private Point FallbackElementToScreen(Point elementPoint)
    {
        var transform = this.element.TransformToVisual(visual: null);
        return transform.TransformPoint(elementPoint);
    }

    private Point FallbackWindowToScreen(Point windowPoint)
    {
        if (this.window.Content is UIElement content)
        {
            var transform = content.TransformToVisual(visual: null);
            return transform.TransformPoint(windowPoint);
        }

        throw new InvalidOperationException("Window content is not available for screen projection.");
    }

    private Point FallbackScreenToWindow(Point screenPoint)
    {
        if (this.window.Content is UIElement content)
        {
            var transform = content.TransformToVisual(visual: null);
            var inverse = transform.Inverse ?? throw new InvalidOperationException("Unable to invert window transform.");
            return inverse.TransformPoint(screenPoint);
        }

        throw new InvalidOperationException("Window content is not available for screen projection.");
    }

    private Point FallbackScreenToElement(Point screenPoint)
    {
        var transform = this.element.TransformToVisual(visual: null);
        var inverse = transform.Inverse ?? throw new InvalidOperationException("Unable to invert element transform.");
        return inverse.TransformPoint(screenPoint);
    }

    private bool TryGetWindowMetrics(out IntPtr hwnd, out Native.RECT windowRect, out uint dpi)
    {
        hwnd = this.EnsureWindowHandle();
        windowRect = default;
        dpi = DefaultDpi;

        if (hwnd == IntPtr.Zero)
        {
            return false;
        }

        if (!Native.GetWindowRect(hwnd, out windowRect))
        {
            return false;
        }

        var windowDpi = Native.GetDpiForWindow(hwnd);
        if (windowDpi != 0)
        {
            dpi = windowDpi;
            return true;
        }

        var xamlDpi = Native.GetDpiFromXamlRoot(this.element.XamlRoot);
        if (xamlDpi != 0)
        {
            dpi = xamlDpi;
        }

        return true;
    }

    private IntPtr EnsureWindowHandle()
    {
        if (this.cachedHwnd != IntPtr.Zero)
        {
            return this.cachedHwnd;
        }

        var handle = this.TryGetWindowHandleViaWindow();
        if (handle == IntPtr.Zero)
        {
            handle = Native.GetHwndForElement(this.element);
        }

        this.cachedHwnd = handle;
        return handle;
    }

    private IntPtr TryGetWindowHandleViaWindow()
    {
#pragma warning disable CA1031 // Do not catch general exception types
        try
        {
            return WindowNative.GetWindowHandle(this.window);
        }
        catch
        {
            return IntPtr.Zero;
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }
}
