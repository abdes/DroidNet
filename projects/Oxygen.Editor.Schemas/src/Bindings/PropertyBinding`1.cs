// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Schemas.Bindings;

/// <summary>
/// A multi-selection-aware property binding for inspector views.
/// </summary>
/// <typeparam name="T">The value type.</typeparam>
/// <remarks>
/// <para>
/// Holds a single observable surface (<see cref="Value"/> and
/// <see cref="IsMixed"/>) together with the descriptor that drives
/// validation and the typed property id. The value folds the property of
/// every target the selection edits.
/// </para>
/// <para>
/// Setting <see cref="Value"/> from the view raises a
/// <see cref="ValueRequested"/> event; the binding does not apply to the
/// model directly. The owning component routes the event to the edit
/// that changes the model.
/// </para>
/// <para>
/// "Mixed" is a state of the binding, not two parallel observable
/// fields. The view can render a placeholder by checking <see cref="IsMixed"/>.
/// </para>
/// </remarks>
public class PropertyBinding<T> : INotifyPropertyChanged
{
    private readonly PropertyDescriptor<T> descriptor;
    private T value = default!;
    private bool hasValue;
    private bool isMixed;

    /// <summary>
    /// Initializes a new instance of the <see cref="PropertyBinding{T}"/> class.
    /// </summary>
    /// <param name="descriptor">The descriptor.</param>
    public PropertyBinding(PropertyDescriptor<T> descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        this.descriptor = descriptor;
    }

    /// <inheritdoc />
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Raised when the view writes a new value. The handler is expected
    /// to validate, build a <see cref="PropertyEdit"/>, and route it to
    /// the edit that changes the model.
    /// </summary>
    public event EventHandler<PropertyBindingChangedEventArgs<T>>? ValueRequested;

    /// <summary>
    /// Gets the descriptor.
    /// </summary>
    public PropertyDescriptor<T> Descriptor => this.descriptor;

    /// <summary>
    /// Gets the typed property id.
    /// </summary>
    public PropertyId<T> Id => this.descriptor.TypedId;

    /// <summary>
    /// Gets a value indicating whether the binding currently has any
    /// contributing nodes.
    /// </summary>
    public bool HasValue
    {
        get => this.hasValue;
        private set
        {
            if (this.hasValue == value)
            {
                return;
            }

            this.hasValue = value;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.HasValue)));
        }
    }

    /// <summary>
    /// Gets a value indicating whether the contributing nodes disagree on
    /// the value (the inspector should show an indeterminate placeholder).
    /// </summary>
    public bool IsMixed
    {
        get => this.isMixed;
        private set
        {
            if (this.isMixed == value)
            {
                return;
            }

            this.isMixed = value;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.IsMixed)));
        }
    }

    /// <summary>
    /// Gets or sets the value. Setting this from the view raises
    /// <see cref="ValueRequested"/>; the model is not updated until the
    /// event handler completes (and the handler is expected to call
    /// <see cref="UpdateFromModel"/> with the post-edit values).
    /// </summary>
    public T Value
    {
        get => this.value;
        set
        {
            // Keep the displayed value optimistic-by-default. The handler
            // can call UpdateFromModel to override on validation failure.
            var previous = this.value;
            this.value = value;
            this.IsMixed = false;
            this.OnValueChanged();
            this.ValueRequested?.Invoke(
                this,
                new PropertyBindingChangedEventArgs<T>(previous, value));
        }
    }

    /// <summary>
    /// Recomputes the value / mixed-value state by reading the property off
    /// the model target of each edited item.
    /// </summary>
    /// <param name="targets">The model target of each edited item (e.g. a
    /// transform component instance); <see langword="null"/> targets are
    /// skipped.</param>
    public void UpdateFromModel(IEnumerable<object?> targets)
    {
        ArgumentNullException.ThrowIfNull(targets);

        var present = targets.OfType<object>().ToList();
        if (present.Count == 0)
        {
            this.HasValue = false;
            this.IsMixed = false;
            this.value = default!;
            this.OnValueChanged();
            return;
        }

        var folded = MixedValue<T>.Fold(present.Select(this.descriptor.Read));
        this.HasValue = folded.HasValue;
        this.IsMixed = folded.IsMixed;
        if (!Equals(this.value, folded.Value))
        {
            this.value = folded.Value;
            this.OnValueChanged();
        }
    }

    /// <summary>Raises <see cref="PropertyChanged"/> for <see cref="Value"/> and the properties derived from it.</summary>
    protected virtual void OnValueChanged()
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.Value)));

    /// <summary>Raises <see cref="PropertyChanged"/> for a property.</summary>
    /// <param name="propertyName">The changed property.</param>
    protected void OnPropertyChanged(string propertyName)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

}

/// <summary>
/// Event payload for <see cref="PropertyBinding{T}.ValueRequested"/>.
/// </summary>
/// <typeparam name="T">The value type.</typeparam>
/// <param name="oldValue">The value before the assignment.</param>
/// <param name="newValue">The requested new value.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "tightly linked types")]
public sealed class PropertyBindingChangedEventArgs<T>(T oldValue, T newValue) : EventArgs
{
    /// <summary>Gets the value before the assignment.</summary>
    public T OldValue { get; } = oldValue;

    /// <summary>Gets the requested new value.</summary>
    public T NewValue { get; } = newValue;
}
