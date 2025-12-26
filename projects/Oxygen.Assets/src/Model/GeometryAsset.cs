// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Import.Geometry;

namespace Oxygen.Assets.Model;

/// <summary>
/// Represents a geometry asset containing Level of Detail (LOD) meshes.
/// </summary>
/// <remarks>
/// This class mirrors the engine's <c>GeometryAsset</c> structure, where a geometry contains
/// multiple LODs, and each LOD contains one or more submeshes.
/// </remarks>
public sealed class GeometryAsset : Asset
{
    /// <summary>
    /// Gets or sets the source geometry metadata.
    /// </summary>
    /// <value>
    /// The imported geometry metadata containing submesh definitions and bounds.
    /// Vertex and index data are stored separately in the intermediate cache.
    /// </value>
    public ImportedGeometry? Source { get; set; }

    /// <summary>
    /// Gets the list of Level of Detail (LOD) meshes for this geometry.
    /// </summary>
    /// <value>
    /// A list of <see cref="MeshLod"/> objects representing different detail levels of the geometry.
    /// </value>
    public required IList<MeshLod> Lods { get; init; } = [];
}
