// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Text.Json.Serialization;
using Oxygen.Core;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Represents a component of a scene node, such as transform, geometry, material, etc.
/// Concrete components implement instance-level <c>Hydrate</c>/<c>Dehydrate</c>.
/// </summary>
[JsonDerivedType(typeof(GeometryComponent), "Geometry")]
[JsonDerivedType(typeof(TransformComponent), "Transform")]
[JsonDerivedType(typeof(GameComponent), "Base")]
public abstract partial class GameComponent : ScopedObservableObject, INamed, IPersistent<ComponentData>
{
    private static readonly ConcurrentDictionary<Type, Func<ComponentData, GameComponent>> Factories
        = new();

    /// <summary>
    /// Gets or sets the name of the component.
    /// </summary>
    public required string Name { get; set; }

    /// <summary>
    /// Gets the owner scene node of this component.
    /// Set by the owning <see cref="SceneNode"/> when components are added.
    /// </summary>
    [JsonIgnore]
    public SceneNode? Node { get; internal set; }

    /// <summary>
    /// Gets a value indicating whether this component is locked (read-only) in the editor.
    /// Concrete component types can override this to report that they cannot be deleted.
    /// Default is <c>false</c>.
    /// </summary>
    [JsonIgnore]
    public virtual bool IsLocked => false;

    /// <summary>
    /// Create and hydrate a game component from a DTO.
    /// This method selects the concrete component type for the given DTO and
    /// returns a hydrated instance. Keep the mapping small and explicit here
    /// to avoid scattering creation logic.
    /// </summary>
    /// <param name="data">The DTO to create and hydrate from.</param>
    /// <returns>The created and hydrated component.</returns>
    public static GameComponent CreateAndHydrate(ComponentData data)
    {
        ArgumentNullException.ThrowIfNull(data);
        var dtoType = data.GetType();
        return Factories.TryGetValue(dtoType, out var factory)
            ? factory(data)
            : throw new InvalidOperationException($"No component factory registered for DTO type '{dtoType.FullName}'.");
    }

    /// <summary>
    /// Hydrates this component from the specified data transfer object.
    /// Concrete components should override and populate their own state.
    /// </summary>
    /// <param name="data">The data transfer object containing the state to load.</param>
    public virtual void Hydrate(ComponentData data)
    {
        using (this.SuppressNotifications())
        {
            this.Name = data.Name;
        }
    }

    /// <summary>
    /// Dehydrates this component to a data transfer object.
    /// Concrete components implement this to return their DTO representation.
    /// </summary>
    /// <returns>A data transfer object containing the current state of this component.</returns>
    public abstract ComponentData Dehydrate();

    /// <summary>
    /// Register a creator for the given DTO type. The creator receives the strongly-typed DTO
    /// and must return a hydrated component instance. Intended for use by concrete
    /// <see cref="GameComponent"/> derived types to self-register their creators.
    /// </summary>
    /// <typeparam name="TDto">The DTO type this creator handles.</typeparam>
    /// <param name="creator">Function that creates and hydrates the component from the DTO.</param>
    protected static void Register<TDto>(Func<TDto, GameComponent> creator)
        where TDto : ComponentData
    {
        ArgumentNullException.ThrowIfNull(creator);
        Factories[typeof(TDto)] = dto => creator((TDto)dto);
    }
}
