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
/// An MVVM application is typically a tree of view models. Each view model is
/// rendered via corresponding views. Assuming we are in a windowed desktop
/// environment, the views are displayed in windows. While many applications
/// would just have a single main window, more complex applications open and
/// manage multiple windows simultaneously. Navigation in the application
/// generally happens within the same window, but sometimes it is necessary to
/// navigate into a different window (for example, opening a document in a
/// dedicated separate window).
/// <para>
/// The router thinks of navigation only between view models, and within a
/// rendering context specifying the target where the corresponding view should
/// be rendered. That context in a windowed application could be the main
/// window, a new window or the currently active window.
/// </para>
/// <para>
/// Depending on the current application states, each window (represented by a
/// <see cref="RouterContext" />), contains a tree of views, corresponding to a
/// tree of view models. A particular tree of view models is internally
/// represented in the router as a <see cref="RouterState" />. For example,
/// when application starts and its main window displays the view for a 'Home'
/// view model, that application states corresponds to a particular router
/// state, with its <see cref="RouterContext" /> having the main window as a
/// target, and the 'Home' view being loaded as content inside an 'outlet' in
/// that window.
/// </para>
/// <para>
/// The focus of the Router is to enable the construction of the view model
/// tree in the application and the navigation among them. It accomplishes this
/// by letting the application developer specify, in a configuration object,
/// which view models to display for a given URL. That configuration object is
/// the <see cref="Routes" /> object passed to the router. For example, the
/// routes configuration for a simple application might look like this:
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
/// <para>
/// At any time, a particular arrangement of view models, being displayed on
/// screen to the user, based on the current navigation URL, is a router state,
/// and the URL is nothing else than the serialized form of that state. This
/// arrangement is also known as the active route, implemented in the
/// <see cref="RouterState" /> as a tree of <see cref="ActiveRoute" />s.
/// </para>
/// </remarks>
public class Router
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

    /// <summary>Gets the routes configuration for this router.</summary>
    /// <value>
    /// A <see cref="Routes" /> configuration, usually injected into the Router
    /// via the dependency injector.
    /// </value>
    public Routes Config { get; }

    public IObservable<RouterEvent> Events { get; }

    public UrlTree? GetCurrentUrlTreeForTarget(string target)
        => this.contextManager.GetContextForTarget(target).State?.UrlTree;

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
    /// <seealso cref="Navigate(UrlTree,NavigationOptions?)" />
    public void Navigate(string url, NavigationOptions? options = null) =>
        this.Navigate(this.urlSerializer.Parse(url), options);

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
    internal static void OptimizeRouteActivation(RouterContext context) =>

        // TODO(abdes) implement OptimizeRouteActivation
        _ = context;

    /// <summary>Navigate using the specified URL tree.</summary>
    /// <param name="urlTree">The URL tree to use to navigate.</param>
    /// <param name="options">
    /// The navigation options. When <c>null</c>, default
    /// <see cref="NavigationOptions" /> are used.
    /// </param>
    /// <seealso cref="Navigate(string,NavigationOptions?)" />
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

    public static class Outlet
    {
        public const string Primary = "";
    }
}

/// <summary>
/// Represents options used when requesting the <see cref="Router" /> to
/// navigate.
/// </summary>
public record NavigationOptions
{
    /// <summary>Gets the target of the navigation.</summary>
    /// <value>
    /// A string identifying the navigation target. Can be a special
    /// <see cref="RouterContext" />target or a custom one. In both cases,
    /// the value should be a key to an appropriate object registered with the
    /// dependency injector.
    /// </value>
    public string? Target { get; init; }

    /// <summary>
    /// Gets the <see cref="IActiveRoute" />, relative to which the navigation
    /// will happen.
    /// </summary>
    /// <value>
    /// When not <c>null</c>, it contains the <see cref="ActiveRoute" />
    /// relative to which the url tree for navigation will be resolved before
    /// navigation starts.
    /// </value>
    public IActiveRoute? RelativeTo { get; init; }
}
