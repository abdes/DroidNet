// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System;
using System.Diagnostics;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Routing.Detail;
using DroidNet.Routing.Events;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using static DroidNet.Routing.Utils.RelativeUrlTreeResolver;

/// <inheritdoc cref="IRouter" />
public partial class Router : IRouter, IDisposable
{
    private readonly ILogger logger;

    // TODO(abdes): implement router state manager, with history
    private readonly IRouterStateManager states;
    private readonly RouterContextManager contextManager;
    private readonly IRouteActivator routeActivator;
    private readonly IContextProvider contextProvider;
    private readonly IUrlSerializer urlSerializer;

    private readonly BehaviorSubject<RouterEvent> eventSource = new(new RouterReady());
    private readonly IRouteActivationObserver routeActivationObserver;

    /// <summary>
    /// Initializes a new instance of the <see cref="Router" /> class.
    /// </summary>
    /// <param name="container">
    /// The IoC container used to resolve ViewModel instances for the routes being activated.
    /// </param>
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
    /// <param name="loggerFactory">
    /// We inject a <see cref="ILoggerFactory" /> to be able to silently use a
    /// <see cref="NullLogger" /> if we fail to obtain a <see cref="ILogger" />
    /// from the Dependency Injector.
    /// </param>
    public Router(
        IContainer container,
        IRoutes config,
        IRouterStateManager states,
        RouterContextManager contextManager,
        IRouteActivator routeActivator,
        IContextProvider contextProvider,
        IUrlSerializer urlSerializer,
        ILoggerFactory? loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<Router>() ?? NullLoggerFactory.Instance.CreateLogger<Router>();

        this.routeActivationObserver = new RouteActivationObserver(container);

        this.states = states;
        this.contextManager = contextManager;
        this.routeActivator = routeActivator;
        this.contextProvider = contextProvider;
        this.urlSerializer = urlSerializer;
        this.Config = config;

        this.Events = Observable.Merge(
            Observable.FromEventPattern<EventHandler<ContextEventArgs>, ContextEventArgs>(
                    h => this.contextProvider.ContextCreated += h,
                    h => this.contextProvider.ContextCreated -= h)
                .Select(e => new ContextCreated(e.EventArgs.Context!)),
            Observable.FromEventPattern<EventHandler<ContextEventArgs>, ContextEventArgs>(
                    h => this.contextProvider.ContextDestroyed += h,
                    h => this.contextProvider.ContextDestroyed -= h)
                .Select(e => new ContextDestroyed(e.EventArgs.Context!)),
            Observable.FromEventPattern<EventHandler<ContextEventArgs>, ContextEventArgs>(
                    h => this.contextProvider.ContextChanged += h,
                    h => this.contextProvider.ContextChanged -= h)
                .Select(e => new ContextChanged(e.EventArgs.Context)),
            this.eventSource);

        _ = this.Events.Subscribe(e => LogRouterEvent(this.logger, e));
    }

    /// <inheritdoc />
    public IRoutes Config { get; }

    public IObservable<RouterEvent> Events { get; }

    /// <inheritdoc />
    public void Navigate(string url, FullNavigation? options = null)
        => this.Navigate(this.urlSerializer.Parse(url), options ?? new FullNavigation());

    /// <inheritdoc />
    /// TODO: implement partial navigation with a URL
    public void Navigate(string url, PartialNavigation options)
        => this.Navigate(this.urlSerializer.Parse(url), options);

    /// <inheritdoc />
    public void Navigate(IList<RouteChangeItem> changes, PartialNavigation options)
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

            this.eventSource.OnNext(new NavigationStart(url, options));

            ApplyChangesToRouterState(changes, activeRoute);

            this.eventSource.OnNext(new RoutesRecognized(currentState.UrlTree));

            this.eventSource.OnNext(new ActivationStarted(options, currentState));

            // TODO: Activate only the new routes
            var success = this.routeActivator.ActivateRoutesRecursive(activeRoute.Root, currentContext);
            this.contextProvider.ActivateContext(currentContext);

            this.eventSource.OnNext(new ActivationComplete(options, currentState));

            if (success)
            {
                this.eventSource.OnNext(new NavigationEnd(currentState.Url, options));
            }
            else
            {
                this.eventSource.OnNext(new NavigationError(options));
            }
        }
        catch (Exception ex)
        {
            LogNavigationFailed(this.logger, ex);
            this.eventSource.OnNext(new NavigationError(options));
        }
    }

    public void Dispose()
    {
        this.eventSource.Dispose();
        GC.SuppressFinalize(this);
    }

    private static void ApplyChangesToRouterState(
        IList<RouteChangeItem> changes,
        ActiveRoute activeRoute)
    {
        foreach (var change in changes)
        {
            switch (change.ChangeAction)
            {
                case RouteChangeAction.Add:
                    ApplyAddChange(activeRoute, change);
                    break;

                case RouteChangeAction.Delete:
                    ApplyDeleteChange(activeRoute, change);
                    break;

                case RouteChangeAction.Update:
                    ApplyUpdateChange(activeRoute, change);
                    break;

                default:
                    throw new ArgumentOutOfRangeException(
                        $"unexpected change action `{change.ChangeAction}`",
                        nameof(change));
            }
        }
    }

    private static void ApplyUpdateChange(ActiveRoute activeRoute, RouteChangeItem change)
    {
        if (change.Parameters is null)
        {
            return;
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
    }

    private static void ApplyDeleteChange(ActiveRoute activeRoute, RouteChangeItem change)
    {
        var route = activeRoute.Children.FirstOrDefault(
            r => r.Outlet.Name.Equals(change.Outlet, StringComparison.Ordinal));
        Debug.Assert(
            route is not null,
            $"expecting to find an active route with the outlet name `{change.Outlet}`");
        _ = activeRoute.RemoveChild(route);
    }

    private static void ApplyAddChange(ActiveRoute activeRoute, RouteChangeItem change)
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
    }

    private static void ApplyChangesToUrlTree(
        IList<RouteChangeItem> changes,
        UrlSegmentGroup urlTree,
        ActiveRoute activeRoute)
    {
        foreach (var change in changes)
        {
            switch (change.ChangeAction)
            {
                case RouteChangeAction.Add:
                    // Compute the path based on the view model type and the active route config
                    Debug.Assert(change.ViewModelType != null, "change.ViewModelType != null");
                    var config = MatchViewModelType(activeRoute.RouteConfig, change);

                    var urlSegmentGroup = new UrlSegmentGroup(
                        config.Path?
                            .Split('/')
                            .Select(s => new UrlSegment(s)) ?? []);

                    urlTree.AddChild(change.Outlet, urlSegmentGroup);

                    break;

                case RouteChangeAction.Delete:
                    var removed = urlTree.RemoveChild(change.Outlet);
                    Debug.Assert(
                        removed,
                        $"expecting to find a child in the url tree with the outlet name `{change.Outlet}`");
                    break;

                case RouteChangeAction.Update:
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
                        var lastSegment = mySegmentGroup.Segments[^1] as UrlSegment;
                        Debug.Assert(lastSegment is not null, "was expecting a UrlSegment");
                        lastSegment.UpdateParameters(change.Parameters);
                    }

                    break;

                default:
                    throw new ArgumentOutOfRangeException(
                        $"unexpected change action `{change.ChangeAction}`",
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
    private static void OptimizeRouteActivation(NavigationContext context) =>

        // TODO(abdes) implement OptimizeRouteActivation
        _ = context;

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{RouterEvent}")]
    private static partial void LogRouterEvent(ILogger logger, RouterEvent routerEvent);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Navigation failed!")]
    private static partial void LogNavigationFailed(ILogger logger, Exception ex);

    private void Navigate(IUrlTree urlTree, NavigationOptions options)
    {
        this.eventSource.OnNext(new NavigationStart(urlTree.ToString(), options));

        try
        {
            // Get a context for the target.
            var context = this.contextManager.GetContextForTarget(options.Target);

            // If the navigation is relative, resolve the url tree into an
            // absolute one.
            if (options.RelativeTo is not null)
            {
                urlTree = ResolveUrlTreeRelativeTo(urlTree, options.RelativeTo);
            }

            // Parse the url tree into a router state.
            context.State = RouterState.CreateFromUrlTree(urlTree, this.Config);

            this.eventSource.OnNext(new RoutesRecognized(urlTree));

            // TODO(abdes): eventually optimize reuse of previously activated routes in the router state
            OptimizeRouteActivation(context);

            this.eventSource.OnNext(new ActivationStarted(options, context.State));

            // Activate routes in the router state that still need activation after the optimization.
            context.RouteActivationObserver = this.routeActivationObserver;
            var success = this.routeActivator.ActivateRoutesRecursive(context.State.RootNode, context);

            // Finally activate the context.
            this.contextProvider.ActivateContext(context);

            this.eventSource.OnNext(new ActivationComplete(options, context.State));

            if (success)
            {
                this.eventSource.OnNext(new NavigationEnd(context.State.Url, options));
            }
            else
            {
                this.eventSource.OnNext(new NavigationError(options));
            }
        }
        catch (Exception ex)
        {
            LogNavigationFailed(this.logger, ex);
            this.eventSource.OnNext(new NavigationError(options));
        }
    }
}
