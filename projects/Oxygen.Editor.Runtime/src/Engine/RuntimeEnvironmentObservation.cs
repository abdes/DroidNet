// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The correlated outcome and native environment state for one observation.</summary>
/// <param name="Outcome">The result of observing the requested scene activation.</param>
/// <param name="State">Native values when the observation succeeded; otherwise null.</param>
public sealed record RuntimeEnvironmentObservation(RuntimeCommandResult Outcome, RuntimeEnvironmentState? State);
