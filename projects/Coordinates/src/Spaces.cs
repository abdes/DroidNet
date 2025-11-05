// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1402 // File may only contain a single type
#pragma warning disable SA1649 // File name should match first type name

namespace DroidNet.Coordinates;

/// <summary>
///     Marker type for element-local coordinates (logical DIPs, 1 DIP = 1/96″).
/// </summary>
public sealed class ElementSpace;

/// <summary>
///     Marker type for window client-area coordinates (logical DIPs, 1 DIP = 1/96″).
/// </summary>
public sealed class WindowSpace;

/// <summary>
///     Marker type for desktop-global coordinates (logical DIPs, per-monitor DPI aware).
///     Prefer this over <see cref="PhysicalScreenSpace"/> for UI math; convert at Win32 boundaries.
/// </summary>
public sealed class ScreenSpace;

/// <summary>
///     Marker type for physical screen pixels (hardware pixels).
///     Use only for Win32 interop (GetCursorPos, SetWindowPos, GetWindowRect, etc.).
/// </summary>
public sealed class PhysicalScreenSpace;
