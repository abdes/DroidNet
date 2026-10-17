// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Scenes.Nodes;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>A kind of light and the local transform its node starts with.</summary>
/// <param name="Kind">The kind of light.</param>
/// <param name="Position">The node's local position.</param>
/// <param name="Rotation">The node's local rotation.</param>
public sealed record LightTransformCase(LightKind Kind, Vector3 Position, Quaternion Rotation);
