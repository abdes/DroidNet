// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Routing.Detail;
using DroidNet.Routing.Events;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using static DroidNet.Routing.Utils.RelativeUrlTreeResolver;

namespace DroidNet.Routing;

/// <inheritdoc cref="IRouter" />
public sealed partial class Router : IRouter, IDisposable
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
    private readonly Recognizer recognizer;

    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="Router" /> class.
    /// </summary>
    /// <param name="container">The IoC container used to resolve ViewModel instances.</param>
    /// <param name="config">The routes configuration to use.</param>
    /// <param name="states">The router state manager to use.</param>
    /// <param name="contextManager">The router context manager.</param>
    /// <param name="routeActivator">The route activator to use for creating contexts and activating routes.</param>
    /// <param name="contextProvider">The <see cref="IContextProvider" /> to use for getting contexts.</param>
    /// <param name="urlSerializer">The URL serializer to use for converting between url strings and<see cref="UrlTree" />.</param>
    /// <param name="loggerFactory">
    /// Optional factory for creating loggers. If provided, enables detailed logging of the recognition
    /// process. If <see langword="null"/>, logging is disabled.
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
        this.recognizer = new Recognizer(urlSerializer, config, loggerFactory);
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

        _ = this.Events.Subscribe(e => this.LogRouterEvent(e));
        this.contextProvider.ContextDestroyed += this.OnContextDestroyed;
    }

    /// <inheritdoc />
    public IRoutes Config { get; }

    /// <inheritdoc/>
    public IObservable<RouterEvent> Events { get; }

    /// <inheritdoc />
    public Task NavigateAsync(string url, FullNavigation? options = null)
        => this.NavigateAsync(this.urlSerializer.Parse(url), options ?? new FullNavigation());

    /// <inheritdoc />
    /// TODO: implement partial navigation with a URL
    public Task NavigateAsync(string url, PartialNavigation options)
        => this.NavigateAsync(this.urlSerializer.Parse(url), options);

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "All unhandled exceptions will be propagated via the router NavigationFailed event")]
    public async Task NavigateAsync(IList<RouteChangeItem> changes, PartialNavigation options)
    {
        try
        {
            var activeRoute = options.RelativeTo as ActiveRoute ?? throw new ArgumentException(
                "cannot apply navigation changes without a valid active route",
                nameof(options));

            var urlTree = activeRoute.SegmentGroup as UrlSegmentGroup;
            Debug.Assert(
                urlTree is not null,
                $"was expecting the active route to have a valid url tree, but it has `{activeRoute.SegmentGroup}` of type `{activeRoute.SegmentGroup.GetType()}`");

            var currentContext = this.contextManager.GetContextForTarget(Target.Self);
            Debug.Assert(currentContext.State is not null, "the current context must have a valid router state");
            var currentState = currentContext.State;

            ApplyChangesToUrlTree(changes, urlTree, activeRoute);
            var url = currentState.UrlTree.ToString() ??
                      throw new NavigationFailedException(
                          "failed to serialize the modified URL tree into a URL string");
            ((RouterState)currentState).Url = url;

            this.eventSource.OnNext(new NavigationStart(url, options));

            ApplyChangesToRouterState(changes, activeRoute);

            this.eventSource.OnNext(new RoutesRecognized(currentState.UrlTree));

            this.eventSource.OnNext(new ActivationStarted(options, currentContext));

            // TODO: Activate only the new routes
            var success = await this.routeActivator.ActivateRoutesRecursiveAsync(activeRoute.Root, currentContext).ConfigureAwait(true);
            this.contextProvider.ActivateContext(currentContext);

            this.eventSource.OnNext(new ActivationComplete(options, currentContext));

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
            this.LogNavigationFailed(ex);
            this.eventSource.OnNext(new NavigationError(options));
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.contextProvider.ContextDestroyed -= this.OnContextDestroyed;
        this.eventSource.Dispose();
        this.isDisposed = true;
    }

    /// <summary>
    /// Applies a list of route change items to the current router state.
    /// </summary>
    /// <param name="changes">The list of changes to be applied to the router state.</param>
    /// <param name="activeRoute">The active route to which the changes will be applied.</param>
    /// <exception cref="ArgumentOutOfRangeException">Thrown when an unexpected change action is encountered.</exception>
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

                case RouteChangeAction.None:
                default:
                    throw new ArgumentOutOfRangeException(
                        $"unexpected change action `{change.ChangeAction}`",
                        nameof(change));
            }
        }
    }

    /// <summary>
    /// Applies the specified update change to the given active route.
    /// </summary>
    /// <param name="activeRoute">The active route to which the change will be applied.</param>
    /// <param name="change">The change item containing the update details.</param>
    /// <remarks>
    /// This method updates the parameters of an existing route identified by the outlet name in the change item.
    /// If the parameters in the change item are null, the method returns without making any changes.
    /// </remarks>
    private static void ApplyUpdateChange(ActiveRoute activeRoute, RouteChangeItem change)
    {
        if (change.Parameters is null)
        {
            return;
        }

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

    /// <summary>
    /// Applies the specified delete change to the given active route.
    /// </summary>
    /// <param name="activeRoute">The active route from which the child route will be removed.</param>
    /// <param name="change">The change item containing the outlet name of the route to be deleted.</param>
    /// <remarks>
    /// This method locates the child route within the active route's children collection that
    /// matches the outlet name specified in the change item. If the child route is found, it is
    /// removed from the active route's children.
    /// </remarks>
    private static void ApplyDeleteChange(ActiveRoute activeRoute, RouteChangeItem change)
    {
        var route = activeRoute.Children.FirstOrDefault(
            r => r.Outlet.Name.Equals(change.Outlet, StringComparison.Ordinal));
        Debug.Assert(
            route is not null,
            $"expecting to find an active route with the outlet name `{change.Outlet}`");
        _ = activeRoute.RemoveChild(route);
    }

    /// <summary>
    /// Applies the specified add change to the given active route.
    /// </summary>
    /// <param name="activeRoute">The active route to which the change will be applied.</param>
    /// <param name="change">The change item containing the add details.</param>
    /// <remarks>
    /// This method adds a new child route to the active route based on the view model type and the
    /// active route configuration. It computes the path for the new route, creates a new URL
    /// segment group, and adds the new route as a child of the active route.
    /// </remarks>
    private static void ApplyAddChange(ActiveRoute activeRoute, RouteChangeItem change)
    {
        // Compute the path based on the view model type and the active route config
        Debug.Assert(change.ViewModelType != null, "Expecting ViewModelType not to be null");
        var config = MatchViewModelType(activeRoute.Config, change);

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
                Config = config,
                Params = change.Parameters,
                Outlet = change.Outlet,
                QueryParams = activeRoute.QueryParams,
                SegmentGroup = urlSegmentGroup,
                Segments = urlSegmentGroup.Segments,
            });
    }

    /// <summary>
    /// Applies a list of route change items to the URL tree, modifying its structure based on the specified changes.
    /// </summary>
    /// <param name="changes">The list of changes to be applied to the URL tree.</param>
    /// <param name="urlTree">The URL segment group representing the current URL tree.</param>
    /// <param name="activeRoute">The active route to which the changes will be applied.</param>
    /// <exception cref="ArgumentOutOfRangeException">Thrown when an unexpected change action is encountered.</exception>
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
                    Debug.Assert(change.ViewModelType != null, "Expecting ViewModelType not to be null");
                    var config = MatchViewModelType(activeRoute.Config, change);

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
                case RouteChangeAction.None:
                default:
                    throw new ArgumentOutOfRangeException(
                        $"unexpected change action `{change.ChangeAction}`",
                        nameof(change));
            }
        }
    }

    /// <summary>
    /// Matches the specified view model type to a route configuration.
    /// </summary>
    /// <param name="config">The route configuration to search within.</param>
    /// <param name="change">The route change item containing the view model type to match.</param>
    /// <returns>The matched route configuration.</returns>
    /// <exception cref="ArgumentNullException">Thrown when the view model type in the change item is null.</exception>
    /// <exception cref="InvalidOperationException">Thrown when no matching route configuration is found for the specified view model type.</exception>
    private static IRoute MatchViewModelType(IRoute config, RouteChangeItem change)
    {
        Debug.Assert(change.ViewModelType != null, "changemust have a valid ViewModel");

        // The dock is under workspace under root
        var workspaceConfig = config.Children[0].Children[0];

        var childConfig
            = workspaceConfig.Children?.FirstOrDefault(configChild => configChild.ViewModelType == change.ViewModelType);

        Debug.Assert(
            childConfig is not null,
            $"No route config under `{config}` has a view model type `{change.ViewModelType}`");

        return childConfig;
    }

    /// <summary>
    /// Navigates to the specified URL tree with the given navigation options.
    /// </summary>
    /// <param name="urlTree">The URL tree representing the target navigation path.</param>
    /// <param name="options">The navigation options that control the behavior of the navigation.</param>
    /// <remarks>
    /// This method initiates the navigation process by emitting a <see cref="NavigationStart"/> event,
    /// obtaining the appropriate navigation context, resolving the URL tree if the navigation is relative,
    /// and recognizing the URL tree to build the router state. It then optimizes route activation,
    /// activates the routes recursively, and finally activates the context. Throughout the process,
    /// various events are emitted to signal the progress and outcome of the navigation.
    /// </remarks>
    /// <exception cref="NavigationFailedException">
    /// Thrown when the URL tree cannot be recognized or matched to the router configuration.
    /// </exception>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "All unhandled exceptions will be wrapped inside a NavigationFailedException")]
    private async Task NavigateAsync(IUrlTree urlTree, NavigationOptions options)
    {
        this.eventSource.OnNext(new NavigationStart(urlTree.ToString(), options));

        try
        {
            // Get a context for the target.
            var context = this.contextManager.GetContextForTarget(options.Target);

            // Save the previous state for route optimization
            var previousState = context.State;

            // If the navigation is relative, resolve the url tree into an
            // absolute one.
            if (options.RelativeTo is not null)
            {
                urlTree = ResolveUrlTreeRelativeTo(urlTree, options.RelativeTo);
            }

            // Parse the url tree into a router state.
            context.State = this.recognizer.Recognize(urlTree) ??
                            throw new NavigationFailedException("failed to match the parsed URL to the router config");

            this.eventSource.OnNext(new RoutesRecognized(urlTree));

            // Optimize route activation by reusing previously activated routes, but only do it if
            // we are navigating within the same target).
            if (options.Target is null || options.Target == context.NavigationTargetKey)
            {
                this.OptimizeRouteActivation(context, previousState);
            }

            this.eventSource.OnNext(new ActivationStarted(options, context));

            // Activate routes in the router state that still need activation after the optimization.
            context.RouteActivationObserver = this.routeActivationObserver;
            var success = await this.routeActivator.ActivateRoutesRecursiveAsync(context.State.RootNode, context).ConfigureAwait(true);

            // Finally activate the context.
            this.contextProvider.ActivateContext(context);

            this.eventSource.OnNext(new ActivationComplete(options, context));

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
            this.LogNavigationFailed(ex);
            this.eventSource.OnNext(new NavigationError(options));
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Best effort cleanup, so catch all exceptions")]
    private void OnContextDestroyed(object? sender, ContextEventArgs args)
    {
        _ = sender; // Unused
        if (args.Context is not { } context)
        {
            return;
        }

        if (context.State is null)
        {
            return;
        }

        var targetName = context.NavigationTargetKey.Name;
        this.LogContextCleanupStarted(targetName);

        try
        {
            DisposeRouteTree(context.State.RootNode);
            this.LogContextCleanupCompleted(targetName);
        }
        catch (Exception ex)
        {
            this.LogContextCleanupFailed(targetName, ex);
        }
        finally
        {
            if (context is NavigationContext navigationContext)
            {
                navigationContext.State = null;
                navigationContext.RouteActivationObserver = null;
            }
        }

        void DisposeRouteTree(IActiveRoute route)
        {
            foreach (var child in route.Children)
            {
                DisposeRouteTree(child);
            }

            if (route is not ActiveRoute activeRoute)
            {
                return;
            }

            if (activeRoute.ViewModel is IDisposable disposable)
            {
                try
                {
                    disposable.Dispose();
                }
                catch (Exception ex)
                {
                    this.LogViewModelDisposeFailed(activeRoute, ex);
                }
            }

            activeRoute.ViewModel = null;
            activeRoute.IsActivated = false;
        }
    }
}
