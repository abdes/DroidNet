// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;
using Oxygen.Editor.Pickers;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>One source material of the selected file: Imported, or a project material its surfaces use instead, which
/// the import then does not produce.</summary>
public sealed partial class MaterialRow : ObservableObject
{
    private readonly ModelImportFile file;

    /// <summary>Initializes a new instance of the <see cref="MaterialRow"/> class.</summary>
    /// <param name="file">The selected file.</param>
    /// <param name="material">The source material's name in the import.</param>
    /// <param name="choices">The project's materials, shared by every row.</param>
    public MaterialRow(ModelImportFile file, string material, AssetPickerModel choices)
    {
        this.file = file;
        this.Material = material;
        this.Choices = choices;
        this.Refresh();
    }

    /// <summary>Occurs when the row changed the file's settings.</summary>
    public event EventHandler? Edited;

    /// <summary>Gets the source material's name in the import, the row's label.</summary>
    public string Material { get; }

    /// <summary>Gets the project's materials the picker offers.</summary>
    public AssetPickerModel Choices { get; }

    /// <summary>Gets or sets the project material the source material is remapped to, or null when it is imported.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ButtonLabel), nameof(OverrideState))]
    [NotifyCanExecuteChangedFor(nameof(ResetCommand))]
    public partial string? Target { get; set; }

    /// <summary>Gets the picker button's content: the chosen material's name, or Imported.</summary>
    public string ButtonLabel
        => this.Target is null ? "Imported"
            : AssetPath.TryParse(this.Target, out var path) ? this.Choices.Find(path)?.Name ?? AssetFileTypes.RemoveSuffix(path.Name)
            : this.Target;

    /// <summary>Gets the row's override bar: full when the material is remapped.</summary>
    public OverrideState OverrideState => this.Target is null ? OverrideState.None : OverrideState.Full;

    /// <summary>Gets the tooltip of the row's reset button.</summary>
    public string ResetToolTip => "Import this material";

    /// <summary>Reads the remap from the file's settings.</summary>
    public void Refresh() => this.Target = ModelMaterialRemap.Read(this.file.Settings).GetValueOrDefault(this.Material);

    /// <summary>Remaps the material to a chosen project material, or imports it again for the picker's clearing choice.</summary>
    /// <param name="choice">The picker's choice.</param>
    public void Choose(AssetChoice choice)
    {
        ArgumentNullException.ThrowIfNull(choice);
        this.Remap(choice.Asset?.VirtualPath);
    }

    /// <summary>Remaps the material to a project material, or imports it for <see langword="null"/>.</summary>
    /// <param name="target">The project material's virtual path.</param>
    internal void Remap(string? target)
    {
        this.file.Settings = ModelMaterialRemap.WriteTo(this.file.Settings, this.Material, target);
        this.Refresh();
        this.Edited?.Invoke(this, EventArgs.Empty);
    }

    private bool CanReset() => this.Target is not null;

    [RelayCommand(CanExecute = nameof(CanReset))]
    private void Reset() => this.Remap(target: null);
}
