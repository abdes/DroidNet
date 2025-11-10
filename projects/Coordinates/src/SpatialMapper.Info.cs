// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Windows.Foundation;

namespace DroidNet.Coordinates;

/// <summary>
///     Provides spatial coordinate mapping between different UI spaces such as element, window, and screen coordinates.
///     Handles conversions between element-relative, window-relative, and screen-absolute coordinate systems.
/// </summary>
public partial class SpatialMapper
{
    /// <summary>Standard DPI value (100% scaling).</summary>
    private const uint StandardDpi = 96;

    /// <summary>Monitor selection flag: return the monitor with the largest overlap with the target area.</summary>
    private const uint MonitorDefaultToNearest = 2;

    /// <inheritdoc />
    public WindowInfo WindowInfo
    {
        get
        {
            this.EnsureWindowHandle();
            var wndDpi = Native.GetDpiForWindow(this.hwnd);

            // Window outer rect
            if (!Native.GetWindowRect(this.hwnd, out var winRect))
            {
                return new WindowInfo(new Point(0, 0), new Size(0, 0), new Point(0, 0), new Size(0, 0), StandardDpi);
            }

            // Client rect size (physical px) -> convert to logical using window DPI
            var clientOriginLogical = new Point(0, 0);
            var clientSizeLogical = new Size(0, 0);

            if (Native.GetClientRect(this.hwnd, out var clientRect))
            {
                // Determine client origin in physical pixels then convert using window DPI
                // (consistent with window rect conversion above)
                var physOrigin = new Native.POINT(0, 0);
                if (Native.ClientToScreen(this.hwnd, ref physOrigin))
                {
                    var logicalOrigin = Native.GetLogicalPointFromPhysical(physOrigin, wndDpi);
                    clientOriginLogical = logicalOrigin;
                }

                clientSizeLogical = new Size(Native.PhysicalToLogical(clientRect.Width, wndDpi), Native.PhysicalToLogical(clientRect.Height, wndDpi));
            }

            // Window outer rect logical (use window DPI)
            var windowOriginLogical = new Point(Native.PhysicalToLogical(winRect.Left, wndDpi), Native.PhysicalToLogical(winRect.Top, wndDpi));
            var windowSizeLogical = new Size(Native.PhysicalToLogical(winRect.Width, wndDpi), Native.PhysicalToLogical(winRect.Height, wndDpi));

            return new WindowInfo(clientOriginLogical, clientSizeLogical, windowOriginLogical, windowSizeLogical, wndDpi);
        }
    }

    /// <inheritdoc />
    public WindowMonitorInfo WindowMonitorInfo
    {
        get
        {
            this.EnsureWindowHandle();

            if (!Native.GetWindowRect(this.hwnd, out var wr))
            {
                return new WindowMonitorInfo(IntPtr.Zero, 0, 0, 0.0, 0.0, StandardDpi);
            }

            // Use the window center to pick the monitor
            var centerX = wr.Left + (wr.Width / 2);
            var centerY = wr.Top + (wr.Height / 2);
            var mon = Native.MonitorFromPoint(new Native.POINT(centerX, centerY), MonitorDefaultToNearest);
            if (mon == IntPtr.Zero)
            {
                return new WindowMonitorInfo(IntPtr.Zero, 0, 0, 0.0, 0.0, StandardDpi);
            }

            var mi = default(Native.MONITORINFO);
            mi.cbSize = (uint)System.Runtime.InteropServices.Marshal.SizeOf<Native.MONITORINFO>();
            if (!Native.GetMonitorInfo(mon, ref mi))
            {
                return new WindowMonitorInfo(mon, 0, 0, 0.0, 0.0, StandardDpi);
            }

            var physW = mi.rcMonitor.Width;
            var physH = mi.rcMonitor.Height;

            // Query monitor DPI
            if (Native.GetDpiForMonitor(mon, Native.MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out var mdpiX, out var mdpiY) != 0)
            {
                mdpiX = StandardDpi;
            }

            var logicalW = Native.PhysicalToLogical(physW, mdpiX);
            var logicalH = Native.PhysicalToLogical(physH, mdpiX);

            return new WindowMonitorInfo(mon, physW, physH, logicalW, logicalH, mdpiX);
        }
    }
}
