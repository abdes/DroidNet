// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for a scene node: its identity and its components. The name, hierarchy, transform and node
/// flags are the required <see cref="MetadataData"/>, <see cref="HierarchyData"/>, <see cref="TransformData"/> and
/// <see cref="NodeFlagsData"/> entries, exactly one each. A member it does not declare, such as a removed node field,
/// rejects the scene instead of being dropped.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public record SceneNodeData
{
    /// <summary>
    /// Gets or initializes the identity of the node.
    /// </summary>
    [JsonPropertyOrder(-1)]
    public required NodeId Id { get; init; }

    /// <summary>
    /// Gets or sets the node's components, required ones included.
    /// </summary>
    public IList<ComponentData> Components { get; set; } = [];

    /// <summary>
    /// Gets the node's name, held by its metadata component.
    /// </summary>
    /// <exception cref="JsonException">The metadata component is missing or repeated.</exception>
    [JsonIgnore]
    public string Name => this.Required<MetadataData>().Name;

    /// <summary>
    /// Gets the node's ordered children, nested in its hierarchy component.
    /// </summary>
    /// <exception cref="JsonException">The hierarchy component is missing or repeated.</exception>
    [JsonIgnore]
    public IList<SceneNodeData> Children => this.Required<HierarchyData>().Children ?? [];

    /// <summary>
    /// Gets the node's one component of a required kind.
    /// </summary>
    /// <typeparam name="TData">The required component's data type.</typeparam>
    /// <returns>The component data.</returns>
    /// <exception cref="JsonException">The component is missing or repeated.</exception>
    public TData Required<TData>()
        where TData : ComponentData
    {
        TData? found = null;
        foreach (var component in this.Components.OfType<TData>())
        {
            if (found is not null)
            {
                throw new JsonException($"Node {this.Id} has more than one {typeof(TData).Name} component.");
            }

            found = component;
        }

        return found ?? throw new JsonException($"Node {this.Id} has no {typeof(TData).Name} component.");
    }
}
