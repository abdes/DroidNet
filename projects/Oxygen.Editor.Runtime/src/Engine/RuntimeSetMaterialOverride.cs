// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to SetMaterialOverride.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="GeometryPath">The geometry owning the native slot.</param>
/// <param name="SlotId">The opaque native slot identity.</param>
/// <param name="LayoutRevision">The inventory revision observed for this edit.</param>
/// <param name="MaterialPath">The cooked material path, or null to clear the override.</param>
/// <param name="Intent">Whether this is a new observed edit or a retained authored assignment.</param>
public sealed record RuntimeSetMaterialOverride(Guid NodeId, string GeometryPath, Guid SlotId, string LayoutRevision, string? MaterialPath, MaterialSlotAssignmentIntent Intent) : RuntimeWorldCommand;
