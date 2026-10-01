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

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class InspectorFieldControls
{
    internal static async Task<FrameworkElement> FindEnvironmentFieldControlAsync(EnvironmentView view, ScrollViewer scroller, EnvironmentViewModel model, EnvironmentFieldCase field, CancellationToken cancellationToken)
    {
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
            "AtmosphereSlot" => view.FindDescendant<ComboBox>(combo => ReferenceEquals(combo.ItemsSource, light.AtmosphereSlotOptions)),
            "UsePerPixelAtmosphereTransmittance" => view.FindDescendant<ToggleSwitch>(toggle => Equals(toggle.Header, "Per-pixel transmittance")),
            "AffectsWorld" => view.FindDescendant<ToggleSwitch>(toggle => Equals(toggle.Header, "Affects World")),
            "CastsShadows" => view.FindDescendant<ToggleSwitch>(toggle => Equals(toggle.Header, "Cast")),
            "ContactShadows" => view.FindDescendant<ToggleSwitch>(toggle => Equals(toggle.Header, "Contact")),
            "ShadowResolutionHint" => view.FindDescendant<ComboBox>(combo => ReferenceEquals(combo.ItemsSource, light.ShadowResolutionOptions)),
            "SplitMode" => view.FindDescendant<ComboBox>(combo => ReferenceEquals(combo.ItemsSource, light.SplitModeOptions)),
            _ => view.FindDescendant<NumberBox>(number => Equals(number.Tag, field.Field)),
        } : view.FindDescendant<NumberBox>(number => Equals(number.Tag, field.Field));
    }
}
