// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace DroidNet.Controls;

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

/// <summary>
///     P/Invoke wrappers and native helpers for tab drag overlay implementation.
/// </summary>
public static partial class Native
{
    private const string User32 = "user32.dll";
    private const string Gdi32 = "gdi32.dll";
    private const string SHCore = "shcore.dll";

    /// <summary>Window style flags for layered overlay window.</summary>
    [Flags]
    public enum WindowStyles : uint
    {
        /// <summary>
        ///     The window is a pop-up window. This style cannot be used with the WS_CHILD style.
        /// </summary>
        WS_POPUP = 0x80000000,

        /// <summary>
        ///     The window is initially visible. This style can be turned on and off by using the
        ///     ShowWindow or SetWindowPos function.
        /// </summary>
        WS_VISIBLE = 0x10000000,
    }

    /// <summary>Extended window style flags.</summary>
    [Flags]
    public enum WindowStylesEx : uint
    {
        /// <summary>
        ///     The window should be placed above all non-topmost windows and should stay above
        ///     them, even when the window is deactivated.
        /// </summary>
        WS_EX_TOPMOST = 0x00000008,

        /// <summary>
        ///     The window should not be painted until siblings beneath the window (that were
        ///     created by the same thread) have been painted. The window appears transparent
        ///     because the bits of underlying sibling windows have already been painted.
        /// </summary>
        WS_EX_TRANSPARENT = 0x00000020,

        /// <summary>
        ///     The window is intended to be used as a floating toolbar. A tool window has a title
        ///     bar that is shorter than a normal title bar, and the window title is drawn using a
        ///     smaller font.
        /// </summary>
        WS_EX_TOOLWINDOW = 0x00000080,

        /// <summary>
        ///     The window is a layered window. This style cannot be used if the window has a class
        ///     style of either CS_OWNDC or CS_CLASSDC.
        /// </summary>
        WS_EX_LAYERED = 0x00080000,

        /// <summary>
        ///     A top-level window created with this style does not become the foreground window
        ///     when the user clicks it. The system does not bring this window to the foreground
        ///     when the user minimizes or closes the foreground window.
        /// </summary>
        WS_EX_NOACTIVATE = 0x08000000,
    }

    /// <summary>SetWindowPos flags.</summary>
    [Flags]
    public enum SetWindowPosFlags : uint
    {
        /// <summary>
        ///     Retains the current size (ignores the cx and cy parameters).
        /// </summary>
        SWP_NOSIZE = 0x0001,

        /// <summary>
        ///     Retains the current position (ignores X and Y parameters).
        /// </summary>
        SWP_NOMOVE = 0x0002,

        /// <summary>
        ///     Retains the current Z order (ignores the hWndInsertAfter parameter).
        /// </summary>
        SWP_NOZORDER = 0x0004,

        /// <summary>
        ///     Does not activate the window. If this flag is not set, the window is activated and
        ///     moved to the top of either the topmost or non-topmost group.
        /// </summary>
        SWP_NOACTIVATE = 0x0010,

        /// <summary>
        ///     Displays the window.
        /// </summary>
        SWP_SHOWWINDOW = 0x0040,

        /// <summary>
        ///     Hides the window.
        /// </summary>
        SWP_HIDEWINDOW = 0x0080,

        /// <summary>
        ///     Does not change the owner window's position in the Z order.
        /// </summary>
        SWP_NOOWNERZORDER = 0x0200,
    }

    /// <summary>ShowWindow commands.</summary>
    [Flags]
    public enum ShowWindowCommands : int
    {
        /// <summary>
        ///     Hides the window and activates another window.
        /// </summary>
        SW_HIDE = 0,

        /// <summary>
        ///     Activates and displays a window. If the window is minimized, maximized, or arranged,
        ///     the system restores it to its original size and position.
        /// </summary>
        SW_SHOWNORMAL = 1,

        /// <summary>
        ///     Displays a window in its most recent size and position. This value is similar to
        ///     SW_SHOWNORMAL, except that the window is not activated.
        /// </summary>
        SW_SHOWNOACTIVATE = 4,
    }

    /// <summary>UpdateLayeredWindow flags.</summary>
    [Flags]
    public enum UpdateLayeredWindowFlags : uint
    {
        /// <summary>
        ///     Use pblend as the blend function. If the display mode is 256 colors or less, the
        ///     effect of this value is the same as the effect of ULW_OPAQUE.
        /// </summary>
        ULW_ALPHA = 0x00000002,
    }

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

    /// <summary>GetWindowLongPtr index values.</summary>
    public enum WindowLongIndex : int
    {
        /// <summary>
        ///     Retrieves the extended window styles.
        /// </summary>
        GWL_EXSTYLE = -20,

        /// <summary>
        ///     Retrieves the window styles.
        /// </summary>
        GWL_STYLE = -16,
    }

    /// <summary>Blend function for UpdateLayeredWindow.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BLENDFUNCTION
    {
        /// <summary>Currently, the only source blend operation defined.</summary>
        public const byte AC_SRC_OVER = 0x00;

        /// <summary>This flag is set when the bitmap has an Alpha channel (that is, per-pixel alpha).</summary>
        public const byte AC_SRC_ALPHA = 0x01;

        /// <summary>The source blend operation. Currently, the only source blend operation defined is AC_SRC_OVER.</summary>
        public byte BlendOp;

        /// <summary>Must be zero.</summary>
        public byte BlendFlags;

        /// <summary>Specifies an alpha transparency value to be used on the entire source bitmap.</summary>
        public byte SourceConstantAlpha;

        /// <summary>This member controls the way the source and destination bitmaps are interpreted.</summary>
        public byte AlphaFormat;
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

    /// <summary>SIZE structure.</summary>
    /// <remarks>
    /// Initializes a new instance of the <see cref="SIZE"/> struct.
    /// </remarks>
    /// <param name="width">The width.</param>
    /// <param name="height">The height.</param>
    [StructLayout(LayoutKind.Sequential)]
    public struct SIZE(int width, int height)
    {
        /// <summary>
        /// The width.
        /// </summary>
        public int Width = width;

        /// <summary>
        /// The height.
        /// </summary>
        public int Height = height;
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
    public static partial bool GetCursorPos(out POINT lpPoint);

    [LibraryImport(User32, SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial IntPtr CreateWindowExW(
        WindowStylesEx dwExStyle,
        [MarshalAs(UnmanagedType.LPWStr)] string lpClassName,
        [MarshalAs(UnmanagedType.LPWStr)] string lpWindowName,
        WindowStyles dwStyle,
        int x,
        int y,
        int nWidth,
        int nHeight,
        IntPtr hWndParent,
        IntPtr hMenu,
        IntPtr hInstance,
        IntPtr lpParam);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool DestroyWindow(IntPtr hWnd);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool UpdateLayeredWindow(
        IntPtr hWnd,
        IntPtr hdcDst,
        in POINT pptDst,
        in SIZE psize,
        IntPtr hdcSrc,
        in POINT pptSrc,
        uint crKey,
        in BLENDFUNCTION pblend,
        UpdateLayeredWindowFlags dwFlags);

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
        SetWindowPosFlags uFlags);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool ShowWindow(IntPtr hWnd, ShowWindowCommands nCmdShow);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial IntPtr GetDC(IntPtr hWnd);

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [LibraryImport(User32, EntryPoint = "GetWindowLongPtrW", SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial nint GetWindowLongPtr(IntPtr hWnd, WindowLongIndex nIndex);

    [LibraryImport(User32, EntryPoint = "SetWindowLongPtrW", SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial nint SetWindowLongPtr(IntPtr hWnd, WindowLongIndex nIndex, nint dwNewLong);

    [LibraryImport(Gdi32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial IntPtr CreateCompatibleDC(IntPtr hdc);

    [LibraryImport(Gdi32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool DeleteDC(IntPtr hdc);

    /// <summary>Creates a DIB (device-independent bitmap) that applications can write to directly.</summary>
    /// <param name="hdc">A handle to a device context.</param>
    /// <param name="pbmi">A BITMAPINFO structure that specifies various attributes of the DIB.</param>
    /// <param name="usage">The type of data contained in the bmiColors array member of the BITMAPINFO structure.</param>
    /// <param name="ppvBits">A pointer to a variable that receives a pointer to the location of the DIB bit values.</param>
    /// <param name="hSection">A handle to a file-mapping object or NULL.</param>
    /// <param name="offset">The offset from the beginning of the file-mapping object.</param>
    /// <returns>If the function succeeds, the return value is a handle to the newly created DIB.</returns>
    [DllImport(Gdi32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Interoperability", "CA1401:P/Invokes should not be visible", Justification = "Uses DllImport for complex BITMAPINFO marshalling; LibraryImport does not support marshalling this struct with ref semantics.")]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Interoperability", "SYSLIB1054:Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time", Justification = "Uses DllImport for complex BITMAPINFO marshalling; LibraryImport does not support marshalling this struct with ref semantics.")]
    public static extern IntPtr CreateDIBSection(
        IntPtr hdc,
        [In] ref BITMAPINFO pbmi,
        uint usage,
        out IntPtr ppvBits,
        IntPtr hSection,
        uint offset);

    [LibraryImport(Gdi32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool DeleteObject(IntPtr hObject);

    [LibraryImport(Gdi32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    public static partial IntPtr SelectObject(IntPtr hdc, IntPtr h);

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

    /// <summary>BITMAPINFOHEADER structure.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BITMAPINFOHEADER
    {
        /// <summary>The number of bytes required by the structure.</summary>
        public uint biSize;

        /// <summary>The width of the bitmap, in pixels.</summary>
        public int biWidth;

        /// <summary>The height of the bitmap, in pixels. If positive, the bitmap is bottom-up; if negative, top-down.</summary>
        public int biHeight;

        /// <summary>The number of planes for the target device. This value must be set to 1.</summary>
        public ushort biPlanes;

        /// <summary>The number of bits per pixel.</summary>
        public ushort biBitCount;

        /// <summary>The type of compression for a compressed bottom-up bitmap.</summary>
        public uint biCompression;

        /// <summary>The size, in bytes, of the image. This may be set to zero for BI_RGB bitmaps.</summary>
        public uint biSizeImage;

        /// <summary>The horizontal resolution, in pixels per meter, of the target device for the bitmap.</summary>
        public int biXPelsPerMeter;

        /// <summary>The vertical resolution, in pixels per meter, of the target device for the bitmap.</summary>
        public int biYPelsPerMeter;

        /// <summary>The number of color indexes in the color table that are actually used by the bitmap.</summary>
        public uint biClrUsed;

        /// <summary>The number of color indexes that are required for displaying the bitmap.</summary>
        public uint biClrImportant;
    }

    /// <summary>BITMAPINFO structure.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BITMAPINFO
    {
        /// <summary>A BITMAPINFOHEADER structure that contains information about the dimensions and color format of a DIB.</summary>
        public BITMAPINFOHEADER bmiHeader;

        /// <summary>An array of RGBQUAD structures that define the colors in the bitmap.</summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 1)]
        public uint[] bmiColors;
    }

    /// <summary>Uncompressed RGB format constant for BITMAPINFOHEADER.biCompression.</summary>
    public const int BI_RGB = 0;

    /// <summary>Color table contains literal RGB values constant for CreateDIBSection usage parameter.</summary>
    public const int DIB_RGB_COLORS = 0;

    /// <summary>Handle to place the window above all non-topmost windows (SetWindowPos).</summary>
    public static readonly IntPtr HWND_TOPMOST = new(-1);

    /// <summary>Converts logical (DIP) coordinates to physical pixels using the specified DPI.</summary>
    /// <param name="logical">Logical coordinate value.</param>
    /// <param name="dpi">DPI value (e.g., 96 for 100% scaling, 144 for 150%).</param>
    /// <returns>Physical pixel value.</returns>
    public static int LogicalToPhysical(double logical, uint dpi)
    {
        const uint StandardDpi = 96;
        return (int)Math.Round(logical * dpi / StandardDpi);
    }

    /// <summary>Converts physical pixels to logical (DIP) coordinates using the specified DPI.</summary>
    /// <param name="physical">Physical pixel value.</param>
    /// <param name="dpi">DPI value (e.g., 96 for 100% scaling, 144 for 150%).</param>
    /// <returns>Logical coordinate value.</returns>
    public static double PhysicalToLogical(int physical, uint dpi)
    {
        const uint StandardDpi = 96;
        return physical * StandardDpi / (double)dpi;
    }

    /// <summary>Gets the DPI for the monitor containing the specified point.</summary>
    /// <param name="screenPoint">Screen point in logical pixels.</param>
    /// <returns>DPI value, or 96 (100% scaling) if retrieval fails.</returns>
    public static uint GetDpiForPoint(Windows.Foundation.Point screenPoint)
    {
        const uint DefaultDpi = 96;
        const uint MONITOR_DEFAULTTONEAREST = 2;

        var pt = new POINT((int)screenPoint.X, (int)screenPoint.Y);
        var hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

        if (hMonitor == IntPtr.Zero)
        {
            return DefaultDpi;
        }

        var hr = GetDpiForMonitor(hMonitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out var dpiX, out _);
        return hr == 0 ? dpiX : DefaultDpi;
    }

    /// <summary>Converts a WinRT Point from logical to physical coordinates.</summary>
    /// <param name="logicalPoint">Logical point.</param>
    /// <param name="dpi">DPI value.</param>
    /// <returns>Physical point.</returns>
    public static POINT LogicalToPhysicalPoint(Windows.Foundation.Point logicalPoint, uint dpi)
        => new(LogicalToPhysical(logicalPoint.X, dpi), LogicalToPhysical(logicalPoint.Y, dpi));

    /// <summary>Converts a POINT from physical to logical coordinates.</summary>
    /// <param name="physicalPoint">Physical point.</param>
    /// <param name="dpi">DPI value.</param>
    /// <returns>Logical point as WinRT Point.</returns>
    public static Windows.Foundation.Point PhysicalToLogicalPoint(POINT physicalPoint, uint dpi)
        => new(PhysicalToLogical(physicalPoint.X, dpi), PhysicalToLogical(physicalPoint.Y, dpi));

    /// <summary>Converts a WinRT Size from logical to physical coordinates.</summary>
    /// <param name="logicalSize">Logical size.</param>
    /// <param name="dpi">DPI value.</param>
    /// <returns>Physical size.</returns>
    public static SIZE LogicalToPhysicalSize(Windows.Foundation.Size logicalSize, uint dpi)
        => new(LogicalToPhysical(logicalSize.Width, dpi), LogicalToPhysical(logicalSize.Height, dpi));
}
