// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using CommunityToolkit.Mvvm.ComponentModel;

/// <summary>
/// Represents content (a <see cref="ViewModel" />) that can be docked in a <see cref="DockGroup" />.
/// </summary>
public partial class Dockable : ObservableObject, IDockable
{
    private string? title;

    private string? minimizedTitle;

    private string? tabbedTitle;

    /// <summary>
    /// Initializes a new instance of the <see cref="Dockable" /> class.
    /// </summary>
    /// <param name="id">A unique identifier for this <see cref="Dockable" />.</param>
    private Dockable(string id) => this.Id = id;

    public string Title
    {
        get => this.title ?? this.Id;
        set
        {
            if (this.title == value)
            {
                return;
            }

            if (!this.SetProperty(ref this.title, value))
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

    public IDockable.Width PreferredWidth { get; set; } = new();

    public IDockable.Height PreferredHeight { get; set; } = new();

    public object? ViewModel { get; set; }

    public IDock? Owner { get; set; }

    public bool IsActive { get; set; }
}
