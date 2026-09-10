// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to ReparentSceneNodes.</summary>
/// <param name="Children">The Children command value.</param>
/// <param name="Parent">The Parent command value.</param>
/// <param name="PreserveWorldTransform">The PreserveWorldTransform command value.</param>
public sealed record RuntimeReparentSceneNodes(ImmutableArray<Guid> Children, Guid? Parent, bool PreserveWorldTransform) : RuntimeWorldCommand;
