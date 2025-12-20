// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Mvvm.Converters;
using DryIoc;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// A custom factory to create views for the docks in the docking workspace.
/// </summary>
/// <param name="container">The IoC container to use for resolution of view models and other services.</param>
[SuppressMessage("Microsoft.Performance", "CA1812:Avoid uninstantiated internal classes", Justification = "This class is instantiated by dependency injection.")]
internal sealed class DockViewFactory(IResolver container) : IDockViewFactory
{
    /// <inheritdoc/>
    public UIElement CreateViewForDock(IDock dock)
        => dock is CenterDock
            ? new DocumentHostView() { ViewModel = container.Resolve<DocumentHostViewModel>(), }
            : new Border()
            {
                BorderThickness = new Thickness(0.5),
                BorderBrush = new SolidColorBrush(Colors.Bisque),
                Child = new DockPanel()
                {
                    // We explicitly set the VmToViewConverter property of the DockPanel to ensure that
                    // its view will use our own converter, resolved through the child IoC container of
                    // the docking workspace.
                    VmToViewConverter = container.Resolve<ViewModelToView>(),

                    // We manually resolve the DockPanel ViewModel, using a factory function, yo pass
                    // the specific dock we are docking.
                    ViewModel = container.Resolve<Func<IDock, DockPanelViewModel>>()(dock),
                },
            };
}
