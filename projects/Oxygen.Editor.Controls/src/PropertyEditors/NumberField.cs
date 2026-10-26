// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.Controls;

/// <summary>Composes a property row around a NumberBox, with its field feedback below the row.</summary>
/// <remarks>
///     The row is a <see cref="PropertyCard"/> with its default style; the NumberBox uses the library's
///     <c>PropertyNumberBoxStyle</c>. The field never edits a model: it reports the value through
///     <see cref="NumberValue"/> and forwards the NumberBox's edit-session and validation events, raised with the field
///     as sender so its <see cref="FrameworkElement.Tag"/> names the edited field.
/// </remarks>
[TemplatePart(Name = NumberBoxPartName, Type = typeof(NumberBox))]
[TemplatePart(Name = ErrorTextPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = ApplicabilityTextPartName, Type = typeof(TextBlock))]
public sealed partial class NumberField : Control
{
    /// <summary>Identifies the field caption.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), string.Empty);

    /// <summary>Identifies the property qualifier.</summary>
    public static readonly DependencyProperty QualifierProperty = Register<string>(nameof(Qualifier), string.Empty);

    /// <summary>Identifies the value's unit.</summary>
    public static readonly DependencyProperty UnitProperty = Register<string>(nameof(Unit), string.Empty);

    /// <summary>Identifies the name of the rows whose editors end at the same right edge.</summary>
    public static readonly DependencyProperty QualifierGroupProperty = Register<string>(nameof(QualifierGroup), string.Empty);

    /// <summary>Identifies the value prefix.</summary>
    public static readonly DependencyProperty PrefixProperty = Register<string>(nameof(Prefix), string.Empty);

    /// <summary>Identifies the numeric value.</summary>
    public static readonly DependencyProperty NumberValueProperty = Register<double>(nameof(NumberValue), 0d, OnNumberValueChanged);

    /// <summary>Identifies mixed-value presentation.</summary>
    public static readonly DependencyProperty IsMixedProperty = Register<bool>(nameof(IsMixed), defaultValue: false);

    /// <summary>Identifies the existing NumberBox mask.</summary>
    public static readonly DependencyProperty MaskProperty = Register<string>(nameof(Mask), "~.###");

    /// <summary>Identifies the property-row layout.</summary>
    public static readonly DependencyProperty LayoutProperty = Register<PropertyLayout>(nameof(Layout), PropertyLayout.Auto);

    /// <summary>Identifies the shared caption width.</summary>
    public static readonly DependencyProperty LabelWidthProperty = Register<double>(nameof(LabelWidth), 124d);

    /// <summary>Identifies current validation text.</summary>
    public static readonly DependencyProperty ErrorTextProperty = Register<string>(nameof(ErrorText), string.Empty);

    /// <summary>Identifies whether validation text is shown.</summary>
    public static readonly DependencyProperty HasErrorProperty = Register<bool>(nameof(HasError), defaultValue: false, OnFeedbackChanged);

    /// <summary>Identifies current stored-value applicability text.</summary>
    public static readonly DependencyProperty ApplicabilityTextProperty = Register<string>(nameof(ApplicabilityText), string.Empty, OnFeedbackChanged);

    /// <summary>Identifies how many edited objects override the value.</summary>
    public static readonly DependencyProperty OverrideStateProperty = Register<OverrideState>(nameof(OverrideState), OverrideState.None);

    /// <summary>Identifies the command that resets the value to its source.</summary>
    public static readonly DependencyProperty ResetCommandProperty = Register<ICommand>(nameof(ResetCommand), defaultValue: null);

    /// <summary>Identifies the reset button's tooltip and automation name.</summary>
    public static readonly DependencyProperty ResetToolTipProperty = Register<string>(nameof(ResetToolTip), "Reset to the source value");

    private const string NumberBoxPartName = "PART_NumberBox";
    private const string ErrorTextPartName = "PART_ErrorText";
    private const string ApplicabilityTextPartName = "PART_ApplicabilityText";

    private readonly TemplatePartValue numberValue;
    private NumberBox? numberBox;
    private TextBlock? errorText;
    private TextBlock? applicabilityText;

    /// <summary>Initializes a new instance of the <see cref="NumberField"/> class.</summary>
    public NumberField()
    {
        this.DefaultStyleKey = typeof(NumberField);
        this.numberValue = new(this, NumberValueProperty, NumberBox.NumberValueProperty);
    }

    /// <summary>Occurs when the native NumberBox begins an edit.</summary>
    public event EventHandler<NumberBoxEditSessionEventArgs>? EditSessionStarted;

    /// <summary>Occurs when the native NumberBox commits or cancels an edit.</summary>
    public event EventHandler<NumberBoxEditSessionEventArgs>? EditSessionCompleted;

    /// <summary>Forwards the original validation event, including raw expression text.</summary>
    public event EventHandler<ValidationEventArgs<double>>? Validate;

    /// <summary>Gets or sets the property name shown by the row.</summary>
    public string Label { get => (string)this.GetValue(LabelProperty); set => this.SetValue(LabelProperty, value); }

    /// <summary>Gets or sets the optional qualifier of the property, shown by the row after the editor.</summary>
    public string Qualifier { get => (string)this.GetValue(QualifierProperty); set => this.SetValue(QualifierProperty, value); }

    /// <summary>Gets or sets the value's unit, such as m or °, shown by the NumberBox after its value.</summary>
    public string Unit { get => (string)this.GetValue(UnitProperty); set => this.SetValue(UnitProperty, value); }

    /// <summary>Gets or sets the name of the rows whose editors end at the same right edge; empty for none.</summary>
    public string QualifierGroup { get => (string)this.GetValue(QualifierGroupProperty); set => this.SetValue(QualifierGroupProperty, value); }

    /// <summary>Gets or sets the value's prefix, such as f/, shown by the NumberBox before its value.</summary>
    public string Prefix { get => (string)this.GetValue(PrefixProperty); set => this.SetValue(PrefixProperty, value); }

    /// <summary>Gets or sets the numeric value.</summary>
    public double NumberValue { get => (double)this.GetValue(NumberValueProperty); set => this.SetValue(NumberValueProperty, value); }

    /// <summary>Gets or sets a value indicating whether the field has mixed values.</summary>
    public bool IsMixed { get => (bool)this.GetValue(IsMixedProperty); set => this.SetValue(IsMixedProperty, value); }

    /// <summary>Gets or sets the native formatting mask.</summary>
    public string Mask { get => (string)this.GetValue(MaskProperty); set => this.SetValue(MaskProperty, value); }

    /// <summary>Gets or sets the property-row layout policy.</summary>
    public PropertyLayout Layout { get => (PropertyLayout)this.GetValue(LayoutProperty); set => this.SetValue(LayoutProperty, value); }

    /// <summary>Gets or sets the shared label-column width.</summary>
    public double LabelWidth { get => (double)this.GetValue(LabelWidthProperty); set => this.SetValue(LabelWidthProperty, value); }

    /// <summary>Gets or sets current field validation text.</summary>
    public string ErrorText { get => (string)this.GetValue(ErrorTextProperty); set => this.SetValue(ErrorTextProperty, value); }

    /// <summary>Gets or sets a value indicating whether field validation text is visible.</summary>
    public bool HasError { get => (bool)this.GetValue(HasErrorProperty); set => this.SetValue(HasErrorProperty, value); }

    /// <summary>Gets or sets applicability text, independently of errors; the line hides while it is empty.</summary>
    public string ApplicabilityText { get => (string)this.GetValue(ApplicabilityTextProperty); set => this.SetValue(ApplicabilityTextProperty, value); }

    /// <summary>Gets or sets how many edited objects override the value.</summary>
    public OverrideState OverrideState { get => (OverrideState)this.GetValue(OverrideStateProperty); set => this.SetValue(OverrideStateProperty, value); }

    /// <summary>Gets or sets the command that resets the value to its source.</summary>
    public ICommand? ResetCommand { get => (ICommand?)this.GetValue(ResetCommandProperty); set => this.SetValue(ResetCommandProperty, value); }

    /// <summary>Gets or sets the reset button's tooltip, which names the source.</summary>
    public string ResetToolTip { get => (string)this.GetValue(ResetToolTipProperty); set => this.SetValue(ResetToolTipProperty, value); }

    /// <summary>Retrieves the template parts and forwards the NumberBox's events, detaching from a previous template.</summary>
    protected override void OnApplyTemplate()
    {
        if (this.numberBox is not null)
        {
            this.numberBox.EditSessionStarted -= this.OnEditSessionStarted;
            this.numberBox.EditSessionCompleted -= this.OnEditSessionCompleted;
            this.numberBox.Validate -= this.OnValidate;
        }

        base.OnApplyTemplate();
        this.numberBox = this.GetTemplateChild(NumberBoxPartName) as NumberBox;
        this.numberValue.Attach(this.numberBox);
        this.errorText = this.GetTemplateChild(ErrorTextPartName) as TextBlock;
        this.applicabilityText = this.GetTemplateChild(ApplicabilityTextPartName) as TextBlock;
        if (this.numberBox is not null)
        {
            this.numberBox.EditSessionStarted += this.OnEditSessionStarted;
            this.numberBox.EditSessionCompleted += this.OnEditSessionCompleted;
            this.numberBox.Validate += this.OnValidate;
        }

        this.UpdateFeedback();
    }

    private static DependencyProperty Register<T>(string propertyName, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(propertyName, typeof(T), typeof(NumberField), new PropertyMetadata(defaultValue, changed));

    private static void OnNumberValueChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((NumberField)sender).numberValue.Push();

    private static void OnFeedbackChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((NumberField)sender).UpdateFeedback();

    // The validation line shows while the field has an error; the applicability line while it has text.
    private void UpdateFeedback()
    {
        this.errorText?.Visibility = this.HasError ? Visibility.Visible : Visibility.Collapsed;
        this.applicabilityText?.Visibility = string.IsNullOrEmpty(this.ApplicabilityText) ? Visibility.Collapsed : Visibility.Visible;
    }

    private void OnEditSessionStarted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.EditSessionStarted?.Invoke(this, args);

    private void OnEditSessionCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.EditSessionCompleted?.Invoke(this, args);

    private void OnValidate(object? sender, ValidationEventArgs<double> args)
        => this.Validate?.Invoke(this, args);
}
