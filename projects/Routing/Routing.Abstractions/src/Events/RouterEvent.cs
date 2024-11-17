// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents the base class for all events in the routing system.
/// </summary>
/// <remarks>
/// All router events derive from this class to form a hierarchy that includes <strong>navigation
/// events,</strong> such as URL resolution, route recognition, and activation, as well as
/// <strong>context events</strong>, such as creation, destruction, and changes.
/// </remarks>
public class RouterEvent;
