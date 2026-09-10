// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.Documents;

/// <summary>A dirty document presented for an explicit save or discard decision.</summary>
/// <param name="metadata">The document metadata.</param>
/// <param name="save">The operation that saves and reports failures.</param>
public sealed class DocumentCloseItem(IDocumentMetadata metadata, Func<Task<bool>> save)
{
    /// <summary>Gets the document metadata.</summary>
    public IDocumentMetadata Metadata { get; } = metadata;

    /// <summary>Gets or sets a value indicating whether to save this document. Unselected documents will be discarded.</summary>
    public bool IsSelected { get; set; } = true;

    /// <summary>Gets a value indicating whether the selected save has succeeded.</summary>
    public bool IsSaved { get; private set; }

    /// <summary>Saves the document, retaining successful saves across retries.</summary>
    /// <returns>True when the document is saved.</returns>
    public async Task<bool> SaveAsync()
    {
        if (!this.IsSaved || this.Metadata.IsDirty)
        {
            this.IsSaved = await save().ConfigureAwait(true);
        }

        return this.IsSaved;
    }
}
