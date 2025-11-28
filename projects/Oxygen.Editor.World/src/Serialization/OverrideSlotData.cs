// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Base data transfer object for override slots.
/// </summary>
[JsonPolymorphic(TypeDiscriminatorPropertyName = "$type")]
[JsonDerivedType(typeof(RenderingSlotData), "RenderingSlot")]
[JsonDerivedType(typeof(LightingSlotData), "LightingSlot")]
[JsonDerivedType(typeof(MaterialsSlotData), "MaterialsSlot")]
[JsonDerivedType(typeof(LevelOfDetailSlotData), "LevelOfDetailSlot")]
public abstract record OverrideSlotData
{
}
