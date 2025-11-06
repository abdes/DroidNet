// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using WinRT.Interop;

namespace DroidNet.Aura.Drag;

/// <summary>
///     A frameless, transparent overlay window used for drag visual feedback.
/// </summary>
/// <remarks>
///     This window is configured as a frameless, non-activating overlay that displays drag visual content.
///     It is managed entirely by <see cref="DragVisualService"/> and should not be created or manipulated directly.
///     The window's DataContext should be set to a <see cref="DragVisualDescriptor"/> for data binding.
/// </remarks>
public partial class DragOverlayWindow : Window
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="DragOverlayWindow"/> class.
    /// </summary>
    public DragOverlayWindow()
    {
        this.InitializeComponent();

        // Configure the AppWindow for overlay behavior
        this.AppWindow.IsShownInSwitchers = false;
        this.AppWindow.TitleBar.ExtendsContentIntoTitleBar = true;
        this.ExtendsContentIntoTitleBar = true;

        // Set window as non-activating overlay
        this.SystemBackdrop = null; // No backdrop - fully transparent
    }

    /// <summary>
    ///     Shows the window without activating it (doesn't steal focus).
    /// </summary>
    public virtual void ShowNoActivate()
    {
        const int GWL_EXSTYLE = -20;
        const nint WS_EX_NOACTIVATE = 0x08000000;
        const int SW_SHOWNOACTIVATE = 4;
        const uint SWP_NOMOVE = 0x0002;
        const uint SWP_NOSIZE = 0x0001;
        const uint SWP_NOACTIVATE = 0x0010;
        var hwndTopmost = new nint(-1);

        var hwnd = WindowNative.GetWindowHandle(this);

        // Get current extended window style
        var exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

        // Add WS_EX_NOACTIVATE to prevent activation
        _ = SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_NOACTIVATE);

        // Show window without activating it
        _ = ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        // Set as topmost
        _ = SetWindowPos(
            hwnd,
            hwndTopmost,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    /// <summary>
    ///     Updates the window size to match the requested dimensions.
    /// </summary>
    /// <param name="size">The requested size in DIPs.</param>
    public virtual void SetSize(Windows.Foundation.Size size)
    {
        if (size.Width > 0 && size.Height > 0)
        {
            this.AppWindow.Resize(new Windows.Graphics.SizeInt32(
                (int)Math.Round(size.Width),
                (int)Math.Round(size.Height)));
        }
    }

    /// <summary>
    ///     Moves the window to the specified position.
    /// </summary>
    /// <param name="position">The position in physical screen coordinates.</param>
    public virtual void MoveWindow(Windows.Graphics.PointInt32 position) => this.AppWindow.Move(position);

    // Win32 P/Invoke methods
    [LibraryImport("user32.dll", EntryPoint = "GetWindowLongPtrW", SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial nint GetWindowLongPtr(nint hWnd, int nIndex);

    [LibraryImport("user32.dll", EntryPoint = "SetWindowLongPtrW", SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial nint SetWindowLongPtr(nint hWnd, int nIndex, nint dwNewLong);

    [LibraryImport("user32.dll", SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool ShowWindow(nint hWnd, int nCmdShow);

    [LibraryImport("user32.dll", SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetWindowPos(
        nint hWnd,
        nint hWndInsertAfter,
        int x,
        int y,
        int cx,
        int cy,
        uint uFlags);
}
