// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to SetMaterialOverride.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="SlotIndex">The SlotIndex command value.</param>
/// <param name="MaterialPath">The cooked material path, or null to clear the override.</param>
public sealed record RuntimeSetMaterialOverride(Guid NodeId, int SlotIndex, string? MaterialPath) : RuntimeWorldCommand;
