// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Controls;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>One execution setting of an import run: unset, it shows the ImportTool's default; set, it carries the
/// override bar and a reset.</summary>
public sealed partial class RunValue : ObservableObject
{
    /// <summary>The largest value a field accepts; the schema states only a minimum of one.</summary>
    internal const int Maximum = 4096;

    /// <summary>Initializes a new instance of the <see cref="RunValue"/> class.</summary>
    /// <param name="label">The value's name, such as Threads.</param>
    /// <param name="defaultValue">The ImportTool's default.</param>
    public RunValue(string label, int defaultValue)
    {
        this.Label = label;
        this.Default = defaultValue;
    }

    /// <summary>Gets the value's name.</summary>
    public string Label { get; }

    /// <summary>Gets the ImportTool's default.</summary>
    public int Default { get; }

    /// <summary>Gets or sets the value the run sets, or <see langword="null"/> for the default.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(NumberValue), nameof(IsSet), nameof(OverrideState))]
    [NotifyCanExecuteChangedFor(nameof(ResetCommand))]
    public partial int? Value { get; set; }

    /// <summary>Gets a value indicating whether the run sets this value.</summary>
    public bool IsSet => this.Value is not null;

    /// <summary>Gets the field's override bar: full when the run sets the value.</summary>
    public OverrideState OverrideState => this.IsSet ? OverrideState.Full : OverrideState.None;

    /// <summary>Gets the text that states the default, shown as the field's tooltip.</summary>
    public string DefaultText => string.Create(CultureInfo.CurrentCulture, $"Default: {this.Default}");

    /// <summary>Gets or sets the shown number: the set value, or the default. Editing it sets the value.</summary>
    public double NumberValue
    {
        get => this.Value ?? this.Default;
        set
        {
            if (double.IsFinite(value))
            {
                this.Value = Math.Clamp((int)Math.Round(value), 1, Maximum);
            }
        }
    }

    [RelayCommand(CanExecute = nameof(IsSet))]
    private void Reset() => this.Value = null;
}
