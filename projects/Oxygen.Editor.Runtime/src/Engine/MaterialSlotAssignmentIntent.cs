// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Controls validation of a material assignment at the native command boundary.</summary>
public enum MaterialSlotAssignmentIntent : byte
{
    /// <summary>A new edit must match the inventory observed before the interaction.</summary>
    ObservedEdit = 0,

    /// <summary>Resolve saved or historical geometry/slot identity against its current inventory.</summary>
    RetainedAssignment = 1,
}
