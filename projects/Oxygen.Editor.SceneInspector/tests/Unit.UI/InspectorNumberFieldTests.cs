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
using Oxygen.Editor.Controls;
using static Oxygen.Editor.SceneInspector.TestSupport.InspectorControls;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

[TestClass]
internal sealed class InspectorNumberFieldTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task ComposedFieldForwardsItsNameUnitMixedStateAndFeedback() => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = CreateField(host);
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
        var field = CreateField(host);
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var number = field.FindDescendant<NumberBox>()!;
        NumberBoxEditSessionEventArgs? nativeCompletion = null;
        NumberBoxEditSessionEventArgs? forwardedCompletion = null;
        field.EditSessionCompleted += (_, args) => forwardedCompletion = args;
        number.EditSessionCompleted += (_, args) => nativeCompletion = args;
        await EnterTextAsync(number, "90/2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        _ = field.NumberValue.Should().Be(45);
        _ = forwardedCompletion.Should().BeSameAs(nativeCompletion);
        _ = forwardedCompletion!.InputText.Should().Be("90/2");
    });

    private static Grid CreateHost(double width)
    {
        var host = new Grid { Width = width, HorizontalAlignment = HorizontalAlignment.Left };
        host.Resources.MergedDictionaries.Add(new ResourceDictionary
        {
            Source = new Uri("ms-appx:///Oxygen.Editor.SceneInspector/SceneInspectorStyles.xaml"),
        });
        return host;
    }

    private static InspectorNumberField CreateField(Grid host)
        => new()
        {
            Label = "Near plane",
            Unit = "m",
            NumberValue = 2,
            FontSize = 14,
            NumberStyle = (Style)host.Resources["SceneNumberBoxStyle"],
            CardStyle = (Style)host.Resources["ScenePropertyCardStyle"],
        };
}
