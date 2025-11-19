// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1201 // Elements should appear in the correct order
#pragma warning disable SA1307 // Accessible fields should begin with upper-case letter

using System.Runtime.InteropServices;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Interop helpers for window placement persistence helpers for <see cref="WindowManagerService"/>.
/// </summary>
public sealed partial class WindowManagerService
{
    private const uint MonitorDefaultToNearest = 2;
    private const uint MonitorDefaultToPrimary = 1;

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT(int x, int y)
    {
        public int X = x;
        public int Y = y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;

        public readonly int Width => this.Right - this.Left;

        public readonly int Height => this.Bottom - this.Top;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MONITORINFO
    {
        public uint cbSize;
        public RECT rcMonitor;
        public RECT rcWork;
        public uint dwFlags;
    }

    // Move method before structs to fix SA1201
    [LibraryImport("user32.dll", EntryPoint = "MonitorFromPoint", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial IntPtr MonitorFromPoint(POINT pt, uint dwFlags);

    [LibraryImport("user32.dll", EntryPoint = "GetMonitorInfoW", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFO lpmi);

    private static bool RectsIntersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
    {
        var aLeft = ax;
        var aTop = ay;
        var aRight = ax + aw;
        var aBottom = ay + ah;
        var bLeft = bx;
        var bTop = by;
        var bRight = bx + bw;
        var bBottom = by + bh;
        return aLeft < bRight && aRight > bLeft && aTop < bBottom && aBottom > bTop;
    }

    private static Windows.Graphics.RectInt32 ClampToWorkArea(Windows.Graphics.RectInt32 rect, RECT work)
    {
        var width = Math.Min(rect.Width, work.Width);
        var height = Math.Min(rect.Height, work.Height);
        var x = Math.Max(work.Left, Math.Min(rect.X, work.Right - width));
        var y = Math.Max(work.Top, Math.Min(rect.Y, work.Bottom - height));
        return new Windows.Graphics.RectInt32 { X = x, Y = y, Width = width, Height = height };
    }
}
