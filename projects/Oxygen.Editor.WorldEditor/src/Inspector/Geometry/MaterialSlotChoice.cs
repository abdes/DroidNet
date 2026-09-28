// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Inspector.Geometry;

/// <summary>One observed native slot; its label never establishes assignment identity.</summary>
/// <param name="Target">The geometry, opaque slot and observed inventory revision.</param>
/// <param name="DisplayName">The native presentation label.</param>
public sealed record MaterialSlotChoice(MaterialSlotTarget Target, string DisplayName);
