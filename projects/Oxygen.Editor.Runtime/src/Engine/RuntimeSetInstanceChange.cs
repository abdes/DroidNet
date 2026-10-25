// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to set one change of a node's instance.</summary>
/// <param name="NodeId">The instance root.</param>
/// <param name="Change">The change; it replaces the change of the same node path, property and part.</param>
public sealed record RuntimeSetInstanceChange(NodeId NodeId, RuntimeInstanceChange Change) : RuntimeWorldCommand;
