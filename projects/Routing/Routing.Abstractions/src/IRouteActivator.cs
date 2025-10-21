// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Defines the contract for a route activator - a key component responsible for bringing routes to
/// life during navigation by creating their view models and loading their corresponding views into
/// outlets.
/// </summary>
/// <remarks>
/// When navigation occurs, the router builds a state tree from the URL and hands it to the route
/// activator. The activator orchestrates three crucial steps in the routing lifecycle:
/// <list type="number">
///   <item>Creates view model instances using dependency injection.</item>
///   <item>Injects the <see cref="IActiveRoute"/> into view models that implement <see cref="IRoutingAware"/>.</item>
///   <item>Loads the corresponding views into their designated outlets in the visual tree.</item>
/// </list>
/// </remarks>
/// <example>
/// <strong>Example Usage</strong>
/// <para>
/// Here's an example of how route activation typically flows:
/// </para>
/// <code><![CDATA[
/// // Given a route configuration:
/// {
///     Path = "users/:id",
///     ViewModelType = typeof(UserDetailsViewModel),
///     Outlet = "primary"
/// }
///
/// // And a navigation to "/users/123"
/// // The activator will:
/// 1. Resolve UserDetailsViewModel from the DI container
/// 2. If UserDetailsViewModel implements IRoutingAware, call its OnNavigatedTo method with the active route.
/// 3. Load the view into the "primary" outlet
/// ]]></code>
/// </example>
/// <para><strong>Implementation Guidelines:</strong></para>
/// <para>
/// The <see cref="IRouteActivator"/> and <see cref="IRouteActivationObserver"/> work together to
/// provide a flexible and testable route activation system. The activator handles the mechanics of
/// loading views and managing the visual tree, while the observer controls the view model lifecycle
/// and activation flow. This separation allows the complex view loading logic to remain in
/// platform-specific activator implementations, while keeping the core activation logic testable
/// and reusable.
/// </para>
/// <para>
/// During activation, the activator first consults its observer through <c>OnActivating</c>. The
/// observer can then create the view model (typically via DI), perform any necessary injections
/// (such as the <see cref="IActiveRoute"/> for <see cref="IRoutingAware"/> view models), and decide
/// whether activation should proceed. After successful view loading, the activator calls
/// <c>OnActivated</c>, allowing the observer to track activation state. A typical implementation
/// prevents duplicate activations and manages view model creation, while leaving the actual view
/// loading to platform-specific activators.
/// </para>
public interface IRouteActivator
{
    /// <summary>
    /// Activates a single route within the specified navigation context.
    /// </summary>
    /// <param name="route">The route to activate, containing view model type and outlet information.</param>
    /// <param name="context">
    /// The navigation context containing the target (e.g., window or content frame) where the view
    /// will be loaded.
    /// </param>
    /// <returns>
    /// A <see cref="Task{TResult}"/> representing the asynchronous operation. The task result is
    /// <see langword="true"/> if activation succeeded; <see langword="false"/> if any step failed.
    /// </returns>
    /// <remarks>
    /// Works in collaboration with an <see cref="IRouteActivationObserver"/> to manage the view model
    /// lifecycle and activation state. The observer gets opportunities to participate in the activation
    /// process through its lifecycle hooks, enabling view model creation via dependency injection and
    /// proper state tracking. This design allows for flexible, platform-specific view loading while
    /// maintaining a consistent activation model across different implementations.
    /// </remarks>
    public Task<bool> ActivateRouteAsync(IActiveRoute route, INavigationContext context);

    /// <summary>
    /// Recursively activates a tree of routes, starting from the specified root.
    /// </summary>
    /// <param name="root">The root route of the tree to activate.</param>
    /// <param name="context">
    /// The navigation context containing the target where views will be loaded.
    /// </param>
    /// <returns>
    /// A <see cref="Task{TResult}"/> representing the asynchronous operation. The task result is
    /// <see langword="true"/> if all routes in the tree were activated successfully;
    /// <see langword="false"/> if any activation failed.
    /// </returns>
    /// <remarks>
    /// Ensures proper activation order in hierarchical routing scenarios by activating parent routes
    /// before their children. This ordering is crucial as parent routes often provide the outlet
    /// containers needed by their children's views. The method tracks activation success across the
    /// entire tree, allowing the router to handle partial activation failures appropriately.
    /// </remarks>
    public Task<bool> ActivateRoutesRecursiveAsync(IActiveRoute root, INavigationContext context);
}
