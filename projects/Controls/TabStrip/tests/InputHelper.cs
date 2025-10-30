// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;
using Windows.UI.Input.Preview.Injection;

namespace DroidNet.Controls.Tabs.Tests;

static internal class InputHelper
{
    [StructLayout(LayoutKind.Sequential)]
    private struct POINT
    {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    private static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern int GetSystemMetrics(int nIndex);

    [DllImport("user32.dll")]
    private static extern bool GetCursorPos(out POINT lpPoint);

    [DllImport("user32.dll")]
    private static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint dwFlags);

    private const uint MONITOR_DEFAULTTONEAREST = 2;

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT
    {
        public int left;
        public int top;
        public int right;
        public int bottom;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    private struct MONITORINFO
    {
        public int cbSize;
        public RECT rcMonitor;
        public RECT rcWork;
        public uint dwFlags;
    }

    [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFO lpmi);

    private const int SM_XVIRTUALSCREEN = 76;
    private const int SM_YVIRTUALSCREEN = 77;
    private const int SM_CXVIRTUALSCREEN = 78;
    private const int SM_CYVIRTUALSCREEN = 79;

    private static int NormalizeToAbsolute(double pixel, int origin, int extent)
    {
        // Map pixel (screen coord) to [0, 65535] over [origin .. origin + extent - 1]
        double denom = Math.Max(1, extent - 1);
        int value = (int)Math.Round(((pixel - origin) * 65535.0) / denom);
        if (value < 0) value = 0;
        else if (value > 65535) value = 65535;
        return value;
    }

    public static async Task SimulatePointerOverAsync(
        TabStripItem tabStripItem,
        InputInjector inputInjector)
    {
        // 1. Transform the TabStripItem into window content coordinates (DIPs)
        var transform = tabStripItem.TransformToVisual(
            VisualUserInterfaceTestsApp.ContentRoot);

        var itemRectDip = transform.TransformBounds(
            new Rect(0, 0, tabStripItem.ActualWidth, tabStripItem.ActualHeight));

        // 2. Compute the center of the item in content DIPs
        double centerXDip = itemRectDip.Left + (itemRectDip.Width / 2.0);
        double centerYDip = itemRectDip.Top + (itemRectDip.Height / 2.0);

        // 3. Get the HWND and translate client (0,0) to screen coordinates
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(VisualUserInterfaceTestsApp.MainWindow);
        var origin = new POINT { X = 0, Y = 0 };
        if (!ClientToScreen(hwnd, ref origin))
            throw new InvalidOperationException("ClientToScreen failed.");

        // 4. Convert to physical pixels using the window DPI (matches ClientToScreen coordinates)
        // Use GetDpiForWindow to get the DPI for the HWND so conversions to pixels align with Win32 coordinates.
        uint dpi = GetDpiForWindow(hwnd);
        double scale = dpi / 96.0;
        double centerXPx = centerXDip * scale;
        double centerYPx = centerYDip * scale;

        // 5. Final target in screen pixels: client origin + content-relative center
        double targetXpx = origin.X + centerXPx;
        double targetYpx = origin.Y + centerYPx;

        // Note: compute physical-pixel target using the HWND DPI (GetDpiForWindow) and
        // normalize injection coordinates relative to the monitor that contains the window
        // (MonitorFromWindow/GetMonitorInfo). This ensures the absolute injected coordinates
        // use the same pixel origin and DPI as ClientToScreen and avoids virtual-desktop vs
        // per-monitor DPI mismatches.

        // 6. Determine the monitor that contains the window and obtain its bounds.
        //    Using the monitor rectangle for normalization avoids multi-monitor DPI/scale mismatches
        //    that can occur when normalizing across the entire virtual desktop.
        int monitorLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int monitorTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int monitorWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int monitorHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        try
        {
            var hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (hmon != IntPtr.Zero)
            {
                var mi = new MONITORINFO { cbSize = Marshal.SizeOf<MONITORINFO>() };
                if (GetMonitorInfo(hmon, ref mi))
                {
                    monitorLeft = mi.rcMonitor.left;
                    monitorTop = mi.rcMonitor.top;
                    monitorWidth = mi.rcMonitor.right - mi.rcMonitor.left;
                    monitorHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
                }
            }
        }
        catch
        {
            // If any monitor APIs fail, fall back to virtual desktop metrics gathered above.
        }

        // 7. Normalize to [0..65535] using monitor-local bounds
        int absX = NormalizeToAbsolute(targetXpx, monitorLeft, monitorWidth);
        int absY = NormalizeToAbsolute(targetYpx, monitorTop, monitorHeight);

        // 9. Inject absolute mouse move
        var moveInfo = new InjectedInputMouseInfo
        {
            MouseOptions = InjectedInputMouseOptions.Move | InjectedInputMouseOptions.Absolute,
            DeltaX = absX,
            DeltaY = absY
        };

        // 8. ACTIVATE the window just before injection
        VisualUserInterfaceTestsApp.MainWindow.Activate();
        await Task.Delay(100).ConfigureAwait(true);

        inputInjector.InjectMouseInput(new[] { moveInfo });
        await Task.Delay(1000).ConfigureAwait(true);
    }

    private static async Task SimulateClickAsync(Button button, InputInjector inputInjector)
    {
        // Get app bounds
        var appBounds = VisualUserInterfaceTestsApp.MainWindow.Bounds;

        // Get transform to window content
        var transform = button.TransformToVisual(VisualUserInterfaceTestsApp.MainWindow.Content);

        // Get bounds of button
        var buttonRect = transform.TransformBounds(new Rect(0, 0, button.ActualWidth, button.ActualHeight));

        // Calculate center position in screen coordinates
        var centerX = appBounds.Left + buttonRect.Left + (buttonRect.Width / 2);
        var centerY = appBounds.Top + buttonRect.Top + (buttonRect.Height / 2);

        // Inject left mouse button down and up at button center
        var downInfo = new InjectedInputMouseInfo
        {
            MouseOptions = InjectedInputMouseOptions.LeftDown | InjectedInputMouseOptions.Absolute,
            DeltaX = (int)(centerX * 65535 / appBounds.Width),
            DeltaY = (int)(centerY * 65535 / appBounds.Height),
        };

        var upInfo = new InjectedInputMouseInfo
        {
            MouseOptions = InjectedInputMouseOptions.LeftUp | InjectedInputMouseOptions.Absolute,
            DeltaX = (int)(centerX * 65535 / appBounds.Width),
            DeltaY = (int)(centerY * 65535 / appBounds.Height),
        };

        inputInjector.InjectMouseInput(new[] { downInfo, upInfo });
    }
}
