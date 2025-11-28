// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Assets;

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
    /// Gets the name of this submesh.
    /// </summary>
    /// <value>
    /// A descriptive name such as "Head", "Body", or "Main".
    /// </value>
    public required string Name { get; init; }

    /// <summary>
    /// Gets the default material index for this submesh.
    /// </summary>
    /// <value>
    /// The zero-based index of the material to apply to this submesh.
    /// </value>
    public required int MaterialIndex { get; init; }
}
