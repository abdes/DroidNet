// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.ComponentModel;

namespace Oxygen.Editor.Schemas.Bindings;

/// <summary>
/// A multi-selection-aware property binding for inspector views.
/// </summary>
/// <typeparam name="T">The value type.</typeparam>
/// <remarks>
/// <para>
/// Holds a single observable surface (<see cref="Value"/> and
/// <see cref="IsMixed"/>) together with the descriptor that drives
/// validation, the typed property id, and the set of nodes the binding
/// edits.
/// </para>
/// <para>
/// Setting <see cref="Value"/> from the view raises a
/// <see cref="ValueRequested"/> event; the binding does not apply to the
/// model directly. The owning component is expected to route the event
/// through a <c>CommitGroupController</c> or directly to the command
/// service.
/// </para>
/// <para>
/// "Mixed" is a state of the binding, not two parallel observable
/// fields. The view can render a placeholder by checking <see cref="IsMixed"/>.
/// </para>
/// </remarks>
public sealed class PropertyBinding<T> : INotifyPropertyChanged
{
    private readonly PropertyDescriptor<T> descriptor;
    private readonly List<Guid> nodes;
    private T value = default!;
    private bool hasValue;
    private bool isMixed;

    /// <summary>
    /// Initializes a new binding.
    /// </summary>
    /// <param name="descriptor">The descriptor.</param>
    public PropertyBinding(PropertyDescriptor<T> descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        this.descriptor = descriptor;
        this.nodes = [];
    }

    /// <inheritdoc />
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Raised when the view writes a new value. The handler is expected
    /// to validate, build a <see cref="PropertyEdit"/>, and route it
    /// through the command service.
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
    /// Gets the node ids the binding edits.
    /// </summary>
    public IReadOnlyList<Guid> Nodes => this.nodes;

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
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.Value)));
            this.ValueRequested?.Invoke(
                this,
                new PropertyBindingChangedEventArgs<T>(previous, value));
        }
    }

    /// <summary>
    /// Re-binds the contributing nodes and recomputes the value /
    /// mixed-value state by reading the property off each provided
    /// model target.
    /// </summary>
    /// <param name="nodeIds">The node ids.</param>
    /// <param name="targets">A function from node id to the model target
    /// (e.g. C# transform component instance).</param>
    public void UpdateFromModel(IReadOnlyList<Guid> nodeIds, Func<Guid, object?> targets)
    {
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(targets);

        this.nodes.Clear();
        this.nodes.AddRange(nodeIds);

        if (this.nodes.Count == 0)
        {
            this.HasValue = false;
            this.IsMixed = false;
            this.value = default!;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.Value)));
            return;
        }

        var folded = MixedValue<T>.Fold(EnumerateValues(this.nodes, targets, this.descriptor));
        this.HasValue = folded.HasValue;
        this.IsMixed = folded.IsMixed;
        if (!Equals(this.value, folded.Value))
        {
            this.value = folded.Value;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.Value)));
        }
    }

    private static IEnumerable<T> EnumerateValues(
        IReadOnlyList<Guid> nodeIds,
        Func<Guid, object?> targets,
        PropertyDescriptor<T> descriptor)
    {
        foreach (var id in nodeIds)
        {
            var target = targets(id);
            if (target is not null)
            {
                yield return descriptor.Read(target);
            }
        }
    }
}

/// <summary>
/// Event payload for <see cref="PropertyBinding{T}.ValueRequested"/>.
/// </summary>
/// <typeparam name="T">The value type.</typeparam>
/// <param name="OldValue">The value before the assignment.</param>
/// <param name="NewValue">The requested new value.</param>
public sealed record PropertyBindingChangedEventArgs<T>(T OldValue, T NewValue);
