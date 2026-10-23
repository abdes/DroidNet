// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Native surface inventory for the selected authored geometry.</summary>
/// <param name="GeometryUri">The requested authored identity.</param>
/// <param name="NativeGeometryKey">The native geometry key parsed from canonical UUID text.</param>
/// <param name="LayoutRevision">The native SHA-256 layout revision.</param>
/// <param name="Surfaces">The ordered native surfaces.</param>
public sealed record GeometrySurfaceMetadata(AssetPath GeometryUri, Guid NativeGeometryKey, string LayoutRevision, ImmutableArray<GeometrySurface> Surfaces);

/// <summary>One opaque semantic surface and its exact surface bindings.</summary>
/// <param name="SurfaceId">The native identity; labels and ordering do not establish identity.</param>
/// <param name="DisplayName">The native presentation label.</param>
/// <param name="Visible">The asset's default Visibility of the surface, which applies where a scene does not override it.</param>
/// <param name="CastsShadows">The asset's default Cast Shadows of the surface, which applies where a scene does not override it.</param>
/// <param name="Bindings">All LOD/submesh bindings owned by this surface.</param>
public sealed record GeometrySurface(Guid SurfaceId, string DisplayName, bool Visible, bool CastsShadows, ImmutableArray<GeometrySurfaceBinding> Bindings);

/// <summary>One surface's native default assignment.</summary>
/// <param name="LodIndex">The zero-based LOD index.</param>
/// <param name="SubmeshIndex">The zero-based submesh index.</param>
/// <param name="DefaultMaterialKey">The native material key; empty retains the omitted-reference sentinel.</param>
public sealed record GeometrySurfaceBinding(uint LodIndex, uint SubmeshIndex, Guid DefaultMaterialKey);
