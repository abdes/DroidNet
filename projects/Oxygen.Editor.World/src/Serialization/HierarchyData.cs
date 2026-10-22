// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for a node's hierarchy component: the node's ordered children, nested in it. The parent is
/// the node whose hierarchy nests this one, or none for a scene root.
/// </summary>
public record HierarchyData : ComponentData
{
    /// <summary>
    /// Gets or initializes the ordered child nodes.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public IList<SceneNodeData>? Children { get; init; }
}
