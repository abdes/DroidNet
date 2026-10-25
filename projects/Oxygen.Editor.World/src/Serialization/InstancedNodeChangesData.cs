// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for the <see cref="InstancedNodeChanges"/> of one node path.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public record InstancedNodeChangesData
{
    /// <summary>
    /// Gets or initializes the node's path below the instance root: one node ID per nesting level, shallowest first.
    /// </summary>
    public required IList<NodeId> Path { get; init; }

    /// <summary>
    /// Gets or initializes the node's last known names, or <see langword="null"/> when they are not known.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? Names { get; init; }

    /// <summary>
    /// Gets or initializes the source a swap carried the changes over from, or <see langword="null"/>.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public AssetPath? CarriedFrom { get; init; }

    /// <summary>
    /// Gets or initializes the changes of each changed component, by component kind.
    /// </summary>
    public required IDictionary<string, SourceOverridesData> Components { get; init; }

    /// <summary>
    /// Gets the script parameter values the scene changes, by the identity of the instanced node's script slot and the
    /// parameter's name; <see langword="null"/> when it changes none.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IDictionary<Guid, IDictionary<string, ScriptValueData>>? Parameters { get; init; }
}
