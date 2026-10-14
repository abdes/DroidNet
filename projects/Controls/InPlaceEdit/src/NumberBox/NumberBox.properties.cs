// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display numeric values.
/// </summary>
public partial class NumberBox
{
    /// <summary>Identifies whether an external label moves above the value at narrow widths.</summary>
    public static readonly DependencyProperty AutoStackLabelProperty = RegisterLabelLayoutProperty(nameof(AutoStackLabel), typeof(bool), defaultValue: false);

    /// <summary>Identifies the external label-column width; NaN retains the default proportional layout.</summary>
    public static readonly DependencyProperty LabelWidthProperty = RegisterLabelLayoutProperty(nameof(LabelWidth), typeof(double), double.NaN);

    /// <summary>Identifies the label's share of horizontal space, or NaN to use <see cref="LabelWidth"/>.</summary>
    public static readonly DependencyProperty LabelWidthRatioProperty = RegisterLabelLayoutProperty(nameof(LabelWidthRatio), typeof(double), double.NaN);

    /// <summary>Identifies the horizontal gap between the label and value region.</summary>
    public static readonly DependencyProperty LabelSpacingProperty = RegisterLabelLayoutProperty(nameof(LabelSpacing), typeof(double), 5d);

    /// <summary>Identifies the vertical gap between the label and value region.</summary>
    public static readonly DependencyProperty LabelRowSpacingProperty = RegisterLabelLayoutProperty(nameof(LabelRowSpacing), typeof(double), 5d);

    /// <summary>Identifies the read-only annotation preceding the value field.</summary>
    public static readonly DependencyProperty PrefixProperty = RegisterLabelLayoutProperty(nameof(Prefix), typeof(string), string.Empty);

    /// <summary>Identifies the read-only unit following the value field.</summary>
    public static readonly DependencyProperty UnitProperty = RegisterLabelLayoutProperty(nameof(Unit), typeof(string), string.Empty);

    /// <summary>
    ///     Identifies the <see cref="IsIndeterminate" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsIndeterminateProperty =
        DependencyProperty.Register(
            nameof(IsIndeterminate),
            typeof(bool),
            typeof(NumberBox),
            new PropertyMetadata(defaultValue: false, OnIsIndeterminatePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="NumberValue" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty NumberValueProperty =
        DependencyProperty.Register(
            nameof(NumberValue),
            typeof(double),
            typeof(NumberBox),
            new PropertyMetadata(defaultValue: 0.0d, OnValuePropertyChanged));

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
            new PropertyMetadata(string.Empty, OnLabelPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="LabelPosition" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty LabelPositionProperty =
        DependencyProperty.Register(
            nameof(LabelPosition),
            typeof(LabelPosition),
            typeof(NumberBox),
            new PropertyMetadata(LabelPosition.Left, OnLabelPositionPropertyChanged));

    /// <summary>Identifies the <see cref="LabelForeground"/> dependency property.</summary>
    public static readonly DependencyProperty LabelForegroundProperty = DependencyProperty.Register(
        nameof(LabelForeground),
        typeof(Brush),
        typeof(NumberBox),
        new PropertyMetadata(defaultValue: null, OnLabelForegroundPropertyChanged));

    /// <summary>Identifies the <see cref="IsCompact"/> dependency property.</summary>
    public static readonly DependencyProperty IsCompactProperty = DependencyProperty.Register(
        nameof(IsCompact),
        typeof(bool),
        typeof(NumberBox),
        new PropertyMetadata(defaultValue: false, OnIsCompactPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="Multiplier" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty MultiplierProperty =
        DependencyProperty.Register(
            nameof(Multiplier),
            typeof(double),
            typeof(NumberBox),
            new PropertyMetadata(1d));

    /// <summary>Identifies the <see cref="Minimum" /> dependency property.</summary>
    public static readonly DependencyProperty MinimumProperty =
        DependencyProperty.Register(
            nameof(Minimum),
            typeof(double),
            typeof(NumberBox),
            new PropertyMetadata(double.NegativeInfinity));

    /// <summary>Identifies the <see cref="Maximum" /> dependency property.</summary>
    public static readonly DependencyProperty MaximumProperty =
        DependencyProperty.Register(
            nameof(Maximum),
            typeof(double),
            typeof(NumberBox),
            new PropertyMetadata(double.PositiveInfinity));

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
            new PropertyMetadata(TextAlignment.Center, OnHorizontalValueAlignmentChanged));

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

    /// <summary>Identifies the <see cref="TrimTrailingZeros"/> dependency property.</summary>
    public static readonly DependencyProperty TrimTrailingZerosProperty = DependencyProperty.Register(
        nameof(TrimTrailingZeros),
        typeof(bool),
        typeof(NumberBox),
        new PropertyMetadata(defaultValue: false, static (d, _) => ((NumberBox)d).OnTrimTrailingZerosChanged()));

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
    ///     Identifies the <see cref="LoggerFactory" /> dependency property. Hosts can provide an
    ///     <see cref="ILoggerFactory" /> to enable logging for NumberBox instances.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(LoggerFactory),
        typeof(ILoggerFactory),
        typeof(NumberBox),
        new PropertyMetadata(defaultValue: null, static (d, e) => ((NumberBox)d).OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue)));

    /// <summary>Gets or sets a value indicating whether an external label stacks above its value at narrow widths.</summary>
    public bool AutoStackLabel { get => (bool)this.GetValue(AutoStackLabelProperty); set => this.SetValue(AutoStackLabelProperty, value); }

    /// <summary>Gets or sets the external label width in DIPs, or NaN for proportional columns.</summary>
    public double LabelWidth { get => (double)this.GetValue(LabelWidthProperty); set => this.SetValue(LabelWidthProperty, value); }

    /// <summary>
    /// Gets or sets the label's share of horizontal space after spacing, between zero and one (exclusive).
    /// NaN retains the <see cref="LabelWidth"/> policy; a ratio uses that width only as the auto-stacking minimum.
    /// Proportional labels stay on one line and trim with an ellipsis; value annotations share the remaining column.
    /// </summary>
    public double LabelWidthRatio { get => (double)this.GetValue(LabelWidthRatioProperty); set => this.SetValue(LabelWidthRatioProperty, value); }

    /// <summary>Gets or sets the horizontal label gap in DIPs.</summary>
    public double LabelSpacing { get => (double)this.GetValue(LabelSpacingProperty); set => this.SetValue(LabelSpacingProperty, value); }

    /// <summary>Gets or sets the vertical label gap in DIPs.</summary>
    public double LabelRowSpacing { get => (double)this.GetValue(LabelRowSpacingProperty); set => this.SetValue(LabelRowSpacingProperty, value); }

    /// <summary>Gets or sets the read-only prefix outside the value field.</summary>
    public string Prefix { get => (string)this.GetValue(PrefixProperty); set => this.SetValue(PrefixProperty, value); }

    /// <summary>Gets or sets the read-only unit after the value field, such as m or °.</summary>
    /// <remarks>
    ///     The unit is an annotation outside the value border and is never edited. A unit written in the
    ///     <see cref="Mask"/>, such as <c>~.## m</c>, is part of the formatted value text instead.
    /// </remarks>
    public string Unit { get => (string)this.GetValue(UnitProperty); set => this.SetValue(UnitProperty, value); }

    /// <summary>Gets the width the unit takes after the value field, including its gap, as of the last measure.</summary>
    /// <remarks>Zero without a unit. Containers use it to line up the value fields of rows with different units.</remarks>
    public double UnitWidth => this.unitTextBlock is { Visibility: Visibility.Visible } unit ? unit.DesiredSize.Width : 0;

    /// <summary>Gets the label placement selected during measurement.</summary>
    public LabelPosition ActualLabelPosition { get; private set; }

    /// <summary>Gets or sets a value indicating whether fractional trailing zeros are omitted, retaining a leading zero.</summary>
    public bool TrimTrailingZeros
    {
        get => (bool)this.GetValue(TrimTrailingZerosProperty);
        set => this.SetValue(TrimTrailingZerosProperty, value);
    }

    /// <summary>
    ///     Gets or sets the numeric value of the <see cref="NumberBox" />.
    /// </summary>
    public double NumberValue
    {
        get => (double)this.GetValue(NumberValueProperty);
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

    /// <summary>Gets or sets the optional foreground brush for the label.</summary>
    public Brush? LabelForeground
    {
        get => (Brush?)this.GetValue(LabelForegroundProperty);
        set => this.SetValue(LabelForegroundProperty, value);
    }

    /// <summary>Gets or sets a value indicating whether a left or right label shows inside the value field.</summary>
    /// <remarks>
    ///     A compact label sits inside the field, before the value for <see cref="LabelPosition.Left"/> and after it for
    ///     <see cref="LabelPosition.Right"/>, and the value takes the remaining width. A label above or below the value
    ///     is never compact, so <see cref="LabelPosition.Top"/> and <see cref="LabelPosition.Bottom"/> ignore it.
    /// </remarks>
    public bool IsCompact
    {
        get => (bool)this.GetValue(IsCompactProperty);
        set => this.SetValue(IsCompactProperty, value);
    }

    /// <summary>
    ///     Gets or sets the change one drag step makes; Shift makes it a hundredth for small moves and a tenth for
    ///     large ones. Use a fraction for values with a small range, such as 0.01 for 0 to 1.
    /// </summary>
    public double Multiplier
    {
        get => (double)this.GetValue(MultiplierProperty);
        set => this.SetValue(MultiplierProperty, value);
    }

    /// <summary>
    ///     Gets or sets the smallest value user input may give: a drag stops there and a smaller typed value is invalid.
    ///     A value set by code beyond it is kept and shows as invalid.
    /// </summary>
    public double Minimum
    {
        get => (double)this.GetValue(MinimumProperty);
        set => this.SetValue(MinimumProperty, value);
    }

    /// <summary>
    ///     Gets or sets the largest value user input may give: a drag stops there and a larger typed value is invalid.
    ///     A value set by code beyond it is kept and shows as invalid.
    /// </summary>
    public double Maximum
    {
        get => (double)this.GetValue(MaximumProperty);
        set => this.SetValue(MaximumProperty, value);
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

    /// <summary>
    ///     Gets or sets the <see cref="ILoggerFactory" /> used to create a logger for this control.
    ///     Assigning the factory will initialize the internal logger to a non-null logger instance
    ///     (falls back to <see cref="NullLoggerFactory.Instance"/> if null).
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get => (ILoggerFactory?)this.GetValue(LoggerFactoryProperty);
        set => this.SetValue(LoggerFactoryProperty, value);
    }

    private static DependencyProperty RegisterLabelLayoutProperty(string name, Type type, object defaultValue)
        => DependencyProperty.Register(name, type, typeof(NumberBox), new PropertyMetadata(defaultValue, static (d, _) => ((NumberBox)d).UpdateLabelPosition()));

    private static void OnValuePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            var oldValue = e.OldValue is double previous ? previous : 0d;
            var newValue = e.NewValue is double current ? current : 0d;
            numberBox.OnValueChanged(oldValue, newValue);
        }
    }

    private static void OnLabelPositionPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnLabelPositionChanged();
        }
    }

    private static void OnLabelPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnLabelPositionChanged();
        }
    }

    private static void OnLabelForegroundPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnLabelForegroundChanged();
        }
    }

    private static void OnIsCompactPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
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

    private static void OnHorizontalValueAlignmentChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NumberBox numberBox)
        {
            numberBox.OnHorizontalValueAlignmentChanged();
        }
    }

    // Initialize the logger for this NumberBox. Use the NumberBox type as the category.
    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory) =>
        this.logger = loggerFactory?.CreateLogger<NumberBox>() ?? NullLoggerFactory.Instance.CreateLogger<NumberBox>();
}
