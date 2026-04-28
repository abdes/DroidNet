// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Metadata for a material editor document tab.
/// </summary>
/// <param name="materialUri">The material source asset URI.</param>
/// <param name="documentId">The optional editor document identity.</param>
public sealed class MaterialDocumentMetadata(Uri materialUri, Guid? documentId = null) : BaseDocumentMetadata(documentId)
{
    /// <summary>
    /// Gets the material source asset URI.
    /// </summary>
    public Uri MaterialUri { get; } = materialUri;

    /// <inheritdoc />
    public override string DocumentType => "Material";
}
