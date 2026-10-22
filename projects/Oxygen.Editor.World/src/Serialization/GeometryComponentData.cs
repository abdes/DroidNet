// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for <see cref="GeometryComponent"/>. A member it does not declare, such as a removed override
/// slot, rejects the scene instead of being dropped.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public record GeometryComponentData : ComponentData
{
    /// <summary>
    /// Gets or initializes the geometry asset URI.
    /// </summary>
    public required AssetPath GeometryUri { get; init; }

    /// <summary>
    /// Gets or sets the values the scene stores over the geometry asset's, or <see langword="null"/> when there are none.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public SourceOverridesData? Overrides { get; set; }
}
