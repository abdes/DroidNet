// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Pickers;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>An editable texture slot and the textures it can be assigned.</summary>
public sealed partial class MaterialTextureChannel : ObservableObject
{
    private static readonly AssetChoice NoTexture = new("None", Asset: null, "Remove the texture", "No texture", IsEnabled: true, Glyph: string.Empty);

    private readonly Action<MaterialTextureChannel, string?> selectionChanged;
    private IReadOnlyDictionary<string, string> namesByPath = new Dictionary<string, string>(StringComparer.Ordinal);
    private string? selectedVirtualPath;

    /// <summary>Initializes a new instance of the <see cref="MaterialTextureChannel"/> class.</summary>
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

    /// <summary>Gets the textures the channel can be assigned, grouped as Engine, Packages and Content, with a choice that removes the texture.</summary>
    public AssetPickerModel Choices { get; } = new(static asset => AssetChoice.For(asset, "Texture", "\uE7C3"), NoTexture);

    /// <summary>Gets or sets the assigned canonical virtual path.</summary>
    public string? SelectedVirtualPath
    {
        get => this.selectedVirtualPath;
        set
        {
            if (this.SetProperty(ref this.selectedVirtualPath, value))
            {
                this.OnSelectionShown();
                this.selectionChanged(this, value);
            }
        }
    }

    /// <summary>Gets the picker's text: the assigned texture's name, <c>None</c>, or the missing path.</summary>
    public string SelectedText => this.SelectedVirtualPath is not { } path
        ? "None"
        : this.namesByPath.GetValueOrDefault(path) ?? "Missing: " + path;

    /// <summary>Gets a warning for a missing or invalid assigned texture.</summary>
    public string WarningText => this.SelectedVirtualPath is { } path && !this.namesByPath.ContainsKey(path)
        ? $"Missing or invalid {this.DisplayName} texture: {path}"
        : string.Empty;

    /// <summary>Gets whether the warning is visible.</summary>
    public Visibility WarningVisibility => string.IsNullOrEmpty(this.WarningText) ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Shows the textures the channel accepts now and the source's assignment, which stays visible when unresolved so it can be replaced or removed.</summary>
    /// <param name="available">The textures the channel accepts.</param>
    /// <param name="selectedVirtualPath">The source's current path.</param>
    public void Refresh(IReadOnlyList<AssetEntry> available, string? selectedVirtualPath)
    {
        ArgumentNullException.ThrowIfNull(available);
        this.namesByPath = available
            .DistinctBy(static texture => texture.Identity.VirtualPath, StringComparer.Ordinal)
            .ToDictionary(static texture => texture.Identity.VirtualPath, static texture => texture.DisplayName, StringComparer.Ordinal);
        this.Choices.Update(available);
        _ = this.SetProperty(ref this.selectedVirtualPath, selectedVirtualPath, nameof(this.SelectedVirtualPath));
        this.OnSelectionShown();
    }

    private void OnSelectionShown()
    {
        this.OnPropertyChanged(nameof(this.SelectedText));
        this.OnPropertyChanged(nameof(this.WarningText));
        this.OnPropertyChanged(nameof(this.WarningVisibility));
    }
}
