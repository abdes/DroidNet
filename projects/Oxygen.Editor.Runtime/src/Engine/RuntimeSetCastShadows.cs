// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to SetCastShadows.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="CastsShadows">The CastsShadows command value.</param>
public sealed record RuntimeSetCastShadows(Guid NodeId, bool CastsShadows) : RuntimeWorldCommand;
