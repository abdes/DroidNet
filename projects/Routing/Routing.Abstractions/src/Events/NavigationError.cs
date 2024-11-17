// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when navigation fails.
/// </summary>
/// <remarks>
/// Signals that the router encountered an error during navigation, such as:
/// failing to recognize routes, activate view models, or load content into outlets.
/// </remarks>
/// <param name="options">The navigation options that were used in the failed navigation attempt.</param>
public class NavigationError(NavigationOptions options) : NavigationEvent(options);
