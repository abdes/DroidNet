// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The view of a document's editor, which has work to finish before the document area shows another editor's view
///     in its place.
/// </summary>
public interface IDocumentEditorView
{
    /// <summary>
    ///     Ends the view's input and releases what it holds on screen, before another editor's view replaces it.
    /// </summary>
    /// <returns>A task that completes when the view can be replaced.</returns>
    public Task DeactivateAsync();
}
