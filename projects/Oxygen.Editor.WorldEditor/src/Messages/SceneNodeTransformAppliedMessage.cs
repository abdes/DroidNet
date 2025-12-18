// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Runtime.InteropServices;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
/// Snapshot of transform component values for messaging/undo purposes.
/// </summary>
[StructLayout(LayoutKind.Auto)]
[System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.DocumentationRules", "SA1649:File name should match first type name", Justification = "It's an auxiliary type")]
public readonly record struct TransformSnapshot(Vector3 Position, Quaternion Rotation, Vector3 Scale);

/// <summary>
/// Message sent when one or more <see cref="SceneNode"/> transform values have
/// been applied by a UI editor.
/// </summary>
/// <param name="nodes">The nodes that were modified.</param>
/// <param name="oldValues">Snapshots of transform values before the edit.</param>
/// <param name="newValues">Snapshots of transform values after the edit.</param>
/// <param name="property">Optional name of the specific property that was edited (for example, "PositionX").</param>
/// <remarks>
/// Each list in <see cref="Nodes"/>, <see cref="OldValues"/>, and <see cref="NewValues"/> is expected to
/// be the same length and correspond by index.
/// </remarks>
internal sealed class SceneNodeTransformAppliedMessage(IList<SceneNode> nodes, IList<TransformSnapshot> oldValues, IList<TransformSnapshot> newValues, string property)
{
    /// <summary>
    /// Gets the nodes that were modified by the editor.
    /// </summary>
    public IList<SceneNode> Nodes { get; } = nodes;

    /// <summary>
    /// Gets snapshots of the transform values before the change, aligned by index with <see cref="Nodes"/>.
    /// </summary>
    public IList<TransformSnapshot> OldValues { get; } = oldValues;

    /// <summary>
    /// Gets snapshots of the transform values after the change, aligned by index with <see cref="Nodes"/>.
    /// </summary>
    public IList<TransformSnapshot> NewValues { get; } = newValues;

    /// <summary>
    /// Gets optional name of the property edited (for example, "PositionX" or "RotationY"). Useful for logging.
    /// </summary>
    public string Property { get; } = property;
}
