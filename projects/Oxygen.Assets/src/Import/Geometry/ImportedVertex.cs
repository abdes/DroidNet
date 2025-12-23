// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Runtime.InteropServices;

namespace Oxygen.Assets.Import.Geometry;

/// <summary>
/// Matches the runtime <c>oxygen::data::Vertex</c> layout (72 bytes).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly record struct ImportedVertex(
    Vector3 Position,
    Vector3 Normal,
    Vector2 Texcoord,
    Vector3 Tangent,
    Vector3 Bitangent,
    Vector4 Color);
