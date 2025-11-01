// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Runtime.InteropServices;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;

namespace DroidNet.Controls;

/// <summary>
///     Implementation of <see cref="IDragVisualService"/> using a Win32 layered window for the drag
///     overlay. The overlay is topmost, non-activating, click-through, and survives source
///     AppWindow closure.
/// </summary>
public partial class DragVisualService : IDragVisualService
{
    private readonly ILogger logger;

    private readonly Lock syncLock = new();
    private readonly DispatcherQueue dispatcherQueue;

    private DragSessionToken? activeToken;
    private DragVisualDescriptor? activeDescriptor;
    private IntPtr overlayWindow;
    private IntPtr overlayDC;
    private IntPtr overlayBitmap;
    private IntPtr overlayBits;
    private int overlayWidth;
    private int overlayHeight;
    private Windows.Foundation.Point hotspot;

    /// <summary>
    /// Initializes a new instance of the <see cref="DragVisualService"/> class.
    /// </summary>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public DragVisualService(ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<DragVisualService>() ?? NullLogger<DragVisualService>.Instance;
        this.dispatcherQueue = DispatcherQueue.GetForCurrentThread()
            ?? throw new InvalidOperationException("DragVisualService must be created on a thread with a DispatcherQueue.");
        this.LogCreated();
    }

    /// <inheritdoc/>
    public DragSessionToken StartSession(DragVisualDescriptor descriptor, Windows.Foundation.Point hotspot)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (this.activeToken.HasValue)
            {
                throw new InvalidOperationException("A drag visual session is already active.");
            }

            var token = new DragSessionToken { Id = Guid.NewGuid() };
            this.activeToken = token;
            this.activeDescriptor = descriptor;
            this.hotspot = hotspot;

            // Subscribe to descriptor changes
            this.activeDescriptor.PropertyChanged += this.OnDescriptorPropertyChanged;

            // Create the layered overlay window
            this.CreateLayeredWindow();

            return token;
        }
    }

    /// <inheritdoc/>
    public void UpdatePosition(DragSessionToken token, Windows.Foundation.Point screenPoint)
    {
        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (!this.activeToken.HasValue || this.activeToken.Value != token)
            {
                return;
            }

            if (this.overlayWindow == IntPtr.Zero)
            {
                return;
            }

            // Get DPI for the screen point
            var dpi = Native.GetDpiForPoint(screenPoint);

            // Calculate window position (apply hotspot offset)
            var windowX = (int)(screenPoint.X - this.hotspot.X);
            var windowY = (int)(screenPoint.Y - this.hotspot.Y);

            // Convert to physical coordinates
            var physicalPos = Native.LogicalToPhysicalPoint(
                new Windows.Foundation.Point(windowX, windowY),
                dpi);

            // Update window position
            _ = Native.SetWindowPos(
                this.overlayWindow,
                Native.HWND_TOPMOST,
                physicalPos.X,
                physicalPos.Y,
                0,
                0,
                Native.SetWindowPosFlags.SWP_NOSIZE | Native.SetWindowPosFlags.SWP_NOACTIVATE | Native.SetWindowPosFlags.SWP_SHOWWINDOW);
        }
    }

    /// <inheritdoc/>
    public void EndSession(DragSessionToken token)
    {
        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (!this.activeToken.HasValue || this.activeToken.Value != token)
            {
                return;
            }

            if (this.activeDescriptor is not null)
            {
                this.activeDescriptor.PropertyChanged -= this.OnDescriptorPropertyChanged;
            }

            this.DestroyLayeredWindow();

            this.activeDescriptor = null;
            this.activeToken = null;
            this.hotspot = default;
        }
    }

    /// <inheritdoc/>
    public DragVisualDescriptor? GetDescriptor(DragSessionToken token)
    {
        lock (this.syncLock)
        {
            if (!this.activeToken.HasValue || this.activeToken.Value != token)
            {
                return null;
            }

            return this.activeDescriptor;
        }
    }

    private static uint PremultiplyAlpha(uint argb)
    {
        var a = (argb >> 24) & 0xFF;
        var r = (argb >> 16) & 0xFF;
        var g = (argb >> 8) & 0xFF;
        var b = argb & 0xFF;

        // Premultiply RGB by alpha
        r = r * a / 255;
        g = g * a / 255;
        b = b * a / 255;

        // Return as BGRA (Windows DIB format)
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    private void AssertUIThread()
    {
        if (!this.dispatcherQueue.HasThreadAccess)
        {
            throw new InvalidOperationException("IDragVisualService methods must be called from the UI thread.");
        }
    }

    private void CreateLayeredWindow()
    {
        // Destroy existing window if any
        this.DestroyLayeredWindow();

        // Get initial size from descriptor
        var requestedSize = this.activeDescriptor?.RequestedSize ?? new Windows.Foundation.Size(200, 100);
        if (requestedSize.Width <= 0 || requestedSize.Height <= 0)
        {
            requestedSize = new Windows.Foundation.Size(200, 100);
        }

        // Use default DPI for initial creation
        const uint defaultDpi = 96;
        var physicalSize = Native.LogicalToPhysicalSize(requestedSize, defaultDpi);

        this.overlayWidth = Math.Max(1, physicalSize.Width);
        this.overlayHeight = Math.Max(1, physicalSize.Height);

        // Create layered window
        this.overlayWindow = Native.CreateWindowExW(
            Native.WindowStylesEx.WS_EX_LAYERED
            | Native.WindowStylesEx.WS_EX_TRANSPARENT
            | Native.WindowStylesEx.WS_EX_TOOLWINDOW
            | Native.WindowStylesEx.WS_EX_TOPMOST
            | Native.WindowStylesEx.WS_EX_NOACTIVATE,
            "Static",
            "DragOverlay",
            Native.WindowStyles.WS_POPUP,
            0,
            0,
            this.overlayWidth,
            this.overlayHeight,
            nint.Zero,
            nint.Zero,
            nint.Zero,
            nint.Zero);

        if (this.overlayWindow == IntPtr.Zero)
        {
            throw new InvalidOperationException($"Failed to create layered window. Error: {Marshal.GetLastWin32Error()}");
        }

        // Create DIB section for rendering
        this.CreateDIBSection();

        // Render initial content
        this.RenderOverlayContent();

        // Show the window
        _ = Native.ShowWindow(this.overlayWindow, Native.ShowWindowCommands.SW_SHOWNOACTIVATE);
    }

    private void CreateDIBSection()
    {
        // Clean up existing resources
        if (this.overlayBitmap != IntPtr.Zero)
        {
            _ = Native.DeleteObject(this.overlayBitmap);
            this.overlayBitmap = IntPtr.Zero;
            this.overlayBits = IntPtr.Zero;
        }

        if (this.overlayDC != IntPtr.Zero)
        {
            _ = Native.DeleteDC(this.overlayDC);
            this.overlayDC = IntPtr.Zero;
        }

        // Create memory DC
        var screenDC = Native.GetDC(nint.Zero);
        this.overlayDC = Native.CreateCompatibleDC(screenDC);
        _ = Native.ReleaseDC(nint.Zero, screenDC);

        if (this.overlayDC == IntPtr.Zero)
        {
            throw new InvalidOperationException("Failed to create compatible DC.");
        }

        // Create DIB section with 32-bit BGRA.
        // Note: biHeight is set to negative value to create a top-down DIB, which simplifies pixel access:
        // - Pixel (0,0) is at the top-left, matching screen coordinates.
        // - Pixel rows are stored from top to bottom in memory.
        // - The unsafe pixel iteration in RenderPlaceholderContent relies on this layout.
        var bmi = new Native.BITMAPINFO
        {
            bmiHeader = new Native.BITMAPINFOHEADER
            {
                biSize = (uint)Marshal.SizeOf<Native.BITMAPINFOHEADER>(),
                biWidth = this.overlayWidth,
                biHeight = -this.overlayHeight,
                biPlanes = 1,
                biBitCount = 32,
                biCompression = Native.BI_RGB,
            },
            bmiColors = [0],
        };

        this.overlayBitmap = Native.CreateDIBSection(
            this.overlayDC,
            ref bmi,
            (uint)Native.DIB_RGB_COLORS,
            out this.overlayBits,
            nint.Zero,
            0U);

        if (this.overlayBitmap == IntPtr.Zero)
        {
            throw new InvalidOperationException("Failed to create DIB section.");
        }

        _ = Native.SelectObject(this.overlayDC, this.overlayBitmap);
    }

    private void RenderOverlayContent()
    {
        if (this.overlayBits == IntPtr.Zero || this.activeDescriptor is null)
        {
            return;
        }

        // Clear to transparent
        var pixelCount = this.overlayWidth * this.overlayHeight;
        unsafe
        {
            var pixels = (uint*)this.overlayBits;
            for (var i = 0; i < pixelCount; i++)
            {
                pixels[i] = 0; // Transparent black
            }
        }

        // Render header image and preview image if available.
        // PHASE 2 NOTE: Currently renders a placeholder blue rectangle with border.
        // Full ImageSource → BGRA decoding and composition is deferred to Phase 3.
        // This placeholder allows drag visualization and hotspot testing in Phase 2.
        this.RenderPlaceholderContent();

        // Update the layered window with the new bitmap
        this.UpdateLayeredWindowContent();
    }

    private void RenderPlaceholderContent()
    {
        // Render a simple semi-transparent blue rectangle as a placeholder
        // In production, this would composite the actual HeaderImage and PreviewImage
        var bgColor = 0x80_00_80_FF; // Semi-transparent blue (ARGB)
        var pixelCount = this.overlayWidth * this.overlayHeight;

        unsafe
        {
            var pixels = (uint*)this.overlayBits;

            // Draw a rounded rectangle background
            for (var y = 0; y < this.overlayHeight; y++)
            {
                for (var x = 0; x < this.overlayWidth; x++)
                {
                    var idx = (y * this.overlayWidth) + x;

                    // Simple border detection
                    var isBorder = x < 2 || x >= this.overlayWidth - 2 || y < 2 || y >= this.overlayHeight - 2;

                    if (isBorder)
                    {
                        pixels[idx] = 0xFF_00_00_00; // Opaque black border
                    }
                    else
                    {
                        pixels[idx] = PremultiplyAlpha(bgColor);
                    }
                }
            }
        }
    }

    private void UpdateLayeredWindowContent()
    {
        if (this.overlayWindow == IntPtr.Zero || this.overlayDC == IntPtr.Zero)
        {
            return;
        }

        var size = new Native.SIZE(this.overlayWidth, this.overlayHeight);
        var ptSrc = new Native.POINT(0, 0);

        // NOTE: ptDst is set to (0, 0) because window position is managed separately via SetWindowPos() in UpdatePosition().
        // UpdateLayeredWindow() here only updates the bitmap content on the current window bounds;
        // it does not affect window positioning. The hotspot-adjusted screen coordinates are applied via SetWindowPos.
        var ptDst = new Native.POINT(0, 0);

        var blend = new Native.BLENDFUNCTION
        {
            BlendOp = Native.BLENDFUNCTION.AC_SRC_OVER,
            BlendFlags = 0,
            SourceConstantAlpha = 255,
            AlphaFormat = Native.BLENDFUNCTION.AC_SRC_ALPHA,
        };

        _ = Native.UpdateLayeredWindow(
            this.overlayWindow,
            nint.Zero,
            in ptDst,
            in size,
            this.overlayDC,
            in ptSrc,
            0,
            in blend,
            Native.UpdateLayeredWindowFlags.ULW_ALPHA);
    }

    private void DestroyLayeredWindow()
    {
        if (this.overlayBitmap != IntPtr.Zero)
        {
            _ = Native.DeleteObject(this.overlayBitmap);
            this.overlayBitmap = IntPtr.Zero;
            this.overlayBits = IntPtr.Zero;
        }

        if (this.overlayDC != IntPtr.Zero)
        {
            _ = Native.DeleteDC(this.overlayDC);
            this.overlayDC = IntPtr.Zero;
        }

        if (this.overlayWindow != IntPtr.Zero)
        {
            _ = Native.DestroyWindow(this.overlayWindow);
            this.overlayWindow = IntPtr.Zero;
        }

        this.overlayWidth = 0;
        this.overlayHeight = 0;
    }

    private void OnDescriptorPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        // Queue render update on UI thread
        _ = this.dispatcherQueue.TryEnqueue(() =>
        {
            lock (this.syncLock)
            {
                // Validate descriptor reference inside lock to ensure consistency
                if (sender is not DragVisualDescriptor descriptor || this.activeDescriptor != descriptor)
                {
                    return;
                }

                // Re-render content when descriptor properties change
                switch (e.PropertyName)
                {
                    case var name when string.Equals(name, nameof(DragVisualDescriptor.HeaderImage), StringComparison.Ordinal)
                        || string.Equals(name, nameof(DragVisualDescriptor.PreviewImage), StringComparison.Ordinal)
                        || string.Equals(name, nameof(DragVisualDescriptor.Title), StringComparison.Ordinal):
                        this.RenderOverlayContent();
                        break;
                    case var name when string.Equals(name, nameof(DragVisualDescriptor.RequestedSize), StringComparison.Ordinal):
                        // Size change requires recreating the window
                        var token = this.activeToken;
                        if (token.HasValue)
                        {
                            this.CreateLayeredWindow();
                        }

                        break;
                }
            }
        });
    }
}
