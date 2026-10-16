// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A scene node with visible geometry inside a picked rectangle.</summary>
/// <param name="NodeId">The authored node.</param>
/// <param name="Depth">Device depth of the node's nearest pixel in the rectangle.</param>
/// <param name="GeometrySlot">Geometry slot (submesh) under the node's pixel closest to the centre.</param>
/// <param name="CenterDistance">Pixels from the rectangle centre to the node's closest pixel.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimePickHit(NodeId NodeId, float Depth, uint GeometrySlot, float CenterDistance);
