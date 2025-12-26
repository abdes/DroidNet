// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Geometry;

/// <summary>
/// Source metadata for a mesh asset.
/// Contains structure and bounds, but no vertex data.
/// </summary>
public sealed record ImportedGeometry(
    string Schema,
    string Name,
    string Source,
    int MeshIndex,
    ImportedBounds Bounds,
    IReadOnlyList<ImportedSubMesh> SubMeshes);
