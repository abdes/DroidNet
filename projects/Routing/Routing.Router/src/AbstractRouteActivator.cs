// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Routing;

/// <summary>
/// Provides an abstract base class for route activators that handle the activation of routes within the routing system.
/// </summary>
/// <param name="loggerFactory">
/// Optional factory for creating loggers. If provided, enables detailed logging of the recognition
/// process. If <see langword="null"/>, logging is disabled.
/// </param>
/// <remarks>
/// The <see cref="AbstractRouteActivator"/> class serves as a base for implementing route
/// activators that manage the lifecycle of routes. It provides common functionality for activating
/// routes, including resolving view models, handling route parameters, and managing the state of
/// the route.
/// <para>
/// <strong>Note:</strong> This class focuses on the contract, leaving the specific implementation
/// details to derived classes. Implementors should override <see cref="DoActivateRoute"/> to define
/// custom activation behavior.
/// </para>
/// </remarks>
public abstract partial class AbstractRouteActivator(ILoggerFactory? loggerFactory = null)
    : IRouteActivator
{
    /// <summary>
    /// Gets the logger used by the activator.
    /// </summary>
    [SuppressMessage("ReSharper", "MemberCanBePrivate.Global", Justification = "may be used by derived classes")]
    protected ILogger Logger { get; } = loggerFactory?.CreateLogger<IRouteActivator>() ??
                                        NullLoggerFactory.Instance.CreateLogger<IRouteActivator>();

    /// <inheritdoc/>
    public bool ActivateRoutesRecursive(IActiveRoute root, INavigationContext context)
    {
        // Activate the root node
        var activationSuccessful = this.ActivateRoute(root, context);

        return root.Children.Aggregate(
            activationSuccessful,
            (currentStatus, route) => this.ActivateRoutesRecursive(route, context) && currentStatus);
    }

    /// <summary>
    /// Activates a single route.
    /// </summary>
    /// <param name="route">The <see cref="IActiveRoute"/> to activate.</param>
    /// <param name="context">The navigation context.</param>
    /// <returns>
    /// <see langword="true"/> if the route was activated successfully; otherwise, <see langword="false"/>.
    /// </returns>
    /// <remarks>
    /// This method attempts to activate the specified route by executing custom activation logic
    /// defined in <see cref="DoActivateRoute"/>. It handles any exceptions that may occur during
    /// activation, logging them appropriately and returning <see langword="false"/> if an error
    /// occurs. Before activation, it checks with the <see cref="IRouteActivationObserver"/> (if
    /// available) to determine whether activation should proceed.
    /// </remarks>
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "activation failures cannot be fixed, all failures are returned as false")]
    public bool ActivateRoute(IActiveRoute route, INavigationContext context)
    {
        Debug.Assert(context is NavigationContext, "expecting the context to use my implementation as a base");
        var activationObserver = ((NavigationContext)context).RouteActivationObserver;

        try
        {
            // Check if observer is available; proceed if OnActivating returns true or observer is null.
            if (activationObserver?.OnActivating(route, context) != false)
            {
                LogRouteActivating(this.Logger, route);

                this.DoActivateRoute(route, context);

                // Notify observer post-activation if it exists.
                activationObserver?.OnActivated(route, context);

                LogRouteActivated(this.Logger, route);
                return true;
            }

            LogActivationSkipped(this.Logger, route);
            return true;
        }
        catch (Exception ex)
        {
            LogActivationFailed(this.Logger, route, ex);
            return false;
        }
    }

    /// <summary>
    /// Executes the actual activation logic for the specified route.
    /// </summary>
    /// <param name="route">The route to activate.</param>
    /// <param name="context">The navigation context.</param>
    /// <remarks>
    /// Derived classes must implement this method to define the specific activation behavior for
    /// routes. This may include initializing view models, setting up data bindings, or any other
    /// necessary setup for the route.
    /// <para><strong>Implementation Guidelines</strong></para>
    /// <para>
    /// Ensure that this method handles any exceptions internally and does not allow them to
    /// propagate, any propagated exception will be absorbed.
    /// </para>
    /// </remarks>
    protected abstract void DoActivateRoute(IActiveRoute route, INavigationContext context);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Route activation failed `{Route}`")]
    private static partial void LogActivationFailed(ILogger logger, IActiveRoute route, Exception ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Activation not needed because the route `{Route}` is already activated")]
    private static partial void LogActivationSkipped(ILogger logger, IActiveRoute route);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Route `{Route}` activating...")]
    private static partial void LogRouteActivating(ILogger logger, IActiveRoute route);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Route `{Route}` activated")]
    private static partial void LogRouteActivated(ILogger logger, IActiveRoute route);
}
