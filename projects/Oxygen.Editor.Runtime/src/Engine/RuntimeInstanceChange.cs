// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A property change of a node an instance brings in.</summary>
/// <param name="Path">The node below the instance root, one node ID per nesting level.</param>
/// <param name="Id">The property changed.</param>
/// <param name="PartId">The surface of a surface property, or <see cref="Guid.Empty"/>.</param>
/// <param name="Value">The value.</param>
public sealed record RuntimeInstanceChange(NodePath Path, RuntimeOverrideId Id, Guid PartId, RuntimeInstanceValue Value);
