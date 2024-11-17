// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking.Controls;
using DroidNet.Docking.Demo.Controls;
using DroidNet.Docking.Layouts;
using DryIoc;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Docking.Demo;

/// <summary>
/// Provides a factory for creating views for docks within the docking framework.
/// </summary>
/// <param name="container">The dependency injection container used to resolve view models and other dependencies.</param>
/// <remarks>
/// <para>
/// The <see cref="DockViewFactory"/> class implements the <see cref="IDockViewFactory"/> interface to create custom views for docks.
/// It uses a dependency injection container to resolve view models and other dependencies required for creating the views.
/// </para>
/// <para>
/// This factory is particularly useful for dynamically generating UI elements based on the state and type of the dock.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create an instance of <see cref="DockViewFactory"/> and use it to create views for docks, use the following code:
/// </para>
/// <code><![CDATA[
/// var container = new Container();
/// var factory = new DockViewFactory(container);
/// var dock = new CustomDock();
/// var view = factory.CreateViewForDock(dock);
/// ]]></code>
/// </example>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public because the 'ViewModel' property in the View is public")]
public class DockViewFactory(IResolver container) : IDockViewFactory
{
    /// <inheritdoc/>
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
