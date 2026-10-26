// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Pickers;

/// <summary>
/// A stretched split button that shows the current assignment and opens an <see cref="AssetPicker"/> in its flyout. The
/// button hides its flyout when a choice is picked and reports the choice; the owner assigns it.
/// </summary>
/// <remarks>
/// Its default style, in the assembly's <c>Themes/Generic.xaml</c>, templates a <see cref="SplitButton"/> named
/// <c>PART_Button</c> with a flyout. The button creates the picker in that flyout when it first opens, so a list of many
/// buttons, such as one per material, does not build a picker for each.
/// </remarks>
[TemplatePart(Name = ButtonPartName, Type = typeof(SplitButton))]
public sealed partial class AssetPickerButton : Control
{
    /// <summary>Identifies the <see cref="Label"/> dependency property.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), string.Empty);

    /// <summary>Identifies the <see cref="Model"/> dependency property.</summary>
    public static readonly DependencyProperty ModelProperty = Register<AssetPickerModel>(nameof(Model), defaultValue: null, OnModelChanged);

    /// <summary>Identifies the <see cref="AutomationName"/> dependency property.</summary>
    public static readonly DependencyProperty AutomationNameProperty = Register<string>(nameof(AutomationName), string.Empty);

    private const string ButtonPartName = "PART_Button";

    private Flyout? pickerFlyout;
    private AssetPicker? choicePicker;

    /// <summary>Initializes a new instance of the <see cref="AssetPickerButton"/> class.</summary>
    public AssetPickerButton()
    {
        this.DefaultStyleKey = typeof(AssetPickerButton);
    }

    /// <summary>Raised when the flyout opens, so the owner can prepare the choices the picker shows.</summary>
    public event EventHandler? PickerOpening;

    /// <summary>Raised when the user picks an enabled choice, after the button hides its flyout.</summary>
    public event EventHandler<AssetChoice>? ChoiceInvoked;

    /// <summary>Gets or sets the button's content and tooltip, such as the name of the assigned asset.</summary>
    public string Label { get => (string)this.GetValue(LabelProperty); set => this.SetValue(LabelProperty, value); }

    /// <summary>Gets or sets the choices the flyout's picker shows.</summary>
    public AssetPickerModel? Model { get => (AssetPickerModel?)this.GetValue(ModelProperty); set => this.SetValue(ModelProperty, value); }

    /// <summary>Gets or sets the button's automation name; empty to let the label name it.</summary>
    public string AutomationName { get => (string)this.GetValue(AutomationNameProperty); set => this.SetValue(AutomationNameProperty, value); }

    /// <summary>Retrieves the button's flyout and listens to it, detaching from a previous template and its picker.</summary>
    protected override void OnApplyTemplate()
    {
        if (this.pickerFlyout is not null)
        {
            this.pickerFlyout.Opening -= this.OnFlyoutOpening;
        }

        if (this.choicePicker is not null)
        {
            this.choicePicker.ChoiceInvoked -= this.OnChoiceInvoked;
            this.choicePicker = null;
        }

        base.OnApplyTemplate();
        this.pickerFlyout = (this.GetTemplateChild(ButtonPartName) as SplitButton)?.Flyout as Flyout;
        if (this.pickerFlyout is not null)
        {
            this.pickerFlyout.Opening += this.OnFlyoutOpening;
        }
    }

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(AssetPickerButton), new PropertyMetadata(defaultValue, changed));

    private static void OnModelChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        if (((AssetPickerButton)sender).choicePicker is { } picker)
        {
            picker.Model = (AssetPickerModel?)args.NewValue;
        }
    }

    private void OnFlyoutOpening(object? sender, object args)
    {
        if (this.choicePicker is null && this.pickerFlyout is not null)
        {
            this.choicePicker = new AssetPicker { Model = this.Model };
            this.choicePicker.ChoiceInvoked += this.OnChoiceInvoked;
            this.pickerFlyout.Content = this.choicePicker;
        }

        this.PickerOpening?.Invoke(this, EventArgs.Empty);
    }

    private void OnChoiceInvoked(object? sender, AssetChoice choice)
    {
        this.pickerFlyout?.Hide();
        this.ChoiceInvoked?.Invoke(this, choice);
    }
}
