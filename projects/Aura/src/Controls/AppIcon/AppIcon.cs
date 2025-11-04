// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Controls;

internal partial class AppIcon : Control
{
    public AppIcon()
    {
        this.DefaultStyleKey = typeof(AppIcon);
    }

    //[DllImport("user32.dll")]
    //static extern IntPtr GetSystemMenu(IntPtr hWnd, bool bRevert);

    //[DllImport("user32.dll")]
    //static extern bool GetCursorPos(out POINT lpPoint);

    //[DllImport("user32.dll")]
    //static extern int TrackPopupMenu(IntPtr hMenu, uint uFlags, int x, int y,
    //                                 int nReserved, IntPtr hWnd, IntPtr prcRect);

    //private void OnIconRightTapped(object sender, RightTappedRoutedEventArgs e)
    //{

    //    const uint TPM_LEFTALIGN = 0x0000;
    //    const uint TPM_RETURNCMD = 0x0100;

    //    // Usage inside your right-click handler:
    //    var window = Microsoft.UI.Xaml.Window.GetWindow(this);
    //    if (window is not null)
    //    {
    //        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(window);
    //        // now you can call GetSystemMenu/TrackPopupMenu safely
    //    }
    //    var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(Window.Current);
    //    var hMenu = GetSystemMenu(hwnd, false);

    //    GetCursorPos(out var pt);
    //    int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RETURNCMD,
    //                             pt.X, pt.Y, 0, hwnd, IntPtr.Zero);
    //    if (cmd != 0)
    //        SendMessage(hwnd, 0x112, new IntPtr(cmd), IntPtr.Zero); // WM_SYSCOMMAND}

    //    e.Handled = true;
    //}
}
