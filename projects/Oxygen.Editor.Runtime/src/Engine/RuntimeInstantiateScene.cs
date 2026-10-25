// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Immutable runtime request to instance a cooked scene under a node, with the scene's changes; it replaces an instance
/// the node has.
/// </summary>
/// <remarks>
/// The engine keeps the request: mounting content again builds the instance again, and every later outcome of the
/// instance, its changes included, is reported against this request.
/// </remarks>
/// <param name="NodeId">The node that becomes the instance root.</param>
/// <param name="SourcePath">The engine path of the cooked scene instanced.</param>
/// <param name="Changes">The changes, at most one per node path, property and part.</param>
public sealed record RuntimeInstantiateScene(NodeId NodeId, string SourcePath, ImmutableArray<RuntimeInstanceChange> Changes) : RuntimeWorldCommand;
