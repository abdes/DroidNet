// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when the router completes initialization.
/// </summary>
/// <remarks>
/// This is the first event emitted by the router, indicating it is ready to handle navigation requests.
/// </remarks>
public class RouterReady : RouterEvent;
