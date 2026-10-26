// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using static Oxygen.Editor.Controls.Testing.PropertyEditorProbes;
using static Oxygen.Editor.SceneInspector.TestSupport.InspectorControls;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

public sealed partial class InspectorBindingTests
{
    private const float AlbedoRedLinear = 0.21404114f; // sRGB 128/255
    private const float AlbedoGreenLinearPicked = 0.05126946f; // sRGB 64/255
    private const string AppliesInAutoExposureModeCopy = "applies in Auto exposure mode";
    private const string AppliesInAutoExposureSpotMeteringCopy = "applies in Auto exposure mode with Spot metering";
    private const string StoredValueNotePrefixCopy = "Stored value";

    private static readonly Dictionary<string, Func<Oxygen.Editor.SceneInspector.TestSupport.EnvironmentInspectorScenario, Task>> VectorFieldPreparations = new(StringComparer.Ordinal)
    {
        ["GroundAlbedo"] = scenario => scenario.ExpandGroupAsync("SkyAtmosphereSection", "Planet & ground"),
        ["SolidColor"] = scenario =>
        {
            ShowSolidColorBackdrop(scenario.Fixture);
            return scenario.WaitForRenderAsync();
        },
    };

    // Shows the solid color backdrop without recording authoring history.
    private static void ShowSolidColorBackdrop(Oxygen.Editor.SceneInspector.TestSupport.SceneAuthoringFixture fixture)
        => fixture.Scene.SetEnvironment(fixture.Scene.Environment with
        {
            AtmosphereEnabled = false,
            SkySphere = fixture.Scene.Environment.SkySphere with { Enabled = true, Source = Oxygen.Editor.World.Serialization.SkySphereSource.SolidColor },
        });

    private static IEnumerable<Oxygen.Editor.Controls.PropertyCard> SceneCards(Oxygen.Editor.Controls.PropertiesExpander section)
        => section.Items.SelectMany(SceneCardsOf);

    // Disclosures and conditional field groups nest cards one panel deep.
    private static IEnumerable<Oxygen.Editor.Controls.PropertyCard> SceneCardsOf(object item)
        => item switch
        {
            Oxygen.Editor.Controls.PropertyCard card => [card],
            Oxygen.Editor.Controls.NumberField number => [CardOf(number)],
            Oxygen.Editor.Controls.ColorField color => [CardOf(color)],
            Expander { Content: StackPanel content } => content.Children.SelectMany(SceneCardsOf),
            StackPanel panel => panel.Children.SelectMany(SceneCardsOf),
            _ => [],
        };

    private static IEnumerable<string> SceneFieldLabels(Oxygen.Editor.Controls.PropertiesExpander section)
        => section.Items.SelectMany(item => item switch
        {
            Expander { Content: StackPanel content } => content.Children.Cast<object>(),
            _ => [item],
        }).Where(item => item is not TextBlock).Select(item => item switch
        {
            Oxygen.Editor.Controls.NumberField number => number.Label,
            Oxygen.Editor.Controls.ColorField color => color.Label,
            Oxygen.Editor.Controls.PropertyCard card => card.PropertyName,
            _ => throw new InvalidOperationException($"Unexpected field composition {item.GetType().Name}."),
        });

    private static void AssertDisclosureCardOrder(Expander disclosure, params string[] propertyIdentities)
    {
        var identities = ((StackPanel)disclosure.Content).Children.Where(child => child is not TextBlock).Select(child => child switch
        {
            Oxygen.Editor.Controls.NumberField numeric => (string)numeric.Tag,
            Oxygen.Editor.Controls.ColorField color => (string)color.Tag,
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
}
