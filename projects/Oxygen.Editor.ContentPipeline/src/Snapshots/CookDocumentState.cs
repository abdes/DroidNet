// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Immutable document facts captured while its saved file is protected.</summary>
/// <param name="DocumentId">The document identity.</param>
/// <param name="SourcePath">The canonical source file path.</param>
/// <param name="DisplayName">The user-visible document name.</param>
/// <param name="Revision">The current authoring revision.</param>
/// <param name="SavedRevision">The acknowledged saved revision.</param>
/// <param name="IsDirty">Whether the document has unsaved changes.</param>
/// <param name="SavedContentHash">The saved file hash known to the document owner.</param>
public sealed record CookDocumentState(
    Guid DocumentId,
    string SourcePath,
    string DisplayName,
    long Revision,
    long SavedRevision,
    bool IsDirty,
    string SavedContentHash);
