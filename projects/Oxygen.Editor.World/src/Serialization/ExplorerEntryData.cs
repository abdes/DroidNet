// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Lightweight editor-only DTO used to persist the Scene Explorer layout.
/// This is intentionally separate from scene nodes so files remain compatible
/// with runtime code that only cares about the scene graph.
/// </summary>
public record ExplorerEntryData
{
    /// <summary>
    /// Gets one of: "Node" or "Folder". Kept as a string for forward-compatibility.
    /// </summary>
    public string Type { get; init; } = "Node";

    /// <summary>
    /// Gets when Type == "Node", the referenced SceneNode Id.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public System.Guid? NodeId { get; init; }

    /// <summary>
    /// Gets when Type == "Folder", optional folder id (useful for stable ids across edits).
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public System.Guid? FolderId { get; init; }

    /// <summary>
    /// Gets or sets human friendly folder name (only for Type=="Folder").
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? Name { get; set; }

    /// <summary>
    /// Gets or sets child entries for folder nodes.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public IList<ExplorerEntryData>? Children { get; set; } = null;

    /// <summary>
    /// Gets or sets or initializes a value indicating whether the item is expanded in the explorer view.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public bool? IsExpanded { get; set; }
}
