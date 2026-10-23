// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Controls how the native command boundary validates a part's layout revision.</summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1028:Enum Storage should be Int32", Justification = "It mirrors the native OverrideIntent, a std::uint8_t that the transport passes as a byte.")]
public enum OverrideIntent : byte
{
    /// <summary>A new edit must match the inventory observed before the interaction.</summary>
    ObservedEdit = 0,

    /// <summary>A saved override resolves its geometry and surface identity against the current inventory.</summary>
    RetainedOverride = 1,
}
