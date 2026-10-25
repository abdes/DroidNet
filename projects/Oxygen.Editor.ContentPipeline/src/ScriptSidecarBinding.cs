// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

// These types are very tightly coupled, so they are kept together in one file
// to avoid unnecessary file proliferation.
#pragma warning disable SA1402 // File may only contain a single type

/// <summary>
/// One script slot of a scene, as the cooker's script sidecar job takes it.
/// </summary>
/// <param name="NodeIndex">The node's index in the cooked scene.</param>
/// <param name="SlotId">The slot identity.</param>
/// <param name="ScriptVirtualPath">The script, such as <c>/Content/Player/move.oscript</c>.</param>
/// <param name="ExecutionOrder">The execution order; lower runs earlier.</param>
/// <param name="Params">The parameter values that differ from the script's defaults.</param>
public sealed record ScriptSidecarBinding(
    [property: JsonPropertyName("node_index")] uint NodeIndex,
    [property: JsonPropertyName("slot_id")] string SlotId,
    [property: JsonPropertyName("script_virtual_path")] string ScriptVirtualPath,
    [property: JsonPropertyName("execution_order")] int ExecutionOrder,
    [property: JsonPropertyName("params")] IReadOnlyList<ScriptSidecarParam> Params);

/// <summary>
/// The script parameter changes a scene makes of one script slot of a node an instance brings in, as the cooker's script
/// sidecar job takes them.
/// </summary>
/// <param name="NodeIndex">The instance root's index in the cooked scene.</param>
/// <param name="NodePath">The node below the instance root, one node ID per nesting level, shallowest first.</param>
/// <param name="SlotId">The identity of the instanced node's slot.</param>
/// <param name="Params">The parameter values the scene changes.</param>
public sealed record ScriptSidecarInstanceBinding(
    [property: JsonPropertyName("node_index")] uint NodeIndex,
    [property: JsonPropertyName("node_path")] IReadOnlyList<string> NodePath,
    [property: JsonPropertyName("slot_id")] string SlotId,
    [property: JsonPropertyName("params")] IReadOnlyList<ScriptSidecarParam> Params);

/// <summary>
/// One parameter value of a script slot.
/// </summary>
/// <param name="Key">The parameter name.</param>
/// <param name="Type">The type: <see langword="bool"/>, <c>int32</c>, <see langword="float"/>, <see langword="string"/>, <c>vec2</c>, <c>vec3</c>,
/// <c>vec4</c>, <c>asset</c> or <c>node</c>.</param>
/// <param name="Value">The value: a boolean, an integer, a number, a string, an array of numbers, an asset's virtual
/// path, or a <see cref="ScriptSidecarNodeRef"/>.</param>
public sealed record ScriptSidecarParam(
    [property: JsonPropertyName("key")] string Key,
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("value")] object Value);

/// <summary>
/// A node value of a script parameter, as the cooked scene names a node.
/// </summary>
/// <param name="Node">The node's index in the cooked scene; an instance root when <paramref name="Path"/> is set.</param>
/// <param name="Path">The node an instance brings in below that root, one node ID per nesting level, or
/// <see langword="null"/> for the node itself.</param>
public sealed record ScriptSidecarNodeRef(
    [property: JsonPropertyName("node")] uint Node,
    [property: JsonPropertyName("path"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] IReadOnlyList<string>? Path = null);

#pragma warning restore SA1402 // File may only contain a single type
