// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display numeric values.
/// </summary>
/// <remarks>
///     The <see cref="NumberBox" /> control provides a way for users to input numeric values with
///     optional formatting and validation. It supports various features such as custom masks, value
///     clamping, and label positioning. The numeric value can be changed by using the mouse scroll
///     wheel or dragging the mouse left or right.
///     <para>
///         Multiple scenarios can be implemented using the mask format. You can limit the number of digits
///         before or after the decimal point `.` with masks like `#.##` or `##.#`. You can also leave the
///         digits before the decimal point unbounded with `~`, such as in "~.##". You can also totally
///         suppress the fractional part (value will be rounded) with "~." . You can also limit the values to
///         negative or positive values by using the mask "-#.##" or "+#.##".
///     </para>
///     <para>
///         You can also replace any missing digits with '0' to have a fixed size formatting of the number
///         value. This behavior is enabled through the <see cref="WithPadding" /> property.
///     </para>
/// </remarks>
/// <example>
///     <para>
///         <strong>Example Usage</strong>
///     </para>
///     <code lang="xaml"><![CDATA[
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
///     This example demonstrates how to use the <see cref="NumberBox" /> control in XAML with various
///     properties set. The control is configured to display a numeric value with a label positioned at
///     the top, and it uses a custom mask for formatting.
/// </example>
[TemplatePart(Name = RootGridPartName, Type = typeof(CustomGrid))]
[TemplatePart(Name = BackgroundBorderPartName, Type = typeof(Border))]
[TemplatePart(Name = ValueTextBlockPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = LabelTextBlockPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = EditBoxPartName, Type = typeof(TextBox))]
[TemplateVisualState(Name = NormalStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = HoverStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = PressedStateName, GroupName = CommonStatesGroupName)]
[TemplateVisualState(Name = ShowingValidValueStateName, GroupName = ValueStatesGroupName)]
[TemplateVisualState(Name = ShowingInvalidValueStateName, GroupName = ValueStatesGroupName)]
[TemplateVisualState(Name = EditingValidValueStateName, GroupName = ValueStatesGroupName)]
[TemplateVisualState(Name = EditingInvalidValueStateName, GroupName = ValueStatesGroupName)]
public partial class NumberBox : Control
{
    private const string EditBoxPartName = "PartEditBox";
    private const string ValueTextBlockPartName = "PartValueTextBlock";
    private const string LabelTextBlockPartName = "PartLabelTextBlock";
    private const string RootGridPartName = "PartRootGrid";
    private const string BackgroundBorderPartName = "PartBackgroundBorder";

    private const string CommonStatesGroupName = "CommonStates";
    private const string NormalStateName = "Normal";
    private const string HoverStateName = "Hover";
    private const string PressedStateName = "Pressed";

    private const string ValueStatesGroupName = "ValueStates";
    private const string ShowingValidValueStateName = "ShowingValidValue";
    private const string ShowingInvalidValueStateName = "ShowingInvalidValue";
    private const string EditingValidValueStateName = "EditingValidValue";
    private const string EditingInvalidValueStateName = "EditingInvalidValue";

    private readonly InputSystemCursor defaultCursor;
    private readonly InputSystemCursor dragCursor;
    private InputCursor? originalCursor;
    private MaskParser maskParser;

    private Border? backgroundBorder;
    private TextBox? editTextBox;
    private TextBlock? valueTextBlock;
    private TextBlock? labelTextBlock;
    private CustomGrid? rootGrid;
    private Point? capturePoint;
    private bool isPointerOver;
    private bool valueIsValid = true;
    private bool isEditing;
    private string? originalValue;

    /// <summary>
    ///     Initializes a new instance of the <see cref="NumberBox" /> class.
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

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.rootGrid = this.GetTemplateChild(RootGridPartName) as CustomGrid;
        this.backgroundBorder = this.GetTemplateChild(BackgroundBorderPartName) as Border;
        this.labelTextBlock = this.GetTemplateChild(LabelTextBlockPartName) as TextBlock;
        this.SetupValueTextBlockPart();
        this.SetupEditTextBoxPart();

        this.ValidateValue(this.NumberValue);
        this.UpdateDisplayText();
        this.UpdateMinimumWidth();
        this.UpdateLabelPosition(); // After UpdateMinimumWidth()
        this.UpdateVisualState();
    }

    private void SetupValueTextBlockPart()
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
            oldValueTextBlock.Tapped -= this.OnValueTextBlockTapped;
        }

        this.valueTextBlock = this.GetTemplateChild(ValueTextBlockPartName) as TextBlock;

        if (this.valueTextBlock == null)
        {
            return;
        }

        this.valueTextBlock.PointerPressed += this.OnValueTextBlockPointerPressed;
        this.valueTextBlock.PointerMoved += this.OnValueTextBlockPointerMoved;
        this.valueTextBlock.PointerReleased += this.OnValueTextBlockPointerReleased;
        this.valueTextBlock.PointerWheelChanged += this.OnValueTextBlockPointerWheelChanged;
        this.valueTextBlock.PointerEntered += this.OnValueTextBlockPointerEntered;
        this.valueTextBlock.PointerExited += this.OnValueTextBlockPointerExited;
        this.valueTextBlock.Tapped += this.OnValueTextBlockTapped;
    }

    private void SetupEditTextBoxPart()
    {
        var oldEditTextBox = this.editTextBox;

        if (oldEditTextBox != null)
        {
            oldEditTextBox.GotFocus -= this.OnEditBoxGotFocus;
            oldEditTextBox.KeyDown -= this.OnEditBoxKeyDown;
            oldEditTextBox.LostFocus -= this.OnEditBoxLostFocus;
            oldEditTextBox.TextChanged -= this.OnTextChanged;
        }

        this.editTextBox = this.GetTemplateChild(EditBoxPartName) as TextBox;

        if (this.editTextBox == null)
        {
            return;
        }

        this.editTextBox.GotFocus += this.OnEditBoxGotFocus;
        this.editTextBox.KeyDown += this.OnEditBoxKeyDown;
        this.editTextBox.LostFocus += this.OnEditBoxLostFocus;
        this.editTextBox.TextChanged += this.OnTextChanged;
    }

    private void UpdateVisualState(bool useTransitions = true)
    {
        if (this.isEditing)
        {
            _ = 1;
            _ = VisualStateManager.GoToState(
                this,
                this.valueIsValid ? EditingValidValueStateName : EditingInvalidValueStateName,
                useTransitions);
        }
        else
        {
            _ = VisualStateManager.GoToState(
                this,
                this.valueIsValid ? ShowingValidValueStateName : ShowingInvalidValueStateName,
                useTransitions);
        }

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
        var newValue = this.NumberValue + increment;

        this.ApplyNewValueIfValid(newValue);
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
        var increment = this.CalculateIncrement(Math.Sign(delta) * 10, e.KeyModifiers);
        var newValue = this.NumberValue + increment;

        this.ApplyNewValueIfValid(newValue);
        e.Handled = true;
    }

    private void ApplyNewValueIfValid(float newValue)
    {
        // If our value before change is not valid, we should apply the change to the value anyway.
        // But if our current value is valid, we only apply valid changes.
        var oldValueIsValid = this.valueIsValid;
        this.ValidateValue(newValue);
        if (oldValueIsValid && !this.valueIsValid)
        {
            // Cancel the change
            this.valueIsValid = true;
            return;
        }

        this.NumberValue = newValue;
    }

    private float CalculateIncrement(double delta, VirtualKeyModifiers keyModifiers)
    {
        // Check if the Shift modifier is active
        var shiftMultiplier = 1.0;
        if (keyModifiers.HasFlag(VirtualKeyModifiers.Shift))
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

    private void StartEdit()
    {
        if (this.valueTextBlock is null || this.editTextBox is null)
        {
            return;
        }

        this.originalValue = this.NumberValue.ToString(CultureInfo.CurrentCulture);
        this.isEditing = true;

        this.UpdateVisualState();

        this.editTextBox.MinWidth = this.valueTextBlock.ActualWidth;
        _ = this.editTextBox.Focus(FocusState.Programmatic);
    }

    private void CommitEdit()
    {
        Debug.Assert(this.valueIsValid, "commit only when newValueIsValid is true");
        this.NumberValue = float.Parse(this.editTextBox!.Text, CultureInfo.CurrentCulture);
        this.EndEdit();
    }

    private void CancelEdit()
    {
        this.editTextBox!.Text = this.originalValue;
        this.EndEdit();
    }

    private void OnTextChanged(object sender, TextChangedEventArgs textChangedEventArgs)
    {
        this.ValidateValue(this.editTextBox!.Text);
        this.UpdateVisualState(useTransitions: false);
    }

    private void OnEditBoxLostFocus(object sender, RoutedEventArgs routedEventArgs)
    {
        if (this.valueIsValid)
        {
            this.CommitEdit();
        }
        else
        {
            this.CancelEdit();
        }
    }

    private void OnEditBoxGotFocus(object sender, RoutedEventArgs routedEventArgs) => this.editTextBox?.SelectAll();

    private void EndEdit()
    {
        this.isEditing = false;
        this.UpdateVisualState();
    }

    private void ValidateValue(string value)
    {
        this.valueIsValid = float.TryParse(value, NumberStyles.Float, CultureInfo.CurrentCulture, out var parsedValue);
        if (!this.valueIsValid)
        {
            return;
        }

        this.ValidateValue(parsedValue);
    }

    private void ValidateValue(float value)
    {
        if (!this.maskParser.IsValidValue(value))
        {
            this.valueIsValid = false;
            return;
        }

        var args = new ValidationEventArgs<float>(this.NumberValue, value);
        this.OnValidate(args);
        Debug.Assert(this.valueIsValid == args.IsValid, "should be updated by the OnValidate method");
    }

    private void OnEditBoxKeyDown(object sender, KeyRoutedEventArgs e)
    {
        // ReSharper disable once SwitchStatementMissingSomeEnumCasesNoDefault
        switch (e.Key)
        {
            case VirtualKey.Enter when !this.valueIsValid:
                return;
            case VirtualKey.Enter:
                if (this.valueIsValid)
                {
                    this.CommitEdit();
                }

                e.Handled = true;
                break;

            case VirtualKey.Escape:
                this.CancelEdit();
                e.Handled = true;
                break;
        }
    }

    private void OnValueTextBlockTapped(object sender, TappedRoutedEventArgs e)
    {
        e.Handled = true;
        this.StartEdit();
    }

    private void OnMaskChanged()
    {
        this.maskParser = new MaskParser(this.Mask);
        this.UpdateMinimumWidth();
        this.UpdateDisplayText();
    }

    private void OnValueChanged()
    {
        this.ValidateValue(this.NumberValue);
        this.UpdateDisplayText();
        this.UpdateVisualState();
    }

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
                // No label
                this.labelTextBlock.Visibility = Visibility.Collapsed;
                break;
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

                // Grid.SetColumn(this.valueTextBlock, 1);
                // Grid.SetColumn(this.editTextBox, 1);
                Grid.SetColumn(this.backgroundBorder, 1);
            }
            else
            {
                this.rootGrid.ColumnDefinitions.Add(valueColumn);
                this.rootGrid.ColumnDefinitions.Add(labelColumn);
                Grid.SetColumn(this.labelTextBlock, 1);

                // Grid.SetColumn(this.valueTextBlock, 0);
                // Grid.SetColumn(this.editTextBox, 0);
                Grid.SetColumn(this.backgroundBorder, 0);
            }
        }

        void LayoutVertically()
        {
            this.rootGrid.ColumnDefinitions.Add(
                new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star), MinWidth = valueWidth });
            this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            if (this.LabelPosition == LabelPosition.Top)
            {
                Grid.SetRow(this.labelTextBlock, 0);

                // Grid.SetRow(this.valueTextBlock, 1);
                // Grid.SetRow(this.editTextBox, 1);
                Grid.SetRow(this.backgroundBorder, 1);
            }
            else
            {
                Grid.SetRow(this.labelTextBlock, 1);

                // Grid.SetRow(this.valueTextBlock, 0);
                // Grid.SetRow(this.editTextBox, 0);
                Grid.SetRow(this.backgroundBorder, 0);
            }

            Grid.SetColumn(this.labelTextBlock, 0);

            // Grid.SetColumn(this.valueTextBlock, 0);
            // Grid.SetColumn(this.editTextBox, 0);
            Grid.SetColumn(this.backgroundBorder, 0);
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
