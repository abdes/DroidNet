// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>A setting row whose value is one of a few choices, shown as a combo box with Mixed as its placeholder.</summary>
public sealed partial class ChoiceRow : SettingRow
{
    /// <summary>Initializes a new instance of the <see cref="ChoiceRow"/> class.</summary>
    /// <param name="label">The row's label.</param>
    /// <param name="setting">The setting it edits.</param>
    /// <param name="options">The choices the row offers, in order.</param>
    /// <param name="qualifier">What the row says when it edits only some of the selected files.</param>
    /// <param name="appliesTo">Whether the row applies to a file beyond the setting's formats; null for always.</param>
    public ChoiceRow(string label, ModelImportSetting setting, IReadOnlyList<ChoiceOption> options, string qualifier = "", Func<ModelImportFile, bool>? appliesTo = null)
        : base(label, setting, qualifier, appliesTo)
    {
        this.Options = options;
    }

    /// <summary>Gets or sets the choices the row offers.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<ChoiceOption> Options { get; set; }

    /// <summary>Gets or sets the chosen option; null while the files' values are mixed or are none of the options.</summary>
    [ObservableProperty]
    public partial ChoiceOption? SelectedOption { get; set; }

    /// <summary>Gets the combo box's placeholder: Mixed while the files' values differ.</summary>
    public string PlaceholderText => this.IsMixed ? "Mixed" : string.Empty;

    /// <inheritdoc />
    protected override void OnRefreshed(object? value)
    {
        this.SelectedOption = this.IsMixed ? null : this.Options.FirstOrDefault(option => Equals(option.Value, value));
        this.OnPropertyChanged(nameof(this.PlaceholderText));
    }

    partial void OnSelectedOptionChanged(ChoiceOption? value)
    {
        if (value is not null)
        {
            this.Apply(value.Value);
        }
    }
}
