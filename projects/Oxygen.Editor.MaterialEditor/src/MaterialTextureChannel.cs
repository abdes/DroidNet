// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>A named cooked texture asset offered by the material assignment picker.</summary>
public sealed record MaterialTextureChoice(string? VirtualPath, string DisplayName, bool IsAvailable);

/// <summary>An editable texture slot and its available assignments.</summary>
public sealed partial class MaterialTextureChannel : ObservableObject
{
    private readonly Action<MaterialTextureChannel, string?> selectionChanged;
    private string? selectedVirtualPath;
    private IReadOnlyList<MaterialTextureChoice> choices = [new(null, "(None)", true)];

    /// <summary>Initializes a texture channel row.</summary>
    /// <param name="channel">The native material schema field.</param>
    /// <param name="displayName">The user-facing channel name.</param>
    /// <param name="selectionChanged">The owning document edit callback.</param>
    public MaterialTextureChannel(string channel, string displayName, Action<MaterialTextureChannel, string?> selectionChanged)
    {
        this.Channel = channel;
        this.DisplayName = displayName;
        this.selectionChanged = selectionChanged;
    }

    /// <summary>Gets the native channel key.</summary>
    public string Channel { get; }

    /// <summary>Gets the user-facing channel name.</summary>
    public string DisplayName { get; }

    /// <summary>Gets or sets the assigned canonical virtual path.</summary>
    public string? SelectedVirtualPath
    {
        get => this.selectedVirtualPath;
        set
        {
            if (this.SetProperty(ref this.selectedVirtualPath, value))
            {
                this.OnPropertyChanged(nameof(this.WarningText));
                this.OnPropertyChanged(nameof(this.WarningVisibility));
                this.selectionChanged(this, value);
            }
        }
    }

    /// <summary>Gets the valid authored texture assets and the current unavailable assignment, if any.</summary>
    public IReadOnlyList<MaterialTextureChoice> Choices
    {
        get => this.choices;
        private set => this.SetProperty(ref this.choices, value);
    }

    /// <summary>Gets a warning for a missing or invalid assigned texture.</summary>
    public string WarningText
    {
        get
        {
            if (this.SelectedVirtualPath is null)
            {
                return string.Empty;
            }

            var current = this.Choices.FirstOrDefault(choice => string.Equals(choice.VirtualPath, this.SelectedVirtualPath, StringComparison.Ordinal));
            return current is { IsAvailable: true }
                ? string.Empty
                : $"Missing or invalid {this.DisplayName} texture: {this.SelectedVirtualPath}";
        }
    }

    /// <summary>Gets whether the warning is visible.</summary>
    public Visibility WarningVisibility => string.IsNullOrEmpty(this.WarningText) ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Refreshes choices while retaining unresolved paths so users can see and clear them.</summary>
    /// <param name="available">The current valid texture assets.</param>
    /// <param name="selectedVirtualPath">The source's current path.</param>
    public void Refresh(IReadOnlyList<MaterialTextureChoice> available, string? selectedVirtualPath)
    {
        var choices = new List<MaterialTextureChoice>(available.Count + 2) { new(null, "(None)", true) };
        choices.AddRange(available);
        if (selectedVirtualPath is not null
            && !choices.Any(choice => string.Equals(choice.VirtualPath, selectedVirtualPath, StringComparison.Ordinal)))
        {
            choices.Add(new(selectedVirtualPath, $"Missing or invalid: {selectedVirtualPath}", false));
        }

        if (!this.Choices.SequenceEqual(choices))
        {
            this.Choices = choices;
        }

        _ = this.SetProperty(ref this.selectedVirtualPath, selectedVirtualPath, nameof(this.SelectedVirtualPath));
        this.OnPropertyChanged(nameof(this.WarningText));
        this.OnPropertyChanged(nameof(this.WarningVisibility));
    }

}
