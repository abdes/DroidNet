// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Import.Geometry;

[StructLayout(LayoutKind.Sequential)]
public readonly record struct ImportedSubMesh(
    string Name,
    AssetKey MaterialAssetKey,
    uint FirstIndex,
    uint IndexCount,
    uint FirstVertex,
    uint VertexCount,
    ImportedBounds Bounds);
