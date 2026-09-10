// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to RenameSceneNode.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="NewName">The NewName command value.</param>
public sealed record RuntimeRenameSceneNode(Guid NodeId, string NewName) : RuntimeWorldCommand;
