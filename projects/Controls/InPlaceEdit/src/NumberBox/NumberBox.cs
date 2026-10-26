// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display numeric values.
/// </summary>
/// <remarks>
///     The <see cref="NumberBox" /> control provides numeric input with optional formatting and
///     validation (masking, label positioning, and label-driven pointer scrubbing).
///     <para>
///     Important note about "indeterminate" state and ViewModel patterns:
///     - When <see cref="IsIndeterminate" /> is true the control displays <see cref="IndeterminateDisplayText" />
///       (default "-.-") instead of the formatted <see cref="NumberValue" />. This presentation flag is
///       separate from the numeric backing value: the control keeps a numeric <see cref="NumberValue" /> at
///       all times and does not clear or null it when switching to indeterminate.
///     - User edits (typing and pointer drag on a visible label) clear <see cref="IsIndeterminate" /> and operate on the
///       existing <see cref="NumberValue" />. Likewise, externally setting <see cref="NumberValue" /> does
///       not implicitly toggle <see cref="IsIndeterminate" />. The two properties are independent and must be
///       coordinated by the host (ViewModel or code-behind) when a nullable or optional representation is
///       required.
///     </para>
///     <para>
///     A suggested model for handling the <see cref="IsIndeterminate" /> is to provide a <see cref="Nullable"/> value
///     from the <c>ViewModel</c> (see the demo app for an example): expose a nullable ViewModel property that maps to
///     the control's two pieces of state. For example, the demo ViewModel implements:
///     - A numeric property `NumberValue` (double) and a boolean `DemoIsIndeterminate` (bound to
///       <see cref="IsIndeterminate" />).
///     - A derived nullable property `DemoNullableValue` that returns `null` when `DemoIsIndeterminate` is
///       true, otherwise returns the numeric `NumberValue`. Setting `DemoNullableValue` to a non-null value
///       sets `NumberValue` and clears the indeterminate flag; setting it to null sets the indeterminate
///       flag but intentionally preserves the last numeric value. This preserves the numeric backing value
///       while allowing the UI to show an indeterminate presentation.
///     </para>
///     <para>
///     In short: the control presents an indeterminate state for UI purposes but keeps a numeric value
///     internally; if you need a nullable model, map the two states in your ViewModel (the demo shows a
///     simple, practical pattern to do this safely).
///     </para>
/// </remarks>
/// <example>
///     <para>
///         <strong>Example Usage</strong>
///     </para>
///     <code lang="xaml"><![CDATA[
///     <controls:NumberBox
///         x:Name="numberBox"
///         Value="123.45"
///         Label="Amount"
///         LabelPosition="Top"
///         ShowLabel="True"
///         Multiplier="10"
///         Mask="~.##"
///         HorizontalValueAlignment="Center"
///         HorizontalLabelAlignment="Left"
///         Validate="NumberBox_Validate" />
///     ]]></code>
///     This example demonstrates how to use the <see cref="NumberBox" /> control in XAML with various
///     properties set. The control is configured to display a numeric value with a label positioned at
///     the top, and it uses a custom mask for formatting.
/// </example>
[TemplatePart(Name = RootGridPartName, Type = typeof(CustomGrid))]
[TemplatePart(Name = BackgroundBorderPartName, Type = typeof(Border))]
[TemplatePart(Name = ValueTextBlockPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = LabelTextBlockPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = CompactLabelTextBlockPartName, Type = typeof(TextBlock))]
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
    private const string CompactLabelTextBlockPartName = "PartCompactLabelTextBlock";
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

    private const string DefaultIndeterminateDisplayText = "-.-";

    private const int MaxRoundingDecimals = 15;

    private readonly ScrubGesture scrub = new();

    private InputSystemCursor? defaultCursor;
    private InputSystemCursor? dragCursor;

    private InputCursor? originalCursor;
    private MaskParser maskParser;

    private Border? backgroundBorder;
    private TextBox? editTextBox;
    private TextBlock? valueTextBlock;
    private TextBlock? labelTextBlock;
    private TextBlock? compactLabelTextBlock;
    private CustomGrid? rootGrid;
    private UIElement? rootInputEventSource;
    private PointerEventHandler? rootPointerPressedHandler;
    private UIElement? scrubHandle;
    private bool isPointerOver;
    private bool valueIsValid = true;
    private bool isEditing;
    private string? originalValue;

    private ILogger? logger;

    /// <summary>
    ///     Initializes a new instance of the <see cref="NumberBox" /> class.
    /// </summary>
    public NumberBox()
    {
        this.DefaultStyleKey = typeof(NumberBox);
        this.maskParser = new MaskParser("~.#");
        TextScale.InvalidateMeasureOnChange(this);

        this.defaultCursor = InputSystemCursor.Create(InputSystemCursorShape.Arrow);
        this.dragCursor = InputSystemCursor.Create(InputSystemCursorShape.SizeWestEast);

        this.Loaded += (_, _) =>
        {
            this.defaultCursor ??= InputSystemCursor.Create(InputSystemCursorShape.Arrow);
            this.dragCursor ??= InputSystemCursor.Create(InputSystemCursorShape.SizeWestEast);
            this.AttachRootPointerPressedHandler();
        };
        this.Unloaded += (_, _) =>
        {
            this.DetachRootPointerPressedHandler();
            this.CompleteActiveEditSessionOnUnload();
            this.rootGrid?.InputCursor = null;

            this.defaultCursor?.Dispose();
            this.dragCursor?.Dispose();
            this.defaultCursor = null;
            this.dragCursor = null;
            this.originalCursor = null;
        };
    }

    private bool IsMouseCaptured => this.scrub.IsScrubbing;

    private (double Minimum, double Maximum) Range
        => (double.IsNaN(this.Minimum) ? double.NegativeInfinity : this.Minimum, double.IsNaN(this.Maximum) ? double.PositiveInfinity : this.Maximum);

    /// <summary>Completes pending text input before an owning document saves or closes.</summary>
    /// <remarks>Valid text is committed; invalid text is cancelled using the control's normal validation behavior.</remarks>
    public void CompletePendingTextEdit()
    {
        if (this.isEditing)
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
    }

    /// <summary>Starts editing the value as text, as tapping the value does.</summary>
    /// <remarks>
    ///     The value leaves its indeterminate state, the edit box takes focus with the current value, and
    ///     <see cref="EditSessionStarted"/> reports a <see cref="NumberBoxEditInteractionKind.Text"/> session. Complete
    ///     the edit with <see cref="CompletePendingTextEdit"/> or abandon it with <see cref="CancelEdit"/>.
    /// </remarks>
    public void BeginTextEdit()
    {
        if (this.valueTextBlock is null || this.editTextBox is null)
        {
            return;
        }

        this.LogEditStarted(this.IsIndeterminate);

        // Leaving indeterminate for editing: do not reset NumberValue, only switch presentation
        this.IsIndeterminate = false;

        this.originalValue = EditText(this.NumberValue);
        this.isEditing = true;
        this.OnEditSessionStarted(NumberBoxEditInteractionKind.Text);

        this.UpdateVisualState();

        this.editTextBox.Text = this.originalValue ?? string.Empty;
        _ = this.editTextBox.Focus(FocusState.Programmatic);
    }

    /// <summary>Abandons the text edit, as pressing Escape does: the value is restored and the edit box closes.</summary>
    /// <remarks>
    ///     <see cref="EditSessionCompleted"/> reports the session as cancelled. Does nothing when no text edit is in
    ///     progress.
    /// </remarks>
    public void CancelEdit()
    {
        if (!this.isEditing)
        {
            return;
        }

        var wasIndeterminate = this.IsIndeterminate;
        if (this.IsIndeterminate)
        {
            this.editTextBox!.Text = string.Empty;
        }
        else
        {
            this.editTextBox!.Text = this.originalValue;
        }

        this.EndEdit();
        this.OnEditSessionCompleted(NumberBoxEditInteractionKind.Text, NumberBoxEditCompletionKind.Cancel);

        this.LogEditCanceled(wasIndeterminate);
    }

    /// <summary>Gets the value a drag reaches, kept to the decimals of the step and of the value as shown: 12.2 + 0.1
    /// is 12.3, not 12.299999999999999, and 12.234 + 0.1 is 12.334.</summary>
    /// <param name="value">The value the drag steps from.</param>
    /// <param name="steps">The number of steps, negative for a drag down.</param>
    /// <param name="step">The change one step makes.</param>
    /// <returns>The value <paramref name="steps"/> steps away from <paramref name="value"/>.</returns>
    internal static double Stepped(double value, int steps, double step)
    {
        var decimals = Math.Max(Decimals(value), StepDecimals(step));
        return Math.Round(value + (steps * step), decimals, MidpointRounding.AwayFromZero);
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.rootGrid = this.GetTemplateChild(RootGridPartName) as CustomGrid;
        this.backgroundBorder = this.GetTemplateChild(BackgroundBorderPartName) as Border;
        this.valueGroup = this.GetTemplateChild("PartValueGroup") as FrameworkElement ?? this.backgroundBorder;
        this.prefixTextBlock = this.GetTemplateChild("PartValuePrefix") as TextBlock;
        this.unitTextBlock = this.GetTemplateChild("PartValueUnit") as TextBlock;
        this.labelTextBlock = this.GetTemplateChild(LabelTextBlockPartName) as TextBlock;
        this.compactLabelTextBlock = this.GetTemplateChild(CompactLabelTextBlockPartName) as TextBlock;
        this.SetupValueTextBlockPart();
        this.SetupLabelTextBlockPart();
        this.SetupEditTextBoxPart();

        this.ValidateValue(this.NumberValue);
        this.UpdateDisplayText();
        this.UpdateMinimumWidth();
        this.OnLabelForegroundChanged();
        this.UpdateLabelPosition(); // After UpdateMinimumWidth()
        this.UpdateVisualState();
    }

    /// <inheritdoc />
    protected override void OnKeyDown(KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.Escape && this.IsMouseCaptured)
        {
            this.CancelScrub();
            e.Handled = true;
            return;
        }

        base.OnKeyDown(e);
    }

    // A float widened to double shows the shortest text that reads back as that float (0.3f, not
    // 0.30000001192092896); any other double shows its own shortest text.
    private static string ShortestText(double value, IFormatProvider format)
        => double.IsFinite(value) && (double)(float)value == value
            ? ((float)value).ToString(format)
            : value.ToString(format);

    private static string EditText(double value) => ShortestText(value, CultureInfo.CurrentCulture);

    // The decimal places of a value's shortest text, at most the 15 that Math.Round takes.
    private static int Decimals(double value)
    {
        var text = ShortestText(value, CultureInfo.InvariantCulture);
        if (text.Contains('E', StringComparison.Ordinal))
        {
            return MaxRoundingDecimals;
        }

        var point = text.IndexOf('.', StringComparison.Ordinal);
        return point < 0 ? 0 : Math.Min(MaxRoundingDecimals, text.Length - point - 1);
    }

    // A step's decimal places, read through decimal's 15 significant digits so that the Shift step 0.1 × 0.1 counts
    // as 0.01, not as 0.010000000000000002.
    private static int StepDecimals(double step)
    {
        if (!double.IsFinite(step) || Math.Abs(step) >= (double)decimal.MaxValue)
        {
            return 0;
        }

        // Dividing by 1 with 33 zero decimals drops the trailing zeros the conversion leaves.
        var normalized = (decimal)step / 1.000000000000000000000000000000000m;
        return Math.Min(MaxRoundingDecimals, (decimal.GetBits(normalized)[3] >> 16) & 0xFF);
    }

    private void SetupValueTextBlockPart()
    {
        if (this.valueTextBlock is { } oldValueTextBlock)
        {
            oldValueTextBlock.PointerEntered -= this.OnValueTextBlockPointerEntered;
            oldValueTextBlock.PointerExited -= this.OnValueTextBlockPointerExited;
            oldValueTextBlock.Tapped -= this.OnValueTextBlockTapped;
            this.UnhookScrubHandle(oldValueTextBlock);
        }

        this.valueTextBlock = this.GetTemplateChild(ValueTextBlockPartName) as TextBlock;

        if (this.valueTextBlock == null)
        {
            return;
        }

        this.valueTextBlock.PointerEntered += this.OnValueTextBlockPointerEntered;
        this.valueTextBlock.PointerExited += this.OnValueTextBlockPointerExited;
        this.valueTextBlock.Tapped += this.OnValueTextBlockTapped;
        this.HookScrubHandle(this.valueTextBlock);
    }

    private void SetupLabelTextBlockPart()
    {
        if (this.labelTextBlock is { } oldLabelTextBlock)
        {
            this.UnhookScrubHandle(oldLabelTextBlock);
            oldLabelTextBlock.PointerEntered -= this.OnValueTextBlockPointerEntered;
            oldLabelTextBlock.PointerExited -= this.OnValueTextBlockPointerExited;
        }

        if (this.compactLabelTextBlock is { } oldCompactLabelTextBlock)
        {
            this.UnhookScrubHandle(oldCompactLabelTextBlock);
            oldCompactLabelTextBlock.PointerEntered -= this.OnValueTextBlockPointerEntered;
            oldCompactLabelTextBlock.PointerExited -= this.OnValueTextBlockPointerExited;
        }

        Setup(this.labelTextBlock);
        Setup(this.compactLabelTextBlock);

        void Setup(TextBlock? labelPart)
        {
            if (labelPart is null)
            {
                return;
            }

            this.HookScrubHandle(labelPart);
            labelPart.PointerEntered += this.OnValueTextBlockPointerEntered;
            labelPart.PointerExited += this.OnValueTextBlockPointerExited;
        }
    }

    private void HookScrubHandle(UIElement handle)
    {
        handle.PointerPressed += this.OnScrubPointerPressed;
        handle.PointerMoved += this.OnScrubPointerMoved;
        handle.PointerReleased += this.OnScrubPointerReleased;
        handle.PointerCaptureLost += this.OnScrubPointerCaptureLost;
    }

    private void UnhookScrubHandle(UIElement handle)
    {
        handle.PointerPressed -= this.OnScrubPointerPressed;
        handle.PointerMoved -= this.OnScrubPointerMoved;
        handle.PointerReleased -= this.OnScrubPointerReleased;
        handle.PointerCaptureLost -= this.OnScrubPointerCaptureLost;
    }

    private void CompleteActiveEditSessionOnUnload()
    {
        if (this.IsMouseCaptured)
        {
            this.CancelScrub();
        }

        if (this.isEditing)
        {
            this.isEditing = false;
            this.OnEditSessionCompleted(NumberBoxEditInteractionKind.Text, NumberBoxEditCompletionKind.Cancel);
        }
    }

    private void AttachRootPointerPressedHandler()
    {
        if (this.XamlRoot?.Content is not UIElement rootContent)
        {
            return;
        }

        if (ReferenceEquals(this.rootInputEventSource, rootContent))
        {
            return;
        }

        this.DetachRootPointerPressedHandler();
        this.rootPointerPressedHandler ??= this.OnRootPointerPressed;
        rootContent.AddHandler(UIElement.PointerPressedEvent, this.rootPointerPressedHandler, handledEventsToo: true);
        this.rootInputEventSource = rootContent;
    }

    private void DetachRootPointerPressedHandler()
    {
        if (this.rootInputEventSource is null || this.rootPointerPressedHandler is null)
        {
            return;
        }

        this.rootInputEventSource.RemoveHandler(UIElement.PointerPressedEvent, this.rootPointerPressedHandler);
        this.rootInputEventSource = null;
    }

    private void OnRootPointerPressed(object sender, PointerRoutedEventArgs e)
        => this.CompleteTextEditIfPointerPressedOutside(e.OriginalSource);

    private void CompleteTextEditIfPointerPressedOutside(object? originalSource)
    {
        if (this.isEditing && !IsDescendantOf(originalSource as DependencyObject, this))
        {
            this.CompletePendingTextEdit();
        }

        static bool IsDescendantOf(DependencyObject? source, DependencyObject ancestor)
        {
            for (var current = source; current is not null; current = VisualTreeHelper.GetParent(current))
            {
                if (ReferenceEquals(current, ancestor))
                {
                    return true;
                }
            }

            return false;
        }
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
        this.LogPointerEvent("Entered");
        this.isPointerOver = true;
        this.UpdateVisualState();
    }

    private void OnValueTextBlockPointerExited(object sender, PointerRoutedEventArgs e)
    {
        this.LogPointerEvent("Exited");
        if (!this.IsMouseCaptured && this.originalCursor is not null && this.rootGrid is not null)
        {
            this.rootGrid.InputCursor = this.originalCursor;
        }

        this.isPointerOver = false;
        this.UpdateVisualState();
    }

    // Labels scrub from the press. The value region waits for horizontal travel, so a plain click still edits text.
    private void OnScrubPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (sender is not UIElement { IsHitTestVisible: true } handle)
        {
            return;
        }

        var immediate = !ReferenceEquals(handle, this.valueTextBlock);
        if (immediate)
        {
            this.CompletePendingTextEdit();
        }

        this.scrubHandle = handle;
        this.scrub.Press(e.GetCurrentPoint(handle).Position.X, immediate);
        this.LogPointerEvent("Pressed");
        if (immediate && this.BeginScrub(e.Pointer))
        {
            e.Handled = true;
        }
    }

    private void OnScrubPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        this.LogPointerEvent("Moved");
        if (this.scrubHandle is not { } handle || !ReferenceEquals(sender, handle))
        {
            return;
        }

        var move = this.scrub.Move(e.GetCurrentPoint(handle).Position.X, e.Pointer.IsInContact);
        if ((move.Started && !this.BeginScrub(e.Pointer)) || !this.scrub.IsScrubbing)
        {
            return;
        }

        if (move.Steps != 0)
        {
            this.ApplyScrubSteps(move.Steps, e.KeyModifiers);
        }

        e.Handled = true;
    }

    private void OnScrubPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        this.LogPointerEvent("Released");
        if (this.scrubHandle is not { } handle || !ReferenceEquals(sender, handle))
        {
            return;
        }

        this.scrubHandle = null;
        if (!this.scrub.Release())
        {
            return;
        }

        handle.ReleasePointerCapture(e.Pointer);
        this.EndScrub(NumberBoxEditCompletionKind.Commit);
        e.Handled = true;
    }

    private void OnScrubPointerCaptureLost(object sender, PointerRoutedEventArgs e)
    {
        this.LogPointerEvent("CaptureLost");
        if (!this.IsMouseCaptured || !ReferenceEquals(sender, this.scrubHandle))
        {
            return;
        }

        this.scrub.Cancel();
        this.scrubHandle = null;
        this.EndScrub(NumberBoxEditCompletionKind.Cancel);
    }

    private bool BeginScrub(Pointer pointer)
    {
        if (this.scrubHandle is not { } handle || !handle.CapturePointer(pointer))
        {
            this.scrub.Cancel();
            this.scrubHandle = null;
            return false;
        }

        this.CompletePendingTextEdit();
        _ = this.Focus(FocusState.Pointer);
        this.OnEditSessionStarted(NumberBoxEditInteractionKind.PointerDrag);
        this.UpdateInputCursor();
        this.UpdateVisualState();
        return true;
    }

    private void CancelScrub()
    {
        this.scrub.Cancel();
        var handle = this.scrubHandle;
        this.scrubHandle = null;
        handle?.ReleasePointerCaptures();
        this.EndScrub(NumberBoxEditCompletionKind.Cancel);
    }

    private void EndScrub(NumberBoxEditCompletionKind completion)
    {
        this.OnEditSessionCompleted(NumberBoxEditInteractionKind.PointerDrag, completion);
        this.UpdateInputCursor();
        this.UpdateVisualState();
    }

    // A scrub on an indeterminate value shows the stored value and steps from it.
    private void ApplyScrubSteps(int steps, VirtualKeyModifiers keyModifiers)
    {
        if (this.IsIndeterminate)
        {
            this.IsIndeterminate = false;
        }

        var step = keyModifiers.HasFlag(VirtualKeyModifiers.Shift) ? this.Multiplier * 0.1 : this.Multiplier;
        this.ApplyNewValueIfValid(this.ClampToRange(Stepped(this.NumberValue, steps, step)));
    }

    private void ApplyNewValueIfValid(double newValue)
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

    // A drag stops at a bound rather than short of it. A NaN or inverted bound leaves the value as it is.
    private double ClampToRange(double value)
    {
        var (minimum, maximum) = this.Range;
        return minimum <= maximum ? Math.Clamp(value, minimum, maximum) : value;
    }

    // Typed values outside the bounds are invalid, like values the mask cannot show.
    private bool IsInRange(double value)
    {
        var (minimum, maximum) = this.Range;
        return minimum > maximum || (value >= minimum && value <= maximum);
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
            this.LogCursorChanged("Drag");
            return;
        }

        if (this.originalCursor is null)
        {
            return;
        }

        this.rootGrid.InputCursor = this.originalCursor;
        this.originalCursor = null;
        this.LogCursorChanged("Default");
    }

    private void CommitEdit()
    {
        Debug.Assert(this.valueIsValid, "commit only when newValueIsValid is true");

        var text = this.editTextBox!.Text?.Trim();
        if (string.IsNullOrEmpty(text))
        {
            this.LogEditCommitted(this.NumberValue);

            // user left the field empty -> treat as indeterminate
            // Treat committing to indeterminate as a commit action (no numeric value)
            this.IsIndeterminate = true;
        }
        else
        {
            try
            {
                if (string.Equals(text, this.originalValue, StringComparison.Ordinal))
                {
                    // Unchanged text keeps the value it shows: a widened float's shortest text read back as a double
                    // would be another value.
                    this.IsIndeterminate = false;
                    this.LogEditCommitted(this.NumberValue);
                    this.OnEditSessionCompleted(NumberBoxEditInteractionKind.Text, NumberBoxEditCompletionKind.Commit, text);
                    this.EndEdit();
                    return;
                }

                if (!NumericInputParser.TryParse(text, this.NumberValue, out var expression))
                {
                    this.CancelEdit();
                    return;
                }

                this.NumberValue = expression.Apply(this.NumberValue);
                this.IsIndeterminate = false;
                this.LogEditCommitted(this.NumberValue);
            }
            catch (Exception ex)
            {
                this.LogCommitFailed(ex);
                throw;
            }
        }

        this.OnEditSessionCompleted(NumberBoxEditInteractionKind.Text, NumberBoxEditCompletionKind.Commit, text);
        this.EndEdit();
    }

    private void OnTextChanged(object sender, TextChangedEventArgs textChangedEventArgs)
    {
        this.ValidateValue(this.editTextBox!.Text);
        this.UpdateVisualState(useTransitions: false);
    }

    private void OnEditBoxLostFocus(object sender, RoutedEventArgs routedEventArgs)
    {
        if (!this.isEditing)
        {
            return;
        }

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
        var oldIsValid = this.valueIsValid;
        this.valueIsValid = NumericInputParser.TryParse(value, this.NumberValue, out var expression);
        if (!this.valueIsValid)
        {
            this.LogValidation(oldIsValid, this.valueIsValid);

            return;
        }

        this.ValidateValue(expression.Apply(this.NumberValue), value);
    }

    private void ValidateValue(double value, string? inputText = null)
    {
        var oldIsValid = this.valueIsValid;
        if (!this.maskParser.IsValidValue(value) || !this.IsInRange(value))
        {
            this.valueIsValid = false;
            this.LogValidation(oldIsValid, this.valueIsValid);
            return;
        }

        var args = new ValidationEventArgs<double>(this.NumberValue, value, inputText);
        this.OnValidate(args);
        Debug.Assert(this.valueIsValid == args.IsValid, "should be updated by the OnValidate method");

        this.LogValidation(oldIsValid, this.valueIsValid);
    }

    private void OnEditBoxKeyDown(object sender, KeyRoutedEventArgs e)
    {
        this.LogKeyEvent(e.Key, "KeyDown");

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
        this.LogPointerEvent("Tapped");
        if (this.scrub.AcceptTap())
        {
            this.BeginTextEdit();
        }
    }

    private void OnValueChanged(double oldValue, double newValue)
    {
        // Log the value change
        this.LogValueChanged(oldValue, newValue);

        // external numeric assignment does not by itself clear the indeterminate presentation
        this.ValidateValue(this.NumberValue);
        this.UpdateDisplayText();
        this.UpdateVisualState();
    }

    private void OnMaskChanged()
    {
        this.maskParser = new MaskParser(this.Mask);
        this.UpdateMinimumWidth();
        this.UpdateDisplayText();
    }

    private void OnValueChanged()
    {
        // external numeric assignment does not by itself clear the indeterminate presentation
        this.ValidateValue(this.NumberValue);
        this.UpdateDisplayText();
        this.UpdateVisualState();
    }

    private void OnLabelPositionChanged() => this.UpdateLabelPosition();

    private void OnLabelForegroundChanged()
    {
        if (this.labelTextBlock is not null)
        {
            if (this.LabelForeground is { } labelBrush)
            {
                this.labelTextBlock.Foreground = labelBrush;
            }
            else
            {
                this.labelTextBlock.ClearValue(TextBlock.ForegroundProperty);
            }
        }

        if (this.compactLabelTextBlock is not null)
        {
            if (this.LabelForeground is { } labelBrush)
            {
                this.compactLabelTextBlock.Foreground = labelBrush;
            }
            else
            {
                this.compactLabelTextBlock.ClearValue(TextBlock.ForegroundProperty);
            }
        }
    }

    private void UpdateDisplayText()
    {
        if (this.IsIndeterminate)
        {
            this.DisplayText = this.IndeterminateDisplayText ?? DefaultIndeterminateDisplayText;
            return;
        }

        this.DisplayText = this.maskParser.FormatValue(this.NumberValue, trimTrailingZeros: this.TrimTrailingZeros);
    }

    private void UpdateMinimumWidth()
    {
        if (this.valueTextBlock == null)
        {
            return;
        }

        var sampleText = "-" + this.maskParser.FormatValue(0, withPadding: true, trimTrailingZeros: this.TrimTrailingZeros);
        this.valueTextBlock.MinWidth = TextMeasurer.Width(sampleText, this.valueTextBlock);
    }

    private void OnWithPaddingChanged() => this.UpdateDisplayText();

    private void OnTrimTrailingZerosChanged()
    {
        this.UpdateDisplayText();
        this.UpdateMinimumWidth();
        this.UpdateLabelPosition();
    }

    private void OnIsIndeterminateChanged()
    {
        // indeterminate is considered valid
        this.valueIsValid = true;
        this.UpdateDisplayText();
        this.UpdateVisualState();
    }

    private void OnIndeterminateDisplayTextChanged() => this.UpdateDisplayText();

    // Setting the alignments invalidates the parts' layout, which the next layout pass applies.
    private void OnHorizontalValueAlignmentChanged()
    {
        this.editTextBox?.TextAlignment = this.HorizontalValueAlignment;
        this.valueTextBlock?.HorizontalTextAlignment = this.HorizontalValueAlignment;
    }
}
