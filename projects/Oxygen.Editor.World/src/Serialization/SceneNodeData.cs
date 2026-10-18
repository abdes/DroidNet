// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for scene node.
/// </summary>
public record SceneNodeData : NamedData
{
    /// <summary>
    /// Gets or initializes the identity of the node.
    /// </summary>
    [JsonPropertyOrder(-1)]
    public required NodeId Id { get; init; }

    /// <summary>
    /// Gets or initializes the list of components (including TransformComponent).
    /// TransformComponent is represented by a <see cref="TransformData"/> entry.
    /// </summary>
    public IList<ComponentData> Components { get; init; } = [];

    /// <summary>
    /// Gets or initializes the list of child nodes.
    /// </summary>
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]
    public IList<SceneNodeData>? Children { get; init; } = null;

    /// <summary>
    /// Gets or initializes the list of override slots for this node.
    /// </summary>
    public IList<OverrideSlotData> OverrideSlots { get; init; } = [];

    /// <summary>
    /// Gets a value indicating whether gets or initializes a value indicating whether the node is active.
    /// </summary>
    public bool IsActive { get; init; }

    /// <summary>
    /// Gets a value indicating whether gets or initializes a value indicating whether the node is visible.
    /// </summary>
    public bool IsVisible { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether gets or initializes a value indicating whether the node casts shadows.
    /// </summary>
    public bool CastsShadows { get; init; }

    /// <summary>
    /// Gets a value indicating whether gets or initializes a value indicating whether the node receives shadows.
    /// </summary>
    public bool ReceivesShadows { get; init; }

    /// <summary>
    /// Gets a value indicating whether gets or initializes a value indicating whether the node is ray casting selectable.
    /// </summary>
    public bool IsRayCastingSelectable { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether gets or initializes a value indicating whether the node ignores parent transform.
    /// </summary>
    public bool IgnoreParentTransform { get; init; }

    /// <summary>
    /// Gets a value indicating whether gets or initializes a value indicating whether the node is static.
    /// </summary>
    public bool IsStatic { get; init; }
}
