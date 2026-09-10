// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>An immutable node-creation request with authored identity.</summary>
/// <param name="Name">The authored name.</param>
/// <param name="NodeId">The authored node identity.</param>
/// <param name="ParentId">The optional authored parent identity.</param>
/// <param name="InitializeWorldAsRoot">Whether initial world transform treats the node as a root.</param>
public sealed record RuntimeCreateNode(string Name, Guid NodeId, Guid? ParentId, bool InitializeWorldAsRoot) : RuntimeWorldCommand;
