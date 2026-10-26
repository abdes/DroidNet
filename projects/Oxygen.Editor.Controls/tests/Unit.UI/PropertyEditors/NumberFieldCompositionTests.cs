// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using static Oxygen.Editor.Controls.Testing.PropertyEditorProbes;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>
/// A number field composes a property row around a NumberBox from its template: it forwards its name, unit, prefix,
/// mixed state and feedback to the template's parts, and forwards the NumberBox's events with itself as sender.
/// </summary>
[TestClass]
internal sealed class NumberFieldCompositionTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task ComposedFieldForwardsItsNameUnitMixedStateAndFeedback() => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = CreateField();
        field.Prefix = "≈";
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var number = field.FindDescendant<NumberBox>()!;
        _ = CardOf(field).PropertyName.Should().Be("Near plane");
        _ = AutomationProperties.GetName(number).Should().Be("Near plane");
        _ = number.Unit.Should().Be("m");
        _ = number.Prefix.Should().Be("≈");
        _ = number.Mask.Should().Be("~.###");

        field.IsMixed = true;
        field.HasError = true;
        field.ErrorText = "The near plane must be before the far plane.";
        field.ApplicabilityText = "Stored value.";
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = number.IsIndeterminate.Should().BeTrue();
        _ = field.FindDescendants().OfType<TextBlock>().Should().Contain(note => note.Text == field.ErrorText && note.Visibility == Visibility.Visible);
        _ = field.FindDescendants().OfType<TextBlock>().Should().Contain(note => note.Text == field.ApplicabilityText && note.Visibility == Visibility.Visible);

        field.HasError = false;
        field.ApplicabilityText = string.Empty;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = field.FindDescendants().OfType<TextBlock>().Should().NotContain(note => note.Visibility == Visibility.Visible && (note.Text == field.ErrorText || note.Text == "Stored value."));
    });

    [TestMethod]
    public Task ComposedFieldForwardsOriginalExpressionValidationAndCompletionArguments() => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = CreateField();
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var number = field.FindDescendant<NumberBox>()!;
        NumberBoxEditSessionEventArgs? nativeCompletion = null;
        NumberBoxEditSessionEventArgs? forwardedCompletion = null;
        object? completionSender = null;
        field.EditSessionCompleted += (sender, args) => (completionSender, forwardedCompletion) = (sender, args);
        number.EditSessionCompleted += (_, args) => nativeCompletion = args;
        await EnterTextAsync(number, "90/2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        _ = field.NumberValue.Should().Be(45);
        _ = forwardedCompletion.Should().BeSameAs(nativeCompletion);
        _ = forwardedCompletion!.InputText.Should().Be("90/2");
        _ = completionSender.Should().BeSameAs(field, "the field's tag names the edited field");
    });

    [TestMethod]
    public Task ANewValueIsValidatedThroughTheFieldWithTheFieldAsSender() => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = CreateField();
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var validations = new List<(object? Sender, double? NewValue)>();
        field.Validate += (sender, args) => validations.Add((sender, args.NewValue));

        await EnterTextAsync(field.FindDescendant<NumberBox>()!, "7").ConfigureAwait(true);
        field.FindDescendant<NumberBox>()!.CompletePendingTextEdit();

        _ = validations.Should().Contain(validation => ReferenceEquals(validation.Sender, field) && validation.NewValue == 7);
        _ = field.NumberValue.Should().Be(7);
    });

    private static Grid CreateHost(double width) => new() { Width = width, HorizontalAlignment = HorizontalAlignment.Left };

    private static NumberField CreateField()
        => new()
        {
            Label = "Near plane",
            Unit = "m",
            NumberValue = 2,
            FontSize = 14,
        };
}
