// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for scene.
/// </summary>
public record SceneData : GameObjectData
{
    /// <summary>
    /// Gets or initializes the list of root nodes in the scene.
    /// </summary>
    public IList<SceneNodeData> RootNodes { get; init; } = [];

    /// <summary>
    /// Gets optional editor-only data representing the scene explorer layout (folders and node references).
    /// This field is ignored by runtime code but saved/loaded to preserve editor state.
    /// </summary>
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]
    public IList<ExplorerEntryData>? ExplorerLayout { get; init; } = null;
}
