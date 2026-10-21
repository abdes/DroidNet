// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents;

/// <summary>
/// The open documents with unsaved changes: a document is unsaved while it is dirty or has an edit in progress. An
/// operation that reads saved files, such as a cook or a relocation, asks which of its files are unsaved before it runs.
/// The Shell's document service implements it.
/// </summary>
/// <remarks>It can be asked from any thread.</remarks>
public interface IUnsavedDocuments
{
    /// <summary>Raised when a document becomes unsaved or saved, follows its file, or closes.</summary>
    /// <remarks>It is raised on the thread that edits the documents.</remarks>
    public event EventHandler? Changed;

    /// <summary>Finds the unsaved documents that edit some files.</summary>
    /// <param name="paths">The files, or folders whose files are all included.</param>
    /// <returns>The unsaved documents whose file is one of the paths or inside one of them, sorted by title.</returns>
    public IReadOnlyList<UnsavedDocument> Find(IEnumerable<FileSystemPath> paths);
}
