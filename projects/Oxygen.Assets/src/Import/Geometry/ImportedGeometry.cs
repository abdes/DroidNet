// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Assets.Import.Geometry;

/// <summary>
/// Minimal canonical geometry payload produced by importers and consumed by the loose-cooked build step.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public sealed record ImportedGeometry(
    string Name,
    IReadOnlyList<ImportedVertex> Vertices,
    IReadOnlyList<uint> Indices,
    IReadOnlyList<ImportedSubMesh> SubMeshes,
    ImportedBounds Bounds);
