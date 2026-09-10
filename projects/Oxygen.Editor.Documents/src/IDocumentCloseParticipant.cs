// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>
/// Provides authoring operations for an editor document during an approved close workflow.
/// </summary>
public interface IDocumentCloseParticipant
{
    /// <summary>Prepares the authoring owner and awaits pending operations needed for a close decision.</summary>
    /// <returns>The preparation task.</returns>
    public Task PrepareForCloseAsync();

    /// <summary>Saves authoring state and reports whether it was persisted successfully.</summary>
    /// <returns>True only when the document was saved.</returns>
    public Task<bool> SaveForCloseAsync();

    /// <summary>Releases the document after all close decisions and saves have succeeded.</summary>
    /// <param name="discard">Whether the user authorized discarding unsaved changes.</param>
    /// <returns>The close task.</returns>
    public Task CloseAsync(bool discard);

    /// <summary>Re-enables authoring when close preparation ends.</summary>
    public void ResumeEditing();
}
