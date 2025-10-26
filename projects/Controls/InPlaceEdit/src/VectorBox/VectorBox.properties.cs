// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display small fixed-size numeric vectors (Vector2/Vector3).
/// </summary>
public partial class VectorBox
{
    /// <summary>
    ///     Identifies the <see cref="VectorValue" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty VectorValueProperty =
        DependencyProperty.Register(
            nameof(VectorValue),
            typeof(Vector3),
            typeof(VectorBox),
            new PropertyMetadata(new Vector3(0.0f, 0.0f, 0.0f), OnVectorValuePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="Dimension" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty DimensionProperty =
        DependencyProperty.Register(
            nameof(Dimension),
            typeof(int),
            typeof(VectorBox),
            new PropertyMetadata(3, OnDimensionPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="XValue" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty XValueProperty =
        DependencyProperty.Register(
            nameof(XValue),
            typeof(float),
            typeof(VectorBox),
            new PropertyMetadata(0.0f, OnXValuePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="YValue" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty YValueProperty =
        DependencyProperty.Register(
            nameof(YValue),
            typeof(float),
            typeof(VectorBox),
            new PropertyMetadata(0.0f, OnYValuePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="ZValue" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty ZValueProperty =
        DependencyProperty.Register(
            nameof(ZValue),
            typeof(float),
            typeof(VectorBox),
            new PropertyMetadata(0.0f, OnZValuePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="XIsIndeterminate" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty XIsIndeterminateProperty =
        DependencyProperty.Register(
            nameof(XIsIndeterminate),
            typeof(bool),
            typeof(VectorBox),
            new PropertyMetadata(defaultValue: false, OnXIsIndeterminatePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="YIsIndeterminate" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty YIsIndeterminateProperty =
        DependencyProperty.Register(
            nameof(YIsIndeterminate),
            typeof(bool),
            typeof(VectorBox),
            new PropertyMetadata(defaultValue: false, OnYIsIndeterminatePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="ZIsIndeterminate" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty ZIsIndeterminateProperty =
        DependencyProperty.Register(
            nameof(ZIsIndeterminate),
            typeof(bool),
            typeof(VectorBox),
            new PropertyMetadata(defaultValue: false, OnZIsIndeterminatePropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="ComponentMask" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty ComponentMaskProperty =
        DependencyProperty.Register(
            nameof(ComponentMask),
            typeof(string),
            typeof(VectorBox),
            new PropertyMetadata(DefaultComponentMask, OnComponentMaskPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="IndeterminateDisplayText" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty IndeterminateDisplayTextProperty =
        DependencyProperty.Register(
            nameof(IndeterminateDisplayText),
            typeof(string),
            typeof(VectorBox),
            new PropertyMetadata(DefaultIndeterminateDisplayText, OnIndeterminateDisplayTextPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="Label" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty LabelProperty =
        DependencyProperty.Register(
            nameof(Label),
            typeof(string),
            typeof(VectorBox),
            new PropertyMetadata(string.Empty));

    /// <summary>
    ///     Identifies the <see cref="LabelPosition" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty LabelPositionProperty =
        DependencyProperty.Register(
            nameof(LabelPosition),
            typeof(LabelPosition),
            typeof(VectorBox),
            new PropertyMetadata(LabelPosition.Left, OnLabelPositionPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="HorizontalValueAlignment" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty HorizontalValueAlignmentProperty =
        DependencyProperty.Register(
            nameof(HorizontalValueAlignment),
            typeof(TextAlignment),
            typeof(VectorBox),
            new PropertyMetadata(TextAlignment.Center, OnHorizontalValueAlignmentChanged));

    /// <summary>
    ///     Identifies the <see cref="HorizontalLabelAlignment" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty HorizontalLabelAlignmentProperty =
        DependencyProperty.Register(
            nameof(HorizontalLabelAlignment),
            typeof(HorizontalAlignment),
            typeof(VectorBox),
            new PropertyMetadata(HorizontalAlignment.Left));

    /// <summary>
    ///     Identifies the <see cref="ComponentLabelPosition" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty ComponentLabelPositionProperty =
        DependencyProperty.Register(
            nameof(ComponentLabelPosition),
            typeof(LabelPosition),
            typeof(VectorBox),
            new PropertyMetadata(LabelPosition.None, OnComponentLabelPositionPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="Multiplier" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty MultiplierProperty =
        DependencyProperty.Register(
            nameof(Multiplier),
            typeof(int),
            typeof(VectorBox),
            new PropertyMetadata(1, OnMultiplierPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="WithPadding" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty WithPaddingProperty =
        DependencyProperty.Register(
            nameof(WithPadding),
            typeof(bool),
            typeof(VectorBox),
            new PropertyMetadata(defaultValue: false, OnWithPaddingPropertyChanged));

    /// <summary>
    ///     Gets or sets the aggregate numeric vector value.
    /// </summary>
    public Vector3 VectorValue
    {
        get => (Vector3)this.GetValue(VectorValueProperty);
        set => this.SetValue(VectorValueProperty, value);
    }

    /// <summary>
    ///     Gets or sets the dimension of the vector (2 or 3).
    /// </summary>
    public int Dimension
    {
        get => (int)this.GetValue(DimensionProperty);
        set => this.SetValue(DimensionProperty, value);
    }

    /// <summary>
    ///     Gets or sets the X component numeric value (proxy to the internal X editor).
    /// </summary>
    public float XValue
    {
        get => (float)this.GetValue(XValueProperty);
        set => this.SetValue(XValueProperty, value);
    }

    /// <summary>
    ///     Gets or sets the Y component numeric value (proxy to the internal Y editor).
    /// </summary>
    public float YValue
    {
        get => (float)this.GetValue(YValueProperty);
        set => this.SetValue(YValueProperty, value);
    }

    /// <summary>
    ///     Gets or sets the Z component numeric value (proxy to the internal Z editor).
    /// </summary>
    public float ZValue
    {
        get => (float)this.GetValue(ZValueProperty);
        set => this.SetValue(ZValueProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the X component is in an indeterminate presentation state.
    /// </summary>
    public bool XIsIndeterminate
    {
        get => (bool)this.GetValue(XIsIndeterminateProperty);
        set => this.SetValue(XIsIndeterminateProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the Y component is in an indeterminate presentation state.
    /// </summary>
    public bool YIsIndeterminate
    {
        get => (bool)this.GetValue(YIsIndeterminateProperty);
        set => this.SetValue(YIsIndeterminateProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the Z component is in an indeterminate presentation state.
    /// </summary>
    public bool ZIsIndeterminate
    {
        get => (bool)this.GetValue(ZIsIndeterminateProperty);
        set => this.SetValue(ZIsIndeterminateProperty, value);
    }

    /// <summary>
    ///     Gets or sets the default mask applied to all components.
    /// </summary>
    public string ComponentMask
    {
        get => (string)this.GetValue(ComponentMaskProperty);
        set => this.SetValue(ComponentMaskProperty, value);
    }

    /// <summary>
    ///     Gets or sets the text displayed when a component is indeterminate.
    /// </summary>
    public string IndeterminateDisplayText
    {
        get => (string)this.GetValue(IndeterminateDisplayTextProperty);
        set => this.SetValue(IndeterminateDisplayTextProperty, value);
    }

    /// <summary>
    ///     Gets or sets the control-level label text.
    /// </summary>
    public string Label
    {
        get => (string)this.GetValue(LabelProperty);
        set => this.SetValue(LabelProperty, value);
    }

    /// <summary>
    ///     Gets or sets the position of the control-level label.
    /// </summary>
    public LabelPosition LabelPosition
    {
        get => (LabelPosition)this.GetValue(LabelPositionProperty);
        set => this.SetValue(LabelPositionProperty, value);
    }

    /// <summary>
    ///     Gets or sets the horizontal alignment of numeric values in component editors.
    /// </summary>
    public TextAlignment HorizontalValueAlignment
    {
        get => (TextAlignment)this.GetValue(HorizontalValueAlignmentProperty);
        set => this.SetValue(HorizontalValueAlignmentProperty, value);
    }

    /// <summary>
    ///     Gets or sets the horizontal alignment of the control-level label.
    /// </summary>
    public HorizontalAlignment HorizontalLabelAlignment
    {
        get => (HorizontalAlignment)this.GetValue(HorizontalLabelAlignmentProperty);
        set => this.SetValue(HorizontalLabelAlignmentProperty, value);
    }

    /// <summary>
    ///     Gets or sets the default position of per-component labels.
    /// </summary>
    public LabelPosition ComponentLabelPosition
    {
        get => (LabelPosition)this.GetValue(ComponentLabelPositionProperty);
        set => this.SetValue(ComponentLabelPositionProperty, value);
    }

    /// <summary>
    ///     Gets or sets the multiplier for value adjustments during keyboard/wheel/drag operations.
    /// </summary>
    public int Multiplier
    {
        get => (int)this.GetValue(MultiplierProperty);
        set => this.SetValue(MultiplierProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether to pad numeric components with zeros.
    /// </summary>
    public bool WithPadding
    {
        get => (bool)this.GetValue(WithPaddingProperty);
        set => this.SetValue(WithPaddingProperty, value);
    }

    /// <summary>
    ///     Gets the per-component mask overrides. Keys are "X", "Y", "Z".
    /// </summary>
    public IDictionary<string, string> ComponentMasks =>
        this.componentMasks ??= new Dictionary<string, string>(StringComparer.Ordinal);

    /// <summary>
    ///     Gets the per-component label position overrides. Keys are "X", "Y", "Z".
    /// </summary>
    public IDictionary<string, LabelPosition> ComponentLabelPositions =>
        this.componentLabelPositions ??= new Dictionary<string, LabelPosition>(StringComparer.Ordinal);

    private static void OnVectorValuePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnVectorValueChanged();
        }
    }

    private static void OnXValuePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnXValueChanged((float)e.OldValue, (float)e.NewValue);
        }
    }

    private static void OnYValuePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnYValueChanged((float)e.OldValue, (float)e.NewValue);
        }
    }

    private static void OnZValuePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnZValueChanged((float)e.OldValue, (float)e.NewValue);
        }
    }

    private static void OnXIsIndeterminatePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnXIsIndeterminateChanged();
        }
    }

    private static void OnYIsIndeterminatePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnYIsIndeterminateChanged();
        }
    }

    private static void OnZIsIndeterminatePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnZIsIndeterminateChanged();
        }
    }

    private static void OnComponentMaskPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnComponentMaskChanged();
        }
    }

    private static void OnIndeterminateDisplayTextPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnIndeterminateDisplayTextChanged();
        }
    }

    private static void OnLabelPositionPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnLabelPositionChanged();
        }
    }

    private static void OnComponentLabelPositionPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnComponentLabelPositionChanged();
        }
    }

    private static void OnMultiplierPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnMultiplierChanged();
        }
    }

    private static void OnWithPaddingPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnWithPaddingChanged();
        }
    }

    private static void OnDimensionPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnDimensionChanged();
        }
    }

    private static void OnHorizontalValueAlignmentChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is VectorBox vectorBox)
        {
            vectorBox.OnHorizontalValueAlignmentChanged();
        }
    }
}
