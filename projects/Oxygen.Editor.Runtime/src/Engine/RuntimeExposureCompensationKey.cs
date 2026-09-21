// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>One ordered exposure compensation point transported to the engine.</summary>
/// <param name="MeteredEv">Raw metered EV100.</param>
/// <param name="CompensationEv">Additional compensation in EV stops.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimeExposureCompensationKey(float MeteredEv, float CompensationEv);
