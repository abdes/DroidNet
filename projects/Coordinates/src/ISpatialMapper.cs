// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Coordinates;

/// <summary>
///     Factory delegate for creating <see cref="ISpatialMapper"/> instances via dependency injection.
/// </summary>
/// <param name="window">Optional window for HWND resolution and WindowSpace/ScreenSpace conversions (null if not needed).</param>
/// <param name="element">Optional element defining ElementSpace origin (null if not needed).</param>
/// <returns>A configured mapper instance.</returns>
public delegate ISpatialMapper SpatialMapperFactory(Window? window, FrameworkElement? element);

/// <summary>
///     Maps spatial points between coordinate spaces with per-monitor DPI awareness.
///     <para>
///     <b>Coordinate spaces:</b><br/>
///     • ElementSpace, WindowSpace, ScreenSpace: logical pixels (DIPs, 1 DIP = 1/96″)<br/>
///     • PhysicalScreenSpace: physical device pixels for Win32 interop<br/>
///     All conversions are per-monitor DPI aware and safe across mixed-DPI setups.
///     </para>
/// </summary>
public interface ISpatialMapper
{
    /// <summary>
    ///     Gets window metrics: client/window origins and sizes in logical DIPs, and effective DPI.
    /// </summary>
    public WindowInfo WindowInfo { get; }

    /// <summary>
    ///     Gets monitor metrics: handle, physical/logical dimensions, and effective DPI.
    /// </summary>
    public WindowMonitorInfo WindowMonitorInfo { get; }

    /// <summary>
    ///     Converts a point between arbitrary coordinate spaces.
    /// </summary>
    /// <typeparam name="TSource">Source space marker type.</typeparam>
    /// <typeparam name="TTarget">Target space marker type.</typeparam>
    /// <param name="point">The point to convert.</param>
    /// <returns>A new point in the target space.</returns>
    public SpatialPoint<TTarget> Convert<TSource, TTarget>(SpatialPoint<TSource> point);

    /// <summary>
    ///     Converts a point to logical screen coordinates (desktop-global DIPs).
    /// </summary>
    /// <typeparam name="TSource">Source space marker type.</typeparam>
    /// <param name="point">The point to convert.</param>
    /// <returns>A point in ScreenSpace.</returns>
    public SpatialPoint<ScreenSpace> ToScreen<TSource>(SpatialPoint<TSource> point);

    /// <summary>
    ///     Converts a point to logical window-client coordinates (relative to window content root).
    /// </summary>
    /// <typeparam name="TSource">Source space marker type.</typeparam>
    /// <param name="point">The point to convert.</param>
    /// <returns>A point in WindowSpace.</returns>
    public SpatialPoint<WindowSpace> ToWindow<TSource>(SpatialPoint<TSource> point);

    /// <summary>
    ///     Converts a point to element-local coordinates.
    /// </summary>
    /// <typeparam name="TSource">Source space marker type.</typeparam>
    /// <param name="point">The point to convert.</param>
    /// <returns>A point in ElementSpace.</returns>
    public SpatialPoint<ElementSpace> ToElement<TSource>(SpatialPoint<TSource> point);

    /// <summary>
    ///     Converts a point to physical screen pixels for Win32 interop.
    ///     Requires a valid HWND unless converting from PhysicalScreenSpace (no-op).
    /// </summary>
    /// <typeparam name="TSource">Source space marker type.</typeparam>
    /// <param name="point">The point to convert.</param>
    /// <returns>A point in PhysicalScreenSpace.</returns>
    public SpatialPoint<PhysicalScreenSpace> ToPhysicalScreen<TSource>(SpatialPoint<TSource> point);
}

/// <summary>
///     Window metrics in logical DIPs: client and window origins/sizes, plus effective DPI.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly record struct WindowInfo(
    Point ClientOriginLogical,
    Size ClientSizeLogical,
    Point WindowOriginLogical,
    Size WindowSizeLogical,
    uint WindowDpi)
{
    /// <summary>Gets the client origin in logical DIPs formatted for display.</summary>
    public string ClientOriginDisplay => FormatPoint(this.ClientOriginLogical, "F2");

    /// <summary>Gets the client size in logical DIPs formatted for display.</summary>
    public string ClientSizeDisplay => string.Create(CultureInfo.InvariantCulture, $"{RoundToDisplay(this.ClientSizeLogical.Width)} x {RoundToDisplay(this.ClientSizeLogical.Height)} DIPs");

    /// <summary>Gets the window origin in logical DIPs formatted for display.</summary>
    public string WindowOriginDisplay => FormatPoint(this.WindowOriginLogical, "F2");

    /// <summary>Gets the window size in logical DIPs formatted for display.</summary>
    public string WindowSizeDisplay => string.Create(CultureInfo.InvariantCulture, $"{RoundToDisplay(this.WindowSizeLogical.Width)} x {RoundToDisplay(this.WindowSizeLogical.Height)} DIPs");

    /// <summary>Gets the window DPI formatted as a percentage and raw value for display.</summary>
    public string WindowDpiDisplay => string.Create(CultureInfo.InvariantCulture, $"{(int)Math.Round(this.WindowDpi / 96.0 * 100.0)}% ({this.WindowDpi} DPI)");

    /// <summary>Formats a point with the specified precision for display.</summary>
    /// <param name="point">The point to format.</param>
    /// <param name="format">The format specifier.</param>
    /// <returns>A formatted string representation of the point.</returns>
    private static string FormatPoint(Point point, string format)
    {
        var culture = CultureInfo.InvariantCulture;
        return string.Create(culture, $"({point.X.ToString(format, culture)}, {point.Y.ToString(format, culture)})");
    }

    /// <summary>Rounds a value to two decimal places for display.</summary>
    /// <param name="value">The value to round.</param>
    /// <returns>A formatted string representation of the rounded value.</returns>
    private static string RoundToDisplay(double value) => value.ToString("F2", CultureInfo.InvariantCulture);
}

/// <summary>
///     Monitor metrics: handle, physical/logical dimensions, and effective DPI.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly record struct WindowMonitorInfo(
    IntPtr MonitorHandle,
    int PhysicalWidth,
    int PhysicalHeight,
    double LogicalWidth,
    double LogicalHeight,
    uint MonitorDpi)
{
    /// <summary>Gets a value indicating whether the monitor handle is valid.</summary>
    public bool IsValid => this.MonitorHandle != IntPtr.Zero;

    /// <summary>Gets the monitor handle formatted in hexadecimal for display.</summary>
    public string HandleDisplay => string.Create(CultureInfo.InvariantCulture, $"0x{this.MonitorHandle.ToInt64():X}");

    /// <summary>Gets the monitor DPI value formatted for display.</summary>
    public string DpiDisplay => this.MonitorDpi.ToString(CultureInfo.InvariantCulture);

    /// <summary>Gets the physical monitor dimensions formatted for display.</summary>
    public string PhysicalDisplay => string.Create(CultureInfo.InvariantCulture, $"{this.PhysicalWidth}px x {this.PhysicalHeight}px");

    /// <summary>Gets the logical monitor dimensions in DIPs formatted for display.</summary>
    public string LogicalDisplay => string.Create(CultureInfo.InvariantCulture, $"{RoundToDisplay(this.LogicalWidth)} x {RoundToDisplay(this.LogicalHeight)} DIPs");

    /// <summary>Gets the monitor size in both physical pixels and logical DIPs formatted for display.</summary>
    public string SizeDisplay => string.Create(CultureInfo.InvariantCulture, $"{this.PhysicalWidth}px x {this.PhysicalHeight}px — {RoundToDisplay(this.LogicalWidth)} x {RoundToDisplay(this.LogicalHeight)} DIPs");

    /// <summary>Rounds a value to two decimal places for display.</summary>
    /// <param name="value">The value to round.</param>
    /// <returns>A formatted string representation of the rounded value.</returns>
    private static string RoundToDisplay(double value) => value.ToString("F2", CultureInfo.InvariantCulture);
}
