// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to ReparentSceneNode.</summary>
/// <param name="Child">The Child command value.</param>
/// <param name="Parent">The Parent command value.</param>
/// <param name="PreserveWorldTransform">The PreserveWorldTransform command value.</param>
public sealed record RuntimeReparentSceneNode(Guid Child, Guid? Parent, bool PreserveWorldTransform) : RuntimeWorldCommand;
