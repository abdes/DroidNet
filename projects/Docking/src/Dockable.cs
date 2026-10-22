// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace DroidNet.Docking;

/// <summary>
/// Represents content (a <see cref="ViewModel" />) that can be docked in a <see cref="IDock">dock</see>.
/// </summary>
/// <remarks>
/// Dockables are created by <see cref="IDocker.CreateDockable{TDockable}" />, which keeps their identifiers unique in that
/// docker.
/// </remarks>
public partial class Dockable : INotifyPropertyChanged, IDockable
{
    private bool disposed;
    private bool isActive;
    private string? title;

    private string? minimizedTitle;

    private string? tabbedTitle;

    /// <summary>
    /// Initializes a new instance of the <see cref="Dockable" /> class.
    /// </summary>
    /// <param name="id">A unique identifier for this <see cref="Dockable" />.</param>
    protected Dockable(string id)
    {
        this.Id = id;
    }

    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc/>
    public event EventHandler<EventArgs>? OnDisposed;

    /// <inheritdoc/>
    public string Title
    {
        get => this.title ?? this.Id;
        set
        {
            if (!this.SetField(ref this.title, value))
            {
                return;
            }

            // Trigger property update for the other title properties but only
            // if they have a null value.
            if (this.minimizedTitle is null)
            {
                this.OnPropertyChanged(nameof(this.MinimizedTitle));
            }

            if (this.tabbedTitle is null)
            {
                this.OnPropertyChanged(nameof(this.TabbedTitle));
            }
        }
    }

    /// <inheritdoc/>
    public string Id { get; }

    /// <inheritdoc/>
    public string MinimizedTitle
    {
        get => this.minimizedTitle ?? this.Title;
        set => this.minimizedTitle = value;
    }

    /// <inheritdoc/>
    public string TabbedTitle
    {
        get => this.tabbedTitle ?? this.Title;
        set => this.tabbedTitle = value;
    }

    /// <inheritdoc/>
    public Width PreferredWidth { get; set; } = new();

    /// <inheritdoc/>
    public Height PreferredHeight { get; set; } = new();

    /// <inheritdoc/>
    public object? ViewModel { get; set; }

    /// <inheritdoc/>
    public IDock? Owner { get; set; }

    /// <summary>Gets or sets what releases this dockable from its docker, once its disposal subscribers ran.</summary>
    internal Action<Dockable>? Release { get; set; }

    /// <summary>Gets or sets a value indicating whether this dockable is active.</summary>
    /// <remarks>
    /// It is only possible to mark a dockable as active or inactive when it has a valid <see cref="Owner" />. This is to ensure
    /// that at any time, there is only one active dockable within a docker.
    /// </remarks>
    /// <exception cref="InvalidOperationException">If the dock does not have an owner.</exception>
    public bool IsActive
    {
        get => this.isActive;
        set
        {
            if (this.Owner == null)
            {
                throw new InvalidOperationException(
                    $"dockable {this} has no owner, and its active state cannot be changed");
            }

            // The owner will be notified of the property change and do the necessary to maintain a coherency.
            _ = this.SetField(ref this.isActive, value);
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public override string ToString()
        => $"[{this.Id}] {(this.Owner == null ? string.Empty : $"Owner={this.Owner.Id} ")}";

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="Dockable" /> and optionally releases
    /// the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true" /> to release both managed and unmanaged resources; <see langword="false" />
    /// to release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            // Subscribers may still need to use the dockable, and find it in its docker, before it is disposed.
            this.OnDisposed?.Invoke(this, EventArgs.Empty);
            this.Release?.Invoke(this);
            this.Release = null;
        }

        this.disposed = true;
    }

    /// <summary>Raises the PropertyChanged event for the specified property.</summary>
    /// <param name="propertyName">
    /// The name of the property that changed. This optional parameter can be automatically populated by the compiler using the
    /// CallerMemberName attribute.
    /// </param>
    protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    /// <summary>
    /// Sets the field to the given value and raises the PropertyChanged event if the new value is different from the old value.
    /// </summary>
    /// <typeparam name="T">The type of the field.</typeparam>
    /// <param name="field">A reference to the field to set.</param>
    /// <param name="value">The new value for the field.</param>
    /// <param name="propertyName">
    /// The name of the property that corresponds to the field. This optional parameter can be automatically populated by the
    /// compiler using the CallerMemberName attribute.
    /// </param>
    /// <returns>True if the field was changed; false otherwise.</returns>
    protected bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        this.OnPropertyChanged(propertyName);
        return true;
    }
}
