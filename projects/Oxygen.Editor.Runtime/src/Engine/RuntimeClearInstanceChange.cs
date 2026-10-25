// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to clear one change of a node's instance: the node takes its source's value.</summary>
/// <param name="NodeId">The instance root.</param>
/// <param name="Path">The node below the instance root.</param>
/// <param name="Id">The property.</param>
/// <param name="PartId">The surface of a surface property, or <see cref="Guid.Empty"/>.</param>
public sealed record RuntimeClearInstanceChange(NodeId NodeId, NodePath Path, RuntimeOverrideId Id, Guid PartId) : RuntimeWorldCommand;
