// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Specifies the different actions that can be done in a change set for
/// manipulating the router state.
/// </summary>
public enum RouteChangeAction
{
    /// <summary>Add a new active route to the router state.</summary>
    Add = 1,

    /// <summary>Update an existing active route in the router state.</summary>
    Update = 2,

    /// <summary>Delete an active route from the router state.</summary>
    Delete = 3,
}
