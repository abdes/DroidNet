// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The correlated outcome and native state for one node observation.</summary>
/// <param name="Outcome">The requested scene activation's observation outcome.</param>
/// <param name="NodeId">The authored node identity requested by the caller.</param>
/// <param name="State">Native values on success; otherwise null.</param>
public sealed record RuntimeNodeObservation(RuntimeCommandResult Outcome, Guid NodeId, RuntimeNodeState? State);
