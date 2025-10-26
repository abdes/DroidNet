// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display numeric values.
/// </summary>
public partial class NumberBox
{
    /// <summary>
    ///     Identifies the <see cref="NumberValue" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty NumberValueProperty =
        DependencyProperty.Register(
            nameof(NumberValue),
            typeof(float),
            typeof(NumberBox),
            new PropertyMetadata(defaultValue: 0.0f, OnValuePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="DisplayText" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty DisplayTextProperty =
        DependencyProperty.Register(
            nameof(DisplayText),
            typeof(string),
            typeof(NumberBox),
            new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     Identifies the <see cref="Label" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty LabelProperty =
        DependencyProperty.Register(
            nameof(Label),
            typeof(string),
            typeof(NumberBox),
            new PropertyMetadata(string.Empty));

    /// <summary>
    ///     Identifies the <see cref="LabelPosition" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty LabelPositionProperty =
        DependencyProperty.Register(
            nameof(LabelPosition),
            typeof(LabelPosition),
            typeof(NumberBox),
            new PropertyMetadata(LabelPosition.Left, OnLabelPositionPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="Multiplier" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty MultiplierProperty =
        DependencyProperty.Register(
            nameof(Multiplier),
            typeof(int),
            typeof(NumberBox),
            new PropertyMetadata(1));

    /// <summary>
    ///     Identifies the <see cref="Mask" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty MaskProperty =
        DependencyProperty.Register(
            nameof(Mask),
            typeof(string),
            typeof(NumberBox),
            new PropertyMetadata("~.#", OnMaskPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="HorizontalValueAlignment" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty HorizontalValueAlignmentProperty =
        DependencyProperty.Register(
            nameof(HorizontalValueAlignment),
            typeof(TextAlignment),
            typeof(NumberBox),
            new PropertyMetadata(TextAlignment.Center));

    /// <summary>
    ///     Identifies the <see cref="HorizontalLabelAlignment" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty HorizontalLabelAlignmentProperty =
        DependencyProperty.Register(
            nameof(HorizontalLabelAlignment),
            typeof(HorizontalAlignment),
            typeof(NumberBox),
            new PropertyMetadata(HorizontalAlignment.Left));

    /// <summary>
    ///     Identifies the <see cref="WithPadding" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty WithPaddingProperty =
        DependencyProperty.Register(
            nameof(WithPadding),
            typeof(bool),
            typeof(NumberBox),
            new PropertyMetadata(defaultValue: false, OnWithPaddingPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="IsIndeterminate" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsIndeterminateProperty =
        DependencyProperty.Register(
            nameof(IsIndeterminate),
            typeof(bool),
            typeof(NumberBox),
            new PropertyMetadata(false, OnIsIndeterminatePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="IndeterminateDisplayText" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty IndeterminateDisplayTextProperty =
        DependencyProperty.Register(
            nameof(IndeterminateDisplayText),
            typeof(string),
            typeof(NumberBox),
            new PropertyMetadata(DefaultIndeterminateDisplayText, OnIndeterminateDisplayTextPropertyChanged));

    /// <summary>
    ///     Gets or sets the numeric value of the <see cref="NumberBox" />.
    /// </summary>
    public float NumberValue
    {
        get => (float)this.GetValue(NumberValueProperty);
        set => this.SetValue(NumberValueProperty, value);
    }

    /// <summary>
    ///     Gets or sets the label text of the <see cref="NumberBox" />.
    /// </summary>
    public string Label
    {
        get => (string)this.GetValue(LabelProperty);
        set => this.SetValue(LabelProperty, value);
    }

    /// <summary>
    ///     Gets or sets the position of the label relative to the <see cref="NumberBox" />.
    /// </summary>
    public LabelPosition LabelPosition
    {
        get => (LabelPosition)this.GetValue(LabelPositionProperty);
        set => this.SetValue(LabelPositionProperty, value);
    }

    /// <summary>
    ///     Gets or sets the multiplier used for value adjustments.
    /// </summary>
    public int Multiplier
    {
        get => (int)this.GetValue(MultiplierProperty);
        set => this.SetValue(MultiplierProperty, value);
    }

    /// <summary>
    ///     Gets or sets the mask used for parsing and formatting the value.
    /// </summary>
    public string Mask
    {
        get => (string)this.GetValue(MaskProperty);
        set => this.SetValue(MaskProperty, value);
    }

    /// <summary>
    ///     Gets or sets the display text of the label. This is a dependency property.
    /// </summary>
    /// <value>The display text of the label.</value>
    public string DisplayText
    {
        get => (string)this.GetValue(DisplayTextProperty);
        set => this.SetValue(DisplayTextProperty, value);
    }

    /// <summary>
    ///     Gets or sets the horizontal alignment of the value text.
    /// </summary>
    public TextAlignment HorizontalValueAlignment
    {
        get => (TextAlignment)this.GetValue(HorizontalValueAlignmentProperty);
        set => this.SetValue(HorizontalValueAlignmentProperty, value);
    }

    /// <summary>
    ///     Gets or sets the horizontal alignment of the label text.
    /// </summary>
    public HorizontalAlignment HorizontalLabelAlignment
    {
        get => (HorizontalAlignment)this.GetValue(HorizontalLabelAlignmentProperty);
        set => this.SetValue(HorizontalLabelAlignmentProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether to pad the formatted value with zeros.
    /// </summary>
    public bool WithPadding
    {
        get => (bool)this.GetValue(WithPaddingProperty);
        set => this.SetValue(WithPaddingProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the control is in an indeterminate state.
    /// </summary>
    public bool IsIndeterminate
    {
        get => (bool)this.GetValue(IsIndeterminateProperty);
        set => this.SetValue(IsIndeterminateProperty, value);
    }

    /// <summary>
    ///     Gets or sets the text displayed when the value is indeterminate.
    /// </summary>
    public string IndeterminateDisplayText
    {
        get => (string)this.GetValue(IndeterminateDisplayTextProperty);
        set => this.SetValue(IndeterminateDisplayTextProperty, value);
    }

    private static void OnValuePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnValueChanged();
        }
    }

    private static void OnLabelPositionPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnLabelPositionChanged();
        }
    }

    private static void OnMaskPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnMaskChanged();
        }
    }

    private static void OnWithPaddingPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnWithPaddingChanged();
        }
    }

    private static void OnIsIndeterminatePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnIsIndeterminateChanged();
        }
    }

    private static void OnIndeterminateDisplayTextPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnIndeterminateDisplayTextChanged();
        }
    }
}
