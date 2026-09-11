// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>Resolves external-file conflicts without implicitly redirecting or closing the original document.</summary>
public interface IDocumentConflictParticipant
{
    /// <summary>Gets a value indicating whether the last save encountered an external-file conflict.</summary>
    public bool HasSaveConflict { get; }

    /// <summary>Gets the suggested file-name stem for a distinct authored copy.</summary>
    public string SuggestedCopyName { get; }

    /// <summary>Reloads after the caller explicitly confirms discarding unsaved edits and history.</summary>
    /// <returns>The authoring result and a user-facing status message.</returns>
    public Task<DocumentConflictResult> ReloadFromDiskAsync();

    /// <summary>Creates a distinct authored asset while keeping the original document and references unchanged.</summary>
    /// <param name="name">The new asset's file-name stem.</param>
    /// <returns>The copy result and a user-facing status message.</returns>
    public Task<DocumentConflictResult> SaveCopyAsync(string name);
}
