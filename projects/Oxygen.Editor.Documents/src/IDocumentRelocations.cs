// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>
/// The relocations of the project's assets as open documents see them: files that moved or whose references were
/// rewritten, and files that were deleted. The module that relocates assets implements it.
/// </summary>
public interface IDocumentRelocations
{
    /// <summary>Raised after a change in the editor deleted files.</summary>
    public event EventHandler<DocumentFilesDeletedEventArgs>? FilesDeleted;

    /// <summary>
    /// Follows every relocation: <paramref name="follow"/> runs once for each committed relocation, before the
    /// relocation completes, with what each open document follows.
    /// </summary>
    /// <param name="follow">
    /// Follows a relocation. Its argument maps an open document to what it follows, or to <see langword="null"/> when
    /// the relocation neither moved nor rewrote the document's file. A failure fails the relocation.
    /// </param>
    /// <returns>Stops following when disposed.</returns>
    public IDisposable Follow(Func<Func<Document, DocumentRelocation?>, Task> follow);
}
