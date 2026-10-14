// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Controls;

/// <summary>Composes an authored color's caption, its <see cref="ColorBox"/> and its field feedback.</summary>
/// <remarks>
///     <see cref="Kind"/> decides how the channels are shown and picked and the unit the caption shows. The field never
///     edits a model: it reports channel edits and picks through its events, raised with the field as sender so its
///     <see cref="FrameworkElement.Tag"/> names the edited field.
/// </remarks>
public sealed partial class InspectorColorField : UserControl
{
    /// <summary>Identifies the property caption.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), "Color", OnPresentationChanged);

    /// <summary>Identifies the color kind.</summary>
    public static readonly DependencyProperty KindProperty = Register<InspectorColorKind>(nameof(Kind), InspectorColorKind.Linear, OnPresentationChanged);

    /// <summary>Identifies whether the color has alpha.</summary>
    public static readonly DependencyProperty HasAlphaProperty = Register<bool>(nameof(HasAlpha), defaultValue: false, OnPresentationChanged);

    /// <summary>Identifies the red channel.</summary>
    public static readonly DependencyProperty RedProperty = Register<double>(nameof(Red), 0d);

    /// <summary>Identifies the green channel.</summary>
    public static readonly DependencyProperty GreenProperty = Register<double>(nameof(Green), 0d);

    /// <summary>Identifies the blue channel.</summary>
    public static readonly DependencyProperty BlueProperty = Register<double>(nameof(Blue), 0d);

    /// <summary>Identifies the alpha channel.</summary>
    public static readonly DependencyProperty AlphaProperty = Register<double>(nameof(Alpha), 1d);

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

    /// <summary>Initializes a new instance of the <see cref="InspectorColorField"/> class.</summary>
    public InspectorColorField()
    {
        this.InitializeComponent();
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
    public InspectorColorKind Kind { get => (InspectorColorKind)this.GetValue(KindProperty); set => this.SetValue(KindProperty, value); }

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
        InspectorColorKind.Luminance => "Luminance RGB",
        InspectorColorKind.Multiplier => "RGB ×",
        _ => this.HasAlpha ? "Linear RGBA" : "Linear RGB",
    };

    private string ChannelAutomationName => $"{this.Label}, {this.Qualifier}";

    private ColorRange Range => this.Kind == InspectorColorKind.Linear ? ColorRange.Normalized : ColorRange.HighDynamicRange;

    private bool IsPickerVisible => this.Kind != InspectorColorKind.Multiplier;

    private Visibility ErrorVisibility => this.HasError ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Closes the picker, committing an open pick.</summary>
    public void HidePicker() => this.Editor.HidePicker();

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(InspectorColorField), new PropertyMetadata(defaultValue, changed));

    private static void OnPresentationChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((InspectorColorField)sender).Bindings?.Update();

    private void OnEditSessionStarted(object? sender, ColorBoxEditSessionEventArgs args)
        => this.EditSessionStarted?.Invoke(this, args);

    private void OnEditSessionCompleted(object? sender, ColorBoxEditSessionEventArgs args)
        => this.EditSessionCompleted?.Invoke(this, args);

    private void OnColorPicked(object? sender, ColorBoxColorPickedEventArgs args)
        => this.ColorPicked?.Invoke(this, args);
}
