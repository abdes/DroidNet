// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.World.Serialization;

/// <summary>An authored compensation control point in EV stops.</summary>
/// <param name="MeteredEv">The raw metered EV100 coordinate.</param>
/// <param name="CompensationEv">The exposure compensation at that coordinate.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct ExposureCompensationKeyData(float MeteredEv, float CompensationEv);
