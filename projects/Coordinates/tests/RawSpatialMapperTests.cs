// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using DroidNet.Tests;
using DryIoc;
using AwesomeAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
public sealed partial class RawSpatialMapperTests : VisualUserInterfaceTests, IDisposable
{
    private readonly IContainer container = new Container().WithSpatialMapping();
    private bool isDisposed;

    [TestMethod]
    public Task NativeWindow_ClientToScreen_Roundtrip_Async() => EnqueueAsync(() =>
    {
        using var win = NativeWindow.Create("DroidNet.Test.NativeWnd");

        var factory = this.container.Resolve<RawSpatialMapperFactory>();
        var mapper = factory(win.Hwnd);

        // Client point inside the window
        var client = new SpatialPoint<WindowSpace>(new Point(10, 12));

        // Map client -> screen -> client and verify approximate roundtrip
        var screen = mapper.ToScreen(client);
        var client2 = mapper.ToWindow(screen);

        client2.Point.X.Should().BeApproximately(client.Point.X, 2.0);
        client2.Point.Y.Should().BeApproximately(client.Point.Y, 2.0);

        return Task.CompletedTask;
    });

    [TestMethod]
    public Task NativeWindow_ZeroAndNonZeroHwnd_CreateMapper_Async() => EnqueueAsync(() =>
    {
        var factory = this.container.Resolve<RawSpatialMapperFactory>();

        // Zero hwnd is valid (mapper defers to mappings that require hwnd)
        var mapperZero = factory();
        mapperZero.Should().NotBeNull().And.BeOfType<SpatialMapper>();

        using var win = NativeWindow.Create("DroidNet.Test.NativeWnd2");
        var mapper = factory(win.Hwnd);
        mapper.Should().NotBeNull().And.BeOfType<SpatialMapper>();

        return Task.CompletedTask;
    });

    [TestMethod]
    public void RawSpatialMapperFactory_ZeroHwnd_Throws_OnWindowConversions()
    {
        // Arrange
        var factory = this.container.Resolve<RawSpatialMapperFactory>();
        var mapperZero = factory();

        // Act / Assert - calling window-specific conversions without an HWND should throw
        Action a1 = () => mapperZero.ToWindow(new SpatialPoint<ScreenSpace>(new Point(0, 0)));
        Action a2 = () => mapperZero.ToPhysicalScreen(new SpatialPoint<WindowSpace>(new Point(0, 0)));

        a1.Should().Throw<InvalidOperationException>();
        a2.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public Task RawMapper_Physical_Screen_Physical_Roundtrip_NoWindow_Async() => EnqueueAsync(() =>
    {
        // Arrange - raw mapper (no HWND)
        var factory = this.container.Resolve<RawSpatialMapperFactory>();
        var mapper = factory();

        // Pick a realistic physical point within typical screen bounds (avoids relying on
        // the same helper functions for expected values). We'll verify roundtrip fidelity
        // instead of reproducing the exact conversion implementation.
        var originalPhysical = new Point(300.0, 200.0);

        // Act
        var toScreen = mapper.ToScreen(new SpatialPoint<PhysicalScreenSpace>(originalPhysical));
        var backToPhysical = mapper.ToPhysicalScreen(toScreen);

        // Assert - roundtrip should return approximately the original physical point
        backToPhysical.Point.X.Should().BeApproximately(originalPhysical.X, 1.0);
        backToPhysical.Point.Y.Should().BeApproximately(originalPhysical.Y, 1.0);

        return Task.CompletedTask;
    });

    [TestMethod]
    public Task RawMapper_Screen_Physical_Screen_Roundtrip_NoWindow_Async() => EnqueueAsync(() =>
    {
        // Arrange - raw mapper (no HWND)
        var factory = this.container.Resolve<RawSpatialMapperFactory>();
        var mapper = factory();

        // Pick a logical screen point near origin of a window-local coordinate system.
        var originalScreen = new Point(150.0, 120.0);

        // Act
        var toPhysical = mapper.ToPhysicalScreen(new SpatialPoint<ScreenSpace>(originalScreen));
        var backToScreen = mapper.ToScreen(toPhysical);

        // Assert - roundtrip should return approximately the original logical screen point
        backToScreen.Point.X.Should().BeApproximately(originalScreen.X, 1.0);
        backToScreen.Point.Y.Should().BeApproximately(originalScreen.Y, 1.0);

        return Task.CompletedTask;
    });

    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (!this.isDisposed)
        {
            if (disposing)
            {
            }

            this.container.Dispose();
            this.isDisposed = true;
        }
    }

    /// <summary>
    /// Minimal native window helper for tests. Registers a unique class and creates a top-level
    /// window to obtain an HWND for SpatialMapper interop tests.
    /// </summary>
    private sealed partial class NativeWindow : IDisposable
    {
        private const string User32 = "user32.dll";

        private const string Kernel32 = "kernel32.dll";

        private readonly string className;

        private NativeWindow(string className, IntPtr hwnd)
        {
            this.className = className;
            this.Hwnd = hwnd;
        }

        private delegate IntPtr WndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        public IntPtr Hwnd { get; }

        public static NativeWindow Create(string baseName)
        {
            var wndClassName = baseName + Guid.NewGuid().ToString("N");

            var wndClass = new WNDCLASSEX
            {
                CbSize = Marshal.SizeOf<WNDCLASSEX>(),
                Style = 0,
                LpfnWndProc = DefWindowProc,
                CbClsExtra = 0,
                CbWndExtra = 0,
                HInstance = GetModuleHandle(IntPtr.Zero),
                HIcon = IntPtr.Zero,
                HCursor = IntPtr.Zero,
                HbrBackground = IntPtr.Zero,
                LpszMenuName = null,
                LpszClassName = wndClassName,
                HIconSm = IntPtr.Zero,
            };

            var reg = RegisterClassEx(ref wndClass);
            if (reg == 0)
            {
                throw new InvalidOperationException("RegisterClassEx failed.");
            }

            const int WS_OVERLAPPEDWINDOW = 0x00CF0000;
            var hwnd = CreateWindowExW(
                0,
                wndClassName,
                "DroidNet Test Window",
                WS_OVERLAPPEDWINDOW,
                0,
                0,
                200,
                200,
                IntPtr.Zero,
                IntPtr.Zero,
                wndClass.HInstance,
                IntPtr.Zero);

            if (hwnd == IntPtr.Zero)
            {
                UnregisterClass(wndClassName, wndClass.HInstance);
                throw new InvalidOperationException("CreateWindowEx failed.");
            }

            // Make the window visible so DPI and client/screen geometry are established.
            const int SW_SHOWNOACTIVATE = 4;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            UpdateWindow(hwnd);

            return new NativeWindow(wndClassName, hwnd);
        }

        public void Dispose()
        {
            if (this.Hwnd != IntPtr.Zero)
            {
                DestroyWindow(this.Hwnd);
            }

            UnregisterClass(this.className, GetModuleHandle(IntPtr.Zero));
        }

        [DllImport(User32, SetLastError = true, CharSet = CharSet.Unicode, EntryPoint = "RegisterClassExW")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static extern ushort RegisterClassEx([In] ref WNDCLASSEX lpwcx);

        [LibraryImport(User32, SetLastError = true, EntryPoint = "UnregisterClassW")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static partial bool UnregisterClass([MarshalAs(UnmanagedType.LPWStr)] string lpClassName, IntPtr hInstance);

        [LibraryImport(User32, SetLastError = true, EntryPoint = "CreateWindowExW")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial IntPtr CreateWindowExW(int dwExStyle, [MarshalAs(UnmanagedType.LPWStr)] string lpClassName, [MarshalAs(UnmanagedType.LPWStr)] string lpWindowName,
            int dwStyle, int x, int y, int nWidth, int nHeight, IntPtr hWndParent, IntPtr hMenu, IntPtr hInstance, IntPtr lpParam);

        [LibraryImport(User32, SetLastError = true, EntryPoint = "DestroyWindow")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static partial bool DestroyWindow(IntPtr hWnd);

        [LibraryImport(Kernel32, SetLastError = true, EntryPoint = "GetModuleHandleW")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial IntPtr GetModuleHandle(IntPtr lpModuleName);

        [LibraryImport(User32, EntryPoint = "DefWindowProcW")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial IntPtr DefWindowProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [LibraryImport(User32, SetLastError = true, EntryPoint = "ShowWindow")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static partial bool ShowWindow(IntPtr hWnd, int nCmdShow);

        [LibraryImport(User32, SetLastError = true, EntryPoint = "UpdateWindow")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static partial bool UpdateWindow(IntPtr hWnd);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]

        private struct WNDCLASSEX
        {
            public int CbSize;
            public int Style;
            public WndProc LpfnWndProc;
            public int CbClsExtra;
            public int CbWndExtra;
            public IntPtr HInstance;
            public IntPtr HIcon;
            public IntPtr HCursor;
            public IntPtr HbrBackground;
            [MarshalAs(UnmanagedType.LPWStr)]
            public string? LpszMenuName;
            [MarshalAs(UnmanagedType.LPWStr)]
            public string LpszClassName;
            public IntPtr HIconSm;
        }
   }
}
