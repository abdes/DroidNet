// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Geometry;

/// <summary>
/// Heavy geometry data (vertices and indices) separated from metadata.
/// </summary>
public sealed record GeometryData(
    IReadOnlyList<ImportedVertex> Vertices,
    IReadOnlyList<uint> Indices);
