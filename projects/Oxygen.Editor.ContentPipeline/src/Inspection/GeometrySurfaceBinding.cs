// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>One surface's native default assignment.</summary>
/// <param name="LodIndex">The zero-based LOD index.</param>
/// <param name="SubmeshIndex">The zero-based submesh index.</param>
/// <param name="DefaultMaterialKey">The native material key; empty retains the omitted-reference sentinel.</param>
/// <param name="TriangleCount">The triangles the bound submesh draws at this LOD.</param>
public sealed record GeometrySurfaceBinding(uint LodIndex, uint SubmeshIndex, Guid DefaultMaterialKey, ulong TriangleCount);
