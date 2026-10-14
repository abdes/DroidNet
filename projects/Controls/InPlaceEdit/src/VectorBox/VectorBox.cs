// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using Windows.UI.ViewManagement;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display small fixed-size numeric vectors of two, three or
///     four components, such as positions, quaternions and colors with alpha.
/// </summary>
/// <remarks>
///     The <see cref="VectorBox" /> control provides a clean, XAML-friendly editor for vectors through coordinated
///     per-component <see cref="NumberBox" /> editors. It matches the behavior and conventions of <see cref="NumberBox" />
///     while managing multiple numeric components.
///     <para>
///     Each component (X, Y, Z, W) is presented through a writable dependency property (<see cref="XValue" /> to
///     <see cref="WValue" />) and has an independent indeterminate presentation flag (<see cref="XIsIndeterminate" /> to
///     <see cref="WIsIndeterminate" />) useful for multi-selection UI scenarios. <see cref="Dimension"/> selects how many
///     are shown.
///     </para>
///     <para>
///     The control orchestrates per-component validation through its <see cref="Validate" /> event, which relays component-specific
///     validation requests from internal editors with target discrimination.
///     </para>
/// </remarks>
/// <example>
///     <para>
///         <strong>Example Usage</strong>
///     </para>
///     <code lang="xaml"><![CDATA[
///     <controls:VectorBox
///         x:Name="vectorBox"
///         Dimension="4"
///         XValue="0"
///         YValue="0"
///         ZValue="0"
///         WValue="1"
///         Label="Rotation"
///         LabelPosition="Left"
///         ComponentLabelPosition="Left"
///         ComponentMask="~.##"
///         Validate="VectorBox_Validate" />
///     ]]></code>
/// </example>
[TemplatePart(Name = RootGridPartName, Type = typeof(CustomGrid))]
[TemplatePart(Name = BackgroundBorderPartName, Type = typeof(Border))]
[TemplatePart(Name = LabelTextBlockPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = ComponentPanelPartName, Type = typeof(Panel))]
[TemplatePart(Name = NumberBoxPartPrefix + "X", Type = typeof(NumberBox))]
[TemplatePart(Name = NumberBoxPartPrefix + "Y", Type = typeof(NumberBox))]
[TemplatePart(Name = NumberBoxPartPrefix + "Z", Type = typeof(NumberBox))]
[TemplatePart(Name = NumberBoxPartPrefix + "W", Type = typeof(NumberBox))]
[TemplateVisualState(Name = NormalStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = HoverStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = PressedStateName, GroupName = CommonStatesGroupName)]
public partial class VectorBox : Control
{
    private const string RootGridPartName = "PartRootGrid";
    private const string BackgroundBorderPartName = "PartBackgroundBorder";
    private const string LabelTextBlockPartName = "PartLabelTextBlock";
    private const string ComponentPanelPartName = "PartComponentPanel";
    private const string NumberBoxPartPrefix = "PartNumberBox";

    private const string CommonStatesGroupName = "CommonStates";
    private const string NormalStateName = "Normal";
    private const string HoverStateName = "Hover";
    private const string PressedStateName = "Pressed";

    private const string DefaultComponentMask = "~.#";
    private const string DefaultIndeterminateDisplayText = "-.-";

    private readonly UISettings uiSettings = new();
    private readonly ComponentPart[] parts =
    [
        new(Component.X, XValueProperty, XIsIndeterminateProperty),
        new(Component.Y, YValueProperty, YIsIndeterminateProperty),
        new(Component.Z, ZValueProperty, ZIsIndeterminateProperty),
        new(Component.W, WValueProperty, WIsIndeterminateProperty),
    ];

    private readonly ComponentMap<string> componentMasks;
    private readonly ComponentMap<LabelPosition> componentLabelPositions;
    private readonly ComponentMap<string> componentLabels;
    private readonly ComponentMap<Brush> componentLabelForegrounds;
    private readonly Dictionary<Component, (double minimum, double maximum)> componentRanges = [];

    private CustomGrid? rootGrid;
    private Border? backgroundBorder;
    private TextBlock? labelTextBlock;
    private Panel? componentPanel;
    private ILogger? logger;

    private bool isSyncingValues;
    private bool? stackedComponents;

    /// <summary>
    ///     Initializes a new instance of the <see cref="VectorBox" /> class.
    /// </summary>
    public VectorBox()
    {
        this.DefaultStyleKey = typeof(VectorBox);
        this.componentMasks = new(this.OnComponentMaskChanged);
        this.componentLabelPositions = new(this.UpdateComponentLabelPositions);
        this.componentLabels = new(this.UpdateComponentLabelPositions);
        this.componentLabelForegrounds = new(this.UpdateComponentLabelPositions);
    }

    /// <summary>Gets a value indicating whether the current available width requires vertical component editors.</summary>
    public bool AreComponentsStacked => this.stackedComponents == true;

    /// <summary>Gets the number of components shown: <see cref="Dimension"/>, limited to the supported range.</summary>
    private int ComponentCount => Math.Clamp(this.Dimension, MinimumDimension, MaximumDimension);

    /// <summary>Gets the shown components, in order.</summary>
    private ReadOnlySpan<ComponentPart> ShownParts => this.parts.AsSpan(0, this.ComponentCount);

    /// <summary>Sets the range user input gives one component, in place of <see cref="Minimum"/> and
    /// <see cref="Maximum"/>, such as 0 to 1 for the alpha of a color whose other channels are unbounded.</summary>
    /// <param name="component">The component.</param>
    /// <param name="minimum">The smallest value.</param>
    /// <param name="maximum">The largest value.</param>
    public void SetComponentRange(Component component, double minimum, double maximum)
    {
        this.componentRanges[component] = (minimum, maximum);
        this.UpdateComponentRanges();
    }

    /// <summary>Makes a component use <see cref="Minimum"/> and <see cref="Maximum"/> again.</summary>
    /// <param name="component">The component.</param>
    public void ClearComponentRange(Component component)
    {
        _ = this.componentRanges.Remove(component);
        this.UpdateComponentRanges();
    }

    /// <summary>
    ///     Atomically updates the shown components in a single operation.
    /// </summary>
    /// <param name="values">One value per shown component, in X, Y, Z, W order.</param>
    /// <param name="preserveIndeterminate">Whether to keep the components' indeterminate flags.</param>
    /// <exception cref="ArgumentException">The number of values is not <see cref="Dimension"/>.</exception>
    public void SetValues(ReadOnlySpan<double> values, bool preserveIndeterminate = false)
    {
        if (values.Length != this.ComponentCount)
        {
            throw new ArgumentException(string.Create(CultureInfo.InvariantCulture, $"Expected {this.ComponentCount} values, one per shown component."), nameof(values));
        }

        this.LogSetValues(values.Length, preserveIndeterminate);
        try
        {
            this.isSyncingValues = true;
            for (var index = 0; index < values.Length; index++)
            {
                var part = this.parts[index];
                this.SetValue(part.ValueProperty, values[index]);
                if (!preserveIndeterminate)
                {
                    this.SetValue(part.IndeterminateProperty, value: false);
                }
            }
        }
        catch (Exception ex)
        {
            this.LogException(ex);
            throw;
        }
        finally
        {
            this.isSyncingValues = false;
        }
    }

    /// <summary>
    ///     Returns the current numeric values of the shown components.
    /// </summary>
    /// <returns>One value per shown component, in X, Y, Z, W order.</returns>
    public double[] GetValues()
    {
        var values = new double[this.ComponentCount];
        for (var index = 0; index < values.Length; index++)
        {
            values[index] = (double)this.GetValue(this.parts[index].ValueProperty);
        }

        return values;
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.rootGrid = this.GetTemplateChild(RootGridPartName) as CustomGrid;
        this.backgroundBorder = this.GetTemplateChild(BackgroundBorderPartName) as Border;
        this.labelTextBlock = this.GetTemplateChild(LabelTextBlockPartName) as TextBlock;
        this.componentPanel = this.GetTemplateChild(ComponentPanelPartName) as Panel;
        this.stackedComponents = null;

        foreach (var part in this.parts)
        {
            this.Detach(part);
            part.Box = this.GetTemplateChild(NumberBoxPartPrefix + part.Name) as NumberBox;
            this.Attach(part);
        }

        this.UpdateLabelPosition();
        this.UpdateShownComponents();
        this.SyncAllComponentsFromProperties();
        this.UpdateVisualState();
        this.LogApplyTemplate();
    }

    /// <inheritdoc />
    /// <remarks>
    ///     Measured without a width limit, the box asks for one row of usable components, so a container probing its
    ///     natural width learns where the components would have to stack.
    /// </remarks>
    protected override Size MeasureOverride(Size availableSize)
    {
        var rowWidth = double.NaN;
        if (this.componentPanel is Grid grid && this.FindShownContainers() is { } containers)
        {
            rowWidth = this.SingleRowWidth(grid, containers);
            var stacked = this.AutoStackComponents && availableSize.Width < rowWidth;
            this.ArrangeComponents(grid, containers, stacked);
        }

        var size = base.MeasureOverride(availableSize);
        return double.IsPositiveInfinity(availableSize.Width) && size.Width < rowWidth ? new Size(rowWidth, size.Height) : size;
    }

    // The width that keeps every shown component usable on one row, including the label beside them and padding.
    private double SingleRowWidth(Grid grid, (ComponentPart part, Grid container)[] containers)
    {
        var count = containers.Length;
        var minimum = this.ComponentMinimumWidth * Math.Max(this.uiSettings.TextScaleFactor, this.FontSize / 14);
        foreach (var (part, container) in containers)
        {
            minimum = Math.Max(minimum, this.ComponentMinimumWidth * part.Box!.FontSize / 14);
            container.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            minimum = Math.Max(minimum, container.DesiredSize.Width);
        }

        var labelWidth = 0d;
        if (!string.IsNullOrEmpty(this.Label) && this.LabelPosition is LabelPosition.Left or LabelPosition.Right && this.labelTextBlock is not null)
        {
            this.labelTextBlock.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            labelWidth = this.labelTextBlock.DesiredSize.Width + (this.rootGrid?.ColumnSpacing ?? 0);
        }

        return this.Padding.Left + this.Padding.Right + labelWidth + (minimum * count) + (grid.ColumnSpacing * (count - 1));
    }

    private void ArrangeComponents(Grid grid, (ComponentPart part, Grid container)[] containers, bool stacked)
    {
        var count = containers.Length;
        if (stacked == this.stackedComponents && grid.ColumnDefinitions.Count == (stacked ? 1 : count) && grid.RowDefinitions.Count == (stacked ? count : 0))
        {
            return;
        }

        grid.ColumnDefinitions.Clear();
        grid.RowDefinitions.Clear();
        for (var i = 0; i < (stacked ? 1 : count); i++)
        {
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        }

        for (var i = 0; i < count; i++)
        {
            Grid.SetColumn(containers[i].container, stacked ? 0 : i);
            Grid.SetRow(containers[i].container, stacked ? i : 0);
            if (stacked)
            {
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            }
        }

        this.stackedComponents = stacked;
    }

    private (ComponentPart part, Grid container)[]? FindShownContainers()
    {
        var shown = this.ShownParts;
        var containers = new (ComponentPart, Grid)[shown.Length];
        for (var index = 0; index < shown.Length; index++)
        {
            var part = shown[index];
            if (part.Box?.Parent is not Grid container)
            {
                return null;
            }

            containers[index] = (part, container);
        }

        return containers;
    }

    private void Attach(ComponentPart part)
    {
        if (part.Box is not { } box)
        {
            return;
        }

        box.Validate += this.OnNumberBoxValidate;
        box.EditSessionStarted += this.OnNumberBoxEditSessionStarted;
        box.EditSessionCompleted += this.OnNumberBoxEditSessionCompleted;
        box.LoggerFactory = this.LoggerFactory;
        this.LogNumberBoxAttached(part.Name);
        this.ApplyComponentProperties(box, part.Component);
        this.BindToEditor(box, NumberBox.NumberValueProperty, part.Name + "Value");
        this.BindToEditor(box, NumberBox.IsIndeterminateProperty, part.Name + "IsIndeterminate");
    }

    private void Detach(ComponentPart part)
    {
        if (part.Box is not { } box)
        {
            return;
        }

        box.Validate -= this.OnNumberBoxValidate;
        box.EditSessionStarted -= this.OnNumberBoxEditSessionStarted;
        box.EditSessionCompleted -= this.OnNumberBoxEditSessionCompleted;
    }

    // Binds an editor property to this control's property of the given name, such as XValue or WIsIndeterminate.
    private void BindToEditor(NumberBox numberBox, DependencyProperty numberBoxProperty, string vectorBoxPropertyName)
        => Microsoft.UI.Xaml.Data.BindingOperations.SetBinding(numberBox, numberBoxProperty, new Microsoft.UI.Xaml.Data.Binding
        {
            Source = this,
            Path = new PropertyPath(vectorBoxPropertyName),
            Mode = Microsoft.UI.Xaml.Data.BindingMode.TwoWay,
        });

    private void ApplyComponentProperties(NumberBox numberBox, Component component)
    {
        numberBox.Mask = this.componentMasks.GetValueOrDefault(component, this.ComponentMask);
        numberBox.IndeterminateDisplayText = this.IndeterminateDisplayText;
        numberBox.WithPadding = this.WithPadding;
        numberBox.HorizontalValueAlignment = this.HorizontalValueAlignment;
        numberBox.HorizontalLabelAlignment = this.HorizontalLabelAlignment;
        numberBox.Multiplier = this.Multiplier;
        this.ApplyComponentRange(numberBox, component);
    }

    private void ApplyComponentRange(NumberBox numberBox, Component component)
        => (numberBox.Minimum, numberBox.Maximum) = this.componentRanges.GetValueOrDefault(component, (this.Minimum, this.Maximum));

    private void UpdateComponentRanges()
    {
        foreach (var part in this.parts)
        {
            if (part.Box is { } box)
            {
                this.ApplyComponentRange(box, part.Component);
            }
        }
    }

    private void ForEachEditor(Action<VectorBox, NumberBox> apply)
    {
        foreach (var part in this.parts)
        {
            if (part.Box is { } box)
            {
                apply(this, box);
            }
        }
    }

    private void SyncAllComponentsFromProperties()
    {
        if (this.isSyncingValues)
        {
            return;
        }

        this.LogSyncStart();
        try
        {
            this.isSyncingValues = true;
            foreach (var part in this.parts)
            {
                if (part.Box is { } box)
                {
                    box.NumberValue = (double)this.GetValue(part.ValueProperty);
                    box.IsIndeterminate = (bool)this.GetValue(part.IndeterminateProperty);
                }
            }
        }
        catch (Exception ex)
        {
            this.LogException(ex);
            throw;
        }
        finally
        {
            this.isSyncingValues = false;
            this.LogSyncEnd();
        }
    }

    private void OnNumberBoxValidate(object? sender, ValidationEventArgs<double> e)
    {
        e.Target = this.FindPart(sender)?.Component;
        this.OnValidate(e);
        this.LogValidationRelayed(e.Target?.ToString() ?? "__NULL__", e.OldValue, e.NewValue, e.IsValid);
    }

    private ComponentPart? FindPart(object? editor)
        => Array.Find(this.parts, part => part.Box is not null && ReferenceEquals(part.Box, editor));

    // Short-circuit to the correct state based on pointer hover.
    private void UpdateVisualState(bool useTransitions = true)
        => _ = VisualStateManager.GoToState(
            this,
            this.isPointerOver ? HoverStateName : NormalStateName,
            useTransitions);

    private void OnComponentValueChanged(Component component, double oldValue, double newValue)
    {
        var part = this.parts[(int)component];
        if (this.isSyncingValues || part.Box is not { } box)
        {
            return;
        }

        try
        {
            this.isSyncingValues = true;
            var args = new ValidationEventArgs<double>(oldValue, newValue) { Target = component };
            this.OnValidate(args);
            if (!args.IsValid)
            {
                this.SetValue(part.ValueProperty, oldValue);
                return;
            }

            box.NumberValue = newValue;
            this.SetValue(part.IndeterminateProperty, value: false);
            this.LogComponentChanged(part.Name, oldValue, newValue);
        }
        finally
        {
            this.isSyncingValues = false;
        }
    }

    private void OnComponentIndeterminateChanged(Component component)
    {
        var part = this.parts[(int)component];
        if (!this.isSyncingValues && part.Box is { } box)
        {
            box.IsIndeterminate = (bool)this.GetValue(part.IndeterminateProperty);
        }
    }

    private void OnComponentMaskChanged()
    {
        foreach (var part in this.parts)
        {
            if (part.Box is { } box)
            {
                box.Mask = this.componentMasks.GetValueOrDefault(part.Component, this.ComponentMask);
            }
        }
    }

    private void OnDimensionChanged() => this.UpdateShownComponents();

    // Shows the first Dimension components and lays them out again; hidden components keep their values.
    private void UpdateShownComponents()
    {
        if (this.componentPanel is not Grid)
        {
            return;
        }

        var count = this.ComponentCount;
        for (var index = 0; index < this.parts.Length; index++)
        {
            var part = this.parts[index];
            if (part.Box?.Parent is Grid container)
            {
                container.Visibility = index < count ? Visibility.Visible : Visibility.Collapsed;
            }
        }

        this.stackedComponents = null;
        this.UpdateComponentLabelPositions();
        this.InvalidateMeasure();
    }

    // Each component's NumberBox shows its label, so every component label scrubs, wherever it is placed.
    private void UpdateComponentLabelPositions()
    {
        foreach (var part in this.ShownParts)
        {
            this.UpdateComponentLabel(part);
        }
    }

    private void UpdateComponentLabel(ComponentPart part)
    {
        if (part.Box is not { } box)
        {
            return;
        }

        var componentLabel = this.componentLabels.GetValueOrDefault(part.Component, part.Name);
        var accessibleName = AutomationProperties.GetName(this);
        AutomationProperties.SetName(box, $"{(string.IsNullOrEmpty(accessibleName) ? this.Label : accessibleName)} {componentLabel}".Trim());
        var position = this.componentLabelPositions.GetValueOrDefault(part.Component, this.ComponentLabelPosition);
        box.Label = position == LabelPosition.None ? string.Empty : componentLabel;
        box.LabelForeground = this.componentLabelForegrounds.TryGetValue(part.Component, out var brush) ? brush : null;
        box.LabelPosition = position;
        box.IsCompact = this.CompactComponentLabels;
    }

    private void UpdateLabelPosition()
    {
        if (this.rootGrid == null || this.labelTextBlock == null || this.backgroundBorder == null)
        {
            return;
        }

        // Clear existing column and row definitions
        this.rootGrid.RowDefinitions.Clear();
        this.rootGrid.ColumnDefinitions.Clear();

        var effectivePosition = string.IsNullOrWhiteSpace(this.Label) ? LabelPosition.None : this.LabelPosition;
        switch (effectivePosition)
        {
            case LabelPosition.Left or LabelPosition.Right:
                this.labelTextBlock.Visibility = Visibility.Visible;
                LayoutHorizontally();
                break;
            case LabelPosition.Top or LabelPosition.Bottom:
                this.labelTextBlock.Visibility = Visibility.Visible;
                LayoutVertically();
                break;
            default:
                this.labelTextBlock.Visibility = Visibility.Collapsed;
                LayoutNoLabel();
                break;
        }

        void LayoutNoLabel()
        {
            this.rootGrid.ColumnDefinitions.Add(
                new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            Grid.SetColumn(this.labelTextBlock, 0);
            Grid.SetColumn(this.componentPanel, 0);
            Grid.SetRow(this.labelTextBlock, 0);
            Grid.SetRow(this.componentPanel, 0);
        }

        void LayoutHorizontally()
        {
            if (this.LabelPosition == LabelPosition.Left)
            {
                this.rootGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                this.rootGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                Grid.SetColumn(this.labelTextBlock, 0);
                Grid.SetColumn(this.componentPanel, 1);
            }
            else
            {
                this.rootGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                this.rootGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                Grid.SetColumn(this.labelTextBlock, 1);
                Grid.SetColumn(this.componentPanel, 0);
            }

            Grid.SetRow(this.labelTextBlock, 0);
            Grid.SetRow(this.componentPanel, 0);
        }

        void LayoutVertically()
        {
            this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            if (this.LabelPosition == LabelPosition.Top)
            {
                Grid.SetRow(this.labelTextBlock, 0);
                Grid.SetRow(this.componentPanel, 1);
            }
            else
            {
                Grid.SetRow(this.labelTextBlock, 1);
                Grid.SetRow(this.componentPanel, 0);
            }

            Grid.SetColumn(this.labelTextBlock, 0);
            Grid.SetColumn(this.componentPanel, 0);
        }
    }

    /// <summary>One vector component: its value and indeterminate properties, and its template editor and label.</summary>
    /// <param name="component">The component.</param>
    /// <param name="valueProperty">The component's value property.</param>
    /// <param name="indeterminateProperty">The component's indeterminate presentation property.</param>
    private sealed class ComponentPart(Component component, DependencyProperty valueProperty, DependencyProperty indeterminateProperty)
    {
        /// <summary>Gets the component.</summary>
        public Component Component { get; } = component;

        /// <summary>Gets the component name, used for template parts and as the default label.</summary>
        public string Name { get; } = component.ToString();

        /// <summary>Gets the component's value property.</summary>
        public DependencyProperty ValueProperty { get; } = valueProperty;

        /// <summary>Gets the component's indeterminate presentation property.</summary>
        public DependencyProperty IndeterminateProperty { get; } = indeterminateProperty;

        /// <summary>Gets or sets the template editor.</summary>
        public NumberBox? Box { get; set; }
    }
}
