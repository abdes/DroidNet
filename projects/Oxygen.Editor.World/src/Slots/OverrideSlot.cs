// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using Oxygen.Core;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Slots;

/// <summary>
/// Base class for all override slots.
/// </summary>
/// <remarks>
/// Override slots are observable containers for <see cref="World.OverridableProperty{T}"/> values.
/// They can be attached to <see cref="GameObject"/> (global scope), <see cref="GameComponent"/> (component scope),
/// or <see cref="GeometryOverrideTarget"/> (LOD/submesh scope) to override rendering properties.
/// </remarks>
public abstract class OverrideSlot : ScopedObservableObject, IPersistent<OverrideSlotData>
{
    private static readonly ConcurrentDictionary<Type, Func<OverrideSlotData, OverrideSlot>> Factories
        = new();

    /// <summary>
    /// Create and hydrate an <see cref="OverrideSlot"/> instance from a DTO.
    /// </summary>
    /// <param name="data">The DTO to create from.</param>
    /// <returns>The created and hydrated <see cref="OverrideSlot"/>.</returns>
    public static OverrideSlot CreateAndHydrate(OverrideSlotData data)
    {
        ArgumentNullException.ThrowIfNull(data);
        var dtoType = data.GetType();
        if (Factories.TryGetValue(dtoType, out var factory))
        {
            var slot = factory(data);
            slot.Hydrate(data);
            return slot;
        }

        throw new InvalidOperationException($"No override slot factory registered for DTO type '{dtoType.FullName}'.");
    }

    /// <summary>
    /// Hydrate this slot from DTO. Default implementation does nothing; concrete types override.
    /// </summary>
    /// <param name="data">The DTO containing state to copy into this slot.</param>
    public virtual void Hydrate(OverrideSlotData data)
    {
    }

    /// <summary>
    /// Dehydrate this slot into a DTO. Concrete types must implement.
    /// </summary>
    /// <returns>A DTO representing the persisted state for this slot.</returns>
    public abstract OverrideSlotData Dehydrate();

    /// <summary>
    /// Register a creator for the given DTO type.
    /// This mirrors the GameComponent pattern and keeps creation centralized.
    /// </summary>
    /// <typeparam name="TDto">The DTO type being registered.</typeparam>
    /// <param name="creator">Function that creates an override slot from the DTO.</param>
    protected static void Register<TDto>(Func<TDto, OverrideSlot> creator)
        where TDto : OverrideSlotData
    {
        ArgumentNullException.ThrowIfNull(creator);
        Factories[typeof(TDto)] = dto => creator((TDto)dto);
    }
}
