// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

public sealed partial class InspectorBindingTests
{
    private const float AlbedoRedLinear = 0.21404114f; // sRGB 128/255
    private const float AlbedoGreenLinearPicked = 0.05126946f; // sRGB 64/255
    private const string ChevronExpandedGlyph = "\uE70D"; // Segoe MDL2 chevron pointing down
    private const string ChevronCollapsedGlyph = "\uE76C"; // Segoe MDL2 chevron pointing right

    private static readonly (string Part, string Letter, string Label, Windows.UI.Color Color)[] RgbChannels =
    [
        ("PartNumberBoxX", "X", "R", Microsoft.UI.Colors.Red),
        ("PartNumberBoxY", "Y", "G", Microsoft.UI.Colors.Green),
        ("PartNumberBoxZ", "Z", "B", Microsoft.UI.Colors.Blue),
    ];

    private const string AppliesInAutoExposureModeCopy = "applies in Auto exposure mode";
    private const string AppliesInAutoExposureSpotMeteringCopy = "applies in Auto exposure mode with Spot metering";
    private const string StoredValueNotePrefixCopy = "Stored value";

    private static readonly Dictionary<string, Func<Oxygen.Editor.WorldEditor.TestSupport.EnvironmentInspectorScenario, Task>> VectorFieldPreparations = new(StringComparer.Ordinal)
    {
        ["GroundAlbedo"] = scenario => scenario.ExpandGroupAsync("SkyAtmosphereSection", "Planet & ground"),
    };

    private static IEnumerable<Oxygen.Editor.Controls.PropertyCard> SceneCards(Oxygen.Editor.Controls.PropertiesExpander section)
        => section.Items.SelectMany(item => item switch
        {
            Oxygen.Editor.Controls.PropertyCard card => [card],
            Oxygen.Editor.Controls.InspectorNumberField { Content: Oxygen.Editor.Controls.PropertyCard card } => [card],
            Oxygen.Editor.World.Inspector.Controls.InspectorRgbField { Content: Oxygen.Editor.Controls.PropertyCard card } => [card],
            Expander { Content: StackPanel content } => content.Children.SelectMany(child => child switch
            {
                Oxygen.Editor.Controls.PropertyCard card => [card],
                Oxygen.Editor.Controls.InspectorNumberField { Content: Oxygen.Editor.Controls.PropertyCard card } => [card],
                Oxygen.Editor.World.Inspector.Controls.InspectorRgbField { Content: Oxygen.Editor.Controls.PropertyCard card } => [card],
                _ => Enumerable.Empty<Oxygen.Editor.Controls.PropertyCard>(),
            }),
            _ => Enumerable.Empty<Oxygen.Editor.Controls.PropertyCard>(),
        });

    private static IEnumerable<string> SceneFieldLabels(Oxygen.Editor.Controls.PropertiesExpander section)
        => section.Items.SelectMany(item => item switch
        {
            Expander { Content: StackPanel content } => content.Children.Cast<object>(),
            _ => [item],
        }).Select(item => item switch
        {
            Oxygen.Editor.Controls.InspectorNumberField number => number.Label,
            Oxygen.Editor.World.Inspector.Controls.InspectorRgbField rgb => rgb.Label,
            Oxygen.Editor.Controls.PropertyCard card => card.PropertyName,
            _ => throw new InvalidOperationException($"Unexpected field composition {item.GetType().Name}."),
        });

    private static void AssertDisclosureCardOrder(Expander disclosure, params string[] propertyIdentities)
    {
        var identities = ((StackPanel)disclosure.Content).Children.Select(child => child switch
        {
            Oxygen.Editor.Controls.InspectorNumberField numeric => (string)numeric.Tag,
            Oxygen.Editor.World.Inspector.Controls.InspectorRgbField rgb => (string)rgb.Tag,
            Oxygen.Editor.Controls.PropertyCard card => GetCardPropertyIdentity(card),
            _ => throw new InvalidOperationException($"Unexpected field composition {child.GetType().Name}."),
        }).ToArray();
        _ = identities.Should().Equal(propertyIdentities);
    }

    private static string GetCardPropertyIdentity(Oxygen.Editor.Controls.PropertyCard card)
    {
        if (card.Content is FrameworkElement { Tag: string directTag })
        {
            return directTag.Split(' ', StringSplitOptions.RemoveEmptyEntries)[0];
        }

        if (card.Content is DependencyObject content
            && content.FindDescendant<FrameworkElement>(element => element.Tag is string) is { Tag: string nestedTag })
        {
            return nestedTag.Split(' ', StringSplitOptions.RemoveEmptyEntries)[0];
        }

        throw new InvalidOperationException($"Card '{card.PropertyName}' has no stable tagged editor identity.");
    }

    private static void AssertRgbChannelBasics(NumberBox input, string expectedLabel, Windows.UI.Color expectedColor)
    {
        var compactLabel = input.FindDescendant<TextBlock>(text => text.Name == "PartCompactLabelTextBlock")!;
        _ = compactLabel.Visibility.Should().Be(Visibility.Visible);
        _ = compactLabel.Text.Should().Be(expectedLabel);
        _ = ((Microsoft.UI.Xaml.Media.SolidColorBrush)compactLabel.Foreground).Color.Should().Be(expectedColor);
    }

    private async Task AssertEditSessionPreservesGeometryAsync(NumberBox input, FrameworkElement container, Func<TextBox, Task>? whileEditing = null)
    {
        var beforeWidth = input.ActualWidth;
        var beforeHeight = input.ActualHeight;
        var beforePosition = input.TransformToVisual(container).TransformPoint(default);
        var beforeContainerWidth = container.ActualWidth;
        var beforeContainerHeight = container.ActualHeight;
        input.StartEdit();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var editor = input.FindDescendant<TextBox>(text => text.Name == "PartEditBox")!;
        _ = editor.Visibility.Should().Be(Visibility.Visible);
        if (whileEditing is { } hook)
        {
            await hook(editor).ConfigureAwait(true);
        }

        _ = editor.FindDescendants().OfType<Button>().Should().NotContain(button => button.Visibility == Visibility.Visible && button.ActualWidth > 0);
        _ = input.ActualWidth.Should().BeApproximately(beforeWidth, 1);
        _ = input.ActualHeight.Should().BeApproximately(beforeHeight, 1);
        _ = input.TransformToVisual(container).TransformPoint(default).X.Should().BeApproximately(beforePosition.X, 1);
        _ = input.TransformToVisual(container).TransformPoint(default).Y.Should().BeApproximately(beforePosition.Y, 1);
        _ = container.ActualWidth.Should().BeApproximately(beforeContainerWidth, 1);
        _ = container.ActualHeight.Should().BeApproximately(beforeContainerHeight, 1);
        input.CancelEdit();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
    }
}
