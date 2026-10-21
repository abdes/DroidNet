// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Controls validation of a material assignment at the native command boundary.</summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1028:Enum Storage should be Int32", Justification = "It mirrors the native MaterialSlotAssignmentIntent, a std::uint8_t that the transport passes as a byte.")]
public enum MaterialSlotAssignmentIntent : byte
{
    /// <summary>A new edit must match the inventory observed before the interaction.</summary>
    ObservedEdit = 0,

    /// <summary>Resolve saved or historical geometry/slot identity against its current inventory.</summary>
    RetainedAssignment = 1,
}
