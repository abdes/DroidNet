// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable native content facts for one engine lifetime and content revision.</summary>
/// <param name="RunId">The native runtime lifetime.</param>
/// <param name="Revision">The monotonically increasing status revision.</param>
/// <param name="State">The acknowledged content state.</param>
/// <param name="Roots">The complete last acknowledged root set; use only when the state is Mounted.</param>
/// <param name="Reason">An optional explanation of native unavailability.</param>
public sealed record RuntimeContentSnapshot(Guid RunId, long Revision, RuntimeContentState State, ImmutableArray<string> Roots, string? Reason = null);
