// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Controls.PropertyEditors;

namespace Oxygen.Editor.Controls;

/// <summary>
///     Composes a boolean property's caption and a <see cref="ToggleSwitch"/> in a <see cref="PropertyCard"/>, with the
///     mixed state of a multi-selection, the override bar and the reset button.
/// </summary>
/// <remarks>
///     A <see langword="null"/> <see cref="IsOn"/> is mixed: the switch shows Off with "Mixed" as its content, so the
///     first click sets On. The field never edits a model: <see cref="IsOn"/> is its only output, set only by a click.
/// </remarks>
[TemplatePart(Name = SwitchPartName, Type = typeof(ToggleSwitch))]
public sealed partial class ToggleField : Control
{
    /// <summary>Identifies the property caption.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), string.Empty);

    /// <summary>Identifies the value; <see langword="null"/> when the edited objects disagree.</summary>
    public static readonly DependencyProperty IsOnProperty = Register<bool?>(nameof(IsOn), defaultValue: null, OnValueChanged);

    /// <summary>Identifies how many edited objects override the value.</summary>
    public static readonly DependencyProperty OverrideStateProperty = Register<OverrideState>(nameof(OverrideState), OverrideState.None);

    /// <summary>Identifies the command that resets the value to its source.</summary>
    public static readonly DependencyProperty ResetCommandProperty = Register<ICommand>(nameof(ResetCommand), defaultValue: null);

    /// <summary>Identifies the reset button's tooltip and automation name.</summary>
    public static readonly DependencyProperty ResetToolTipProperty = Register<string>(nameof(ResetToolTip), "Reset to the source value");

    /// <summary>Identifies the source of the commands the row's context menu offers besides reset.</summary>
    public static readonly DependencyProperty OverrideActionsProperty = Register<IOverrideActionSource>(nameof(OverrideActions), defaultValue: null);

    private const string MixedText = "Mixed";
    private const string SwitchPartName = "PART_Switch";

    private ToggleSwitch? toggleSwitch;
    private bool showing;

    /// <summary>Initializes a new instance of the <see cref="ToggleField"/> class.</summary>
    public ToggleField()
    {
        this.DefaultStyleKey = typeof(ToggleField);
    }

    /// <summary>Gets or sets the property caption.</summary>
    public string Label { get => (string)this.GetValue(LabelProperty); set => this.SetValue(LabelProperty, value); }

    /// <summary>Gets or sets the value; <see langword="null"/> shows the mixed state.</summary>
    public bool? IsOn { get => (bool?)this.GetValue(IsOnProperty); set => this.SetValue(IsOnProperty, value); }

    /// <summary>Gets or sets how many edited objects override the value.</summary>
    public OverrideState OverrideState { get => (OverrideState)this.GetValue(OverrideStateProperty); set => this.SetValue(OverrideStateProperty, value); }

    /// <summary>Gets or sets the command that resets the value to its source.</summary>
    public ICommand? ResetCommand { get => (ICommand?)this.GetValue(ResetCommandProperty); set => this.SetValue(ResetCommandProperty, value); }

    /// <summary>Gets or sets the reset button's tooltip, which names the source.</summary>
    public string ResetToolTip { get => (string)this.GetValue(ResetToolTipProperty); set => this.SetValue(ResetToolTipProperty, value); }

    /// <summary>Gets or sets the source of the commands the row's context menu offers while the value is overridden.</summary>
    public IOverrideActionSource? OverrideActions { get => (IOverrideActionSource?)this.GetValue(OverrideActionsProperty); set => this.SetValue(OverrideActionsProperty, value); }

    /// <summary>Retrieves the switch, shows the value on it and listens to its clicks, detaching from a previous template.</summary>
    protected override void OnApplyTemplate()
    {
        this.toggleSwitch?.Toggled -= this.OnToggled;

        base.OnApplyTemplate();
        this.toggleSwitch = this.GetTemplateChild(SwitchPartName) as ToggleSwitch;
        this.ShowValue();
        this.toggleSwitch?.Toggled += this.OnToggled;
    }

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(ToggleField), new PropertyMetadata(defaultValue, changed));

    private static void OnValueChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((ToggleField)sender).ShowValue();

    private void ShowValue()
    {
        if (this.toggleSwitch is not { } toggle)
        {
            return;
        }

        this.showing = true;
        try
        {
            toggle.IsOn = this.IsOn == true;
            toggle.OffContent = this.IsOn is null ? MixedText : string.Empty;
        }
        finally
        {
            this.showing = false;
        }
    }

    private void OnToggled(object sender, RoutedEventArgs args)
    {
        if (!this.showing && this.toggleSwitch is { } toggle)
        {
            this.IsOn = toggle.IsOn;
        }
    }
}
