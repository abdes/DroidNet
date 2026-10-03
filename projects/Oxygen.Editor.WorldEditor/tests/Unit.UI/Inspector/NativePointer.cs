// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.InteropServices;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

/// <summary>Injects actual input exclusively into the foreground window owned by this test process.</summary>
internal sealed partial class NativePointer : IDisposable
{
    private readonly nint window;
    private readonly NativePoint originalPosition;
    private readonly double scale;
    private readonly FrameworkElement target;
    private NativePoint position;
    private bool pressed;

    private NativePointer(FrameworkElement target)
    {
        this.window = WinRT.Interop.WindowNative.GetWindowHandle(VisualUserInterfaceTestsApp.MainWindow);
        _ = GetWindowThreadProcessId(this.window, out var processId);
        if (processId != Environment.ProcessId || !GetCursorPos(out this.originalPosition))
        {
            throw new InvalidOperationException("Native input requires the current process's test window and an available desktop cursor.");
        }

        this.scale = target.XamlRoot.RasterizationScale;
        this.target = target;
        this.position = this.GetTargetPosition();
    }

    private NativePoint GetTargetPosition()
    {
        var target = this.target;
        var center = target.TransformToVisual(target.XamlRoot.Content).TransformPoint(new Point(target.ActualWidth / 2, target.ActualHeight / 2));
        if (!target.IsLoaded || target.ActualWidth <= 0 || target.ActualHeight <= 0
            || !VisualTreeHelper.FindElementsInHostCoordinates(center, target.XamlRoot.Content).Contains(target))
        {
            throw new InvalidOperationException($"The native input target is not visible and hit-testable: loaded={target.IsLoaded}, size={target.ActualWidth}x{target.ActualHeight}, center={center}, root={target.XamlRoot.Content.RenderSize}.");
        }

        var point = new NativePoint { X = (int)Math.Round(center.X * this.scale), Y = (int)Math.Round(center.Y * this.scale) };
        if (!ClientToScreen(this.window, ref point))
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }

        return point;
    }

    public static async Task<NativePointer> PressAsync(FrameworkElement target, CancellationToken cancellationToken)
    {
        var pointer = new NativePointer(target);
        try
        {
            VisualUserInterfaceTestsApp.MainWindow.Activate();
            _ = SetForegroundWindow(pointer.window);
            await Task.Delay(80, cancellationToken).ConfigureAwait(true);
            pointer.CheckForeground();
            await pointer.PositionOverTargetAsync(cancellationToken).ConfigureAwait(true);
            _ = GetWindowThreadProcessId(WindowFromPoint(pointer.position), out var pointerWindowProcess);
            if (pointerWindowProcess != Environment.ProcessId)
            {
                throw new InvalidOperationException("Native input stopped because another process obscures the target.");
            }

            pointer.CheckForeground();
            pointer.Move(0x0002);
            pointer.pressed = true;
            await Task.Delay(80, cancellationToken).ConfigureAwait(true);
            return pointer;
        }
        catch
        {
            pointer.Dispose();
            throw;
        }
    }

    public async Task MoveAsync(double horizontalDips, CancellationToken cancellationToken)
    {
        this.CheckForeground();
        this.position.X += (int)Math.Round(horizontalDips * this.scale);
        this.Move();
        await Task.Delay(60, cancellationToken).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
    }

    public async Task ReleaseAsync(CancellationToken cancellationToken)
    {
        this.Release();
        await Task.Delay(80, cancellationToken).ConfigureAwait(true);
    }

    public async Task EscapeAsync(CancellationToken cancellationToken)
    {
        this.CheckForeground();
        Send(new NativeInput { Type = 1, Keyboard = new KeyboardInput { VirtualKey = 0x1B } });
        Send(new NativeInput { Type = 1, Keyboard = new KeyboardInput { VirtualKey = 0x1B, Flags = 2 } });
        await Task.Delay(80, cancellationToken).ConfigureAwait(true);
    }

    public void Dispose()
    {
        this.Release();
        if (!SetCursorPos(this.originalPosition.X, this.originalPosition.Y))
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }
    }

    private static void Send(NativeInput input)
    {
        if (SendInput(1, in input, Marshal.SizeOf<NativeInput>()) != 1)
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }
    }

    private async Task PositionOverTargetAsync(CancellationToken cancellationToken)
    {
        for (var attempt = 0; attempt < 8; attempt++)
        {
            this.position = this.GetTargetPosition();
            this.Move();
            await Task.Delay(60, cancellationToken).ConfigureAwait(true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var current = this.GetTargetPosition();
            if (current.X == this.position.X && current.Y == this.position.Y)
            {
                return;
            }
        }

        throw new InvalidOperationException("The native pointer target did not settle after hover and layout.");
    }

    private void Release()
    {
        if (this.pressed)
        {
            Send(new NativeInput { Mouse = new MouseInput { Flags = 0x0004 } });
            this.pressed = false;
        }
    }

    private void Move(uint buttonFlags = 0)
    {
        var left = GetSystemMetrics(76);
        var top = GetSystemMetrics(77);
        var width = GetSystemMetrics(78);
        var height = GetSystemMetrics(79);
        Send(new NativeInput
        {
            Mouse = new MouseInput
            {
                X = (int)(((long)this.position.X - left) * 65536 / width) + (65536 / (width * 2)),
                Y = (int)(((long)this.position.Y - top) * 65536 / height) + (65536 / (height * 2)),
                Flags = 0xC001 | buttonFlags,
            },
        });
    }

    private void CheckForeground()
    {
        if (GetForegroundWindow() != this.window)
        {
            _ = GetWindowThreadProcessId(GetForegroundWindow(), out var foregroundProcess);
            throw new InvalidOperationException($"Native input stopped because the owned test window is no longer foreground: expected={this.window}, actual={GetForegroundWindow()}, foregroundProcess={foregroundProcess}, testProcess={Environment.ProcessId}.");
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Explicit, Size = 40)]
    private struct NativeInput
    {
        [FieldOffset(0)] public uint Type;
        [FieldOffset(8)] public MouseInput Mouse;
        [FieldOffset(8)] public KeyboardInput Keyboard;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput
    {
        public int X;
        public int Y;
        public uint Data;
        public uint Flags;
        public uint Time;
        public nuint ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardInput
    {
        public ushort VirtualKey;
        public ushort ScanCode;
        public uint Flags;
        public uint Time;
        public nuint ExtraInfo;
    }

    [LibraryImport("user32.dll", SetLastError = true)]
    private static partial uint SendInput(uint count, in NativeInput input, int size);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool ClientToScreen(nint window, ref NativePoint point);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetCursorPos(out NativePoint point);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetCursorPos(int x, int y);

    [LibraryImport("user32.dll")]
    private static partial uint GetWindowThreadProcessId(nint window, out uint processId);

    [LibraryImport("user32.dll")]
    private static partial nint GetForegroundWindow();

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetForegroundWindow(nint window);

    [LibraryImport("user32.dll")]
    private static partial int GetSystemMetrics(int index);

    [LibraryImport("user32.dll")]
    private static partial nint WindowFromPoint(NativePoint point);
}
