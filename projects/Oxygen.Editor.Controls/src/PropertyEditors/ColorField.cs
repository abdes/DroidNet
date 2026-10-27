// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Controls.PropertyEditors;

namespace Oxygen.Editor.Controls;

/// <summary>Composes an authored color's caption, its <see cref="ColorBox"/> and its field feedback.</summary>
/// <remarks>
///     <see cref="Kind"/> decides how the channels are shown and picked and the unit the caption shows. The field never
///     edits a model: it reports channel edits and picks through its events, raised with the field as sender so its
///     <see cref="FrameworkElement.Tag"/> names the edited field.
/// </remarks>
[TemplatePart(Name = CardPartName, Type = typeof(PropertyCard))]
[TemplatePart(Name = ColorBoxPartName, Type = typeof(ColorBox))]
[TemplatePart(Name = ErrorTextPartName, Type = typeof(TextBlock))]
public sealed partial class ColorField : Control
{
    /// <summary>Identifies the property caption.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), "Color", OnPresentationChanged);

    /// <summary>Identifies the color kind.</summary>
    public static readonly DependencyProperty KindProperty = Register<ColorFieldKind>(nameof(Kind), ColorFieldKind.Linear, OnPresentationChanged);

    /// <summary>Identifies whether the color has alpha.</summary>
    public static readonly DependencyProperty HasAlphaProperty = Register<bool>(nameof(HasAlpha), defaultValue: false, OnPresentationChanged);

    /// <summary>Identifies the red channel.</summary>
    public static readonly DependencyProperty RedProperty = Register<double>(nameof(Red), 0d, OnChannelChanged);

    /// <summary>Identifies the green channel.</summary>
    public static readonly DependencyProperty GreenProperty = Register<double>(nameof(Green), 0d, OnChannelChanged);

    /// <summary>Identifies the blue channel.</summary>
    public static readonly DependencyProperty BlueProperty = Register<double>(nameof(Blue), 0d, OnChannelChanged);

    /// <summary>Identifies the alpha channel.</summary>
    public static readonly DependencyProperty AlphaProperty = Register<double>(nameof(Alpha), 1d, OnChannelChanged);

    /// <summary>Identifies red-channel mixed state.</summary>
    public static readonly DependencyProperty RedIsMixedProperty = Register<bool>(nameof(RedIsMixed), defaultValue: false);

    /// <summary>Identifies green-channel mixed state.</summary>
    public static readonly DependencyProperty GreenIsMixedProperty = Register<bool>(nameof(GreenIsMixed), defaultValue: false);

    /// <summary>Identifies blue-channel mixed state.</summary>
    public static readonly DependencyProperty BlueIsMixedProperty = Register<bool>(nameof(BlueIsMixed), defaultValue: false);

    /// <summary>Identifies alpha-channel mixed state.</summary>
    public static readonly DependencyProperty AlphaIsMixedProperty = Register<bool>(nameof(AlphaIsMixed), defaultValue: false);

    /// <summary>Identifies the channel formatting mask.</summary>
    public static readonly DependencyProperty ComponentMaskProperty = Register<string>(nameof(ComponentMask), "~.###");

    /// <summary>Identifies the swatch's automation name and tooltip.</summary>
    public static readonly DependencyProperty PickerAutomationNameProperty = Register<string>(nameof(PickerAutomationName), "Pick color");

    /// <summary>Identifies the current validation text.</summary>
    public static readonly DependencyProperty ErrorTextProperty = Register<string>(nameof(ErrorText), string.Empty);

    /// <summary>Identifies whether the validation text shows.</summary>
    public static readonly DependencyProperty HasErrorProperty = Register<bool>(nameof(HasError), defaultValue: false, OnPresentationChanged);

    /// <summary>Identifies the spacing between the row and the validation text.</summary>
    public static readonly DependencyProperty DiagnosticSpacingProperty = Register<double>(nameof(DiagnosticSpacing), 4d);

    /// <summary>Identifies the name of the rows whose editors end at the same right edge.</summary>
    public static readonly DependencyProperty QualifierGroupProperty = Register<string>(nameof(QualifierGroup), string.Empty);

    /// <summary>Identifies the side of each channel field its label shows on.</summary>
    public static readonly DependencyProperty ChannelLabelPositionProperty = Register<LabelPosition>(nameof(ChannelLabelPosition), LabelPosition.Left);

    /// <summary>Identifies how many edited objects override the value.</summary>
    public static readonly DependencyProperty OverrideStateProperty = Register<OverrideState>(nameof(OverrideState), OverrideState.None);

    /// <summary>Identifies the command that resets the value to its source.</summary>
    public static readonly DependencyProperty ResetCommandProperty = Register<ICommand>(nameof(ResetCommand), defaultValue: null);

    /// <summary>Identifies the reset button's tooltip and automation name.</summary>
    public static readonly DependencyProperty ResetToolTipProperty = Register<string>(nameof(ResetToolTip), "Reset to the source value");

    /// <summary>Identifies the source of the commands the row's context menu offers besides reset.</summary>
    public static readonly DependencyProperty OverrideActionsProperty = Register<IOverrideActionSource>(nameof(OverrideActions), defaultValue: null);

    private const string CardPartName = "PART_Card";
    private const string ColorBoxPartName = "PART_ColorBox";
    private const string ErrorTextPartName = "PART_ErrorText";

    private readonly TemplatePartValue[] channels;
    private PropertyCard? card;
    private ColorBox? colorBox;
    private TextBlock? errorText;

    /// <summary>Initializes a new instance of the <see cref="ColorField"/> class.</summary>
    public ColorField()
    {
        this.DefaultStyleKey = typeof(ColorField);
        this.channels =
        [
            new(this, RedProperty, ColorBox.RedProperty),
            new(this, GreenProperty, ColorBox.GreenProperty),
            new(this, BlueProperty, ColorBox.BlueProperty),
            new(this, AlphaProperty, ColorBox.AlphaProperty),
        ];
    }

    /// <summary>Occurs when a channel edit or a pick starts.</summary>
    public event EventHandler<ColorBoxEditSessionEventArgs>? EditSessionStarted;

    /// <summary>Occurs when a channel edit or a pick commits or is canceled.</summary>
    public event EventHandler<ColorBoxEditSessionEventArgs>? EditSessionCompleted;

    /// <summary>Occurs for each picked color; a handler applies it as one edit and marks it handled.</summary>
    public event EventHandler<ColorBoxColorPickedEventArgs>? ColorPicked;

    /// <summary>Gets or sets the property caption.</summary>
    public string Label { get => (string)this.GetValue(LabelProperty); set => this.SetValue(LabelProperty, value); }

    /// <summary>Gets or sets what the color means.</summary>
    public ColorFieldKind Kind { get => (ColorFieldKind)this.GetValue(KindProperty); set => this.SetValue(KindProperty, value); }

    /// <summary>Gets or sets a value indicating whether the color has an alpha channel.</summary>
    public bool HasAlpha { get => (bool)this.GetValue(HasAlphaProperty); set => this.SetValue(HasAlphaProperty, value); }

    /// <summary>Gets or sets the linear red channel.</summary>
    public double Red { get => (double)this.GetValue(RedProperty); set => this.SetValue(RedProperty, value); }

    /// <summary>Gets or sets the linear green channel.</summary>
    public double Green { get => (double)this.GetValue(GreenProperty); set => this.SetValue(GreenProperty, value); }

    /// <summary>Gets or sets the linear blue channel.</summary>
    public double Blue { get => (double)this.GetValue(BlueProperty); set => this.SetValue(BlueProperty, value); }

    /// <summary>Gets or sets the straight alpha channel.</summary>
    public double Alpha { get => (double)this.GetValue(AlphaProperty); set => this.SetValue(AlphaProperty, value); }

    /// <summary>Gets or sets a value indicating whether red values are mixed.</summary>
    public bool RedIsMixed { get => (bool)this.GetValue(RedIsMixedProperty); set => this.SetValue(RedIsMixedProperty, value); }

    /// <summary>Gets or sets a value indicating whether green values are mixed.</summary>
    public bool GreenIsMixed { get => (bool)this.GetValue(GreenIsMixedProperty); set => this.SetValue(GreenIsMixedProperty, value); }

    /// <summary>Gets or sets a value indicating whether blue values are mixed.</summary>
    public bool BlueIsMixed { get => (bool)this.GetValue(BlueIsMixedProperty); set => this.SetValue(BlueIsMixedProperty, value); }

    /// <summary>Gets or sets a value indicating whether alpha values are mixed.</summary>
    public bool AlphaIsMixed { get => (bool)this.GetValue(AlphaIsMixedProperty); set => this.SetValue(AlphaIsMixedProperty, value); }

    /// <summary>Gets or sets the channel formatting mask.</summary>
    public string ComponentMask { get => (string)this.GetValue(ComponentMaskProperty); set => this.SetValue(ComponentMaskProperty, value); }

    /// <summary>Gets or sets the swatch's automation name and tooltip.</summary>
    public string PickerAutomationName { get => (string)this.GetValue(PickerAutomationNameProperty); set => this.SetValue(PickerAutomationNameProperty, value); }

    /// <summary>Gets or sets how many edited objects override the value.</summary>
    public OverrideState OverrideState { get => (OverrideState)this.GetValue(OverrideStateProperty); set => this.SetValue(OverrideStateProperty, value); }

    /// <summary>Gets or sets the command that resets the value to its source.</summary>
    public ICommand? ResetCommand { get => (ICommand?)this.GetValue(ResetCommandProperty); set => this.SetValue(ResetCommandProperty, value); }

    /// <summary>Gets or sets the reset button's tooltip, which names the source.</summary>
    public string ResetToolTip { get => (string)this.GetValue(ResetToolTipProperty); set => this.SetValue(ResetToolTipProperty, value); }

    /// <summary>Gets or sets the source of the commands the row's context menu offers while the value is overridden.</summary>
    public IOverrideActionSource? OverrideActions { get => (IOverrideActionSource?)this.GetValue(OverrideActionsProperty); set => this.SetValue(OverrideActionsProperty, value); }

    /// <summary>Gets or sets the current validation text.</summary>
    public string ErrorText { get => (string)this.GetValue(ErrorTextProperty); set => this.SetValue(ErrorTextProperty, value); }

    /// <summary>Gets or sets a value indicating whether the validation text shows.</summary>
    public bool HasError { get => (bool)this.GetValue(HasErrorProperty); set => this.SetValue(HasErrorProperty, value); }

    /// <summary>Gets or sets the spacing between the row and the validation text.</summary>
    public double DiagnosticSpacing { get => (double)this.GetValue(DiagnosticSpacingProperty); set => this.SetValue(DiagnosticSpacingProperty, value); }

    /// <summary>Gets or sets the name of the rows whose editors end at the same right edge; empty for none.</summary>
    public string QualifierGroup { get => (string)this.GetValue(QualifierGroupProperty); set => this.SetValue(QualifierGroupProperty, value); }

    /// <summary>Gets or sets the side of each channel field its R, G, B or A label shows on: Left or Right.</summary>
    /// <remarks>Channel labels always show inside their fields; see <see cref="ColorBox.ChannelLabelPosition"/>.</remarks>
    public LabelPosition ChannelLabelPosition { get => (LabelPosition)this.GetValue(ChannelLabelPositionProperty); set => this.SetValue(ChannelLabelPositionProperty, value); }

    /// <summary>Gets the qualifier the row shows, from <see cref="Kind"/> and <see cref="HasAlpha"/>.</summary>
    public string Qualifier => this.Kind switch
    {
        ColorFieldKind.Luminance => "Luminance RGB",
        ColorFieldKind.Multiplier => "RGB ×",
        _ => this.HasAlpha ? "Linear RGBA" : "Linear RGB",
    };

    /// <summary>Closes the picker, committing an open pick.</summary>
    public void HidePicker() => this.colorBox?.HidePicker();

    /// <summary>Retrieves the template parts, shows the kind on them and forwards the ColorBox's events, detaching from a
    /// previous template.</summary>
    protected override void OnApplyTemplate()
    {
        if (this.colorBox is not null)
        {
            this.colorBox.EditSessionStarted -= this.OnEditSessionStarted;
            this.colorBox.EditSessionCompleted -= this.OnEditSessionCompleted;
            this.colorBox.ColorPicked -= this.OnColorPicked;
        }

        base.OnApplyTemplate();
        this.card = this.GetTemplateChild(CardPartName) as PropertyCard;
        this.colorBox = this.GetTemplateChild(ColorBoxPartName) as ColorBox;
        foreach (var channel in this.channels)
        {
            channel.Attach(this.colorBox);
        }

        this.errorText = this.GetTemplateChild(ErrorTextPartName) as TextBlock;
        this.UpdatePresentation();
        if (this.colorBox is not null)
        {
            this.colorBox.EditSessionStarted += this.OnEditSessionStarted;
            this.colorBox.EditSessionCompleted += this.OnEditSessionCompleted;
            this.colorBox.ColorPicked += this.OnColorPicked;
        }
    }

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(ColorField), new PropertyMetadata(defaultValue, changed));

    private static void OnChannelChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        foreach (var channel in ((ColorField)sender).channels)
        {
            channel.Push();
        }
    }

    private static void OnPresentationChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((ColorField)sender).UpdatePresentation();

    // What the kind decides: the row's qualifier, the channels' range, whether the swatch picks, and the channels' name.
    private void UpdatePresentation()
    {
        var qualifier = this.Qualifier;
        this.card?.Qualifier = qualifier;
        if (this.colorBox is { } box)
        {
            box.Range = this.Kind == ColorFieldKind.Linear ? ColorRange.Normalized : ColorRange.HighDynamicRange;
            box.IsPickerVisible = this.Kind != ColorFieldKind.Multiplier;
            AutomationProperties.SetName(box, $"{this.Label}, {qualifier}");
        }

        this.errorText?.Visibility = this.HasError ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnEditSessionStarted(object? sender, ColorBoxEditSessionEventArgs args)
        => this.EditSessionStarted?.Invoke(this, args);

    private void OnEditSessionCompleted(object? sender, ColorBoxEditSessionEventArgs args)
        => this.EditSessionCompleted?.Invoke(this, args);

    private void OnColorPicked(object? sender, ColorBoxColorPickedEventArgs args)
        => this.ColorPicked?.Invoke(this, args);
}
