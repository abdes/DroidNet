// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace DroidNet.Aura.Drag;

#pragma warning disable CA1034 // Nested types should not be visible
#pragma warning disable CA1815 // Override equals and operator equals on value types

/// <summary>
///     Application-wide drag coordinator that maintains active drag state across all <see
///     cref="ITabStrip"/> instances within the process. It serializes drag lifecycle operations
///     (start/move/end) and drives the <see cref="IDragVisualService"/>.
/// </summary>
public partial class TabDragCoordinator
{
    private const string User32 = "user32.dll";
    private const int VirtualKeyLeftButton = 0x01;
    private const int VirtualKeyRightButton = 0x02;
    private const int VirtualKeyEscape = 0x1B;
    private const int SystemMetricSwapButton = 23;

    /// <summary>Gets or sets how the coordinator reads the keys that end a drag; tests replace it.</summary>
    internal Func<DragButtons> ReadButtons { get; set; } = ReadSystemButtons;

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool GetCursorPos(out POINT lpPoint);

    [LibraryImport(User32)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial short GetAsyncKeyState(int vKey);

    [LibraryImport(User32)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial int GetSystemMetrics(int nIndex);

    // The primary button is the right one when the user swapped the mouse buttons.
    private static DragButtons ReadSystemButtons()
        => new(
            IsPrimaryDown: IsKeyDown(GetSystemMetrics(SystemMetricSwapButton) != 0 ? VirtualKeyRightButton : VirtualKeyLeftButton),
            IsEscapeDown: IsKeyDown(VirtualKeyEscape));

    private static bool IsKeyDown(int virtualKey) => (GetAsyncKeyState(virtualKey) & 0x8000) != 0;

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
}
