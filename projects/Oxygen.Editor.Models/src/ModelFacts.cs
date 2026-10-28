// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>
/// What the Properties pane says of a model on its root (model editor 5.4): its source file, where its last import
/// stands and the counts of what it holds.
/// </summary>
public sealed record ModelFacts
{
    /// <summary>Gets the model's source file.</summary>
    public required AssetPath Source { get; init; }

    /// <summary>Gets where the model's last import stands, such as "Imported" or "Source changed".</summary>
    public required string LastImport { get; init; }

    /// <summary>
    /// Gets what the source file holds: its nodes, meshes, materials and textures; <see langword="null"/> when the file
    /// could not be read.
    /// </summary>
    public ModelSourceFacts? SourceFacts { get; init; }

    /// <summary>
    /// Gets the most LODs a geometry of the imported model has; <see langword="null"/> when no imported geometry could be
    /// read.
    /// </summary>
    public int? LodCount { get; init; }

    /// <summary>
    /// Gets the triangles of the imported model's LOD 0, summed over its geometries; <see langword="null"/> when no
    /// imported geometry could be read.
    /// </summary>
    public ulong? TriangleCount { get; init; }
}
