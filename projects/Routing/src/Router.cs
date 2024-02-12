// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics;
using System.Reactive.Linq;
using DroidNet.Routing.Contracts;
using DroidNet.Routing.Events;
using static DroidNet.Routing.Utils.RelativeUrlTreeResolver;

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
/// <see cref="RouterContext">rendering context</see>, which determines where
/// the corresponding view should be rendered. In a windowed application, this
/// context could be the main window, a new window, or the currently active
/// window.
/// </para>
/// <para>
/// At any moment, the tree of view models loaded in the application represents
/// the current <see cref="RouterState">router state</see>, and the URL is
/// nothing else than the serialized form of the router state. For instance,
/// when the application starts and its main window displays the 'Home' view
/// model, the application state corresponds to a specific router state. In this
/// state, the <see cref="RouterContext" /> targets the main window, and the
/// 'Home' view is loaded as content inside an 'outlet' in that window.
/// <para>
/// </para>
/// </para>
/// <para>
/// The primary role of the Router is to facilitate the construction of the view
/// model tree in the application and enable navigation among them. It achieves
/// this by allowing the application developer to specify, via a
/// <see cref="Routes">configuration object</see>, which view models to display
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
public class Router : IRouter
{
    private readonly IRouterStateManager states;
    private readonly RouterContextManager contextManager;
    private readonly IRouteActivator routeActivator;
    private readonly IContextProvider contextProvider;
    private readonly IUrlSerializer urlSerializer;

    /// <summary>
    /// Initializes a new instance of the <see cref="Router" /> class.
    /// </summary>
    /// <param name="config">The routes configuration to use.</param>
    /// <param name="states">The router state manager to use.</param>
    /// <param name="contextManager">The router context manager.</param>
    /// <param name="routeActivator">
    /// The route activator to use for creating contexts and activating routes.
    /// </param>
    /// <param name="contextProvider">
    /// The <see cref="IContextProvider" /> to use for getting contexts for
    /// routing targets and activating them.
    /// </param>
    /// <param name="urlSerializer">
    /// The URL serializer to use for converting between url strings and
    /// <see cref="UrlTree" />.
    /// </param>
    public Router(
        Routes config,
        IRouterStateManager states,
        RouterContextManager contextManager,
        IRouteActivator routeActivator,
        IContextProvider contextProvider,
        IUrlSerializer urlSerializer)
    {
        this.states = states;
        this.contextManager = contextManager;
        this.routeActivator = routeActivator;
        this.contextProvider = contextProvider;
        this.urlSerializer = urlSerializer;
        this.Config = config;

        this.Events = Observable.Merge<RouterEvent>(
            Observable.FromEventPattern<EventHandler<RouterContext>, RouterContext>(
                    h => this.contextProvider.ContextCreated += h,
                    h => this.contextProvider.ContextCreated -= h)
                .Select(e => new ContextCreated(e.EventArgs)),
            Observable.FromEventPattern<EventHandler<RouterContext>, RouterContext>(
                    h => this.contextProvider.ContextDestroyed += h,
                    h => this.contextProvider.ContextDestroyed -= h)
                .Select(e => new ContextDestroyed(e.EventArgs)),
            Observable.FromEventPattern<EventHandler<RouterContext?>, RouterContext?>(
                    h => this.contextProvider.ContextChanged += h,
                    h => this.contextProvider.ContextChanged -= h)
                .Select(e => new ContextChanged(e.EventArgs)));

        _ = this.Events.Subscribe(e => Debug.WriteLine($"========== {e}"));
    }

    /// <summary>Gets the configuration for this router.</summary>
    /// <value>
    /// A <see cref="Routes" /> configuration, usually injected into the Router
    /// via the dependency injector.
    /// </value>
    public Routes Config { get; }

    public IObservable<RouterEvent> Events { get; }

    public void Navigate(string url, NavigationOptions? options = null) =>
        this.Navigate(this.urlSerializer.Parse(url), options);

    public UrlTree? GetCurrentUrlTreeForTarget(string target)
        => this.contextManager.GetContextForTarget(target).State?.UrlTree;

    public IActiveRoute? GetCurrentStateForTarget(string target)
        => this.contextManager.GetContextForTarget(target).State?.Root;

    /// <summary>
    /// Optimizes the activation of routes in the given context.
    /// </summary>
    /// <param name="context">The router context in which to optimize route activation.</param>
    /// <remarks>
    /// This method is intended to improve the efficiency of route activation by
    /// reusing previously activated routes in the router state.
    /// </remarks>
    private static void OptimizeRouteActivation(RouterContext context) =>

        // TODO(abdes) implement OptimizeRouteActivation
        _ = context;

    private void Navigate(UrlTree urlTree, NavigationOptions? options = null)
    {
        options ??= new NavigationOptions();
        try
        {
            // Get a context for the target.
            var context = this.contextManager.GetContextForTarget(options.Target);
            Debug.WriteLine($"Navigating to: '{urlTree}', within context: '{context}'");

            // If the navigation is relative, resolve the url tree into an
            // absolute one.
            if (options.RelativeTo is not null)
            {
                urlTree = ResolveUrlTreeRelativeTo(urlTree, options.RelativeTo);
            }

            // Parse the url tree into a router state.
            context.State = this.states.CreateFromUrlTree(urlTree);

            // TODO(abdes): eventually optimize reuse of previously activated routes in the router state
            OptimizeRouteActivation(context);

            // Activate routes in the router state that still need activation after
            // the optimization.
            this.routeActivator.ActivateRoutesRecursive(context.State.Root, context);

            // Finally activate the context.
            this.contextProvider.ActivateContext(context);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Navigation failed:\n{ex}");
            throw new NavigationFailedException(ex);
        }
    }
}
