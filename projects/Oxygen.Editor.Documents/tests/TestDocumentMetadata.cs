// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>
/// A test implementation of <see cref="BaseDocumentMetadata"/> for unit testing.
/// </summary>
/// <param name="documentId">
///     The unique identifier for the document. If <see langword="null" /> a new <c>Guid</c> will
///     be automatically assigned.
/// </param>
[ExcludeFromCodeCoverage]
public class TestDocumentMetadata(Guid? documentId = null) : BaseDocumentMetadata(documentId)
{
    /// <inheritdoc/>
    public override string DocumentType { get; } = "Test";
}
