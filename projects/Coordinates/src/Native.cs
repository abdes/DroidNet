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

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

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
    /// <param name="logicalValue">Logical coordinate value in DIPs.</param>
    /// <param name="dpi">DPI value (e.g., 96 for 100% scaling, 144 for 150%).</param>
    /// <returns>Physical pixel value.</returns>
    public static int LogicalToPhysical(double logicalValue, uint dpi)
    {
        const uint StandardDpi = 96;
        return (int)Math.Round(logicalValue * dpi / StandardDpi);
    }

    /// <summary>Converts physical pixels to logical (DIP) coordinates using the specified DPI.</summary>
    /// <param name="physicalValue">Physical pixel value.</param>
    /// <param name="dpi">DPI value (e.g., 96 for 100% scaling, 144 for 150%).</param>
    /// <returns>Logical coordinate value in DIPs.</returns>
    public static double PhysicalToLogical(int physicalValue, uint dpi)
    {
        const uint StandardDpi = 96;
        return physicalValue * StandardDpi / (double)dpi;
    }

    /// <summary>Gets the DPI for the monitor containing the specified point.</summary>
    /// <param name="physicalScreenPoint">Screen point in **PHYSICAL PIXELS** (e.g., from GetCursorPos).</param>
    /// <returns>DPI value, or 96 (100% scaling) if retrieval fails.</returns>
    public static uint GetDpiForPhysicalPoint(Windows.Foundation.Point physicalScreenPoint)
    {
        const uint DefaultDpi = 96;
        const uint MONITOR_DEFAULTTONEAREST = 2;

        var pt = new POINT((int)physicalScreenPoint.X, (int)physicalScreenPoint.Y);
        var hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

        if (hMonitor == IntPtr.Zero)
        {
            return DefaultDpi;
        }

        var hr = GetDpiForMonitor(hMonitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out var dpiX, out _);
        return hr == 0 ? dpiX : DefaultDpi;
    }

    /// <summary>Converts a WinRT Point from logical DIPs to physical screen pixels.</summary>
    /// <param name="logicalPoint">Logical point in DIPs.</param>
    /// <param name="dpi">DPI value.</param>
    /// <returns>Physical point in screen pixels.</returns>
    public static POINT GetPhysicalPointFromLogical(Windows.Foundation.Point logicalPoint, uint dpi)
        => new(LogicalToPhysical(logicalPoint.X, dpi), LogicalToPhysical(logicalPoint.Y, dpi));

    /// <summary>Converts a logical screen point to physical screen pixels using the correct monitor DPI.</summary>
    /// <param name="logicalScreenPoint">Logical screen point in DIPs.</param>
    /// <returns>Physical point in screen pixels.</returns>
    /// <remarks>
    /// For multi-monitor setups with different DPIs, this method finds the monitor containing
    /// the logical point and uses that monitor's DPI for the conversion.
    /// </remarks>
    public static Windows.Foundation.Point GetPhysicalScreenPointFromLogical(Windows.Foundation.Point logicalScreenPoint)
    {
        // First, convert using default DPI to get approximate physical location
        const uint DefaultDpi = 96;
        var approxPhysicalPoint = GetPhysicalPointFromLogical(logicalScreenPoint, DefaultDpi);

        // Find the monitor at this location and get its actual DPI
        var actualDpi = GetDpiForPhysicalPoint(new Windows.Foundation.Point(approxPhysicalPoint.X, approxPhysicalPoint.Y));

        // Convert again using the correct DPI
        var correctPhysicalPoint = GetPhysicalPointFromLogical(logicalScreenPoint, actualDpi);
        return new Windows.Foundation.Point(correctPhysicalPoint.X, correctPhysicalPoint.Y);
    }

    /// <summary>Converts a POINT from physical screen pixels to logical DIPs.</summary>
    /// <param name="physicalPoint">Physical point in screen pixels.</param>
    /// <param name="dpi">DPI value.</param>
    /// <returns>Logical point in DIPs as WinRT Point.</returns>
    public static Windows.Foundation.Point GetLogicalPointFromPhysical(POINT physicalPoint, uint dpi)
        => new(PhysicalToLogical(physicalPoint.X, dpi), PhysicalToLogical(physicalPoint.Y, dpi));

    /// <summary>Converts a WinRT Size from logical DIPs to physical screen pixels.</summary>
    /// <param name="logicalSize">Logical size in DIPs.</param>
    /// <param name="dpi">DPI value.</param>
    /// <returns>Physical size in screen pixels.</returns>
    public static SIZE GetPhysicalSizeFromLogical(Windows.Foundation.Size logicalSize, uint dpi)
        => new(LogicalToPhysical(logicalSize.Width, dpi), LogicalToPhysical(logicalSize.Height, dpi));

    /// <summary>
    /// Represents a rectangle in physical screen pixels.
    /// </summary>
    /// <param name="Left">The x-coordinate of the left edge.</param>
    /// <param name="Top">The y-coordinate of the top edge.</param>
    /// <param name="Right">The x-coordinate of the right edge.</param>
    /// <param name="Bottom">The y-coordinate of the bottom edge.</param>
    [StructLayout(LayoutKind.Sequential)]
    public readonly record struct PhysicalRect(int Left, int Top, int Right, int Bottom)
    {
        /// <summary>Gets the width of the rectangle in physical pixels.</summary>
        public int Width => this.Right - this.Left;

        /// <summary>Gets the height of the rectangle in physical pixels.</summary>
        public int Height => this.Bottom - this.Top;

        /// <summary>
        /// Checks if a physical point is within this rectangle.
        /// </summary>
        /// <param name="physicalPoint">Point in physical screen pixels.</param>
        /// <returns>True if the point is within the rectangle bounds.</returns>
        public bool Contains(Windows.Foundation.Point physicalPoint)
            => physicalPoint.X >= this.Left && physicalPoint.X <= this.Right &&
               physicalPoint.Y >= this.Top && physicalPoint.Y <= this.Bottom;
    }

    /// <summary>
    /// Gets the DPI value from a WinUI XamlRoot's RasterizationScale.
    /// </summary>
    /// <param name="xamlRoot">The XamlRoot to get DPI from.</param>
    /// <returns>DPI value (e.g., 96 for 100%, 168 for 175%).</returns>
    public static uint GetDpiFromXamlRoot(Microsoft.UI.Xaml.XamlRoot? xamlRoot)
    {
        const uint DefaultDpi = 96;
        return xamlRoot == null ? DefaultDpi : (uint)(xamlRoot.RasterizationScale * 96);
    }

    /// <summary>
    /// Gets the Window containing the specified UI element using the XamlRoot's AppWindowId.
    /// </summary>
    /// <param name="element">The UI element to find the window for.</param>
    /// <returns>The Window containing the element, or null if not found or no XamlRoot.</returns>
    /// <remarks>
    /// This method uses XamlRoot.ContentIslandEnvironment.AppWindowId to get the WindowId,
    /// then converts it back to an HWND to find the matching Window. This works even when
    /// XamlRoot.Content is a custom type like RouterOutlet.
    /// </remarks>
    public static Microsoft.UI.Xaml.Window? GetWindowForElement(Microsoft.UI.Xaml.FrameworkElement element)
    {
        if (element.XamlRoot?.ContentIslandEnvironment == null)
        {
            return null;
        }

        var windowId = element.XamlRoot.ContentIslandEnvironment.AppWindowId;
        var hwnd = Microsoft.UI.Win32Interop.GetWindowFromWindowId(windowId);

        if (hwnd == IntPtr.Zero)
        {
            return null;
        }

        // Unfortunately, there's no direct API to get Window from HWND in WinUI 3
        // The app must track windows themselves using a Dictionary<WindowId, Window>
        // For now, return null - callers should pass Window reference directly
        return null;
    }

    /// <summary>
    /// Gets the HWND (window handle) for the window containing the specified UI element.
    /// </summary>
    /// <param name="element">The UI element to find the window handle for.</param>
    /// <returns>The HWND of the window, or IntPtr.Zero if not found.</returns>
    public static IntPtr GetHwndForElement(Microsoft.UI.Xaml.FrameworkElement element)
    {
        if (element.XamlRoot?.ContentIslandEnvironment == null)
        {
            return IntPtr.Zero;
        }

        var windowId = element.XamlRoot.ContentIslandEnvironment.AppWindowId;
        return Microsoft.UI.Win32Interop.GetWindowFromWindowId(windowId);
    }

    /// <summary>
    /// Gets the physical screen bounds of a UI element using the element's XamlRoot to calculate
    /// logical-to-physical conversion. This method uses TransformToVisual(null) which may have
    /// coordinate system issues in multi-monitor setups with different DPI scales.
    /// </summary>
    /// <param name="element">The UI element to get bounds for.</param>
    /// <returns>Physical rectangle in screen pixels, or null if the element has no XamlRoot.</returns>
    /// <remarks>
    /// IMPORTANT: This method has known accuracy issues in multi-monitor setups where the window
    /// extends content into the titlebar. TransformToVisual(null) returns coordinates in a
    /// DPI-virtualized space that doesn't properly map to physical screen coordinates from GetCursorPos.
    /// For accurate bounds, use GetPhysicalScreenBoundsUsingWindowRect instead.
    /// </remarks>
    public static PhysicalRect? GetPhysicalScreenBounds(Microsoft.UI.Xaml.FrameworkElement element)
    {
        if (element.XamlRoot == null)
        {
            return null;
        }

        var dpi = GetDpiFromXamlRoot(element.XamlRoot);

        // Get element's position in screen coordinates (LOGICAL, desktop-relative DIPs)
        // Note: This may not account properly for multi-monitor DPI differences
        var transform = element.TransformToVisual(visual: null);
        var logicalTopLeft = transform.TransformPoint(new Windows.Foundation.Point(0, 0));
        var logicalBottomRight = transform.TransformPoint(new Windows.Foundation.Point(element.ActualWidth, element.ActualHeight));

        // Convert from LOGICAL to PHYSICAL using the element's DPI
        return new PhysicalRect(
            Left: LogicalToPhysical(logicalTopLeft.X, dpi),
            Top: LogicalToPhysical(logicalTopLeft.Y, dpi),
            Right: LogicalToPhysical(logicalBottomRight.X, dpi),
            Bottom: LogicalToPhysical(logicalBottomRight.Y, dpi));
    }

    /// <summary>
    /// Gets the physical screen bounds of a UI element using Win32 GetWindowRect for accurate positioning.
    /// This method uses the element's XamlRoot to get the window HWND directly, avoiding the need
    /// for a Window reference.
    /// </summary>
    /// <param name="element">The UI element to get bounds for.</param>
    /// <returns>Physical rectangle in screen pixels, or null on failure.</returns>
    /// <remarks>
    /// This is the ACCURATE method for getting physical screen bounds in multi-monitor setups.
    /// It uses Win32 GetWindowRect to get the window's physical position, then transforms the
    /// element's position relative to the window content, avoiding WinUI's DPI virtualization issues.
    /// </remarks>
    public static PhysicalRect? GetPhysicalScreenBoundsUsingWindowRect(Microsoft.UI.Xaml.FrameworkElement element)
    {
        var hwnd = GetHwndForElement(element);
        if (hwnd == IntPtr.Zero || !GetWindowRect(hwnd, out var windowRect))
        {
            return null;
        }

        if (element.XamlRoot?.Content == null)
        {
            return null;
        }

        var dpi = GetDpiForWindow(hwnd);

        // Get element's position within its window (LOGICAL window-relative coordinates)
        var transformToWindow = element.TransformToVisual(element.XamlRoot.Content);
        var logicalTopLeftInWindow = transformToWindow.TransformPoint(new Windows.Foundation.Point(0, 0));
        var logicalBottomRightInWindow = transformToWindow.TransformPoint(new Windows.Foundation.Point(element.ActualWidth, element.ActualHeight));

        // Convert element's window-relative position from LOGICAL to PHYSICAL
        var physicalLeftInWindow = LogicalToPhysical(logicalTopLeftInWindow.X, dpi);
        var physicalTopInWindow = LogicalToPhysical(logicalTopLeftInWindow.Y, dpi);
        var physicalRightInWindow = LogicalToPhysical(logicalBottomRightInWindow.X, dpi);
        var physicalBottomInWindow = LogicalToPhysical(logicalBottomRightInWindow.Y, dpi);

        // Calculate element's absolute PHYSICAL screen bounds
        return new PhysicalRect(
            Left: windowRect.Left + physicalLeftInWindow,
            Top: windowRect.Top + physicalTopInWindow,
            Right: windowRect.Left + physicalRightInWindow,
            Bottom: windowRect.Top + physicalBottomInWindow);
    }

    /// <summary>
    /// Gets the physical screen bounds of a UI element using Win32 GetWindowRect for accurate positioning.
    /// This method requires a Window instance to get the HWND.
    /// </summary>
    /// <param name="element">The UI element to get bounds for.</param>
    /// <param name="window">The Window containing the element.</param>
    /// <returns>Physical rectangle in screen pixels, or null on failure.</returns>
    [Obsolete("Use GetPhysicalScreenBoundsUsingWindowRect() instead - it doesn't require a Window parameter")]
    public static PhysicalRect? GetPhysicalScreenBoundsFromWindow(Microsoft.UI.Xaml.FrameworkElement element, Microsoft.UI.Xaml.Window window)
    {
        if (window?.Content == null)
        {
            return null;
        }

        IntPtr hwnd;
        try
        {
            hwnd = WinRT.Interop.WindowNative.GetWindowHandle(window);
        }
        catch (InvalidCastException)
        {
            return null;
        }

        if (hwnd == IntPtr.Zero || !GetWindowRect(hwnd, out var windowRect))
        {
            return null;
        }

        var dpi = GetDpiForWindow(hwnd);

        // Get element's position within its window (LOGICAL window-relative coordinates)
        var transformToWindow = element.TransformToVisual(window.Content);
        var logicalTopLeftInWindow = transformToWindow.TransformPoint(new Windows.Foundation.Point(0, 0));
        var logicalBottomRightInWindow = transformToWindow.TransformPoint(new Windows.Foundation.Point(element.ActualWidth, element.ActualHeight));

        // Convert element's window-relative position from LOGICAL to PHYSICAL
        var physicalLeftInWindow = LogicalToPhysical(logicalTopLeftInWindow.X, dpi);
        var physicalTopInWindow = LogicalToPhysical(logicalTopLeftInWindow.Y, dpi);
        var physicalRightInWindow = LogicalToPhysical(logicalBottomRightInWindow.X, dpi);
        var physicalBottomInWindow = LogicalToPhysical(logicalBottomRightInWindow.Y, dpi);

        // Calculate element's absolute PHYSICAL screen bounds
        return new PhysicalRect(
            Left: windowRect.Left + physicalLeftInWindow,
            Top: windowRect.Top + physicalTopInWindow,
            Right: windowRect.Left + physicalRightInWindow,
            Bottom: windowRect.Top + physicalBottomInWindow);
    }
}
