// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to RemoveSceneNodes.</summary>
/// <param name="Nodes">The Nodes command value.</param>
public sealed record RuntimeRemoveSceneNodes(ImmutableArray<Guid> Nodes) : RuntimeWorldCommand;
