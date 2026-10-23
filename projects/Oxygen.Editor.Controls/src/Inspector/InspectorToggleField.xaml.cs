// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Controls;

/// <summary>
///     Composes a boolean property's caption and a <see cref="ToggleSwitch"/> in a <see cref="PropertyCard"/>, with the
///     mixed state of a multi-selection, the override bar and the reset button.
/// </summary>
/// <remarks>
///     A <see langword="null"/> <see cref="Value"/> is mixed: the switch shows Off with "Mixed" as its content, so the
///     first click sets On. The field never edits a model: <see cref="Value"/> is its only output, set only by a click.
/// </remarks>
public sealed partial class InspectorToggleField : UserControl
{
    /// <summary>Identifies the property caption.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), string.Empty);

    /// <summary>Identifies the value; <see langword="null"/> when the edited objects disagree.</summary>
    public static readonly DependencyProperty ValueProperty = Register<bool?>(nameof(Value), defaultValue: null, OnValueChanged);

    /// <summary>Identifies how many edited objects override the value.</summary>
    public static readonly DependencyProperty OverrideStateProperty = Register<OverrideState>(nameof(OverrideState), OverrideState.None);

    /// <summary>Identifies the command that resets the value to its source.</summary>
    public static readonly DependencyProperty ResetCommandProperty = Register<ICommand>(nameof(ResetCommand), defaultValue: null);

    /// <summary>Identifies the reset button's tooltip and automation name.</summary>
    public static readonly DependencyProperty ResetToolTipProperty = Register<string>(nameof(ResetToolTip), "Reset to the source value");

    private const string MixedText = "Mixed";

    private bool showing;

    /// <summary>Initializes a new instance of the <see cref="InspectorToggleField"/> class.</summary>
    public InspectorToggleField()
    {
        this.InitializeComponent();
        this.ShowValue();
    }

    /// <summary>Gets or sets the property caption.</summary>
    public string Label { get => (string)this.GetValue(LabelProperty); set => this.SetValue(LabelProperty, value); }

    /// <summary>Gets or sets the value; <see langword="null"/> shows the mixed state.</summary>
    public bool? Value { get => (bool?)this.GetValue(ValueProperty); set => this.SetValue(ValueProperty, value); }

    /// <summary>Gets or sets how many edited objects override the value.</summary>
    public OverrideState OverrideState { get => (OverrideState)this.GetValue(OverrideStateProperty); set => this.SetValue(OverrideStateProperty, value); }

    /// <summary>Gets or sets the command that resets the value to its source.</summary>
    public ICommand? ResetCommand { get => (ICommand?)this.GetValue(ResetCommandProperty); set => this.SetValue(ResetCommandProperty, value); }

    /// <summary>Gets or sets the reset button's tooltip, which names the source.</summary>
    public string ResetToolTip { get => (string)this.GetValue(ResetToolTipProperty); set => this.SetValue(ResetToolTipProperty, value); }

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(InspectorToggleField), new PropertyMetadata(defaultValue, changed));

    private static void OnValueChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((InspectorToggleField)sender).ShowValue();

    private void ShowValue()
    {
        this.showing = true;
        try
        {
            this.Switch.IsOn = this.Value == true;
            this.Switch.OffContent = this.Value is null ? MixedText : string.Empty;
        }
        finally
        {
            this.showing = false;
        }
    }

    private void OnToggled(object sender, RoutedEventArgs args)
    {
        if (!this.showing)
        {
            this.Value = this.Switch.IsOn;
        }
    }
}
