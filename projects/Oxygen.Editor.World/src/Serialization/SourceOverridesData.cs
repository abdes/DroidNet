// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

#pragma warning disable SA1402 // File may only contain a single type

/// <summary>
/// Data transfer object for <see cref="Components.SourceOverrides"/>: each value is the JSON of its property's type,
/// under the property's key.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record SourceOverridesData
{
    /// <summary>
    /// Gets or sets the overrides of the component's own properties, by key.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IDictionary<string, JsonElement>? Values { get; set; }

    /// <summary>
    /// Gets or sets the overrides of each part.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IList<PartOverridesData>? Parts { get; set; }
}

/// <summary>
/// Data transfer object for the overrides of one part.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record PartOverridesData
{
    /// <summary>
    /// Gets the part kind.
    /// </summary>
    public required string Kind { get; init; }

    /// <summary>
    /// Gets the asset-owned identity of the part.
    /// </summary>
    public required Guid Id { get; init; }

    /// <summary>
    /// Gets the revision of the part layout the part was last witnessed in.
    /// </summary>
    public required string LayoutRevision { get; init; }

    /// <summary>
    /// Gets the overridden values, by key.
    /// </summary>
    public required IDictionary<string, JsonElement> Values { get; init; }
}

#pragma warning restore SA1402 // File may only contain a single type
