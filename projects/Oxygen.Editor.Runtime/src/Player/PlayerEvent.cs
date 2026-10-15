// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;

namespace Oxygen.Editor.Runtime.Player;

/// <summary>
/// One event the player published to a topic the client subscribed to, as <c>oxygen.player-events.schema.json</c>
/// and the control server's README define it.
/// </summary>
/// <param name="Sequence">The event's number, increasing by one per event across all topics.</param>
/// <param name="Topic">The topic: <c>lifecycle</c>, <c>log</c>, <c>diagnostic</c> and others.</param>
/// <param name="Data">The topic's data.</param>
public sealed record PlayerEvent(long Sequence, string Topic, JsonObject Data)
{
    /// <summary>The lifecycle topic.</summary>
    public const string LifecycleTopic = "lifecycle";

    /// <summary>The log topic.</summary>
    public const string LogTopic = "log";

    /// <summary>The script diagnostic topic.</summary>
    public const string DiagnosticTopic = "diagnostic";

    /// <summary>The topic of frame statistics: <c>fps</c>, published a few times a second while subscribed.</summary>
    public const string StatsTopic = "stats";

    /// <summary>Gets a string field of the data.</summary>
    /// <param name="name">The field name.</param>
    /// <returns>The value, or an empty string when the field is missing or not a string.</returns>
    public string GetString(string name)
        => this.Data[name] is JsonValue value && value.TryGetValue<string>(out var text) ? text : string.Empty;
}
