// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Assets;

/// <summary>
/// Represents a single Level of Detail (LOD) mesh within a geometry asset.
/// </summary>
public sealed class MeshLod
{
    /// <summary>
    /// Gets the LOD index for this mesh.
    /// </summary>
    /// <value>
    /// The zero-based LOD index. Lower values represent higher detail.
    /// </value>
    public required int LodIndex { get; init; }

    /// <summary>
    /// Gets the list of submeshes for this LOD.
    /// </summary>
    /// <value>
    /// A list of <see cref="SubMesh"/> objects representing logical partitions of this LOD mesh.
    /// </value>
    public required IList<SubMesh> SubMeshes { get; init; } = [];
}
