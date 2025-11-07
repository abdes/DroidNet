// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

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

    [LibraryImport(User32, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool GetCursorPos(out POINT lpPoint);

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
