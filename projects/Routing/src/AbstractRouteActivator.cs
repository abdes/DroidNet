// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

/// <summary>
/// Abstract implementation of the <see cref="IRouteActivator" /> that handles recursive route activation. It injects
/// the <see cref="ActivateRoute" /> method in the ViewModel of an activated route if it implements the
/// <see cref="IRoutingAware" /> interface. It skips activation for routes that are already activated.
/// </summary>
/// <param name="loggerFactory">
/// The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger cannot be obtained, a
/// <see cref="NullLogger" /> is used silently.
/// </param>
/// <remarks>
/// Extend this class and implement the <see cref="DoActivateRoute" /> method to complete the activation of child
/// routes.
/// </remarks>
public abstract partial class AbstractRouteActivator(ILoggerFactory? loggerFactory = null)
    : IRouteActivator
{
    /// <summary>Gets the logger used by this base class and its derived classes.</summary>
    /// <remarks>
    /// The logger is obtained using the provided <see cref="ILoggerFactory" />. If the factory is <see langword="null" />,
    /// a <see cref="NullLogger" /> is used instead. This ensures logging can be completely disabled by supplying a
    /// <see langword="null" /> <see cref="ILoggerFactory" />.
    /// </remarks>
    [SuppressMessage("ReSharper", "MemberCanBePrivate.Global", Justification = "may be used by derived classes")]
    protected ILogger Logger { get; } = loggerFactory?.CreateLogger<IRouteActivator>() ??
                                        NullLoggerFactory.Instance.CreateLogger<IRouteActivator>();

    /// <inheritdoc />
    public bool ActivateRoutesRecursive(IActiveRoute root, INavigationContext context)
    {
        var activationSuccessful = true;

        // The root node is a placeholder for the tree and cannot be activated.
        if (root.Parent != null)
        {
            activationSuccessful = this.ActivateRoute(root, context) && activationSuccessful;
        }

        return root.Children.Aggregate(
            activationSuccessful,
            (currentStatus, route) => this.ActivateRoutesRecursive(route, context) && currentStatus);
    }

    /// <inheritdoc />
    public bool ActivateRoute(IActiveRoute route, INavigationContext context)
    {
        var activationObserver = context.RouteActivationObserver;

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
            return false;
        }
        catch (Exception ex)
        {
            LogActivationFailed(this.Logger, route, ex);
            return false;
        }
    }

    /// <summary>Executes the actual activation logic for the specified route.</summary>
    /// <param name="route">The route to activate.</param>
    /// <param name="context">The navigation context.</param>
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
