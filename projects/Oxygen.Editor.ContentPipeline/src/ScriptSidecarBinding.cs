// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

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
/// One parameter value of a script slot.
/// </summary>
/// <param name="Key">The parameter name.</param>
/// <param name="Type">The type: <c>bool</c>, <c>int32</c>, <c>float</c>, <c>string</c>, <c>vec2</c>, <c>vec3</c>,
/// <c>vec4</c>, <c>asset</c> or <c>node</c>.</param>
/// <param name="Value">The value: a boolean, an integer, a number, a string, an array of numbers, an asset's virtual
/// path, or a node index.</param>
public sealed record ScriptSidecarParam(
    [property: JsonPropertyName("key")] string Key,
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("value")] object Value);
