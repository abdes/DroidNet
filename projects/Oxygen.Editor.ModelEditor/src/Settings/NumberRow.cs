// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>A setting row whose value is a number, shown as a number field with its unit or prefix, validated against
/// the setting's range and indeterminate while the files' values differ; a whole-number setting takes whole numbers
/// only.</summary>
public sealed partial class NumberRow : SettingRow
{
    /// <summary>Initializes a new instance of the <see cref="NumberRow"/> class.</summary>
    /// <param name="label">The row's label.</param>
    /// <param name="setting">The setting it edits.</param>
    /// <param name="unit">The unit shown after the value, such as °; empty for none.</param>
    /// <param name="prefix">The prefix shown before the value, such as ×; empty for none.</param>
    /// <param name="qualifier">What the row says when it edits only some of the selected files.</param>
    /// <param name="appliesTo">Whether the row applies to a file beyond the setting's formats; null for always.</param>
    public NumberRow(string label, ModelImportSetting setting, string unit = "", string prefix = "", string qualifier = "", Func<ModelImportFile, bool>? appliesTo = null)
        : base(label, setting, qualifier, appliesTo)
    {
        this.Unit = unit;
        this.Prefix = prefix;
    }

    /// <summary>Gets the unit shown after the value.</summary>
    public string Unit { get; }

    /// <summary>Gets the prefix shown before the value.</summary>
    public string Prefix { get; }

    /// <summary>Gets the number field's mask: whole numbers for a whole-number setting, else up to three decimals.</summary>
    public string Mask => this.IsWholeNumber ? "#." : "~.###";

    private bool IsWholeNumber => this.Setting.Kind == ModelImportSettingKind.WholeNumber;

    /// <summary>Gets or sets the shown value; an accepted edit applies it to every file the row edits.</summary>
    [ObservableProperty]
    public partial double NumberValue { get; set; }

    /// <summary>Gets or sets why the last edit was refused; empty when it was accepted.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError))]
    public partial string ErrorText { get; set; } = string.Empty;

    /// <summary>Gets a value indicating whether the last edit was refused.</summary>
    public bool HasError => this.ErrorText.Length != 0;

    /// <summary>Gets why <paramref name="value"/> is refused.</summary>
    /// <param name="value">A value typed into the field.</param>
    /// <returns>The error text, or empty when the setting accepts the value.</returns>
    public string Check(double value)
    {
        if (this.ToSettingValue(value) is { } setting && this.Setting.Accepts(setting))
        {
            return string.Empty;
        }

        var minimum = this.Setting.Minimum?.ToString(CultureInfo.CurrentCulture);
        var maximum = this.Setting.Maximum?.ToString(CultureInfo.CurrentCulture);
        var kind = this.IsWholeNumber ? "a whole number" : "a value";
        return (minimum, maximum, this.Setting.IsMinimumExclusive) switch
        {
            ({ } low, { } high, _) => $"Enter {kind} from {low} to {high}.",
            ({ } low, null, true) => $"Enter {kind} greater than {low}.",
            ({ } low, null, false) => $"Enter {kind} of at least {low}.",
            _ => this.IsWholeNumber ? "Enter a whole number." : "Enter a number.",
        };
    }

    /// <inheritdoc />
    protected override void OnRefreshed(object? value)
    {
        this.ErrorText = string.Empty;
        this.NumberValue = value switch
        {
            double number => number,
            int number => number,
            _ => 0,
        };
    }

    partial void OnNumberValueChanged(double value)
    {
        if (this.IsRefreshing)
        {
            return;
        }

        this.ErrorText = this.Check(value);
        if (!this.HasError)
        {
            this.Apply(this.ToSettingValue(value));
        }
    }

    // The setting's value of a typed number: the number itself, or for a whole-number setting the integer it equals,
    // null when it equals none.
    private object? ToSettingValue(double value)
        => !this.IsWholeNumber ? value
            : double.IsInteger(value) && value is >= int.MinValue and <= int.MaxValue ? (int)value : null;
}
