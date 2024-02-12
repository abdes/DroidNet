// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using DroidNet.Routing.Events;

/// <summary>
/// The central building block of router based navigation in the application.
/// </summary>
/// <remarks>
/// An MVVM application is typically structured as a tree of view models, each
/// of which is rendered via corresponding views. In a windowed desktop
/// environment, these views are displayed in windows. While many applications
/// feature a single main window, more complex applications can open and manage
/// multiple windows simultaneously. Generally, navigation within the
/// application occurs within the same window. However, there are instances
/// where navigation to a different window is necessary, such as when opening a
/// document in a separate window.
/// <para>
/// The router facilitates navigation between view models within a specified
/// <see cref="IRouterContext">rendering context</see>, which determines where
/// the corresponding view should be rendered. In a windowed application, this
/// context could be the main window, a new window, or the currently active
/// window.
/// </para>
/// <para>
/// At any moment, the tree of view models loaded in the application represents
/// the current <see cref="IRouterState">router state</see>, and the URL is
/// nothing else than the serialized form of the router state. For instance,
/// when the application starts and its main window displays the 'Home' view
/// model, the application state corresponds to a specific router state. In this
/// state, the <see cref="IRouterContext" /> targets the main window, and the
/// 'Home' view is loaded as content inside an 'outlet' in that window.
/// <para>
/// </para>
/// </para>
/// <para>
/// The primary role of the Router is to facilitate the construction of the view
/// model tree in the application and enable navigation among them. It achieves
/// this by allowing the application developer to specify, via a
/// <see cref="IRoutes">configuration object</see>, which view models to display
/// for a given URL. For example, the routes configuration for a simple
/// application might look like this:
/// </para>
/// <code>
/// private static Routes MakeRoutes() => new(
/// [
///     new Route
///     {
///         Path = "Home",
///         ViewModelType = typeof(HomeViewModel),
///         Children = new Routes(
///         [
///             new Route
///             {
///                 Path = "Documentation",
///                 ViewModelType = typeof(DocBrowserViewModel),
///             },
///             new Route
///             {
///                 Path = "Tutorials",
///                 ViewModelType = typeof(TutorialsViewModel),
///             },
///         ]),
///     },
/// ]);
/// </code>
/// </remarks>
public interface IRouter
{
    /// <summary>Gets the routes configuration for this router.</summary>
    /// <value>
    /// A <see cref="IRoutes" /> configuration, usually injected into the Router
    /// via the dependency injector.
    /// </value>
    IRoutes Config { get; }

    IObservable<RouterEvent> Events { get; }

    IUrlTree? GetCurrentUrlTreeForTarget(string target);

    /// <summary>Navigates to the specified URL.</summary>
    /// <param name="url">
    /// The URL to navigate to. Represents an absolute URL if it starts with
    /// <c>'/'</c>, or a relative one if not.
    /// </param>
    /// <param name="options">
    /// The navigation options. When <c>null</c>, default
    /// <see cref="NavigationOptions" /> are used.
    /// </param>
    /// <remarks>
    /// TODO(abdes): describe the details of the navigation process.
    /// </remarks>
    void Navigate(string url, NavigationOptions? options = null);

    IActiveRoute? GetCurrentStateForTarget(string target);
}
