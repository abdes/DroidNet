// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Assets;

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
    /// Gets or sets the list of Level of Detail (LOD) meshes for this geometry.
    /// </summary>
    /// <value>
    /// A list of <see cref="MeshLod"/> objects representing different detail levels of the geometry.
    /// </value>
    public required List<MeshLod> Lods { get; init; } = [];
}

/// <summary>
/// Represents a single Level of Detail (LOD) mesh within a geometry asset.
/// </summary>
public sealed class MeshLod
{
    /// <summary>
    /// Gets or sets the LOD index for this mesh.
    /// </summary>
    /// <value>
    /// The zero-based LOD index. Lower values represent higher detail.
    /// </value>
    public required int LodIndex { get; init; }

    /// <summary>
    /// Gets or sets the list of submeshes for this LOD.
    /// </summary>
    /// <value>
    /// A list of <see cref="SubMesh"/> objects representing logical partitions of this LOD mesh.
    /// </value>
    public required List<SubMesh> SubMeshes { get; init; } = [];
}

/// <summary>
/// Represents a submesh within a LOD mesh.
/// </summary>
/// <remarks>
/// Submeshes are logical partitions of a mesh, typically used for material binding
/// and rendering optimization.
/// </remarks>
public sealed class SubMesh
{
    /// <summary>
    /// Gets or sets the name of this submesh.
    /// </summary>
    /// <value>
    /// A descriptive name such as "Head", "Body", or "Main".
    /// </value>
    public required string Name { get; init; }

    /// <summary>
    /// Gets or sets the default material index for this submesh.
    /// </summary>
    /// <value>
    /// The zero-based index of the material to apply to this submesh.
    /// </value>
    public required int MaterialIndex { get; init; }
}
