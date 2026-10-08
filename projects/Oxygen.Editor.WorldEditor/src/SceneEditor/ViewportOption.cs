// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// One choice in a viewport flyout: a camera mode, an orthographic direction or a scene camera.
/// Choosing it runs its own command; the owning pane keeps <see cref="IsSelected"/> current.
/// </summary>
public sealed partial class ViewportOption : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="ViewportOption"/> class.</summary>
    /// <param name="label">The short label.</param>
    /// <param name="description">The supporting description, or <see langword="null"/>.</param>
    /// <param name="choose">Applies the choice to the pane.</param>
    public ViewportOption(string label, string? description, Func<Task> choose)
    {
        this.Label = label;
        this.Description = description;
        this.ChooseCommand = new AsyncRelayCommand(choose);
    }

    /// <summary>Gets the short label.</summary>
    public string Label { get; }

    /// <summary>Gets the supporting description, or <see langword="null"/> when the label says enough.</summary>
    public string? Description { get; }

    /// <summary>Gets a value indicating whether the option has a supporting description.</summary>
    public bool HasDescription => this.Description is not null;

    /// <summary>Gets the command that applies the choice.</summary>
    public IAsyncRelayCommand ChooseCommand { get; }

    /// <summary>Gets or sets a value indicating whether the pane currently uses this choice.</summary>
    [ObservableProperty]
    public partial bool IsSelected { get; set; }
}

/// <summary>A titled group of choices in a viewport flyout.</summary>
/// <param name="Title">The group title, or <see langword="null"/> for the leading untitled group.</param>
/// <param name="Options">The choices, in display order.</param>
public sealed record ViewportOptionGroup(string? Title, IReadOnlyList<ViewportOption> Options)
{
    /// <summary>Gets a value indicating whether the group shows a title.</summary>
    public bool HasTitle => this.Title is not null;
}
