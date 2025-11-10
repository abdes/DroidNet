// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Windows.Graphics;
using Windows.Graphics.Imaging;
using WinPoint = Windows.Foundation.Point;
using WinSize = Windows.Foundation.Size;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Implementation of <see cref="IDragVisualService"/> using a Win32 layered window for the drag
///     overlay. The overlay is topmost, non-activating, click-through, and survives source
///     AppWindow closure.
/// </summary>
public partial class DragVisualService : IDragVisualService
{
    private const double DefaultWidthDip = 400.0;
    private const double DefaultHeightDip = 200.0;
    private const double PreviewSpacingDip = 4.0;

    private readonly ILogger logger;
    private readonly DispatcherQueue dispatcherQueue;
    private readonly Lock syncLock = new();
    private readonly bool suppressOverlay;
    private readonly DroidNet.Coordinates.RawSpatialMapperFactory spatialMapperFactory;

    private DragSessionToken? activeToken;
    private DragVisualDescriptor? activeDescriptor;
    private NativeDragOverlayWindow? overlayWindow;
    private ISpatialMapper? spatial;
    private SpatialPoint<ScreenSpace> windowPositionOffsets;
    private bool windowIsShown;
    private SpatialPoint<PhysicalScreenSpace> lastPointerPosition;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DragVisualService"/> class.
    /// </summary>
    /// <param name="hosting">The hosting context that exposes the UI dispatcher.</param>
    /// <param name="loggerFactory">Optional logger factory.</param>
    public DragVisualService(
        HostingContext hosting,
        RawSpatialMapperFactory rawMapperFactory,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(hosting);

        this.spatialMapperFactory = rawMapperFactory ?? throw new ArgumentNullException(nameof(rawMapperFactory));

        this.logger = loggerFactory?.CreateLogger<DragVisualService>() ?? NullLogger<DragVisualService>.Instance;
        this.dispatcherQueue = hosting.Dispatcher;
        this.suppressOverlay = DesignModeService.IsDesignModeEnabled;

        this.LogCreated();
    }

    /// <inheritdoc />
    public DragSessionToken StartSession(
        DragVisualDescriptor descriptor,
        SpatialPoint<PhysicalScreenSpace> initialPosition,
        SpatialPoint<ScreenSpace> hotspotOffsets)
    {
        this.ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(descriptor);

        if (this.suppressOverlay)
        {
            this.LogSessionSuppressedForDesignMode();
            return default;
        }

        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (this.activeToken.HasValue)
            {
                this.LogSessionAlreadyActive();
                throw new InvalidOperationException("A drag visual session is already active.");
            }

            var token = new DragSessionToken { Id = Guid.NewGuid() };
            this.activeToken = token;
            this.activeDescriptor = descriptor;
            this.windowPositionOffsets = hotspotOffsets;
            this.lastPointerPosition = initialPosition;

            descriptor.PropertyChanged += this.OnDescriptorPropertyChanged;

            this.overlayWindow = new NativeDragOverlayWindow(this);

            // Create a mapper bound to the overlay HWND via the injected factory. The
            // factory is required; do not fall back to manual math.
            var hwnd = new IntPtr((long)this.overlayWindow.Hwnd);
            this.spatial = this.spatialMapperFactory(hwnd)
                ?? throw new InvalidOperationException("A spatial mapper is required for overlay positioning.");

            this.RefreshOverlayVisual();
            this.MoveOverlay(initialPosition);

            this.overlayWindow.Show();
            this.windowIsShown = true;

            this.LogSessionStarted();

            return token;
        }
    }

    /// <inheritdoc />
    public void UpdatePosition(DragSessionToken token, SpatialPoint<PhysicalScreenSpace> position)
    {
        this.ThrowIfDisposed();
        if (this.suppressOverlay)
        {
            return;
        }

        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (!this.activeToken.HasValue || this.activeToken.Value != token)
            {
                this.LogTokenMismatchUpdatePosition(token);
                return;
            }

            if (this.overlayWindow is null)
            {
                this.LogOverlayWindowNotInitialized();
                return;
            }

            this.lastPointerPosition = position;
            this.MoveOverlay(position);

            if (!this.windowIsShown)
            {
                this.overlayWindow.Show();
                this.windowIsShown = true;
            }

            var windowPosition = this.overlayWindow.CurrentPosition;
            this.LogPositionUpdated(
                position.Point,
                new WinPoint(windowPosition.X, windowPosition.Y));
        }
    }

    /// <inheritdoc />
    public void EndSession(DragSessionToken token)
    {
        this.ThrowIfDisposed();
        if (this.suppressOverlay)
        {
            return;
        }

        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (!this.activeToken.HasValue || this.activeToken.Value != token)
            {
                this.LogTokenMismatchEndSession(token);
                return;
            }

            if (this.activeDescriptor is not null)
            {
                this.activeDescriptor.PropertyChanged -= this.OnDescriptorPropertyChanged;
                this.activeDescriptor.HeaderBitmap = null;
                this.activeDescriptor.PreviewBitmap = null;
            }

            this.DestroyOverlayWindow();
            this.windowIsShown = false;
            this.activeDescriptor = null;
            this.activeToken = null;
            this.windowPositionOffsets = default;
            this.lastPointerPosition = default;

            this.LogSessionEnded();
        }
    }

    /// <inheritdoc />
    public DragVisualDescriptor? GetDescriptor(DragSessionToken token)
    {
        this.ThrowIfDisposed();
        lock (this.syncLock)
        {
            return !this.activeToken.HasValue || this.activeToken.Value != token ? null : this.activeDescriptor;
        }
    }

    private void AssertUIThread()
    {
        if (!this.dispatcherQueue.HasThreadAccess)
        {
            throw new InvalidOperationException("IDragVisualService methods must be called from the UI thread.");
        }
    }

    private void RefreshOverlayVisual()
    {
        if (this.overlayWindow is null || this.activeDescriptor is null)
        {
            return;
        }

        Debug.Assert(this.spatial is not null, "Overlay mapper should be initialized by StartSession and must not be null here.");

        var descriptor = this.activeDescriptor;
        var size = this.ToPixelSize(descriptor.RequestedSize);
        var header = CreateNativeImage(descriptor.HeaderBitmap);
        var preview = CreateNativeImage(descriptor.PreviewBitmap);
        var spacingPx = this.DipToPixels(PreviewSpacingDip);

        this.overlayWindow.SetSize(size);
        this.MoveOverlay(this.lastPointerPosition);
        this.overlayWindow.UpdateContent(header, preview, size, spacingPx);
    }

    private void MoveOverlay(SpatialPoint<PhysicalScreenSpace> pointerPosition)
    {
        if (this.overlayWindow is null)
        {
            return;
        }

        var pointerX = (int)Math.Round(pointerPosition.Point.X, MidpointRounding.AwayFromZero);
        var pointerY = (int)Math.Round(pointerPosition.Point.Y, MidpointRounding.AwayFromZero);

        // Convert the stored hotspot/window offsets (in logical ScreenSpace DIPs) to
        // physical pixels for the monitor under the pointer using the overlay mapper.
        Debug.Assert(this.spatial is not null, "Overlay mapper should be initialized by StartSession and must not be null here.");

        var phys = this.spatial!.Convert<ScreenSpace, PhysicalScreenSpace>(this.windowPositionOffsets).Point;
        var offsetPhysical = phys;

        var offsetXi = (int)Math.Round(offsetPhysical.X, MidpointRounding.AwayFromZero);
        var offsetYi = (int)Math.Round(offsetPhysical.Y, MidpointRounding.AwayFromZero);

        var position = new PointInt32(pointerX - offsetXi, pointerY - offsetYi);
        this.overlayWindow.Move(position);
    }

    private SizeInt32 ToPixelSize(WinSize requestedSize)
    {
        var widthDip = requestedSize.Width > 0 ? requestedSize.Width : DefaultWidthDip;
        var heightDip = requestedSize.Height > 0 ? requestedSize.Height : DefaultHeightDip;

        Debug.Assert(this.spatial is not null, "Overlay mapper should be initialized by StartSession and must not be null here.");

        var topLeft = new SpatialPoint<ScreenSpace>(new WinPoint(0, 0));
        var bottomRight = new SpatialPoint<ScreenSpace>(new WinPoint(widthDip, heightDip));
        var physTopLeft = this.spatial!.Convert<ScreenSpace, PhysicalScreenSpace>(topLeft).Point;
        var physBottomRight = this.spatial!.Convert<ScreenSpace, PhysicalScreenSpace>(bottomRight).Point;

        var pixelWidth = Math.Max(1, (int)Math.Round(Math.Abs(physBottomRight.X - physTopLeft.X), MidpointRounding.AwayFromZero));
        var pixelHeight = Math.Max(1, (int)Math.Round(Math.Abs(physBottomRight.Y - physTopLeft.Y), MidpointRounding.AwayFromZero));

        return new SizeInt32(pixelWidth, pixelHeight);
    }

    private int DipToPixels(double dips)
    {
        Debug.Assert(this.spatial is not null, "Overlay mapper should be initialized by StartSession and must not be null here.");

        var sp = new SpatialPoint<ScreenSpace>(new WinPoint(dips, 0));
        var phys = this.spatial!.Convert<ScreenSpace, PhysicalScreenSpace>(sp).Point;
        return (int)Math.Round(phys.X, MidpointRounding.AwayFromZero);
    }

    private static NativeImage? CreateNativeImage(SoftwareBitmap? bitmap)
    {
        if (bitmap is null)
        {
            return null;
        }

        try
        {
            using var normalized = SoftwareBitmap.Convert(bitmap, BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied);
            var pixelCount = normalized.PixelWidth * normalized.PixelHeight * 4;
            var pixels = new byte[pixelCount];
            normalized.CopyToBuffer(pixels.AsBuffer());
            return new NativeImage(pixels, normalized.PixelWidth, normalized.PixelHeight);
        }
        catch (Exception)
        {
            return null;
        }
    }

    private void DestroyOverlayWindow()
    {
        if (this.overlayWindow is null)
        {
            return;
        }

        this.overlayWindow.Dispose();
        this.overlayWindow = null;

        // Drop the overlay mapper when the overlay window is destroyed.
        this.spatial = null;
    }

    private void ThrowIfDisposed()
    {
        if (this.disposed)
        {
            throw new ObjectDisposedException(nameof(DragVisualService));
        }
    }

    /// <summary>
    ///     Dispose pattern implementation. Disposes managed resources when <paramref name="disposing"/> is true.
    /// </summary>
    /// <param name="disposing">True when called from Dispose(), false when called from a finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            // dispose managed resources
            lock (this.syncLock)
            {
                if (this.activeDescriptor is not null)
                {
                    this.activeDescriptor.PropertyChanged -= this.OnDescriptorPropertyChanged;
                    this.activeDescriptor.HeaderBitmap = null;
                    this.activeDescriptor.PreviewBitmap = null;
                }

                this.DestroyOverlayWindow();

                this.windowIsShown = false;
                this.activeDescriptor = null;
                this.activeToken = null;
                this.windowPositionOffsets = default;
                this.lastPointerPosition = default;
                this.spatial = null;
            }
        }

        this.disposed = true;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(true);
        GC.SuppressFinalize(this);
    }

    private void OnDescriptorPropertyChanged(object? sender, PropertyChangedEventArgs e) =>
        _ = this.dispatcherQueue.TryEnqueue(() =>
        {
            lock (this.syncLock)
            {
                if (sender is not DragVisualDescriptor descriptor || !ReferenceEquals(this.activeDescriptor, descriptor))
                {
                    return;
                }

                var propertyName = e.PropertyName;

                switch (propertyName)
                {
                    case nameof(DragVisualDescriptor.HeaderBitmap):
                    case nameof(DragVisualDescriptor.PreviewBitmap):
                        this.LogDescriptorPropertyChanged(propertyName ?? "Unknown", "update layered content");
                        this.RefreshOverlayVisual();
                        break;

                    case nameof(DragVisualDescriptor.RequestedSize):
                        this.LogDescriptorPropertyChanged(propertyName ?? "Unknown", "resize layered window");
                        this.RefreshOverlayVisual();
                        break;
                }
            }
        });

    private readonly record struct NativeImage(byte[] Pixels, int Width, int Height);

    private sealed class NativeDragOverlayWindow : IDisposable
    {
        private readonly DragVisualService owner;
        private readonly nint hwnd;
        private PointInt32 currentPosition;
        private SizeInt32 currentSize;

        internal nint Hwnd => this.hwnd;

        internal NativeDragOverlayWindow(DragVisualService owner)
        {
            this.owner = owner;
            NativeWindowClass.EnsureRegistered();
            this.hwnd = this.CreateWindow();
            this.currentPosition = new PointInt32(0, 0);
            this.currentSize = new SizeInt32(1, 1);
            this.owner.LogLayeredWindowCreated();
        }

        internal PointInt32 CurrentPosition => this.currentPosition;

        public void Show()
        {
            const int ShowNoActivate = 4;
            _ = NativeMethods.ShowWindow(this.hwnd, ShowNoActivate);
            _ = NativeMethods.SetWindowPos(
                this.hwnd,
                NativeMethods.HwndTopMost,
                this.currentPosition.X,
                this.currentPosition.Y,
                this.currentSize.Width,
                this.currentSize.Height,
                NativeMethods.SwpNoActivate | NativeMethods.SwpShowWindow);
        }

        public void Move(PointInt32 position)
        {
            this.currentPosition = position;
            _ = NativeMethods.SetWindowPos(
                this.hwnd,
                NativeMethods.HwndTopMost,
                position.X,
                position.Y,
                0,
                0,
                NativeMethods.SwpNoSize | NativeMethods.SwpNoActivate | NativeMethods.SwpNoOwnerZOrder);
        }

        public void SetSize(SizeInt32 size)
        {
            if (size.Width <= 0 || size.Height <= 0)
            {
                size = new SizeInt32(1, 1);
            }

            if (size.Equals(this.currentSize))
            {
                return;
            }

            this.currentSize = size;
            _ = NativeMethods.SetWindowPos(
                this.hwnd,
                NativeMethods.HwndTopMost,
                0,
                0,
                size.Width,
                size.Height,
                NativeMethods.SwpNoMove | NativeMethods.SwpNoActivate | NativeMethods.SwpNoOwnerZOrder);
        }

        public void UpdateContent(NativeImage? header, NativeImage? preview, SizeInt32 windowSize, int spacingPx)
        {
            using var surface = new Bitmap(windowSize.Width, windowSize.Height, PixelFormat.Format32bppPArgb);

            using (var graphics = Graphics.FromImage(surface))
            {
                graphics.Clear(Color.Transparent);
                graphics.CompositingMode = CompositingMode.SourceOver;
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                graphics.SmoothingMode = SmoothingMode.HighQuality;

                var y = 0;

                if (header.HasValue)
                {
                    using var headerBitmap = CreateBitmapFromNativeImage(header.Value);
                    var headerHeight = Math.Min(header.Value.Height, windowSize.Height);
                    var headerWidth = Math.Min(header.Value.Width, windowSize.Width);
                    var destRect = new Rectangle(0, 0, headerWidth, headerHeight);
                    graphics.DrawImage(headerBitmap, destRect);
                    y = destRect.Bottom;
                }

                if (preview.HasValue && y < windowSize.Height)
                {
                    if (header.HasValue && spacingPx > 0)
                    {
                        y = Math.Min(windowSize.Height, y + spacingPx);
                    }

                    var availableHeight = windowSize.Height - y;
                    if (availableHeight > 0 && windowSize.Width > 0)
                    {
                        using var previewBitmap = CreateBitmapFromNativeImage(preview.Value);
                        var scale = Math.Min(
                            windowSize.Width / (double)preview.Value.Width,
                            availableHeight / (double)preview.Value.Height);

                        var destWidth = Math.Max(1, (int)Math.Round(preview.Value.Width * scale, MidpointRounding.AwayFromZero));
                        var destHeight = Math.Max(1, (int)Math.Round(preview.Value.Height * scale, MidpointRounding.AwayFromZero));
                        var destRect = new Rectangle(0, y, destWidth, destHeight);
                        graphics.DrawImage(previewBitmap, destRect);
                    }
                }
            }

            using var gdi = new GdiBitmap(this.owner, surface);
            gdi.Commit(this.hwnd, windowSize, this.currentPosition);
        }

        public void Dispose()
        {
            if (this.hwnd != IntPtr.Zero)
            {
                _ = NativeMethods.DestroyWindow(this.hwnd);
                this.owner.LogLayeredWindowDestroyed();
            }
        }

        private nint CreateWindow()
        {
            var exStyle = NativeMethods.WsExLayered | NativeMethods.WsExTransparent | NativeMethods.WsExToolWindow | NativeMethods.WsExNoActivate;
            var hwnd = NativeMethods.CreateWindowEx(
                exStyle,
                NativeWindowClass.ClassName,
                string.Empty,
                NativeMethods.WsPopup,
                0,
                0,
                1,
                1,
                IntPtr.Zero,
                IntPtr.Zero,
                NativeWindowClass.ModuleHandle,
                IntPtr.Zero);

            if (hwnd == IntPtr.Zero)
            {
                var error = Marshal.GetLastWin32Error();
                this.owner.LogCreateLayeredWindowFailed(error);
                throw new InvalidOperationException("Failed to create drag overlay window.");
            }

            return hwnd;
        }

        private static Bitmap CreateBitmapFromNativeImage(NativeImage image)
        {
            var bitmap = new Bitmap(image.Width, image.Height, PixelFormat.Format32bppPArgb);
            var rect = new Rectangle(0, 0, image.Width, image.Height);
            var data = bitmap.LockBits(rect, ImageLockMode.WriteOnly, bitmap.PixelFormat);

            try
            {
                Marshal.Copy(image.Pixels, 0, data.Scan0, image.Pixels.Length);
            }
            finally
            {
                bitmap.UnlockBits(data);
            }

            return bitmap;
        }

        private sealed class GdiBitmap : IDisposable
        {
            private readonly DragVisualService owner;
            private readonly nint hBitmap;

            internal GdiBitmap(DragVisualService owner, Bitmap surface)
            {
                this.owner = owner;
                this.hBitmap = surface.GetHbitmap(Color.FromArgb(0));
                if (this.hBitmap == IntPtr.Zero)
                {
                    this.owner.LogCreateDibSectionFailed();
                    throw new InvalidOperationException("Failed to create DIB section.");
                }
            }

            public void Commit(nint hwnd, SizeInt32 size, PointInt32 position)
            {
                var memoryDc = NativeMethods.CreateCompatibleDC(IntPtr.Zero);
                if (memoryDc == IntPtr.Zero)
                {
                    this.owner.LogCreateCompatibleDcFailed();
                    return;
                }

                try
                {
                    var previous = NativeMethods.SelectObject(memoryDc, this.hBitmap);
                    try
                    {
                        var dest = new NativeMethods.Point(position.X, position.Y);
                        var src = new NativeMethods.Point(0, 0);
                        var winSize = new NativeMethods.Size(size.Width, size.Height);
                        var blend = new NativeMethods.BlendFunction
                        {
                            BlendOp = NativeMethods.AcSrcOver,
                            BlendFlags = 0,
                            SourceConstantAlpha = 255,
                            AlphaFormat = NativeMethods.AcSrcAlpha,
                        };

                        if (!NativeMethods.UpdateLayeredWindow(hwnd, IntPtr.Zero, ref dest, ref winSize, memoryDc, ref src, 0, ref blend, NativeMethods.UlwAlpha))
                        {
                            var error = Marshal.GetLastWin32Error();
                            this.owner.LogCreateLayeredWindowFailed(error);
                        }
                        else
                        {
                            this.owner.LogDibSectionCreated();
                        }
                    }
                    finally
                    {
                        _ = NativeMethods.SelectObject(memoryDc, previous);
                    }
                }
                finally
                {
                    NativeMethods.DeleteDC(memoryDc);
                }
            }

            public void Dispose()
            {
                if (this.hBitmap != IntPtr.Zero)
                {
                    _ = NativeMethods.DeleteObject(this.hBitmap);
                }
            }
        }

        private static class NativeWindowClass
        {
            internal const string ClassName = "DroidNet_Aura_DragOverlay";
            private static ushort atom;
            private static readonly NativeMethods.WindowProcedure WindowProcedureDelegate = WindowProc;
            private static readonly nint WindowProcedurePointer = Marshal.GetFunctionPointerForDelegate(WindowProcedureDelegate);

            internal static nint ModuleHandle => NativeMethods.GetModuleHandle(null);

            internal static void EnsureRegistered()
            {
                if (atom != 0)
                {
                    return;
                }

                var wndClass = new NativeMethods.WndClassEx
                {
                    Size = (uint)Marshal.SizeOf<NativeMethods.WndClassEx>(),
                    Style = 0,
                    WindowProcedure = WindowProcedurePointer,
                    ClassExtraBytes = 0,
                    WindowExtraBytes = 0,
                    InstanceHandle = ModuleHandle,
                    CursorHandle = NativeMethods.LoadCursor(IntPtr.Zero, NativeMethods.IdcArrow),
                    BackgroundBrush = IntPtr.Zero,
                    MenuName = IntPtr.Zero,
                };

                var classNameHandle = Marshal.StringToHGlobalUni(ClassName);
                try
                {
                    wndClass.ClassName = classNameHandle;
                    atom = NativeMethods.RegisterClassEx(ref wndClass);
                }
                finally
                {
                    if (classNameHandle != IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(classNameHandle);
                    }
                }

                if (atom == 0)
                {
                    throw new InvalidOperationException("Failed to register drag overlay window class.");
                }
            }

            private static nint WindowProc(nint hwnd, uint msg, nint wParam, nint lParam) =>
                NativeMethods.DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }

    private static partial class NativeMethods
    {
        private const string User32 = "user32.dll";
        private const string Kernel32 = "kernel32.dll";
        private const string Gdi32 = "gdi32.dll";

        internal const int WsPopup = unchecked((int)0x8000_0000);
        internal const int WsExLayered = 0x0008_0000;
        internal const int WsExTransparent = 0x0000_0020;
        internal const int WsExToolWindow = 0x0000_0080;
        internal const int WsExNoActivate = 0x0800_0000;

        internal const uint SwpNoSize = 0x0001;
        internal const uint SwpNoMove = 0x0002;
        internal const uint SwpNoActivate = 0x0010;
        internal const uint SwpShowWindow = 0x0040;
        internal const uint SwpNoOwnerZOrder = 0x0200;

        internal const uint UlwAlpha = 0x0000_0002;
        internal const byte AcSrcOver = 0x00;
        internal const byte AcSrcAlpha = 0x01;

        internal const int MonitorDefaultToNearest = 0x0000_0002;

        internal static readonly nint HwndTopMost = new(-1);
        internal static readonly nint IdcArrow = new(32512);

        internal delegate nint WindowProcedure(nint windowHandle, uint message, nint wParam, nint lParam);

        [StructLayout(LayoutKind.Sequential)]
        internal readonly struct Point
        {
            internal readonly int X;
            internal readonly int Y;

            internal Point(int x, int y)
            {
                this.X = x;
                this.Y = y;
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        internal readonly struct Size
        {
            internal readonly int Width;
            internal readonly int Height;

            internal Size(int width, int height)
            {
                this.Width = width;
                this.Height = height;
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct WndClassEx
        {
            internal uint Size;
            internal uint Style;
            internal nint WindowProcedure;
            internal int ClassExtraBytes;
            internal int WindowExtraBytes;
            internal nint InstanceHandle;
            internal nint IconHandle;
            internal nint CursorHandle;
            internal nint BackgroundBrush;
            internal nint MenuName;
            internal nint ClassName;
            internal nint SmallIconHandle;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct BlendFunction
        {
            internal byte BlendOp;
            internal byte BlendFlags;
            internal byte SourceConstantAlpha;
            internal byte AlphaFormat;
        }

        [LibraryImport(User32, SetLastError = true, EntryPoint = "CreateWindowExW", StringMarshalling = StringMarshalling.Utf16)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [System.Diagnostics.CodeAnalysis.SuppressMessage("Interoperability", "SYSLIB1051:Types used with LibraryImport must be blittable", Justification = "Struct is manually marshalled and uses raw pointers for strings.")]
        internal static partial nint CreateWindowEx(
            int extendedStyle,
            string className,
            string? windowName,
            int style,
            int x,
            int y,
            int width,
            int height,
            nint parentHandle,
            nint menuHandle,
            nint instanceHandle,
            nint param);

        [LibraryImport(User32, SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static partial bool DestroyWindow(nint windowHandle);

        [LibraryImport(User32, SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static partial bool ShowWindow(nint windowHandle, int commandShow);

        [LibraryImport(User32, SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static partial bool SetWindowPos(
            nint windowHandle,
            nint insertAfter,
            int x,
            int y,
            int width,
            int height,
            uint flags);

        [LibraryImport(User32, SetLastError = true, EntryPoint = "RegisterClassExW")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static unsafe partial ushort RegisterClassEx(ref WndClassEx classEx);

        [LibraryImport(User32, EntryPoint = "DefWindowProcW")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static partial nint DefWindowProc(nint windowHandle, uint message, nint wParam, nint lParam);

        [LibraryImport(User32, EntryPoint = "LoadCursorW")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static partial nint LoadCursor(nint instanceHandle, nint cursorName);

        [LibraryImport(Kernel32, EntryPoint = "GetModuleHandleW", StringMarshalling = StringMarshalling.Utf16)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static partial nint GetModuleHandle(string? moduleName);

        [LibraryImport(Gdi32, SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static partial nint CreateCompatibleDC(nint hdc);

        [LibraryImport(Gdi32, SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static partial bool DeleteDC(nint hdc);

        [LibraryImport(Gdi32, SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static partial nint SelectObject(nint hdc, nint handle);

        [LibraryImport(Gdi32, SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static partial bool DeleteObject(nint handle);

        [LibraryImport(User32, SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static partial bool UpdateLayeredWindow(
            nint windowHandle,
            nint destinationDc,
            ref Point destinationPoint,
            ref Size windowSize,
            nint sourceDc,
            ref Point sourcePoint,
            int colorKey,
            ref BlendFunction blend,
            uint flags);
    }
}
