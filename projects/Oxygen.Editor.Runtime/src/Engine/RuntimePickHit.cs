// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A scene node with visible geometry inside a picked rectangle.</summary>
/// <param name="Node">The node; a node an instance brought in is named by its instance root and node path.</param>
/// <param name="Depth">Device depth of the node's nearest pixel in the rectangle.</param>
/// <param name="GeometrySlot">Geometry slot (submesh) under the node's pixel closest to the centre.</param>
/// <param name="CenterDistance">Pixels from the rectangle centre to the node's closest pixel.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimePickHit(NodeReference Node, float Depth, uint GeometrySlot, float CenterDistance);
