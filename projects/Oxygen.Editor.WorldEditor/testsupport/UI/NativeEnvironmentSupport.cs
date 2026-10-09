// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldCases;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class NativeEnvironmentSupport
{
    internal static ToggleSwitch? FindEnvironmentToggle(EnvironmentView view, string section) => view.FindDescendant<ToggleSwitch>(element => Equals(element.FindAscendant<Oxygen.Editor.Controls.PropertiesExpander>()?.Header, section));

    internal static FrameworkElement? FindEnvironmentControl(EnvironmentView view, EnvironmentViewModel model, EnvironmentFieldCase field) => field.Field switch
    {
        "ExposureEnabled" => FindEnvironmentToggle(view, "Exposure"),
        "SunDiskEnabled" => view.FindDescendant<ToggleSwitch>(element => Equals(element.Tag, "SunDiskEnabled")),
        "ExposureMode" => view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.Exposure.ExposureModes)),
        "ToneMapping" => view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.PostProcessing.ToneMappingModes)),
        "AutoExposureMeteringMode" => view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.Exposure.MeteringModes)),
        _ when field.VectorTag is { } tag => view.FindDescendant<VectorBox>(element => Equals(element.Tag, tag))?.FindDescendant<NumberBox>(element => string.Equals(element.Name, $"PartNumberBox{field.VectorAxis}", StringComparison.Ordinal)),
        _ => view.FindDescendant<NumberBox>(element => Equals(element.Tag, field.Field)),
    };
}
