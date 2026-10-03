// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
public sealed class TransformPresentationTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow(280d, ElementTheme.Light)]
    [DataRow(280d, ElementTheme.Dark)]
    [DataRow(760d, ElementTheme.Light)]
    [DataRow(760d, ElementTheme.Dark)]
    public Task TransformRowsShowUnitsAndColoredAxisLabels(double width, ElementTheme theme) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var inspector = fixture.CreateInspectorHost("Transform");
        var model = inspector.PropertyEditors.OfType<TransformViewModel>().Single();
        var view = new TransformView { ViewModel = model, Width = width, RequestedTheme = theme };
        await LoadTestContentAsync(view).ConfigureAwait(true);

        var cards = view.FindDescendants().OfType<PropertyCard>().ToArray();
        _ = cards.Should().HaveCount(3);
        foreach (var card in cards)
        {
            var expectedUnit = card.PropertyName switch
            {
                "Position" => "m",
                "Rotation" => "°",
                "Scale" => "x",
                _ => throw new InvalidOperationException($"Unexpected Transform field: {card.PropertyName}"),
            };
            _ = card.Qualifier.Should().Be(expectedUnit);
            var suffix = card.FindDescendant<TextBlock>(text => text.Name == (card.ActualLayout == PropertyLayout.Stacked ? "HeaderQualifier" : "ValueQualifier"))!;
            _ = suffix.Text.Should().Be(expectedUnit);
            _ = suffix.Visibility.Should().Be(Visibility.Visible);
            _ = suffix.ActualWidth.Should().BePositive();

            var vector = card.FindDescendant<VectorBox>()!;
            foreach (var number in vector.FindDescendants().OfType<NumberBox>())
            {
                var label = number.FindDescendant<TextBlock>(text => text.Name == "PartCompactLabelTextBlock")!;
                var expectedColor = label.Text switch
                {
                    "X" => Colors.Red,
                    "Y" => Colors.Green,
                    "Z" => Colors.Blue,
                    _ => throw new InvalidOperationException($"Unexpected Transform axis: {label.Text}"),
                };
                _ = label.Visibility.Should().Be(Visibility.Visible);
                _ = ((SolidColorBrush)label.Foreground).Color.Should().Be(expectedColor);
                if (card.PropertyName == "Scale")
                {
                    _ = AutomationProperties.GetName(number).Should().Be($"Scale, multiplier {label.Text}");
                }
            }
        }

        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });
}
