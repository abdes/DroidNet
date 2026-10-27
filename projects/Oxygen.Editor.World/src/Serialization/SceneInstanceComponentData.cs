// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for <see cref="SceneInstanceComponent"/>. A member it does not declare rejects the scene instead
/// of being dropped.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public record SceneInstanceComponentData : ComponentData
{
    /// <summary>
    /// Gets or initializes the instanced source: a model's source file or an authored scene.
    /// </summary>
    public required AssetPath Source { get; init; }

    /// <summary>
    /// Gets or initializes the changes, one entry per changed node, or <see langword="null"/> when there are none.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IList<InstancedNodeChangesData>? Changes { get; init; }
}
