// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Native material-slot inventory for the selected authored geometry.</summary>
/// <param name="GeometryUri">The requested authored identity.</param>
/// <param name="NativeGeometryKey">The native geometry key parsed from canonical UUID text.</param>
/// <param name="LayoutRevision">The native SHA-256 layout revision.</param>
/// <param name="Slots">The ordered native slots.</param>
public sealed record GeometryMaterialSlotMetadata(Uri GeometryUri, Guid NativeGeometryKey, string LayoutRevision, ImmutableArray<GeometryMaterialSlot> Slots);

/// <summary>One opaque semantic slot and its exact surface bindings.</summary>
/// <param name="SlotId">The native identity; labels and ordering do not establish identity.</param>
/// <param name="DisplayName">The native presentation label.</param>
/// <param name="Bindings">All LOD/submesh bindings owned by this slot.</param>
public sealed record GeometryMaterialSlot(Guid SlotId, string DisplayName, ImmutableArray<GeometryMaterialSlotBinding> Bindings);

/// <summary>One surface's native default assignment.</summary>
/// <param name="LodIndex">The zero-based LOD index.</param>
/// <param name="SubmeshIndex">The zero-based submesh index.</param>
/// <param name="DefaultMaterialKey">The native material key; empty retains the omitted-reference sentinel.</param>
public sealed record GeometryMaterialSlotBinding(uint LodIndex, uint SubmeshIndex, Guid DefaultMaterialKey);
