// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Mvvm.Converters;
using DryIoc;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.WorldEditor.ViewModels;
using Oxygen.Editor.WorldEditor.Views;

namespace Oxygen.Editor.WorldEditor.Workspace;

internal sealed class DockViewFactory(IResolver container) : IDockViewFactory
{
    /// <inheritdoc/>
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
                VmToViewConverter = container.Resolve<ViewModelToView>(),
                ViewModel = container.Resolve<Func<IDock, DockPanelViewModel>>()(dock),
            },
        };
}
