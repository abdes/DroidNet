// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Enables a view model to receive and interact with its corresponding route during navigation.
/// </summary>
/// <remarks>
/// <para>
/// When implementing this interface, view models gain access to their active route, providing
/// crucial navigation context such as URL parameters and segment information. The router
/// automatically injects the route during activation, enabling view models to respond to navigation
/// state changes and access routing data.
/// </para>
///
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
///         set
///         {
///             activeRoute = value;
///             if (value?.Params.TryGetValue("id", out var userId) == true)
///             {
///                 LoadUserDetails(userId);
///             }
///         }
///     }
/// }
/// ]]></code>
/// </example>
///
/// <para><strong>Implementation Guidelines</strong></para>
/// <para>
/// View models implementing this interface participate in the routing lifecycle and can access URL
/// parameters and query strings through <see cref="IActiveRoute.Params"/>, parent and child routes
/// in the navigation hierarchy, and the outlet where their view is rendered. The <see cref="ActiveRoute"/>
/// property setter may be called multiple times during the view model's lifetime as navigation occurs.
/// Implementations should handle <see langword="null"/> values gracefully, as the route reference
/// may be cleared during deactivation.
/// </para>
/// <para>
/// The <see cref="ActiveRoute"/> property setter may be called multiple times during the view
/// model's lifetime as navigation occurs. Implementations should handle <see langword="null"/>
/// values gracefully, as the route reference may be cleared during deactivation.
/// </para>
/// </remarks>
public interface IRoutingAware
{
    /// <summary>
    /// Gets or sets the active route associated with this view model.
    /// </summary>
    /// <remarks>
    /// The router injects this property during route activation. View models can use it to access
    /// navigation parameters, query the route hierarchy, or participate in navigation state changes.
    /// The value may be <see langword="null"/> when the view model is not currently active in
    /// the routing system.
    /// </remarks>
    IActiveRoute? ActiveRoute { get; set; }
}
