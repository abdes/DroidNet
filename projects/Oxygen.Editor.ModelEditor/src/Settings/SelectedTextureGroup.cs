// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>
/// The Selected texture group of the Model Editor's import settings (model editor 5.3): the textures the selected mesh's
/// materials use, and the settings of the one chosen, each an override of the model's Textures group.
/// </summary>
/// <remarks>The group shows only in the Model Editor, while the selected mesh's materials use a texture.</remarks>
public sealed partial class SelectedTextureGroup : ObservableObject
{
    private IReadOnlyList<ModelImportFile> files = [];

    /// <summary>Initializes a new instance of the <see cref="SelectedTextureGroup"/> class.</summary>
    /// <param name="sizes">The maximum size choices, as the Textures group offers them.</param>
    /// <param name="qualities">The compression quality choices, as the Textures group offers them.</param>
    public SelectedTextureGroup(IReadOnlyList<ChoiceOption> sizes, IReadOnlyList<ChoiceOption> qualities)
    {
        this.MaxTextureSize = new("Maximum size", ModelImportSettingCatalog.MaxTextureSize, sizes);
        this.CompressionQuality = new("Compression quality", ModelImportSettingCatalog.Bc7Quality, qualities);
        this.FlipNormalGreen = new("Flip normal map green", ModelImportSettingCatalog.FlipNormalGreen);
        foreach (var row in this.Rows)
        {
            row.Edited += (_, _) =>
            {
                this.Refresh(this.files);
                this.Edited?.Invoke(this, new SettingEditedEventArgs($"Edit {row.Label} of {this.Texture}"));
            };
        }
    }

    /// <summary>Occurs when a row changed the chosen texture's settings.</summary>
    public event EventHandler<SettingEditedEventArgs>? Edited;

    /// <summary>Gets the chosen texture's largest stored size.</summary>
    public ChoiceRow MaxTextureSize { get; }

    /// <summary>Gets the chosen texture's BC7 compression quality.</summary>
    public ChoiceRow CompressionQuality { get; }

    /// <summary>Gets whether the chosen texture flips its green channel.</summary>
    public ToggleRow FlipNormalGreen { get; }

    /// <summary>Gets the textures the selected mesh's materials use, by the names the import gives them.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsVisible))]
    public partial IReadOnlyList<string> Textures { get; private set; } = [];

    /// <summary>Gets or sets the chosen texture, whose settings the rows edit.</summary>
    [ObservableProperty]
    public partial string? Texture { get; set; }

    /// <summary>Gets a value indicating whether the group shows: the selected mesh's materials use a texture.</summary>
    public bool IsVisible => this.Textures.Count != 0;

    private IEnumerable<SettingRow> Rows => [this.MaxTextureSize, this.CompressionQuality, this.FlipNormalGreen];

    /// <summary>Offers the textures of the selected mesh; the chosen one stays while it is offered.</summary>
    /// <param name="textures">The textures, by name; empty when no mesh is selected or its materials use none.</param>
    public void Show(IReadOnlyList<string> textures)
    {
        ArgumentNullException.ThrowIfNull(textures);
        this.Textures = [.. textures];
        this.Texture = this.Texture is { } chosen && this.Textures.Contains(chosen, StringComparer.Ordinal) ? chosen : this.Textures.FirstOrDefault();
        this.Refresh(this.files);
    }

    /// <summary>Fills the rows from the selected files' overrides of the chosen texture.</summary>
    /// <param name="selection">The selected files.</param>
    public void Refresh(IReadOnlyList<ModelImportFile> selection)
    {
        ArgumentNullException.ThrowIfNull(selection);
        this.files = selection;
        if (this.Texture is not { } texture)
        {
            return;
        }

        var scope = ModelImportSettingScope.Texture(texture);
        foreach (var row in this.Rows)
        {
            row.Scope = scope;
            row.Refresh(selection);
        }
    }

    partial void OnTextureChanged(string? value) => this.Refresh(this.files);
}
