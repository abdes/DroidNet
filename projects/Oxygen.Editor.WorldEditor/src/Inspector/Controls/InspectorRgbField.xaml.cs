// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace Oxygen.Editor.World.Inspector.Controls;

/// <summary>Composes linear RGB channels and, for colors only, the existing display picker.</summary>
public sealed partial class InspectorRgbField : UserControl
{
    /// <summary>Identifies the property caption.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), "Color");

    /// <summary>Identifies the shared channel annotation.</summary>
    public static readonly DependencyProperty QualifierProperty = Register<string>(nameof(Qualifier), "Linear RGB");

    /// <summary>Identifies the red authoring channel.</summary>
    public static readonly DependencyProperty RedProperty = Register<float>(nameof(Red), 0f, OnPresentationChanged);

    /// <summary>Identifies the green authoring channel.</summary>
    public static readonly DependencyProperty GreenProperty = Register<float>(nameof(Green), 0f, OnPresentationChanged);

    /// <summary>Identifies the blue authoring channel.</summary>
    public static readonly DependencyProperty BlueProperty = Register<float>(nameof(Blue), 0f, OnPresentationChanged);

    /// <summary>Identifies red-channel mixed state.</summary>
    public static readonly DependencyProperty RedIsMixedProperty = Register<bool>(nameof(RedIsMixed), defaultValue: false);

    /// <summary>Identifies green-channel mixed state.</summary>
    public static readonly DependencyProperty GreenIsMixedProperty = Register<bool>(nameof(GreenIsMixed), defaultValue: false);

    /// <summary>Identifies blue-channel mixed state.</summary>
    public static readonly DependencyProperty BlueIsMixedProperty = Register<bool>(nameof(BlueIsMixed), defaultValue: false);

    /// <summary>Identifies multiplier presentation, which never offers a color picker.</summary>
    public static readonly DependencyProperty IsMultiplierProperty = Register<bool>(nameof(IsMultiplier), defaultValue: false, OnKindChanged);

    /// <summary>Identifies the existing per-channel formatting mask.</summary>
    public static readonly DependencyProperty ComponentMaskProperty = Register<string>(nameof(ComponentMask), "~.###");

    /// <summary>Identifies the borrowed edit-session owner for picker lifetime protection.</summary>
    public static readonly DependencyProperty EditOwnerProperty = Register<IInspectorEditSessionOwner>(nameof(EditOwner), defaultValue: null, OnOwnerChanged);

    /// <summary>Identifies the current validation text.</summary>
    public static readonly DependencyProperty ErrorTextProperty = Register<string>(nameof(ErrorText), string.Empty);

    /// <summary>Identifies whether current validation text is visible.</summary>
    public static readonly DependencyProperty HasErrorProperty = Register<bool>(nameof(HasError), defaultValue: false, OnPresentationChanged);

    /// <summary>Identifies the existing editor/diagnostic spacing.</summary>
    public static readonly DependencyProperty DiagnosticSpacingProperty = Register<double>(nameof(DiagnosticSpacing), 4d);

    /// <summary>Identifies channel automation metadata.</summary>
    public static readonly DependencyProperty ChannelAutomationNameProperty = Register<string>(nameof(ChannelAutomationName), "Color, linear RGB");

    /// <summary>Identifies the existing picker action's automation name.</summary>
    public static readonly DependencyProperty PickerAutomationNameProperty = Register<string>(nameof(PickerAutomationName), "Pick color", OnAccessoryChanged);

    /// <summary>Identifies the existing swatch corner treatment.</summary>
    public static readonly DependencyProperty SwatchCornerRadiusProperty = Register<CornerRadius>(nameof(SwatchCornerRadius), new CornerRadius(2), OnAccessoryChanged);

    /// <summary>Identifies a consumer-specific existing swatch border brush.</summary>
    public static readonly DependencyProperty SwatchBorderBrushProperty = Register<Brush>(nameof(SwatchBorderBrush), defaultValue: null, OnAccessoryChanged);

    private Button? swatch;
    private Border? swatchBorder;
    private ColorPicker? picker;

    /// <summary>Initializes a new instance of the <see cref="InspectorRgbField"/> class.</summary>
    public InspectorRgbField()
    {
        this.InitializeComponent();
        InspectorRgbPresentation.Configure(this.Channels);
        this.Loaded += (_, _) => this.UpdateAccessory();
        this.Unloaded += (_, _) => this.swatch?.Flyout.Hide();
    }

    /// <summary>Forwards the original per-channel start event.</summary>
    public event EventHandler<VectorBoxEditSessionEventArgs>? EditSessionStarted;

    /// <summary>Forwards the original per-channel completion event.</summary>
    public event EventHandler<VectorBoxEditSessionEventArgs>? EditSessionCompleted;

    /// <summary>Requests a complete color edit on the captured owner.</summary>
    public event EventHandler<InspectorRgbColorPickedEventArgs>? ColorPicked;

    /// <summary>Gets or sets the property caption.</summary>
    public string Label { get => (string)this.GetValue(LabelProperty); set => this.SetValue(LabelProperty, value); }

    /// <summary>Gets or sets the shared channel annotation.</summary>
    public string Qualifier { get => (string)this.GetValue(QualifierProperty); set => this.SetValue(QualifierProperty, value); }

    /// <summary>Gets or sets the authored red channel.</summary>
    public float Red { get => (float)this.GetValue(RedProperty); set => this.SetValue(RedProperty, value); }

    /// <summary>Gets or sets the authored green channel.</summary>
    public float Green { get => (float)this.GetValue(GreenProperty); set => this.SetValue(GreenProperty, value); }

    /// <summary>Gets or sets the authored blue channel.</summary>
    public float Blue { get => (float)this.GetValue(BlueProperty); set => this.SetValue(BlueProperty, value); }

    /// <summary>Gets or sets a value indicating whether red values are mixed.</summary>
    public bool RedIsMixed { get => (bool)this.GetValue(RedIsMixedProperty); set => this.SetValue(RedIsMixedProperty, value); }

    /// <summary>Gets or sets a value indicating whether green values are mixed.</summary>
    public bool GreenIsMixed { get => (bool)this.GetValue(GreenIsMixedProperty); set => this.SetValue(GreenIsMixedProperty, value); }

    /// <summary>Gets or sets a value indicating whether blue values are mixed.</summary>
    public bool BlueIsMixed { get => (bool)this.GetValue(BlueIsMixedProperty); set => this.SetValue(BlueIsMixedProperty, value); }

    /// <summary>Gets or sets a value indicating whether these channels are multipliers without a picker.</summary>
    public bool IsMultiplier { get => (bool)this.GetValue(IsMultiplierProperty); set => this.SetValue(IsMultiplierProperty, value); }

    /// <summary>Gets or sets the existing component mask.</summary>
    public string ComponentMask { get => (string)this.GetValue(ComponentMaskProperty); set => this.SetValue(ComponentMaskProperty, value); }

    /// <summary>Gets or sets the borrowed picker edit owner; this control never disposes it.</summary>
    public IInspectorEditSessionOwner? EditOwner { get => (IInspectorEditSessionOwner?)this.GetValue(EditOwnerProperty); set => this.SetValue(EditOwnerProperty, value); }

    /// <summary>Gets or sets current field feedback.</summary>
    public string ErrorText { get => (string)this.GetValue(ErrorTextProperty); set => this.SetValue(ErrorTextProperty, value); }

    /// <summary>Gets or sets a value indicating whether validation feedback is shown.</summary>
    public bool HasError { get => (bool)this.GetValue(HasErrorProperty); set => this.SetValue(HasErrorProperty, value); }

    /// <summary>Gets or sets editor/diagnostic spacing.</summary>
    public double DiagnosticSpacing { get => (double)this.GetValue(DiagnosticSpacingProperty); set => this.SetValue(DiagnosticSpacingProperty, value); }

    /// <summary>Gets or sets channel automation metadata.</summary>
    public string ChannelAutomationName { get => (string)this.GetValue(ChannelAutomationNameProperty); set => this.SetValue(ChannelAutomationNameProperty, value); }

    /// <summary>Gets or sets the picker action's automation name.</summary>
    public string PickerAutomationName { get => (string)this.GetValue(PickerAutomationNameProperty); set => this.SetValue(PickerAutomationNameProperty, value); }

    /// <summary>Gets or sets the swatch corner treatment.</summary>
    public CornerRadius SwatchCornerRadius { get => (CornerRadius)this.GetValue(SwatchCornerRadiusProperty); set => this.SetValue(SwatchCornerRadiusProperty, value); }

    /// <summary>Gets or sets a consumer-specific existing swatch border brush.</summary>
    public Brush? SwatchBorderBrush { get => (Brush?)this.GetValue(SwatchBorderBrushProperty); set => this.SetValue(SwatchBorderBrushProperty, value); }

    private Vector3 Rgb => new(this.Red, this.Green, this.Blue);

    private Visibility ErrorVisibility => this.HasError ? Visibility.Visible : Visibility.Collapsed;

    /// <inheritdoc />
    protected override Size MeasureOverride(Size availableSize)
    {
        this.UpdateAccessory();
        return base.MeasureOverride(availableSize);
    }

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(InspectorRgbField), new PropertyMetadata(defaultValue, changed));

    private static void OnPresentationChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        var control = (InspectorRgbField)sender;
        control.Bindings?.Update();
        control.UpdateSwatch();
    }

    private static void OnKindChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        ((InspectorRgbField)sender).UpdateAccessory();
    }

    private static void OnOwnerChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((InspectorRgbField)sender).swatch?.Flyout.Hide();

    private static void OnAccessoryChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((InspectorRgbField)sender).UpdateAccessory();

    private void UpdateAccessory()
    {
        if (this.IsMultiplier)
        {
            this.swatch?.Flyout.Hide();
            this.Card.LeadingContent = null;
            return;
        }

        if (this.swatch is null)
        {
            this.picker = new ColorPicker { IsAlphaEnabled = false, IsColorChannelTextInputVisible = true, IsHexInputVisible = true };
            this.picker.Loaded += this.OnPickerLoaded;
            this.picker.ColorChanged += this.OnPickerColorChanged;
            this.swatchBorder = new Border
            {
                Width = 20,
                Height = 24,
                CornerRadius = this.SwatchCornerRadius,
                Style = (Style)this.Resources["SwatchBorderStyle"],
            };
            this.swatch = new Button
            {
                Name = "Swatch",
                Style = (Style)this.Resources["InspectorColorSwatchStyle"],
                Content = this.swatchBorder,
                Flyout = new Flyout { Content = this.picker },
            };
        }

        this.swatchBorder!.CornerRadius = this.SwatchCornerRadius;
        if (this.SwatchBorderBrush is { } brush)
        {
            this.swatchBorder.BorderBrush = brush;
        }
        else
        {
            this.swatchBorder.ClearValue(Border.BorderBrushProperty);
        }

        AutomationProperties.SetName(this.swatch, this.PickerAutomationName);
        ToolTipService.SetToolTip(this.swatch, this.PickerAutomationName);
        this.UpdateSwatch();
        this.Card.LeadingContent = this.swatch;
    }

    private void UpdateSwatch()
    {
        if (this.swatchBorder is not null && this.picker is not null)
        {
            this.swatchBorder.Background = InspectorRgbPresentation.ToBrush(this.Rgb);
            this.picker.Color = InspectorRgbPresentation.ToDisplayColor(this.Rgb);
        }
    }

    private void OnVectorEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.EditSessionStarted?.Invoke(this, args);

    private void OnVectorEditCompleted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.EditSessionCompleted?.Invoke(this, args);

    private void OnPickerLoaded(object sender, RoutedEventArgs args)
    {
        if (sender is ColorPicker picker && !this.IsMultiplier)
        {
            InspectorColorGestures.Attach(picker, this.EditOwner, this.Tag as string ?? string.Empty);
        }
    }

    private void OnPickerColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        if (!this.IsMultiplier && InspectorRgbPresentation.ToDisplayColor(this.Rgb) != args.NewColor)
        {
            InspectorColorGestures.Apply(sender, owner => this.ColorPicked?.Invoke(
                this, new InspectorRgbColorPickedEventArgs(owner, InspectorRgbPresentation.ToLinearRgb(args.NewColor))));
        }
    }
}
