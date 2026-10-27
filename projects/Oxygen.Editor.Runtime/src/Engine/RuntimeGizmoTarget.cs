// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Runtime.InteropServices;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A manipulated node's new local transform.</summary>
/// <param name="Node">The node: a node of the scene, or a node an instance brings in.</param>
/// <param name="Position">Local position.</param>
/// <param name="Rotation">Local rotation.</param>
/// <param name="Scale">Local scale.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimeGizmoTarget(NodeReference Node, Vector3 Position, Quaternion Rotation, Vector3 Scale);
