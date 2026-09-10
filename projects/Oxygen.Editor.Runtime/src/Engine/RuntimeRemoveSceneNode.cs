// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to RemoveSceneNode.</summary>
/// <param name="NodeId">The NodeId command value.</param>
public sealed record RuntimeRemoveSceneNode(Guid NodeId) : RuntimeWorldCommand;
