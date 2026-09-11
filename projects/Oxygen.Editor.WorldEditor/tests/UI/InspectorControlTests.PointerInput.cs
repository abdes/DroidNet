// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace Oxygen.Editor.World.Tests;

/// <summary>Delivers mouse input through Windows to the realized test controls.</summary>
public sealed partial class InspectorControlTests
{
    private static partial class PointerInput
    {
        public static async Task WaitForTargetAsync(FrameworkElement element, CancellationToken cancellationToken)
        {
            var root = VisualUserInterfaceTestsApp.ContentRoot!;
            Point? previous = null;
            var stableFrames = 0;
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeout.CancelAfter(TimeSpan.FromSeconds(5));
            while (stableFrames < 3)
            {
                timeout.Token.ThrowIfCancellationRequested();
                var center = element.TransformToVisual(root).TransformPoint(new Point(element.ActualWidth / 2, element.ActualHeight / 2));
                var hit = VisualTreeHelper.FindElementsInHostCoordinates(center, root).Contains(element);
                stableFrames = hit && previous == center ? stableFrames + 1 : 0;
                previous = center;
                if (!hit)
                {
                    element.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
                }

                await Task.Delay(20, timeout.Token).ConfigureAwait(true);
                _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            }
        }

        public static IDisposable Capture()
        {
            var window = WinRT.Interop.WindowNative.GetWindowHandle(VisualUserInterfaceTestsApp.MainWindow);
            VisualUserInterfaceTestsApp.MainWindow.Activate();
            _ = SetForegroundWindow(window);
            _ = GetForegroundWindow().Should().Be(window, "pointer tests must own the foreground window");
            _ = GetCursorPos(out var previous).Should().NotBe(0);
            return System.Reactive.Disposables.Disposable.Create(() => _ = SetCursorPos(previous.X, previous.Y));
        }

        public static async Task MoveAsync(FrameworkElement element, double x, double y)
        {
            var position = element.TransformToVisual(VisualUserInterfaceTestsApp.ContentRoot).TransformPoint(new Point(element.ActualWidth * x, element.ActualHeight * y));
            var scale = element.XamlRoot.RasterizationScale;
            var point = new ScreenPoint { X = (int)(position.X * scale), Y = (int)(position.Y * scale) };
            _ = ClientToScreen(WinRT.Interop.WindowNative.GetWindowHandle(VisualUserInterfaceTestsApp.MainWindow), ref point).Should().NotBe(0);
            var input = new NativeInput
            {
                X = (point.X - GetSystemMetrics(76)) * 65535 / (GetSystemMetrics(78) - 1),
                Y = (point.Y - GetSystemMetrics(77)) * 65535 / (GetSystemMetrics(79) - 1),
                Flags = 0xC001,
            };
            _ = SendInput(1, in input, Marshal.SizeOf<NativeInput>()).Should().Be(1);
            await SettleAsync().ConfigureAwait(true);
        }

        public static async Task ButtonAsync(bool down)
        {
            var input = new NativeInput { Flags = down ? 0x0002u : 0x0004u };
            _ = SendInput(1, in input, Marshal.SizeOf<NativeInput>()).Should().Be(1);
            await SettleAsync().ConfigureAwait(true);
        }

        public static async Task KeyAsync(ushort key)
        {
            var input = new NativeInput { Type = 1, VirtualKey = key };
            _ = SendInput(1, in input, Marshal.SizeOf<NativeInput>()).Should().Be(1);
            input.KeyboardFlags = 0x0002;
            _ = SendInput(1, in input, Marshal.SizeOf<NativeInput>()).Should().Be(1);
            await SettleAsync().ConfigureAwait(true);
        }

        private static async Task SettleAsync()
        {
            await Task.Delay(50).ConfigureAwait(true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }

        [LibraryImport("user32.dll")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial int GetCursorPos(out ScreenPoint point);

        [LibraryImport("user32.dll")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial int SetForegroundWindow(nint window);

        [LibraryImport("user32.dll")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial nint GetForegroundWindow();

        [LibraryImport("user32.dll")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial int GetSystemMetrics(int index);

        [LibraryImport("user32.dll")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial int ClientToScreen(nint window, ref ScreenPoint point);

        [LibraryImport("user32.dll")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial int SetCursorPos(int x, int y);

        [LibraryImport("user32.dll", SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial uint SendInput(uint count, in NativeInput input, int size);

        [StructLayout(LayoutKind.Sequential)]
        private struct ScreenPoint
        {
            public int X;
            public int Y;
        }

        // INPUT with the MOUSEINPUT union at offset 8 on the x64 test platform.
        [StructLayout(LayoutKind.Explicit, Size = 40)]
        private struct NativeInput
        {
            [FieldOffset(0)]
            public uint Type;

            [FieldOffset(8)]
            public int X;

            [FieldOffset(8)]
            public ushort VirtualKey;

            [FieldOffset(12)]
            public int Y;

            [FieldOffset(12)]
            public uint KeyboardFlags;

            [FieldOffset(20)]
            public uint Flags;
        }
    }
}
