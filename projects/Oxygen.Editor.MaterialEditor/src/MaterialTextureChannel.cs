// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Pickers;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>An editable texture slot and the textures it can be assigned.</summary>
public sealed partial class MaterialTextureChannel : ObservableObject
{
    private static readonly AssetChoice NoTexture = new("None", Asset: null, "Remove the texture", "No texture", IsEnabled: true, Glyph: string.Empty);

    private readonly Action<MaterialTextureChannel, AssetPath?> selectionChanged;
    private IReadOnlyDictionary<AssetPath, string> namesByTexture = new Dictionary<AssetPath, string>();
    private AssetPath? selectedTexture;

    /// <summary>Initializes a new instance of the <see cref="MaterialTextureChannel"/> class.</summary>
    /// <param name="channel">The native material schema field.</param>
    /// <param name="displayName">The user-facing channel name.</param>
    /// <param name="selectionChanged">The owning document edit callback.</param>
    public MaterialTextureChannel(string channel, string displayName, Action<MaterialTextureChannel, AssetPath?> selectionChanged)
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

    /// <summary>Gets or sets the assigned texture.</summary>
    public AssetPath? SelectedTexture
    {
        get => this.selectedTexture;
        set
        {
            if (this.SetProperty(ref this.selectedTexture, value))
            {
                this.OnSelectionShown();
                this.selectionChanged(this, value);
            }
        }
    }

    /// <summary>Gets the picker's text: the assigned texture's name, <c>None</c>, or the missing path.</summary>
    public string SelectedText => this.SelectedTexture is not { } texture
        ? "None"
        : this.namesByTexture.GetValueOrDefault(texture) ?? "Missing: " + texture.VirtualPath;

    /// <summary>Gets a warning for a missing or invalid assigned texture.</summary>
    public string WarningText => this.SelectedTexture is { } texture && !this.namesByTexture.ContainsKey(texture)
        ? $"Missing or invalid {this.DisplayName} texture: {texture.VirtualPath}"
        : string.Empty;

    /// <summary>Gets whether the warning is visible.</summary>
    public Visibility WarningVisibility => string.IsNullOrEmpty(this.WarningText) ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Shows the textures the channel accepts now and the source's assignment, which stays visible when unresolved so it can be replaced or removed.</summary>
    /// <param name="available">The textures the channel accepts.</param>
    /// <param name="selectedTexture">The source's current texture.</param>
    public void Refresh(IReadOnlyList<AssetEntry> available, AssetPath? selectedTexture)
    {
        ArgumentNullException.ThrowIfNull(available);
        this.namesByTexture = available
            .DistinctBy(static texture => texture.Identity)
            .ToDictionary(static texture => texture.Identity, static texture => texture.DisplayName);
        this.Choices.Update(available);
        _ = this.SetProperty(ref this.selectedTexture, selectedTexture, nameof(this.SelectedTexture));
        this.OnSelectionShown();
    }

    private void OnSelectionShown()
    {
        this.OnPropertyChanged(nameof(this.SelectedText));
        this.OnPropertyChanged(nameof(this.WarningText));
        this.OnPropertyChanged(nameof(this.WarningVisibility));
    }
}
