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
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldCases;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class NativeEnvironmentSupport
{
    internal static ToggleSwitch? FindEnvironmentToggle(EnvironmentView view, string section) => view.FindDescendant<ToggleSwitch>(element => Equals(element.FindAscendant<Oxygen.Editor.Controls.PropertiesExpander>()?.Header, section));

    internal static FrameworkElement? FindEnvironmentControl(EnvironmentView view, EnvironmentViewModel model, EnvironmentFieldCase field) => field.Field switch
    {
        "AtmosphereEnabled" => FindEnvironmentToggle(view, "Sky Atmosphere"),
        "ExposureEnabled" => FindEnvironmentToggle(view, "Exposure"),
        "SunDiskEnabled" => view.FindDescendant<CheckBox>(),
        "ExposureMode" => view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.ExposureModes)),
        "ToneMapping" => view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.ToneMappingModes)),
        "AutoExposureMeteringMode" => view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.MeteringModes)),
        _ when field.VectorTag is { } tag => view.FindDescendant<VectorBox>(element => Equals(element.Tag, tag))?.FindDescendant<NumberBox>(element => string.Equals(element.Name, $"PartNumberBox{field.VectorAxis}", StringComparison.Ordinal)),
        _ => view.FindDescendant<NumberBox>(element => Equals(element.Tag, field.Field)),
    };
}
