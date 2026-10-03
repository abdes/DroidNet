// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Controls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
public sealed class InspectorNumberFieldTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow(260d, 1d)]
    [DataRow(340d, 1d)]
    [DataRow(480d, 1d)]
    [DataRow(420d, 1.5d)]
    [DataRow(480d, 2d)]
    [DataRow(760d, 2d)]
    public Task ComposedField_PreservesNativeCaptionTypographyAndUnitAlignment(double width, double textScale) => EnqueueAsync(async () =>
    {
        var host = CreateHost(width);
        var field = CreateField(host);
        field.FontSize = 14 * textScale;
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var number = field.FindDescendant<NumberBox>()!;
        var card = number.FindAscendant<PropertyCard>()!;
        var label = number.FindDescendant<TextBlock>(part => part.Name == "PartLabelTextBlock")!;
        var suffix = number.FindDescendant<TextBlock>(part => part.Name == "PartValueQualifier")!;
        var border = number.FindDescendant<Border>(part => part.Name == "PartBackgroundBorder")!;
        _ = number.Label.Should().Be("Near plane");
        _ = label.Visibility.Should().Be(Visibility.Visible);
        _ = label.FontSize.Should().Be(14 * textScale);
        _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
        _ = label.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
        _ = card.FindDescendant<TextBlock>(part => part.Name == "PropertyName")!.ActualWidth.Should().Be(0);
        _ = number.Padding.Should().Be(new Thickness(6, 4, 6, 4));
        _ = number.Mask.Should().Be("~.###");
        var inputPoint = border.TransformToVisual(card).TransformPoint(default);
        var suffixPoint = suffix.TransformToVisual(card).TransformPoint(default);
        _ = (suffixPoint.X - inputPoint.X - border.ActualWidth - border.Margin.Right).Should().BeApproximately(4, 1);
        _ = (suffixPoint.X + suffix.ActualWidth).Should().BeLessThanOrEqualTo(width + 1);
        if (card.ActualLayout == PropertyLayout.Inline)
        {
            _ = (inputPoint.X - border.Margin.Left).Should().BeApproximately(((width - 12) * 0.4) + 12, 1);
        }

        field.IsMixed = true;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = number.IsIndeterminate.Should().BeTrue();
        field.HasError = true;
        field.ErrorText = "The near plane must be before the far plane.";
        field.ApplicabilityText = "Stored value.";
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = field.FindDescendants().OfType<TextBlock>().Should().Contain(note => note.Text == field.ErrorText && note.Visibility == Visibility.Visible);
        _ = field.FindDescendants().OfType<TextBlock>().Should().Contain(note => note.Text == field.ApplicabilityText && note.Visibility == Visibility.Visible);
    });

    [TestMethod]
    public Task ComposedField_ForwardsOriginalExpressionValidationAndCompletionArguments() => EnqueueAsync(async () =>
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

    [TestMethod]
    public Task ComposedField_NativeCaptionDragSurvivesReflowAndReload() => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = CreateField(host);
        host.Children.Add(field);
        var viewport = new Grid { Width = 760, Height = 480 };
        viewport.Children.Add(host);
        using var scaled = new ScaledXamlHost();
        await scaled.LoadAsync(viewport, 1, CancellationToken.None).ConfigureAwait(true);
        var number = field.FindDescendant<NumberBox>()!;
        var label = number.FindDescendant<TextBlock>(part => part.Name == "PartLabelTextBlock")!;
        var starts = 0;
        var completions = 0;
        field.EditSessionStarted += (_, _) => starts++;
        field.EditSessionCompleted += (_, _) => completions++;
        using (var pointer = await NativePointer.PressAsync(label, CancellationToken.None).ConfigureAwait(true))
        {
            await pointer.MoveAsync(12, CancellationToken.None).ConfigureAwait(true);
            host.Width = 260;
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = number.FindDescendant<TextBlock>(part => part.Name == "PartLabelTextBlock").Should().BeSameAs(label);
            await pointer.MoveAsync(12, CancellationToken.None).ConfigureAwait(true);
            await pointer.ReleaseAsync(CancellationToken.None).ConfigureAwait(true);
        }

        _ = starts.Should().Be(1);
        _ = completions.Should().Be(1);
        _ = field.NumberValue.Should().BeGreaterThan(2);
        host.Children.Clear();
        await WaitForRenderAsync().ConfigureAwait(true);
        host.Children.Add(field);
        await WaitForRenderAsync().ConfigureAwait(true);
        label = number.FindDescendant<TextBlock>(part => part.Name == "PartLabelTextBlock")!;
        using var reloaded = await NativePointer.PressAsync(label, CancellationToken.None).ConfigureAwait(true);
        await reloaded.MoveAsync(12, CancellationToken.None).ConfigureAwait(true);
        await reloaded.ReleaseAsync(CancellationToken.None).ConfigureAwait(true);
        _ = starts.Should().Be(2);
        _ = completions.Should().Be(2);
    });

    private static Grid CreateHost(double width)
    {
        var host = new Grid { Width = width, HorizontalAlignment = HorizontalAlignment.Left };
        host.Resources.MergedDictionaries.Add(new ResourceDictionary
        {
            Source = new Uri("ms-appx:///Oxygen.Editor.WorldEditor/Inspector/SceneInspectorStyles.xaml"),
        });
        return host;
    }

    private static InspectorNumberField CreateField(Grid host)
        => new()
        {
            Label = "Near plane",
            Qualifier = "m",
            NumberValue = 2,
            FontSize = 14,
            NumberStyle = (Style)host.Resources["SceneNumberBoxStyle"],
            CardStyle = (Style)host.Resources["ScenePropertyCardStyle"],
        };
}
