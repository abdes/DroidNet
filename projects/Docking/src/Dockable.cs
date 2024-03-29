// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.ComponentModel;
using System.Runtime.CompilerServices;
using DroidNet.Docking.Detail;

/// <summary>
/// Represents content (a <see cref="ViewModel" />) that can be docked in a <see cref="DockGroup" />.
/// </summary>
public partial class Dockable : INotifyPropertyChanged, IDockable
{
    private bool isActive;
    private string? title;

    private string? minimizedTitle;

    private string? tabbedTitle;

    /// <summary>
    /// Initializes a new instance of the <see cref="Dockable" /> class.
    /// </summary>
    /// <param name="id">A unique identifier for this <see cref="Dockable" />.</param>
    protected Dockable(string id) => this.Id = id;

    public event PropertyChangedEventHandler? PropertyChanged;

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

    public string Id { get; }

    public string MinimizedTitle
    {
        get => this.minimizedTitle ?? this.Title;
        set => this.minimizedTitle = value;
    }

    public string TabbedTitle
    {
        get => this.tabbedTitle ?? this.Title;
        set => this.tabbedTitle = value;
    }

    public Width PreferredWidth { get; set; } = new();

    public Height PreferredHeight { get; set; } = new();

    public object? ViewModel { get; set; }

    public IDock? Owner { get; set; }

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
    public override string ToString()
        => $"[{this.Id}] {(this.Owner == null ? string.Empty : $"Owner={this.Owner.Id} ")}";

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
