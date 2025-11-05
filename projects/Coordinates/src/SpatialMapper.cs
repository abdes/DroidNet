// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Windows.Foundation;
using WinRT.Interop;

namespace DroidNet.Coordinates;

/// <summary>
///     Maps spatial points between element, window, screen, and physical coordinate spaces with
///     per-monitor DPI awareness. Conversions compose through a strict hierarchy:
///     Element ↔ Window ↔ Screen (logical) ↔ Physical.
/// </summary>
/// <param name="element">The framework element defining the ElementSpace origin.</param>
/// <param name="window">
///     Optional window for WindowSpace/ScreenSpace conversions and HWND resolution.
///     Required for logical→physical conversions; omit only when working purely with logical spaces.
/// </param>
public partial class SpatialMapper(FrameworkElement element, Window? window = null) : ISpatialMapper
{
    private readonly FrameworkElement element = element ?? throw new ArgumentNullException(nameof(element));
    private readonly Window? window = window;

    private IntPtr cachedHwnd;

    /// <summary>
    ///     Converts a spatial point between coordinate spaces using strict one-level composition.
    ///     Identity conversions (source == target) return immediately; Physical→Physical requires no HWND.
    /// </summary>
    /// <typeparam name="TSource">Source coordinate space marker type.</typeparam>
    /// <typeparam name="TTarget">Target coordinate space marker type.</typeparam>
    /// <param name="point">The point to convert.</param>
    /// <returns>A new point in the target space.</returns>
    /// <remarks>
    ///     <b>Logical→Physical conversion semantics:</b><br/>
    ///     Converting from ElementSpace, WindowSpace, or ScreenSpace to PhysicalScreenSpace requires
    ///     a valid HWND (either cached or resolvable via the associated Window). If no Window was
    ///     provided at construction or the HWND becomes invalid, <see cref="InvalidOperationException"/> is thrown.
    ///     Physical→Physical conversions are no-ops and do not require an HWND.
    /// </remarks>
    /// <exception cref="NotSupportedException">Source or target space is not one of the four supported marker types.</exception>
    /// <exception cref="InvalidOperationException">
    ///     Logical→Physical conversion attempted without a valid HWND, or element is not in a visual tree.
    /// </exception>
    public SpatialPoint<TTarget> Convert<TSource, TTarget>(SpatialPoint<TSource> point)
    {
        var sourceType = typeof(TSource);
        var targetType = typeof(TTarget);

        // Identity fast-path
        if (sourceType == targetType)
        {
            return new SpatialPoint<TTarget>(point.Point);
        }

        // One-level helper functions
        Point ElementToWindow(Point p)
        {
            var root = this.window?.Content is UIElement windowContent
                ? windowContent
                : this.element.XamlRoot?.Content is UIElement xamlRootContent
                    ? xamlRootContent
                    : throw new InvalidOperationException("Element is not associated with a visual tree.");
            var transform = this.element.TransformToVisual(root);
            return transform.TransformPoint(p);
        }

        Point WindowToElement(Point p)
        {
            var root = this.window?.Content is UIElement windowContent
                ? windowContent
                : this.element.XamlRoot?.Content is UIElement xamlRootContent
                    ? xamlRootContent
                    : throw new InvalidOperationException("Element is not associated with a visual tree.");
            var transform = this.element.TransformToVisual(root);
            var inverse = transform.Inverse ?? throw new InvalidOperationException("Unable to invert element transform.");
            return inverse.TransformPoint(p);
        }

        Point WindowToScreen(Point p)
        {
            var hwnd = this.EnsureWindowHandle();
            var dpi = Native.GetDpiForWindow(hwnd);

            var physicalPoint = Native.GetPhysicalPointFromLogical(p, dpi);
            if (!Native.ClientToScreen(hwnd, ref physicalPoint))
            {
                throw new InvalidOperationException("ClientToScreen failed for the associated window.");
            }

            var monitorDpi = Native.GetDpiForPhysicalPoint(new Point(physicalPoint.X, physicalPoint.Y));
            return Native.GetLogicalPointFromPhysical(physicalPoint, monitorDpi);
        }

        Point ScreenToWindow(Point p)
        {
            var hwnd = this.EnsureWindowHandle();
            var dpi = Native.GetDpiForWindow(hwnd);

            var physicalPoint = Native.GetPhysicalPointFromLogical(p, dpi);
            if (!Native.ScreenToClient(hwnd, ref physicalPoint))
            {
                throw new InvalidOperationException("ScreenToClient failed for the associated window.");
            }

            var logicalX = Native.PhysicalToLogical(physicalPoint.X, dpi);
            var logicalY = Native.PhysicalToLogical(physicalPoint.Y, dpi);
            return new Point(logicalX, logicalY);
        }

        Point PhysicalToScreen(Point p)
        {
            var dpi = Native.GetDpiForPhysicalPoint(p);
            return Native.GetLogicalPointFromPhysical(
                new Native.POINT(RoundToInt(p.X), RoundToInt(p.Y)),
                dpi);
        }

        Point PhysicalToWindow(Point p)
        {
            var hwnd = this.EnsureWindowHandle();
            var dpi = Native.GetDpiForWindow(hwnd);

            var phys = new Native.POINT(RoundToInt(p.X), RoundToInt(p.Y));
            if (!Native.ScreenToClient(hwnd, ref phys))
            {
                throw new InvalidOperationException("ScreenToClient failed for the associated window.");
            }

            var logicalX = Native.PhysicalToLogical(phys.X, dpi);
            var logicalY = Native.PhysicalToLogical(phys.Y, dpi);
            return new Point(logicalX, logicalY);
        }

        Point ScreenToPhysical(Point p)
        {
            // Logical→Physical requires valid HWND
            _ = this.EnsureWindowHandle();
            var physical = Native.GetPhysicalScreenPointFromLogical(p);
            return new Point(physical.X, physical.Y);
        }

        // Source-based dispatch
        Point FromElement(Point p) => targetType.Name switch
        {
            nameof(WindowSpace) => ElementToWindow(p),
            nameof(ScreenSpace) => WindowToScreen(ElementToWindow(p)),
            nameof(PhysicalScreenSpace) => ScreenToPhysical(WindowToScreen(ElementToWindow(p))),
            _ => throw new NotSupportedException($"Unsupported target space {targetType.Name}."),
        };

        Point FromWindow(Point p) => targetType.Name switch
        {
            nameof(ElementSpace) => WindowToElement(p),
            nameof(ScreenSpace) => WindowToScreen(p),
            nameof(PhysicalScreenSpace) => ScreenToPhysical(WindowToScreen(p)),
            _ => throw new NotSupportedException($"Unsupported target space {targetType.Name}."),
        };

        Point FromScreen(Point p) => targetType.Name switch
        {
            nameof(WindowSpace) => ScreenToWindow(p),
            nameof(ElementSpace) => WindowToElement(ScreenToWindow(p)),
            nameof(PhysicalScreenSpace) => ScreenToPhysical(p),
            _ => throw new NotSupportedException($"Unsupported target space {targetType.Name}."),
        };

        Point FromPhysical(Point p) => targetType.Name switch
        {
            nameof(ScreenSpace) => PhysicalToScreen(p),
            nameof(WindowSpace) => PhysicalToWindow(p),
            nameof(ElementSpace) => WindowToElement(PhysicalToWindow(p)),
            nameof(PhysicalScreenSpace) => p, // Physical→Physical no-op
            _ => throw new NotSupportedException($"Unsupported target space {targetType.Name}."),
        };

        var result = sourceType.Name switch
        {
            nameof(ElementSpace) => FromElement(point.Point),
            nameof(WindowSpace) => FromWindow(point.Point),
            nameof(ScreenSpace) => FromScreen(point.Point),
            nameof(PhysicalScreenSpace) => FromPhysical(point.Point),
            _ => throw new NotSupportedException($"Unsupported source space {sourceType.Name}."),
        };

        return new SpatialPoint<TTarget>(result);
    }

    /// <inheritdoc />
    public SpatialPoint<ScreenSpace> ToScreen<TSource>(SpatialPoint<TSource> point)
        => this.Convert<TSource, ScreenSpace>(point);

    /// <inheritdoc />
    public SpatialPoint<WindowSpace> ToWindow<TSource>(SpatialPoint<TSource> point)
        => this.Convert<TSource, WindowSpace>(point);

    /// <inheritdoc />
    public SpatialPoint<ElementSpace> ToElement<TSource>(SpatialPoint<TSource> point)
        => this.Convert<TSource, ElementSpace>(point);

    /// <inheritdoc />
    public SpatialPoint<PhysicalScreenSpace> ToPhysicalScreen<TSource>(SpatialPoint<TSource> point)
        => this.Convert<TSource, PhysicalScreenSpace>(point);

    // Static helpers must appear before private instance methods per ordering rules
    private static int RoundToInt(double value)
        => (int)Math.Round(value, MidpointRounding.AwayFromZero);

    private IntPtr EnsureWindowHandle()
    {
        if (this.cachedHwnd != IntPtr.Zero)
        {
            // Validate cached HWND. If the window was closed or the handle is no longer valid,
            // consider the window gone and fail rather than attempting to re-resolve.
            return !Native.IsWindow(this.cachedHwnd)
                ? throw new InvalidOperationException("The previously resolved HWND is no longer valid; the associated window appears to have been closed.")
                : this.cachedHwnd;
        }

        var handle = this.window != null ? GetWindowHandleViaWindow(this.window) : IntPtr.Zero;
        if (handle == IntPtr.Zero)
        {
            throw new InvalidOperationException("Unable to resolve the HWND for the associated element.");
        }

        this.cachedHwnd = handle;
        return handle;

        static IntPtr GetWindowHandleViaWindow(Window owningWindow)
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
    }
}
