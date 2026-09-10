// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A target-validated native background observation.</summary>
/// <param name="Outcome">The observation outcome, including operation and scene identity.</param>
/// <param name="State">The native state, present only when observation succeeded.</param>
public sealed record RuntimeBackgroundObservation(RuntimeCommandResult Outcome, RuntimeBackgroundState? State);
