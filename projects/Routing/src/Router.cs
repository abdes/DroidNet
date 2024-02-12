// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics;
using System.Reactive.Linq;
using DroidNet.Routing.Events;
using static DroidNet.Routing.Utils.RelativeUrlTreeResolver;

/// <inheritdoc />
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
        IRoutes config,
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

    /// <inheritdoc />
    public IRoutes Config { get; }

    public IObservable<RouterEvent> Events { get; }

    /// <inheritdoc />
    public void Navigate(string url, NavigationOptions? options = null) =>
        this.Navigate(this.urlSerializer.Parse(url), options);

    public IUrlTree? GetCurrentUrlTreeForTarget(string target)
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

    private void Navigate(IUrlTree urlTree, NavigationOptions? options = null)
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
