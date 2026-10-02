// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.Tests;

/// <summary>Exercises save-time completion on a realized numeric control and its real text editor.</summary>
[TestClass]
[TestCategory("UITest")]
public sealed class NumberBoxSaveTests : VisualUserInterfaceTests
{
    /// <summary>Saving commits valid focused text once, including the later focus-loss callback.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task SaveCompletesFocusedValidTextExactlyOnce() => EnqueueAsync(async () =>
    {
        var number = new NumberBox { NumberValue = 5, Mask = "~.###" };
        var next = new Button { Content = "Next" };
        var root = new StackPanel();
        root.Children.Add(number);
        root.Children.Add(next);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        var completions = new List<NumberBoxEditCompletionKind?>();
        number.EditSessionCompleted += (_, args) => completions.Add(args.CompletionKind);
        var input = BeginTextInput(number);
        input.Text = "8";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(5, "draft text is committed through the edit session");

        number.CompletePendingTextEdit();
        number.CompletePendingTextEdit();
        _ = next.Focus(FocusState.Programmatic);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        _ = number.NumberValue.Should().Be(8);
        _ = completions.Should().Equal(NumberBoxEditCompletionKind.Commit);
    });

    /// <summary>Saving cancels invalid focused text and preserves the last numeric value.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task SaveCancelsInvalidTextWithoutChangingTheValue() => EnqueueAsync(async () =>
    {
        var number = new NumberBox { NumberValue = 5, Mask = "~.###" };
        await LoadTestContentAsync(number).ConfigureAwait(true);
        var completions = new List<NumberBoxEditCompletionKind?>();
        number.EditSessionCompleted += (_, args) => completions.Add(args.CompletionKind);
        var input = BeginTextInput(number);
        input.Text = "invalid";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        number.CompletePendingTextEdit();

        _ = number.NumberValue.Should().Be(5);
        _ = completions.Should().Equal(NumberBoxEditCompletionKind.Cancel);
    });

    /// <summary>A pointer press on non-focusable content outside the control completes text editing.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task PointerPressOutsideOnNonFocusableContentCompletesTextEdit() => EnqueueAsync(async () =>
    {
        var number = new NumberBox { NumberValue = 5, Mask = "~.###" };
        var outside = new Border { Height = 40, Background = new SolidColorBrush(Colors.Transparent) };
        var root = new StackPanel();
        root.Children.Add(number);
        root.Children.Add(outside);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        var completions = new List<NumberBoxEditCompletionKind?>();
        number.EditSessionCompleted += (_, args) => completions.Add(args.CompletionKind);

        var input = BeginTextInput(number);
        input.Text = "8";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = typeof(NumberBox).GetMethod("CompleteTextEditIfPointerPressedOutside", BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(number, [outside]);

        _ = number.NumberValue.Should().Be(8);
        _ = input.Visibility.Should().Be(Visibility.Collapsed);
        _ = completions.Should().Equal(NumberBoxEditCompletionKind.Commit);
    });

    /// <summary>Opening the text editor does not round the value through XAML's string conversion.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task UnchangedTextCommitPreservesTheExactNumericValue() => EnqueueAsync(async () =>
    {
        const float original = 16f / 9f;
        var number = new NumberBox { NumberValue = original, Mask = "~.###" };
        await LoadTestContentAsync(number).ConfigureAwait(true);
        _ = BeginTextInput(number);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(original);

        number.CompletePendingTextEdit();

        _ = number.NumberValue.Should().Be(original);
    });

    /// <summary>Text commits evaluate arithmetic expressions and relative operations.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task ArithmeticTextCommitEvaluatesExpressionsAndRelativeOperations() => EnqueueAsync(async () =>
    {
        var number = new NumberBox { NumberValue = 5, Mask = "~.###" };
        await LoadTestContentAsync(number).ConfigureAwait(true);
        var committedText = new List<string?>();
        number.EditSessionCompleted += (_, args) => committedText.Add(args.InputText);

        var input = BeginTextInput(number);
        input.Text = "90/2";
        number.CompletePendingTextEdit();
        _ = number.NumberValue.Should().Be(45);

        input = BeginTextInput(number);
        input.Text = "+=2";
        number.CompletePendingTextEdit();
        _ = number.NumberValue.Should().Be(47);
        _ = committedText.Should().Equal("90/2", "+=2");
    });

    /// <summary>Labels are optional and compact labels share the value editor border.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task CompactLabelIsOptionalAndUsesItsConfiguredForeground() => EnqueueAsync(async () =>
    {
        var brush = new SolidColorBrush(Colors.Red);
        var withoutLabel = new NumberBox { NumberValue = 1, LabelPosition = LabelPosition.None };
        var compact = new NumberBox
        {
            NumberValue = 1,
            Label = "X",
            LabelPosition = LabelPosition.Left,
            LabelForeground = brush,
            IsCompact = true,
        };
        var root = new StackPanel();
        root.Children.Add(withoutLabel);
        root.Children.Add(compact);
        await LoadTestContentAsync(root).ConfigureAwait(true);

        _ = withoutLabel.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartLabelTextBlock", StringComparison.Ordinal))!.Visibility.Should().Be(Visibility.Collapsed);
        var compactLabel = compact.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartCompactLabelTextBlock", StringComparison.Ordinal))!;
        _ = compactLabel.Visibility.Should().Be(Visibility.Visible);
        _ = compactLabel.Foreground.Should().BeSameAs(brush);
        _ = ((Grid)compactLabel.Parent).ColumnSpacing.Should().Be(0);
    });

    private static TextBox BeginTextInput(NumberBox number)
    {
        // Enter the same method reached by the value-label tap; validation, binding,
        // template parts, focus and completion all run on the realized control.
        _ = typeof(NumberBox).GetMethod("StartEdit", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(number, parameters: null);
        return number.FindDescendant<TextBox>(element => string.Equals(element.Name, "PartEditBox", StringComparison.Ordinal))!;
    }
}
