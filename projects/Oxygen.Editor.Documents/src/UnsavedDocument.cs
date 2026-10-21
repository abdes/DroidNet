// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.Documents;

/// <summary>An open document with unsaved changes, as <see cref="IUnsavedDocuments"/> reports it.</summary>
/// <param name="Id">The document's identity.</param>
/// <param name="File">The file it edits.</param>
/// <param name="Title">The document's name.</param>
public sealed record UnsavedDocument(DocumentId Id, DocumentFile File, string Title);
