// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A manipulated node's new local transform.</summary>
/// <param name="NodeId">The authored node.</param>
/// <param name="Position">Local position.</param>
/// <param name="Rotation">Local rotation.</param>
/// <param name="Scale">Local scale.</param>
public readonly record struct RuntimeGizmoTarget(Guid NodeId, Vector3 Position, Quaternion Rotation, Vector3 Scale);
