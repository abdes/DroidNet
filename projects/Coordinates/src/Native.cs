// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace DroidNet.Coordinates;

// Stay consistent with Windows APIs, keep things grouped together
#pragma warning disable RCS1135 // Declare enum member with zero value (when enum has FlagsAttribute)
#pragma warning disable SA1201 // Elements should appear in the correct order
#pragma warning disable SA1310 // Field names should not contain underscore
#pragma warning disable SA1307 // Accessible fields should begin with upper-case letter
#pragma warning disable CA1008 // Enums should have zero value
#pragma warning disable CA1028 // Enum Storage should be Int32
#pragma warning disable CA1711 // Identifiers should not have incorrect suffix
#pragma warning disable CA1034 // Nested types should not be visible
#pragma warning disable CA1815 // Override equals and operator equals on value types
#pragma warning disable CA1707 // Identifiers should not contain underscores

/// <summary>
///     P/Invoke wrappers and native helpers for spatial coordinate transformations.
/// </summary>
internal static partial class Native
{
    private const string User32 = "user32.dll";
    private const string SHCore = "shcore.dll";
    private const uint StandardDpi = 96;

    /// <summary>Monitor DPI type.</summary>
    public enum MONITOR_DPI_TYPE
    {
        /// <summary>
        ///     Effective DPI that incorporates accessibility overrides and matches what Desktop
        ///     Window Manager (DWM) uses to scale desktop applications.
        /// </summary>
        MDT_EFFECTIVE_DPI = 0,

        /// <summary>
        ///     DPI that ensures rendering at a compliant angular resolution on the screen.
        /// </summary>
        MDT_ANGULAR_DPI = 1,

        /// <summary>
        ///     Linear DPI of the screen as measured on the screen itself.
        /// </summary>
        MDT_RAW_DPI = 2,

        /// <summary>
        ///     Default DPI setting (same as MDT_EFFECTIVE_DPI).
        /// </summary>
        MDT_DEFAULT = MDT_EFFECTIVE_DPI,
    }

    /// <summary>POINT structure for screen coordinates.</summary>
    /// <remarks>
    /// Initializes a new instance of the <see cref="POINT"/> struct.
    /// </remarks>
    /// <param name="x">The x-coordinate.</param>
    /// <param name="y">The y-coordinate.</param>
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT(int x, int y)
    {
        /// <summary>
        /// The x-coordinate of the point.
        /// </summary>
        public int X = x;

        /// <summary>
        /// The y-coordinate of the point.
        /// </summary>
        public int Y = y;
    }

    /// <summary>RECT structure.</summary>
    /// <remarks>
    /// Initializes a new instance of the <see cref="RECT"/> struct.
    /// </remarks>
    /// <param name="left">The left coordinate.</param>
    /// <param name="top">The top coordinate.</param>
    /// <param name="right">The right coordinate.</param>
    /// <param name="bottom">The bottom coordinate.</param>
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT(int left, int top, int right, int bottom)
    {
        /// <summary>
        /// The x-coordinate of the upper-left corner of the rectangle.
        /// </summary>
        public int Left = left;

        /// <summary>
        /// The y-coordinate of the upper-left corner of the rectangle.
        /// </summary>
        public int Top = top;

        /// <summary>
        /// The x-coordinate of the lower-right corner of the rectangle.
        /// </summary>
        public int Right = right;

        /// <summary>
        /// The y-coordinate of the lower-right corner of the rectangle.
        /// </summary>
        public int Bottom = bottom;

        /// <summary>
        /// Gets the width of the rectangle.
        /// </summary>
        public readonly int Width => this.Right - this.Left;

        /// <summary>
        /// Gets the height of the rectangle.
        /// </summary>
        public readonly int Height => this.Bottom - this.Top;
    }

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool GetClientRect(IntPtr hWnd, out RECT lpRect);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool ScreenToClient(IntPtr hWnd, ref POINT lpPoint);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetWindowPos(
        IntPtr hWnd,
        IntPtr hWndInsertAfter,
        int x,
        int y,
        int cx,
        int cy,
        uint uFlags);

    /// <summary>
    ///     Retrieves the DPI (dots per inch) for the specified monitor.
    /// </summary>
    /// <param name="hmonitor">Handle to the monitor.</param>
    /// <param name="dpiType">Type of DPI to retrieve.</param>
    /// <param name="dpiX">Receives the DPI along the X axis.</param>
    /// <param name="dpiY">Receives the DPI along the Y axis.</param>
    /// <returns>A Win32 HRESULT-like value (0 for success).</returns>
    /// <remarks>
    ///     Important: the value returned by <c>GetDpiForMonitor</c> depends on the DPI
    ///     awareness of the calling process. DPI awareness is an application-level property
    ///     (see <c>PROCESS_DPI_AWARENESS</c>), and the same physical monitor can yield
    ///     different DPI values depending on that setting. The typical behavior is:
    ///
    ///     - <c>PROCESS_DPI_UNAWARE</c>: returns 96 (the system default) because the process
    ///       is unaware of any scaling.
    ///     - <c>PROCESS_SYSTEM_DPI_AWARE</c>: returns the system DPI (the DPI used by the
    ///       primary display at system startup), because the process assumes a single system DPI.
    ///     - <c>PROCESS_PER_MONITOR_DPI_AWARE</c>: returns the actual DPI value configured for
    ///       that display (per-monitor DPI).
    ///
    ///     For per-monitor (V2) DPI-aware callers that need a DPI value specific to a particular
    ///     window, consider using <c>GetDpiForWindow</c> instead. If you cannot control the
    ///     calling process DPI awareness, be aware that results may vary and consider passing
    ///     explicit context (monitor handle or DPI) to your conversion helpers.
    /// </remarks>
    [LibraryImport(SHCore, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial int GetDpiForMonitor(
        IntPtr hmonitor,
        MONITOR_DPI_TYPE dpiType,
        out uint dpiX,
        out uint dpiY);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial uint GetDpiForWindow(IntPtr hwnd);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial IntPtr MonitorFromPoint(POINT pt, uint dwFlags);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool IsWindow(IntPtr hWnd);

    /// <summary>MONITORINFO structure used by GetMonitorInfo.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct MONITORINFO
    {
        /// <summary>The size of the structure.</summary>
        public uint cbSize;

        /// <summary>The display monitor rectangle, in virtual-screen coordinates.</summary>
        public RECT rcMonitor;

        /// <summary>The work area rectangle of the monitor, in virtual-screen coordinates.</summary>
        public RECT rcWork;

        /// <summary>The attributes of the display monitor.</summary>
        public uint dwFlags;
    }

    [LibraryImport(User32, SetLastError = true, EntryPoint = "GetMonitorInfoW")]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFO lpmi);

    /// <summary>Converts logical (DIP) coordinates to physical pixels using the specified DPI.</summary>
    /// <param name="logicalValue">Logical coordinate value in DIPs.</param>
    /// <param name="dpi">DPI value (e.g., 96 for 100% scaling, 144 for 150%).</param>
    /// <returns>Physical pixel value.</returns>
    public static int LogicalToPhysical(double logicalValue, uint dpi)
        => (int)Math.Round(logicalValue * dpi / StandardDpi);

    /// <summary>Converts physical pixels to logical (DIP) coordinates using the specified DPI.</summary>
    /// <param name="physicalValue">Physical pixel value.</param>
    /// <param name="dpi">DPI value (e.g., 96 for 100% scaling, 144 for 150%).</param>
    /// <returns>Logical coordinate value in DIPs.</returns>
    public static double PhysicalToLogical(int physicalValue, uint dpi)
        => physicalValue * StandardDpi / (double)dpi;

    /// <summary>
    ///     Gets the DPI for the monitor that contains the specified physical screen point.
    /// </summary>
    /// <param name="physicalScreenPoint">Screen point in physical pixels (e.g., from GetCursorPos).</param>
    /// <returns>The effective DPI for the monitor containing <paramref name="physicalScreenPoint"/>.</returns>
    /// <exception cref="System.InvalidOperationException">
    ///     Thrown when the monitor for the supplied point cannot be resolved, or when
    ///     <c>GetDpiForMonitor</c> fails for the resolved monitor. Callers are expected to be
    ///     Per-Monitor (V2) DPI aware (<c>PROCESS_PER_MONITOR_DPI_AWARE_V2</c>) so that logical
    ///     desktop coordinates map to the monitor they fall on. If the process is not PMv2,
    ///     provide explicit monitor/DPI context instead of relying on this helper.
    /// </exception>
    public static uint GetDpiForPhysicalPoint(Windows.Foundation.Point physicalScreenPoint)
    {
        const uint MONITOR_DEFAULTTONEAREST = 2;

        var pt = new POINT((int)physicalScreenPoint.X, (int)physicalScreenPoint.Y);
        var hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        if (hMonitor == IntPtr.Zero)
        {
            throw new InvalidOperationException("Unable to resolve monitor for the supplied physical point. This method requires the caller to be Per-Monitor (V2) DPI aware or to supply explicit monitor/DPI context.");
        }

        var hr = GetDpiForMonitor(hMonitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out var dpiX, out _);
        return hr != 0
            ? throw new InvalidOperationException("GetDpiForMonitor failed for the resolved monitor. Cannot determine monitor DPI.")
            : dpiX;
    }

    /// <summary>Converts a WinRT Point from logical DIPs to physical screen pixels.</summary>
    /// <param name="logicalPoint">Logical point in DIPs.</param>
    /// <param name="dpi">DPI value.</param>
    /// <returns>Physical point in screen pixels.</returns>
    public static POINT GetPhysicalPointFromLogical(Windows.Foundation.Point logicalPoint, uint dpi)
        => new(LogicalToPhysical(logicalPoint.X, dpi), LogicalToPhysical(logicalPoint.Y, dpi));

    /// <summary>
    ///     Converts a logical desktop point (DIPs) to physical screen pixels using the DPI of the
    ///     monitor that contains the point.
    /// </summary>
    /// <param name="logicalScreenPoint">Logical desktop point in device-independent pixels (DIPs).</param>
    /// <returns>Physical point in screen pixels.</returns>
    /// <exception cref="InvalidOperationException">
    ///     Thrown when a monitor cannot be resolved for the supplied logical point. This method
    ///     requires the caller to be Per-Monitor (V2) DPI aware or to supply explicit monitor/DPI
    ///     context.
    /// </exception>
    /// <exception cref="InvalidOperationException">
    ///     Thrown when <c>GetDpiForMonitor</c> fails for the resolved monitor and a valid monitor
    ///     DPI cannot be retrieved; conversion cannot proceed without a valid DPI.
    /// </exception>
    /// <remarks>
    ///     This method is deterministic for Per-Monitor DPI Aware (V2) applications where logical
    ///     desktop coordinates are expressed in the coordinate space of the monitor they fall on.
    ///     In that case the method selects the monitor with <c>MonitorFromPoint</c> using the
    ///     supplied logical coordinates and queries that monitor's DPI via <c>GetDpiForMonitor</c>.
    ///     Notes and edge-cases:
    ///     - For non-PMv2 scenarios (where logical coordinates are globally virtualized) callers
    ///       should provide explicit context (monitor handle, window, or DPI) to avoid ambiguity.
    ///     - Points on monitor seams may require care: if a caller cannot guarantee PMv2 semantics
    ///       consider using <see cref="GetPhysicalPointFromLogical(Windows.Foundation.Point, uint)"/>
    ///       with an explicit DPI or implement logic that supplies the appropriate monitor handle.
    /// </remarks>
    public static Windows.Foundation.Point GetPhysicalScreenPointFromLogical(Windows.Foundation.Point logicalScreenPoint)
    {
        const uint MONITOR_DEFAULTTONEAREST = 2;

        var pt = new POINT((int)logicalScreenPoint.X, (int)logicalScreenPoint.Y);

        var hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        if (hMonitor == IntPtr.Zero)
        {
            throw new InvalidOperationException("Unable to resolve monitor for the supplied logical point. This method requires the caller to be Per-Monitor (V2) DPI aware or to supply explicit monitor/DPI context.");
        }

        var hr = GetDpiForMonitor(hMonitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out var dpiX, out _);
        if (hr != 0)
        {
            throw new InvalidOperationException("GetDpiForMonitor failed for the resolved monitor. Cannot convert logical point without a valid monitor DPI.");
        }

        var actualDpi = dpiX;
        var physical = GetPhysicalPointFromLogical(logicalScreenPoint, actualDpi);
        return new Windows.Foundation.Point(physical.X, physical.Y);
    }

    /// <summary>Converts a POINT from physical screen pixels to logical DIPs.</summary>
    /// <param name="physicalPoint">Physical point in screen pixels.</param>
    /// <param name="dpi">DPI value.</param>
    /// <returns>Logical point in DIPs as WinRT Point.</returns>
    public static Windows.Foundation.Point GetLogicalPointFromPhysical(POINT physicalPoint, uint dpi)
        => new(PhysicalToLogical(physicalPoint.X, dpi), PhysicalToLogical(physicalPoint.Y, dpi));
}
