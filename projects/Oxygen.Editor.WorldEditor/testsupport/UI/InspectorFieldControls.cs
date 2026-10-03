// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldCases;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeEnvironmentSupport;
using NumberBox = DroidNet.Controls.NumberBox;
using Expander = Microsoft.UI.Xaml.Controls.Expander;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class InspectorFieldControls
{
    internal static async Task<FrameworkElement> FindEnvironmentFieldControlAsync(EnvironmentView view, ScrollViewer scroller, EnvironmentViewModel model, EnvironmentFieldCase field, CancellationToken cancellationToken)
    {
        ((TextBox)view.FindName("ScenePropertySearchBox")).Text = field.VectorTag
            ?? (string.Equals(field.Field, "ToneMapping", StringComparison.Ordinal) ? "tone_mapper" : field.Field);
        foreach (var section in view.FindDescendants().OfType<Oxygen.Editor.Controls.PropertiesExpander>())
        {
            section.IsExpanded = true;
        }

        return await FindInspectorControlAsync(scroller, () => FindEnvironmentControl(view, model, field), field.Field, cancellationToken).ConfigureAwait(true);
    }

    internal static FrameworkElement? FindNodeControl(UserControl view, IPropertyEditor<SceneNode> model, NodeFieldCase field)
    {
        if (model is TransformViewModel)
        {
            var group = field.Field[..^1];
            return view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(card => Equals(card.PropertyName, group))?.FindDescendant<NumberBox>(number => string.Equals(number.Name, $"PartNumberBox{field.Field[^1]}", StringComparison.Ordinal));
        }

        return model is DirectionalLightViewModel light ? field.Field switch
        {
            "ColorR" or "ColorG" or "ColorB" or "DiskScaleR" or "DiskScaleG" or "DiskScaleB" => FindRgbChannel(view, field.Field),
            "AtmosphereSlot" => view.FindDescendant<ComboBox>(combo => ReferenceEquals(combo.ItemsSource, light.AtmosphereSlotOptions)),
            "UsePerPixelAtmosphereTransmittance" => FindToggle(view, "Per-pixel transmittance"),
            "AffectsWorld" => FindToggle(view, "Affects world"),
            "CastsShadows" => FindToggle(view, "Cast shadows"),
            "ContactShadows" => FindToggle(view, "Contact shadows"),
            "ShadowResolutionHint" => view.FindDescendant<ComboBox>(combo => ReferenceEquals(combo.ItemsSource, light.ShadowResolutionOptions)),
            "SplitMode" => view.FindDescendant<ComboBox>(combo => ReferenceEquals(combo.ItemsSource, light.SplitModeOptions)),
            _ => view.FindDescendant<NumberBox>(number => Equals(number.Tag, field.Field)),
        } : view.FindDescendant<NumberBox>(number => Equals(number.Tag, field.Field));
    }

    internal static Task<FrameworkElement> FindNodeFieldControlAsync(UserControl view, ScrollViewer scroller, IPropertyEditor<SceneNode> model, NodeFieldCase field, CancellationToken cancellationToken)
        => FindInspectorControlAsync(scroller, () =>
        {
            foreach (var disclosure in view.FindDescendants().OfType<Expander>())
            {
                disclosure.IsExpanded = true;
            }

            return FindNodeControl(view, model, field);
        }, field.Field, cancellationToken);

    private static NumberBox? FindRgbChannel(UserControl view, string field)
    {
        var component = field[^1] switch { 'R' => 'X', 'G' => 'Y', _ => 'Z' };
        return view.FindDescendant<VectorBox>(vector => Equals(vector.Tag, field[..^1]))?
            .FindDescendant<NumberBox>(number => string.Equals(number.Name, $"PartNumberBox{component}", StringComparison.Ordinal));
    }

    private static ToggleSwitch? FindToggle(UserControl view, string propertyName)
        => view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(card => Equals(card.PropertyName, propertyName))?.FindDescendant<ToggleSwitch>();
}
