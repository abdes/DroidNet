// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for scene.
/// </summary>
public record SceneData : NamedData
{
    /// <summary>
    /// Gets or initializes the identity of the scene.
    /// </summary>
    [JsonPropertyOrder(-1)]
    public required SceneId Id { get; init; }

    /// <summary>
    /// Gets or sets the list of root nodes in the scene.
    /// </summary>
    public IList<SceneNodeData> RootNodes { get; set; } = [];

    /// <summary>
    /// Gets or sets scene-level environment authoring data.
    /// </summary>
    public SceneEnvironmentData Environment { get; set; } = new();

    /// <summary>
    /// Gets optional scene-level native asset references.
    /// </summary>
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public SceneReferencesData? References { get; init; }

    /// <summary>
    /// Gets the camera node the scene starts viewing through, or <see langword="null"/> for the first camera in
    /// hierarchy order.
    /// </summary>
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public NodeId? StartingCamera { get; init; }

    /// <summary>
    /// Gets the input mapping contexts that are active when the scene starts, in order, or <see langword="null"/> for
    /// none.
    /// </summary>
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public IList<StartingInputContextData>? StartingInputContexts { get; init; }
}
