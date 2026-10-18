// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>A material override of one geometry slot, as an edit records it.</summary>
/// <param name="Target">The slot, with the inventory revision it was chosen from.</param>
/// <param name="Material">The material, or <see langword="null"/> for no override.</param>
public sealed record MaterialSlotAssignment(MaterialSlotTarget Target, AssetPath? Material);
