// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Routing.Detail;
using DroidNet.Routing.Events;
using static DroidNet.Routing.Utils.RelativeUrlTreeResolver;

/// <inheritdoc cref="IRouter" />
public class Router : IRouter, IDisposable
{
    private readonly IRouterStateManager states;
    private readonly RouterContextManager contextManager;
    private readonly IRouteActivator routeActivator;
    private readonly IContextProvider contextProvider;
    private readonly IUrlSerializer urlSerializer;

    private readonly BehaviorSubject<RouterEvent> eventSource = new(new RouterReady());

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

        this.Events = Observable.Merge(
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
                .Select(e => new ContextChanged(e.EventArgs)),
            this.eventSource);

        _ = this.Events.Subscribe(e => Debug.WriteLine($"========== {e}"));
    }

    /// <inheritdoc />
    public IRoutes Config { get; }

    public IObservable<RouterEvent> Events { get; }

    /// <inheritdoc />
    public void Navigate(string url, NavigationOptions? options = null) =>
        this.Navigate(this.urlSerializer.Parse(url), options);

    public IUrlTree? GetCurrentUrlTreeForTarget(Target target)
        => this.contextManager.GetContextForTarget(target).State?.UrlTree;

    public IActiveRoute? GetCurrentStateForTarget(Target target)
        => this.contextManager.GetContextForTarget(target).State?.Root;

    public void Navigate(List<RouteChangeItem> changes, NavigationOptions options)
    {
        try
        {
            var activeRoute = options.RelativeTo as ActiveRoute ?? throw new ArgumentException(
                "cannot apply navigation changes without a valid active route",
                nameof(options));

            var urlTree = activeRoute.UrlSegmentGroup as UrlSegmentGroup;
            Debug.Assert(
                urlTree is not null,
                $"was expecting the active route to have a valid url tree, but it has `{activeRoute.UrlSegmentGroup}` of type `{activeRoute.UrlSegmentGroup.GetType()}`");

            var currentContext = this.contextManager.GetContextForTarget(Target.Self);
            var currentState = currentContext.State!;

            ApplyChangesToUrlTree(changes, urlTree, activeRoute);
            var url = currentState.UrlTree.ToString() ??
                      throw new NavigationFailedException(
                          "failed to serialize the modified URL tree into a URL string");
            ((RouterState)currentState).Url = url;

            this.eventSource.OnNext(new NavigationStart(url));
            this.eventSource.OnNext(new RoutesRecognized(currentState.UrlTree));

            ApplyChangesToRouterState(changes, urlTree, activeRoute);

            this.eventSource.OnNext(new ActivationStarted(currentState));

            // TODO: Activate only the new routes
            this.routeActivator.ActivateRoutesRecursive(activeRoute.Root, currentContext);
            this.contextProvider.ActivateContext(currentContext);

            this.eventSource.OnNext(new ActivationComplete(currentState));

            this.eventSource.OnNext(new NavigationEnd(currentState.Url));
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Navigation failed:\n{ex}");
            this.eventSource.OnNext(new NavigationError());
            throw new NavigationFailedException("an exception was thrown", ex);
        }
    }

    public void Dispose()
    {
        this.eventSource.Dispose();
        GC.SuppressFinalize(this);
    }

    private static void ApplyChangesToRouterState(
        List<RouteChangeItem> changes,
        UrlSegmentGroup urlTree,
        ActiveRoute activeRoute)
    {
        foreach (var change in changes)
        {
            switch (change.Action)
            {
                case RouteAction.Add:
                {
                    // Compute the path based on the view model type and the active route config
                    Debug.Assert(change.ViewModelType != null, "change.ViewModelType != null");
                    var config = MatchViewModelType(activeRoute.RouteConfig, change);

                    var urlSegmentGroup = new UrlSegmentGroup(
                        config.Path?
                            .Split('/')
                            .Select(s => new UrlSegment(s)) ?? []);

                    Debug.Assert(
                        change.Parameters != null,
                        "change.Parameters != null when adding a new route");
                    activeRoute.AddChild(
                        new ActiveRoute()
                        {
                            RouteConfig = config,
                            Params = change.Parameters,
                            Outlet = change.Outlet,
                            QueryParams = activeRoute.QueryParams,
                            UrlSegmentGroup = urlSegmentGroup,
                            UrlSegments = urlSegmentGroup.Segments,
                        });

                    break;
                }

                case RouteAction.Delete:
                {
                    var route = activeRoute.Children.FirstOrDefault(
                        r => r.Outlet.Name.Equals(change.Outlet, StringComparison.Ordinal));
                    Debug.Assert(
                        route is not null,
                        $"expecting to find an active route with the outlet name `{change.Outlet}`");
                    _ = activeRoute.RemoveChild(route);
                    break;
                }

                case RouteAction.Update:
                {
                    if (change.Parameters is null)
                    {
                        break;
                    }

                    Debug.Assert(
                        change.Parameters != null,
                        "change.Parameters != null when updating an existing route");
                    var route = activeRoute.Children.FirstOrDefault(
                        r => r.Outlet.Name.Equals(change.Outlet, StringComparison.Ordinal));
                    if (route is ActiveRoute myRoute)
                    {
                        myRoute.Params = change.Parameters;
                    }
                    else
                    {
                        Debug.Fail($"expecting to find an active route with the outlet name `{change.Outlet}`");
                    }

                    break;
                }

                default:
                    throw new ArgumentOutOfRangeException(
                        $"unexpected change action `{change.Action}`",
                        nameof(change));
            }
        }
    }

    private static void ApplyChangesToUrlTree(
        List<RouteChangeItem> changes,
        UrlSegmentGroup urlTree,
        ActiveRoute activeRoute)
    {
        foreach (var change in changes)
        {
            switch (change.Action)
            {
                case RouteAction.Add:
                {
                    // Compute the path based on the view model type and the active route config
                    Debug.Assert(change.ViewModelType != null, "change.ViewModelType != null");
                    var config = MatchViewModelType(activeRoute.RouteConfig, change);

                    var urlSegmentGroup = new UrlSegmentGroup(
                        config.Path?
                            .Split('/')
                            .Select(s => new UrlSegment(s)) ?? []);

                    urlTree.AddChild(change.Outlet, urlSegmentGroup);

                    break;
                }

                case RouteAction.Delete:
                {
                    var removed = urlTree.RemoveChild(change.Outlet);
                    Debug.Assert(
                        removed,
                        $"expecting to find a child in the url tree with the outlet name `{change.Outlet}`");
                    break;
                }

                case RouteAction.Update:
                {
                    if (change.Parameters is null)
                    {
                        break;
                    }

                    Debug.Assert(
                        change.Parameters != null,
                        "change.Parameters != null when updating an existing route");
                    var child = urlTree.Children.FirstOrDefault(
                        sg => sg.Key.Name.Equals(change.Outlet, StringComparison.Ordinal));
                    var mySegmentGroup = child.Value as UrlSegmentGroup;
                    Debug.Assert(
                        mySegmentGroup != null,
                        $"expecting to find an active route with the outlet name `{change.Outlet}`");

                    if (mySegmentGroup.Segments.Count != 0)
                    {
                        var lastSegment = mySegmentGroup.Segments.Last() as UrlSegment;
                        Debug.Assert(lastSegment is not null, $"was expecting a UrlSegment");
                        lastSegment.UpdateParameters(change.Parameters);
                    }

                    break;
                }

                default:
                    throw new ArgumentOutOfRangeException(
                        $"unexpected change action `{change.Action}`",
                        nameof(change));
            }
        }
    }

    private static IRoute MatchViewModelType(IRoute config, RouteChangeItem change)
    {
        Debug.Assert(change.ViewModelType != null, "change.ViewModelType != null");

        var childConfig
            = config.Children?.FirstOrDefault(configChild => configChild.ViewModelType == change.ViewModelType);

        Debug.Assert(
            childConfig is not null,
            $"No route config under `{config}` has a view model type `{change.ViewModelType}`");

        return childConfig;
    }

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
        this.eventSource.OnNext(new NavigationStart(urlTree.ToString()));

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

            this.eventSource.OnNext(new RoutesRecognized(urlTree));

            // TODO(abdes): eventually optimize reuse of previously activated routes in the router state
            OptimizeRouteActivation(context);

            this.eventSource.OnNext(new ActivationStarted(context.State));

            // Activate routes in the router state that still need activation after
            // the optimization.
            this.routeActivator.ActivateRoutesRecursive(context.State.Root, context);

            // Finally activate the context.
            this.contextProvider.ActivateContext(context);

            this.eventSource.OnNext(new ActivationComplete(context.State));

            this.eventSource.OnNext(new NavigationEnd(context.State.Url));
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Navigation failed:\n{ex}");
            this.eventSource.OnNext(new NavigationError());
            throw new NavigationFailedException("an exception was thrown", ex);
        }
    }
}
