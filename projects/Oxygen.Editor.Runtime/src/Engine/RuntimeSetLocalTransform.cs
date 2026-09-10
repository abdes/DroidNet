// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to SetLocalTransform.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="Position">The Position command value.</param>
/// <param name="Rotation">The Rotation command value.</param>
/// <param name="Scale">The Scale command value.</param>
public sealed record RuntimeSetLocalTransform(Guid NodeId, Vector3 Position, Quaternion Rotation, Vector3 Scale) : RuntimeWorldCommand;
