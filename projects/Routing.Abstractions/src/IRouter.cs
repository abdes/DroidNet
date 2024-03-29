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

    /// <summary>
    /// Request a full navigation to the specified absolute URL.
    /// </summary>
    /// <param name="url">The URL to navigate to.</param>
    /// <param name="options">
    /// The navigation options. When <see langword="null" />, <see cref="NavigationOptions">defaults</see> are used.
    /// </param>
    /// <remarks>
    /// TODO(abdes): describe the details of the navigation process.
    /// </remarks>
    void Navigate(string url, FullNavigation? options = null);

    /// <summary>
    /// Request a partial navigation to the specified relative URL.
    /// </summary>
    /// <param name="url">The relative URL to navigate to.</param>
    /// <param name="options">The navigation options, which must contain at
    /// least the relative route from which navigation will start.</param>
    void Navigate(string url, PartialNavigation options);

    /// <summary>
    /// Request a partial navigation using the specified changes, which need to
    /// be applied to the current router state.
    /// </summary>
    /// <param name="changes">The changes to be applied to the router
    /// state.</param>
    /// <param name="options">The navigation options, which must contain least
    /// the relative route, at which the changes are to be applied.</param>
    void Navigate(IList<RouteChangeItem> changes, PartialNavigation options);
}
