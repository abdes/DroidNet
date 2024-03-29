// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo;

using DroidNet.Docking.Controls;
using DroidNet.Docking.Demo.Welcome;
using DroidNet.Docking.Layouts;
using DroidNet.Hosting.Generators;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

[InjectAs(ServiceLifetime.Singleton, ContractType = typeof(IDockViewFactory))]
public class DockViewFactory(IResolver container) : IDockViewFactory
{
    public UIElement CreateViewForDock(IDock dock) => dock is CenterDock
        ? new WelcomeView()
        {
            ViewModel = container.Resolve<WelcomeViewModel>(),
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
