// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display small fixed-size numeric vectors of two, three or
///     four components.
/// </summary>
public partial class VectorBox
{
    /// <summary>The smallest supported <see cref="Dimension"/>.</summary>
    public const int MinimumDimension = 2;

    /// <summary>The largest supported <see cref="Dimension"/>.</summary>
    public const int MaximumDimension = 4;

    /// <summary>Identifies whether channels stack when their usable width cannot be preserved.</summary>
    public static readonly DependencyProperty AutoStackComponentsProperty = DependencyProperty.Register(
        nameof(AutoStackComponents),
        typeof(bool),
        typeof(VectorBox),
        new PropertyMetadata(defaultValue: false, static (d, _) => ((VectorBox)d).InvalidateMeasure()));

    /// <summary>Identifies the minimum usable width per channel.</summary>
    public static readonly DependencyProperty ComponentMinimumWidthProperty = DependencyProperty.Register(
        nameof(ComponentMinimumWidth),
        typeof(double),
        typeof(VectorBox),
        new PropertyMetadata(80d, static (d, _) => ((VectorBox)d).InvalidateMeasure()));

    /// <summary>Identifies the <see cref="Dimension" /> dependency property.</summary>
    public static readonly DependencyProperty DimensionProperty = DependencyProperty.Register(
        nameof(Dimension),
        typeof(int),
        typeof(VectorBox),
        new PropertyMetadata(3, static (d, _) => ((VectorBox)d).OnDimensionChanged()));

    /// <summary>Identifies the <see cref="XValue" /> dependency property.</summary>
    public static readonly DependencyProperty XValueProperty = RegisterComponentValue(nameof(XValue), Component.X);

    /// <summary>Identifies the <see cref="YValue" /> dependency property.</summary>
    public static readonly DependencyProperty YValueProperty = RegisterComponentValue(nameof(YValue), Component.Y);

    /// <summary>Identifies the <see cref="ZValue" /> dependency property.</summary>
    public static readonly DependencyProperty ZValueProperty = RegisterComponentValue(nameof(ZValue), Component.Z);

    /// <summary>Identifies the <see cref="WValue" /> dependency property.</summary>
    public static readonly DependencyProperty WValueProperty = RegisterComponentValue(nameof(WValue), Component.W);

    /// <summary>Identifies the <see cref="XIsIndeterminate" /> dependency property.</summary>
    public static readonly DependencyProperty XIsIndeterminateProperty = RegisterComponentIndeterminate(nameof(XIsIndeterminate), Component.X);

    /// <summary>Identifies the <see cref="YIsIndeterminate" /> dependency property.</summary>
    public static readonly DependencyProperty YIsIndeterminateProperty = RegisterComponentIndeterminate(nameof(YIsIndeterminate), Component.Y);

    /// <summary>Identifies the <see cref="ZIsIndeterminate" /> dependency property.</summary>
    public static readonly DependencyProperty ZIsIndeterminateProperty = RegisterComponentIndeterminate(nameof(ZIsIndeterminate), Component.Z);

    /// <summary>Identifies the <see cref="WIsIndeterminate" /> dependency property.</summary>
    public static readonly DependencyProperty WIsIndeterminateProperty = RegisterComponentIndeterminate(nameof(WIsIndeterminate), Component.W);

    /// <summary>Identifies the <see cref="ComponentMask" /> dependency property.</summary>
    public static readonly DependencyProperty ComponentMaskProperty = DependencyProperty.Register(
        nameof(ComponentMask),
        typeof(string),
        typeof(VectorBox),
        new PropertyMetadata(DefaultComponentMask, static (d, _) => ((VectorBox)d).OnComponentMaskChanged()));

    /// <summary>Identifies the <see cref="IndeterminateDisplayText" /> dependency property.</summary>
    public static readonly DependencyProperty IndeterminateDisplayTextProperty = DependencyProperty.Register(
        nameof(IndeterminateDisplayText),
        typeof(string),
        typeof(VectorBox),
        new PropertyMetadata(DefaultIndeterminateDisplayText, static (d, _) => ((VectorBox)d).ForEachEditor(static (owner, box) => box.IndeterminateDisplayText = owner.IndeterminateDisplayText)));

    /// <summary>Identifies the <see cref="Label" /> dependency property.</summary>
    public static readonly DependencyProperty LabelProperty = DependencyProperty.Register(
        nameof(Label),
        typeof(string),
        typeof(VectorBox),
        new PropertyMetadata(string.Empty, static (d, _) => ((VectorBox)d).UpdateLabelPosition()));

    /// <summary>Identifies the <see cref="LabelPosition" /> dependency property.</summary>
    public static readonly DependencyProperty LabelPositionProperty = DependencyProperty.Register(
        nameof(LabelPosition),
        typeof(LabelPosition),
        typeof(VectorBox),
        new PropertyMetadata(LabelPosition.Left, static (d, _) => ((VectorBox)d).UpdateLabelPosition()));

    /// <summary>Identifies the <see cref="HorizontalValueAlignment" /> dependency property.</summary>
    public static readonly DependencyProperty HorizontalValueAlignmentProperty = DependencyProperty.Register(
        nameof(HorizontalValueAlignment),
        typeof(TextAlignment),
        typeof(VectorBox),
        new PropertyMetadata(TextAlignment.Center, static (d, _) => ((VectorBox)d).ForEachEditor(static (owner, box) => box.HorizontalValueAlignment = owner.HorizontalValueAlignment)));

    /// <summary>Identifies the <see cref="HorizontalLabelAlignment" /> dependency property.</summary>
    public static readonly DependencyProperty HorizontalLabelAlignmentProperty = DependencyProperty.Register(
        nameof(HorizontalLabelAlignment),
        typeof(HorizontalAlignment),
        typeof(VectorBox),
        new PropertyMetadata(HorizontalAlignment.Left));

    /// <summary>Identifies the <see cref="ComponentLabelPosition" /> dependency property.</summary>
    public static readonly DependencyProperty ComponentLabelPositionProperty = DependencyProperty.Register(
        nameof(ComponentLabelPosition),
        typeof(LabelPosition),
        typeof(VectorBox),
        new PropertyMetadata(LabelPosition.None, static (d, _) => ((VectorBox)d).UpdateComponentLabelPositions()));

    /// <summary>Identifies the <see cref="CompactComponentLabels" /> dependency property.</summary>
    public static readonly DependencyProperty CompactComponentLabelsProperty = DependencyProperty.Register(
        nameof(CompactComponentLabels),
        typeof(bool),
        typeof(VectorBox),
        new PropertyMetadata(defaultValue: false, static (d, _) => ((VectorBox)d).UpdateComponentLabelPositions()));

    /// <summary>Identifies the <see cref="Multiplier" /> dependency property.</summary>
    public static readonly DependencyProperty MultiplierProperty = DependencyProperty.Register(
        nameof(Multiplier),
        typeof(double),
        typeof(VectorBox),
        new PropertyMetadata(1d, static (d, _) => ((VectorBox)d).ForEachEditor(static (owner, box) => box.Multiplier = owner.Multiplier)));

    /// <summary>Identifies the <see cref="Minimum" /> dependency property.</summary>
    public static readonly DependencyProperty MinimumProperty = DependencyProperty.Register(
        nameof(Minimum),
        typeof(double),
        typeof(VectorBox),
        new PropertyMetadata(double.NegativeInfinity, static (d, _) => ((VectorBox)d).UpdateComponentRanges()));

    /// <summary>Identifies the <see cref="Maximum" /> dependency property.</summary>
    public static readonly DependencyProperty MaximumProperty = DependencyProperty.Register(
        nameof(Maximum),
        typeof(double),
        typeof(VectorBox),
        new PropertyMetadata(double.PositiveInfinity, static (d, _) => ((VectorBox)d).UpdateComponentRanges()));

    /// <summary>Identifies the <see cref="WithPadding" /> dependency property.</summary>
    public static readonly DependencyProperty WithPaddingProperty = DependencyProperty.Register(
        nameof(WithPadding),
        typeof(bool),
        typeof(VectorBox),
        new PropertyMetadata(defaultValue: false, static (d, _) => ((VectorBox)d).ForEachEditor(static (owner, box) => box.WithPadding = owner.WithPadding)));

    /// <summary>Identifies the <see cref="ComponentStyle"/> dependency property.</summary>
    public static readonly DependencyProperty ComponentStyleProperty = DependencyProperty.Register(
        nameof(ComponentStyle),
        typeof(Style),
        typeof(VectorBox),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     Identifies the <see cref="LoggerFactory" /> dependency property. Hosts can provide an
    ///     <see cref="ILoggerFactory" /> to enable logging for VectorBox and its inner NumberBox components.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(LoggerFactory),
        typeof(ILoggerFactory),
        typeof(VectorBox),
        new PropertyMetadata(defaultValue: null, static (d, e) => ((VectorBox)d).OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue)));

    /// <summary>Gets or sets a value indicating whether channels stack vertically at narrow widths.</summary>
    public bool AutoStackComponents
    {
        get => (bool)this.GetValue(AutoStackComponentsProperty);
        set => this.SetValue(AutoStackComponentsProperty, value);
    }

    /// <summary>Gets or sets the minimum usable channel width in DIPs.</summary>
    public double ComponentMinimumWidth
    {
        get => (double)this.GetValue(ComponentMinimumWidthProperty);
        set => this.SetValue(ComponentMinimumWidthProperty, value);
    }

    /// <summary>
    ///     Gets or sets the number of components shown, from <see cref="MinimumDimension"/> to
    ///     <see cref="MaximumDimension"/>; other values show the nearest supported number. It can change at any time.
    /// </summary>
    public int Dimension
    {
        get => (int)this.GetValue(DimensionProperty);
        set => this.SetValue(DimensionProperty, value);
    }

    /// <summary>Gets or sets the X component numeric value.</summary>
    public double XValue
    {
        get => (double)this.GetValue(XValueProperty);
        set => this.SetValue(XValueProperty, value);
    }

    /// <summary>Gets or sets the Y component numeric value.</summary>
    public double YValue
    {
        get => (double)this.GetValue(YValueProperty);
        set => this.SetValue(YValueProperty, value);
    }

    /// <summary>Gets or sets the Z component numeric value.</summary>
    public double ZValue
    {
        get => (double)this.GetValue(ZValueProperty);
        set => this.SetValue(ZValueProperty, value);
    }

    /// <summary>Gets or sets the W component numeric value.</summary>
    public double WValue
    {
        get => (double)this.GetValue(WValueProperty);
        set => this.SetValue(WValueProperty, value);
    }

    /// <summary>Gets or sets a value indicating whether the X component is in an indeterminate presentation state.</summary>
    public bool XIsIndeterminate
    {
        get => (bool)this.GetValue(XIsIndeterminateProperty);
        set => this.SetValue(XIsIndeterminateProperty, value);
    }

    /// <summary>Gets or sets a value indicating whether the Y component is in an indeterminate presentation state.</summary>
    public bool YIsIndeterminate
    {
        get => (bool)this.GetValue(YIsIndeterminateProperty);
        set => this.SetValue(YIsIndeterminateProperty, value);
    }

    /// <summary>Gets or sets a value indicating whether the Z component is in an indeterminate presentation state.</summary>
    public bool ZIsIndeterminate
    {
        get => (bool)this.GetValue(ZIsIndeterminateProperty);
        set => this.SetValue(ZIsIndeterminateProperty, value);
    }

    /// <summary>Gets or sets a value indicating whether the W component is in an indeterminate presentation state.</summary>
    public bool WIsIndeterminate
    {
        get => (bool)this.GetValue(WIsIndeterminateProperty);
        set => this.SetValue(WIsIndeterminateProperty, value);
    }

    /// <summary>Gets or sets the default mask applied to all components.</summary>
    public string ComponentMask
    {
        get => (string)this.GetValue(ComponentMaskProperty);
        set => this.SetValue(ComponentMaskProperty, value);
    }

    /// <summary>Gets or sets the text displayed when a component is indeterminate.</summary>
    public string IndeterminateDisplayText
    {
        get => (string)this.GetValue(IndeterminateDisplayTextProperty);
        set => this.SetValue(IndeterminateDisplayTextProperty, value);
    }

    /// <summary>Gets or sets the control-level label text.</summary>
    public string Label
    {
        get => (string)this.GetValue(LabelProperty);
        set => this.SetValue(LabelProperty, value);
    }

    /// <summary>Gets or sets the position of the control-level label.</summary>
    public LabelPosition LabelPosition
    {
        get => (LabelPosition)this.GetValue(LabelPositionProperty);
        set => this.SetValue(LabelPositionProperty, value);
    }

    /// <summary>Gets or sets the horizontal alignment of numeric values in component editors.</summary>
    public TextAlignment HorizontalValueAlignment
    {
        get => (TextAlignment)this.GetValue(HorizontalValueAlignmentProperty);
        set => this.SetValue(HorizontalValueAlignmentProperty, value);
    }

    /// <summary>Gets or sets the horizontal alignment of the control-level label.</summary>
    public HorizontalAlignment HorizontalLabelAlignment
    {
        get => (HorizontalAlignment)this.GetValue(HorizontalLabelAlignmentProperty);
        set => this.SetValue(HorizontalLabelAlignmentProperty, value);
    }

    /// <summary>Gets or sets the default position of per-component labels.</summary>
    public LabelPosition ComponentLabelPosition
    {
        get => (LabelPosition)this.GetValue(ComponentLabelPositionProperty);
        set => this.SetValue(ComponentLabelPositionProperty, value);
    }

    /// <summary>Gets or sets a value indicating whether left or right component labels show inside their fields.</summary>
    /// <remarks>Each component's <see cref="NumberBox.IsCompact"/>; top and bottom labels are never compact.</remarks>
    public bool CompactComponentLabels
    {
        get => (bool)this.GetValue(CompactComponentLabelsProperty);
        set => this.SetValue(CompactComponentLabelsProperty, value);
    }

    /// <summary>Gets or sets the change one label-drag step makes to a component; see <see cref="NumberBox.Multiplier"/>.</summary>
    public double Multiplier
    {
        get => (double)this.GetValue(MultiplierProperty);
        set => this.SetValue(MultiplierProperty, value);
    }

    /// <summary>Gets or sets the smallest value user input gives a component, unless
    /// <see cref="SetComponentRange"/> sets its own.</summary>
    public double Minimum
    {
        get => (double)this.GetValue(MinimumProperty);
        set => this.SetValue(MinimumProperty, value);
    }

    /// <summary>Gets or sets the largest value user input gives a component, unless
    /// <see cref="SetComponentRange"/> sets its own.</summary>
    public double Maximum
    {
        get => (double)this.GetValue(MaximumProperty);
        set => this.SetValue(MaximumProperty, value);
    }

    /// <summary>Gets or sets a value indicating whether to pad numeric components with zeros.</summary>
    public bool WithPadding
    {
        get => (bool)this.GetValue(WithPaddingProperty);
        set => this.SetValue(WithPaddingProperty, value);
    }

    /// <summary>Gets or sets the style of each NumberBox without replacing the vector editor template.</summary>
    public Style? ComponentStyle
    {
        get => (Style?)this.GetValue(ComponentStyleProperty);
        set => this.SetValue(ComponentStyleProperty, value);
    }

    /// <summary>Gets the per-component mask overrides.</summary>
    public IDictionary<Component, string> ComponentMasks => this.componentMasks;

    /// <summary>Gets the per-component label position overrides.</summary>
    public IDictionary<Component, LabelPosition> ComponentLabelPositions => this.componentLabelPositions;

    /// <summary>Gets the per-component label text overrides, such as <c>R</c>, <c>G</c>, <c>B</c> and <c>A</c>.</summary>
    public IDictionary<Component, string> ComponentLabels => this.componentLabels;

    /// <summary>Gets the per-component label foreground overrides.</summary>
    public IDictionary<Component, Brush> ComponentLabelForegrounds => this.componentLabelForegrounds;

    /// <summary>Gets or sets the <see cref="ILoggerFactory" /> used to create loggers for this control and children.</summary>
    public ILoggerFactory? LoggerFactory
    {
        get => (ILoggerFactory?)this.GetValue(LoggerFactoryProperty);
        set => this.SetValue(LoggerFactoryProperty, value);
    }

    private static DependencyProperty RegisterComponentValue(string propertyName, Component component)
        => DependencyProperty.Register(
            propertyName,
            typeof(double),
            typeof(VectorBox),
            new PropertyMetadata(0.0d, (d, e) => ((VectorBox)d).OnComponentValueChanged(component, (double)e.OldValue, (double)e.NewValue)));

    private static DependencyProperty RegisterComponentIndeterminate(string propertyName, Component component)
        => DependencyProperty.Register(
            propertyName,
            typeof(bool),
            typeof(VectorBox),
            new PropertyMetadata(defaultValue: false, (d, _) => ((VectorBox)d).OnComponentIndeterminateChanged(component)));

    private void OnLoggerFactoryChanged(ILoggerFactory? factory)
    {
        // Initialize the logger for this VectorBox and propagate the factory to child NumberBox instances.
        this.logger = factory?.CreateLogger<VectorBox>() ?? NullLoggerFactory.Instance.CreateLogger<VectorBox>();
        this.LogLoggerFactoryChanged(factory is not null);
        this.ForEachEditor(static (owner, box) => box.LoggerFactory = owner.LoggerFactory);
    }
}
