// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>Where a new root node is created: a world position and, when given, a world rotation.</summary>
/// <param name="Position">The world position.</param>
/// <param name="Rotation">The world rotation, or <see langword="null"/> to keep the node kind's default.</param>
public sealed record NodePlacement(Vector3 Position, Quaternion? Rotation = null);
