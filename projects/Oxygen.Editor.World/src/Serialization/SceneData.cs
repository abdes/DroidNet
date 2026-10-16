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
    /// Gets or initializes the list of root nodes in the scene.
    /// </summary>
    public IList<SceneNodeData> RootNodes { get; init; } = [];

    /// <summary>
    /// Gets scene-level environment authoring data.
    /// </summary>
    public SceneEnvironmentData Environment { get; init; } = new();

    /// <summary>
    /// Gets optional scene-level native asset references.
    /// </summary>
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public SceneReferencesData? References { get; init; }

    /// <summary>
    /// Gets how the scene starts when played, or <see langword="null"/> when it starts with the defaults.
    /// </summary>
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public ScenePlayData? Play { get; init; }

    /// <summary>
    /// Gets optional editor-only data representing the scene explorer layout (folders and node references).
    /// This field is ignored by runtime code but saved/loaded to preserve editor state.
    /// </summary>
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]
    public IList<ExplorerEntryData>? ExplorerLayout { get; init; } = null;
}
