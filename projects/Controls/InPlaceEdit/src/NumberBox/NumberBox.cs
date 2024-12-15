// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;

namespace DroidNet.Controls;

/// <summary>
/// Represents a control that allows the user to input and display numeric values.
/// </summary>
/// <remarks>
/// The <see cref="NumberBox"/> control provides a way for users to input numeric values with
/// optional formatting and validation. It supports various features such as custom masks, value
/// clamping, and label positioning. The numeric value can be changed by using the mouse scroll
/// wheel or dragging the mouse left or right.
/// <para>
/// Multiple scenarios can be implemented using the mask format. You can limit the number of digits
/// before or after the decimal point `.` with masks like `#.##` or `##.#`. You can also leave the
/// digits before the decimal point unbounded with `~`, such as in "~.##". You can also totally
/// suppress the fractional part (value will be rounded) with "~.". You can also limit the values to
/// negative or positive values by using the mask "-#.##" or "+#.##".
/// </para>
/// <para>
/// You can also replace any missing digits with '0' to have a fixed size formatting of the number
/// value. This behavior is enabled through the <see cref="WithPadding"/> property.
/// </para>
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <code lang="xaml"><![CDATA[
/// <controls:NumberBox
///     x:Name="numberBox"
///     Value="123.45"
///     Label="Amount"
///     LabelPosition="Top"
///     ShowLabel="True"
///     Multiplier="10"
///     Mask="~.##"
///     HorizontalValueAlignment="Center"
///     HorizontalLabelAlignment="Left"
///     Validate="NumberBox_Validate" />
/// ]]></code>
/// This example demonstrates how to use the <see cref="NumberBox"/> control in XAML with various
/// properties set. The control is configured to display a numeric value with a label positioned at
/// the top, and it uses a custom mask for formatting.
/// </example>
[TemplatePart(Name = RootGridPartName, Type = typeof(CustomGrid))]
[TemplatePart(Name = ValueTextBlockPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = LabelTextBlockPartName, Type = typeof(TextBlock))]
[TemplateVisualState(Name = NormalStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = HoverStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = PressedStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = ValidStateName, GroupName = ValidationStatesGroupName)]
[TemplateVisualState(Name = InvalidStateName, GroupName = ValidationStatesGroupName)]
public partial class NumberBox : Control
{
    private const string CommonStatesGroupName = "CommonStates";
    private const string NormalStateName = "Normal";
    private const string HoverStateName = "Hover";
    private const string PressedStateName = "Pressed";

    private const string ValidationStatesGroupName = "ValidationStates";
    private const string ValidStateName = "Valid";
    private const string InvalidStateName = "Invalid";

    private const string ValueTextBlockPartName = "PartValueTextBlock";
    private const string LabelTextBlockPartName = "PartLabelTextBlock";
    private const string RootGridPartName = "PartRootGrid";

    private readonly InputSystemCursor defaultCursor;
    private readonly InputSystemCursor dragCursor;
    private InputCursor? originalCursor;
    private MaskParser maskParser;

    private TextBlock? valueTextBlock;
    private TextBlock? labelTextBlock;
    private CustomGrid? rootGrid;
    private Point? capturePoint;
    private bool isPointerOver;
    private bool newValueIsValid = true;

    /// <summary>
    /// Initializes a new instance of the <see cref="NumberBox"/> class.
    /// </summary>
    public NumberBox()
    {
        this.DefaultStyleKey = typeof(NumberBox);
        this.maskParser = new MaskParser("~.#");

        this.defaultCursor = InputSystemCursor.Create(InputSystemCursorShape.Arrow);
        this.dragCursor = InputSystemCursor.Create(InputSystemCursorShape.SizeWestEast);

        this.Unloaded += (_, _) =>
        {
            this.defaultCursor.Dispose();
            this.dragCursor.Dispose();
        };
    }

    private bool IsMouseCaptured => this.capturePoint is not null;

    /// <inheritdoc/>
    protected override void OnApplyTemplate()
    {
        var oldValueTextBlock = this.valueTextBlock;

        if (oldValueTextBlock != null)
        {
            oldValueTextBlock.PointerPressed -= this.OnValueTextBlockPointerPressed;
            oldValueTextBlock.PointerMoved -= this.OnValueTextBlockPointerMoved;
            oldValueTextBlock.PointerReleased -= this.OnValueTextBlockPointerReleased;
            oldValueTextBlock.PointerWheelChanged -= this.OnValueTextBlockPointerWheelChanged;
            oldValueTextBlock.PointerEntered -= this.OnValueTextBlockPointerEntered;
            oldValueTextBlock.PointerExited -= this.OnValueTextBlockPointerExited;
        }

        base.OnApplyTemplate();

        this.rootGrid = this.GetTemplateChild(RootGridPartName) as CustomGrid;
        this.valueTextBlock = this.GetTemplateChild(ValueTextBlockPartName) as TextBlock;
        this.labelTextBlock = this.GetTemplateChild(LabelTextBlockPartName) as TextBlock;

        if (this.valueTextBlock != null)
        {
            this.valueTextBlock.PointerPressed += this.OnValueTextBlockPointerPressed;
            this.valueTextBlock.PointerMoved += this.OnValueTextBlockPointerMoved;
            this.valueTextBlock.PointerReleased += this.OnValueTextBlockPointerReleased;
            this.valueTextBlock.PointerWheelChanged += this.OnValueTextBlockPointerWheelChanged;
            this.valueTextBlock.PointerEntered += this.OnValueTextBlockPointerEntered;
            this.valueTextBlock.PointerExited += this.OnValueTextBlockPointerExited;
        }

        this.UpdateDisplayText();
        this.UpdateMinimumWidth();
        this.UpdateLabelPosition(); // After UpdateMinimumWidth()
        this.UpdateVisualState();
    }

    private void UpdateVisualState(bool useTransitions = true)
    {
        _ = VisualStateManager.GoToState(
            this,
            this.newValueIsValid ? ValidStateName : InvalidStateName,
            useTransitions);

        _ = VisualStateManager.GoToState(
            this,
            this.IsMouseCaptured ? PressedStateName : this.isPointerOver ? HoverStateName : NormalStateName,
            useTransitions);
    }

    private void OnValueTextBlockPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        this.isPointerOver = true;
        this.UpdateVisualState();
    }

    private void OnValueTextBlockPointerExited(object sender, PointerRoutedEventArgs e)
    {
        if (!this.IsMouseCaptured && this.originalCursor is not null && this.rootGrid is not null)
        {
            this.rootGrid.InputCursor = this.originalCursor;
        }

        this.isPointerOver = false;
        this.UpdateVisualState();
    }

    private void OnValueTextBlockPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (this.valueTextBlock?.CapturePointer(e.Pointer) != true)
        {
            return;
        }

        this.capturePoint = e.GetCurrentPoint(this.valueTextBlock).Position;
        this.UpdateInputCursor();
        this.UpdateVisualState();
        e.Handled = true;
    }

    private void OnValueTextBlockPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (this.capturePoint is null)
        {
            return;
        }

        var currentPoint = e.GetCurrentPoint(this.valueTextBlock).Position;
        var delta = currentPoint.X - this.capturePoint.Value.X;

        // Round delta up to the nearest integer
        delta = Math.Ceiling(delta);

        // Calculate the increment
        var increment = this.CalculateIncrement(delta, e.KeyModifiers);

        if (increment != 0 && this.TryUpdateValue(this.NumberValue + increment))
        {
            this.capturePoint = currentPoint;
        }

        e.Handled = true;
    }

    private void OnValueTextBlockPointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        var properties = e.GetCurrentPoint(this.valueTextBlock).Properties;
        if (properties.IsHorizontalMouseWheel)
        {
            return;
        }

        var delta = properties.MouseWheelDelta;

        // Calculate the increment
        var increment = this.CalculateIncrement(Math.Sign(delta) * 10, e.KeyModifiers);

        _ = this.TryUpdateValue(this.NumberValue + increment);
        e.Handled = true;
    }

    private float CalculateIncrement(double delta, Windows.System.VirtualKeyModifiers keyModifiers)
    {
        // Check if the Shift modifier is active
        var shiftMultiplier = 1.0;
        if (keyModifiers.HasFlag(Windows.System.VirtualKeyModifiers.Shift))
        {
            shiftMultiplier = Math.Abs(delta) < 10 ? 0.01 : 0.1;
        }

        return (float)(Math.Sign(delta) * this.Multiplier * shiftMultiplier);
    }

    private void OnValueTextBlockPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        if (this.valueTextBlock is null || !this.IsMouseCaptured)
        {
            return;
        }

        this.valueTextBlock.ReleasePointerCapture(e.Pointer);
        this.capturePoint = null;
        this.UpdateInputCursor();
        this.UpdateVisualState();
        e.Handled = true;
    }

    private void UpdateInputCursor()
    {
        if (this.rootGrid is null)
        {
            return;
        }

        if (this.IsMouseCaptured)
        {
            this.originalCursor ??= this.rootGrid.InputCursor ?? this.defaultCursor;
            this.rootGrid.InputCursor = this.dragCursor;
            return;
        }

        if (this.originalCursor is null)
        {
            return;
        }

        this.rootGrid.InputCursor = this.originalCursor;
        this.originalCursor = null;
    }

    private bool TryUpdateValue(float newValue)
    {
        if (!this.maskParser.IsValidValue(newValue))
        {
            return false;
        }

        var args = new ValidationEventArgs<float>(this.NumberValue, newValue);
        this.OnValidate(args);

        if (args.IsValid)
        {
            this.NumberValue = newValue;
        }

        return args.IsValid;
    }

    private void OnMaskChanged()
    {
        this.maskParser = new MaskParser(this.Mask);
        this.UpdateMinimumWidth();
        this.UpdateDisplayText();
    }

    private void OnValueChanged() => this.UpdateDisplayText();

    private void OnLabelPositionChanged() => this.UpdateLabelPosition();

    private void UpdateDisplayText() => this.DisplayText = this.maskParser.FormatValue(this.NumberValue);

    private void UpdateLabelPosition()
    {
        if (this.rootGrid == null || this.labelTextBlock == null || this.valueTextBlock == null)
        {
            return;
        }

        this.rootGrid.RowDefinitions.Clear();
        this.rootGrid.ColumnDefinitions.Clear();

        this.valueTextBlock.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        var valueWidth = Math.Max(this.valueTextBlock.MinWidth, this.valueTextBlock.DesiredSize.Width);

        if (this.LabelPosition is LabelPosition.Left or LabelPosition.Right)
        {
            LayoutHorizontally();
        }
        else if (this.LabelPosition is LabelPosition.Top or LabelPosition.Bottom)
        {
            LayoutVertically();
        }

        return;

        void LayoutHorizontally()
        {
            var labelColumn = new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) };
            var valueColumn = new ColumnDefinition
            {
                Width = new GridLength(1, GridUnitType.Star),
                MinWidth = valueWidth,
            };

            if (this.LabelPosition == LabelPosition.Left)
            {
                this.rootGrid.ColumnDefinitions.Add(labelColumn);
                this.rootGrid.ColumnDefinitions.Add(valueColumn);
                Grid.SetColumn(this.labelTextBlock, 0);
                Grid.SetColumn(this.valueTextBlock, 1);
            }
            else
            {
                this.rootGrid.ColumnDefinitions.Add(valueColumn);
                this.rootGrid.ColumnDefinitions.Add(labelColumn);
                Grid.SetColumn(this.labelTextBlock, 1);
                Grid.SetColumn(this.valueTextBlock, 0);
            }
        }

        void LayoutVertically()
        {
            this.rootGrid.ColumnDefinitions.Add(
                new ColumnDefinition
                {
                    Width = new GridLength(1, GridUnitType.Star),
                    MinWidth = valueWidth,
                });
            this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            if (this.LabelPosition == LabelPosition.Top)
            {
                Grid.SetRow(this.labelTextBlock, 0);
                Grid.SetRow(this.valueTextBlock, 1);
            }
            else
            {
                Grid.SetRow(this.labelTextBlock, 1);
                Grid.SetRow(this.valueTextBlock, 0);
            }

            Grid.SetColumn(this.labelTextBlock, 0);
            Grid.SetColumn(this.valueTextBlock, 0);
        }
    }

    private void UpdateMinimumWidth()
    {
        if (this.valueTextBlock == null)
        {
            return;
        }

        var sampleText = "-" + this.maskParser.FormatValue(0, withPadding: true);
        var measureTextBlock = new TextBlock
        {
            Text = sampleText,
            FontSize = this.valueTextBlock.FontSize,
            FontFamily = this.valueTextBlock.FontFamily,
            FontWeight = this.valueTextBlock.FontWeight,
            FontStyle = this.valueTextBlock.FontStyle,
        };

        measureTextBlock.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        this.valueTextBlock.MinWidth = measureTextBlock.DesiredSize.Width;
    }

    private void OnWithPaddingChanged() => this.UpdateDisplayText();
}
