// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display small fixed-size numeric vectors (Vector2/Vector3).
/// </summary>
/// <remarks>
///     The <see cref="VectorBox" /> control provides a clean, XAML-friendly editor for 2D or 3D vectors through coordinated
///     per-component <see cref="NumberBox" /> editors. It matches the behavior and conventions of <see cref="NumberBox" />
///     while managing multiple numeric components.
///     <para>
///     Each component (X, Y, Z) is presented through a writable dependency property (<see cref="XValue" />, <see cref="YValue" />,
///     <see cref="ZValue" />) and has an independent indeterminate presentation flag (<see cref="XIsIndeterminate" />,
///     <see cref="YIsIndeterminate" />, <see cref="ZIsIndeterminate" />) useful for multi-selection UI scenarios.
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
///         Dimension="3"
///         XValue="1.0"
///         YValue="2.0"
///         ZValue="3.0"
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
[TemplatePart(Name = NumberBoxXPartName, Type = typeof(NumberBox))]
[TemplatePart(Name = NumberBoxYPartName, Type = typeof(NumberBox))]
[TemplatePart(Name = NumberBoxZPartName, Type = typeof(NumberBox))]
[TemplateVisualState(Name = NormalStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = HoverStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = PressedStateName, GroupName = CommonStatesGroupName)]
public partial class VectorBox : Control
{
    private const string RootGridPartName = "PartRootGrid";
    private const string BackgroundBorderPartName = "PartBackgroundBorder";
    private const string LabelTextBlockPartName = "PartLabelTextBlock";
    private const string ComponentPanelPartName = "PartComponentPanel";
    private const string NumberBoxXPartName = "PartNumberBoxX";
    private const string NumberBoxYPartName = "PartNumberBoxY";
    private const string NumberBoxZPartName = "PartNumberBoxZ";

    private const string CommonStatesGroupName = "CommonStates";
    private const string NormalStateName = "Normal";
    private const string HoverStateName = "Hover";
    private const string PressedStateName = "Pressed";

    private const string DefaultComponentMask = "~.#";
    private const string DefaultIndeterminateDisplayText = "-.-";

    private const string LabelXPartName = "PartLabelX";
    private const string LabelYPartName = "PartLabelY";
    private const string LabelZPartName = "PartLabelZ";

    private CustomGrid? rootGrid;
    private Border? backgroundBorder;
    private TextBlock? labelTextBlock;
    private Panel? componentPanel;
    private TextBlock? labelX;
    private TextBlock? labelY;
    private TextBlock? labelZ;
    private NumberBox? numberBoxX;
    private NumberBox? numberBoxY;
    private NumberBox? numberBoxZ;
    private ILogger? logger;

    private bool isSyncingValues;
    private Dictionary<string, string>? componentMasks;
    private Dictionary<string, LabelPosition>? componentLabelPositions;

    /// <summary>
    ///     Initializes a new instance of the <see cref="VectorBox" /> class.
    /// </summary>
    public VectorBox()
    {
        this.DefaultStyleKey = typeof(VectorBox);
    }

    /// <summary>
    ///     Atomically updates all component values in a single operation.
    /// </summary>
    /// <param name="x">The X component value.</param>
    /// <param name="y">The Y component value.</param>
    /// <param name="z">The Z component value.</param>
    public void SetValues(float x, float y, float z) =>
        this.SetValues(x, y, z, preserveIndeterminate: false);

    /// <summary>
    ///     Atomically updates all component values in a single operation.
    /// </summary>
    /// <param name="x">The X component value.</param>
    /// <param name="y">The Y component value.</param>
    /// <param name="z">The Z component value.</param>
    /// <param name="preserveIndeterminate">Whether to preserve indeterminate flags.</param>
    public void SetValues(float x, float y, float z, bool preserveIndeterminate)
    {
        this.LogSetValues(x, y, z, preserveIndeterminate);
        try
        {
            this.isSyncingValues = true;

            this.XValue = x;
            this.YValue = y;
            this.ZValue = z;

            if (!preserveIndeterminate)
            {
                this.XIsIndeterminate = false;
                this.YIsIndeterminate = false;
                this.ZIsIndeterminate = false;
            }

            this.UpdateVectorValue();
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
    ///     Atomically updates X and Y component values (2D variant).
    /// </summary>
    /// <param name="x">The X component value.</param>
    /// <param name="y">The Y component value.</param>
    public void SetValues(float x, float y) =>
        this.SetValues(x, y, this.ZValue, preserveIndeterminate: false);

    /// <summary>
    ///     Returns the current numeric values as an array of floats.
    /// </summary>
    /// <returns>An array of length 2 or 3 depending on <see cref="Dimension" />.</returns>
    public float[] GetValues() =>
        this.Dimension == 2
            ? [this.XValue, this.YValue]
            : [this.XValue, this.YValue, this.ZValue];

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.rootGrid = this.GetTemplateChild(RootGridPartName) as CustomGrid;
        this.backgroundBorder = this.GetTemplateChild(BackgroundBorderPartName) as Border;
        this.labelTextBlock = this.GetTemplateChild(LabelTextBlockPartName) as TextBlock;
        this.componentPanel = this.GetTemplateChild(ComponentPanelPartName) as Panel;

        this.labelX = this.GetTemplateChild(LabelXPartName) as TextBlock;
        this.labelY = this.GetTemplateChild(LabelYPartName) as TextBlock;
        this.labelZ = this.GetTemplateChild(LabelZPartName) as TextBlock;

        this.SetupNumberBoxParts();

        this.UpdateLabelPosition();
        this.UpdateComponentLabelPositions();
        this.SyncAllComponentsFromProperties();
        this.UpdateVisualState();
    }

    private void SetupNumberBoxParts()
    {
        SetupX();
        SetupY();
        SetupZ();

        void SetupZ()
        {
            if (this.numberBoxZ != null)
            {
                this.numberBoxZ.Validate -= this.OnNumberBoxValidate;
            }

            this.numberBoxZ = this.GetTemplateChild(NumberBoxZPartName) as NumberBox;

            // Set up Z component (only if dimension is 3)
            if (this.numberBoxZ != null)
            {
                if (this.Dimension == 3)
                {
                    this.numberBoxZ.Validate += this.OnNumberBoxValidate;

                    // Propagate logger factory to child NumberBox
                    this.numberBoxZ.LoggerFactory = this.LoggerFactory;
                    this.LogNumberBoxAttached("Z");
                    this.ApplyComponentProperties(this.numberBoxZ, "Z");
                    this.SetupNumberBoxBinding(this.numberBoxZ, NumberBox.NumberValueProperty, nameof(this.ZValue));
                    this.SetupNumberBoxBinding(this.numberBoxZ, NumberBox.IsIndeterminateProperty, nameof(this.ZIsIndeterminate));
                    this.numberBoxZ.Visibility = Visibility.Visible;
                }
                else
                {
                    this.numberBoxZ.Visibility = Visibility.Collapsed;
                }
            }

            // Hide/show Z label based on dimension
            _ = this.labelZ?.Visibility = this.Dimension == 3 ? Visibility.Visible : Visibility.Collapsed;
        }

        void SetupX()
        {
            // Disconnect old event handlers
            if (this.numberBoxX != null)
            {
                this.numberBoxX.Validate -= this.OnNumberBoxValidate;
            }

            // Retrieve new NumberBox instances
            this.numberBoxX = this.GetTemplateChild(NumberBoxXPartName) as NumberBox;

            // Set up X component
            if (this.numberBoxX != null)
            {
                this.numberBoxX.Validate += this.OnNumberBoxValidate;

                // Propagate logger factory to child NumberBox
                this.numberBoxX.LoggerFactory = this.LoggerFactory;
                this.LogNumberBoxAttached("X");
                this.ApplyComponentProperties(this.numberBoxX, "X");
                this.SetupNumberBoxBinding(this.numberBoxX, NumberBox.NumberValueProperty, nameof(this.XValue));
                this.SetupNumberBoxBinding(this.numberBoxX, NumberBox.IsIndeterminateProperty, nameof(this.XIsIndeterminate));
            }
        }

        void SetupY()
        {
            // Disconnect old event handlers
            if (this.numberBoxY != null)
            {
                this.numberBoxY.Validate -= this.OnNumberBoxValidate;
            }

            // Retrieve new NumberBox instances
            this.numberBoxY = this.GetTemplateChild(NumberBoxYPartName) as NumberBox;

            // Set up Y component
            if (this.numberBoxY != null)
            {
                this.numberBoxY.Validate += this.OnNumberBoxValidate;

                // Propagate logger factory to child NumberBox
                this.numberBoxY.LoggerFactory = this.LoggerFactory;
                this.LogNumberBoxAttached("Y");
                this.ApplyComponentProperties(this.numberBoxY, "Y");
                this.SetupNumberBoxBinding(this.numberBoxY, NumberBox.NumberValueProperty, nameof(this.YValue));
                this.SetupNumberBoxBinding(this.numberBoxY, NumberBox.IsIndeterminateProperty, nameof(this.YIsIndeterminate));
            }
        }
    }

    private void SetupNumberBoxBinding(NumberBox numberBox, DependencyProperty numberBoxProperty, string vectorBoxPropertyName)
    {
        var binding = new Microsoft.UI.Xaml.Data.Binding
        {
            Source = this,
            Path = new PropertyPath(vectorBoxPropertyName),
            Mode = Microsoft.UI.Xaml.Data.BindingMode.TwoWay,
        };
        Microsoft.UI.Xaml.Data.BindingOperations.SetBinding(numberBox, numberBoxProperty, binding);
    }

    private void ApplyComponentProperties(NumberBox numberBox, string component)
    {
        // Apply mask
        var mask = this.componentMasks?.TryGetValue(component, out var componentMask) ?? false
            ? componentMask
            : this.ComponentMask;
        numberBox.Mask = mask;

        // Apply indeterminate display text
        numberBox.IndeterminateDisplayText = this.IndeterminateDisplayText;

        // NOTE: LabelPosition is always None for NumberBox controls in VectorBox.
        // The component labels (X, Y, Z) are managed by the VectorBox template, not by NumberBox.
        numberBox.LabelPosition = LabelPosition.None;

        // Apply other properties
        numberBox.WithPadding = this.WithPadding;
        numberBox.HorizontalValueAlignment = this.HorizontalValueAlignment;
        numberBox.HorizontalLabelAlignment = this.HorizontalLabelAlignment;
        numberBox.Multiplier = this.Multiplier;
    }

    private void SyncAllComponentsFromProperties()
    {
        if (this.isSyncingValues || this.numberBoxX == null || this.numberBoxY == null)
        {
            return;
        }

        this.LogSyncStart();
        try
        {
            this.isSyncingValues = true;

            // Sync numeric values
            this.numberBoxX.NumberValue = this.XValue;
            this.numberBoxY.NumberValue = this.YValue;

            if (this.Dimension == 3 && this.numberBoxZ != null)
            {
                this.numberBoxZ.NumberValue = this.ZValue;
            }

            // Sync indeterminate flags
            this.numberBoxX.IsIndeterminate = this.XIsIndeterminate;
            this.numberBoxY.IsIndeterminate = this.YIsIndeterminate;

            if (this.Dimension == 3 && this.numberBoxZ != null)
            {
                this.numberBoxZ.IsIndeterminate = this.ZIsIndeterminate;
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

    private void OnNumberBoxValidate(object? sender, ValidationEventArgs<float> e)
    {
        // Identify which component is being validated
        Component? target = null;
        if (ReferenceEquals(sender, this.numberBoxX))
        {
            target = Component.X;
        }
        else if (ReferenceEquals(sender, this.numberBoxY))
        {
            target = Component.Y;
        }
        else if (ReferenceEquals(sender, this.numberBoxZ))
        {
            target = Component.Z;
        }

        // Set the target for discrimination
        e.Target = target;

        // Relay the validation event to consumers
        this.OnValidate(e);

        // Log the relayed validation result
        var comp = target?.ToString() ?? "__NULL__";
        this.LogValidationRelayed(comp, e.OldValue, e.NewValue, e.IsValid);
    }

    // Short-circuit to the correct state based on pointer hover.
    private void UpdateVisualState(bool useTransitions = true)
        => _ = VisualStateManager.GoToState(
            this,
            this.isPointerOver ? HoverStateName : NormalStateName,
            useTransitions);

    private void UpdateVectorValue()
    {
        var currentVector = this.VectorValue;
        if (this.Dimension == 2)
        {
            currentVector.X = this.XValue;
            currentVector.Y = this.YValue;
        }
        else
        {
            currentVector.X = this.XValue;
            currentVector.Y = this.YValue;
            currentVector.Z = this.ZValue;
        }

        // Update vector value
        this.SetValue(VectorValueProperty, currentVector);
    }

    private void OnVectorValueChanged()
    {
        if (this.isSyncingValues)
        {
            return;
        }

        try
        {
            this.isSyncingValues = true;

            var vector = this.VectorValue;
            this.SetValue(XValueProperty, vector.X);
            this.SetValue(YValueProperty, vector.Y);

            if (this.Dimension == 3)
            {
                this.SetValue(ZValueProperty, vector.Z);
            }

            // Clear indeterminate flags by default
            this.SetValue(XIsIndeterminateProperty, value: false);
            this.SetValue(YIsIndeterminateProperty, value: false);

            if (this.Dimension == 3)
            {
                this.SetValue(ZIsIndeterminateProperty, value: false);
            }
        }
        finally
        {
            this.isSyncingValues = false;
        }
    }

    private void OnXValueChanged(float oldValue, float newValue)
    {
        if (this.isSyncingValues || this.numberBoxX == null)
        {
            return;
        }

        try
        {
            this.isSyncingValues = true;

            // Validate the new value
            var args = new ValidationEventArgs<float>(oldValue, newValue)
            {
                Target = Component.X,
            };
            this.OnValidate(args);
            if (!args.IsValid)
            {
                // Revert on validation failure
                this.SetValue(XValueProperty, oldValue);
                return;
            }

            // Update the internal editor
            this.numberBoxX.NumberValue = newValue;

            // Clear indeterminate flag
            this.SetValue(XIsIndeterminateProperty, value: false);

            // Update vector
            this.LogComponentChanged("X", oldValue, newValue);
            this.UpdateVectorValue();
        }
        finally
        {
            this.isSyncingValues = false;
        }
    }

    private void OnYValueChanged(float oldValue, float newValue)
    {
        if (this.isSyncingValues || this.numberBoxY == null)
        {
            return;
        }

        try
        {
            this.isSyncingValues = true;

            // Validate the new value
            var args = new ValidationEventArgs<float>(oldValue, newValue)
            {
                Target = Component.Y,
            };
            this.OnValidate(args);
            if (!args.IsValid)
            {
                // Revert on validation failure
                this.SetValue(YValueProperty, oldValue);
                return;
            }

            // Update the internal editor
            this.numberBoxY.NumberValue = newValue;

            // Clear indeterminate flag
            this.SetValue(YIsIndeterminateProperty, value: false);

            // Update vector
            this.LogComponentChanged("Y", oldValue, newValue);
            this.UpdateVectorValue();
        }
        finally
        {
            this.isSyncingValues = false;
        }
    }

    private void OnZValueChanged(float oldValue, float newValue)
    {
        if (this.isSyncingValues || this.numberBoxZ == null || this.Dimension != 3)
        {
            return;
        }

        try
        {
            this.isSyncingValues = true;

            // Validate the new value
            var args = new ValidationEventArgs<float>(oldValue, newValue)
            {
                Target = Component.Z,
            };
            this.OnValidate(args);
            if (!args.IsValid)
            {
                // Revert on validation failure
                this.SetValue(ZValueProperty, oldValue);
                return;
            }

            // Update the internal editor
            this.numberBoxZ.NumberValue = newValue;

            // Clear indeterminate flag
            this.SetValue(ZIsIndeterminateProperty, value: false);

            // Update vector
            this.LogComponentChanged("Z", oldValue, newValue);
            this.UpdateVectorValue();
        }
        finally
        {
            this.isSyncingValues = false;
        }
    }

    private void OnXIsIndeterminateChanged()
    {
        if (this.isSyncingValues || this.numberBoxX == null)
        {
            return;
        }

        this.numberBoxX.IsIndeterminate = this.XIsIndeterminate;
    }

    private void OnYIsIndeterminateChanged()
    {
        if (this.isSyncingValues || this.numberBoxY == null)
        {
            return;
        }

        this.numberBoxY.IsIndeterminate = this.YIsIndeterminate;
    }

    private void OnZIsIndeterminateChanged()
    {
        if (this.isSyncingValues || this.numberBoxZ == null || this.Dimension != 3)
        {
            return;
        }

        this.numberBoxZ.IsIndeterminate = this.ZIsIndeterminate;
    }

    private void OnComponentMaskChanged()
    {
        if (this.numberBoxX != null && this.componentMasks?.ContainsKey("X") == false)
        {
            this.numberBoxX.Mask = this.ComponentMask;
        }

        if (this.numberBoxY != null && this.componentMasks?.ContainsKey("Y") == false)
        {
            this.numberBoxY.Mask = this.ComponentMask;
        }

        if (this.numberBoxZ != null && this.Dimension == 3 && this.componentMasks?.ContainsKey("Z") == false)
        {
            this.numberBoxZ.Mask = this.ComponentMask;
        }
    }

    private void OnIndeterminateDisplayTextChanged()
    {
        _ = this.numberBoxX?.IndeterminateDisplayText = this.IndeterminateDisplayText;
        _ = this.numberBoxY?.IndeterminateDisplayText = this.IndeterminateDisplayText;

        if (this.numberBoxZ is not null && this.Dimension == 3)
        {
            this.numberBoxZ.IndeterminateDisplayText = this.IndeterminateDisplayText;
        }
    }

    private void OnComponentLabelPositionChanged() => this.UpdateComponentLabelPositions();

    private void UpdateComponentLabelPositions()
    {
        // Update the layout of the component grid (X/Y/Z labels and NumberBox editors) to reflect the new component label position
        if (this.componentPanel is not Grid grid)
        {
            return;
        }

        var dim = this.Dimension;
        var compCount = dim == 3 ? 3 : 2;

        // Configure each component container without touching the outer grid
        for (var i = 0; i < compCount; i++)
        {
            var label = GetComponentLabel(i);
            var box = GetComponentBox(i);
            var container = GetComponentContainer(i);
            if (label == null || box == null || container == null)
            {
                continue;
            }

            container.RowDefinitions.Clear();
            container.ColumnDefinitions.Clear();

            switch (this.ComponentLabelPosition)
            {
                case LabelPosition.Left:
                    LayoutComponentHorizontally(container, label, box, labelOnLeft: true);
                    break;
                case LabelPosition.Right:
                    LayoutComponentHorizontally(container, label, box, labelOnLeft: false);
                    break;
                case LabelPosition.Top:
                    LayoutComponentVertically(container, label, box, labelOnTop: true);
                    break;
                case LabelPosition.Bottom:
                    LayoutComponentVertically(container, label, box, labelOnTop: false);
                    break;
                default:
                    LayoutComponentNoLabel(container, label, box);
                    break;
            }
        }

        TextBlock GetComponentLabel(int idx)
            => idx switch
            {
                0 => this.labelX!,
                1 => this.labelY!,
                2 => this.labelZ!,
                _ => throw new ArgumentOutOfRangeException(nameof(idx)),
            };
        NumberBox GetComponentBox(int idx)
            => idx switch
            {
                0 => this.numberBoxX!,
                1 => this.numberBoxY!,
                2 => this.numberBoxZ!,
                _ => throw new ArgumentOutOfRangeException(nameof(idx)),
            };

        Grid? GetComponentContainer(int idx)
        {
            var lbl = GetComponentLabel(idx);
            if (lbl?.Parent is Grid g1)
            {
                return g1;
            }

            var bx = GetComponentBox(idx);
            if (bx?.Parent is Grid g2)
            {
                return g2;
            }

            // Fallback: search children of the outer panel for a Grid that contains either element
            foreach (var child in grid.Children)
            {
                if (child is Grid g)
                {
                    if (g.Children.Contains(lbl) || g.Children.Contains(bx))
                    {
                        return g;
                    }
                }
            }

            return null;
        }

        // Local layout helpers for a single per-component container
        void LayoutComponentNoLabel(Grid container, TextBlock label, NumberBox box)
        {
            label.Visibility = Visibility.Collapsed;
            container.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            Grid.SetRow(box, 0);
            Grid.SetColumn(box, 0);
        }

        void LayoutComponentHorizontally(Grid container, TextBlock label, NumberBox box, bool labelOnLeft)
        {
            label.Visibility = Visibility.Visible;
            container.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            if (labelOnLeft)
            {
                container.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                container.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                Grid.SetColumn(label, 0);
                Grid.SetColumn(box, 1);
            }
            else
            {
                container.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                container.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                Grid.SetColumn(box, 0);
                Grid.SetColumn(label, 1);
            }

            Grid.SetRow(label, 0);
            Grid.SetRow(box, 0);
        }

        void LayoutComponentVertically(Grid container, TextBlock label, NumberBox box, bool labelOnTop)
        {
            label.Visibility = Visibility.Visible;
            container.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            container.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            label.HorizontalTextAlignment = TextAlignment.Center;
            label.HorizontalAlignment = HorizontalAlignment.Stretch;

            if (labelOnTop)
            {
                Grid.SetRow(label, 0);
                Grid.SetRow(box, 1);
            }
            else
            {
                Grid.SetRow(box, 0);
                Grid.SetRow(label, 1);
            }

            Grid.SetColumn(box, 0);
            Grid.SetColumn(label, 0);
        }
    }

    private void OnLabelPositionChanged() => this.UpdateLabelPosition();

    private void UpdateLabelPosition()
    {
        if (this.rootGrid == null || this.labelTextBlock == null || this.backgroundBorder == null)
        {
            return;
        }

        // Clear existing column and row definitions
        this.rootGrid.RowDefinitions.Clear();
        this.rootGrid.ColumnDefinitions.Clear();

        switch (this.LabelPosition)
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

    private void OnMultiplierChanged()
    {
        _ = this.numberBoxX?.Multiplier = this.Multiplier;
        _ = this.numberBoxY?.Multiplier = this.Multiplier;

        if (this.Dimension == 3)
        {
            _ = this.numberBoxZ?.Multiplier = this.Multiplier;
        }
    }

    private void OnWithPaddingChanged()
    {
        _ = this.numberBoxX?.WithPadding = this.WithPadding;
        _ = this.numberBoxY?.WithPadding = this.WithPadding;
        _ = this.numberBoxZ?.WithPadding = this.WithPadding;
    }

    private void OnDimensionChanged()
    {
        // Update Z component visibility based on dimension
        _ = this.numberBoxZ?.Visibility = this.Dimension == 3 ? Visibility.Visible : Visibility.Collapsed;
        _ = this.labelZ?.Visibility = this.Dimension == 3 ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnHorizontalValueAlignmentChanged()
    {
        // Propagate the change to the internal NumberBox controls
        _ = this.numberBoxX?.HorizontalValueAlignment = this.HorizontalValueAlignment;
        _ = this.numberBoxY?.HorizontalValueAlignment = this.HorizontalValueAlignment;
        _ = this.numberBoxZ?.HorizontalValueAlignment = this.HorizontalValueAlignment;
    }
}
