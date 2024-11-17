// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Demo.Navigation;

/// <summary>
/// Represents the view model for the settings page.
/// </summary>
/// <remarks>
/// Implements the <see cref="IRoutingAware"/> interface to interact with the routing system.
/// </remarks>
[System.Diagnostics.CodeAnalysis.SuppressMessage(
    "Maintainability",
    "CA1515:Consider making public types internal",
    Justification = "ViewModel classes must be public because the ViewModel property in the generated code for the view is public")]
public class SettingsViewModel : IRoutingAware
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
    public IActiveRoute? ActiveRoute { get; set; }
}
