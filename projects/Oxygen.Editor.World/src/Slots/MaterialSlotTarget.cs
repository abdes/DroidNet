// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Slots;

/// <summary>Identifies an existing native slot and the inventory observed by its editor.</summary>
/// <param name="GeometryUri">The geometry whose native inventory owns the slot.</param>
/// <param name="SlotId">The opaque native slot identity.</param>
/// <param name="LayoutRevision">The native inventory revision observed for this assignment.</param>
public sealed record MaterialSlotTarget(Uri GeometryUri, Guid SlotId, string LayoutRevision);
