// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace Oxygen.Editor.Projects;

/// <summary>
/// The base class for any game object. Exposes a <see cref="Name" /> property and provides the required boilerplate for
/// implementing <see cref="INotifyPropertyChanged" /> for observable properties.
/// </summary>
/// <remarks>
/// The <see cref="GameObject"/> class serves as a base class for all game objects, providing common functionality such as
/// property change notification and value validation. It includes a <see cref="Name"/> property that must be set and validated.
/// </remarks>
public partial class GameObject : INotifyPropertyChanged, INotifyPropertyChanging
{
    private string name;

    /// <summary>
    /// Delegate for validating property values.
    /// </summary>
    /// <param name="value">The value to validate.</param>
    protected delegate void ValidateValueCallback(object? value);

    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc/>
    public event PropertyChangingEventHandler? PropertyChanging;

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
    /// Raises the <see cref="PropertyChanging"/> event.
    /// </summary>
    /// <param name="propertyName">The name of the property that is changing.</param>
    protected virtual void OnPropertyChanging([CallerMemberName] string? propertyName = null)
        => this.PropertyChanging?.Invoke(this, new PropertyChangingEventArgs(propertyName));

    /// <summary>
    /// Raises the <see cref="PropertyChanged"/> event.
    /// </summary>
    /// <param name="propertyName">The name of the property that changed.</param>
    protected virtual void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    /// <summary>
    /// Sets the field and raises the <see cref="PropertyChanged"/> event if the value has changed.
    /// </summary>
    /// <typeparam name="T">The type of the field.</typeparam>
    /// <param name="field">The field to set.</param>
    /// <param name="value">The new value.</param>
    /// <param name="propertyName">The name of the property that changed.</param>
    /// <returns><see langword="true"/> if the value has changed; otherwise, <see langword="false"/>.</returns>
    protected bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        this.OnPropertyChanging(propertyName);
        field = value;
        this.OnPropertyChanged(propertyName);
        return true;
    }

    /// <summary>
    /// Validates and sets the field, and raises the <see cref="PropertyChanged"/> event if the value has changed.
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
        [CallerMemberName]
        string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        validator.Invoke(value);

        field = value;
        this.OnPropertyChanged(propertyName);
        return true;
    }
}
