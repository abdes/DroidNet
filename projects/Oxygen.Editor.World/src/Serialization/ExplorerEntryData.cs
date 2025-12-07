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
    /// One of: "Node" or "Folder". Kept as a string for forward-compatibility.
    /// </summary>
    public string Type { get; init; } = "Node";

    /// <summary>
    /// When Type == "Node", the referenced SceneNode Id.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public System.Guid? NodeId { get; init; }

    /// <summary>
    /// When Type == "Folder", optional folder id (useful for stable ids across edits).
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public System.Guid? FolderId { get; init; }

    /// <summary>
    /// Human friendly folder name (only for Type=="Folder").
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? Name { get; init; }

    /// <summary>
    /// Child entries for folder nodes.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public IList<ExplorerEntryData>? Children { get; init; } = null;
}
