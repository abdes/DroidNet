// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to remove a node's instance; the node and its owned children stay.</summary>
/// <param name="NodeId">The instance root.</param>
public sealed record RuntimeDetachInstance(NodeId NodeId) : RuntimeWorldCommand;
