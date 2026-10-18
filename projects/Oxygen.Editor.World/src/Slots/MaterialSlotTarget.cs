// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Slots;

/// <summary>Identifies an existing native slot and the inventory observed by its editor.</summary>
/// <param name="Geometry">The geometry whose native inventory owns the slot.</param>
/// <param name="SlotId">The opaque native slot identity.</param>
/// <param name="LayoutRevision">The native inventory revision observed for this assignment.</param>
public sealed record MaterialSlotTarget(AssetPath Geometry, Guid SlotId, string LayoutRevision);
