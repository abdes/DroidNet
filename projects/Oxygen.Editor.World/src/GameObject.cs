// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using Oxygen.Editor.Core;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World;

/// <summary>
/// The base class for any game object. Exposes a <see cref="Name" /> property and provides the required boilerplate for
/// implementing <see cref="System.ComponentModel.INotifyPropertyChanged" /> for observable properties.
/// </summary>
/// <remarks>
/// The <see cref="GameObject"/> class serves as a base class for all game objects, providing common functionality such as
/// property change notification and value validation. It includes a <see cref="Name"/> property that must be set and validated.
/// </remarks>
public abstract class GameObject : ScopedObservableObject, INamed
{
    private string name = string.Empty;

    /// <summary>
    /// Delegate for validating property values.
    /// </summary>
    /// <param name="value">The value to validate.</param>
    protected delegate void ValidateValueCallback(object? value);

    /// <summary>
    /// Gets the unique identifier for this game object.
    /// </summary>
    public Guid Id { get; init; } = Guid.NewGuid();

    /// <summary>
    /// Gets or sets the name of the game object.
    /// </summary>
    /// <exception cref="ArgumentException">Thrown if the name is null, empty, or consists only of white spaces.</exception>
    public required string Name
    {
        get => this.name;
        [MemberNotNull(nameof(name))]
        set => _ = this.ValidateAndSetField(ref this.name!, value, ValidateName);
    }

    /// <summary>
    /// Gets the collection of override slots for this game object.
    /// </summary>
    /// <remarks>
    /// Override slots at the GameObject level apply globally to all components.
    /// For component-specific or targeted overrides, use the slots on individual components.
    /// </remarks>
    public ObservableCollection<OverrideSlot> OverrideSlots { get; } = [];

    /// <summary>
    /// Validates the name of the game object.
    /// </summary>
    /// <param name="name">The name to validate.</param>
    /// <exception cref="ArgumentException">Thrown if the name is null, empty, or consists only of white spaces.</exception>
    public static void ValidateName(object? name)
    {
        if (string.IsNullOrWhiteSpace(name as string))
        {
            throw new ArgumentException(
                "a scene must have a name and it should not be only white spaces",
                nameof(name));
        }
    }

    /// <summary>
    /// Gets an existing override slot of the specified type, or creates and adds a new one if not found.
    /// </summary>
    /// <typeparam name="T">The type of override slot to get or create.</typeparam>
    /// <returns>The existing or newly created override slot.</returns>
    public T GetOrCreateSlot<T>()
        where T : OverrideSlot, new()
    {
        var existing = this.OverrideSlots.OfType<T>().FirstOrDefault();
        if (existing != null)
        {
            return existing;
        }

        var newSlot = new T();
        this.OverrideSlots.Add(newSlot);
        return newSlot;
    }

    /// <summary>
    /// Validates and sets the field, and raises the <see cref="ScopedObservableObject.PropertyChanged"/> event if the value has changed.
    /// </summary>
    /// <typeparam name="T">The type of the field.</typeparam>
    /// <param name="field">The field to set.</param>
    /// <param name="value">The new value.</param>
    /// <param name="validator">The validation callback to validate the value.</param>
    /// <param name="propertyName">The name of the property that changed.</param>
    /// <returns><see langword="true"/> if the value has changed; otherwise, <see langword="false"/>.</returns>
    protected bool ValidateAndSetField<T>(
        ref T field,
        T value,
        ValidateValueCallback validator,
        [System.Runtime.CompilerServices.CallerMemberName]
        string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        validator.Invoke(value);

        this.OnPropertyChanging(propertyName);
        field = value;
        this.OnPropertyChanged(propertyName);
        return true;
    }
}
