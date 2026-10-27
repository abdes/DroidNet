// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     What the module that owns a document kind contributes for its documents' tabs: how the tab looks and which editor
///     shows the document.
/// </summary>
/// <param name="kind">The kind whose documents this descriptor edits.</param>
/// <param name="createEditor">
///     Creates the editor of a document of <paramref name="kind"/> from its tab, in the scope of the tab's window.
/// </param>
public sealed class DocumentEditorDescriptor(IDocumentKind kind, Func<DocumentTab, object> createEditor)
{
    /// <summary>
    ///     Gets the kind whose documents this descriptor edits.
    /// </summary>
    public IDocumentKind Kind { get; } = kind ?? throw new ArgumentNullException(nameof(kind));

    /// <summary>
    ///     Gets a value indicating whether the user can close the tab of a document of this kind.
    /// </summary>
    public bool IsClosable { get; init; } = true;

    /// <summary>
    ///     Gets a value indicating whether the tab of a document of this kind can leave its window: be torn out into a new
    ///     window or moved to another one. A tab that cannot still reorders within its window.
    /// </summary>
    public bool CanTearOut { get; init; } = true;

    /// <summary>
    ///     Gets a value indicating whether the tab of a document of this kind is pinned.
    /// </summary>
    public bool IsPinned { get; init; }

    /// <summary>
    ///     Gets a value indicating whether the pinned state of the tab of a document of this kind is locked: the user can
    ///     neither unpin the tab nor drag it into another window.
    /// </summary>
    public bool IsPinLocked { get; init; }

    /// <summary>
    ///     Gets the URI of the icon the tab of a document of this kind shows, if any.
    /// </summary>
    public Uri? IconUri { get; init; }

    /// <summary>
    ///     Gets a value indicating whether a window shows at most one document of this kind. Opening another one in the
    ///     window replaces it: the shown document closes first, after its unsaved changes are asked about.
    /// </summary>
    public bool IsOnePerArea { get; init; }

    /// <summary>
    ///     Creates the editor of a document shown in a tab.
    /// </summary>
    /// <param name="tab">The tab of the document to edit; the document's kind is <see cref="Kind"/>.</param>
    /// <returns>The editor's view model.</returns>
    /// <exception cref="ArgumentException">The tab's document is not of this descriptor's kind.</exception>
    public object CreateEditor(DocumentTab tab)
    {
        ArgumentNullException.ThrowIfNull(tab);
        return ReferenceEquals(tab.Document.Kind, this.Kind)
            ? createEditor(tab)
            : throw new ArgumentException($"A {tab.Document.Kind.Name} document has no {this.Kind.Name} editor.", nameof(tab));
    }
}
