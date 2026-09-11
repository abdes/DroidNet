// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Suspends input capture until the user saves exactly the listed documents.</summary>
/// <param name="documents">The unsaved source owners discovered under their read leases.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "A resumable blocked cook requires its participating document list; message-only constructors cannot represent that state.")]
public sealed class CookInputsNeedSaveException(IEnumerable<CookDocumentState> documents) : Exception("Participating documents have unsaved changes.")
{
    /// <summary>Gets the immutable list presented for explicit save consent.</summary>
    public ImmutableArray<CookDocumentState> Documents { get; } = [.. documents];
}
