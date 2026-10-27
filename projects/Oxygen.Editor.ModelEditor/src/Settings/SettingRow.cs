// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>One row of the import settings panel, editing a setting of the selected files as the Properties pane edits
/// a multi-selection.</summary>
/// <remarks>
/// A row shows when it applies to at least one selected file and edits only those; when that is not all of them, its
/// qualifier says which. A value the files share shows as it is, values that differ are mixed, and the override bar is
/// full when every file sets the value, half when only some do. An edit or a reset applies to every file the row edits.
/// </remarks>
public abstract partial class SettingRow : ObservableObject
{
    private readonly Func<ModelImportFile, bool> appliesTo;
    private readonly string qualifier;
    private IReadOnlyList<ModelImportFile> files = [];

    /// <summary>Initializes a new instance of the <see cref="SettingRow"/> class.</summary>
    /// <param name="label">The row's label.</param>
    /// <param name="setting">The setting it edits.</param>
    /// <param name="qualifier">What the row says when it edits only some of the selected files, such as FBX files.</param>
    /// <param name="appliesTo">Whether the row applies to a file beyond the setting's formats; null for always.</param>
    protected SettingRow(string label, ModelImportSetting setting, string qualifier, Func<ModelImportFile, bool>? appliesTo)
    {
        this.Label = label;
        this.Setting = setting;
        this.qualifier = qualifier;
        this.appliesTo = file => setting.Formats.HasFlag(file.SourceFormat) && (appliesTo?.Invoke(file) ?? true);
    }

    /// <summary>Occurs when the row changed the settings of its files.</summary>
    public event EventHandler? Edited;

    /// <summary>Gets the row's label.</summary>
    public string Label { get; }

    /// <summary>Gets the setting the row edits.</summary>
    public ModelImportSetting Setting { get; }

    /// <summary>Gets or sets a value indicating whether the row applies to a selected file.</summary>
    [ObservableProperty]
    public partial bool IsVisible { get; set; }

    /// <summary>Gets or sets the row's qualifier: empty when it edits every selected file.</summary>
    [ObservableProperty]
    public partial string Qualifier { get; set; } = string.Empty;

    /// <summary>Gets or sets the row's override bar.</summary>
    [ObservableProperty]
    public partial OverrideState OverrideState { get; set; }

    /// <summary>Gets or sets a value indicating whether the files' values differ.</summary>
    [ObservableProperty]
    public partial bool IsMixed { get; set; }

    /// <summary>Gets the tooltip of the row's reset button.</summary>
    public string ResetToolTip => "Reset to the importer's default";

    /// <summary>Gets a value indicating whether the row is being filled from its files, when edits are not applied.</summary>
    protected bool IsRefreshing { get; private set; }

    /// <summary>Fills the row from the selected files.</summary>
    /// <param name="selection">The selected files.</param>
    public void Refresh(IReadOnlyList<ModelImportFile> selection)
    {
        ArgumentNullException.ThrowIfNull(selection);
        this.files = [.. selection.Where(this.appliesTo)];
        this.IsVisible = this.files.Count != 0;
        this.Qualifier = this.files.Count != 0 && this.files.Count < selection.Count ? this.qualifier : string.Empty;
        var values = this.files.Select(file => this.Setting.ReadFrom(file.Settings)).ToArray();
        var set = values.Count(static value => value is not null);
        this.OverrideState = set == 0 ? OverrideState.None : set == values.Length ? OverrideState.Full : OverrideState.Partial;
        var effective = values.Select(value => value ?? this.Setting.Default).Distinct().ToArray();
        this.IsMixed = effective.Length > 1;
        this.IsRefreshing = true;
        try
        {
            this.OnRefreshed(effective.Length == 1 ? effective[0] : null);
        }
        finally
        {
            this.IsRefreshing = false;
        }

        this.ResetCommand.NotifyCanExecuteChanged();
    }

    /// <summary>Shows the files' shared value.</summary>
    /// <param name="value">The value every file has, its default included; null when mixed or when the default is the
    /// file's own.</param>
    protected abstract void OnRefreshed(object? value);

    /// <summary>Sets the value of every file the row edits.</summary>
    /// <param name="value">The value, or <see langword="null"/> to remove the setting.</param>
    protected void Apply(object? value)
    {
        if (this.IsRefreshing)
        {
            return;
        }

        foreach (var file in this.files)
        {
            file.Settings = this.Setting.WriteTo(file.Settings, value);
        }

        this.Edited?.Invoke(this, EventArgs.Empty);
    }

    private bool CanReset() => this.OverrideState != OverrideState.None;

    [RelayCommand(CanExecute = nameof(CanReset))]
    private void Reset() => this.Apply(value: null);
}
