// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
///     Metadata for a Scene document, used by Aura document tabs.
///     Viewport layout and configuration are only applicable for scene documents.
/// </summary>
public class SceneDocumentMetadata : BaseDocumentMetadata
{
    /// <inheritdoc/>
    public override string DocumentType => "Scene";

    /// <summary>
    ///     Gets or sets the viewport layout and configuration, only applicable for scene documents.
    /// </summary>
    public string? Layout { get; set; }
}
