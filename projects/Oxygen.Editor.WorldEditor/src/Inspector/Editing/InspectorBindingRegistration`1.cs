// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;

namespace Oxygen.Editor.World.Inspector.Editing;

/// <summary>Owns typed binding subscriptions and routes requests through the existing captured-target coordinator.</summary>
/// <typeparam name="T">The authored value type.</typeparam>
internal sealed class InspectorBindingRegistration<T> : IDisposable
{
    private readonly PropertyBinding<T> binding;
    private readonly InspectorEditSessionCoordinator? coordinator;
    private readonly Func<bool> inputEnabled;
    private readonly Action refresh;
    private readonly Action? changed;
    private bool disposed;
    private bool refreshing;

    /// <summary>Initializes a new instance of the <see cref="InspectorBindingRegistration{T}"/> class.</summary>
    /// <param name="binding">The sole value/mixed-state owner.</param>
    /// <param name="coordinator">The borrowed command/session owner.</param>
    /// <param name="inputEnabled">The current input/lifetime gate.</param>
    /// <param name="refresh">The rejected/disabled-input restoration operation.</param>
    /// <param name="changed">Optional computed-presentation notification.</param>
    internal InspectorBindingRegistration(
        PropertyBinding<T> binding,
        InspectorEditSessionCoordinator? coordinator,
        Func<bool> inputEnabled,
        Action refresh,
        Action? changed = null)
    {
        this.binding = binding;
        this.coordinator = coordinator;
        this.inputEnabled = inputEnabled;
        this.refresh = refresh;
        this.changed = changed;
        binding.ValueRequested += this.OnValueRequested;
        binding.PropertyChanged += this.OnBindingChanged;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (!this.disposed)
        {
            this.binding.ValueRequested -= this.OnValueRequested;
            this.binding.PropertyChanged -= this.OnBindingChanged;
            this.disposed = true;
        }
    }

    /// <summary>Refreshes canonical state while ignoring two-way control echoes.</summary>
    /// <param name="nodes">Captured contributing node identities.</param>
    /// <param name="targets">The current typed component lookup.</param>
    internal void Refresh(IReadOnlyList<Guid> nodes, Func<Guid, object?> targets)
    {
        var previous = this.binding.HasValue ? (object?)this.binding.Value : null;
        var wasMixed = this.binding.IsMixed;
        this.refreshing = true;
        try
        {
            this.binding.UpdateFromModel(nodes, targets);
        }
        finally
        {
            this.refreshing = false;
        }

        if (!Equals(previous, this.binding.HasValue ? this.binding.Value : null) || wasMixed != this.binding.IsMixed)
        {
            this.coordinator?.ModelChanged(this.binding.Id.Id);
        }
    }

    private void OnValueRequested(object? sender, PropertyBindingChangedEventArgs<T> args)
    {
        if (this.disposed || this.refreshing)
        {
            return;
        }

        if (!this.inputEnabled())
        {
            this.refresh();
            return;
        }

        this.coordinator?.Submit(PropertyEdit.SingleEdit(this.binding.Id, args.NewValue));
    }

    private void OnBindingChanged(object? sender, PropertyChangedEventArgs args) => this.changed?.Invoke();
}
