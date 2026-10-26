// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>A setting row shown as a toggle, Mixed while the files' values differ. The toggle may stand for a setting
/// that is not itself a boolean, or for its inverse: Keep node pivots is <c>bake_transforms</c> off, Drop empty nodes
/// is <c>node_pruning</c> <c>drop-empty</c>.</summary>
public sealed partial class ToggleRow : SettingRow
{
    private readonly Func<bool, object> toValue;
    private readonly Func<object?, bool?> fromValue;

    /// <summary>Initializes a new instance of the <see cref="ToggleRow"/> class.</summary>
    /// <param name="label">The row's label.</param>
    /// <param name="setting">The setting it edits.</param>
    /// <param name="toValue">The setting's value for the toggle's state; null for the setting's own boolean.</param>
    /// <param name="fromValue">The toggle's state for a setting's value; null for the setting's own boolean.</param>
    /// <param name="qualifier">What the row says when it edits only some of the selected files.</param>
    public ToggleRow(string label, ModelImportSetting setting, Func<bool, object>? toValue = null, Func<object?, bool?>? fromValue = null, string qualifier = "")
        : base(label, setting, qualifier, appliesTo: null)
    {
        this.toValue = toValue ?? (static state => state);
        this.fromValue = fromValue ?? (static value => value as bool?);
    }

    /// <summary>Gets or sets the toggle's state; null while the files' values differ.</summary>
    [ObservableProperty]
    public partial bool? Value { get; set; }

    /// <inheritdoc />
    protected override void OnRefreshed(object? value) => this.Value = this.IsMixed ? null : this.fromValue(value);

    partial void OnValueChanged(bool? value)
    {
        if (value is { } state)
        {
            this.Apply(this.toValue(state));
        }
    }
}
