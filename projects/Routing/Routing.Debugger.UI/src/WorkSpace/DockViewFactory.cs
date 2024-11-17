// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Routing.Debugger.UI.Docks;
using DryIoc;
using Microsoft.UI.Xaml;

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

/// <summary>
/// Factory class for creating views for docks.
/// </summary>
/// <param name="container">The dependency injection container used to resolve view models.</param>
public class DockViewFactory(IResolver container) : IDockViewFactory
{
    /// <summary>
    /// Creates a UIElement for the given dock.
    /// </summary>
    /// <param name="dock">The dock object for which a UIElement needs to be created.</param>
    /// <returns>A UIElement for the dock, which can be placed into the docking workspace visual tree.</returns>
    public UIElement CreateViewForDock(IDock dock) => dock is ApplicationDock appDock
        ? new EmbeddedAppView()
        {
            ViewModel = container.Resolve<Func<object?, EmbeddedAppViewModel>>()(appDock.ApplicationViewModel),
        }
        : new DockPanel()
        {
            ViewModel = container.Resolve<Func<IDock, DockPanelViewModel>>()(dock),
        };
}
