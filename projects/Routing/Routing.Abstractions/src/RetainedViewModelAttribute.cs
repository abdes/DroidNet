// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Marks a view model that outlives the routes that show it, and that its owner, not routing, disposes.
/// </summary>
/// <remarks>
/// Routing disposes the view models it shows when they go away: when a change-set navigation deletes their route,
/// when their navigation context is destroyed, or when an outlet shows another view model in their place. A view
/// model that its container keeps, such as a panel whose state must survive the panel being closed and shown again,
/// carries this attribute; routing then only lets go of it, and the next route to show it gets the same instance.
/// <see cref="RoutedViewModels.Release"/> applies the rule.
/// </remarks>
[AttributeUsage(AttributeTargets.Class, Inherited = true, AllowMultiple = false)]
public sealed class RetainedViewModelAttribute : Attribute;
