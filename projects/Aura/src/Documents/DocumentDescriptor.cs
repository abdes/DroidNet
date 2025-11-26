// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace DroidNet.Aura.Documents;

/// <summary>
///     Internal runtime descriptor used by Aura to coordinate an app document model with the
///     TabStrip UI. This type is intentionally internal to Aura and must not be exposed by apps;
///     apps only interact with Aura via <see cref="IDocumentMetadata"/>.
/// </summary>
internal sealed class DocumentDescriptor(Guid documentId)
{
    /// <summary>
    ///     Gets the document identifier. Aura ensures UI <c>TabItem.ContentId</c> equals this value
    ///     when a tab corresponds to a document.
    /// </summary>
    public Guid DocumentId { get; } = documentId;
}
