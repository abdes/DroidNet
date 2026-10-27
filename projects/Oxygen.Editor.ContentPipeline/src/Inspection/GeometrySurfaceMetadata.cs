// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Native surface inventory for the selected authored geometry.</summary>
/// <param name="GeometryUri">The requested authored identity.</param>
/// <param name="NativeGeometryKey">The native geometry key parsed from canonical UUID text.</param>
/// <param name="LayoutRevision">The native SHA-256 layout revision.</param>
/// <param name="Surfaces">The ordered native surfaces.</param>
/// <param name="LodPolicy">The asset's default LOD policy, which applies where a scene does not override it.</param>
public sealed record GeometrySurfaceMetadata(AssetPath GeometryUri, Guid NativeGeometryKey, string LayoutRevision, ImmutableArray<GeometrySurface> Surfaces, LodPolicy LodPolicy)
{
    /// <summary>Gets the geometry's number of LODs: every LOD binds a submesh to a surface, and a geometry has at least one.</summary>
    public int LodCount => this.Surfaces.SelectMany(static surface => surface.Bindings).Select(static binding => (int)binding.LodIndex + 1).DefaultIfEmpty(1).Max();
}
