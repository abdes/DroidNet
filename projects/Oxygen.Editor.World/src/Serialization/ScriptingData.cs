// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

#pragma warning disable SA1402 // File may only contain a single type

/// <summary>Persisted state of a node's scripting component: its script slots, in order.</summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record ScriptingData : ComponentData
{
    /// <summary>Gets the slots; list order is the slot position.</summary>
    public IList<ScriptSlotData> Slots { get; init; } = [];
}

/// <summary>Persisted state of one script slot.</summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record ScriptSlotData
{
    /// <summary>Gets the stable slot identity.</summary>
    public Guid Id { get; init; }

    /// <summary>Gets the script asset identity, if one is picked.</summary>
    public Uri? Script { get; init; }

    /// <summary>Gets the execution order.</summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public int ExecutionOrder { get; init; }

    /// <summary>Gets the parameter values that differ from the declared defaults.</summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IDictionary<string, ScriptValueData>? Overrides { get; init; }
}

/// <summary>Persisted script parameter value.</summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record ScriptValueData
{
    /// <summary>Gets the value type.</summary>
    public required string Type { get; init; }

    /// <summary>Gets the value.</summary>
    public required JsonElement Value { get; init; }
}

#pragma warning restore SA1402 // File may only contain a single type
