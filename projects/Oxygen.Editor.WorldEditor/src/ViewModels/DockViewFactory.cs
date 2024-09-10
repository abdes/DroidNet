// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ViewModels;

using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DryIoc;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.WorldEditor.Views;

internal class DockViewFactory(IResolver container) : IDockViewFactory
{
    public UIElement CreateViewForDock(IDock dock) => dock is CenterDock
        ? new RendererView()
        {
            ViewModel = container.Resolve<RendererViewModel>(),
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
