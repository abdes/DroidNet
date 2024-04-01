// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System;
using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Hosting.Generators;
using DroidNet.Routing.Debugger.UI.Docks;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

[InjectAs(ServiceLifetime.Singleton, ContractType = typeof(IDockViewFactory))]
public class DockViewFactory(IResolver container) : IDockViewFactory
{
    public UIElement CreateViewForDock(IDock dock) => dock is ApplicationDock appDock
        ? new EmbeddedAppView()
        {
            ViewModel = container.Resolve<Func<object?, EmbeddedAppViewModel>>()(appDock.ApplicationViewModel),
        }
        : new Border()
        {
            BorderThickness = new Thickness(0.5),
            BorderBrush = new SolidColorBrush(Colors.Bisque),
            Child = new DockPanel()
            {
                ViewModel = container.Resolve<Func<IDock, DockPanelViewModel>>()(dock),
            },
        };
}
