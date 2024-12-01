// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Enables a view model to receive and interact with its corresponding route during navigation.
/// </summary>
/// <remarks>
/// When implementing this interface, view models gain access to their active route, providing
/// crucial navigation context such as URL parameters and segment information. The router
/// automatically injects the route during activation, enabling view models to respond to navigation
/// state changes and access routing data.
/// <para>
/// The <see cref="OnNavigatedToAsync"/> method is called during the view model's activation, providing
/// an opportunity to preload data or perform other initialization tasks. View models can access URL
/// parameters and query strings through <see cref="IActiveRoute.Params"/>, parent and child routes
/// in the navigation hierarchy, and the outlet where their view is rendered.
/// </para>
/// </remarks>
/// <example>
/// <strong>Example Usage</strong>
/// <code><![CDATA[
/// public class UserDetailsViewModel : IRoutingAware
/// {
///     private IActiveRoute? activeRoute;
///
///     public IActiveRoute? ActiveRoute
///     {
///         get => activeRoute;
///         set => activeRoute = value;
///     }
///
///     public async Task OnActivatingAsync(IActiveRoute route)
///     {
///         ActiveRoute = route;
///         if (route.Params.TryGetValue("id", out var userId))
///         {
///             await LoadUserDetailsAsync(userId);
///         }
///     }
///
///     private async Task LoadUserDetailsAsync(string userId)
///     {
///         // Load user details asynchronously
///     }
/// }
/// ]]></code>
/// </example>
public interface IRoutingAware
{
    /// <summary>
    /// Called by the router when the component, which implements <see cref="IRoutingAware"/>, is being activated.
    /// </summary>
    /// <remarks>
    /// The router calls this method during route activation, just after the ViewModel is resolved.
    /// View models can use it to access navigation parameters, query the route hierarchy, preload
    /// data, or participate in navigation state changes.
    /// </remarks>
    /// <param name="route">The active route associated with this view model.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    Task OnNavigatedToAsync(IActiveRoute route);
}
