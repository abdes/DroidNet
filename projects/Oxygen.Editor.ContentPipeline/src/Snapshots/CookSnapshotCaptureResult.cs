// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Reports a saved snapshot or actionable document blockers before native work.</summary>
/// <param name="Snapshot">The successful immutable input set.</param>
/// <param name="NeedsSave">Participating documents with unsaved changes.</param>
/// <param name="ExternalChanges">Open documents whose saved baseline differs from disk.</param>
public sealed record CookSnapshotCaptureResult(
    CookInputSnapshot? Snapshot,
    ImmutableArray<CookDocumentState> NeedsSave,
    ImmutableArray<CookDocumentState> ExternalChanges);
