// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
/// Snapshot of transform component values for messaging/undo purposes.
/// </summary>
public readonly record struct TransformSnapshot(Vector3 Position, Quaternion Rotation, Vector3 Scale);

/// <summary>
/// Message sent when one or more SceneNode transform values have been applied by a UI editor.
/// Contains snapshots of the old and new transform values for each node.
/// </summary>
internal sealed class SceneNodeTransformAppliedMessage(IList<SceneNode> nodes, IList<TransformSnapshot> oldValues, IList<TransformSnapshot> newValues, string property)
{
    public IList<SceneNode> Nodes { get; } = nodes;

    public IList<TransformSnapshot> OldValues { get; } = oldValues;

    public IList<TransformSnapshot> NewValues { get; } = newValues;

    /// <summary>
    /// Optional name of the property edited (e.g. "PositionX", "RotationY"). Useful for logging.
    /// </summary>
    public string Property { get; } = property;
}
