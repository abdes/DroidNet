// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing.Events;

namespace DroidNet.Routing;

/// <summary>
/// The central building block of router-based navigation in an MVVM application.
/// </summary>
/// <remarks>
/// <para>
/// In an MVVM application, the router orchestrates navigation between view models, maintaining a
/// tree structure that reflects the current application state. Each view model in this tree is
/// rendered through its corresponding view within a specific navigation context - typically a
/// window in a desktop environment. While most navigation occurs within the same window, the router
/// also supports scenarios requiring navigation across different windows, such as opening documents
/// in separate views.
/// </para>
/// <para>
/// The router maintains a bidirectional mapping between URLs and application state. At any moment,
/// the current tree of view models can be serialized into a URL, and conversely, a URL can be
/// parsed to reconstruct a specific application state. For example, when the application displays
/// its home screen, this corresponds to a particular router state where the 'Home' view model is
/// loaded into the main window's primary outlet.
/// </para>
/// <para>
/// Application developers define navigation structure through a route configuration that maps URLs
/// to view models. For example, a simple application might define its routes as:
/// </para>
/// <code><![CDATA[
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
/// ]]></code>
/// <para>
/// With this configuration, the URL "/Home/Documentation" would activate a <c>DocBrowserViewModel</c>
/// as a child of <c>HomeViewModel</c>. The router handles all aspects of this activation, from URL
/// parsing to view model creation and view loading.
/// </para>
/// </remarks>
public interface IRouter
{
    /// <summary>
    /// Gets an observable sequence of events that occur during navigation.
    /// </summary>
    /// <remarks>
    /// The event stream provides real-time insights into the navigation lifecycle, including start
    /// and completion of navigation, route recognition, context changes, and error conditions.
    /// Subscribers can use these events to coordinate UI updates, handle navigation failures,
    /// or maintain navigation history.
    /// </remarks>
    IObservable<RouterEvent> Events { get; }

    /// <summary>
    /// Gets the route configuration that defines the application's navigation structure.
    /// </summary>
    IRoutes Config { get; }

    /// <summary>
    /// Initiates a full navigation to the specified URL.
    /// </summary>
    /// <param name="url">The target URL that defines the desired application state.</param>
    /// <param name="options">Optional settings that control navigation behavior.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    /// <remarks>
    /// A full navigation reconstructs the entire view model tree based on the provided URL. The
    /// router parses the URL, matches it against the route configuration, and activates the
    /// corresponding view models in their designated navigation contexts. If navigation fails at
    /// any point, the router emits appropriate error events while maintaining the application in a
    /// consistent state.
    /// </remarks>
    Task NavigateAsync(string url, FullNavigation? options = null);

    /// <summary>
    /// Performs a partial navigation that surgically updates the current application state.
    /// </summary>
    /// <param name="url">A relative URL that describes the desired state changes.</param>
    /// <param name="options">Settings that specify the scope and behavior of the partial update.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    /// <remarks>
    /// Partial navigation provides fine-grained control over the view model tree, allowing updates
    /// to specific branches while preserving the rest of the application state. This is
    /// particularly useful for auxiliary content, dialog boxes, or panel updates that don't warrant
    /// a full navigation cycle. The navigation context from the options determines the scope of the
    /// update.
    /// </remarks>
    Task NavigateAsync(string url, PartialNavigation options);

    /// <summary>
    /// Request a partial navigation using the specified changes, which need to
    /// be applied to the current router state.
    /// </summary>
    /// <param name="changes">
    /// The list of changes to be applied to the router state. Each change specifies an action
    /// (add, update, or delete) to be performed on a specific route, identified by its outlet name.
    /// </param>
    /// <param name="options">
    /// The navigation options, which must contain at least the relative route at which the changes
    /// are to be applied. The options provide context for resolving relative URLs and determining
    /// where state changes should be applied.
    /// </param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    /// <remarks>
    /// This method allows for fine-grained control over the router state by applying specific
    /// changes to the current navigation context. It is particularly useful for dynamic UI updates,
    /// such as adding, updating, or removing panels in a workspace layout without performing a full
    /// URL-based navigation.
    /// </remarks>
    ///
    /// <example>
    /// <strong>Example Usage</strong>
    /// <code><![CDATA[
    /// var changes = new List<RouteChangeItem>
    /// {
    ///     new RouteChangeItem
    ///     {
    ///         ChangeAction = RouteChangeAction.Add,
    ///         Outlet = new OutletName("details"),
    ///         ViewModelType = typeof(DetailsViewModel),
    ///         Parameters = new Parameters { ["id"] = "123" }
    ///     },
    ///     new RouteChangeItem
    ///     {
    ///         ChangeAction = RouteChangeAction.Update,
    ///         Outlet = new OutletName("sidebar"),
    ///         Parameters = new Parameters { ["width"] = "300" }
    ///     },
    ///     new RouteChangeItem
    ///     {
    ///         ChangeAction = RouteChangeAction.Delete,
    ///         Outlet = new OutletName("footer")
    ///     }
    /// };
    ///
    /// var options = new PartialNavigation
    /// {
    ///     RelativeTo = currentActiveRoute,
    ///     AdditionalInfo = new AdditionalInfo("Some additional info")
    /// };
    ///
    /// router.Navigate(changes, options);
    /// ]]></code>
    /// </example>
    Task NavigateAsync(IList<RouteChangeItem> changes, PartialNavigation options);
}
