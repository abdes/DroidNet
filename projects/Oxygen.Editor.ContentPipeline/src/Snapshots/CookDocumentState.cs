// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>What an open document's owner reports to the cook: whether its file has unsaved edits, and the saved file's identity.</summary>
/// <param name="SourcePath">The canonical source file path.</param>
/// <param name="DisplayName">The user-visible document name.</param>
/// <param name="IsDirty">Whether the document has unsaved changes or an edit in progress.</param>
/// <param name="SavedContentHash">The saved file hash known to the document owner.</param>
public sealed record CookDocumentState(
    string SourcePath,
    string DisplayName,
    bool IsDirty,
    string SavedContentHash);
