// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to SetProperties.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="Entries">The Entries command value.</param>
public sealed record RuntimeSetProperties(Guid NodeId, ImmutableArray<RuntimePropertyValue> Entries) : RuntimeWorldCommand;
