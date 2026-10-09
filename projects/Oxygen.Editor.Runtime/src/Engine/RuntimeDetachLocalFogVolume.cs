// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to remove a node's local fog volume.</summary>
/// <param name="NodeId">The node that carries the volume.</param>
public sealed record RuntimeDetachLocalFogVolume(Guid NodeId) : RuntimeWorldCommand;
