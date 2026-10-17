// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>A removed material override and where it was in its component's override list.</summary>
/// <param name="Assignment">The override.</param>
/// <param name="Index">Its position in the override list.</param>
public sealed record RestoredMaterialSlot(MaterialSlotAssignment Assignment, int Index);
